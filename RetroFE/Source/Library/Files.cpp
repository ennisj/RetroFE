/* This file is part of RetroFE.
 *
 * RetroFE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RetroFE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RetroFE.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "Files.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{

bool readFile(const std::string& path, std::string& content)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& content, std::string& error)
{
    std::error_code ec;
    fs::path target(path);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
    }
    std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Could not write " + path;
            return false;
        }
        out << content;
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temp, ec);
            error = "Could not write " + path;
            return false;
        }
    }
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "Could not replace " + path;
        return false;
    }
    return true;
}

bool copyFileAtomic(const std::string& from, const std::string& to, std::string& error)
{
    std::error_code ec;
    fs::path target(to);
    fs::create_directories(target.parent_path(), ec);
    fs::path temp = target.parent_path() / ("." + target.filename().string() + ".tmp");
    fs::remove(temp, ec);
    if (!fs::copy_file(from, temp, fs::copy_options::overwrite_existing, ec) || ec) {
        fs::remove(temp, ec);
        error = "Could not copy " + from + " to " + to;
        return false;
    }
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "Could not replace " + to;
        return false;
    }
    return true;
}

const char* defaultNewline()
{
#ifdef WIN32
    return "\r\n";
#else
    return "\n";
#endif
}

bool narrowName(const fs::path& path, std::string& name)
{
    try {
        name = path.string();
        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}

std::string narrow(const fs::path& path)
{
    std::string name;
    if (!narrowName(path, name)) {
        try {
            return path.u8string();
        }
        catch (const std::exception&) {
            return "";
        }
    }
    return name;
}

std::string toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool iequals(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

bool icontains(const std::string& haystackLower, const std::string& needleLower)
{
    return needleLower.empty() || haystackLower.find(needleLower) != std::string::npos;
}

std::string normalPath(const std::string& path)
{
    if (path.empty()) {
        return path;
    }
    fs::path p = fs::path(path).lexically_normal();
    std::string result = narrow(p.make_preferred());
    // "a/b/" normalises to "a/b/"; drop the trailing separator unless it's a root
    while (result.size() > 1 && (result.back() == '/' || result.back() == '\\')
           && !(result.size() == 3 && result[1] == ':')) {
        result.pop_back();
    }
    return result;
}

bool isInside(const std::string& path, const std::string& folder)
{
    fs::path relative = fs::path(normalPath(path)).lexically_relative(fs::path(normalPath(folder)));
    if (relative.empty()) {
        return false;
    }
    return relative.native().empty() || *relative.begin() != "..";
}

Lines Lines::parse(const std::string& content)
{
    Lines result;
    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\n', start);
        Line line;
        if (end == std::string::npos) {
            line.text = content.substr(start);
            start = content.size();
        }
        else {
            size_t textEnd = end;
            if (textEnd > start && content[textEnd - 1] == '\r') {
                textEnd--;
                line.eol = "\r\n";
            }
            else {
                line.eol = "\n";
            }
            line.text = content.substr(start, textEnd - start);
            start = end + 1;
        }
        result.lines.push_back(std::move(line));
    }
    return result;
}

std::string Lines::join() const
{
    std::string result;
    for (const Line& line : lines) {
        result += line.text;
        result += line.eol;
    }
    return result;
}

std::string Lines::newline() const
{
    bool lf = false;
    for (const Line& line : lines) {
        if (line.eol == "\r\n") {
            return "\r\n";
        }
        if (line.eol == "\n") {
            lf = true;
        }
    }
    return lf ? "\n" : fallbackNewline;
}

void Lines::append(const std::string& text)
{
    insert(lines.size(), text);
}

void Lines::insert(size_t index, const std::string& text)
{
    std::string eol = newline();
    if (index >= lines.size()) {
        if (!lines.empty() && lines.back().eol.empty()) {
            lines.back().eol = eol;
        }
        lines.push_back(Line{ text, eol });
        return;
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(index), Line{ text, eol });
}

void Lines::erase(size_t index)
{
    if (index >= lines.size()) {
        return;
    }
    // Removing the last line of a file that doesn't end in a line break: the
    // new last line keeps its break, which is the safer shape for appends.
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(index));
}

}

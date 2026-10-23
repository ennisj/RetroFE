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
#include "SettingsFile.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

SettingsFile::SettingsFile(const std::string& root)
    : root_(root)
{
    load();
}

void SettingsFile::load()
{
    // Same order as ImportConfiguration in Main.cpp
    std::vector<std::string> names = { "settings.conf" };
    for (int i = 1; i < 16; i++) {
        names.push_back("settings" + std::to_string(i) + ".conf");
    }
    names.push_back("settings_saved.conf");

    files_.clear();
    keys_.clear();
    for (const std::string& name : names) {
        File file;
        file.name = name;
        file.path = Utils::combinePath(root_, name);
        file.exists = fs::exists(file.path);
        if (file.exists) {
            std::ifstream in(file.path, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            file.crlf = content.find("\r\n") != std::string::npos;

            std::istringstream lines(content);
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                file.lines.push_back(line);
            }
        }

        // Only settings.conf is kept when missing, as the place new keys go
        if (file.exists || files_.empty()) {
            files_.push_back(std::move(file));
            const File& added = files_.back();
            for (size_t i = 0; i < added.lines.size(); i++) {
                std::string key;
                if (parseKey(added.lines[i], key)) {
                    keys_[key] = Location{ files_.size() - 1, i };
                }
            }
        }
    }
}

bool SettingsFile::has(const std::string& key) const
{
    return keys_.count(key) > 0;
}

std::string SettingsFile::get(const std::string& key) const
{
    auto it = keys_.find(key);
    if (it == keys_.end()) {
        return "";
    }
    return valueOf(files_[it->second.file].lines[it->second.line]);
}

std::string SettingsFile::source(const std::string& key) const
{
    auto it = keys_.find(key);
    return it == keys_.end() ? "" : files_[it->second.file].name;
}

bool SettingsFile::ensureExists(const std::string& initialContent, std::string& error)
{
    File& settings = files_[0];
    if (settings.exists) {
        return true;
    }

    std::istringstream lines(initialContent);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        settings.lines.push_back(line);
    }
#ifdef WIN32
    settings.crlf = true;
#endif

    // Later files still win for keys they set
    for (size_t i = 0; i < settings.lines.size(); i++) {
        std::string key;
        if (parseKey(settings.lines[i], key) && !keys_.count(key)) {
            keys_[key] = Location{ 0, i };
        }
    }
    return save(settings, error);
}

bool SettingsFile::set(const std::string& key, const std::string& value, const std::string& initialContent, std::string& error)
{
    if (!keys_.count(key) && !ensureExists(initialContent, error)) {
        return false;
    }

    auto it = keys_.find(key);
    if (it != keys_.end()) {
        // Replace just the value, keeping the spacing and any comment
        File& file = files_[it->second.file];
        std::string& line = file.lines[it->second.line];
        size_t equals = line.find('=');
        size_t valueStart = line.find_first_not_of(" \t", equals + 1);
        size_t comment = line.find('#', equals + 1);
        std::string spacing = valueStart == std::string::npos || (comment != std::string::npos && valueStart > comment)
            ? " " : line.substr(equals + 1, valueStart - equals - 1);
        std::string trailing;
        if (comment != std::string::npos) {
            // Keep the whitespace in front of the comment too
            size_t beforeComment = line.find_last_not_of(" \t", comment - 1);
            trailing = line.substr(beforeComment == std::string::npos || beforeComment < equals ? comment : beforeComment + 1);
        }
        line = line.substr(0, equals + 1) + spacing + value + trailing;
        return save(file, error);
    }

    File& settings = files_[0];
    settings.lines.push_back(key + " = " + value);
    keys_[key] = Location{ 0, settings.lines.size() - 1 };
    return save(settings, error);
}

bool SettingsFile::parseKey(const std::string& line, std::string& key)
{
    std::string content = Utils::filterComments(line);
    size_t equals = content.find('=');
    if (equals == std::string::npos) {
        return false;
    }
    key = Utils::trimEnds(content.substr(0, equals));
    return !key.empty();
}

std::string SettingsFile::valueOf(const std::string& line)
{
    std::string content = Utils::filterComments(line);
    size_t equals = content.find('=');
    return equals == std::string::npos ? "" : Utils::trimEnds(content.substr(equals + 1));
}

bool SettingsFile::save(File& file, std::string& error)
{
    // Write next to the file, then swap it in, so a failed write can't leave half a file
    std::string temp = file.path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Could not write " + file.path;
            return false;
        }
        const char* newline = file.crlf ? "\r\n" : "\n";
        for (const std::string& line : file.lines) {
            out << line << newline;
        }
        if (!out) {
            error = "Could not write " + file.path;
            return false;
        }
    }

    std::error_code ec;
    fs::rename(temp, file.path, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "Could not replace " + file.path;
        return false;
    }
    file.exists = true;
    LOG_INFO("Settings", "Saved " + file.path);
    return true;
}

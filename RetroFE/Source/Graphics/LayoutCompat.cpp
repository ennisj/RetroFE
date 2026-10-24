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
#include "LayoutCompat.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    void say(std::string* why, const std::string& text)
    {
        if (why) *why = text;
    }

    bool readText(const fs::path& path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }

    // At s[i] == '[': the level of a Lua long bracket ("[[" 0, "[==[" 2), or -1
    int longBracket(const std::string& s, size_t i)
    {
        if (i >= s.size() || s[i] != '[') return -1;
        size_t j = i + 1;
        while (j < s.size() && s[j] == '=') j++;
        return j < s.size() && s[j] == '[' ? static_cast<int>(j - i - 1) : -1;
    }

    // Past the "]=*]" closing a long bracket of this level that opens at i
    size_t longBracketEnd(const std::string& s, size_t i, int level)
    {
        const std::string close = "]" + std::string(static_cast<size_t>(level), '=') + "]";
        size_t end = s.find(close, i + static_cast<size_t>(level) + 2);
        return end == std::string::npos ? s.size() : end + close.size();
    }

    // Lua without its comments ("-- ...", "--[[ ... ]]", "--[==[ ... ]==]"),
    // and the same text with every string literal's contents blanked ('x'), so
    // keywords and brackets are only looked for in code. Same length and layout.
    void lex(const std::string& s, std::string& code, std::string& masked)
    {
        code.clear();
        masked.clear();
        size_t i = 0;
        auto literal = [&](size_t from, size_t to, size_t open, size_t close) {
            code.append(s, from, to - from);
            masked.append(s, from, open);
            masked.append(to - from - open - close, 'x');
            masked.append(s, to - close, close);
        };
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"' || c == '\'') {
                size_t j = i + 1;
                while (j < s.size() && s[j] != c && s[j] != '\n') j += (s[j] == '\\') ? 2 : 1;
                const size_t end = std::min(j + 1, s.size());
                const bool closed = j < s.size() && s[j] == c;
                literal(i, end, 1, closed ? 1 : 0);
                i = end;
                continue;
            }
            if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
                const int level = longBracket(s, i + 2);
                if (level >= 0) {
                    i = longBracketEnd(s, i + 2, level);
                }
                else {
                    size_t end = s.find('\n', i);
                    i = end == std::string::npos ? s.size() : end;
                }
                continue;
            }
            const int level = longBracket(s, i);
            if (level >= 0) {
                const size_t end = longBracketEnd(s, i, level);
                const size_t open = static_cast<size_t>(level) + 2;
                const size_t close = end - i >= 2 * open ? open : 0;
                literal(i, end, std::min(open, end - i), close);
                i = end;
                continue;
            }
            code += c;
            masked += c;
            i++;
        }
    }

    std::string escapeRegex(const std::string& s)
    {
        static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
        return std::regex_replace(s, special, R"(\$&)");
    }

    // The parameters and body of "function <name>(...)" up to its first "end".
    // A body that opens a block of its own (if, for, while, a nested function)
    // isn't read. Keywords are only looked for outside strings (masked).
    bool functionBody(const std::string& code, const std::string& masked, const std::string& name,
                      std::vector<std::string>& params, std::string& body, std::string* why)
    {
        std::smatch m;
        if (!std::regex_search(masked, m, std::regex("\\bfunction\\s+" + escapeRegex(name) + "\\s*\\(([^)]*)\\)"))) {
            say(why, "it has no function " + name + "()");
            return false;
        }
        params.clear();
        std::string list = m[1].str();
        static const std::regex ident(R"([A-Za-z_][A-Za-z0-9_]*)");
        for (std::sregex_iterator it(list.begin(), list.end(), ident), end; it != end; ++it) params.push_back(it->str());

        const size_t start = static_cast<size_t>(m.position(0) + m.length(0));
        const std::string rest = masked.substr(start);
        std::smatch e;
        if (!std::regex_search(rest, e, std::regex(R"(\bend\b)"))) {
            say(why, name + "() has no end");
            return false;
        }
        const size_t length = static_cast<size_t>(e.position(0));
        if (std::regex_search(rest.substr(0, length), std::regex(R"(\b(if|for|while|repeat|function|do)\b)"))) {
            say(why, name + "() does more than build a page (RetroFE doesn't run Lua)");
            return false;
        }
        body = code.substr(start, length);
        return true;
    }

    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n;");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n;");
        return s.substr(a, b - a + 1);
    }

    // The lua file's folder joined with a path written after layoutPath:
    // "/collections/main.xml", "collections/main.xml" and "/../../x.xml" all
    // name a path relative to that folder
    std::string underFolder(const std::string& folder, std::string rel)
    {
        while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) rel.erase(rel.begin());
        return (fs::path(folder) / fs::path(rel)).lexically_normal().string();
    }

    // The numbers in a transform: decimal, with an optional exponent. False if
    // one doesn't fit a double or isn't finite.
    bool numbers(const std::string& text, std::vector<double>& out)
    {
        static const std::regex number(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)");
        for (std::sregex_iterator it(text.begin(), text.end(), number), end; it != end; ++it) {
            const std::string token = it->str();
            errno = 0;
            char* stop = nullptr;
            const double value = std::strtod(token.c_str(), &stop);
            if (errno == ERANGE || !stop || *stop != '\0' || !std::isfinite(value)) return false;
            out.push_back(value);
        }
        return true;
    }
}

namespace LayoutCompat
{

namespace
{
std::string readBuildTarget(const std::string& luaFile, const std::string& themeFolder, std::string* why)
{
    std::string raw;
    if (!readText(luaFile, raw)) {
        say(why, "can't read it");
        return "";
    }
    std::string text;
    std::string masked;
    lex(raw, text, masked);
    const std::string luaDir = fs::path(luaFile).parent_path().string();

    std::vector<std::string> params;
    std::string body;
    if (!functionBody(text, masked, "build", params, body, why)) return "";
    if (params.empty()) {
        say(why, "build() takes no page builder");
        return "";
    }
    const std::string pb = escapeRegex(params[0]);
    const std::string call = trim(body);

    // pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. "<path>")
    std::smatch m;
    const std::regex direct("^" + pb + ":buildFromXmlFile\\(\\s*" + pb + "\\.layoutPath\\s*\\.\\.\\s*([\"'])([^\"']*)\\1\\s*\\)$");
    if (std::regex_match(call, m, direct)) {
        return underFolder(luaDir, m[2].str());
    }

    // module.function(pageBuilder, "<arg>"), module = require("<name>")
    const std::regex viaModule("^([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\(\\s*" + pb +
                               "\\s*,\\s*([\"'])([^\"']*)\\3\\s*\\)$");
    if (!std::regex_match(call, m, viaModule)) {
        say(why, "build() doesn't just build one XML file");
        return "";
    }
    const std::string moduleVar = m[1].str();
    const std::string function = m[2].str();
    const std::string argument = m[4].str();

    std::smatch r;
    if (!std::regex_search(text, r, std::regex("\\blocal\\s+" + escapeRegex(moduleVar) +
                                                "\\s*=\\s*require\\s*\\(?\\s*([\"'])([^\"']+)\\1\\s*\\)?"))) {
        say(why, moduleVar + " isn't a require()d module");
        return "";
    }
    const std::string moduleName = r[2].str();
    std::string moduleRaw;
    bool found = false;
    for (const fs::path& candidate : { fs::path(themeFolder) / "lua" / (moduleName + ".lua"),
                                       fs::path(luaDir) / (moduleName + ".lua") }) {
        if (readText(candidate, moduleRaw)) {
            found = true;
            break;
        }
    }
    if (!found) {
        say(why, "its module " + moduleName + ".lua isn't in the theme's lua folder");
        return "";
    }
    std::string moduleText;
    std::string moduleMasked;
    lex(moduleRaw, moduleText, moduleMasked);

    // function M.<function>(pageBuilder, xmlFile)
    //   pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. "<prefix>" .. xmlFile)
    std::smatch f;
    if (!std::regex_search(moduleMasked, f, std::regex("\\bfunction\\s+([A-Za-z_][A-Za-z0-9_]*)\\." + escapeRegex(function) + "\\s*\\("))) {
        say(why, moduleName + ".lua has no function " + function + "()");
        return "";
    }
    std::vector<std::string> moduleParams;
    std::string moduleBody;
    if (!functionBody(moduleText, moduleMasked, f[1].str() + "." + function, moduleParams, moduleBody, why)) return "";
    if (moduleParams.size() != 2) {
        say(why, moduleName + "." + function + "() doesn't take (pageBuilder, file)");
        return "";
    }
    const std::string mpb = escapeRegex(moduleParams[0]);
    const std::string arg = escapeRegex(moduleParams[1]);
    const std::regex appended("^" + mpb + ":buildFromXmlFile\\(\\s*" + mpb + "\\.layoutPath\\s*\\.\\.\\s*(?:([\"'])([^\"']*)\\1\\s*\\.\\.\\s*)?" +
                              arg + "\\s*\\)$");
    std::smatch b;
    const std::string moduleCall = trim(moduleBody);
    if (!std::regex_match(moduleCall, b, appended)) {
        say(why, moduleName + "." + function + "() doesn't just build the XML file it's given");
        return "";
    }
    return underFolder(luaDir, b[2].str() + argument);
}
}

std::string luaBuildTarget(const std::string& luaFile, const std::string& themeFolder, std::string* why)
{
    try {
        return readBuildTarget(luaFile, themeFolder, why);
    }
    catch (const std::exception& e) {
        say(why, e.what());
        return "";
    }
}

namespace
{
bool readTransform(const std::string& value, QuadTransform& out, std::string* why)
{
    std::string v = trim(value);
    std::string lower;
    for (char c : v) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.rfind("quad", 0) == 0) {
        say(why, "the \"quad (x, y, u, v)\" form isn't supported");
        return false;
    }
    if (lower.find("inf") != std::string::npos || lower.find("nan") != std::string::npos) {
        say(why, "a number isn't finite");
        return false;
    }
    std::vector<double> n;
    if (!numbers(v, n)) {
        say(why, "a number is out of range");
        return false;
    }
    if (n.size() != 12 && n.size() != 13) {
        say(why, "expected a rectangle and four corners");
        return false;
    }
    if (n[2] <= 0 || n[3] <= 0) {
        say(why, "the rectangle has no size");
        return false;
    }
    for (double x : n) {
        if (std::fabs(x) > maxCoordinate) {
            say(why, "a number is out of range");
            return false;
        }
    }

    // Unit square to quad: (0,0) top-left, (1,0) top-right, (1,1) bottom-right, (0,1) bottom-left
    const double x0 = n[4], y0 = n[5];    // top-left
    const double x1 = n[6], y1 = n[7];    // top-right
    const double x3 = n[8], y3 = n[9];    // bottom-left
    const double x2 = n[10], y2 = n[11];  // bottom-right
    const double dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
    const double dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
    double g = 0, h = 0;
    const double den = dx1 * dy2 - dx2 * dy1;
    if (std::fabs(den) < 1e-9) {
        say(why, "the corners don't make a quad");
        return false;
    }
    if (std::fabs(dx3) > 1e-9 || std::fabs(dy3) > 1e-9) {
        g = (dx3 * dy2 - dx2 * dy3) / den;
        h = (dx1 * dy3 - dx3 * dy1) / den;
    }
    // w at the four corners: positive for a convex quad given in this order;
    // anything else (crossed, mis-ordered or concave corners) folds the picture
    constexpr double eps = 1e-6;
    if (1 + g <= eps || 1 + h <= eps || 1 + g + h <= eps) {
        say(why, "the corners aren't a convex quad in the order top-left, top-right, bottom-left, bottom-right");
        return false;
    }
    double coefficients[9] = { x1 - x0 + g * x1, x3 - x0 + h * x3, x0, y1 - y0 + g * y1, y3 - y0 + h * y3, y0, g, h, 1 };
    for (int i = 0; i < 9; ++i) {
        if (!std::isfinite(coefficients[i])) {
            say(why, "the corners don't make a quad");
            return false;
        }
        out.h[i] = coefficients[i];
    }
    // The corners must also turn the same way round (a convex quad, not a mirror image of one folded over)
    const double cross = (x1 - x0) * (y3 - y0) - (y1 - y0) * (x3 - x0);
    const double cross2 = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1);
    if (cross == 0 || cross2 == 0 || (cross > 0) != (cross2 > 0)) {
        say(why, "the corners aren't a convex quad in the order top-left, top-right, bottom-left, bottom-right");
        return false;
    }
    for (int i = 0; i < 4; ++i) out.src[i] = static_cast<float>(n[i]);
    for (int i = 0; i < 8; ++i) out.dst[i] = static_cast<float>(n[4 + i]);
    return true;
}
}

bool parseTransform(const std::string& value, QuadTransform& out, std::string* why)
{
    try {
        QuadTransform parsed;
        if (!readTransform(value, parsed, why)) return false;
        out = parsed;
        return true;
    }
    catch (const std::exception& e) {
        say(why, e.what());
        return false;
    }
}

bool QuadTransform::apply(float x, float y, float& outX, float& outY) const
{
    const double u = (x - src[0]) / src[2];
    const double v = (y - src[1]) / src[3];
    const double w = h[6] * u + h[7] * v + h[8];
    if (!(w > 1e-6)) {
        outX = outY = 0;
        return false;
    }
    const double X = (h[0] * u + h[1] * v + h[2]) / w;
    const double Y = (h[3] * u + h[4] * v + h[5]) / w;
    outX = static_cast<float>(std::clamp(X, -static_cast<double>(maxCoordinate), static_cast<double>(maxCoordinate)));
    outY = static_cast<float>(std::clamp(Y, -static_cast<double>(maxCoordinate), static_cast<double>(maxCoordinate)));
    return std::isfinite(X) && std::isfinite(Y);
}

std::string preferredFolder(const std::string& layoutFolder, const std::string& normalFolder,
                            const std::string& basename, const std::vector<std::string>& extensions)
{
    std::string found;
    if (!layoutFolder.empty() && Utils::findMatchingFile(Utils::combinePath(layoutFolder, basename), extensions, found)) {
        return layoutFolder;
    }
    return normalFolder;
}

std::string themeFile(const std::string& pageFolder, const std::string& themeFolder, const std::string& src)
{
    std::error_code ec;
    const fs::path rel(src);
    for (const std::string& folder : { pageFolder, themeFolder }) {
        if (folder.empty() && !rel.is_absolute()) continue;
        const fs::path candidate = (rel.is_absolute() ? rel : fs::path(folder) / rel).lexically_normal();
        if (fs::is_regular_file(candidate, ec)) return candidate.string();
    }
    return "";
}

}

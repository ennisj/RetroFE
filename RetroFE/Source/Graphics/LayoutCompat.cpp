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
#include <cmath>
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

    // Lua without its comments ("-- ..." and "--[[ ... ]]"); strings are kept
    std::string stripComments(const std::string& s)
    {
        std::string out;
        size_t i = 0;
        while (i < s.size()) {
            char c = s[i];
            if (c == '"' || c == '\'') {
                size_t j = i + 1;
                while (j < s.size() && s[j] != c && s[j] != '\n') j += (s[j] == '\\') ? 2 : 1;
                out.append(s, i, std::min(j + 1, s.size()) - i);
                i = j + 1;
                continue;
            }
            if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
                if (s.compare(i + 2, 2, "[[") == 0) {
                    size_t end = s.find("]]", i + 4);
                    i = end == std::string::npos ? s.size() : end + 2;
                }
                else {
                    size_t end = s.find('\n', i);
                    i = end == std::string::npos ? s.size() : end;
                }
                continue;
            }
            out += c;
            i++;
        }
        return out;
    }

    std::string escapeRegex(const std::string& s)
    {
        static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
        return std::regex_replace(s, special, R"(\$&)");
    }

    // The parameters and body of "function <name>(...)" up to its first "end".
    // A body that opens a block of its own (if, for, while, a nested function)
    // isn't read.
    bool functionBody(const std::string& text, const std::string& name, std::vector<std::string>& params,
                      std::string& body, std::string* why)
    {
        std::smatch m;
        if (!std::regex_search(text, m, std::regex("\\bfunction\\s+" + escapeRegex(name) + "\\s*\\(([^)]*)\\)"))) {
            say(why, "it has no function " + name + "()");
            return false;
        }
        params.clear();
        std::string list = m[1].str();
        static const std::regex ident(R"([A-Za-z_][A-Za-z0-9_]*)");
        for (std::sregex_iterator it(list.begin(), list.end(), ident), end; it != end; ++it) params.push_back(it->str());

        const size_t start = static_cast<size_t>(m.position(0) + m.length(0));
        std::string rest = text.substr(start);
        std::smatch e;
        if (!std::regex_search(rest, e, std::regex(R"(\bend\b)"))) {
            say(why, name + "() has no end");
            return false;
        }
        body = rest.substr(0, static_cast<size_t>(e.position(0)));
        if (std::regex_search(body, std::regex(R"(\b(if|for|while|repeat|function|do)\b)"))) {
            say(why, name + "() does more than build a page (RetroFE doesn't run Lua)");
            return false;
        }
        return true;
    }

    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n;");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n;");
        return s.substr(a, b - a + 1);
    }

    std::string normal(const std::string& path)
    {
        return fs::path(path).lexically_normal().string();
    }
}

namespace LayoutCompat
{

std::string luaBuildTarget(const std::string& luaFile, const std::string& themeFolder, std::string* why)
{
    std::string text;
    if (!readText(luaFile, text)) {
        say(why, "can't read it");
        return "";
    }
    text = stripComments(text);
    const std::string luaDir = fs::path(luaFile).parent_path().string();

    std::vector<std::string> params;
    std::string body;
    if (!functionBody(text, "build", params, body, why)) return "";
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
        return normal(luaDir + m[2].str());
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
    std::string moduleText;
    std::string modulePath;
    for (const fs::path& candidate : { fs::path(themeFolder) / "lua" / (moduleName + ".lua"),
                                       fs::path(luaDir) / (moduleName + ".lua") }) {
        if (readText(candidate, moduleText)) {
            modulePath = candidate.string();
            break;
        }
    }
    if (modulePath.empty()) {
        say(why, "its module " + moduleName + ".lua isn't in the theme's lua folder");
        return "";
    }
    moduleText = stripComments(moduleText);

    // function M.<function>(pageBuilder, xmlFile)
    //   pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. "<prefix>" .. xmlFile)
    std::smatch f;
    if (!std::regex_search(moduleText, f, std::regex("\\bfunction\\s+([A-Za-z_][A-Za-z0-9_]*)\\." + escapeRegex(function) + "\\s*\\("))) {
        say(why, moduleName + ".lua has no function " + function + "()");
        return "";
    }
    std::vector<std::string> moduleParams;
    std::string moduleBody;
    if (!functionBody(moduleText, f[1].str() + "." + function, moduleParams, moduleBody, why)) return "";
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
    return normal(luaDir + b[2].str() + argument);
}

bool parseTransform(const std::string& value, QuadTransform& out, std::string* why)
{
    std::string v = trim(value);
    std::string lower;
    for (char c : v) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.rfind("quad", 0) == 0) {
        say(why, "the \"quad (x, y, u, v)\" form isn't supported");
        return false;
    }
    static const std::regex number(R"([-+]?(?:\d+\.?\d*|\.\d+))");
    std::vector<double> n;
    for (std::sregex_iterator it(v.begin(), v.end(), number), end; it != end; ++it) n.push_back(std::stod(it->str()));
    if (n.size() != 12 && n.size() != 13) {
        say(why, "expected a rectangle and four corners");
        return false;
    }
    if (n[2] <= 0 || n[3] <= 0) {
        say(why, "the rectangle has no size");
        return false;
    }
    for (int i = 0; i < 4; ++i) out.src[i] = static_cast<float>(n[i]);
    for (int i = 0; i < 8; ++i) out.dst[i] = static_cast<float>(n[4 + i]);

    // Unit square to quad: (0,0) top-left, (1,0) top-right, (1,1) bottom-right, (0,1) bottom-left
    const double x0 = n[4], y0 = n[5];    // top-left
    const double x1 = n[6], y1 = n[7];    // top-right
    const double x3 = n[8], y3 = n[9];    // bottom-left
    const double x2 = n[10], y2 = n[11];  // bottom-right
    const double dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
    const double dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
    double g = 0, h = 0;
    if (std::fabs(dx3) > 1e-9 || std::fabs(dy3) > 1e-9) {
        const double den = dx1 * dy2 - dx2 * dy1;
        if (std::fabs(den) < 1e-9) {
            say(why, "the corners don't make a quad");
            return false;
        }
        g = (dx3 * dy2 - dx2 * dy3) / den;
        h = (dx1 * dy3 - dx3 * dy1) / den;
    }
    out.h[0] = x1 - x0 + g * x1;
    out.h[1] = x3 - x0 + h * x3;
    out.h[2] = x0;
    out.h[3] = y1 - y0 + g * y1;
    out.h[4] = y3 - y0 + h * y3;
    out.h[5] = y0;
    out.h[6] = g;
    out.h[7] = h;
    out.h[8] = 1;
    return true;
}

void QuadTransform::apply(float x, float y, float& outX, float& outY) const
{
    const double u = (x - src[0]) / src[2];
    const double v = (y - src[1]) / src[3];
    double w = h[6] * u + h[7] * v + h[8];
    if (std::fabs(w) < 1e-9) w = 1e-9;
    outX = static_cast<float>((h[0] * u + h[1] * v + h[2]) / w);
    outY = static_cast<float>((h[3] * u + h[4] * v + h[5]) / w);
}

}

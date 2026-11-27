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
#include "Package.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace
{
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // "v2.10b3" -> {"2", ".", "10", "b", "3"}: runs of digits and of everything else
    std::vector<std::string> tokens(const std::string& version)
    {
        std::vector<std::string> result;
        size_t i = 0;
        if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) {
            i = 1;
        }
        while (i < version.size()) {
            bool digit = std::isdigit(static_cast<unsigned char>(version[i])) != 0;
            size_t start = i;
            while (i < version.size() && (std::isdigit(static_cast<unsigned char>(version[i])) != 0) == digit) {
                i++;
            }
            std::string token = version.substr(start, i - start);
            if (!digit && token == ".") {
                continue;   // separators don't count, so "1.0" and "1" compare equal
            }
            result.push_back(token);
        }
        return result;
    }

    bool isNumber(const std::string& s)
    {
        return !s.empty() && std::isdigit(static_cast<unsigned char>(s[0])) != 0;
    }
}

namespace Downloads
{

uint64_t Package::totalSize() const
{
    uint64_t total = 0;
    for (const PackageFile& file : files) {
        total += file.size;
    }
    return total;
}

int compareVersions(const std::string& a, const std::string& b)
{
    std::vector<std::string> left = tokens(a);
    std::vector<std::string> right = tokens(b);
    size_t count = std::max(left.size(), right.size());
    for (size_t i = 0; i < count; i++) {
        std::string l = i < left.size() ? left[i] : "0";
        std::string r = i < right.size() ? right[i] : "0";
        if (isNumber(l) && isNumber(r)) {
            // Compare as numbers without overflowing on long digit runs
            size_t lz = l.find_first_not_of('0');
            size_t rz = r.find_first_not_of('0');
            std::string ln = lz == std::string::npos ? "0" : l.substr(lz);
            std::string rn = rz == std::string::npos ? "0" : r.substr(rz);
            if (ln.size() != rn.size()) {
                return ln.size() < rn.size() ? -1 : 1;
            }
            if (ln != rn) {
                return ln < rn ? -1 : 1;
            }
        }
        else if (l != r) {
            return l < r ? -1 : 1;
        }
    }
    return 0;
}

bool isHttpsUrl(const std::string& location)
{
    return lower(location.substr(0, 8)) == "https://" && location.size() > 8
        && location.find_first_of(" \t\r\n") == std::string::npos;
}

bool isReservedName(const std::string& name)
{
    // Windows opens these as devices whatever the extension, and ignores trailing spaces before it
    std::string stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    stem = lower(stem);
    static const std::set<std::string> devices = { "con", "prn", "aux", "nul", "conin$", "conout$" };
    if (devices.count(stem)) {
        return true;
    }
    return stem.size() == 4 && (stem.rfind("com", 0) == 0 || stem.rfind("lpt", 0) == 0) && std::isdigit(static_cast<unsigned char>(stem[3]));
}

bool isValidId(const std::string& id)
{
    if (id.empty() || id.size() > 100 || id[0] == '.' || id.back() == '.' || isReservedName(id)) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '_' || c == '-';
    });
}

bool isSafeRelativePath(const std::string& path)
{
    if (path.empty()) {
        return true;
    }
    if (path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) {
        return false;
    }
    std::string part;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (part == "..") {
                return false;
            }
            // Windows drops trailing dots and spaces, so "a." would be another name for "a"
            if (part != "." && !part.empty() && (part.back() == '.' || part.back() == ' ' || isReservedName(part))) {
                return false;
            }
            part.clear();
        }
        else {
            unsigned char c = static_cast<unsigned char>(path[i]);
            if (c < 0x20 || std::strchr("<>\"|?*", c)) {
                return false;
            }
            part += path[i];
        }
    }
    return true;
}

}

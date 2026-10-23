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

namespace
{
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

namespace Catalog
{

uint64_t Package::totalSize() const
{
    uint64_t total = 0;
    for (const PackageFile& file : files) {
        total += file.size;
    }
    return total;
}

bool isKnownTarget(const std::string& target)
{
    return target == "root" || target == "collections" || target == "items"
        || target == "media" || target == "layouts" || target == "meta";
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

bool satisfies(const std::string& version, const std::string& requirement)
{
    if (requirement.empty()) {
        return true;
    }
    std::string op;
    size_t i = 0;
    while (i < requirement.size() && (requirement[i] == '<' || requirement[i] == '>' || requirement[i] == '=')) {
        op += requirement[i++];
    }
    std::string wanted = requirement.substr(i);
    while (!wanted.empty() && wanted[0] == ' ') {
        wanted.erase(0, 1);
    }

    int comparison = compareVersions(version, wanted);
    if (op == ">=") return comparison >= 0;
    if (op == "<=") return comparison <= 0;
    if (op == ">") return comparison > 0;
    if (op == "<") return comparison < 0;
    return comparison == 0;   // "", "=" or "=="
}

}

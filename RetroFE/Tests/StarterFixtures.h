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
#pragma once

#include <fstream>
#include <string>
#include <vector>

namespace StarterFixtures
{
    inline std::vector<std::string> targets()
    {
        std::ifstream in(std::string(RETROFE_FIXTURES_DIR) + "/starter.txt");
        std::vector<std::string> result;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto trim = [](std::string value) {
                auto first = value.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return std::string();
                return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
            };
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto arrow = line.find("=>");
            std::string source = trim(line.substr(0, arrow));
            result.push_back(arrow == std::string::npos ? source : trim(line.substr(arrow + 2)));
        }
        return result;
    }
}

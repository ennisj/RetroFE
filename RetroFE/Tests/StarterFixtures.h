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
            auto arrow = line.find(" => ");
            result.push_back(arrow == std::string::npos ? line : line.substr(arrow + 4));
        }
        return result;
    }
}

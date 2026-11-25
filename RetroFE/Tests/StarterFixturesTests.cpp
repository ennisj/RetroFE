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
#include "TestSupport.h"
#include "StarterFixtures.h"
#include "../Source/Manager/Systems/GameImport.h"
#include <atomic>
#include <map>

namespace fs = std::filesystem;
using namespace Test;
using Manager::GameImport;

static Test::Suite starter("starter fixtures", [] {
    run("starter games import", [] {
        std::string root = makeRoot("starter-fixtures");
        std::map<std::string, std::vector<std::string>> names;
        auto targets = StarterFixtures::targets();
        check(!targets.empty(), "manifest opens");
        for (const auto& target : targets) {
            auto roms = target.find("/roms/");
            if (roms == std::string::npos) continue;
            std::string system = target.substr(0, roms);
            std::string relative = target.substr(roms + 6);
            names[system].push_back(relative);
            writeFile(root + "/source/" + target, relative);
        }
        struct System { const char* name; std::vector<std::string> extensions; size_t count; };
        std::vector<System> systems = {
            { "Nintendo Entertainment System", { "nes", "dat" }, 12 },
            { "Sega Genesis", { "bin", "gen", "md", "smd" }, 10 },
            { "MAME", { "zip" }, 10 }
        };
        std::atomic<bool> stop{ false };
        for (const auto& system : systems) {
            GameImport::Request request;
            request.from = fs::path(root) / "source" / system.name / "roms";
            request.into = fs::path(root) / "install" / system.name / "roms";
            request.extensions = system.extensions;
            check(names[system.name].size() == system.count, std::string(system.name) + " manifest count");
            check(GameImport::run(request, false, stop).found == system.count, std::string(system.name) + " recursive count");
            if (std::string(system.name) == "Nintendo Entertainment System") {
                request.subfolders = false;
                check(GameImport::run(request, false, stop).found == 11, "NES top folder count");
                request.subfolders = true;
            }
            auto result = GameImport::run(request, true, stop);
            check(result.copied == system.count, std::string(system.name) + " copied");
            for (const auto& name : names[system.name]) {
                auto destination = request.into / fs::path(name).filename();
                check(fs::exists(destination) && readFile(destination.string()) == name, "name and content: " + name);
            }
            result = GameImport::run(request, true, stop);
            check(result.already == system.count && result.copied == 0, std::string(system.name) + " already there");
        }
    });
});

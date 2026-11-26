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
#include "../Source/Database/Configuration.h"
#include "../Source/Execute/LaunchCommand.h"
#include <filesystem>

static Test::Suite launchCommand("launch command", [] {
    Test::run("execution failure follows platform start semantics", [] {
        using namespace Test;
        check(!LaunchCommand::executionFailed(true, true, true), "successful execution has no failure");
        check(!LaunchCommand::executionFailed(false, true, true), "Windows shell target started without a handle");
        check(LaunchCommand::executionFailed(false, false, true), "Windows target did not start");
        check(LaunchCommand::executionFailed(false, true, false), "Unix nonzero exit is a failure");
    });
    Test::run("missing and present game files", [] {
        using namespace Test;
        std::string root = makeRoot("launch-command");
        std::string previousRoot = Configuration::absolutePath;
        Configuration::absolutePath = root;
        Configuration config;
        config.setProperty("launchers.test.executable", std::string("emulator"));
        config.setProperty("collections.test.list.extensions", std::string("nes,dat"));
        LaunchCommand::Game game;
        game.collection = "test";
        game.itemCollection = "test";
        game.itemLauncher = "test";
        game.name = "Example";
        std::string folder = (std::filesystem::path(root) / "collections" / "test" / "roms").string();
        config.setProperty("launchers.test.arguments", std::string("\"%ITEM_FILEPATH%\""));
        auto command = LaunchCommand::build(config, game);
        check(command.failure.find(game.name) != std::string::npos && command.failure == "It couldn't start: Example's file isn't in " + folder + ".", "missing path reports game and folder: " + command.failure);
        check(command.executable.empty(), "missing path has no executable");
        config.setProperty("launchers.test.arguments", std::string("%ITEM_FILENAME%"));
        check(!LaunchCommand::build(config, game).failure.empty(), "missing filename fails");
        config.setProperty("launchers.test.arguments", std::string("%ITEM_NAME%"));
        command = LaunchCommand::build(config, game);
        check(command.failure.empty() && command.executable == "emulator" && command.arguments == game.name, "name alone starts");
        config.setProperty("launchers.test.arguments", std::string(""));
        command = LaunchCommand::build(config, game);
        check(command.failure.empty() && command.executable == "emulator" && command.arguments.empty(), "no arguments starts");
        config.setProperty("launchers.test.executable", std::string("%ITEM_FILEPATH%"));
        check(!LaunchCommand::build(config, game).failure.empty(), "file in executable fails");
        config.setProperty("launchers.test.executable", std::string("emulator"));
        config.setProperty("launchers.test.currentDirectory", std::string("%ITEM_FILENAME%"));
        check(!LaunchCommand::build(config, game).failure.empty(), "file in working directory fails");
        config.setProperty("launchers.test.currentDirectory", std::string(""));
        config.setProperty("launchers.test.arguments", std::string("\"%ITEM_FILEPATH%\""));
        std::string file = (std::filesystem::path(folder) / "Example.nes").string();
        writeFile(file, "game");
        command = LaunchCommand::build(config, game);
        check(command.failure.empty() && command.arguments == "\"" + file + "\"", "present file is passed as before");
        Configuration::absolutePath = previousRoot;
    });
});

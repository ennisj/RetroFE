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

#include <string>
#include <vector>

class Configuration;

// What Launcher::run() works out before it starts a game (the launcher, the
// game's file, the program, its arguments and folder), and what
// Launcher::execute() hands the OS. No SDL or page (an item only for gameFor), so the tests run
// the same code.
class Item;

namespace LaunchCommand
{
	// Windows can start a shell target without obtaining a process handle.
	inline bool executionFailed(bool executeOk, bool started, bool windows)
	{
		return !executeOk && !(windows && started);
	}

	// The game, as run() reads it from its Item
	struct Game
	{
		std::string collection;          // the collection run() is given
		std::string itemCollection;      // the item's own collection (collectionInfo->name)
		std::string itemLauncher;        // that collection's launcher (collectionInfo->launcher)
		std::string name;                // the item's name
		std::string file;                // its file name without extension, when not its name
		std::string filepath;            // its folder, when not the collection's games folder
	};

	Game gameFor(const std::string& collection, const Item& item);

	struct Command
	{
		std::string launcher;            // the launcher it uses (its reboot key is read after the run)
		std::string executable;
		std::string arguments;
		std::string currentDirectory;
		std::string failure;             // why it can't start (Launcher::failure()), or "" when it can
	};

	// Everything run() does before execute(), with the same log lines
	Command build(Configuration& config, const Game& game);

	// Launcher's lookups, as they were
	bool launcherName(Configuration& config, std::string& launcherName, const std::string& collection);
	bool launcherExecutable(Configuration& config, std::string& executable, const std::string& launcherName);
	bool launcherArgs(Configuration& config, std::string& args, const std::string& launcherName);
	bool extensions(Configuration& config, std::string& extensions, const std::string& collection);
	bool collectionDirectory(Configuration& config, std::string& directory, const std::string& collection);
	bool findFile(std::string& foundFilePath, std::string& foundFilename, const std::string& directory,
		const std::string& filenameWithoutExtension, const std::string& extensions);
	std::string replaceVariables(std::string str, const std::string& itemFilePath, const std::string& itemName,
		const std::string& itemFilename, const std::string& itemDirectory, const std::string& itemCollectionName);

	// The command line execute() gives CreateProcess on Windows: the program in
	// quotes, then the arguments (made safe for cmd.exe /c, see Library::cmdArguments)
	std::string windowsCommandLine(const std::string& executable, const std::string& arguments);
	// The argv execute() gives execvp elsewhere: the program, then the
	// arguments split as std::quoted reads them
	std::vector<std::string> unixArgv(const std::string& executable, const std::string& arguments);
}

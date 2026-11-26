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

#include "LaunchCommand.h"
#include "../Collection/Item.h"
#include "../Collection/CollectionInfo.h"
#include "../Database/Configuration.h"
#include "../Library/Files.h"
#include "../Library/Properties.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace LaunchCommand
{

Command build(Configuration& config, const Game& game)
{
	Command command;

	// Step 1: Determine launcher name (with potential per-item override)
	std::string launcherName = game.itemLauncher;
	std::string launcherFile = Utils::combinePath(Configuration::absolutePath, "collections", game.collection, "launchers", game.name + ".conf");

	if (std::ifstream launcherStream(launcherFile); launcherStream.good()) {
		std::string line;
		if (std::getline(launcherStream, line)) {
			std::string localLauncherKey = "localLaunchers." + game.collection + "." + line;
			launcherName = config.propertyPrefixExists(localLauncherKey) ? (game.collection + "." + line) : line;
			LOG_INFO("Launcher", "Using per-item launcher override: " + launcherName);
		}
	}

	// Check for collection-specific launcher if no override
	if (launcherName == game.itemLauncher) {
		std::string collectionLauncherKey = "collectionLaunchers." + game.collection;
		if (config.propertyPrefixExists(collectionLauncherKey)) {
			launcherName = game.itemCollection;
			LOG_INFO("Launcher", "Using collection-specific launcher: " + launcherName);
		}
	}
	command.launcher = launcherName;

	// Step 2: Retrieve executable, extensions, and directory
	std::string executablePath, selectedItemsDirectory, selectedItemsPath, extensionstr, matchedExtension, args;

	if (!launcherExecutable(config, executablePath, launcherName)) {
		LOG_ERROR("Launcher", "Launcher executable not found for: " + launcherName);
		command.failure = "It couldn't start: there's no launcher called " + launcherName + " with a program.";
		return command;
	}
	if (!extensions(config, extensionstr, game.collection)) {
		LOG_ERROR("Launcher", "No file extensions configured for collection: " + game.collection);
		command.failure = "It couldn't start: " + game.collection + " has no list.extensions.";
		return command;
	}
	if (!collectionDirectory(config, selectedItemsDirectory, game.collection)) {
		LOG_ERROR("Launcher", "No valid directory found for collection: " + game.collection);
		command.failure = "It couldn't start: " + game.collection + " has no games folder.";
		return command;
	}

	launcherArgs(config, args, launcherName);

	// Override directory if filepath is provided in the item
	if (!game.filepath.empty()) {
		selectedItemsDirectory = game.filepath;
		LOG_DEBUG("LauncherDebug", "Using filepath from item: " + selectedItemsDirectory);
	}

	// Step 3: Find the item file based on provided or derived names
	bool fileFound = findFile(selectedItemsPath, matchedExtension, selectedItemsDirectory,
		game.file.empty() ? game.name : game.file, extensionstr);
	std::string currentDirectoryKey = "launchers." + launcherName + ".currentDirectory";
	std::string configuredDirectory;
	config.getProperty(currentDirectoryKey, configuredDirectory);
	auto usesItemFile = [](const std::string& value) {
		return value.find("%ITEM_FILEPATH%") != std::string::npos
			|| value.find("%ITEM_FILENAME%") != std::string::npos;
	};
	if (!fileFound && (usesItemFile(args) || usesItemFile(executablePath) || usesItemFile(configuredDirectory))) {
		// The folder findFile searched, without its trailing separator
		std::string folder = selectedItemsDirectory;
		while (folder.size() > 1 && (folder.back() == '/' || folder.back() == '\\')) {
			folder.pop_back();
		}
		command.failure = "It couldn't start: " + game.name + "'s file isn't in " + folder + ".";
		LOG_ERROR("Launcher", "Not starting " + launcherName + ": it needs the game's file. " + command.failure);
		return command;
	}

	LOG_DEBUG("LauncherDebug", "File selected: " + selectedItemsPath);
	LOG_DEBUG("LauncherDebug", "Arguments before replacement: " + args);

	// Step 4: Substitute variables in args and executable path
	args = replaceVariables(args, selectedItemsPath, game.name, Utils::getFileName(selectedItemsPath), selectedItemsDirectory, game.collection);
	executablePath = replaceVariables(executablePath, selectedItemsPath, game.name, Utils::getFileName(selectedItemsPath), selectedItemsDirectory, game.collection);

	LOG_INFO("Launcher", "Final executable path: " + executablePath);
	LOG_INFO("Launcher", "Arguments after replacement: " + args);

	// Step 5: Determine the current working directory
	// By default the program's own folder. A bare name (cmd.exe, found on the
	// system path) has none: it runs in RetroFE's, not in a folder named after it
	std::string currentDirectory = Library::programFolder(executablePath, Configuration::absolutePath);
	config.getProperty(currentDirectoryKey, currentDirectory);

	currentDirectory = replaceVariables(currentDirectory, selectedItemsPath, game.name, Utils::getFileName(selectedItemsPath), selectedItemsDirectory, game.collection);
	LOG_DEBUG("LauncherDebug", "Final working directory: " + currentDirectory);

	command.executable = executablePath;
	command.arguments = args;
	command.currentDirectory = currentDirectory;
	return command;
}

bool launcherName(Configuration& config, std::string& launcherName, const std::string& collection)
{
	// find the launcher for the particular item
	std::string launcherKey = "collections." + collection + ".launcher";
	bool found = config.getProperty(launcherKey, launcherName);
	// Fork-compat (OneSauce data): the first of "launcher = a, b, c"
	launcherName = Library::firstLauncher(launcherName);
	if (!found) {
		std::stringstream ss;

		ss << "Launch failed. Could not find a configured launcher for collection \""
			<< collection
			<< "\" (could not find a property for \""
			<< launcherKey
			<< "\")";

		LOG_ERROR("Launcher", ss.str());

		return false;
	}

	std::stringstream ss;
	ss << "collections."
		<< collection
		<< " is configured to use launchers."
		<< launcherName
		<< "\"";

	LOG_DEBUG("Launcher", ss.str());

	return true;
}

bool launcherExecutable(Configuration& config, std::string& executable, const std::string& launcherName) {
	// Try with the localLauncher prefix
	std::string executableKey = "localLaunchers." + launcherName + ".executable";
	if (!config.getProperty(executableKey, executable)) {
		// Try with the collectionLauncher prefix
		executableKey = "collectionLaunchers." + launcherName + ".executable";
		if (!config.getProperty(executableKey, executable)) {
			// Finally, try with the global launcher prefix
			executableKey = "launchers." + launcherName + ".executable";
			if (!config.getProperty(executableKey, executable)) {
				LOG_ERROR("Launcher", "No launcher found for: " + executableKey);
				return false;
			}
		}
	}
	return true;
}

bool launcherArgs(Configuration& config, std::string& args, const std::string& launcherName) {
	// Try with the localLauncher prefix
	std::string argsKey = "localLaunchers." + launcherName + ".arguments";
	if (!config.getProperty(argsKey, args)) {
		// Try with the collectionLauncher prefix
		argsKey = "collectionLaunchers." + launcherName + ".arguments";
		if (!config.getProperty(argsKey, args)) {
			// Finally, try with the global launcher prefix
			argsKey = "launchers." + launcherName + ".arguments";
			if (!config.getProperty(argsKey, args)) {
				LOG_WARNING("Launcher", "No arguments specified for: " + argsKey);
				args.clear(); // Ensure args is empty if not found
			}
		}
	}
	return true;
}

bool extensions(Configuration& config, std::string& extensions, const std::string& collection)
{
	if (std::string extensionsKey = "collections." + collection + ".list.extensions"; !config.getProperty(extensionsKey, extensions)) {
		LOG_ERROR("Launcher", "No extensions specified for: " + extensionsKey);
		return false;
	}

	extensions = Utils::replace(extensions, " ", "");
	extensions = Utils::replace(extensions, ".", "");

	return true;
}

bool collectionDirectory(Configuration& config, std::string& directory, const std::string& collection)
{
	std::string itemsPathValue;
	std::string mergedCollectionName;

	// find the items path folder (i.e. ROM path)
	config.getCollectionAbsolutePath(collection, itemsPathValue);
	directory += itemsPathValue + Utils::pathSeparator;

	return true;
}

bool findFile(std::string& foundFilePath, std::string& foundFilename, const std::string& directory, const std::string& filenameWithoutExtension, const std::string& extensions) {
	bool fileFound = false;
	std::stringstream ss(extensions);
	std::string extension;

	while (std::getline(ss, extension, ',')) {
		fs::path filePath = fs::path(directory) / (filenameWithoutExtension + "." + extension);

		if (fs::exists(filePath)) {
			foundFilePath = fs::absolute(filePath).string();
			foundFilename = extension;
			fileFound = true;
			LOG_INFO("Launcher", "File found: " + foundFilePath + " with extension: ." + extension);
			break; // Exit the loop once the file is found
		}
	}

	if (!fileFound) {
		LOG_ERROR("Launcher", "No matching files found for \"" + filenameWithoutExtension + "\" in directory \"" + directory + "\" with extensions: " + extensions);
	}

	return fileFound;
}

std::string replaceVariables(std::string str,
	const std::string& itemFilePath,
	const std::string& itemName,
	const std::string& itemFilename,
	const std::string& itemDirectory,
	const std::string& itemCollectionName)
{
	str = Utils::replace(str, "%ITEM_FILEPATH%", itemFilePath);
	str = Utils::replace(str, "%ITEM_NAME%", itemName);
	str = Utils::replace(str, "%ITEM_FILENAME%", itemFilename);
	str = Utils::replace(str, "%ITEM_DIRECTORY%", itemDirectory);
	str = Utils::replace(str, "%ITEM_COLLECTION_NAME%", itemCollectionName);
	str = Utils::replace(str, "%RETROFE_PATH%", Configuration::absolutePath);
	str = Utils::replace(str, "%COLLECTION_PATH%", Utils::combinePath(Configuration::absolutePath, "collections", itemCollectionName));
#ifdef WIN32
	str = Utils::replace(str, "%RETROFE_EXEC_PATH%", Utils::combinePath(Configuration::absolutePath, "retrofe", "RetroFE.exe"));
	const char* comspec = std::getenv("COMSPEC");
	if (comspec) {
		str = Utils::replace(str, "%CMD%", std::string(comspec));
	}
#else
	str = Utils::replace(str, "%RETROFE_EXEC_PATH%", Utils::combinePath(Configuration::absolutePath, "RetroFE"));
#endif

	return str;
}

std::string windowsCommandLine(const std::string& executable, const std::string& arguments)
{
	// Construct command line
	std::string commandLine = "\"" + executable + "\"";
	std::string runArgs = arguments;
	if (Library::iequals(fs::path(executable).filename().string(), "cmd.exe")) {
		// cmd /c drops the first and last quote when there are more than two
		runArgs = Library::cmdArguments(arguments);
		if (runArgs != arguments) {
			LOG_INFO("Launcher", "Quoted the command for cmd.exe /c: " + runArgs);
		}
	}
	if (!runArgs.empty()) {
		commandLine += " " + runArgs;
	}
	return commandLine;
}

std::vector<std::string> unixArgv(const std::string& executable, const std::string& arguments)
{
	std::vector<std::string> argVector;
	argVector.push_back(executable);
	std::istringstream argsStream(arguments);
	std::string arg;
	while (argsStream >> std::quoted(arg)) {
		argVector.push_back(arg);
	}
	return argVector;
}

}

LaunchCommand::Game LaunchCommand::gameFor(const std::string& collection, const Item& item)
{
    Game game;
    game.collection = collection;
    game.itemCollection = item.collectionInfo->name;
    game.itemLauncher = item.collectionInfo->launcher;
    game.name = item.name;
    game.file = item.file;
    game.filepath = item.filepath;
    return game;
}

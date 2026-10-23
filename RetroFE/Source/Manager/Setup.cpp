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
#include "Setup.h"
#include "../Database/Configuration.h"
#include "../Database/GlobalOpts.h"
#include "../Library/Backups.h"
#include "../Library/Edits.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    const char* const mainCollection = "Main";
    Configuration* liveConfig = nullptr;

    std::string collectionsPath()
    {
        return Utils::combinePath(Configuration::absolutePath, "collections");
    }

    // Splits "zip, .7z" into {"zip", "7z"}, lowercased
    std::set<std::string> parseExtensions(const std::string& extensions)
    {
        std::vector<std::string> parts;
        Utils::listToVector(extensions, parts, ',');
        std::set<std::string> result;
        for (std::string part : parts) {
            part = Utils::trimEnds(part);
            if (!part.empty() && part[0] == '.') {
                part.erase(0, 1);
            }
            if (!part.empty()) {
                result.insert(Utils::toLower(part));
            }
        }
        return result;
    }

    std::string joinExtensions(const std::set<std::string>& extensions)
    {
        std::string result;
        for (const std::string& extension : extensions) {
            result += (result.empty() ? "" : ",") + extension;
        }
        return result;
    }

    bool writeFile(const std::string& path, const std::string& content, std::string& error)
    {
        std::ofstream file(path, std::ios::trunc);
        if (!file) {
            error = "Could not write " + path;
            return false;
        }
        file << content;
        if (!file) {
            error = "Could not write " + path;
            return false;
        }
        LOG_INFO("Setup", "Wrote " + path);
        return true;
    }
}

namespace Setup
{

void setConfiguration(Configuration* config)
{
    liveConfig = config;
}

std::string firstCollection()
{
    // RetroFE's own value (settings files plus any command-line override) when it's running
    if (liveConfig) {
        std::string first;
        liveConfig->getProperty(OPTION_FIRSTCOLLECTION, first);
        return first.empty() ? mainCollection : first;
    }
    return Library::MenuFiles::topMenu(Configuration::absolutePath);
}

std::string topMenu()
{
    return Library::MenuFiles::menuForNewCollections(Configuration::absolutePath, firstCollection());
}

std::string checkName(const std::string& name)
{
    std::string problem = Library::MenuFiles::checkChildName(Utils::trimEnds(name));
    return problem.empty() ? "" : "That system name won't work: " + problem;
}

bool addToMainMenu(const std::string& name, std::string& error)
{
    error = checkName(name);
    if (!error.empty()) {
        return false;
    }
    // The top menu can't list itself
    std::string top = topMenu();
    if (Utils::toLower(name) == Utils::toLower(top)) {
        return true;
    }
    Library::Backups backups(Configuration::absolutePath);
    return Library::MenuFiles::add(Configuration::absolutePath, top, name, -1, &backups, error);
}

bool removeFromMainMenu(const std::string& name, std::string& error)
{
    Library::Backups backups(Configuration::absolutePath);
    return Library::MenuFiles::remove(Configuration::absolutePath, topMenu(), name, &backups, error);
}

bool hasSettingsFile()
{
    return fs::exists(Utils::combinePath(Configuration::absolutePath, "settings.conf"));
}

std::vector<std::string> listCollections()
{
    std::vector<std::string> names;
    std::error_code ec;
    std::string top = topMenu();
    for (const auto& entry : fs::directory_iterator(collectionsPath(), ec)) {
        std::string name = entry.path().filename().string();
        if (entry.is_directory() && !name.empty() && name[0] != '_' && name != top) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

int countRoms(const std::string& romPath, const std::string& extensions)
{
    std::error_code ec;
    if (romPath.empty() || !fs::is_directory(romPath, ec)) {
        return -1;
    }

    std::set<std::string> wanted = parseExtensions(extensions);
    int count = 0;
    for (const auto& entry : fs::directory_iterator(romPath, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string extension = entry.path().extension().string();
        if (!extension.empty() && wanted.count(Utils::toLower(extension.substr(1)))) {
            count++;
        }
    }
    return ec ? -1 : count;
}

std::string validate(const NewCollection& collection)
{
    std::string name = Utils::trimEnds(collection.name);
    if (name.empty()) {
        return "Give the system a name.";
    }
    if (name.find_first_of("\\/:*?\"<>|#") != std::string::npos) {
        return "The name can't contain \\ / : * ? \" < > | or #.";
    }
    if (name[0] == '_' || name[0] == '.') {
        return "The name can't start with _ or a dot.";
    }
    std::string top = topMenu();
    if (Utils::toLower(name) == Utils::toLower(top)) {
        return "\"" + top + "\" is the menu new systems are listed in. Pick another name.";
    }
    if (fs::exists(Utils::combinePath(collectionsPath(), name))) {
        return "A system called \"" + name + "\" already exists.";
    }

    std::error_code ec;
    if (collection.romPath.empty() || !fs::is_directory(collection.romPath, ec)) {
        return "Choose the folder your games are in.";
    }
    if (parseExtensions(collection.extensions).empty()) {
        return "List the file extensions to look for, e.g. zip,7z.";
    }
    if (collection.executable.empty() || !fs::is_regular_file(collection.executable, ec)) {
        return "Choose the emulator program to launch games with.";
    }

    // '#' starts a comment in .conf files, so it would cut the value short
    for (const std::string* value : { &collection.romPath, &collection.executable, &collection.arguments }) {
        if (value->find('#') != std::string::npos) {
            return "Paths and arguments can't contain #.";
        }
    }
    return "";
}

bool createCollection(const NewCollection& collection, std::string& error)
{
    error = validate(collection);
    if (!error.empty()) {
        return false;
    }

    std::string name = Utils::trimEnds(collection.name);
    std::string path = Utils::combinePath(collectionsPath(), name);

    try {
        fs::create_directories(path);
    }
    catch (const fs::filesystem_error& e) {
        error = e.what();
        return false;
    }

    std::string settings =
        "# Created by RetroFE setup\n"
        "list.path = " + collection.romPath + "\n"
        "list.extensions = " + joinExtensions(parseExtensions(collection.extensions)) + "\n"
        "list.includeMissingItems = false\n"
        "list.menuSort = yes\n";
    if (!writeFile(Utils::combinePath(path, "settings.conf"), settings, error)) {
        return false;
    }

    // A launcher file inside the collection is picked up automatically for it
    std::string launcher =
        "# Created by RetroFE setup\n"
        "executable = " + collection.executable + "\n"
        "arguments = " + collection.arguments + "\n";
    if (!writeFile(Utils::combinePath(path, "launcher." + Utils::getOSType() + ".conf"), launcher, error)) {
        return false;
    }

    return addToMainMenu(name, error);
}

}

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

// Creates and inspects the files RetroFE reads, for the management UI.
// Everything is written under Configuration::absolutePath.
namespace Setup
{
    // RetroFE's live configuration, so firstCollection is read as RetroFE uses
    // it (a command-line override included). Without it, the settings files are read.
    void setConfiguration(Configuration* config);

    // Where Browse goes: firstCollection, "Main" when unset
    std::string firstCollection();

    // The menu new systems are listed in: firstCollection when it's a menu or
    // can become one, otherwise "Main" (see Library::MenuFiles::menuForNewCollections)
    std::string topMenu();

    // Empty when name can be a system's folder and menu entry, otherwise why not
    std::string checkName(const std::string& name);

    struct NewCollection
    {
        std::string name;
        std::string romPath;
        std::string extensions;  // comma separated, without dots
        std::string executable;
        std::string arguments;
    };

    // True when settings.conf exists, i.e. RetroFE isn't running on built-in defaults
    bool hasSettingsFile();

    // Names of the collection folders, excluding the top menu and "_" folders
    std::vector<std::string> listCollections();

    // Number of files in a folder matching the extensions, or -1 if the folder can't be read
    int countRoms(const std::string& romPath, const std::string& extensions);

    // Returns an empty string when valid, otherwise what's wrong
    std::string validate(const NewCollection& collection);

    // Creates collections/<name> with its settings and launcher, and adds it to the top menu
    bool createCollection(const NewCollection& collection, std::string& error);

    // Adds a collection to topMenu() if it isn't listed yet, or removes it,
    // leaving the other lines as they were. The menu's menu.txt is used, or its
    // menu/ folder when it has no menu.txt (see Library::MenuFiles); originals
    // go to manage/backups. A name checkName() refuses isn't added.
    bool addToMainMenu(const std::string& name, std::string& error);
    bool removeFromMainMenu(const std::string& name, std::string& error);
}

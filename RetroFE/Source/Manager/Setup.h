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

// Creates and inspects the files RetroFE reads, for the management UI.
// Everything is written under Configuration::absolutePath.
namespace Setup
{
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

    // Names of the collection folders, excluding the Main menu and "_" folders
    std::vector<std::string> listCollections();

    // Number of files in a folder matching the extensions, or -1 if the folder can't be read
    int countRoms(const std::string& romPath, const std::string& extensions);

    // Returns an empty string when valid, otherwise what's wrong
    std::string validate(const NewCollection& collection);

    // Creates collections/<name> with its settings and launcher, and adds it to the Main menu
    bool createCollection(const NewCollection& collection, std::string& error);
}

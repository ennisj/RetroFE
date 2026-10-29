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

#include "Sources.h"
#include "Installer.h"

namespace Catalog
{
    struct Emulator
    {
        std::string id, name, folder, repo;
        std::string tagRule, assetRule, archive, topFolder;
        std::string program;
        std::vector<std::string> portableFiles;
        std::string launcher, arguments, description;
    };

    const std::vector<Emulator>& emulators();
    const Emulator* findEmulator(const std::string& packageId);
    bool parseEmulatorReleases(const std::string& text, const Emulator& emulator, const std::string& source,
                               std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error);
    std::string createEmulatorLauncher(const std::string& root, const std::string& catalog, Manifest& manifest);
    std::string removeEmulatorLauncher(const std::string& root, const Manifest& manifest);
    void readEmulators(const SourceConfig& source, const std::string& cacheFolder, Http* http, SourceContents& contents);
}

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

#include "Package.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Installs downloaded packages and records what they wrote.
//
// A package is first extracted into catalog/staging/<id>/ with a plan of
// where each file goes. Only once that's complete is it marked ready and
// moved into place, so an interruption never leaves half a package: on the
// next start recover() finishes a ready install or discards an unfinished one.
// Files that get replaced are backed up to catalog/backups/<id>/<version>/.
// catalog/installed/<id>.json lists every file installed, which is what
// updates and removal go by.
namespace Catalog
{
    // Where each install target is on this install
    struct Targets
    {
        std::string root;
        std::string collections;
        std::string items;      // ROM folders: baseItemPath, or collections
        std::string media;      // artwork: baseMediaPath, or collections
        std::string layouts;
        std::string meta;

        // "" for an unknown target
        std::string resolve(const std::string& target) const;
    };

    struct InstalledFile
    {
        std::string target;
        std::string path;       // under the target, '/' separated, UTF-8
        uint64_t size = 0;
        std::string sha256;
    };

    struct Manifest
    {
        std::string id;
        std::string name;
        std::string kind;
        std::string version;
        std::string source;
        std::vector<InstalledFile> files;
    };

    class Installer
    {
    public:
        Installer(const std::string& catalogFolder, const Targets& targets);

        // Installs from the downloaded files, one local path per package file
        // in the same order. Zip archives are extracted; other files are copied.
        bool install(const Package& package, const std::vector<std::string>& downloads, std::string& error);

        // Removes a package's files, except ones changed since they were
        // installed, which are kept; kept says how many
        bool remove(const std::string& id, int& kept, std::string& error);

        // Finishes or discards installs that were interrupted
        void recover();

        // Every installed package, by id
        std::map<std::string, Manifest> installed() const;

    private:
        bool stage(const Package& package, const std::vector<std::string>& downloads, const std::string& staging, std::string& error);
        bool commit(const std::string& staging, std::string& error);
        std::string manifestPath(const std::string& id) const;

        std::string catalog_;
        Targets targets_;
    };

    bool readManifest(const std::string& file, Manifest& manifest, std::string& error);
    bool writeManifest(const std::string& file, const Manifest& manifest, std::string& error);
}

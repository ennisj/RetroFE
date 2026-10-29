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
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Installs downloaded packages and records what they wrote.
//
// A package is first extracted into catalog/staging/<id>/ with a plan of
// where each file goes. Only once that's complete is it marked ready and
// moved into place. Before the first move, every file about to be replaced
// is backed up to catalog/backups/<id>/<name>/ and listed in a journal, so
// a failed move puts everything back, and an interruption is finished by
// recover() on the next start (or put back, if it can't be finished).
// catalog/installed/<id>.json lists every file installed, which is what
// updates and removal go by. Removing a package puts back the files it
// replaced, and keeps files changed since they were installed.
//
// Executables and scripts are refused except for built-in emulator downloads,
// as is anything under a protected folder (the catalog itself, RetroFE's
// program folder), whatever the target.
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
        std::string emulators;
        std::vector<std::string> protectedFolders;   // never written, even inside a target

        // "" for an unknown target
        std::string resolve(const std::string& target) const;
    };

    struct InstalledFile
    {
        std::string target;
        std::string path;       // under the target, '/' separated, UTF-8
        uint64_t size = 0;
        std::string sha256;
        bool portable = false;
        std::string backup;     // the backups/<id>/ folder holding the file this replaced, if any
    };

    // Whether a file name is a program or script, which packages may not install
    bool isExecutableName(const std::string& name);

    struct Manifest
    {
        std::string id;
        std::string name;
        std::string kind;
        std::string version;
        std::string source;
        std::vector<InstalledFile> files;
        std::string sourceType; // recorded from configuration, never from a package
        std::string launcherPath, launcherSha256, launcherName;
    };

    class Installer
    {
    public:
        Installer(const std::string& catalogFolder, const Targets& targets);

        // Installs from the downloaded files, one local path per package file
        // in the same order. Archives are detected by content; other files are copied.
        bool install(const Package& package, const std::vector<std::string>& downloads, std::string& error);

        struct Removal
        {
            int kept = 0;       // changed since they were installed, so left alone
            int restored = 0;   // files the package had replaced, put back
        };

        // Removes a package's files
        bool remove(const std::string& id, Removal& removal, std::string& error);

        // Finishes interrupted installs, or puts back what they changed
        void recover();

        // Every installed package, by id
        std::map<std::string, Manifest> installed() const;

        // While *stop is set, extracting gives up (safe until an install is marked ready)
        void setStopFlag(const std::atomic<bool>* stop) { stop_ = stop; }

    private:
        bool stage(const Package& package, const std::vector<std::string>& downloads, const std::string& staging, std::string& error);
        bool commit(const std::string& staging, std::string& error);
        std::string manifestPath(const std::string& id) const;
        // "" if a file may be written there, otherwise why not
        std::string refusal(const InstalledFile& file, const std::string& id, const std::string& kind, const std::string& source, bool installedAuthority = false) const;
        bool trustedEmulator(const std::string& id, const std::string& kind, const std::string& source) const;

        std::string catalog_;
        Targets targets_;
        const std::atomic<bool>* stop_ = nullptr;
    };

    bool readManifest(const std::string& file, Manifest& manifest, std::string& error);
    bool writeManifest(const std::string& file, const Manifest& manifest, std::string& error);
}

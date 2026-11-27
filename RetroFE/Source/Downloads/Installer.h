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

// Installs downloaded emulators and cores into emulators/ and records what
// they wrote.
//
// A package is first extracted into manage/downloads/staging/<id>/ with a
// plan of where each file goes. Only once that's complete is it marked ready
// and moved into place. Before the first move, every file about to be
// replaced is backed up to manage/downloads/backups/<id>/<name>/ and listed in
// a journal, so a failed move puts everything back, and an interruption is
// finished by recover() the next time downloads are used (or put back, if it
// can't be finished). manage/downloads/installed/<id>.json lists every file
// installed, which is what updates and removal go by. Removing a package puts
// back the files it replaced, and keeps files changed since they were
// installed.
//
// Only entries of the built-in list install anything (Emulators.h): an
// emulator only under emulators/<its folder>/, a core only its own
// emulators/RetroArch/cores/<core>_libretro.dll. Nothing extracted is run.
namespace Downloads
{
    struct InstalledFile
    {
        std::string path;       // under emulators/, '/' separated, UTF-8: "DuckStation/portable.txt"
        uint64_t size = 0;
        std::string sha256;
        bool portable = false;  // a portable-mode file this module created, not one the archive had
        std::string backup;     // the backups/<id>/ folder holding the file this replaced, if any
    };

    struct Manifest
    {
        std::string id;
        std::string name;
        std::string kind;       // emulator or core
        std::string version;
        std::vector<InstalledFile> files;
        std::string launcherPath, launcherSha256, launcherName;   // the launcher created, if any
        // Removed, but files were left in its folder (settings, changed files):
        // the folder is still this module's, so installing again may use it
        bool removed = false;
    };

    class Installer
    {
    public:
        // downloadsFolder: manage/downloads (staging/, installed/, backups/).
        // emulatorsFolder: <RetroFE>/emulators, the only place files go.
        Installer(const std::string& downloadsFolder, const std::string& emulatorsFolder);

        // Installs (or updates) from the downloaded files, one local path per
        // package file in the same order. Every file is an archive (zip, 7z or
        // a self-extracting 7z, told apart by content).
        bool install(const Package& package, const std::vector<std::string>& downloads, std::string& error);

        // After an install: " Replaced 2 changed files; backups are in ..." or ""
        const std::string& installMessage() const { return installMessage_; }

        struct Removal
        {
            int kept = 0;       // changed since they were installed, so left alone
            int restored = 0;   // files the package had replaced, put back
            std::vector<std::string> left;   // files still in the emulator's folder after removal, under emulators/
        };

        // Removes a package's files. RetroArch is refused while cores are installed.
        bool remove(const std::string& id, Removal& removal, std::string& error);

        // Finishes interrupted installs, or puts back what they changed
        void recover();

        // Every installed package, by id (not removed ones)
        std::map<std::string, Manifest> installed() const;

        // While *stop is set, extracting gives up (safe until an install is marked ready)
        void setStopFlag(const std::atomic<bool>* stop) { stop_ = stop; }

        // A folder that's never written, even inside emulators/
        void protect(const std::string& folder) { protectedFolders_.push_back(folder); }

        // "" if package may be installed where it goes, otherwise why not: an
        // emulator's folder that exists and wasn't installed by this module
        // ("emulators/MAME is already there..."), or a core without RetroArch
        std::string folderRefusal(const Package& package) const;

        std::string manifestPath(const std::string& id) const;

    private:
        bool stage(const Package& package, const std::vector<std::string>& downloads, const std::string& staging, std::string& error);
        bool commit(const std::string& staging, std::string& error);
        // "" if the package id and kind may write this file, otherwise why not
        std::string refusal(const InstalledFile& file, const std::string& id, const std::string& kind) const;

        std::string installMessage_;
        std::string folder_;
        std::string emulators_;
        std::vector<std::string> protectedFolders_;
        const std::atomic<bool>* stop_ = nullptr;
    };

    bool readManifest(const std::string& file, Manifest& manifest, std::string& error);
    bool writeManifest(const std::string& file, const Manifest& manifest, std::string& error);
}

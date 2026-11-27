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
#include <functional>
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
// emulators/RetroArch/cores/<core>_libretro.dll (.so, .dylib). Nothing
// extracted is run.
//
// Off Windows: an AppImage is installed as the entry's program, mode 0755,
// once it's checked to be ELF; a macOS disk image is attached read-only
// (hdiutil, never anything from the image) to copy the entry's .app out, and
// always detached; the executable bits an archive stores are kept (never
// setuid, setgid or sticky); and a symbolic link is recreated only when its
// target is relative and stays inside the emulator's folder (anything else is
// refused). Links are recorded in the manifest, so updates and removal handle
// the link itself and never follow it. Windows refuses links, as before.
namespace Downloads
{
    struct InstalledFile
    {
        std::string path;       // under emulators/, '/' separated, UTF-8: "DuckStation/portable.txt"
        uint64_t size = 0;
        std::string sha256;
        bool portable = false;  // a portable-mode file this module created, not one the archive had
        std::string backup;     // the backups/<id>/ folder holding the file this replaced, if any
        std::string link;       // a symbolic link to this relative target (UTF-8), not a file
        uint32_t mode = 0;      // its permission bits (0777 at most) off Windows; 0 = as created
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

    // Opens a disk image read-only at a folder, and closes it again. macOS's is
    // hdiutil (attach -readonly -nobrowse -noautoopen -mountpoint, then detach);
    // there's none elsewhere, so a DMG can't be installed there. Tests give
    // their own.
    struct ImageOpener
    {
        std::function<bool(const std::string& image, const std::string& mountPoint, std::string& error)> attach;
        std::function<void(const std::string& mountPoint)> detach;
        // Whether an image is attached there (macOS: another device than its parent folder)
        std::function<bool(const std::string& mountPoint)> mounted;
    };
    ImageOpener systemImageOpener();

    // Whether a file starts as an ELF program does (what an AppImage is)
    bool isElfFile(const std::string& path);
    // Whether a file ends with a UDIF disk image's trailer ("koly")
    bool isDiskImage(const std::string& path);

    class Installer
    {
    public:
        // downloadsFolder: manage/downloads (staging/, installed/, backups/).
        // emulatorsFolder: <RetroFE>/emulators, the only place files go.
        Installer(const std::string& downloadsFolder, const std::string& emulatorsFolder);

        // Installs (or updates) from the downloaded files, one local path per
        // package file in the same order. Every file is an archive (zip, 7z or
        // a self-extracting 7z, told apart by content), or what the entry's
        // form says: an AppImage, or a disk image (macOS).
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

        // How a disk image is opened: systemImageOpener() unless a test sets another
        void setImageOpener(ImageOpener opener) { imageOpener_ = std::move(opener); }

        // "" if package may be installed where it goes, otherwise why not: an
        // emulator's folder that exists and wasn't installed by this module
        // ("emulators/MAME is already there..."), or a core without RetroArch
        std::string folderRefusal(const Package& package) const;

        std::string manifestPath(const std::string& id) const;

    private:
        bool stage(const Package& package, const std::vector<std::string>& downloads, const std::string& staging, std::string& error);
        bool commit(const std::string& staging, std::string& error);
        // Removes a staging folder, detaching a disk image still attached in it first
        void clearStaging(const std::string& staging) const;
        // "" if the package id and kind may write this file, otherwise why not
        std::string refusal(const InstalledFile& file, const std::string& id, const std::string& kind) const;

        std::string installMessage_;
        std::string folder_;
        std::string emulators_;
        std::vector<std::string> protectedFolders_;
        const std::atomic<bool>* stop_ = nullptr;
        ImageOpener imageOpener_;
    };

    bool readManifest(const std::string& file, Manifest& manifest, std::string& error);
    bool writeManifest(const std::string& file, const Manifest& manifest, std::string& error);
}

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

#include "Http.h"
#include "Installer.h"
#include "Package.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Emulator downloads as the Manage screens see them: the built-in list of
// emulators (Emulators.h) and RetroArch's cores, checked, installed, updated
// and removed. Every call is synchronous, so a screen runs it on a worker
// thread and passes a progress callback and a stop flag; calls from several
// threads take turns. Nothing here runs at RetroFE's start: the first call
// finishes any interrupted install (recover()).
//
// Everything is relative to RetroFE's folder (Configuration::absolutePath):
//   manage/downloads/cache/      GitHub release lists with their ETags (one
//                                folder per entry, rate-limit.json), and
//                                libretro's core list (cores.index, cores.modified)
//   manage/downloads/partial/    unfinished downloads, continued next time
//   manage/downloads/staging/, installed/<id>.json, backups/   see Installer
//   emulators/<folder>/          what's installed
//   launchers.<os>/              the launchers created, as Manage writes them
//                                (launchers.windows, launchers.linux, launchers.apple)
// Nothing else is written, and never RetroFE's program folder (retrofe/).
namespace Downloads
{
    // What a call is doing, for the screen: a sentence, and bytes so far of
    // a total (0 when there's nothing to count or it isn't known)
    using Progress = std::function<void(const std::string& text, uint64_t done, uint64_t total)>;

    // An entry of the built-in list, without asking the network
    struct EmulatorStatus
    {
        std::string id;                 // package id: emulator-duckstation
        std::string name;               // DuckStation
        std::string folder;             // emulators/<folder>
        std::string description;
        std::string launcher;           // the launcher an install creates; "" for RetroArch (per core)
        std::string installedVersion;   // "" if not installed
        std::string launcherCreated;    // the launcher the install created, if it's recorded
        bool available = true;          // false: no build for this platform (unavailable says so)
        std::string unavailable;        // "No Linux arm64 build"
    };

    // The latest release of an entry, from check()
    struct Release
    {
        std::string id;
        std::string name;
        std::string installedVersion;
        std::string version;            // "" when none could be found (see error)
        std::string url;
        uint64_t size = 0;              // 0 if not published (libretro's)
        std::string sha256;             // GitHub's digest
        bool noChecksum = false;        // libretro doesn't publish a checksum for this download
        bool updateAvailable = false;
        bool fromCache = false;         // GitHub couldn't be asked; this is the last copy
        std::string error;              // offline, no release, no checksum...
        int64_t checked = 0;            // last successful GitHub fetch or confirmation
        int64_t retryAt = 0;            // GitHub's rate limit: asked again after this (seconds since 1970); 0 if not limited
        std::vector<std::string> warnings;
        bool available = true;          // false: no build for this platform, so nothing was asked
    };

    struct Core
    {
        std::string id;                 // core-genesis_plus_gx
        std::string core;               // genesis_plus_gx
        std::string name;               // Genesis Plus GX (RetroArch's info file, else the core)
        std::string system;             // its systemname, for the filter
        std::string description;
        std::string version;            // the nightly build's date
        std::string installedVersion;   // "" if not installed
        bool updateAvailable = false;   // a newer nightly
        uint32_t crc32 = 0;             // of the unpacked DLL, from libretro's list
        std::string url;
    };

    struct CoreList
    {
        std::vector<Core> cores;        // installed first, then by name
        std::string error;              // "Install RetroArch first", or no list at all
        std::vector<std::string> warnings;
        bool fromCache = false;
    };

    // How an install or removal went
    struct Outcome
    {
        bool ok = false;
        bool cancelled = false;         // stopped; downloads are kept, so the next install continues
        std::string message;            // for the screen; names the launcher, kept files...
        bool libraryChanged = false;    // a launcher was written or removed: rescan the library
    };

    class Service
    {
    public:
        // root: RetroFE's folder. http may be null (then nothing is downloaded).
        Service(const std::string& root, std::unique_ptr<Http> http);

        // manage/downloads under root
        static std::string downloadsFolder(const std::string& root);

        // Whether downloads work here: Windows x64, Linux x86_64 or arm64, or
        // macOS (the platforms the list has builds for), with an Http
        bool supported() const;

        // The built-in entries with their installed versions. No network.
        std::vector<EmulatorStatus> list(const Progress& progress = {}, const std::atomic<bool>* stop = nullptr);

        // Every entry's latest release, through the ETag cache. A fresh check
        // asks again after a minute; automatic checks reuse lists for an hour.
        std::vector<Release> check(const Progress& progress = {}, const std::atomic<bool>* stop = nullptr, bool fresh = false);

        // Installs or updates an entry (its package id, from list()):
        // downloads and checks it, installs it, creates its portable files and,
        // on a first install, its launcher (not RetroArch, whose launchers come
        // with cores; not when a launcher of that name exists).
        Outcome install(const std::string& id, const Progress& progress = {}, const std::atomic<bool>* stop = nullptr);

        // Removes an entry's files and the launcher it created, unless the
        // launcher changed or a system or game uses it. Files the emulator
        // wrote or changed stay, and the message says what's left. RetroArch
        // is refused while cores are installed.
        Outcome remove(const std::string& id, const Progress& progress = {}, const std::atomic<bool>* stop = nullptr);

        // libretro's cores for a system ("" or "all" for every system), from
        // its list (cached, asked with If-Modified-Since). Needs RetroArch.
        CoreList cores(const std::string& system = "", const Progress& progress = {}, const std::atomic<bool>* stop = nullptr);
        // core: e.g. genesis_plus_gx. Creates the launcher "RetroArch <core name>".
        Outcome installCore(const std::string& core, const Progress& progress = {}, const std::atomic<bool>* stop = nullptr);
        Outcome removeCore(const std::string& core, const Progress& progress = {}, const std::atomic<bool>* stop = nullptr);

        // Finishes interrupted installs, or puts back what they changed.
        // Every other call does it first.
        void recover();

        // For tests: the time in seconds since 1970, and the drive's free space
        std::function<int64_t()> now;
        std::function<bool(const std::string& folder, uint64_t& available)> freeSpace;

    private:
        Outcome installPackage(const Package& package, bool updating, const Progress& progress, const std::atomic<bool>* stop);
        Release checkOne(size_t entry, const std::map<std::string, Manifest>& installed, int64_t maxAge);
        void readCoreList(CoreList& list, std::vector<Package>& packages);
        std::string cacheFolder() const;

        std::string root_;
        std::string folder_;
        std::unique_ptr<Http> http_;
        Installer installer_;
        std::mutex mutex_;
    };
}

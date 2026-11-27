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

#include "Installer.h"
#include "Package.h"
#include <cstdint>
#include <string>
#include <vector>

// The emulators RetroFE can download, and RetroArch's cores. Only entries of
// this built-in list are ever installed; what they are (folder, program,
// where their downloads may come from) is decided here, in code, never by
// anything downloaded. The system and file extensions each program suits
// are in Manager/Emulators/KnownEmulators.
namespace Downloads
{
    class Http;

    struct Emulator
    {
        std::string id, name, folder, repo;
        std::string tagRule;     // a regex for release tags; group 1 is the version
        std::string assetRule;   // the asset's name, or (RetroArch) its URL; <version> and <tag> are filled in
        std::string archive;     // zip, 7z or sfx (a self-extracting 7z, never run)
        std::string topFolder;   // removed from every name in the archive, e.g. "RetroArch-Win64/"
        std::string program;
        std::vector<std::string> portableFiles;   // created empty if missing
        std::string launcher, arguments, description;   // launcher "" = none (RetroArch: per core)
    };

    const std::vector<Emulator>& emulators();
    // The entry whose package id ("emulator-<id>") this is, or nullptr
    const Emulator* findEmulator(const std::string& packageId);

    // The package an entry's GitHub release list offers: the highest version
    // whose tag matches the entry's rule, not a draft or pre-release, with the
    // entry's asset, a SHA-256 digest from GitHub and a download address on
    // GitHub's own release downloads (RetroArch: libretro's stable builds,
    // which publish no checksum: noChecksum). False only when text isn't a list.
    bool parseEmulatorReleases(const std::string& text, const Emulator& emulator,
                               std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error);

    // libretro's nightly Windows cores, one zip each, listed in .index-extended
    constexpr const char* coreIndexUrl = "https://buildbot.libretro.com/nightly/windows/x86_64/latest/.index-extended";
    std::string coreUrl(const std::string& core);   // core: the file stem, e.g. genesis_plus_gx
    bool isCoreName(const std::string& core);
    bool isCoreId(const std::string& id);           // core-<core>
    // "RetroArch <name>": printable ASCII, spaces collapsed, no launcher-forbidden
    // characters, at most 64 characters
    std::string coreLauncherName(const std::string& name);

    // Packages from the text of .index-extended ("<date> <crc32> <core>_libretro.dll.zip"
    // per line), named from RetroArch's info/<core>_libretro.info under root
    // when it's there. Lines that don't fit are skipped.
    void parseCoreIndex(const std::string& text, const std::string& root, std::vector<Package>& packages);

    struct CoreIndex
    {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        std::string error;          // set when there's no list at all
        bool fromCache = false;
    };
    // The core list, kept as cacheFolder/cores.index with its Last-Modified in
    // cores.modified; asks with If-Modified-Since, so a 304 uses the copy.
    void readCores(const std::string& cacheFolder, const std::string& root, Http* http, CoreIndex& index);

    // The safety rules for a package about to be downloaded or installed: ""
    // when it may be, otherwise why not. An emulator must be an entry of the
    // built-in list, going to its own folder, from its own release origin
    // (https://github.com/<repo>/releases/download/, or libretro's stable
    // RetroArch.7z), with GitHub's SHA-256 unless it is libretro's. A core
    // must be libretro's nightly zip of that core, with its CRC-32, going to
    // RetroArch/cores.
    std::string checkPackage(const Package& package);

    // After an install: creates launchers.windows/<launcher>.conf for an
    // emulator or core, with %RETROFE_PATH% paths, as Manage writes launchers,
    // unless a launcher of that name exists. Records it in the manifest
    // (written to downloadsFolder/installed/). Returns the end of the result
    // sentence: ", with the launcher X.", "; a launcher called X already
    // exists, so it was left as it is.", or a sentence starting with a space.
    std::string createEmulatorLauncher(const std::string& root, const std::string& downloadsFolder, Manifest& manifest);
    // Before or after a removal: removes the launcher the manifest recorded,
    // unless it changed or a system or game uses it (then it says which).
    // Returns sentences starting with a space, or "".
    std::string removeEmulatorLauncher(const std::string& root, const Manifest& manifest);
}

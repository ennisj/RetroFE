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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// What emulator downloads install: one emulator of the built-in list
// (Emulators.h), or one RetroArch core. See docs/design/emulator-downloads.md.
// Packages are made by this module's own code from the built-in list and
// the release lists it reads; nothing downloaded describes a package.
namespace Downloads
{
    struct PackageFile
    {
        std::string url;       // https://
        uint64_t size = 0;     // 0 if unknown
        std::string sha256;    // lowercase hex, or "" (libretro's downloads only)
        std::optional<uint32_t> crc32; // a core's unpacked DLL, from libretro's list
        std::string folder;    // under emulators/, '/' separated: "DuckStation", "RetroArch/cores"
    };

    struct Package
    {
        std::string id;        // emulator-<entry id> or core-<core>
        std::string name;
        std::string kind;      // emulator or core
        std::string version;
        std::string description;
        std::string system;    // a core's systemname, from RetroArch's info file
        std::vector<PackageFile> files;

        bool noChecksum = false; // libretro doesn't publish a checksum for RetroArch.7z

        uint64_t totalSize() const;
    };

    // Compares versions part by part, numbers numerically ("1.10" > "1.9"):
    // negative if a is older, 0 if the same, positive if newer
    int compareVersions(const std::string& a, const std::string& b);

    // Whether a location is an https:// URL
    bool isHttpsUrl(const std::string& location);

    // Whether an id is safe to use as a file name: letters, digits, . _ -,
    // not a Windows device name such as CON or COM1, and no trailing dot
    bool isValidId(const std::string& id);

    // Whether a relative path stays inside its folder and names an ordinary
    // file on every system: no "..", no absolute paths, no drive or stream
    // (":"), no device names, no characters Windows forbids, no segment
    // ending in a dot or space
    bool isSafeRelativePath(const std::string& path);

    // Whether a file name is a Windows device name (CON, NUL, COM1, "aux.txt"...)
    bool isReservedName(const std::string& name);
}

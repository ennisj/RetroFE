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
#include <map>
#include <optional>
#include <string>
#include <vector>

// Content the Catalog can install. See docs/design/catalog-sources.md.
namespace Catalog
{
    struct PackageFile
    {
        std::string url;       // absolute: http(s):// or a local path
        uint64_t size = 0;     // 0 if unknown
        std::string sha256;    // lowercase hex, or ""
        std::string md5;       // lowercase hex, or ""
        std::string sha256Url; // where the SHA-256 can be fetched when it isn't known yet (GitHub's <asset>.sha256)
        std::string target;    // root, collections, items, media, layouts, meta or emulators
        std::string path;      // folder under the target, "" for the target itself
        std::optional<bool> extract; // unset uses the kind and published name
    };

    struct Requirement
    {
        std::string id;
        std::string version;   // e.g. ">=1.0", or "" for any
    };

    struct Package
    {
        std::string source;    // name of the source it came from
        std::string id;
        std::string name;
        std::string kind;      // collection, layout, media, bundle, other, app or emulator
        std::string version;
        std::string description;
        std::map<std::string, std::string> artwork;   // type ("logo"...) -> absolute URL or path
        std::vector<Requirement> requirements;   // "requires" in the index
        std::vector<PackageFile> files;

        bool noChecksum = false; // libretro does not publish a checksum

        uint64_t totalSize() const;
    };

    // The install targets a package file may name
    bool isKnownTarget(const std::string& target);

    // Compares versions part by part, numbers numerically ("1.10" > "1.9"):
    // negative if a is older, 0 if the same, positive if newer
    int compareVersions(const std::string& a, const std::string& b);

    // Whether version satisfies a requirement such as ">=1.0", "1.2" or ""
    bool satisfies(const std::string& version, const std::string& requirement);
}

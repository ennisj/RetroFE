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
#include <string>
#include <vector>

// The catalog index: a JSON list of packages (docs/design/catalog-sources.md).
namespace Catalog
{
    // Whether a location is an http:// or https:// URL rather than a file path
    bool isUrl(const std::string& location);

    // A reference from inside an index or folder (e.g. "packs/a.zip"), made
    // absolute against the location the index was read from
    std::string resolveLocation(const std::string& base, const std::string& reference);

    // Whether an id is safe to use as a file name: letters, digits, . _ -,
    // not a Windows device name such as CON or COM1, and no trailing dot
    bool isValidId(const std::string& id);

    // Whether a relative path stays inside its target and names an ordinary
    // file on every system: no "..", no absolute paths, no drive or stream
    // (":"), no device names, no characters Windows forbids, no segment
    // ending in a dot or space
    bool isSafeRelativePath(const std::string& path);

    // Whether a file name is a Windows device name (CON, NUL, COM1, "aux.txt"...)
    bool isReservedName(const std::string& name);

    // Whether a URL is on this machine or a private network: localhost, a
    // single-label or .local name, or a private IPv4 address
    bool isLocalAddress(const std::string& url);

    // Where a package or its artwork may be fetched from: a file, an https://
    // URL, or plain http:// on the local network
    bool isAllowedLocation(const std::string& location);

    // Parses an index read from location. Packages that are invalid are left
    // out with a warning; false and error only when the index itself is unusable.
    bool parseIndex(const std::string& json, const std::string& location, const std::string& sourceName,
                    std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error);

    // Parses a single package description, as used by .package.json sidecar files
    bool parsePackage(const std::string& json, const std::string& location, const std::string& sourceName,
                      Package& package, std::string& error, std::vector<std::string>* warnings = nullptr);
}

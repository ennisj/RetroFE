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
#include <functional>
#include <string>
#include <vector>

// Reading a GitHub repository's list of releases, for the built-in emulator
// list. GitHub allows 60 unauthenticated requests an hour, so each entry's
// list is kept with its ETag under cache/<entry>/ and fetched at most once a
// minute. Unauthenticated 304 responses count; six requests per check allow
// about ten checks per hour. It does not ask while GitHub says the limit is
// reached (cache/rate-limit.json,
// shared by every entry).
namespace Downloads
{
    class Http;

    // Whether text is a GitHub repository as owner/name
    bool isValidRepository(const std::string& repo);

    // What a "sha256:<hex>" digest (or a .sha256 file) says the SHA-256 of
    // fileName is: "<hex>", sha256sum's "<hex>  <name>" or "<hex> *<name>",
    // "<name> <hex>", or BSD's "SHA256 (<name>) = <hex>". With several files
    // listed, the line naming fileName. "" if it doesn't say.
    std::string parseSha256File(const std::string& text, const std::string& fileName = "");

    // Turns the text of GitHub's list into packages; false and error when it isn't one
    using ReleaseParser = std::function<bool(const std::string& json, std::vector<Package>& packages,
                                             std::vector<std::string>& warnings, std::string& error)>;

    struct ReleaseList
    {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        std::string error;          // set when nothing could be read
        bool fromCache = false;     // GitHub couldn't be asked or answered; this is the last copy
        int64_t retryAt = 0;        // GitHub's limit is reached until then (seconds since 1970); 0 if it isn't
    };

    // Reads repo's releases into result, through the cache in
    // cacheFolder/<entry>/. now is seconds since 1970. http may be null: then
    // only the cache is read.
    void readGitHubReleases(const std::string& repo, const std::string& cacheFolder, const std::string& entry,
                            Http* http, int64_t now, const ReleaseParser& parse, ReleaseList& result, bool latestOnly = false);

    // How long a fetched list is used without asking again
    constexpr int64_t gitHubRecheckSeconds = 60;
}

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
#include "Sources.h"
#include <cstdint>
#include <string>
#include <vector>

// GitHub sources: a repository's releases, offered as RetroFE itself (a
// package of kind "app"). See docs/design/catalog-sources.md.
//
// Release assets are named RetroFE-<version>-<os>-<arch>.zip, e.g.
// RetroFE-2504.3-windows-x64.zip, and hold a retrofe/ folder (the program)
// and optionally layouts/<name>/ folders.
namespace Catalog
{
    class Http;

    // The repository "Add RetroFE from GitHub" adds. Change it here to follow another fork.
    constexpr const char* retrofeRepository = "ennisj/RetroFE";

    // The id of RetroFE itself; only GitHub sources may offer it
    constexpr const char* appPackageId = "retrofe";

    // Whether text is a GitHub repository as owner/name
    bool isValidRepository(const std::string& repo);

    // "owner/name" or a github.com address of a repository, as owner/name; "" if it's neither
    std::string parseRepository(const std::string& text);

    struct Platform
    {
        std::string os;     // windows, linux or macos
        std::string arch;   // x64, arm64 or x86
    };

    // The platform this RetroFE was built for
    Platform currentPlatform();

    // Which asset name is the package for platform: a .zip naming the system
    // and either this architecture or none, and not symbols, debug or source;
    // of equals, the shortest name. -1 if there isn't one.
    int pickAsset(const std::vector<std::string>& names, const Platform& platform);

    // Whether repo is RetroFE's own (retrofeRepository), which may offer RetroFE
    // without the source saying app = yes
    bool isOfficialRepository(const std::string& repo);

    // What a .sha256 file (or a "sha256:<hex>" digest) says the SHA-256 of
    // fileName is: "<hex>", sha256sum's "<hex>  <name>" or "<hex> *<name>",
    // "<name> <hex>", or BSD's "SHA256 (<name>) = <hex>". With several files
    // listed, the line naming fileName. "" if it doesn't say.
    std::string parseSha256File(const std::string& text, const std::string& fileName = "");

    // Turns GitHub's list of releases into the RetroFE package: the highest
    // version that isn't a draft (nor a pre-release, unless the source allows
    // them) and has an asset for platform. Only the official repository, or a
    // source with app = yes, offers it. The SHA-256 comes from the asset's
    // digest, or else sha256Url names the <asset>.sha256 beside it.
    bool parseReleases(const std::string& json, const SourceConfig& source, const Platform& platform,
                       std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error);

    // Reads a github source. GitHub allows 60 unauthenticated requests an hour,
    // so the list is fetched at most once a minute, asking only for changes
    // since the last copy (which don't count), and not at all while GitHub
    // says the limit is reached. now is seconds since 1970.
    void readGitHub(const SourceConfig& source, const std::string& cacheFolder, Http* http, int64_t now,
                    SourceContents& contents);

    // How long a fetched list is used without asking again
    constexpr int64_t gitHubRecheckSeconds = 60;
}

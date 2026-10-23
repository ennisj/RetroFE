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

// Where packages come from: catalog/sources.conf, and the adapters that read
// each kind of source into packages.
namespace Catalog
{
    class Http;

    struct SourceConfig
    {
        std::string name;
        std::string type;      // "index", "folder" or "github"
        std::string url;       // index: http(s) URL or file path; github: the releases API address, if not GitHub's
        std::string path;      // folder: the folder to scan
        std::string repo;      // github: owner/name
        bool prerelease = false;   // github: offer pre-releases too
        bool app = false;          // github: may offer RetroFE itself though it isn't the official repository
        bool enabled = true;
    };

    // Reads sources.conf; a missing file just means no sources
    bool loadSources(const std::string& file, std::vector<SourceConfig>& sources, std::string& error);

    // Writes sources.conf from scratch
    bool saveSources(const std::string& file, const std::vector<SourceConfig>& sources, std::string& error);

    // "" if a new source can be added next to the existing ones, otherwise why not
    std::string validateSource(const SourceConfig& source, const std::vector<SourceConfig>& existing);

    struct SourceContents
    {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        std::string error;         // set when nothing could be read
        bool fromCache = false;    // the source couldn't be reached; this is the last copy
    };

    // Reads a source's packages. Index and GitHub sources are cached in
    // cacheFolder, and the cache is used when they can't be fetched. http may
    // be null, in which case only local sources can be read.
    SourceContents readSource(const SourceConfig& source, const std::string& cacheFolder, Http* http);
}

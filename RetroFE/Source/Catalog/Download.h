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
#include "Package.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Fetches a package's files so the Installer can install them.
//
// Each file downloads to catalog/downloads/<id>/<n>-<key>.part, where the key
// changes whenever the file's URL, size or hash does, so a partial download is
// only ever continued from the same file. Once its size and hash check out it
// loses the .part. Files from folder sources are used where they are.
namespace Catalog
{
    // What to do with a partial download already on disk
    enum class Resume
    {
        Fresh,      // nothing usable: start from the beginning
        Continue,   // ask for the rest
        Complete,   // it's all there; only verify it
        Discard     // larger than the file should be: delete it and start again
    };

    // expected is 0 when the size isn't known
    Resume planResume(bool exists, uint64_t partSize, uint64_t expected);

    // Milliseconds to wait before retry number attempt (1 = first retry):
    // 2 s, doubling, at most 60 s
    int backoffDelay(int attempt);

    // Bytes the drive needs free: what's left to download, plus about as much
    // again for extracting (archives of ROMs and artwork barely compress),
    // plus a margin
    uint64_t spaceNeeded(uint64_t total, uint64_t alreadyDownloaded);

    class Downloader
    {
    public:
        // Bytes so far and the total for the whole package (0 if unknown).
        // Also called while waiting to retry. Return false to stop; the
        // partial downloads are kept, so the next fetch continues them.
        using Progress = std::function<bool(uint64_t received, uint64_t total)>;

        struct Result
        {
            std::vector<std::string> files;  // a local path per package file, in order
            bool cancelled = false;
            std::string error;
        };

        // http may be null, in which case only folder sources work
        Downloader(Http* http, const std::string& downloadsFolder);

        bool fetch(const Package& package, const Progress& progress, Result& result);

        // Deletes a package's downloads, finished or not
        void discard(const std::string& id);

        // For tests: replace waiting and the free space check
        std::function<void(int milliseconds)> sleep;
        std::function<bool(const std::string& folder, uint64_t& available)> freeSpace;
        int tries = 5;

    private:
        bool fetchFile(const PackageFile& file, const std::string& part, uint64_t done, uint64_t total,
                       const Progress& progress, Result& result);
        bool wait(int milliseconds, uint64_t received, uint64_t total, const Progress& progress);

        Http* http_;
        std::string folder_;
    };

    // Whether a file has the size and hash it should, with the reason if not
    bool verifyFile(const std::string& path, const PackageFile& file, std::string& error);

    // e.g. "512 bytes", "48.2 MB"
    std::string describeSize(uint64_t bytes);
}

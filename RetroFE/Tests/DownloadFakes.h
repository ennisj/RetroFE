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

// Fakes for the emulator download tests (DownloadsTests, DownloadScreensTests):
// an Http that serves fixed documents and files, and zip archives to serve.
// Nothing touches the network.

#include "../Source/Downloads/Http.h"
#include "minizip/zip.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace DownloadFakes
{
    // Writes a zip archive with these (name, content) entries
    inline bool makeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& entries, unsigned long attributes = 0)
    {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        zipFile zip = zipOpen64(path.c_str(), APPEND_STATUS_CREATE);
        if (!zip) return false;
        for (const auto& [name, content] : entries) {
            zip_fileinfo info{};
            info.external_fa = attributes;
            if (zipOpenNewFileInZip64(zip, name.c_str(), &info, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0) != ZIP_OK) {
                zipClose(zip, nullptr);
                return false;
            }
            zipWriteInFileInZip(zip, content.data(), static_cast<unsigned>(content.size()));
            zipCloseFileInZip(zip);
        }
        return zipClose(zip, nullptr) == ZIP_OK;
    }

    // Serves fixed documents; anything else fails, as if offline
    class FakeHttp : public Downloads::Http
    {
    public:
        std::map<std::string, std::string> documents;
        std::map<std::string, Response> responses;
        std::vector<std::vector<std::string>> sentHeaders;
        std::vector<std::string> requested;
        bool request(const std::string& url, const std::vector<std::string>& headers, Response& response) override
        {
            sentHeaders.push_back(headers);
            requested.push_back(url);
            auto found = responses.find(url);
            if (found != responses.end()) { response = found->second; return true; }
            return Downloads::Http::request(url, headers, response);
        }

        bool get(const std::string& url, std::string& body, std::string& error) override
        {
            auto it = documents.find(url);
            if (it == documents.end()) {
                error = "offline";
                return false;
            }
            body = it->second;
            return true;
        }

        bool download(const std::string&, const std::string&, uint64_t, const Progress&, Transfer& transfer) override
        {
            transfer.error = "not used";
            return false;
        }
    };

    // Serves files for download like a web server, with scripted failures,
    // and documents like FakeHttp
    class ServingHttp : public FakeHttp
    {
    public:
        std::map<std::string, std::string> files;
        std::vector<size_t> dropAfter;      // for each next request: lose the connection after this many bytes
        bool ranges = true;                 // whether resuming is supported
        std::map<std::string, std::string> etags;   // with one, a resume only continues the same version
        std::vector<uint64_t> offsets;      // the offset each request asked for

        bool download(const std::string& url, const std::string& file, uint64_t offset,
                      const Progress& progress, Transfer& transfer) override
        {
            std::string validator = transfer.validator;
            transfer = Transfer();
            offsets.push_back(offset);
            auto it = files.find(url);
            if (it == files.end()) {
                transfer.error = "The server answered 404";
                return false;
            }
            const std::string& body = it->second;
            uint64_t start = ranges ? std::min<uint64_t>(offset, body.size()) : 0;
            auto etag = etags.find(url);
            if (etag != etags.end()) {
                if (validator != etag->second) start = 0;   // If-Range didn't match: the whole new file
                transfer.validator = etag->second;
            }
            transfer.resumed = start > 0;
            std::string rest = body.substr(start);
            bool drop = !dropAfter.empty();
            if (drop) {
                rest = rest.substr(0, std::min(rest.size(), dropAfter.front()));
                dropAfter.erase(dropAfter.begin());
            }
            std::ofstream out(file, std::ios::binary | (transfer.resumed ? std::ios::app : std::ios::trunc));
            out << rest;
            out.close();
            if (progress && !progress(start + rest.size(), body.size())) {
                transfer.cancelled = true;
                transfer.error = "Cancelled";
                return false;
            }
            if (drop) {
                transfer.retry = true;
                transfer.error = "The connection was lost";
                return false;
            }
            return true;
        }
    };
}

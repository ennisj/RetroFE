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
#include <functional>
#include <memory>
#include <string>

// HTTP(S) for the Catalog. Windows uses WinHTTP, so no extra libraries are needed.
// An Http makes one request at a time; use one per thread.
namespace Catalog
{
    class Http
    {
    public:
        virtual ~Http() = default;

        // Called while downloading with bytes received so far and the total
        // (0 if unknown); return false to cancel
        using Progress = std::function<bool(uint64_t received, uint64_t total)>;

        // How a download went
        struct Transfer
        {
            bool resumed = false;       // the file was continued, not started again
            bool retry = false;         // the failure may go away if tried again
            bool cancelled = false;
            std::string error;
        };

        // Fetches a small document such as an index
        virtual bool get(const std::string& url, std::string& body, std::string& error) = 0;

        // Downloads url into file. With offset > 0 the file already holds that
        // many bytes and the rest is requested; if the server can't resume,
        // the file is started again and resumed is left false.
        virtual bool download(const std::string& url, const std::string& file, uint64_t offset,
                              const Progress& progress, Transfer& transfer) = 0;
    };

    // This platform's implementation, or nullptr where there isn't one yet
    std::unique_ptr<Http> createHttp();
}

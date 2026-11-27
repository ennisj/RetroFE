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
#include <map>
#include <memory>
#include <string>
#include <vector>

// HTTP(S) for emulator downloads. Windows uses WinHTTP, so no extra libraries
// are needed; Linux and macOS use libcurl, with the same behaviour: headers
// sent and read, ETag and Last-Modified kept for If-Range, downloads continued
// with Range, the same timeouts, downloads only over https://, and redirects
// followed only to https://. An Http makes one request at a time; use one per thread.
namespace Downloads
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
            // In: what the server said identifies the partial file's version (an
            // ETag or Last-Modified date), so it only continues the same file.
            // Out: what it says now, to keep with the partial file.
            std::string validator;
            bool resumed = false;       // the file was continued, not started again
            bool retry = false;         // the failure may go away if tried again
            bool cancelled = false;
            std::string error;
        };

        // Fetches a small document such as an index
        virtual bool get(const std::string& url, std::string& body, std::string& error) = 0;

        // What a server answered, whatever the status
        struct Response
        {
            int status = 0;
            std::string body;
            std::map<std::string, std::string> headers;   // names in lower case
            std::string error;                            // when there was no answer
        };

        // Fetches a small document, sending extra request headers ("Name: value")
        // and keeping the status and headers of any answer, e.g. a 304 or 403.
        // False only when no answer came. The default is get(), for Https that
        // don't need more.
        virtual bool request(const std::string& url, const std::vector<std::string>& headers, Response& response)
        {
            (void)headers;
            response = Response();
            if (!get(url, response.body, response.error)) {
                return false;
            }
            response.status = 200;
            return true;
        }

        // Downloads url into file. With offset > 0 the file already holds that
        // many bytes and the rest is requested; if the server can't resume,
        // the file is started again and resumed is left false.
        virtual bool download(const std::string& url, const std::string& file, uint64_t offset,
                              const Progress& progress, Transfer& transfer) = 0;
    };

    // This platform's implementation (WinHTTP, or libcurl), or nullptr if it
    // can't be set up
    std::unique_ptr<Http> createHttp();

    // The parts of the libcurl Http that don't need the network, so they're
    // tested on every system
    namespace HttpText
    {
        // "HTTP/1.1 206 Partial Content" or "HTTP/2 200": its status, or 0
        int statusOf(const std::string& line);
        // "Name: value" as a lower-case name and the value without surrounding
        // blanks; false for anything else (the status line, a blank line,
        // anything but printable ASCII)
        bool header(const std::string& line, std::string& name, std::string& value);
        // What a download asks for to continue at offset: "Range: bytes=<offset>-",
        // and "If-Range: <validator>" so only the same file is continued; nothing at 0
        std::vector<std::string> resumeHeaders(uint64_t offset, const std::string& validator);
        // A strong ETag, or else Last-Modified, from response headers (lower-case
        // names): what If-Range accepts. "" if neither, or it isn't printable ASCII
        std::string validator(const std::map<std::string, std::string>& headers);
        // Whether a redirect from url to location (absolute, or relative to url)
        // may be followed: only to https://
        bool redirectAllowed(const std::string& url, const std::string& location);

        // What a download's answer means, as WinHTTP's is read
        enum class Answer
        {
            Resume,     // 206 to a continued download: append
            Restart,    // 200: the whole file again
            Complete,   // 416 to a continued download: nothing is left to send
            Fail        // anything else
        };
        Answer answer(uint64_t offset, int status);
        // Whether a failed status may go away if tried again (408, 429, 5xx)
        bool retryable(int status);
    }
}

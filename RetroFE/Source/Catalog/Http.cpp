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
#include "Http.h"

#ifdef WIN32
#include <Windows.h>
#include <winhttp.h>
#include <fstream>
#include <vector>

namespace
{
    std::wstring widen(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int length = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), length);
        result.resize(length - 1);
        return result;
    }

    std::string describe(DWORD code)
    {
        switch (code) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "The server's name couldn't be found";
        case ERROR_WINHTTP_CANNOT_CONNECT:    return "Couldn't connect to the server";
        case ERROR_WINHTTP_TIMEOUT:           return "The server took too long to answer";
        case ERROR_WINHTTP_CONNECTION_ERROR:  return "The connection was lost";
        case ERROR_WINHTTP_SECURE_FAILURE:    return "The server's security certificate isn't valid";
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
        case ERROR_WINHTTP_INVALID_URL:       return "That isn't a valid address";
        default:                              return "Network error " + std::to_string(code);
        }
    }

    // Closes WinHTTP handles when they go out of scope
    struct Handle
    {
        HINTERNET handle = nullptr;
        explicit Handle(HINTERNET h = nullptr) : handle(h) {}
        ~Handle() { if (handle) WinHttpCloseHandle(handle); }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        operator HINTERNET() const { return handle; }
    };

    class WinHttp : public Catalog::Http
    {
    public:
        WinHttp()
        {
            // Automatic proxy needs Windows 8.1; fall back to the default proxy settings
            session_.handle = WinHttpOpen(L"RetroFE", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session_.handle) {
                session_.handle = WinHttpOpen(L"RetroFE", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            }
            if (session_.handle) {
                WinHttpSetTimeouts(session_, 15000, 15000, 30000, 30000);
            }
        }

        bool get(const std::string& url, std::string& body, std::string& error) override
        {
            body.clear();
            DWORD status = 0;
            uint64_t length = 0;
            Handle connection;
            Handle request;
            if (!open(url, "", connection, request, status, length, error)) {
                return false;
            }
            if (status != 200) {
                error = "The server answered " + std::to_string(status);
                return false;
            }
            const size_t limit = 64 * 1024 * 1024;   // indexes are small; don't let one fill memory
            bool failedReading = false;
            return read(request, [&](const char* data, size_t size) {
                if (body.size() + size > limit) {
                    error = "The index is too large";
                    return false;
                }
                body.append(data, size);
                return true;
            }, error, failedReading);
        }

        bool download(const std::string& url, const std::string& file, uint64_t offset,
                      const Progress& progress, Transfer& transfer) override
        {
            transfer = Transfer();
            DWORD status = 0;
            uint64_t length = 0;
            Handle connection;
            Handle request;
            std::string range = offset > 0 ? "Range: bytes=" + std::to_string(offset) + "-" : "";
            if (!open(url, range, connection, request, status, length, transfer.error)) {
                transfer.retry = retryable(failure_);
                return false;
            }

            uint64_t start = 0;
            if (offset > 0 && status == 206) {
                transfer.resumed = true;
                start = offset;
            }
            else if (offset > 0 && status == 416) {
                // Nothing left to send: the file is already complete
                transfer.resumed = true;
                return true;
            }
            else if (status != 200) {
                transfer.error = "The server answered " + std::to_string(status);
                transfer.retry = status == 408 || status == 429 || status >= 500;
                return false;
            }

            std::ofstream out(file, std::ios::binary | (transfer.resumed ? std::ios::app : std::ios::trunc));
            if (!out) {
                transfer.error = "Could not write " + file;
                return false;
            }
            uint64_t total = length > 0 ? start + length : 0;
            uint64_t received = start;
            bool failedReading = false;
            auto report = [&] {
                if (progress && !progress(received, total)) {
                    transfer.cancelled = true;
                    transfer.error = "Cancelled";
                    return false;
                }
                return true;
            };
            if (!report()) {
                return false;
            }
            bool ok = read(request, [&](const char* data, size_t size) {
                out.write(data, size);
                if (!out) {
                    transfer.error = "Could not write " + file + " (is the drive full?)";
                    return false;
                }
                received += size;
                return report();
            }, transfer.error, failedReading);
            out.close();
            if (ok && total > 0 && received < total) {
                // The connection closed early without WinHTTP calling it an error
                transfer.error = describe(ERROR_WINHTTP_CONNECTION_ERROR);
                failedReading = true;
                ok = false;
            }
            if (failedReading) {
                transfer.retry = true;
            }
            else if (ok && !out) {
                transfer.error = "Could not write " + file;
                ok = false;
            }
            return ok;
        }

    private:
        // Sends the request and reads the status and length. On failure,
        // failure_ holds the WinHTTP error code (0 if there wasn't one).
        bool open(const std::string& url, const std::string& header, Handle& connection, Handle& request,
                  DWORD& status, uint64_t& length, std::string& error)
        {
            failure_ = 0;
            auto fail = [&] {
                failure_ = GetLastError();
                error = describe(failure_);
                return false;
            };
            if (!session_.handle) {
                error = "Networking isn't available";
                return false;
            }
            std::wstring wideUrl = widen(url);
            URL_COMPONENTS parts{};
            parts.dwStructSize = sizeof(parts);
            parts.dwHostNameLength = static_cast<DWORD>(-1);
            parts.dwUrlPathLength = static_cast<DWORD>(-1);
            parts.dwExtraInfoLength = static_cast<DWORD>(-1);
            if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
                return fail();
            }
            std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
            std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
            if (path.empty()) path = L"/";
            bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

            connection.handle = WinHttpConnect(session_, host.c_str(), parts.nPort, 0);
            if (!connection.handle) {
                return fail();
            }
            request.handle = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
            if (!request.handle) {
                return fail();
            }
            std::wstring wideHeader = widen(header);
            if (!WinHttpSendRequest(request, wideHeader.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wideHeader.c_str(),
                                    wideHeader.empty() ? 0 : static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                || !WinHttpReceiveResponse(request, nullptr)) {
                return fail();
            }

            DWORD size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);

            // Read the length as text: as a number it's limited to 4 GB
            wchar_t text[32] = {};
            size = sizeof(text);
            length = 0;
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX)) {
                length = _wcstoui64(text, nullptr, 10);
            }
            return true;
        }

        // Network failures other than a bad address or certificate may clear up
        static bool retryable(DWORD code)
        {
            return code != ERROR_WINHTTP_INVALID_URL && code != ERROR_WINHTTP_UNRECOGNIZED_SCHEME
                && code != ERROR_WINHTTP_SECURE_FAILURE && code != 0;
        }

        // Passes the response body to sink in pieces; failedReading says the
        // network failed rather than sink
        template <typename Sink>
        bool read(HINTERNET request, Sink sink, std::string& error, bool& failedReading)
        {
            failedReading = false;
            std::vector<char> buffer(1 << 16);
            while (true) {
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
                    error = describe(GetLastError());
                    failedReading = true;
                    return false;
                }
                if (read == 0) {
                    return true;
                }
                if (!sink(buffer.data(), read)) {
                    return false;
                }
            }
        }

        Handle session_;
        DWORD failure_ = 0;
    };
}

namespace Catalog
{
    std::unique_ptr<Http> createHttp()
    {
        return std::make_unique<WinHttp>();
    }
}

#else

namespace Catalog
{
    // Linux and macOS get libcurl later; until then only folder sources work there
    std::unique_ptr<Http> createHttp()
    {
        return nullptr;
    }
}

#endif

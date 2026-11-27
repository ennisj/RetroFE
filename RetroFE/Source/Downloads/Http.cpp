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
#include <cctype>
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

    class WinHttp : public Downloads::Http
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

        bool request(const std::string& url, const std::vector<std::string>& headers, Response& response) override
        {
            response = Response();
            std::string header;
            for (const std::string& line : headers) {
                // A header can't carry another one with it
                if (line.find_first_of("\r\n") != std::string::npos) continue;
                header += (header.empty() ? "" : "\r\n") + line;
            }
            DWORD status = 0;
            uint64_t length = 0;
            Handle connection;
            Handle request;
            if (!open(url, header, connection, request, status, length, response.error)) {
                return false;
            }
            response.status = static_cast<int>(status);
            response.headers = this->headers(request);
            const size_t limit = 64 * 1024 * 1024;
            bool failedReading = false;
            bool ok = read(request, [&](const char* data, size_t size) {
                if (response.body.size() + size > limit) {
                    response.error = "The answer is too large";
                    return false;
                }
                response.body.append(data, size);
                return true;
            }, response.error, failedReading);
            if (!ok) {
                response.status = 0;
            }
            return ok;
        }

        bool download(const std::string& url, const std::string& file, uint64_t offset,
                      const Progress& progress, Transfer& transfer) override
        {
            std::string validator = transfer.validator;
            transfer = Transfer();
            DWORD status = 0;
            uint64_t length = 0;
            Handle connection;
            Handle request;
            // If-Range: the rest of the same file, or else the whole of the new one
            std::string headers;
            if (offset > 0) {
                headers = "Range: bytes=" + std::to_string(offset) + "-";
                if (!validator.empty()) {
                    headers += "\r\nIf-Range: " + validator;
                }
            }
            if (!open(url, headers, connection, request, status, length, transfer.error)) {
                transfer.retry = retryable(failure_);
                return false;
            }
            transfer.validator = this->validator(request);

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

        // A strong ETag, or else Last-Modified: what If-Range accepts
        static std::string validator(HINTERNET request)
        {
            for (DWORD query : { static_cast<DWORD>(WINHTTP_QUERY_ETAG), static_cast<DWORD>(WINHTTP_QUERY_LAST_MODIFIED) }) {
                wchar_t text[256] = {};
                DWORD size = sizeof(text);
                if (WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX)) {
                    std::wstring value(text, size / sizeof(wchar_t));
                    if (query == WINHTTP_QUERY_ETAG && value.rfind(L"W/", 0) == 0) continue;
                    std::string narrow;
                    for (wchar_t c : value) {
                        if (c < 0x20 || c > 0x7e) return "";   // headers are ASCII; anything else isn't worth sending back
                        narrow += static_cast<char>(c);
                    }
                    return narrow;
                }
            }
            return "";
        }

        // Every response header, names in lower case; ASCII only
        static std::map<std::string, std::string> headers(HINTERNET request)
        {
            std::map<std::string, std::string> result;
            DWORD size = 0;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
                return result;
            }
            std::wstring raw(size / sizeof(wchar_t) + 1, L'\0');
            if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
                return result;
            }
            raw.resize(size / sizeof(wchar_t));
            size_t start = 0;
            while (start < raw.size()) {
                size_t end = raw.find(L"\r\n", start);
                if (end == std::wstring::npos) end = raw.size();
                std::wstring line = raw.substr(start, end - start);
                start = end + 2;
                size_t colon = line.find(L':');
                if (colon == std::wstring::npos) continue;   // the status line
                std::string name;
                std::string value;
                bool ascii = true;
                for (size_t i = 0; i < line.size(); i++) {
                    wchar_t c = line[i];
                    if (c > 0x7e) { ascii = false; break; }
                    if (i < colon) name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    else if (i > colon) value += static_cast<char>(c);
                }
                if (!ascii) continue;
                size_t first = value.find_first_not_of(" \t");
                value = first == std::string::npos ? "" : value.substr(first);
                result[name] = value;
            }
            return result;
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

namespace Downloads
{
    std::unique_ptr<Http> createHttp()
    {
        return std::make_unique<WinHttp>();
    }
}

#else
#include "Package.h"
#include <curl/curl.h>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace
{
    std::string describe(CURLcode code)
    {
        switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:     return "The server's name couldn't be found";
        case CURLE_COULDNT_CONNECT:           return "Couldn't connect to the server";
        case CURLE_OPERATION_TIMEDOUT:        return "The server took too long to answer";
        case CURLE_PARTIAL_FILE:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_HTTP2:
        case CURLE_HTTP2_STREAM:              return "The connection was lost";
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_CERTPROBLEM:           return "The server's security certificate isn't valid";
        case CURLE_UNSUPPORTED_PROTOCOL:
        case CURLE_URL_MALFORMAT:
        case CURLE_TOO_MANY_REDIRECTS:        return "That isn't a valid address";
        default:                              return std::string("Network error: ") + curl_easy_strerror(code);
        }
    }

    // Network failures other than a bad address or certificate may clear up
    bool retryable(CURLcode code)
    {
        switch (code) {
        case CURLE_UNSUPPORTED_PROTOCOL:
        case CURLE_URL_MALFORMAT:
        case CURLE_TOO_MANY_REDIRECTS:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_WRITE_ERROR:
        case CURLE_ABORTED_BY_CALLBACK:
        case CURLE_OUT_OF_MEMORY:
            return false;
        default:
            return code != CURLE_OK;
        }
    }

    // One request as it goes: the answer so far, and where its body goes
    struct Exchange
    {
        CURL* curl = nullptr;
        int status = 0;
        std::map<std::string, std::string> headers;   // the latest answer's (after redirects, the last)
        bool redirectRefused = false;
        bool failed = false;                          // the body's sink or progress said stop
        std::function<bool(const char* data, size_t size)> body;
        std::function<bool()> progress;               // false cancels
    };

    // libcurl calls these from C: nothing may be thrown through it
    size_t onHeader(char* data, size_t size, size_t count, void* user)
    {
        auto& exchange = *static_cast<Exchange*>(user);
        const size_t length = size * count;
        try {
            std::string line(data, length);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            int status = Downloads::HttpText::statusOf(line);
            if (status > 0) {
                // A new answer: a redirect's, or the next after 100 Continue
                exchange.status = status;
                exchange.headers.clear();
                return length;
            }
            std::string name, value;
            if (!Downloads::HttpText::header(line, name, value)) return length;
            exchange.headers[name] = value;
            if (name == "location" && exchange.status >= 300 && exchange.status < 400) {
                char* current = nullptr;
                curl_easy_getinfo(exchange.curl, CURLINFO_EFFECTIVE_URL, &current);
                if (!Downloads::HttpText::redirectAllowed(current ? current : "", value)) {
                    exchange.redirectRefused = true;
                    return 0;   // stops the transfer
                }
            }
            return length;
        }
        catch (...) {
            exchange.failed = true;
            return 0;
        }
    }

    size_t onBody(char* data, size_t size, size_t count, void* user)
    {
        auto& exchange = *static_cast<Exchange*>(user);
        const size_t length = size * count;
        try {
            // A redirect's own body isn't the answer
            if (exchange.status >= 300 && exchange.status < 400 && exchange.headers.count("location")) return length;
            if (exchange.body && !exchange.body(data, length)) {
                exchange.failed = true;
                return 0;
            }
            return length;
        }
        catch (...) {
            exchange.failed = true;
            return 0;
        }
    }

    int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        auto& exchange = *static_cast<Exchange*>(user);
        try {
            if (exchange.progress && !exchange.progress()) {
                exchange.failed = true;
                return 1;
            }
            return 0;
        }
        catch (...) {
            exchange.failed = true;
            return 1;
        }
    }

    class CurlHttp : public Downloads::Http
    {
    public:
        CurlHttp() : curl_(curl_easy_init()) {}
        ~CurlHttp() override
        {
            if (curl_) curl_easy_cleanup(curl_);
        }
        CurlHttp(const CurlHttp&) = delete;
        CurlHttp& operator=(const CurlHttp&) = delete;

        bool ready() const { return curl_ != nullptr; }

        bool get(const std::string& url, std::string& body, std::string& error) override
        {
            body.clear();
            const size_t limit = 64 * 1024 * 1024;   // indexes are small; don't let one fill memory
            Exchange exchange;
            exchange.body = [&](const char* data, size_t size) {
                if (body.size() + size > limit) {
                    error = "The index is too large";
                    return false;
                }
                body.append(data, size);
                return true;
            };
            CURLcode code = perform(url, {}, false, exchange);
            if (code != CURLE_OK) {
                if (error.empty()) error = failure(code, exchange);
                body.clear();
                return false;
            }
            if (exchange.status != 200) {
                error = "The server answered " + std::to_string(exchange.status);
                body.clear();
                return false;
            }
            return true;
        }

        bool request(const std::string& url, const std::vector<std::string>& headers, Response& response) override
        {
            response = Response();
            std::vector<std::string> lines;
            for (const std::string& line : headers) {
                // A header can't carry another one with it
                if (line.find_first_of("\r\n") == std::string::npos) lines.push_back(line);
            }
            const size_t limit = 64 * 1024 * 1024;
            Exchange exchange;
            exchange.body = [&](const char* data, size_t size) {
                if (response.body.size() + size > limit) {
                    response.error = "The answer is too large";
                    return false;
                }
                response.body.append(data, size);
                return true;
            };
            CURLcode code = perform(url, lines, false, exchange);
            if (code != CURLE_OK) {
                if (response.error.empty()) response.error = failure(code, exchange);
                response.status = 0;
                response.body.clear();
                return false;
            }
            response.status = exchange.status;
            response.headers = exchange.headers;
            return true;
        }

        bool download(const std::string& url, const std::string& file, uint64_t offset,
                      const Progress& progress, Transfer& transfer) override
        {
            std::string validator = transfer.validator;
            transfer = Transfer();
            if (!Downloads::isHttpsUrl(url)) {
                transfer.error = "That isn't a valid address";
                return false;
            }
            std::ofstream out;
            bool opened = false;
            uint64_t received = 0;
            uint64_t total = 0;
            auto report = [&] {
                if (progress && !progress(received, total)) {
                    transfer.cancelled = true;
                    transfer.error = "Cancelled";
                    return false;
                }
                return true;
            };
            Exchange exchange;
            exchange.body = [&](const char* data, size_t size) {
                using Downloads::HttpText::Answer;
                if (!opened) {
                    // The answer decides: the rest of the same file, or the whole of a new one
                    Answer answer = Downloads::HttpText::answer(offset, exchange.status);
                    if (answer == Answer::Fail || answer == Answer::Complete) return true;   // an error page: not the file
                    transfer.resumed = answer == Answer::Resume;
                    out.open(file, std::ios::binary | (transfer.resumed ? std::ios::app : std::ios::trunc));
                    opened = true;
                    if (!out) {
                        transfer.error = "Could not write " + file;
                        return false;
                    }
                    const uint64_t start = transfer.resumed ? offset : 0;
                    uint64_t length = 0;
                    auto found = exchange.headers.find("content-length");
                    if (found != exchange.headers.end()) length = std::strtoull(found->second.c_str(), nullptr, 10);
                    total = length > 0 ? start + length : 0;
                    received = start;
                    if (!report()) return false;
                }
                out.write(data, static_cast<std::streamsize>(size));
                if (!out) {
                    transfer.error = "Could not write " + file + " (is the drive full?)";
                    return false;
                }
                received += size;
                return report();
            };
            // While waiting for data too, so a stop isn't held up
            exchange.progress = [&] {
                if (!progress) return true;
                if (!progress(opened ? received : offset, opened ? total : 0)) {
                    transfer.cancelled = true;
                    transfer.error = "Cancelled";
                    return false;
                }
                return true;
            };
            CURLcode code = perform(url, Downloads::HttpText::resumeHeaders(offset, validator), true, exchange);
            transfer.validator = Downloads::HttpText::validator(exchange.headers);
            if (out.is_open()) out.close();
            if (transfer.cancelled) return false;
            if (!transfer.error.empty()) return false;   // couldn't write
            if (code != CURLE_OK) {
                transfer.error = failure(code, exchange);
                transfer.retry = !exchange.redirectRefused && retryable(code);
                return false;
            }
            using Downloads::HttpText::Answer;
            Answer answer = Downloads::HttpText::answer(offset, exchange.status);
            if (answer == Answer::Complete) {
                // Nothing left to send: the file is already complete
                transfer.resumed = true;
                return true;
            }
            if (answer == Answer::Fail) {
                transfer.error = "The server answered " + std::to_string(exchange.status);
                transfer.retry = Downloads::HttpText::retryable(exchange.status);
                return false;
            }
            if (!opened) {
                // An empty answer: the file is empty, or left as it was
                transfer.resumed = answer == Answer::Resume;
                std::ofstream empty(file, std::ios::binary | (transfer.resumed ? std::ios::app : std::ios::trunc));
                if (!empty) {
                    transfer.error = "Could not write " + file;
                    return false;
                }
            }
            if (total > 0 && received < total) {
                // The connection closed early without libcurl calling it an error
                transfer.error = describe(CURLE_PARTIAL_FILE);
                transfer.retry = true;
                return false;
            }
            if (opened && !out) {
                transfer.error = "Could not write " + file;
                return false;
            }
            return true;
        }

    private:
        static std::string failure(CURLcode code, const Exchange& exchange)
        {
            return exchange.redirectRefused ? "The server sent the download somewhere other than https://" : describe(code);
        }

        // Sends one request, following redirects only to https://. Downloads
        // are https:// only; the index requests may also be http://.
        CURLcode perform(const std::string& url, const std::vector<std::string>& headers, bool download, Exchange& exchange)
        {
            if (!curl_) return CURLE_FAILED_INIT;
            curl_easy_reset(curl_);
            exchange.curl = curl_;
            curl_slist* list = nullptr;
            for (const std::string& line : headers) {
                curl_slist* longer = curl_slist_append(list, line.c_str());
                if (!longer) {
                    curl_slist_free_all(list);
                    return CURLE_OUT_OF_MEMORY;
                }
                list = longer;
            }
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl_, CURLOPT_USERAGENT, "RetroFE");
            curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 10L);
#if LIBCURL_VERSION_NUM >= 0x075500
            curl_easy_setopt(curl_, CURLOPT_PROTOCOLS_STR, download ? "https" : "http,https");
            curl_easy_setopt(curl_, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
            curl_easy_setopt(curl_, CURLOPT_PROTOCOLS, download ? static_cast<long>(CURLPROTO_HTTPS) : static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
            curl_easy_setopt(curl_, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
            // As WinHTTP's: 15 s to connect, and give up when nothing arrives for 30 s
            curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl_, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl_, CURLOPT_LOW_SPEED_TIME, 30L);
            curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, onHeader);
            curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &exchange);
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, onBody);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &exchange);
            curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, onProgress);
            curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, &exchange);
            if (list) curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, list);
            CURLcode code = curl_easy_perform(curl_);
            curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, nullptr);
            curl_slist_free_all(list);
            return code;
        }

        CURL* curl_ = nullptr;
    };
}

namespace Downloads
{
    std::unique_ptr<Http> createHttp()
    {
        static std::once_flag once;
        static bool initialised = false;
        std::call_once(once, [] { initialised = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; });
        if (!initialised) return nullptr;
        auto http = std::make_unique<CurlHttp>();
        if (!http->ready()) return nullptr;
        return http;
    }
}

#endif

// The libcurl Http's text handling, built everywhere so it's tested everywhere
#include "Package.h"
#include <algorithm>
#include <cctype>
#include <cstddef>

namespace Downloads::HttpText
{
    namespace
    {
        bool printable(const std::string& text)
        {
            return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 0x20 && c <= 0x7e; });
        }

        std::string trimmed(const std::string& text)
        {
            size_t first = text.find_first_not_of(" \t");
            if (first == std::string::npos) return "";
            size_t last = text.find_last_not_of(" \t");
            return text.substr(first, last - first + 1);
        }
    }

    int statusOf(const std::string& line)
    {
        if (line.rfind("HTTP/", 0) != 0) return 0;
        size_t space = line.find(' ');
        if (space == std::string::npos || space + 4 > line.size()) return 0;
        int status = 0;
        for (size_t i = space + 1; i < space + 4; i++) {
            if (!std::isdigit(static_cast<unsigned char>(line[i]))) return 0;
            status = status * 10 + (line[i] - '0');
        }
        if (space + 4 < line.size() && line[space + 4] != ' ') return 0;
        return status >= 100 ? status : 0;
    }

    bool header(const std::string& line, std::string& name, std::string& value)
    {
        size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0 || !printable(line)) return false;
        name = line.substr(0, colon);
        if (name.find_first_of(" \t") != std::string::npos) return false;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        value = trimmed(line.substr(colon + 1));
        return true;
    }

    std::vector<std::string> resumeHeaders(uint64_t offset, const std::string& validator)
    {
        std::vector<std::string> headers;
        if (offset == 0) return headers;
        headers.push_back("Range: bytes=" + std::to_string(offset) + "-");
        if (!validator.empty() && printable(validator)) headers.push_back("If-Range: " + validator);
        return headers;
    }

    std::string validator(const std::map<std::string, std::string>& headers)
    {
        auto etag = headers.find("etag");
        if (etag != headers.end() && !etag->second.empty() && etag->second.rfind("W/", 0) != 0) {
            return printable(etag->second) ? etag->second : "";
        }
        auto modified = headers.find("last-modified");
        if (modified != headers.end() && printable(modified->second)) return modified->second;
        return "";
    }

    bool redirectAllowed(const std::string& url, const std::string& location)
    {
        std::string target = trimmed(location);
        if (target.empty() || !printable(target)) return false;
        // An absolute address starts with a scheme ("https:"); it must be https
        size_t colon = target.find(':');
        size_t slash = target.find_first_of("/?#");
        bool absolute = colon != std::string::npos && colon > 0 && (slash == std::string::npos || colon < slash)
            && std::isalpha(static_cast<unsigned char>(target[0]))
            && std::all_of(target.begin(), target.begin() + static_cast<std::ptrdiff_t>(colon), [](unsigned char c) {
                   return std::isalnum(c) || c == '+' || c == '-' || c == '.';
               });
        if (absolute) return isHttpsUrl(target);
        // Relative ("/path", "//host/path", "file"): it stays on the scheme it came from
        return isHttpsUrl(url);
    }

    Answer answer(uint64_t offset, int status)
    {
        if (offset > 0 && status == 206) return Answer::Resume;
        if (offset > 0 && status == 416) return Answer::Complete;
        if (status == 200) return Answer::Restart;
        return Answer::Fail;
    }

    bool retryable(int status)
    {
        return status == 408 || status == 429 || status >= 500;
    }
}

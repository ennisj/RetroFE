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
#include "GitHub.h"
#include "Http.h"
#include "JsonFields.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
using nlohmann::json;
namespace J = Downloads::JsonFields;

namespace
{
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool readFile(const fs::path& path, std::string& content)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return true;
    }

    bool writeFile(const fs::path& path, const std::string& content)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path temp = path;
        temp += ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            file << content;
            if (!file) return false;
        }
        fs::rename(temp, path, ec);
        if (ec) {
            fs::remove(temp, ec);
            return false;
        }
        return true;
    }

    std::string clock(int64_t when)
    {
        std::time_t time = static_cast<std::time_t>(when);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        char text[16];
        std::strftime(text, sizeof(text), "%H:%M", &local);
        return text;
    }

    // What's remembered between fetches, next to the cached list
    struct FetchState
    {
        std::string etag;
        int64_t checked = 0;      // when GitHub last answered
        int64_t blockedUntil = 0; // GitHub's limit is reached until then
    };

    FetchState readState(const fs::path& file)
    {
        FetchState state;
        std::string text;
        if (!readFile(file, text)) return state;
        json document = json::parse(text, nullptr, false);
        state.etag = J::text(document, "etag");
        state.checked = J::integer(document, "checked");
        state.blockedUntil = J::integer(document, "blockedUntil");
        return state;
    }

    void writeState(const fs::path& file, const FetchState& state)
    {
        writeFile(file, json{ { "etag", state.etag }, { "checked", state.checked }, { "blockedUntil", state.blockedUntil } }.dump(2));
    }

    int64_t number(const std::map<std::string, std::string>& headers, const char* name)
    {
        auto it = headers.find(name);
        if (it == headers.end() || it->second.empty()
            || it->second.find_first_not_of("0123456789") != std::string::npos || it->second.size() > 15) {
            return -1;
        }
        return std::stoll(it->second);
    }
}

namespace Downloads
{

bool isValidRepository(const std::string& repo)
{
    size_t slash = repo.find('/');
    if (slash == std::string::npos || repo.find('/', slash + 1) != std::string::npos) {
        return false;
    }
    std::string owner = repo.substr(0, slash);
    std::string name = repo.substr(slash + 1);
    // GitHub's own rules: owners are letters, digits and inner hyphens; names add . and _
    bool ownerOk = !owner.empty() && owner.size() <= 39 && owner.front() != '-' && owner.back() != '-'
        && std::all_of(owner.begin(), owner.end(), [](unsigned char c) { return std::isalnum(c) != 0 || c == '-'; });
    bool nameOk = !name.empty() && name.size() <= 100 && name != "." && name != ".."
        && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_'; });
    return ownerOk && nameOk;
}

std::string parseSha256File(const std::string& text, const std::string& fileName)
{
    // Each line: the 64-hex-digit word in it, wherever it is
    std::string first;
    int lines = 0;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        std::string hex;
        std::string word;
        for (char c : line + " ") {
            if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '=' || c == '*') {
                std::string candidate = lower(word);
                if (candidate.rfind("sha256:", 0) == 0) candidate = candidate.substr(7);
                if (hex.empty() && candidate.size() == 64 && candidate.find_first_not_of("0123456789abcdef") == std::string::npos) {
                    hex = candidate;
                }
                word.clear();
            }
            else {
                word += c;
            }
        }
        if (hex.empty()) continue;
        lines++;
        if (first.empty()) first = hex;
        if (!fileName.empty() && lower(line).find(lower(fileName)) != std::string::npos) return hex;
    }
    // One hash is the file's; of several, only the one naming it
    return lines == 1 ? first : "";
}

void readGitHubReleases(const std::string& repo, const std::string& cacheFolder, const std::string& entry,
                        Http* http, int64_t now, const ReleaseParser& parse, ReleaseList& result, bool latestOnly, int64_t maxAge)
{
    result = ReleaseList();
    if (!isValidRepository(repo) || !isValidId(entry)) {
        result.error = "\"" + repo + "\" isn't a GitHub repository (owner/name)";
        return;
    }
    std::string url = "https://api.github.com/repos/" + repo + (latestOnly ? "/releases/latest" : "/releases?per_page=30");

    fs::path folder = fs::path(cacheFolder) / entry;
    fs::path cacheFile = folder / "releases.json";
    fs::path stateFile = folder / "state.json";
    FetchState state = readState(stateFile);
    // One limit for every entry: GitHub counts requests per address, not per repository
    fs::path limitFile = fs::path(cacheFolder) / "rate-limit.json";
    FetchState limit = readState(limitFile);
    state.blockedUntil = std::max(state.blockedUntil, limit.blockedUntil);
    std::string cached;
    bool haveCache = readFile(cacheFile, cached);

    // The last copy; problem says why it's used, or is "" when it's simply recent enough
    auto useCache = [&](const std::string& problem) {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        std::string error;
        if (haveCache && parse(cached, packages, warnings, error)) {
            result.packages = packages;
            result.checked = state.checked;
            result.warnings = warnings;
            if (!problem.empty()) {
                result.fromCache = true;
                result.warnings.insert(result.warnings.begin(), "Showing the last copy: " + problem);
            }
            return;
        }
        result.error = problem.empty() ? error : problem;
    };
    auto limited = [&] {
        result.retryAt = state.blockedUntil;
        useCache("GitHub's limit of 60 checks an hour has been reached; RetroFE checks again after " + clock(state.blockedUntil));
    };

    if (now < state.blockedUntil) {
        limited();
        return;
    }
    if (haveCache && now >= state.checked && now - state.checked < maxAge) {
        useCache("");
        return;
    }
    if (!http) {
        useCache("Downloading isn't supported on this system yet");
        return;
    }

    std::vector<std::string> headers = { "Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28" };
    // Ask for changes using the cached ETag; even a 304 counts against the limit.
    if (haveCache && !state.etag.empty()) {
        headers.push_back("If-None-Match: " + state.etag);
    }
    Http::Response response;
    if (!http->request(url, headers, response)) {
        useCache(response.error.empty() ? "GitHub couldn't be reached" : response.error);
        return;
    }

    if (response.status == 304 && haveCache) {
        state.checked = now;
        writeState(stateFile, state);
        useCache("");
        return;
    }
    if (response.status == 200) {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        std::string error;
        if (!parse(response.body, packages, warnings, error)) {
            useCache(error);
            return;
        }
        writeFile(cacheFile, response.body);
        auto etag = response.headers.find("etag");
        state.etag = etag != response.headers.end() && etag->second.find_first_of("\r\n") == std::string::npos ? etag->second : "";
        state.checked = now;
        state.blockedUntil = 0;
        writeState(stateFile, state);
        result.packages = packages;
        result.checked = state.checked;
        result.warnings = warnings;
        return;
    }

    int64_t retryAfter = number(response.headers, "retry-after");
    if (response.status == 429 || response.headers.count("retry-after") || (response.status == 403 && number(response.headers, "x-ratelimit-remaining") == 0)) {
        // Don't ask again until GitHub says to (or for a while, if it doesn't say)
        int64_t reset = number(response.headers, "x-ratelimit-reset");
        int64_t until = retryAfter >= 0 ? now + retryAfter : reset > now ? reset : now + 15 * 60;
        state.blockedUntil = std::min(until, now + 2 * 3600);
        writeState(stateFile, state);
        FetchState shared;
        shared.blockedUntil = state.blockedUntil;
        writeState(limitFile, shared);
        limited();
        return;
    }
    if (response.status == 404) {
        useCache("GitHub has no public repository " + repo);
        return;
    }
    useCache("GitHub answered " + std::to_string(response.status));
}

}

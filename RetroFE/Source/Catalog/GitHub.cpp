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
#include "Index.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string stringField(const json& object, const char* name)
    {
        auto it = object.find(name);
        return it != object.end() && it->is_string() ? it->get<std::string>() : "";
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

    // "v2504.3" -> "2504.3"; "" if the tag isn't a plain version
    std::string versionFromTag(const std::string& tag)
    {
        std::string version = tag;
        if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) {
            version.erase(0, 1);
        }
        if (version.empty() || version.size() > 64 || !std::isdigit(static_cast<unsigned char>(version[0]))) {
            return "";
        }
        bool plain = std::all_of(version.begin(), version.end(), [](unsigned char c) {
            return std::isalnum(c) != 0 || c == '.' || c == '-' || c == '+' || c == '_';
        });
        return plain ? version : "";
    }

    // Release notes as a description: no carriage returns, trimmed, and not endless
    std::string describe(const std::string& notes)
    {
        std::string text;
        for (char c : notes) {
            if (c != '\r') text += c;
        }
        size_t start = text.find_first_not_of(" \t\n");
        if (start == std::string::npos) return "";
        size_t end = text.find_last_not_of(" \t\n");
        text = text.substr(start, end - start + 1);
        const size_t limit = 1500;
        if (text.size() > limit) {
            size_t cut = limit;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) cut--;   // not inside a UTF-8 character
            text = text.substr(0, cut) + "...";
        }
        return text;
    }

    std::vector<std::string> tokens(std::string name)
    {
        // Spellings of the architectures that would otherwise split apart
        for (const auto& [from, to] : { std::pair<const char*, const char*>{ "x86_64", "x64" }, { "x86-64", "x64" } }) {
            size_t at;
            while ((at = name.find(from)) != std::string::npos) name.replace(at, std::string(from).size(), to);
        }
        std::vector<std::string> result;
        std::string token;
        for (char c : name + "-") {
            if (c == '-' || c == '_' || c == '.' || c == ' ') {
                if (!token.empty()) result.push_back(token);
                token.clear();
            }
            else {
                token += c;
            }
        }
        return result;
    }

    std::string osOf(const std::string& token)
    {
        if (token == "windows" || token == "win" || token == "win64" || token == "win32") return "windows";
        if (token == "macos" || token == "osx" || token == "mac" || token == "darwin") return "macos";
        if (token == "linux") return "linux";
        return "";
    }

    std::string archOf(const std::string& token)
    {
        if (token == "x64" || token == "amd64" || token == "win64") return "x64";
        if (token == "arm64" || token == "aarch64") return "arm64";
        if (token == "x86" || token == "win32" || token == "i386" || token == "i686") return "x86";
        return "";
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
        if (!document.is_object()) return state;
        state.etag = document.value("etag", "");
        state.checked = document.value("checked", int64_t{ 0 });
        state.blockedUntil = document.value("blockedUntil", int64_t{ 0 });
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

namespace Catalog
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

std::string parseRepository(const std::string& text)
{
    std::string repo = text;
    size_t start = repo.find_first_not_of(" \t");
    size_t end = repo.find_last_not_of(" \t");
    repo = start == std::string::npos ? "" : repo.substr(start, end - start + 1);
    for (const char* prefix : { "https://github.com/", "http://github.com/", "https://www.github.com/", "github.com/" }) {
        if (lower(repo).rfind(prefix, 0) == 0) {
            // owner/name, ignoring anything after it (/releases, ?tab=...)
            repo = repo.substr(std::string(prefix).size());
            repo = repo.substr(0, repo.find_first_of("?#"));
            size_t first = repo.find('/');
            if (first != std::string::npos && repo.find('/', first + 1) != std::string::npos) {
                repo.resize(repo.find('/', first + 1));
            }
            if (repo.size() > 4 && lower(repo.substr(repo.size() - 4)) == ".git") repo.resize(repo.size() - 4);
            break;
        }
    }
    return isValidRepository(repo) ? repo : "";
}

Platform currentPlatform()
{
    Platform platform;
#if defined(_WIN32)
    platform.os = "windows";
#elif defined(__APPLE__)
    platform.os = "macos";
#else
    platform.os = "linux";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
    platform.arch = "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    platform.arch = "x86";
#else
    platform.arch = "x64";
#endif
    return platform;
}

int pickAsset(const std::vector<std::string>& names, const Platform& platform)
{
    int best = -1;
    int bestScore = 0;
    for (size_t i = 0; i < names.size(); i++) {
        std::string name = lower(names[i]);
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".zip") != 0) continue;
        bool os = false;
        std::string arch;
        for (const std::string& token : tokens(name.substr(0, name.size() - 4))) {
            if (osOf(token) == platform.os) os = true;
            std::string a = archOf(token);
            if (!a.empty() && (arch.empty() || a == platform.arch)) arch = a;
        }
        if (!os || (!arch.empty() && arch != platform.arch)) continue;
        int score = arch.empty() ? 1 : 2;   // naming this architecture beats naming none
        if (score > bestScore) {
            best = static_cast<int>(i);
            bestScore = score;
        }
    }
    return best;
}

bool parseReleases(const std::string& text, const SourceConfig& source, const Platform& platform,
                   std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error)
{
    json document = json::parse(text, nullptr, false);
    if (document.is_discarded()) {
        error = "GitHub's answer isn't valid JSON";
        return false;
    }
    if (!document.is_array()) {
        std::string message = document.is_object() ? stringField(document, "message") : "";
        error = message.empty() ? "GitHub's answer isn't a list of releases" : "GitHub says: " + message;
        return false;
    }

    bool found = false;
    Package best;
    for (const json& release : document) {
        if (!release.is_object()) continue;
        if (release.value("draft", false)) continue;
        bool prerelease = release.value("prerelease", false);
        if (prerelease && !source.prerelease) continue;
        std::string tag = stringField(release, "tag_name");
        std::string version = versionFromTag(tag);
        if (version.empty()) {
            warnings.push_back("release " + tag + ": its tag isn't a version such as v2504.3");
            continue;
        }

        auto assets = release.find("assets");
        if (assets == release.end() || !assets->is_array()) continue;
        std::vector<std::string> names;
        std::vector<const json*> entries;
        for (const json& asset : *assets) {
            // Assets still being uploaded aren't ready
            if (!asset.is_object() || (asset.contains("state") && stringField(asset, "state") != "uploaded")) continue;
            names.push_back(stringField(asset, "name"));
            entries.push_back(&asset);
        }
        int pick = pickAsset(names, platform);
        if (pick < 0) continue;
        const json& asset = *entries[pick];

        PackageFile file;
        file.url = stringField(asset, "browser_download_url");
        if (!isUrl(file.url) || !isAllowedLocation(file.url)) {
            warnings.push_back("release " + tag + ": " + names[pick] + " has no https:// download address");
            continue;
        }
        if (auto size = asset.find("size"); size != asset.end() && size->is_number_unsigned()) {
            file.size = size->get<uint64_t>();
        }
        std::string digest = lower(stringField(asset, "digest"));
        if (digest.rfind("sha256:", 0) == 0) {
            std::string hex = digest.substr(7);
            if (hex.size() == 64 && hex.find_first_not_of("0123456789abcdef") == std::string::npos) {
                file.sha256 = hex;
            }
        }
        file.target = "root";

        if (found && compareVersions(version, best.version) <= 0) continue;
        found = true;
        best = Package();
        best.source = source.name;
        best.id = appPackageId;
        best.name = "RetroFE";
        best.kind = "app";
        best.version = version;
        best.description = describe(stringField(release, "body"));
        best.files = { file };
    }

    if (found) {
        packages.push_back(best);
    }
    else {
        warnings.push_back("No release of " + source.repo + " has a package for " + platform.os + "-" + platform.arch + " yet");
    }
    return true;
}

void readGitHub(const SourceConfig& source, const std::string& cacheFolder, Http* http, int64_t now, SourceContents& contents)
{
    if (!isValidRepository(source.repo)) {
        contents.error = "\"" + source.repo + "\" isn't a GitHub repository (owner/name)";
        return;
    }
    std::string url = source.url.empty() ? "https://api.github.com/repos/" + source.repo + "/releases?per_page=30" : source.url;
    if (!isUrl(url) || !isAllowedLocation(url)) {
        contents.error = "The releases address must be https://";
        return;
    }

    fs::path folder = fs::path(cacheFolder) / source.name;
    fs::path cacheFile = folder / "releases.json";
    fs::path stateFile = folder / "state.json";
    FetchState state = readState(stateFile);
    std::string cached;
    bool haveCache = readFile(cacheFile, cached);
    Platform platform = currentPlatform();

    // The last copy; problem says why it's used, or is "" when it's simply recent enough
    auto useCache = [&](const std::string& problem) {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        std::string error;
        if (haveCache && parseReleases(cached, source, platform, packages, warnings, error)) {
            contents.packages = packages;
            contents.warnings = warnings;
            if (!problem.empty()) {
                contents.fromCache = true;
                contents.warnings.insert(contents.warnings.begin(), "Showing the last copy: " + problem);
            }
            return;
        }
        contents.error = problem.empty() ? error : problem;
    };

    if (now < state.blockedUntil) {
        useCache("GitHub's limit of 60 checks an hour has been reached; RetroFE checks again after " + clock(state.blockedUntil));
        return;
    }
    if (haveCache && now >= state.checked && now - state.checked < gitHubRecheckSeconds) {
        useCache("");
        return;
    }
    if (!http) {
        useCache("Downloading isn't supported on this system yet");
        return;
    }

    std::vector<std::string> headers = { "Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28" };
    // Asking only for changes: an unchanged list costs nothing against the limit
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
        if (!parseReleases(response.body, source, platform, packages, warnings, error)) {
            useCache(error);
            return;
        }
        writeFile(cacheFile, response.body);
        auto etag = response.headers.find("etag");
        state.etag = etag != response.headers.end() && etag->second.find_first_of("\r\n") == std::string::npos ? etag->second : "";
        state.checked = now;
        state.blockedUntil = 0;
        writeState(stateFile, state);
        contents.packages = packages;
        contents.warnings = warnings;
        return;
    }

    int64_t remaining = number(response.headers, "x-ratelimit-remaining");
    int64_t retryAfter = number(response.headers, "retry-after");
    if (response.status == 429 || (response.status == 403 && (remaining == 0 || retryAfter >= 0))) {
        // Don't ask again until GitHub says to (or for a while, if it doesn't say)
        int64_t reset = number(response.headers, "x-ratelimit-reset");
        int64_t until = retryAfter >= 0 ? now + retryAfter : reset > now ? reset : now + 15 * 60;
        state.blockedUntil = std::min(until, now + 2 * 3600);
        writeState(stateFile, state);
        useCache("GitHub's limit of 60 checks an hour has been reached; RetroFE checks again after " + clock(state.blockedUntil));
        return;
    }
    if (response.status == 404) {
        useCache("GitHub has no public repository " + source.repo);
        return;
    }
    useCache("GitHub answered " + std::to_string(response.status));
}

}

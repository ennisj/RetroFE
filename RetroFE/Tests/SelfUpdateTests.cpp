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

// Tests for GitHub sources and RetroFE updating itself.

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "TestSupport.h"
#include "../Source/Catalog/GitHub.h"
#include "../Source/Catalog/Hash.h"
#include "../Source/Catalog/Http.h"
#include "../Source/Catalog/Index.h"
#include "../Source/Catalog/Installer.h"
#include "../Source/Catalog/SelfUpdate.h"
#include "../Source/Catalog/Service.h"
#include "../Source/Catalog/Sources.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Manager/ManagerModel.h"
#include "minizip/zip.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace Catalog;
using nlohmann::json;

namespace
{
    bool makeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& entries)
    {
        fs::create_directories(fs::path(path).parent_path());
        zipFile zip = zipOpen64(path.c_str(), APPEND_STATUS_CREATE);
        if (!zip) return false;
        for (const auto& [name, content] : entries) {
            zip_fileinfo info{};
            if (zipOpenNewFileInZip64(zip, name.c_str(), &info, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0) != ZIP_OK) {
                zipClose(zip, nullptr);
                return false;
            }
            zipWriteInFileInZip(zip, content.data(), static_cast<unsigned>(content.size()));
            zipCloseFileInZip(zip);
        }
        return zipClose(zip, nullptr) == ZIP_OK;
    }

    std::string sha256Of(const std::string& text)
    {
        Sha256 hash;
        hash.update(text.data(), text.size());
        return hash.hex();
    }

    bool contains(const std::vector<std::string>& lines, const std::string& text)
    {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
    }

    // Answers like GitHub's API with scripted responses, and serves downloads
    class GitHubHttp : public Http
    {
    public:
        std::vector<Response> answers;                 // for each next request(); none left means offline
        std::vector<std::vector<std::string>> sent;    // the headers of each request()
        std::map<std::string, std::string> files;      // downloads by URL
        std::map<std::string, std::string> documents;  // get() by URL

        bool get(const std::string& url, std::string& body, std::string& error) override
        {
            auto it = documents.find(url);
            if (it == documents.end()) {
                error = "The server answered 404";
                return false;
            }
            body = it->second;
            return true;
        }

        bool request(const std::string&, const std::vector<std::string>& headers, Response& response) override
        {
            sent.push_back(headers);
            response = Response();
            if (answers.empty()) {
                response.error = "offline";
                return false;
            }
            response = answers.front();
            answers.erase(answers.begin());
            return true;
        }

        bool download(const std::string& url, const std::string& file, uint64_t, const Progress& progress, Transfer& transfer) override
        {
            transfer = Transfer();
            auto it = files.find(url);
            if (it == files.end()) {
                transfer.error = "The server answered 404";
                return false;
            }
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << it->second;
            out.close();
            if (progress) progress(it->second.size(), it->second.size());
            return true;
        }
    };

    // An Http whose every request fails in a way nobody expected
    class ThrowingHttp : public GitHubHttp
    {
    public:
        bool request(const std::string&, const std::vector<std::string>&, Response&) override
        {
            throw std::runtime_error("boom");
        }
    };

    Http::Response answer(int status, const std::string& body, std::map<std::string, std::string> headers = {})
    {
        Http::Response response;
        response.status = status;
        response.body = body;
        response.headers = std::move(headers);
        return response;
    }

    // Releases as GitHub lists them: a draft, a pre-release, one without a
    // Windows package, two with tags that aren't versions, and two good ones
    const std::string releases = R"([
      {"tag_name": "v9.0", "draft": true, "prerelease": false, "body": "draft",
       "assets": [{"name": "RetroFE-9.0-windows-x64.zip", "state": "uploaded", "size": 1,
                   "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v9.0/RetroFE-9.0-windows-x64.zip"}]},
      {"tag_name": "v3.0-beta.1", "draft": false, "prerelease": true, "body": "beta",
       "assets": [{"name": "RetroFE-3.0-beta.1-windows-x64.zip", "state": "uploaded", "size": 3,
                   "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v3.0-beta.1/RetroFE-3.0-beta.1-windows-x64.zip"}]},
      {"tag_name": "v2.2", "draft": false, "prerelease": false, "body": "Linux only",
       "assets": [{"name": "RetroFE-2.2-linux-x64.zip", "state": "uploaded", "size": 22,
                   "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.2/RetroFE-2.2-linux-x64.zip"}]},
      {"tag_name": "nightly", "draft": false, "prerelease": false, "body": "",
       "assets": [{"name": "RetroFE-nightly-windows-x64.zip", "state": "uploaded", "size": 5,
                   "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/nightly/RetroFE-nightly-windows-x64.zip"}]},
      {"tag_name": "latest-build", "draft": false, "prerelease": false, "body": "", "assets": []},
      {"tag_name": "v2.1", "draft": false, "prerelease": false, "body": "\r\n  Fixes things.\r\n\r\nMore.\r\n",
       "assets": [
         {"name": "RetroFE-2.1-windows-arm64.zip", "state": "uploaded", "size": 2101,
          "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-windows-arm64.zip"},
         {"name": "RetroFE-2.1-windows-x64.zip.sha256", "state": "uploaded", "size": 64,
          "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-windows-x64.zip.sha256"},
         {"name": "RetroFE-2.1-windows-x64-symbols.zip", "state": "uploaded", "size": 99999,
          "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-windows-x64-symbols.zip"},
         {"name": "RetroFE-2.1-windows-x64.zip", "state": "uploaded", "size": 1234,
          "digest": "sha256:ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
          "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-windows-x64.zip"},
         {"name": "RetroFE-2.1-linux-x86_64.zip", "state": "uploaded", "size": 2102,
          "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-linux-x86_64.zip"},
         {"name": "RetroFE-2.1-macos-arm64.zip", "state": "uploaded", "size": 2103,
          "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-macos-arm64.zip"}]},
      {"tag_name": "v2.0", "draft": false, "prerelease": false, "body": "Old",
       "assets": [{"name": "RetroFE-2.0-windows-x64.zip", "state": "uploaded", "size": 20,
                   "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.0/RetroFE-2.0-windows-x64.zip"},
                  {"name": "RetroFE-2.0-macos-x64.zip", "state": "uploaded", "size": 20,
                   "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.0/RetroFE-2.0-macos-x64.zip"}]}
    ])";

    const char* const systems[] = { "windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-x64", "macos-arm64" };

    // One release offering the same archive for every system; with checksum
    // "digest" GitHub's digest, "file" a <name>.sha256 beside it, or "none"
    std::string releaseFor(const std::string& version, const std::string& archive, const std::string& checksum = "digest")
    {
        std::string assets;
        for (const char* system : systems) {
            std::string name = "RetroFE-" + version + "-" + system + ".zip";
            assets += std::string(assets.empty() ? "" : ",") + R"({"name": ")" + name + R"(", "state": "uploaded", "size": )"
                    + std::to_string(archive.size()) + (checksum == "digest" ? R"(, "digest": "sha256:)" + sha256Of(archive) + "\"" : "")
                    + R"(, "browser_download_url": "https://example.org/)" + name + R"("})";
            if (checksum == "file") {
                assets += R"(,{"name": ")" + name + R"(.sha256", "state": "uploaded", "size": 90, "browser_download_url": "https://example.org/)"
                        + name + R"(.sha256"})";
            }
        }
        return R"([{"tag_name": "v)" + version + R"(", "draft": false, "prerelease": false, "body": "New things", "assets": [)" + assets + "]}]";
    }

    // A RetroFE install as on Windows: the program in retrofe/, beside the user's own files
    struct FakeInstall
    {
        std::string root;
        fs::path program;
        std::string catalog;
        std::string journal;
        AppInstall app;
    };

    FakeInstall makeInstall(const std::string& name)
    {
        FakeInstall install;
        install.root = makeRoot(name);
        install.program = fs::path(install.root) / "retrofe";
        install.catalog = install.root + "/catalog";
        install.journal = install.root + "/retrofe.update.json";
        writeFile((install.program / "retrofe.exe").string(), "old exe");
        writeFile((install.program / "a.dll").string(), "old a");
        writeFile((install.program / "keep.dll").string(), "same");
        writeFile(install.root + "/layouts/Shipped/layout.xml", "old layout");
        writeFile(install.root + "/layouts/Mine/layout.xml", "mine");
        writeFile(install.root + "/collections/Main/menu.txt", "Arcade");
        writeFile(install.root + "/settings.conf", "layout = Shipped");
        install.app.root = install.root;
        install.app.program = install.program.string();
        install.app.executable = (install.program / "retrofe.exe").string();
        install.app.version = "1.0";
        return install;
    }

    const std::vector<std::pair<std::string, std::string>> updateEntries = {
        { "retrofe/retrofe.exe", "new exe" },
        { "retrofe/a.dll", "new a" },
        { "retrofe/keep.dll", "same" },
        { "retrofe/plugins/b.dll", "new b" },
        { "layouts/Shipped/layout.xml", "new layout" },
        { "RetroFE.lnk", "shortcut" },
        { "README.txt", "readme" },
    };

    Package appPackage(const std::string& version)
    {
        Package package;
        package.id = appPackageId;
        package.name = "RetroFE from ennisj/RetroFE";
        package.kind = "app";
        package.version = version;
        package.source = "retrofe";
        return package;
    }

    std::vector<std::string> setAsideFiles(const fs::path& folder)
    {
        std::vector<std::string> found;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::string name = it->path().filename().string();
            if (name.find(".old") != std::string::npos || name.find(".new") != std::string::npos
                || name.find(".failed") != std::string::npos || name.find(".part") != std::string::npos) {
                found.push_back(name);
            }
        }
        return found;
    }

    // The update's own records and set-aside files are all gone
    void checkTidy(const FakeInstall& install, const std::string& what)
    {
        check(setAsideFiles(install.program).empty() && setAsideFiles(install.root + "/layouts").empty(), what + ": nothing set aside left over");
        check(!fs::exists(install.journal), what + ": no journal");
        check(!fs::exists(install.root + "/retrofe.new") && !fs::exists(install.root + "/retrofe.old"), what + ": no spare program folders");
        check(!fs::exists(install.root + "/retrofe-UPDATE-INTERRUPTED.txt"), what + ": no note");
    }

    // Everything as makeInstall left it
    void checkUnchanged(const FakeInstall& install, const std::string& what)
    {
        checkEqual(readFile((install.program / "retrofe.exe").string()), "old exe", what + ": exe");
        checkEqual(readFile((install.program / "a.dll").string()), "old a", what + ": a.dll");
        checkEqual(readFile((install.program / "keep.dll").string()), "same", what + ": keep.dll");
        checkEqual(readFile(install.root + "/layouts/Shipped/layout.xml"), "old layout", what + ": layout");
        check(!fs::exists(install.program / "plugins" / "b.dll"), what + ": no new file");
        checkTidy(install, what);
    }

    void checkUpdated(const FakeInstall& install, const std::string& what)
    {
        checkEqual(readFile((install.program / "retrofe.exe").string()), "new exe", what + ": program replaced");
        checkEqual(readFile((install.program / "a.dll").string()), "new a", what + ": library replaced");
        checkEqual(readFile((install.program / "keep.dll").string()), "same", what + ": unchanged library kept");
        checkEqual(readFile((install.program / "plugins" / "b.dll").string()), "new b", what + ": new library added");
        checkEqual(readFile(install.root + "/layouts/Shipped/layout.xml"), "new layout", what + ": shipped layout updated");
        checkEqual(readFile(install.root + "/layouts/Mine/layout.xml"), "mine", what + ": other layouts untouched");
        checkEqual(readFile(install.root + "/settings.conf"), "layout = Shipped", what + ": settings untouched");
        checkEqual(readFile(install.root + "/collections/Main/menu.txt"), "Arcade", what + ": collections untouched");
        check(!fs::exists(install.root + "/RetroFE.lnk") && !fs::exists(install.root + "/README.txt"), what + ": nothing outside retrofe/ and layouts/");
    }

    bool plainRename(const fs::path& from, const fs::path& to)
    {
        std::error_code ec;
        if (fs::exists(to, ec)) return false;
        fs::rename(from, to, ec);
        return !ec;
    }

    bool plainRemove(const fs::path& file)
    {
        std::error_code ec;
        fs::remove(file, ec);
        return !fs::exists(file);
    }

    struct Crash {};

    json readJson(const std::string& path)
    {
        return json::parse(readFile(path), nullptr, false);
    }

    std::string generic(const fs::path& path)
    {
        return path.generic_u8string();
    }

    Targets targetsUnder(const std::string& root)
    {
        Targets targets;
        targets.root = root;
        targets.collections = root + "/collections";
        targets.items = root + "/roms";
        targets.media = root + "/media";
        targets.layouts = root + "/layouts";
        targets.meta = root + "/meta";
        return targets;
    }

    template <typename F>
    bool throws(F f)
    {
        try {
            f();
            return false;
        }
        catch (...) {
            return true;
        }
    }
}

void runSelfUpdateTests()
{
    std::string error;
    const int64_t now = 1700000000;

    // --- GitHub releases -----------------------------------------------------------

    run("GitHub repositories are checked and read from addresses", [&] {
        check(isValidRepository("ennisj/RetroFE"), "owner/name");
        check(isValidRepository("CoinOPS-Official/RetroFE.github_io"), "names may have . and _");
        check(!isValidRepository("ennisj"), "no name");
        check(!isValidRepository("a/b/c"), "too many parts");
        check(!isValidRepository("-a/b"), "owner can't start with -");
        check(!isValidRepository("a/.."), "name can't be ..");
        check(!isValidRepository("a b/c"), "no spaces");
        checkEqual(parseRepository(" https://github.com/ennisj/RetroFE/releases?x=1 "), "ennisj/RetroFE", "from a releases page");
        checkEqual(parseRepository("github.com/ennisj/RetroFE.git"), "ennisj/RetroFE", ".git dropped");
        checkEqual(parseRepository("ennisj/RetroFE"), "ennisj/RetroFE", "as is");
        checkEqual(parseRepository("https://example.org/ennisj/RetroFE"), "", "other sites aren't repositories");
        check(isOfficialRepository("EnnisJ/retrofe") && !isOfficialRepository("CoinOPS-Official/RetroFE"), "the official repository");
        std::string hex = sha256Of("x");
        checkEqual(parseSha256File(hex + "  RetroFE-1-windows-x64.zip\n"), hex, "sha256sum's format");
        checkEqual(parseSha256File("\n" + hex), hex, "just the hash");
        checkEqual(parseSha256File("not a hash"), "", "anything else");
    });

    run("release assets are picked by system and architecture", [&] {
        Platform windows{ "windows", "x64" };
        check(pickAsset({ "RetroFE-1-windows.zip", "RetroFE-1-windows-x64.zip" }, windows) == 1, "naming the architecture wins");
        check(pickAsset({ "RetroFE-1-windows.zip" }, Platform{ "windows", "arm64" }) == 0, "no architecture fits any");
        check(pickAsset({ "RetroFE-1-windows-arm64.zip" }, windows) == -1, "another architecture doesn't");
        check(pickAsset({ "RetroFE-1-windows-x64.tar.gz", "RetroFE-1-windows-x64.zip.sha256" }, windows) == -1, "only .zip");
        check(pickAsset({ "RetroFE-1-Win64.zip" }, windows) == 0, "Win64");
        check(pickAsset({ "RetroFE-1-linux-x86_64.zip" }, Platform{ "linux", "x64" }) == 0, "x86_64");
        check(pickAsset({ "retrofe-1-darwin-aarch64.zip" }, Platform{ "macos", "arm64" }) == 0, "darwin aarch64");
        check(pickAsset({ "RetroFE-1-linux-x64.zip", "source.zip" }, windows) == -1, "nothing for Windows");
        check(pickAsset({ "RetroFE-1-windows-x64-symbols.zip", "RetroFE-1-windows-x64-debug.zip", "RetroFE-1-windows-x64-pdb.zip" }, windows) == -1,
              "symbols and debug builds aren't the program");
        check(pickAsset({ "RetroFE-1-windows-x64-portable.zip", "RetroFE-1-windows-x64.zip" }, windows) == 1, "of equals, the plainest name");
    });

    run("the newest suitable release becomes the RetroFE package", [&] {
        SourceConfig source;
        source.name = "retrofe";
        source.type = "github";
        source.repo = "ennisj/RetroFE";
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        check(parseReleases(releases, source, Platform{ "windows", "x64" }, packages, warnings, error), "parsed: " + error);
        check(packages.size() == 1, "one package");
        if (packages.size() == 1) {
            const Package& package = packages[0];
            checkEqual(package.id, "retrofe", "id");
            checkEqual(package.name, "RetroFE from ennisj/RetroFE", "named with its repository");
            checkEqual(package.kind, "app", "kind");
            checkEqual(package.version, "2.1", "not the draft, the pre-release or the one without a Windows package");
            checkEqual(package.description, "Fixes things.\n\nMore.", "release notes, trimmed");
            checkEqual(package.source, "retrofe", "source");
            check(package.files.size() == 1 && package.files[0].size == 1234, "size");
            checkEqual(package.files[0].sha256, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", "digest");
            checkEqual(package.files[0].url, "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-windows-x64.zip", "url, not the symbols");
        }
        size_t tagWarnings = std::count_if(warnings.begin(), warnings.end(), [](const std::string& w) { return w.find("isn't a version") != std::string::npos; });
        check(tagWarnings == 1 && contains(warnings, "nightly, latest-build"), "tags that aren't versions are pointed out once");

        auto versionFor = [&](const SourceConfig& config, const Platform& platform) {
            std::vector<Package> found;
            std::vector<std::string> ignored;
            parseReleases(releases, config, platform, found, ignored, error);
            return found.empty() ? std::string("none") : found[0].version + " " + found[0].files[0].url.substr(found[0].files[0].url.rfind('/') + 1);
        };
        SourceConfig withBetas = source;
        withBetas.prerelease = true;
        checkEqual(versionFor(withBetas, Platform{ "windows", "x64" }), "3.0-beta.1 RetroFE-3.0-beta.1-windows-x64.zip", "pre-releases when asked for");
        checkEqual(versionFor(source, Platform{ "linux", "x64" }), "2.2 RetroFE-2.2-linux-x64.zip", "Linux");
        checkEqual(versionFor(source, Platform{ "macos", "arm64" }), "2.1 RetroFE-2.1-macos-arm64.zip", "macOS arm64");
        checkEqual(versionFor(source, Platform{ "macos", "x64" }), "2.0 RetroFE-2.0-macos-x64.zip", "the newest with a package for it");
        checkEqual(versionFor(source, Platform{ "windows", "x86" }), "none", "nothing for 32-bit Windows");

        packages.clear();
        check(!parseReleases(R"({"message": "Not Found"})", source, Platform{ "windows", "x64" }, packages, warnings, error)
              && error.find("Not Found") != std::string::npos, "GitHub's message is shown: " + error);
    });

    run("other repositories only offer RetroFE when their source says app = yes", [&] {
        SourceConfig fork;
        fork.name = "someone";
        fork.type = "github";
        fork.repo = "someone/RetroFE";
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        check(parseReleases(releases, fork, Platform{ "windows", "x64" }, packages, warnings, error) && packages.empty(), "nothing offered");
        check(contains(warnings, "sources.someone.app = yes"), "says how to allow it");
        fork.app = true;
        packages.clear();
        parseReleases(releases, fork, Platform{ "windows", "x64" }, packages, warnings, error);
        check(packages.size() == 1 && packages[0].name == "RetroFE from someone/RetroFE", "allowed, and named for where it's from");
    });

    run("without GitHub's digest, the .sha256 beside the release file is used", [&] {
        SourceConfig source;
        source.name = "retrofe";
        source.type = "github";
        source.repo = "ennisj/RetroFE";
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        parseReleases(releaseFor("2.0", "zip", "file"), source, Platform{ "windows", "x64" }, packages, warnings, error);
        check(packages.size() == 1 && packages[0].files[0].sha256.empty()
              && packages[0].files[0].sha256Url == "https://example.org/RetroFE-2.0-windows-x64.zip.sha256", "points at the .sha256");
        packages.clear();
        parseReleases(releaseFor("2.0", "zip", "none"), source, Platform{ "windows", "x64" }, packages, warnings, error);
        check(packages.size() == 1 && packages[0].files[0].sha256.empty() && packages[0].files[0].sha256Url.empty(), "no checksum at all");
    });

    run("GitHub is asked at most once a minute, conditionally, and not while limited", [&] {
        std::string cache = makeRoot("github-cache");
        SourceConfig source;
        source.name = "retrofe";
        source.type = "github";
        source.repo = "ennisj/RetroFE";
        GitHubHttp http;
        http.answers.push_back(answer(200, releaseFor("2.5", "zip"), { { "etag", "\"v1\"" } }));

        SourceContents first;
        readGitHub(source, cache, &http, now, first);
        check(first.error.empty() && first.packages.size() == 1 && !first.fromCache, "read: " + first.error);
        check(http.sent.size() == 1 && contains(http.sent[0], "Accept: application/vnd.github+json")
              && !contains(http.sent[0], "If-None-Match"), "asks for GitHub's JSON");

        SourceContents soon;
        readGitHub(source, cache, &http, now + 10, soon);
        check(http.sent.size() == 1 && soon.packages.size() == 1 && soon.warnings.empty(), "not asked again within a minute");

        http.answers.push_back(answer(304, ""));
        SourceContents later;
        readGitHub(source, cache, &http, now + 120, later);
        check(http.sent.size() == 2 && contains(http.sent[1], "If-None-Match: \"v1\""), "asks only for changes");
        check(later.packages.size() == 1 && !later.fromCache && later.warnings.empty(), "unchanged: the copy is current");

        http.answers.push_back(answer(403, R"({"message": "API rate limit exceeded"})",
                                      { { "x-ratelimit-remaining", "0" }, { "x-ratelimit-reset", std::to_string(now + 1800) } }));
        SourceContents limited;
        readGitHub(source, cache, &http, now + 300, limited);
        check(limited.fromCache && limited.packages.size() == 1 && contains(limited.warnings, "limit of 60"), "limited: the last copy, saying why");

        SourceContents waiting;
        readGitHub(source, cache, &http, now + 400, waiting);
        check(http.sent.size() == 3 && waiting.fromCache && contains(waiting.warnings, "limit"), "not asked again until the limit resets");

        SourceContents offline;
        readGitHub(source, cache, &http, now + 1900, offline);
        check(http.sent.size() == 4 && offline.fromCache && contains(offline.warnings, "offline"), "offline: the last copy");

        // Without a copy to fall back on
        std::string empty = makeRoot("github-nocache");
        http.answers.push_back(answer(429, "", { { "retry-after", "60" } }));
        SourceContents none;
        readGitHub(source, empty, &http, now, none);
        check(none.packages.empty() && none.error.find("limit") != std::string::npos, "limited, no copy: " + none.error);
        http.answers.push_back(answer(404, R"({"message": "Not Found"})"));
        SourceConfig missing = source;
        missing.name = "missing";
        readGitHub(missing, empty, &http, now, none);
        check(none.error.find("no public repository") != std::string::npos, "404: " + none.error);
    });

    run("malformed releases, caches, records and journals are read without failing", [&] {
        SourceConfig source;
        source.name = "retrofe";
        source.type = "github";
        source.repo = "ennisj/RetroFE";
        // Every field the wrong type
        std::string hostile = R"([5, null, "x",
            {"tag_name": 5, "draft": "yes", "prerelease": 1, "assets": "none"},
            {"tag_name": "v1.0", "draft": null, "prerelease": "no", "body": 7, "assets": [
               {"name": 7, "size": "big", "state": null, "digest": 5, "browser_download_url": "https://x.org/a.zip"},
               "junk", null,
               {"name": "RetroFE-1.0-windows-x64.zip", "size": -5, "digest": ["x"], "browser_download_url": "https://x.org/RetroFE-1.0-windows-x64.zip"}]}])";
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        check(!throws([&] { parseReleases(hostile, source, Platform{ "windows", "x64" }, packages, warnings, error); }), "releases");
        check(packages.size() == 1 && packages[0].files[0].size == 0 && packages[0].files[0].sha256.empty(), "what makes sense is used");

        std::string cache = makeRoot("github-malformed");
        writeFile(cache + "/retrofe/state.json", R"({"etag": 5, "checked": "x", "blockedUntil": [1]})");
        writeFile(cache + "/retrofe/releases.json", R"({"not": "a list"})");
        GitHubHttp http;
        SourceContents contents;
        check(!throws([&] { readGitHub(source, cache, &http, now, contents); }), "cache and state");

        std::string root = makeRoot("catalog-malformed");
        Manifest manifest;
        writeFile(root + "/m.json", R"({"id": 5, "version": [], "files": [{"target": 3, "path": [], "size": "x", "sha256": {}}, 4]})");
        check(!throws([&] { readManifest(root + "/m.json", manifest, error); }), "manifest");
        writeFile(root + "/catalog/staging/pack/.ready", "");
        writeFile(root + "/catalog/staging/pack/plan.json", R"({"id": 7, "name": [], "files": "x"})");
        writeFile(root + "/catalog/staging/pack/commit.json", R"({"backup": 3, "existing": 5})");
        Installer installer(root + "/catalog", targetsUnder(root));
        check(!throws([&] { installer.recover(); }), "an install's plan and journal");
        writeFile(root + "/catalog/staging/pack2/.ready", "");
        writeFile(root + "/catalog/staging/pack2/plan.json", R"({"id": "pack2", "files": [{"target": 1, "path": 2, "staged": 3}]})");
        check(!throws([&] { installer.recover(); }), "an install's files");

        FakeInstall install = makeInstall("self-update-malformed");
        for (const char* journal : { R"({"state": 5, "steps": "x"})", R"({"state": "swapping", "attempts": "x", "steps": [{"kind": "add", "destination": 5}]})",
                                     R"([1, 2])", "not json" }) {
            writeFile(install.journal, journal);
            StartupRecovery result;
            check(!throws([&] { result = recoverAtStart(install.app.executable); }), std::string("journal ") + journal);
            check(!result.restart && !result.message.empty() && !fs::exists(install.journal), std::string("set aside, and the user told: ") + journal);
        }
        checkEqual(readFile((install.program / "a.dll").string()), "old a", "nothing touched");
    });

    run("RetroFE itself only comes from GitHub sources", [&] {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        check(parseIndex(R"({"packages": [
                {"id": "fake", "kind": "app", "version": "99", "files": [{"url": "fake.zip"}]},
                {"id": "RetroFE", "kind": "collection", "version": "99", "files": [{"url": "fake.zip"}]},
                {"id": "fine", "files": [{"url": "fine.zip"}]}]})", "C:/index.json", "index", packages, warnings, error), "parsed");
        check(packages.size() == 1 && packages[0].id == "fine", "only the ordinary package");
        check(contains(warnings, "only GitHub sources"), "app refused: " + (warnings.empty() ? "" : warnings[0]));
        check(contains(warnings, "RetroFE's own"), "its id refused");

        std::string root = makeRoot("github-folder");
        makeZip(root + "/usb/RetroFE v99.zip", { { "retrofe/retrofe.exe", "x" } });
        SourceConfig folder;
        folder.name = "usb";
        folder.type = "folder";
        folder.path = root + "/usb";
        SourceContents contents = readSource(folder, root + "/cache", nullptr);
        check(contents.packages.empty() && contains(contents.warnings, "GitHub"), "a folder can't offer it by file name either");

        SourceConfig github;
        github.name = "retrofe";
        github.type = "github";
        github.repo = "not a repo";
        check(validateSource(github, {}).find("owner/name") != std::string::npos, "the repository is checked");
        github.repo = "ennisj/RetroFE";
        check(validateSource(github, {}).empty(), "a good one is accepted");
        SourceConfig again = github;
        again.name = "twice";
        again.repo = "EnnisJ/retrofe";
        check(validateSource(again, { github }).find("already a source") != std::string::npos, "the same repository twice");
        github.url = "http://example.org/releases";
        check(!validateSource(github, {}).empty(), "a plain http releases address on the internet");

        github.url.clear();
        github.prerelease = true;
        github.app = true;
        std::string file = root + "/sources.conf";
        std::vector<SourceConfig> loaded;
        check(saveSources(file, { github }, error) && loadSources(file, loaded, error), "saved and loaded: " + error);
        check(loaded.size() == 1 && loaded[0].type == "github" && loaded[0].repo == "ennisj/RetroFE" && loaded[0].prerelease && loaded[0].app,
              "repo, prerelease and app kept");
    });

    // --- Updating RetroFE -------------------------------------------------------------

    run("an update swaps in a whole new program folder, and only RetroFE's files", [&] {
        FakeInstall install = makeInstall("self-update");
        makeZip(install.root + "/share/up.zip", updateEntries);
        SelfUpdater updater(install.catalog, install.app);
        // The old RetroFE is still closing: its program can't be deleted yet
        updater.removeFile = [](const fs::path& file) {
            if (file.filename() == "retrofe.exe" && file.parent_path().filename() == "retrofe.old") return false;
            return plainRemove(file);
        };
        check(updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "applied: " + error);
        checkUpdated(install, "folder swap");

        std::string backup = install.catalog + "/backups/retrofe/1.0";
        checkEqual(readFile(backup + "/retrofe/retrofe.exe"), "old exe", "old program backed up");
        checkEqual(readFile(backup + "/layouts/Shipped/layout.xml"), "old layout", "old layout backed up");
        check(!fs::exists(backup + "/retrofe/keep.dll"), "unchanged files aren't copied");

        check(fs::exists(install.root + "/retrofe.old/retrofe.exe"), "the old program waits until it's free");
        check(!fs::exists(install.root + "/retrofe.old/a.dll"), "the rest of the old folder goes straight away");
        check(!fs::exists(install.root + "/retrofe.new") && !fs::exists(install.root + "/layouts/Shipped/layout.xml.old"), "nothing else left");
        check(!fs::exists(install.root + "/retrofe-UPDATE-INTERRUPTED.txt"), "the note is gone");
        checkEqual(readJson(install.journal).value("state", ""), "cleanup", "only cleaning up is left");

        Manifest manifest;
        check(readManifest(install.catalog + "/installed/retrofe.json", manifest, error), "recorded: " + error);
        check(manifest.name == "RetroFE" && manifest.kind == "app" && manifest.version == "2.0" && manifest.files.size() == 5,
              "every file of the version listed");

        SelfUpdater next(install.catalog, install.app);
        next.sleep = [](int) {};
        check(!next.recover(), "nothing to put back");
        checkTidy(install, "next start");
    });

    run("when the program folder can't be renamed, its files are swapped one by one", [&] {
        FakeInstall install = makeInstall("self-update-files");
        makeZip(install.root + "/share/up.zip", updateEntries);
        SelfUpdater updater(install.catalog, install.app);
        // Something has a file in the program folder open
        updater.renameFile = [&](const fs::path& from, const fs::path& to) {
            if (fs::is_directory(from) && from.filename() == "retrofe") return false;
            return plainRename(from, to);
        };
        updater.removeFile = [](const fs::path& file) { return file.filename() != "retrofe.exe.old" && plainRemove(file); };
        check(updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "applied: " + error);
        checkUpdated(install, "file swap");
        check(fs::exists(install.program / "retrofe.exe.old") && !fs::exists(install.program / "a.dll.old"), "only what's in use waits");
        check(!fs::exists(install.root + "/retrofe.new"), "the unused new folder is gone");
        SelfUpdater next(install.catalog, install.app);
        next.sleep = [](int) {};
        next.recover();
        checkTidy(install, "next start");
    });

    run("a file that can't be swapped puts every file back", [&] {
        FakeInstall install = makeInstall("self-update-fail");
        makeZip(install.root + "/share/up.zip", updateEntries);

        // File by file: the new a.dll can't be moved in
        SelfUpdater files(install.catalog, install.app);
        files.folderSwap = false;
        files.renameFile = [](const fs::path& from, const fs::path& to) {
            if (to.filename() == "a.dll" && from.filename() == "a.dll.new") return false;
            return plainRename(from, to);
        };
        check(!files.apply(appPackage("2.0"), install.root + "/share/up.zip", error) && error.find("Nothing was changed") != std::string::npos,
              "failed, saying nothing changed: " + error);
        checkUnchanged(install, "a.dll failed");
        check(!fs::exists(install.catalog + "/installed/retrofe.json"), "not recorded");

        // File by file: the program itself, last, can't be moved aside
        SelfUpdater last(install.catalog, install.app);
        last.folderSwap = false;
        last.renameFile = [](const fs::path& from, const fs::path& to) {
            return !(from.filename() == "retrofe.exe" && to.filename() == "retrofe.exe.old") && plainRename(from, to);
        };
        check(!last.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "failed: " + error);
        checkUnchanged(install, "exe failed");

        // Whole folder: a layout file can't be swapped, before the folder is
        SelfUpdater layout(install.catalog, install.app);
        layout.renameFile = [](const fs::path& from, const fs::path& to) {
            return !(to.filename() == "layout.xml" && from.filename() == "layout.xml.new") && plainRename(from, to);
        };
        check(!layout.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "failed: " + error);
        checkUnchanged(install, "layout failed");
    });

    run("an update interrupted part way is undone when RetroFE next starts", [&] {
        // File by file: the power goes off after the fourth rename
        FakeInstall install = makeInstall("self-update-crash");
        makeZip(install.root + "/share/up.zip", updateEntries);
        SelfUpdater updater(install.catalog, install.app);
        updater.folderSwap = false;
        int calls = 0;
        updater.renameFile = [&](const fs::path& from, const fs::path& to) {
            if (++calls == 4) throw Crash();
            return plainRename(from, to);
        };
        check(throws([&] { updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error); }), "stopped mid-swap");
        check(fs::exists(install.journal) && fs::exists(install.root + "/retrofe-UPDATE-INTERRUPTED.txt"), "with a journal and a note for the user");
        check(!fs::exists(install.program / "a.dll"), "a.dll was missing at that moment");

        StartupRecovery result = recoverAtStart(install.app.executable);
        check(result.restart && result.message.empty(), "program files put back, so RetroFE starts again");
        checkUnchanged(install, "after recovery");
        check(!recoverAtStart(install.app.executable).restart, "only once");

        // Whole folder: off between the two folder renames, so there's no program folder
        FakeInstall folder = makeInstall("self-update-crash-folder");
        makeZip(folder.root + "/share/up.zip", updateEntries);
        SelfUpdater between(folder.catalog, folder.app);
        between.renameFile = [&](const fs::path& from, const fs::path& to) {
            if (from.filename() == "retrofe.new") throw Crash();
            return plainRename(from, to);
        };
        check(throws([&] { between.apply(appPackage("2.0"), folder.root + "/share/up.zip", error); }), "stopped between the renames");
        check(!fs::exists(folder.program) && fs::exists(folder.root + "/retrofe.old"), "no program folder at that moment");
        result = recoverAtStart(folder.app.executable);
        check(result.restart, "put back");
        checkUnchanged(folder, "between the folder renames");

        // Whole folder: off just after the new folder went in, the commit point
        FakeInstall after = makeInstall("self-update-crash-committed");
        makeZip(after.root + "/share/up.zip", updateEntries);
        SelfUpdater committed(after.catalog, after.app);
        committed.renameFile = [&](const fs::path& from, const fs::path& to) {
            bool ok = plainRename(from, to);
            if (from.filename() == "retrofe.new") throw Crash();
            return ok;
        };
        check(throws([&] { committed.apply(appPackage("2.0"), after.root + "/share/up.zip", error); }), "stopped after the swap");
        result = recoverAtStart(after.app.executable);
        check(!result.restart, "the new version stays: nothing to restart for");
        checkUpdated(after, "finished");
        checkEqual(readJson(after.journal).value("state", ""), "swapped", "to be recorded once the catalog is known");
        SelfUpdater service(after.catalog, after.app);
        service.sleep = [](int) {};
        service.recover();
        Manifest manifest;
        check(readManifest(after.catalog + "/installed/retrofe.json", manifest, error) && manifest.version == "2.0", "recorded by the Catalog");
        checkTidy(after, "after the Catalog's recovery");
    });

    run("a tampered journal never deletes or restores files it doesn't recognise", [&] {
        FakeInstall install = makeInstall("self-update-tampered");
        writeFile((install.program / "SDL2.dll").string(), "the real SDL");
        writeFile((install.program / "keep.dll.old").string(), "not what was backed up");
        // "Undoing" these would delete SDL2.dll and swap keep.dll for something else
        json steps = json::array({
            { { "kind", "add" }, { "destination", generic(install.program / "SDL2.dll") }, { "prepared", generic(install.program / "SDL2.dll.new") },
              { "aside", "" }, { "newSha256", sha256Of("something else") }, { "oldSha256", "" } },
            { { "kind", "replace" }, { "destination", generic(install.program / "keep.dll") }, { "prepared", generic(install.program / "keep.dll.new") },
              { "aside", generic(install.program / "keep.dll.old") }, { "newSha256", sha256Of("same") }, { "oldSha256", sha256Of("what was backed up") } } });
        writeFile(install.journal, json{ { "version", "9" }, { "state", "swapping" }, { "steps", steps } }.dump());
        recoverAtStart(install.app.executable);
        checkEqual(readFile((install.program / "SDL2.dll").string()), "the real SDL", "SDL2.dll not deleted");
        checkEqual(readFile((install.program / "keep.dll").string()), "same", "keep.dll not replaced");

        // A journal naming the user's own files is ignored outright
        json outside = json::array({ { { "kind", "add" }, { "destination", generic(fs::path(install.root) / "collections/Main/menu.txt") },
                                       { "prepared", generic(fs::path(install.root) / "collections/Main/menu.txt.new") },
                                       { "newSha256", sha256Of("Arcade") } } });
        writeFile(install.journal, json{ { "version", "9" }, { "state", "swapping" }, { "steps", outside } }.dump());
        StartupRecovery result = recoverAtStart(install.app.executable);
        checkEqual(readFile(install.root + "/collections/Main/menu.txt"), "Arcade", "the user's file is left alone");
        check(!result.message.empty() && fs::exists(install.root + "/retrofe.update.damaged.json"), "and the user is told");

        // Leftovers are only ever files named as updates name them, in RetroFE's folders
        writeFile((install.program / "x.dll.older").string(), "x");
        writeFile(install.root + "/collections/Main/menu.txt.old", "x");
        json leftovers = json::array({ generic(install.program / "a.dll"), generic(install.program / "x.dll.older"),
                                       generic(fs::path(install.root) / "collections/Main/menu.txt.old") });
        writeFile(install.journal, json{ { "version", "9" }, { "state", "cleanup" }, { "steps", json::array() }, { "leftovers", leftovers } }.dump());
        SelfUpdater updater(install.catalog, install.app);
        updater.recover();
        check(fs::exists(install.program / "a.dll") && fs::exists(install.program / "x.dll.older")
              && fs::exists(install.root + "/collections/Main/menu.txt.old"), "none of them deleted");
    });

    run("recovery that can't finish gives up after three starts and says where the backup is", [&] {
        FakeInstall install = makeInstall("self-update-bounded");
        // A replaced file that's gone, with nothing to put back
        json steps = json::array({ { { "kind", "replace" }, { "destination", generic(install.program / "gone.dll") },
                                     { "prepared", generic(install.program / "gone.dll.new") }, { "aside", generic(install.program / "gone.dll.old") },
                                     { "newSha256", sha256Of("new") }, { "oldSha256", sha256Of("old") } } });
        std::string backup = install.catalog + "/backups/retrofe/1.0";
        writeFile(install.journal, json{ { "version", "2.0" }, { "backup", backup }, { "state", "swapping" }, { "steps", steps } }.dump());
        for (int attempt = 1; attempt <= SelfUpdater::maxRecoveryAttempts; attempt++) {
            StartupRecovery result = recoverAtStart(install.app.executable);
            check(result.message.empty() && readJson(install.journal).value("attempts", 0) == attempt, "try " + std::to_string(attempt));
        }
        makeZip(install.root + "/share/up.zip", updateEntries);
        SelfUpdater blocked(install.catalog, install.app);
        check(!blocked.apply(appPackage("2.0"), install.root + "/share/up.zip", error) && error.find(backup) != std::string::npos,
              "updates wait, and say where the backup is: " + error);
        check(!fs::exists(install.journal) && fs::exists(install.root + "/retrofe.update.damaged.json"), "that was the last try: set aside");
        check(blocked.message().find(backup) != std::string::npos, "the user is told where the backup is: " + blocked.message());
        SelfUpdater updater(install.catalog, install.app);
        check(updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "updates work again: " + error);
    });

    run("only RetroFE for this system is installed, and only into its folders", [&] {
        FakeInstall install = makeInstall("self-update-refused");
        SelfUpdater updater(install.catalog, install.app);
        auto refused = [&](const std::string& name, const std::vector<std::pair<std::string, std::string>>& entries, const std::string& reason) {
            std::string zip = install.root + "/share/" + name + ".zip";
            makeZip(zip, entries);
            bool applied = updater.apply(appPackage("2.0"), zip, error);
            check(!applied && error.find(reason) != std::string::npos, name + ": " + error);
            checkUnchanged(install, name);
        };
        refused("no-program", { { "retrofe/a.dll", "new a" } }, "retrofe.exe");
        refused("layout-program", { { "retrofe/retrofe.exe", "new exe" }, { "layouts/Shipped/run.bat", "format c:" } }, "program in a layout");
        refused("escape", { { "retrofe/retrofe.exe", "new exe" }, { "retrofe/../settings.conf", "x" } }, "safely");
        refused("reserved", { { "retrofe/retrofe.exe", "new exe" }, { "retrofe/a.dll.old", "x" } }, "named like");
        refused("not-a-zip", {}, "");
        checkEqual(readFile(install.root + "/settings.conf"), "layout = Shipped", "settings untouched");

        // Stop, while extracting
        makeZip(install.root + "/share/up.zip", updateEntries);
        SelfUpdater stopping(install.catalog, install.app);
        stopping.shouldStop = [] { return true; };
        check(!stopping.apply(appPackage("2.0"), install.root + "/share/up.zip", error) && error == "Stopped", "stopped: " + error);
        checkUnchanged(install, "stopped");

        AppInstall flat = install.app;
        flat.program = install.root;   // the program right in the root, as on Linux
        flat.executable = install.root + "/retrofe";
        SelfUpdater flatUpdater(install.catalog, flat);
        check(!flatUpdater.problem().empty(), "a program without its own folder isn't updated: " + flatUpdater.problem());
    });

    run("files the new version doesn't have are removed, and only the last version is kept", [&] {
        FakeInstall install = makeInstall("self-update-twice");
        std::vector<std::pair<std::string, std::string>> first = updateEntries;
        first.push_back({ "retrofe/gone.dll", "gone" });
        first.push_back({ "retrofe/edited.dll", "edited" });
        makeZip(install.root + "/share/v2.zip", first);
        SelfUpdater updater(install.catalog, install.app);
        check(updater.apply(appPackage("2.0"), install.root + "/share/v2.zip", error), "2.0: " + error);
        writeFile((install.program / "edited.dll").string(), "changed by hand");
        writeFile((install.program / "mine.txt").string(), "the user's");

        makeZip(install.root + "/share/v3.zip", { { "retrofe/retrofe.exe", "exe 3" }, { "retrofe/a.dll", "new a" } });
        install.app.version = "2.0";
        SelfUpdater next(install.catalog, install.app);
        check(next.apply(appPackage("3.0"), install.root + "/share/v3.zip", error), "3.0: " + error);
        checkEqual(readFile((install.program / "retrofe.exe").string()), "exe 3", "updated");
        check(!fs::exists(install.program / "gone.dll"), "a file 3.0 doesn't have is removed");
        checkEqual(readFile((install.program / "edited.dll").string()), "changed by hand", "unless it was changed");
        checkEqual(readFile((install.program / "mine.txt").string()), "the user's", "files no version installed are kept");
        check(!fs::exists(install.program / "plugins" / "b.dll") && !fs::exists(install.root + "/layouts/Shipped/layout.xml"),
              "files 2.0 installed and 3.0 doesn't have go, layouts included");
        checkEqual(readFile(install.catalog + "/backups/retrofe/2.0/retrofe/gone.dll"), "gone", "and backed up");
        check(!fs::exists(install.catalog + "/backups/retrofe/1.0"), "older backups are dropped");
    });

#ifdef _WIN32
    run("a loaded library is set aside with its folder, or by itself, and deleted once free", [&] {
        for (bool wholeFolder : { true, false }) {
            std::string what = wholeFolder ? "folder" : "files";
            FakeInstall install = makeInstall("self-update-loaded-" + what);
            wchar_t self[MAX_PATH];
            GetModuleFileNameW(nullptr, self, MAX_PATH);
            fs::path library = install.program / "zlib-copy.dll";
            fs::copy_file(fs::path(self).parent_path() / "zlib.dll", library);
            HMODULE module = LoadLibraryW(library.wstring().c_str());
            check(module != nullptr, what + ": loaded");
            makeZip(install.root + "/share/up.zip", { { "retrofe/retrofe.exe", "new exe" }, { "retrofe/zlib-copy.dll", "new zlib" } });

            SelfUpdater updater(install.catalog, install.app);   // real renames, links and deletes
            updater.folderSwap = wholeFolder;
            check(updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error), what + ": applied while loaded: " + error);
            checkEqual(readFile(library.string()), "new zlib", what + ": replaced");
            checkEqual(readFile((install.program / "keep.dll").string()), "same", what + ": others kept");
            fs::path held = wholeFolder ? fs::path(install.root) / "retrofe.old" / "zlib-copy.dll" : install.program / "zlib-copy.dll.old";
            check(fs::exists(held), what + ": the loaded one can't be deleted yet");

            FreeLibrary(module);
            SelfUpdater next(install.catalog, install.app);
            next.sleep = [](int) {};
            next.recover();
            checkTidy(install, what + ": deleted once free");
        }
    });
#endif

    // --- Through the Catalog and its screens ------------------------------------------

    auto serviceFor = [&](FakeInstall& install, GitHubHttp* http, const std::string& releasesJson, SourceConfig source) {
        std::string zip = install.root + "/share/up.zip";
        makeZip(zip, updateEntries);
        std::string archive = readFile(zip);
        http->answers.push_back(answer(200, releasesJson.empty() ? releaseFor("2.0", archive) : releasesJson));
        for (const char* system : systems) {
            std::string name = std::string("RetroFE-2.0-") + system + ".zip";
            http->files["https://example.org/" + name] = archive;
            http->documents["https://example.org/" + name + ".sha256"] = sha256Of(archive) + "  " + name + "\n";
        }
        auto service = std::make_unique<Service>(install.catalog, targetsUnder(install.root), std::unique_ptr<Http>(http));
        service->setApp(install.app);
        service->start();
        if (source.name.empty()) {
            source.name = "retrofe";
            source.type = "github";
            source.repo = "ennisj/RetroFE";
        }
        source.url = "https://example.org/releases";
        bool added = service->addSource(source, error);
        check(added, "added: " + error);
        service->waitUntilIdle();
        return service;
    };

    run("RetroFE updates itself from a GitHub source, then asks to restart", [&] {
        FakeInstall install = makeInstall("self-update-service");
        auto service = serviceFor(install, new GitHubHttp(), "", SourceConfig());

        Entry entry;
        check(service->entry("retrofe", entry) && entry.installedVersion == "1.0" && entry.updateAvailable, "the running 1.0 can be updated");
        service->install("retrofe");
        service->waitUntilIdle();
        bool ok = false;
        std::string message;
        check(service->outcome("retrofe", ok, message) && ok && message == "Updated RetroFE to 2.0. Restarting...", "updated: " + message);
        checkEqual(readFile((install.program / "retrofe.exe").string()), "new exe", "the program is new");
        check(service->entry("retrofe", entry) && !entry.updateAvailable, "the same update isn't offered again before the restart");
        check(service->takeRestartRequest() && !service->takeRestartRequest(), "a restart is asked for once");

        service->remove("retrofe");
        service->waitUntilIdle();
        check(service->outcome("retrofe", ok, message) && !ok && message == "RetroFE can't remove itself.", "not removable: " + message);
        checkEqual(readFile((install.program / "retrofe.exe").string()), "new exe", "still there");

        // The screens say what installing does, and don't offer to remove it
        Configuration::absolutePath = install.root;
        Configuration config;
        ManagerModel model(config);
        model.setCatalog(service.get());
        model.selectPackage("retrofe");
        check(model.get("package:notice").find("restarts") != std::string::npos, "the package screen says it restarts");
        checkEqual(model.get("package:name"), "RetroFE from ennisj/RetroFE", "named with its repository");
        checkEqual(model.get("package:canRemove"), "no", "no Remove button");
        checkEqual(model.actionProblem("package.remove"), "RetroFE can't remove itself.", "remove refused");
        check(model.list("info:installed") == std::vector<std::string>({ "RetroFE 2.0" }), "listed as installed");
        checkEqual(model.get("info:hasInstalled"), "no", "but not offered for removal in Manage");
        check(model.options("installed").empty() && model.get("draft:removePackage").empty(), "not in the removal choice");
    });

    run("the Catalog refuses unchecked, older and unofficial versions of RetroFE", [&] {
        bool ok = true;
        std::string message;

        // No digest and no .sha256
        FakeInstall unchecked = makeInstall("self-update-unchecked");
        auto service = serviceFor(unchecked, new GitHubHttp(), releaseFor("2.0", "x", "none"), SourceConfig());
        Configuration::absolutePath = unchecked.root;
        Configuration config;
        ManagerModel model(config);
        model.setCatalog(service.get());
        model.selectPackage("retrofe");
        checkEqual(model.get("package:checksum"), "This release has no checksum, so RetroFE won't install it.", "the screen says so");
        checkEqual(model.actionProblem("package.install"), "This release has no checksum, so RetroFE won't install it.", "and won't start it");
        service->install("retrofe");
        service->waitUntilIdle();
        check(service->outcome("retrofe", ok, message) && !ok && message.find("no checksum") != std::string::npos, "refused: " + message);
        checkEqual(readFile((unchecked.program / "retrofe.exe").string()), "old exe", "nothing changed");

        // A .sha256 beside the release file
        FakeInstall withFile = makeInstall("self-update-sha256-file");
        std::string zip = withFile.root + "/share/up.zip";
        makeZip(zip, updateEntries);
        service = serviceFor(withFile, new GitHubHttp(), releaseFor("2.0", readFile(zip), "file"), SourceConfig());
        service->install("retrofe");
        service->waitUntilIdle();
        check(service->outcome("retrofe", ok, message) && ok, "checked with the .sha256: " + message);
        checkEqual(readFile((withFile.program / "retrofe.exe").string()), "new exe", "updated");

        // Not newer than what's running
        FakeInstall newer = makeInstall("self-update-older");
        newer.app.version = "2.0";
        service = serviceFor(newer, new GitHubHttp(), "", SourceConfig());
        service->install("retrofe");
        service->waitUntilIdle();
        check(service->outcome("retrofe", ok, message) && !ok && message.find("already this version or newer") != std::string::npos, "refused: " + message);
        checkEqual(readFile((newer.program / "retrofe.exe").string()), "old exe", "nothing changed");

        // Another repository without app = yes offers nothing
        FakeInstall unofficial = makeInstall("self-update-unofficial");
        SourceConfig fork;
        fork.name = "fork";
        fork.type = "github";
        fork.repo = "someone/RetroFE";
        service = serviceFor(unofficial, new GitHubHttp(), "", fork);
        Entry entry;
        check(!service->entry("retrofe", entry) && contains(service->warnings(), "app = yes"), "not offered, and why");
    });

    run("a Catalog job that fails unexpectedly doesn't take RetroFE down", [&] {
        FakeInstall install = makeInstall("self-update-throwing");
        Service service(install.catalog, targetsUnder(install.root), std::make_unique<ThrowingHttp>());
        service.start();
        SourceConfig source;
        source.name = "retrofe";
        source.type = "github";
        source.repo = "ennisj/RetroFE";
        bool added = service.addSource(source, error);
        check(added, "added: " + error);
        service.waitUntilIdle();
        check(contains(service.warnings(), "boom"), "reported as a problem");
        service.refresh();
        service.waitUntilIdle();
        check(service.sources().size() == 1, "and the Catalog keeps working");
    });

    run("the Sources screen adds RetroFE from GitHub in one step", [&] {
        std::string root = makeRoot("github-model");
        Configuration::absolutePath = root;
        Configuration config;
        Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        ManagerModel model(config);
        model.setCatalog(&service);

        checkEqual(model.get("info:hasRetroFESource"), "no", "not yet");
        check(model.get("info:retrofeSourceNote").find(retrofeRepository) != std::string::npos, "says which repository");
        checkEqual(model.actionProblem("source.addRetroFE"), "Confirm that you trust this source.", "trust asked for");
        model.setDefault("draft:retrofeTrusted", "yes");
        checkEqual(model.actionProblem("source.addRetroFE"), "Confirm that you trust this source.", "a layout can't vouch for it");
        model.set("draft:retrofeTrusted", "yes");
        Forms::Model::Result result = model.runAction("source.addRetroFE");
        check(result.ok, "added: " + result.message);
        std::vector<SourceConfig> sources = service.sources();
        check(sources.size() == 1 && sources[0].name == "retrofe" && sources[0].type == "github" && sources[0].repo == retrofeRepository
              && sources[0].app, "a github source for this fork, allowed to offer RetroFE");
        checkEqual(model.get("info:hasRetroFESource"), "yes", "now added");
        check(model.actionProblem("source.addRetroFE").find("already") != std::string::npos, "not twice");
        check(model.list("info:sources") == std::vector<std::string>({ std::string("retrofe: GitHub ") + retrofeRepository }), "listed");

        // Any repository through the ordinary form, as owner/name or its address
        model.set("draft:sourceName", "upstream");
        model.set("draft:sourceType", "github");
        model.set("draft:sourceUrl", "https://github.com/CoinOPS-Official/RetroFE/releases");
        model.set("draft:sourceTrusted", "yes");
        result = model.runAction("source.add");
        check(result.ok, "upstream added: " + result.message);
        sources = service.sources();
        check(sources.size() == 2 && sources[1].repo == "CoinOPS-Official/RetroFE" && !sources[1].app, "repository read from the address");
        service.waitUntilIdle();
    });

    // --- Real HTTP, against Tests/test_server.py (RETROFE_TEST_SERVER) ------------------

    const char* server = std::getenv("RETROFE_TEST_SERVER");
    if (!server || !*server) {
        return;
    }
    std::unique_ptr<Http> http = createHttp();
    if (!http) return;
    std::string base = server;

    run("a real GitHub-style releases list is read, then only checked for changes", [&] {
        std::string cache = makeRoot("live-github");
        SourceConfig source;
        source.name = "live";
        source.type = "github";
        source.repo = "test/RetroFE";
        source.app = true;
        source.url = base + "/github/releases";
        SourceContents first;
        readGitHub(source, cache, http.get(), now, first);
        check(first.error.empty() && first.packages.size() == 1, "read: " + first.error);
        std::string version = first.packages.empty() ? "" : first.packages[0].version;
        SourceContents second;
        readGitHub(source, cache, http.get(), now + 120, second);
        check(second.packages.size() == 1 && second.packages[0].version == version && !second.fromCache,
              "a 304 keeps the copy (the server counts full answers in the version): " + (second.packages.empty() ? second.error : second.packages[0].version));

        source.name = "limited";
        source.url = base + "/github/limited/releases";
        SourceContents limited;
        readGitHub(source, cache, http.get(), now, limited);
        check(limited.error.find("limit of 60") != std::string::npos, "rate limit read from the headers: " + limited.error);
    });

    run("RetroFE updates itself over real HTTP", [&] {
        FakeInstall install = makeInstall("live-self-update");
        Service service(install.catalog, targetsUnder(install.root), createHttp());
        service.setApp(install.app);
        service.start();
        SourceConfig source;
        source.name = "live";
        source.type = "github";
        source.repo = "test/RetroFE";
        source.app = true;
        source.url = base + "/github/releases";
        bool added = service.addSource(source, error);
        check(added, "added: " + error);
        service.waitUntilIdle();
        service.install("retrofe");
        service.waitUntilIdle();
        bool ok = false;
        std::string message;
        check(service.outcome("retrofe", ok, message) && ok, "updated: " + message);
        checkEqual(readFile((install.program / "retrofe.exe").string()), "live exe", "downloaded, verified and swapped in");
        check(service.takeRestartRequest(), "restart asked for");
    });
}

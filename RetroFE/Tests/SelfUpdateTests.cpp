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
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace Catalog;

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

        bool get(const std::string&, std::string&, std::string& error) override
        {
            error = "not used";
            return false;
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

    Http::Response answer(int status, const std::string& body, std::map<std::string, std::string> headers = {})
    {
        Http::Response response;
        response.status = status;
        response.body = body;
        response.headers = std::move(headers);
        return response;
    }

    // Releases as GitHub lists them: a draft, a pre-release, one without a
    // Windows package, one with a tag that isn't a version, and two good ones
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
      {"tag_name": "v2.1", "draft": false, "prerelease": false, "body": "\r\n  Fixes things.\r\n\r\nMore.\r\n",
       "assets": [
         {"name": "RetroFE-2.1-windows-arm64.zip", "state": "uploaded", "size": 2101,
          "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-windows-arm64.zip"},
         {"name": "RetroFE-2.1-windows-x64.zip.sha256", "state": "uploaded", "size": 64,
          "browser_download_url": "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-windows-x64.zip.sha256"},
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

    // One release offering the same archive for every system
    std::string releaseFor(const std::string& version, const std::string& archive)
    {
        std::string assets;
        for (const char* system : { "windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-x64", "macos-arm64" }) {
            std::string name = "RetroFE-" + version + "-" + system + ".zip";
            assets += std::string(assets.empty() ? "" : ",") + R"({"name": ")" + name + R"(", "state": "uploaded", "size": )"
                    + std::to_string(archive.size()) + R"(, "digest": "sha256:)" + sha256Of(archive)
                    + R"(", "browser_download_url": "https://example.org/)" + name + R"("})";
        }
        return R"([{"tag_name": "v)" + version + R"(", "draft": false, "prerelease": false, "body": "New things", "assets": [)" + assets + "]}]";
    }

    // A RetroFE install as on Windows: the program in retrofe/, beside the user's own files
    struct FakeInstall
    {
        std::string root;
        fs::path program;
        std::string catalog;
        AppInstall app;
    };

    FakeInstall makeInstall(const std::string& name)
    {
        FakeInstall install;
        install.root = makeRoot(name);
        install.program = fs::path(install.root) / "retrofe";
        install.catalog = install.root + "/catalog";
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
        package.name = "RetroFE";
        package.kind = "app";
        package.version = version;
        package.source = "retrofe";
        return package;
    }

    // Everything as makeInstall left it
    void checkUnchanged(const FakeInstall& install, const std::string& what)
    {
        checkEqual(readFile((install.program / "retrofe.exe").string()), "old exe", what + ": exe");
        checkEqual(readFile((install.program / "a.dll").string()), "old a", what + ": a.dll");
        checkEqual(readFile(install.root + "/layouts/Shipped/layout.xml"), "old layout", what + ": layout");
        check(!fs::exists(install.program / "plugins" / "b.dll"), what + ": no new file");
        std::vector<std::string> extra;
        for (const auto& entry : fs::recursive_directory_iterator(install.program)) {
            std::string name = entry.path().filename().string();
            if (name.find(".old") != std::string::npos || name.find(".new") != std::string::npos) extra.push_back(name);
        }
        check(extra.empty(), what + ": nothing set aside left over");
        check(!fs::exists(install.catalog + "/update/journal.json"), what + ": no journal");
    }

    bool plainRename(const fs::path& from, const fs::path& to)
    {
        std::error_code ec;
        fs::rename(from, to, ec);
        return !ec;
    }

    struct Crash {};

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
            checkEqual(package.kind, "app", "kind");
            checkEqual(package.version, "2.1", "not the draft, the pre-release or the one without a Windows package");
            checkEqual(package.description, "Fixes things.\n\nMore.", "release notes, trimmed");
            checkEqual(package.source, "retrofe", "source");
            check(package.files.size() == 1 && package.files[0].size == 1234, "size");
            checkEqual(package.files[0].sha256, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", "digest");
            checkEqual(package.files[0].url, "https://github.com/ennisj/RetroFE/releases/download/v2.1/RetroFE-2.1-windows-x64.zip", "url");
        }
        check(contains(warnings, "nightly"), "a tag that isn't a version is pointed out");

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
        std::string file = root + "/sources.conf";
        std::vector<SourceConfig> loaded;
        check(saveSources(file, { github }, error) && loadSources(file, loaded, error), "saved and loaded: " + error);
        check(loaded.size() == 1 && loaded[0].type == "github" && loaded[0].repo == "ennisj/RetroFE" && loaded[0].prerelease,
              "repo and prerelease kept");
    });

    // --- Updating RetroFE -------------------------------------------------------------

    run("an update swaps RetroFE's files and only RetroFE's", [&] {
        FakeInstall install = makeInstall("self-update");
        makeZip(install.root + "/share/up.zip", updateEntries);
        SelfUpdater updater(install.catalog, install.app);
        // The running program can't be deleted yet
        updater.removeFile = [](const fs::path& file) {
            if (file.filename() == "retrofe.exe.old") return false;
            std::error_code ec;
            fs::remove(file, ec);
            return !fs::exists(file);
        };
        check(updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "applied: " + error);
        checkEqual(readFile((install.program / "retrofe.exe").string()), "new exe", "program replaced");
        checkEqual(readFile((install.program / "a.dll").string()), "new a", "library replaced");
        checkEqual(readFile((install.program / "plugins" / "b.dll").string()), "new b", "new library added");
        checkEqual(readFile(install.root + "/layouts/Shipped/layout.xml"), "new layout", "shipped layout updated");
        checkEqual(readFile(install.root + "/layouts/Mine/layout.xml"), "mine", "other layouts untouched");
        checkEqual(readFile(install.root + "/settings.conf"), "layout = Shipped", "settings untouched");
        checkEqual(readFile(install.root + "/collections/Main/menu.txt"), "Arcade", "collections untouched");
        check(!fs::exists(install.root + "/RetroFE.lnk") && !fs::exists(install.root + "/README.txt"), "nothing outside retrofe/ and layouts/");

        std::string backup = install.catalog + "/backups/retrofe/1.0";
        checkEqual(readFile(backup + "/retrofe/retrofe.exe"), "old exe", "old program backed up");
        checkEqual(readFile(backup + "/layouts/Shipped/layout.xml"), "old layout", "old layout backed up");
        check(!fs::exists(backup + "/retrofe/keep.dll"), "unchanged files aren't touched or copied");

        check(fs::exists(install.program / "retrofe.exe.old"), "the running program is set aside until it's free");
        check(!fs::exists(install.program / "a.dll.old"), "files not in use are deleted straight away");
        check(!fs::exists(install.catalog + "/update/journal.json"), "the journal is gone");

        Manifest manifest;
        check(readManifest(install.catalog + "/installed/retrofe.json", manifest, error), "recorded: " + error);
        check(manifest.kind == "app" && manifest.version == "2.0" && manifest.files.size() == 5, "every file of the version listed");

        // The next start deletes what's left once the old process has gone
        SelfUpdater next(install.catalog, install.app);
        next.sleep = [](int) {};
        check(!next.recover(), "nothing to put back");
        check(!fs::exists(install.program / "retrofe.exe.old"), "the old program is deleted");
        check(!fs::exists(install.catalog + "/update/leftovers.json"), "nothing left to delete");
    });

    run("a file that can't be swapped puts every file back", [&] {
        FakeInstall install = makeInstall("self-update-fail");
        makeZip(install.root + "/share/up.zip", updateEntries);
        SelfUpdater updater(install.catalog, install.app);
        // The new a.dll can't be moved in, by rename or by copy
        updater.renameFile = [](const fs::path& from, const fs::path& to) {
            if (to.filename() == "a.dll" && from.filename() != "a.dll.old") return false;
            return plainRename(from, to);
        };
        check(!updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "failed");
        check(error.find("Nothing was changed") != std::string::npos, "says so: " + error);
        checkUnchanged(install, "a.dll failed");
        check(!fs::exists(install.catalog + "/installed/retrofe.json"), "not recorded");

        // The program itself, last, fails to move aside: everything before it is undone
        SelfUpdater last(install.catalog, install.app);
        last.renameFile = [](const fs::path& from, const fs::path& to) {
            if (from.filename() == "retrofe.exe" && to.filename() == "retrofe.exe.old") return false;
            return plainRename(from, to);
        };
        check(!last.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "failed: " + error);
        checkUnchanged(install, "exe failed");
    });

    run("an update interrupted part way is undone on the next start", [&] {
        FakeInstall install = makeInstall("self-update-crash");
        makeZip(install.root + "/share/up.zip", updateEntries);
        SelfUpdater updater(install.catalog, install.app);
        int calls = 0;
        updater.renameFile = [&](const fs::path& from, const fs::path& to) {
            if (++calls == 4) throw Crash();   // the power goes off after three renames
            return plainRename(from, to);
        };
        bool crashed = false;
        try {
            updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error);
        }
        catch (const Crash&) {
            crashed = true;
        }
        check(crashed && fs::exists(install.catalog + "/update/journal.json"), "stopped mid-swap with a journal");
        check(readFile((install.program / "a.dll").string()) == "new a", "some files were already new");

        SelfUpdater after(install.catalog, install.app);
        after.sleep = [](int) {};
        check(after.recover(), "files were put back, so RetroFE restarts");
        checkUnchanged(install, "after recovery");
        SelfUpdater again(install.catalog, install.app);
        check(!again.recover(), "only once");

        // An update that had swapped everything but not recorded it is finished instead
        Manifest manifest;
        manifest.id = "retrofe";
        manifest.name = "RetroFE";
        manifest.kind = "app";
        manifest.version = "2.0";
        writeManifest(install.catalog + "/update/manifest.json", manifest, error);
        writeFile(install.catalog + "/update/journal.json", R"({"version": "2.0", "done": true, "steps": []})");
        check(!again.recover(), "nothing put back");
        Manifest recorded;
        check(readManifest(install.catalog + "/installed/retrofe.json", recorded, error) && recorded.version == "2.0", "recorded");
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
        refused("not-a-zip", {}, "");
        checkEqual(readFile(install.root + "/settings.conf"), "layout = Shipped", "settings untouched");

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

        makeZip(install.root + "/share/v3.zip", { { "retrofe/retrofe.exe", "exe 3" }, { "retrofe/a.dll", "new a" } });
        install.app.version = "2.0";
        SelfUpdater next(install.catalog, install.app);
        check(next.apply(appPackage("3.0"), install.root + "/share/v3.zip", error), "3.0: " + error);
        checkEqual(readFile((install.program / "retrofe.exe").string()), "exe 3", "updated");
        check(!fs::exists(install.program / "gone.dll"), "a file 3.0 doesn't have is removed");
        checkEqual(readFile((install.program / "edited.dll").string()), "changed by hand", "unless it was changed");
        checkEqual(readFile(install.catalog + "/backups/retrofe/2.0/retrofe/gone.dll"), "gone", "and backed up");
        check(!fs::exists(install.catalog + "/backups/retrofe/1.0"), "older backups are dropped");
    });

#ifdef _WIN32
    run("a loaded library is set aside, replaced and deleted once it's free", [&] {
        FakeInstall install = makeInstall("self-update-loaded");
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        fs::path library = install.program / "zlib-copy.dll";
        fs::copy_file(fs::path(self).parent_path() / "zlib.dll", library);
        HMODULE module = LoadLibraryW(library.wstring().c_str());
        check(module != nullptr, "loaded");
        makeZip(install.root + "/share/up.zip", { { "retrofe/retrofe.exe", "new exe" }, { "retrofe/zlib-copy.dll", "new zlib" } });

        SelfUpdater updater(install.catalog, install.app);   // real renames and deletes
        check(updater.apply(appPackage("2.0"), install.root + "/share/up.zip", error), "applied while loaded: " + error);
        checkEqual(readFile(library.string()), "new zlib", "replaced");
        check(fs::exists(install.program / "zlib-copy.dll.old"), "the loaded one can't be deleted yet");

        FreeLibrary(module);
        SelfUpdater next(install.catalog, install.app);
        next.sleep = [](int) {};
        next.recover();
        check(!fs::exists(install.program / "zlib-copy.dll.old"), "deleted once free");
    });
#endif

    // --- Through the Catalog and its screens ------------------------------------------

    run("RetroFE updates itself from a GitHub source, then asks to restart", [&] {
        FakeInstall install = makeInstall("self-update-service");
        std::string zip = install.root + "/share/up.zip";
        makeZip(zip, updateEntries);
        std::string archive = readFile(zip);
        auto* http = new GitHubHttp();
        http->answers.push_back(answer(200, releaseFor("2.0", archive)));
        for (const char* system : { "windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-x64", "macos-arm64" }) {
            http->files[std::string("https://example.org/RetroFE-2.0-") + system + ".zip"] = archive;
        }
        Service service(install.catalog, targetsUnder(install.root), std::unique_ptr<Http>(http));
        service.setApp(install.app);
        service.start();
        SourceConfig source;
        source.name = "retrofe";
        source.type = "github";
        source.repo = "ennisj/RetroFE";
        source.url = "https://example.org/releases";
        bool added = service.addSource(source, error);
        check(added, "added: " + error);
        service.waitUntilIdle();

        Entry entry;
        check(service.entry("retrofe", entry) && entry.installedVersion == "1.0" && entry.updateAvailable, "the running 1.0 can be updated");
        service.install("retrofe");
        service.waitUntilIdle();
        bool ok = false;
        std::string message;
        check(service.outcome("retrofe", ok, message) && ok && message == "Updated RetroFE to 2.0. Restarting...", "updated: " + message);
        checkEqual(readFile((install.program / "retrofe.exe").string()), "new exe", "the program is new");
        check(service.takeRestartRequest() && !service.takeRestartRequest(), "a restart is asked for once");

        service.remove("retrofe");
        service.waitUntilIdle();
        check(service.outcome("retrofe", ok, message) && !ok && message == "RetroFE can't remove itself.", "not removable: " + message);
        checkEqual(readFile((install.program / "retrofe.exe").string()), "new exe", "still there");

        // The screens say what installing does, and don't offer to remove it
        Configuration::absolutePath = install.root;
        Configuration config;
        ManagerModel model(config);
        model.setCatalog(&service);
        model.selectPackage("retrofe");
        check(model.get("package:notice").find("restarts") != std::string::npos, "the package screen says it restarts");
        checkEqual(model.get("package:canRemove"), "no", "no Remove button");
        checkEqual(model.actionProblem("package.remove"), "RetroFE can't remove itself.", "remove refused");
        check(model.list("info:installed") == std::vector<std::string>({ "RetroFE 2.0" }), "listed as installed");
        checkEqual(model.get("info:hasInstalled"), "no", "but not offered for removal in Manage");
        check(model.options("installed").empty() && model.get("draft:removePackage").empty(), "not in the removal choice");
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
        check(sources.size() == 1 && sources[0].name == "retrofe" && sources[0].type == "github" && sources[0].repo == retrofeRepository,
              "a github source for this fork");
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
        check(sources.size() == 2 && sources[1].repo == "CoinOPS-Official/RetroFE", "repository read from the address");
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

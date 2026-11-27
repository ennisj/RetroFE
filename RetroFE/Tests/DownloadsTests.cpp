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

// Tests for emulator downloads (Source/Downloads): GitHub release lists, the
// built-in emulator list, checksum rules, libretro's core list, the
// installer, the downloader and the Service facade. No network: the Http is
// a fake that serves fixed answers.

#include "TestSupport.h"
#include "DownloadFakes.h"
#include "../Source/Downloads/Download.h"
#include "../Source/Downloads/Emulators.h"
#include "../Source/Downloads/GitHub.h"
#include "../Source/Downloads/Hash.h"
#include "../Source/Downloads/Http.h"
#include "../Source/Downloads/Installer.h"
#include "../Source/Downloads/Package.h"
#include "../Source/Downloads/Service.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Index.h"
#include "../Source/Manager/Emulators/KnownEmulators.h"
#include "../Source/Manager/Emulators/Launchers.h"
#include "minizip/zip.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace Downloads;
using namespace DownloadFakes;

namespace
{
    Http::Response answer(int status, const std::string& body, const std::map<std::string, std::string>& headers = {})
    {
        Http::Response response;
        response.status = status;
        response.body = body;
        response.headers = headers;
        return response;
    }

    std::string sha256Of(const std::string& text)
    {
        Sha256 hash;
        hash.update(text.data(), text.size());
        return hash.hex();
    }

    uint32_t crc32Of(const std::string& text)
    {
        Crc32 crc;
        crc.update(text.data(), text.size());
        return crc.value();
    }

    // A Downloader that doesn't wait between retries; delays records what it would have waited
    Downloader testDownloader(Http* http, const std::string& folder, std::vector<int>& delays)
    {
        Downloader downloader(http, folder);
        downloader.sleep = [&delays](int ms) { delays.push_back(ms); };
        downloader.freeSpace = [](const std::string&, uint64_t& available) {
            available = 1ull << 40;
            return true;
        };
        return downloader;
    }

    std::string githubUrl(const Emulator& entry, const std::string& tag, const std::string& asset)
    {
        return "https://github.com/" + entry.repo + "/releases/download/" + tag + "/" + asset;
    }

    // A package for a built-in entry, as its release list would offer it
    Package emulatorPackage(const std::string& entryId, const std::string& version, const std::string& asset = "download.zip")
    {
        const Emulator* entry = findEmulator("emulator-" + entryId);
        Package package;
        package.id = "emulator-" + entryId;
        package.name = entry ? entry->name : entryId;
        package.kind = "emulator";
        package.version = version;
        PackageFile file;
        if (entryId == "retroarch") {
            file.url = "https://buildbot.libretro.com/stable/" + version + "/windows/x86_64/RetroArch.7z";
            package.noChecksum = true;
        }
        else {
            file.url = entry ? githubUrl(*entry, "v" + version, asset) : "https://github.com/x/y/releases/download/v1/" + asset;
            file.sha256 = std::string(64, 'a');
        }
        file.folder = entry ? entry->folder : entryId;
        package.files.push_back(file);
        return package;
    }

    // A package of one file to download, with its size and hash
    Package remotePackage(const std::string& id, const std::string& url, const std::string& content)
    {
        Package package;
        package.id = id;
        package.name = id;
        package.kind = "emulator";
        package.version = "1";
        PackageFile file;
        file.url = url;
        file.size = content.size();
        file.sha256 = sha256Of(content);
        package.files.push_back(file);
        return package;
    }

    Manifest retroarchManifest()
    {
        Manifest manifest;
        manifest.id = "emulator-retroarch";
        manifest.name = "RetroArch";
        manifest.kind = "emulator";
        manifest.version = "1.22.2";
        return manifest;
    }

    Manifest manifestOf(const Installer& installer, const std::string& id)
    {
        auto installed = installer.installed();
        auto found = installed.find(id);
        return found != installed.end() ? found->second : Manifest();
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    std::unique_ptr<Library::Index> windowsIndex(const std::string& root)
    {
        Library::ScanOptions options;
        options.root = root;
        options.os = "windows";
        options.metadata = false;
        return Library::buildIndex(options);
    }
}

static Suite downloads("downloads", [] {
    std::string error;

    // --- libretro's cores ------------------------------------------------------

    run("core index metadata, dates and pinned downloads", [&] {
        auto root = makeRoot("core-index");
        writeFile(root + "/emulators/RetroArch/info/genesis_plus_gx_libretro.info", "corename = \"Genesis Plus GX\"\nsystemname = \"Sega Mega Drive\"\ndisplay_name = \"Sega (Genesis Plus GX)\"\n");
        std::vector<Package> packages;
        parseCoreIndex("2026-10-28 3fa4ba35 genesis_plus_gx_libretro.dll.zip\n2026-10-29 ABCDEF01 unknown_libretro.dll.zip\n"
                       "2026-02-30 00000000 bad_libretro.dll.zip\n2026-13-01 00000000 bad_libretro.dll.zip\n"
                       "junk\n2026-10-29 xyz bad_libretro.dll.zip\n2026-10-29 12345678 ../bad_libretro.dll.zip\n"
                       "2026-10-29 12345678 https://evil/core_libretro.dll.zip\n", root, packages);
        check(packages.size() == 2, "junk, unsafe names and invalid dates skipped");
        if (packages.size() != 2) return;
        checkEqual(packages[0].name, "Genesis Plus GX", "installed info name");
        checkEqual(packages[0].system, "Sega Mega Drive", "info system");
        checkEqual(packages[0].description, "Sega (Genesis Plus GX)", "info display name");
        checkEqual(packages[1].name, "unknown", "missing info uses stem");
        check(packages[0].files[0].crc32 == 0x3fa4ba35u && packages[1].files[0].crc32 == 0xabcdef01u, "CRC hex");
        checkEqual(packages[0].files[0].url, "https://buildbot.libretro.com/nightly/windows/x86_64/latest/genesis_plus_gx_libretro.dll.zip", "pinned URL");
        check(packages[0].id == "core-genesis_plus_gx" && packages[0].kind == "core" && packages[0].files[0].folder == "RetroArch/cores", "id, kind and folder");
        check(packages[0].totalSize() == 0 && packages[0].files[0].sha256.empty(), "unknown size, no SHA-256");
        check(compareVersions(packages[1].version, packages[0].version) > 0, "nightly date ordering");
        check(checkPackage(packages[0]).empty(), "a listed core passes the safety rules: " + checkPackage(packages[0]));
    });

    run("the core list needs RetroArch and caches Last-Modified", [&] {
        auto root = makeRoot("core-cache");
        auto downloadsFolder = Service::downloadsFolder(root);
        auto http = std::make_unique<FakeHttp>();
        FakeHttp* fake = http.get();
        fake->responses[coreIndexUrl] = answer(200, "2026-10-28 3fa4ba35 genesis_plus_gx_libretro.dll.zip\n",
                                               { { "last-modified", "Wed, 28 Oct 2026 12:00:00 GMT" } });
        Service service(root, std::move(http));
        CoreList none = service.cores();
        check(none.cores.empty() && none.error == "Install RetroArch first" && fake->requested.empty(), "no core request without RetroArch");
        check(service.installCore("genesis_plus_gx").message == "Install RetroArch first.", "no core install without RetroArch");
        check(writeManifest(downloadsFolder + "/installed/emulator-retroarch.json", retroarchManifest(), error), "manifest");
        CoreList first = service.cores();
        check(first.cores.size() == 1 && !first.fromCache && first.error.empty(), "fresh list");
        check(fs::exists(downloadsFolder + "/cache/cores.index"), "list cached");
        checkEqual(readFile(downloadsFolder + "/cache/cores.modified"), "Wed, 28 Oct 2026 12:00:00 GMT", "Last-Modified kept");
        fake->responses[coreIndexUrl] = answer(304, "");
        CoreList cached = service.cores();
        check(cached.cores.size() == 1 && !cached.fromCache, "304 uses the cache");
        check(!fake->sentHeaders.empty() && fake->sentHeaders.back() == std::vector<std::string>{ "If-Modified-Since: Wed, 28 Oct 2026 12:00:00 GMT" }, "conditional date sent");
        fake->responses.clear();
        CoreList offline = service.cores();
        check(offline.cores.size() == 1 && offline.fromCache && !offline.warnings.empty(), "offline uses the last copy");
        auto otherRoot = makeRoot("core-cache-none");
        check(writeManifest(Service::downloadsFolder(otherRoot) + "/installed/emulator-retroarch.json", retroarchManifest(), error), "second manifest");
        Service noHttp(otherRoot, nullptr);
        CoreList unsupported = noHttp.cores();
        check(unsupported.cores.empty() && !unsupported.error.empty(), "no Http and no copy is an error: " + unsupported.error);
    });

    run("core ZIP checks, launchers and removal", [&] {
        auto root = makeRoot("core-install");
        auto downloadsFolder = Service::downloadsFolder(root);
        Installer installer(downloadsFolder, root + "/emulators");
        writeFile(root + "/emulators/RetroArch/retroarch.exe", "program");
        std::vector<Package> packages;
        // CRC-32 of "hello"
        parseCoreIndex("2026-10-28 3610a686 genesis_plus_gx_libretro.dll.zip\n", root, packages);
        check(packages.size() == 1, "one core");
        if (packages.empty()) return;
        Package package = packages[0];
        package.name = "Genesis Plus GX";
        std::string archive = root + "/core.zip";
        check(makeZip(archive, { { "genesis_plus_gx_libretro.dll", "hello" } }), "zip");
        check(!installer.install(package, { archive }, error) && error == "Install RetroArch first", "requires RetroArch's manifest: " + error);
        check(writeManifest(installer.manifestPath("emulator-retroarch"), retroarchManifest(), error), "RetroArch manifest");
        Package bad = package;
        bad.files[0].crc32 = 0;
        check(!installer.install(bad, { archive }, error) && error == "The core doesn't match libretro's list", "CRC mismatch refused: " + error);
        bad = package;
        bad.files[0].crc32.reset();
        check(!installer.install(bad, { archive }, error), "a core without a CRC-32 refused");
        bad = package;
        bad.files[0].url = "https://evil.example/genesis_plus_gx_libretro.dll.zip";
        check(!installer.install(bad, { archive }, error), "foreign URL refused");
        bad = package;
        bad.id = "core-other";
        check(!installer.install(bad, { archive }, error), "another core's URL refused");
        bad = package;
        bad.files[0].folder = "RetroArch";
        check(!installer.install(bad, { archive }, error), "another folder refused");
        bad = package;
        bad.kind = "emulator";
        check(!installer.install(bad, { archive }, error), "a core can't pose as an emulator");
        for (const auto& entries : std::vector<std::vector<std::pair<std::string, std::string>>>{
            { { "genesis_plus_gx_libretro.dll", "hello" }, { "extra.txt", "extra" } },
            { { "wrong_libretro.dll", "hello" } }, { { "folder/", "" } } }) {
            check(makeZip(archive, entries), "bad zip");
            check(!installer.install(package, { archive }, error), "multiple, misnamed and directory entries refused");
        }
        for (unsigned long attributes : { 0x10ul, 0040000ul << 16 }) {
            check(makeZip(archive, { { "genesis_plus_gx_libretro.dll", "hello" } }, attributes), "directory attributes zip");
            check(!installer.install(package, { archive }, error), "directory attributes refused without a trailing slash");
        }
        writeFile(archive, "not a zip");
        check(!installer.install(package, { archive }, error) && error == "A core download must be a ZIP archive", "a core must be a zip");
        check(makeZip(archive, { { "genesis_plus_gx_libretro.dll", "hello" } }), "valid zip");
        check(installer.install(package, { archive }, error), "core installed: " + error);
        checkEqual(readFile(root + "/emulators/RetroArch/cores/genesis_plus_gx_libretro.dll"), "hello", "only core destination");
        Manifest manifest = manifestOf(installer, package.id);
        check(manifest.files.size() == 1 && manifest.files[0].path == "RetroArch/cores/genesis_plus_gx_libretro.dll", "manifest has the DLL");
        check(contains(createEmulatorLauncher(root, downloadsFolder, manifest), "with the launcher RetroArch Genesis Plus GX"), "launcher created");
        const auto launcherPath = root + "/launchers.windows/RetroArch Genesis Plus GX.conf";
        auto launcher = readFile(launcherPath);
        check(contains(launcher, "-L \"%RETROFE_PATH%\\emulators\\RetroArch\\cores\\genesis_plus_gx_libretro.dll\" \"%ITEM_FILEPATH%\""), "portable core arguments");
        check(contains(launcher, "%RETROFE_PATH%\\emulators\\RetroArch\\retroarch.exe"), "portable program");
        check(contains(createEmulatorLauncher(root, downloadsFolder, manifest), "already exists"), "existing launcher left alone");
        writeFile(launcherPath, launcher + "# changed\n");
        check(contains(removeEmulatorLauncher(root, manifest), "changed") && fs::exists(launcherPath), "changed core launcher stays");
        writeFile(launcherPath, launcher);
        writeFile(root + "/collections/Genesis/settings.conf", "launcher = RetroArch Genesis Plus GX\nlist.extensions = md\n");
        check(contains(removeEmulatorLauncher(root, manifest), "Genesis") && fs::exists(launcherPath), "used core launcher stays and names its system");
        fs::remove_all(root + "/collections/Genesis");
        Installer::Removal removal;
        check(!installer.remove("emulator-retroarch", removal, error) && error == "Remove its cores first: Genesis Plus GX", "RetroArch removal refused while cores are installed");
        check(installer.remove(package.id, removal, error) && fs::exists(root + "/emulators/RetroArch/retroarch.exe"), "core removal keeps RetroArch");
        check(!fs::exists(root + "/emulators/RetroArch/cores/genesis_plus_gx_libretro.dll") && !fs::exists(installer.manifestPath(package.id)), "core and manifest gone");
        check(contains(removeEmulatorLauncher(root, manifest), "Removed launcher") && !fs::exists(launcherPath), "unused launcher removed");
        auto name = coreLauncherName("  A=,;%/\\:*?\"<>|# B  " + std::string(90, 'x'));
        check(name.size() <= 64 && Library::Editor::checkLauncherName(name).empty() && name.find("  ") == std::string::npos, "sanitised launcher name fits editor rules");
        checkEqual(coreLauncherName("Genesis Plus GX"), "RetroArch Genesis Plus GX", "plain name");
    });

    run("cores filter by system and list installed ones first", [&] {
        auto root = makeRoot("core-rows");
        auto downloadsFolder = Service::downloadsFolder(root);
        check(writeManifest(downloadsFolder + "/installed/emulator-retroarch.json", retroarchManifest(), error), "RetroArch");
        writeFile(root + "/emulators/RetroArch/info/a_libretro.info", "corename = \"A\"\nsystemname = \"Sega\"\n");
        writeFile(root + "/emulators/RetroArch/info/b_libretro.info", "corename = \"B\"\nsystemname = \"Sega\"\n");
        writeFile(root + "/emulators/RetroArch/info/c_libretro.info", "corename = \"C\"\nsystemname = \"Nintendo\"\n");
        for (const auto& [id, version] : { std::make_pair(std::string("core-b"), std::string("2026-10-28")), std::make_pair(std::string("core-c"), std::string("2026-10-29")) }) {
            Manifest manifest;
            manifest.id = id;
            manifest.kind = "core";
            manifest.name = id.substr(5);
            manifest.version = version;
            check(writeManifest(downloadsFolder + "/installed/" + id + ".json", manifest, error), "core manifest");
        }
        auto http = std::make_unique<FakeHttp>();
        http->responses[coreIndexUrl] = answer(200, "2026-10-29 00000001 a_libretro.dll.zip\n2026-10-29 00000002 b_libretro.dll.zip\n2026-10-29 00000003 c_libretro.dll.zip\n");
        Service service(root, std::move(http));
        CoreList all = service.cores("all");
        check(all.cores.size() == 3, "three cores");
        if (all.cores.size() == 3) {
            check(all.cores[0].id == "core-b" && all.cores[1].id == "core-c" && all.cores[2].id == "core-a", "installed first, then by name");
            check(all.cores[0].updateAvailable && all.cores[0].installedVersion == "2026-10-28", "newer nightly");
            check(!all.cores[1].updateAvailable && all.cores[1].installedVersion == "2026-10-29", "up to date");
            check(all.cores[2].installedVersion.empty() && all.cores[2].crc32 == 1 && all.cores[2].core == "a", "available");
        }
        CoreList sega = service.cores("Sega");
        check(sega.cores.size() == 2 && sega.cores[0].id == "core-b" && sega.cores[1].id == "core-a", "system filter");
        check(service.cores("").cores.size() == 3, "no filter");
    });

    // --- The built-in list and GitHub ------------------------------------------

    run("emulator release rules", [&] {
        const std::vector<std::string> tags = { "v1.22.2", "mame0289", "v0.1-12083", "v2.8.2", "v1.20.4", "v2.7" };
        const std::vector<std::string> versions = { "1.22.2", "0.289", "0.1.12083", "2.8.2", "1.20.4", "2.7" };
        const std::vector<std::vector<std::string>> assets = {
            {}, { "mame0289b_arm64.exe", "mame0289s.exe", "mame0289b_x64.exe" },
            { "duckstation-windows-x64-installer.exe", "duckstation-windows-x64-release-symbols.7z", "duckstation-windows-arm64-release.zip", "duckstation-windows-x64-release.zip" },
            { "pcsx2-v2.8.2-windows-x64-Qt-symbols.7z", "pcsx2-v2.8.2-windows-x64-installer.exe", "pcsx2-v2.8.2-linux-appimage-x64-Qt.AppImage", "pcsx2-v2.8.2-windows-x64-Qt.7z" },
            { "PPSSPP-v1.20.4-Windows-ARM64.zip", "PPSSPP-v1.20.4-Windows-x64.zip" },
            { "flycast-win64-2.7.zip" }
        };
        check(emulators().size() == 6, "six entries");
        for (size_t i = 0; i < emulators().size() && i < tags.size(); ++i) {
            const Emulator& entry = emulators()[i];
            nlohmann::json list = nlohmann::json::array();
            nlohmann::json release = { { "tag_name", tags[i] }, { "assets", nlohmann::json::array() } };
            for (const auto& name : assets[i]) release["assets"].push_back({ { "name", name }, { "browser_download_url", githubUrl(entry, tags[i], name) }, { "size", 12345 }, { "digest", "sha256:" + std::string(64, 'a') } });
            list.push_back(release);
            auto bad = release;
            bad["prerelease"] = true;
            std::vector<Package> excluded;
            std::vector<std::string> excludedWarnings;
            check(parseEmulatorReleases(nlohmann::json::array({ bad }).dump(), entry, excluded, excludedWarnings, error) && excluded.empty(), "prereleases excluded");
            for (const auto& tag : { "latest", "preview", "v999.9.9" }) {
                bad["tag_name"] = tag;
                bad["prerelease"] = tag == std::string("v999.9.9");
                list.push_back(bad);
            }
            bad = release;
            bad["draft"] = true;
            bad["tag_name"] = "v999.9.9";
            list.push_back(bad);
            std::vector<Package> packages;
            std::vector<std::string> warnings;
            check(parseEmulatorReleases(list.dump(), entry, packages, warnings, error), "parses " + tags[i]);
            check(packages.size() == 1, "one package");
            if (packages.empty()) continue;
            if (i != 0) {
                auto hostile = release;
                for (auto& asset : hostile["assets"]) asset["browser_download_url"] = "https://evil.example/program.zip";
                std::vector<Package> refused;
                std::vector<std::string> notes;
                check(parseEmulatorReleases(nlohmann::json::array({ hostile }).dump(), entry, refused, notes, error) && refused.empty() && !notes.empty(), "download origin pinned");
                for (auto& asset : hostile["assets"]) asset["browser_download_url"] = githubUrl(entry, tags[i], "../../../other/x.zip");
                refused.clear();
                check(parseEmulatorReleases(nlohmann::json::array({ hostile }).dump(), entry, refused, notes, error) && refused.empty(), "no climbing out of the release");
                auto cache = makeRoot("origin-cache-" + entry.id);
                writeFile(cache + "/" + entry.id + "/releases.json", nlohmann::json::array({ hostile }).dump());
                ReleaseList cached;
                readGitHubReleases(entry.repo, cache, entry.id, nullptr, 1000,
                    [&](const std::string& text, std::vector<Package>& found, std::vector<std::string>& foundNotes, std::string& problem) {
                        return parseEmulatorReleases(text, entry, found, foundNotes, problem);
                    }, cached);
                check(cached.packages.empty() && !cached.warnings.empty(), "hostile cached download origin refused");
            }
            const auto& package = packages[0];
            checkEqual(package.version, versions[i], "version");
            checkEqual(package.id, "emulator-" + entry.id, "id");
            checkEqual(package.kind, "emulator", "kind");
            checkEqual(package.files[0].folder, entry.folder, "folder");
            check(checkPackage(package).empty(), "passes the safety rules: " + checkPackage(package));
            if (i == 0) {
                checkEqual(package.files[0].url, "https://buildbot.libretro.com/stable/1.22.2/windows/x86_64/RetroArch.7z", "libretro URL");
                check(package.noChecksum && package.files[0].sha256.empty(), "explicit checksum exception");
            }
            else {
                checkEqual(package.files[0].url, githubUrl(entry, tags[i], assets[i].back()), "exact asset");
                check(package.files[0].size == 12345 && package.files[0].sha256 == std::string(64, 'a') && !package.noChecksum, "size and digest");
                release["assets"].back()["digest"] = nullptr;
                packages.clear();
                warnings.clear();
                check(parseEmulatorReleases(nlohmann::json::array({ release }).dump(), entry, packages, warnings, error) && packages.empty() && !warnings.empty(), "missing digest is refused");
            }
            packages.clear();
            check(!parseEmulatorReleases("{}", entry, packages, warnings, error), "wrong JSON type refused");
            check(!parseEmulatorReleases(R"({"message":"API rate limit exceeded"})", entry, packages, warnings, error) && contains(error, "rate limit"), "GitHub's message passed on");
        }
        check(compareVersions("0.289", "0.288") > 0, "MAME versions");
        check(compareVersions("0.1.12083", "0.1.11894") > 0, "DuckStation versions");
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        check(parseEmulatorReleases(R"([{"tag_name":"latest"},{"tag_name":"preview"},{"tag_name":"v2.9.148","prerelease":true}])", emulators()[2], packages, warnings, error) && packages.empty(), "rolling tags excluded");
    });

    run("RetroArch download origin is pinned", [&] {
        auto entry = emulators()[0];
        entry.assetRule = "https://buildbot.libretro.com/other/RetroArch.7z";
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        check(parseEmulatorReleases(R"([{"tag_name":"v1.22.2"}])", entry, packages, warnings, error) && packages.empty() && !warnings.empty(), "buildbot outside stable refused");
    });

    run("checksum and origin rules are the built-in list's", [&] {
        check(checkPackage(emulatorPackage("duckstation", "0.1.12083")).empty(), "a GitHub download with its digest");
        check(checkPackage(emulatorPackage("retroarch", "1.22.2")).empty(), "libretro's RetroArch without one");
        Package package = emulatorPackage("duckstation", "0.1.12083");
        package.files[0].sha256.clear();
        checkEqual(checkPackage(package), "This release has no checksum, so RetroFE won't install it.", "GitHub downloads need GitHub's SHA-256");
        package.noChecksum = true;
        check(!checkPackage(package).empty(), "the no-checksum flag is libretro's only");
        package = emulatorPackage("duckstation", "0.1.12083");
        package.files[0].sha256 = "sha256:abc";
        check(!checkPackage(package).empty(), "a digest that isn't a SHA-256");
        package = emulatorPackage("retroarch", "1.22.2");
        package.files[0].url = "http://buildbot.libretro.com/stable/1.22.2/windows/x86_64/RetroArch.7z";
        check(!checkPackage(package).empty(), "libretro over plain http refused");
        package.files[0].url = "https://github.com/libretro/RetroArch/releases/download/v1.22.2/RetroArch.7z";
        check(!checkPackage(package).empty(), "RetroArch only from libretro's stable builds");
        package = emulatorPackage("duckstation", "0.1.12083");
        package.files[0].url = "https://github.com/someone/duckstation/releases/download/v1/duck.zip";
        check(!checkPackage(package).empty(), "another repository refused");
        package = emulatorPackage("duckstation", "0.1.12083");
        package.files[0].folder = "PCSX2";
        check(!checkPackage(package).empty(), "another entry's folder refused");
        package = emulatorPackage("duckstation", "0.1.12083");
        package.files[0].crc32 = 1;
        check(!checkPackage(package).empty(), "emulators aren't checked by CRC");
        package = emulatorPackage("duckstation", "0.1.12083");
        package.id = "emulator-evil";
        check(!checkPackage(package).empty(), "an entry that isn't built in refused");
        package = emulatorPackage("duckstation", "0.1.12083");
        package.kind = "layout";
        check(!checkPackage(package).empty(), "other kinds refused");
        package.kind = "core";
        check(!checkPackage(package).empty(), "an emulator can't pose as a core");
        package.files.clear();
        check(!checkPackage(package).empty(), "no file");
    });

    run("emulator release caches are separate per entry", [&] {
        auto root = makeRoot("emulator-cache");
        FakeHttp http;
        for (const auto& entry : emulators()) {
            http.documents["https://api.github.com/repos/" + entry.repo + "/releases?per_page=30"] = "[]";
            ReleaseList list;
            readGitHubReleases(entry.repo, root, entry.id, &http, 1000,
                [&](const std::string& text, std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& parseError) {
                    return parseEmulatorReleases(text, entry, packages, warnings, parseError);
                }, list);
            check(list.error.empty() && fs::exists(root + "/" + entry.id + "/releases.json") && fs::exists(root + "/" + entry.id + "/state.json"), "entry cache: " + entry.id);
        }
        ReleaseList bad;
        readGitHubReleases("not a repo", root, "x", &http, 1000, [](const std::string&, std::vector<Package>&, std::vector<std::string>&, std::string&) { return true; }, bad);
        check(!bad.error.empty(), "only owner/name repositories");
    });

    run("emulator entry ETags and shared rate limit", [&] {
        auto root = makeRoot("emulator-etags");
        FakeHttp http;
        for (const auto& entry : emulators()) {
            auto url = "https://api.github.com/repos/" + entry.repo + "/releases?per_page=30";
            http.responses[url] = answer(200, "[]", { { "etag", entry.id } });
            auto parse = [&](const std::string& text, std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& problem) {
                return parseEmulatorReleases(text, entry, packages, warnings, problem);
            };
            ReleaseList first, recent, second;
            readGitHubReleases(entry.repo, root, entry.id, &http, 1000, parse, first);
            auto requests = http.requested.size();
            readGitHubReleases(entry.repo, root, entry.id, &http, 1030, parse, recent);
            check(recent.error.empty() && http.requested.size() == requests, "a list checked under a minute ago isn't asked for again");
            http.responses[url].status = 304;
            readGitHubReleases(entry.repo, root, entry.id, &http, 1100, parse, second);
            check(second.error.empty() && !second.fromCache, "304 uses entry cache");
            check(std::find(http.sentHeaders.back().begin(), http.sentHeaders.back().end(), "If-None-Match: " + entry.id) != http.sentHeaders.back().end(), "entry ETag sent");
        }
        auto url = "https://api.github.com/repos/" + emulators()[0].repo + "/releases?per_page=30";
        http.responses[url].status = 429;
        http.responses[url].headers["retry-after"] = "600";
        auto parse = [](const std::string&, std::vector<Package>&, std::vector<std::string>&, std::string&) { return true; };
        ReleaseList limited;
        readGitHubReleases(emulators()[0].repo, root, emulators()[0].id, &http, 1200, parse, limited);
        check(limited.retryAt == 1800 && limited.fromCache && contains(limited.warnings.empty() ? "" : limited.warnings[0], "GitHub's limit"), "limit reached: last copy and when to retry");
        auto requests = http.requested.size();
        for (const auto& entry : emulators()) {
            ReleaseList blocked;
            readGitHubReleases(entry.repo, root, entry.id, &http, 1300, parse, blocked);
            check(blocked.retryAt == 1800, "every entry waits: " + entry.id);
        }
        check(http.requested.size() == requests, "rate limit shared across entries");
        ReleaseList later;
        http.responses[url].status = 200;
        readGitHubReleases(emulators()[0].repo, root, emulators()[0].id, &http, 1900, parse, later);
        check(http.requested.size() == requests + 1 && later.retryAt == 0, "asked again once the time has come");
        ReleaseList none;
        readGitHubReleases(emulators()[1].repo, makeRoot("emulator-etags-none"), emulators()[1].id, nullptr, 1000, parse, none);
        check(!none.error.empty() && none.packages.empty(), "no Http and no copy is an error");
    });

    run("check reports versions, checksums, offline and the rate limit", [&] {
        auto root = makeRoot("emulator-check");
        auto downloadsFolder = Service::downloadsFolder(root);
        Manifest retroarch = retroarchManifest();
        retroarch.version = "1.22.1";
        check(writeManifest(downloadsFolder + "/installed/emulator-retroarch.json", retroarch, error), "installed RetroArch");
        class ReleasesHttp : public FakeHttp {
        public:
            bool request(const std::string& url, const std::vector<std::string>& headers, Response& response) override {
                sentHeaders.push_back(headers);
                requested.push_back(url);
                response = Response();
                response.status = 200;
                response.body = "[]";
                if (contains(url, "libretro/RetroArch")) response.body = R"([{"tag_name":"v1.22.2"}])";
                if (contains(url, "mamedev/mame")) {
                    response.body = R"([{"tag_name":"mame0289","assets":[{"name":"mame0289b_x64.exe","browser_download_url":"https://github.com/mamedev/mame/releases/download/mame0289/mame0289b_x64.exe"}]}])";
                }
                if (contains(url, "stenzek/duckstation")) {
                    response.error = "Offline";
                    return false;
                }
                if (contains(url, "hrydgard/ppsspp")) {
                    response.status = 403;
                    response.headers["x-ratelimit-remaining"] = "0";
                    response.headers["x-ratelimit-reset"] = "5000";
                }
                return true;
            }
        };
        auto http = std::make_unique<ReleasesHttp>();
        ReleasesHttp* fake = http.get();
        Service service(root, std::move(http));
        service.now = [] { return static_cast<int64_t>(1000); };
        std::vector<std::string> texts;
        auto releases = service.check([&](const std::string& text, uint64_t, uint64_t) { texts.push_back(text); });
        check(releases.size() == emulators().size(), "one row per entry");
        check(std::find(texts.begin(), texts.end(), "Checking DuckStation...") != texts.end(), "progress names the entry");
        if (releases.size() != 6) return;
        check(releases[0].version == "1.22.2" && releases[0].noChecksum && releases[0].error.empty(), "RetroArch: no checksum published");
        check(releases[0].installedVersion == "1.22.1" && releases[0].updateAvailable, "update offered");
        checkEqual(releases[0].url, "https://buildbot.libretro.com/stable/1.22.2/windows/x86_64/RetroArch.7z", "libretro URL");
        checkEqual(releases[1].error, "No checksum on this release, so RetroFE won't install it", "MAME without a digest");
        check(contains(releases[2].error, "Offline"), "offline DuckStation: " + releases[2].error);
        check(contains(releases[3].error, "No release of PCSX2"), "nothing for PCSX2: " + releases[3].error);
        check(contains(releases[4].error, "limit") && releases[4].retryAt == 5000, "rate limit with its time: " + releases[4].error);
        check(contains(releases[5].error, "limit") && releases[5].retryAt == 5000, "Flycast waits for the shared limit");
        check(fake->requested.size() == 5, "Flycast wasn't asked for once the limit was reached");
        std::atomic<bool> stop{ true };
        check(service.check({}, &stop).empty(), "stop before the first entry");
    });

    run("entries with launchers are known to Add a system", [&] {
        for (const auto& entry : emulators()) {
            if (entry.launcher.empty()) continue;
            check(::Emulators::findKnownEmulator(entry.program) != nullptr, "known program: " + entry.program);
            check(Library::Editor::checkLauncherName(entry.launcher).empty(), "launcher name: " + entry.launcher);
            check(::Emulators::unknownPlaceholder(entry.arguments).empty(), "arguments: " + entry.arguments);
        }
        check(findEmulator("emulator-retroarch") && findEmulator("emulator-retroarch")->launcher.empty(), "RetroArch's launchers come with cores");
        check(!findEmulator("retroarch") && !findEmulator("emulator-") && !findEmulator("core-mame"), "only package ids");
    });

    // --- Installing ------------------------------------------------------------

    run("emulator installation, portable files and launchers", [&] {
        auto root = makeRoot("emulator-install");
        auto downloadsFolder = Service::downloadsFolder(root);
        Installer installer(downloadsFolder, root + "/emulators");
        auto zip = root + "/server/duck.zip";
        check(makeZip(zip, { { "duckstation-qt-x64-ReleaseLTCG.exe", "program" }, { "data/readme.txt", "data" } }), "zip");
        Package package = emulatorPackage("duckstation", "0.1.12083");
        Package bad = package;
        bad.id = "emulator-evil";
        check(!installer.install(bad, { zip }, error), "an entry that isn't built in refused");
        writeFile(root + "/emulators/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe", "manual");
        check(!installer.install(package, { zip }, error) && error == "emulators/DuckStation is already there. Rename it, or add a launcher for it instead.", "unowned folder refused");
        checkEqual(installer.folderRefusal(package), "emulators/DuckStation is already there. Rename it, or add a launcher for it instead.", "refusal before downloading");
        fs::remove_all(root + "/emulators/DuckStation");
        writeFile(root + "/emulators/DuckStation/settings.ini", "mine");
        check(!installer.install(package, { zip }, error) && contains(error, "already there"), "any unowned content refused");
        checkEqual(readFile(root + "/emulators/DuckStation/settings.ini"), "mine", "untouched");
        fs::remove_all(root + "/emulators/DuckStation");
        fs::create_directories(root + "/emulators/DuckStation");
        check(installer.install(package, { zip }, error), "install into an empty folder: " + error);
        check(fs::exists(root + "/emulators/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe"), "program installed");
        check(fs::exists(root + "/emulators/DuckStation/portable.txt") && fs::file_size(root + "/emulators/DuckStation/portable.txt") == 0, "portable marker");
        check(!fs::exists(downloadsFolder + "/staging/emulator-duckstation"), "staging cleaned up");
        Manifest manifest = manifestOf(installer, package.id);
        check(manifest.files.size() == 3 && std::count_if(manifest.files.begin(), manifest.files.end(), [](const InstalledFile& file) { return file.portable; }) == 1, "two files and the portable marker recorded");
        writeFile(root + "/collections/DuckStation/launcher.windows.conf", "executable = other.exe\n");
        check(contains(createEmulatorLauncher(root, downloadsFolder, manifest), "left alone") && manifest.launcherPath.empty(), "collection launcher name blocks creation");
        fs::remove_all(root + "/collections/DuckStation");
        checkEqual(createEmulatorLauncher(root, downloadsFolder, manifest), ", with the launcher DuckStation.", "created launcher");
        auto launcher = root + "/launchers.windows/DuckStation.conf";
        auto content = readFile(launcher);
        check(contains(content, R"(%RETROFE_PATH%\emulators\DuckStation\duckstation-qt-x64-ReleaseLTCG.exe)") && contains(content, R"(-batch -fullscreen -- "%ITEM_FILEPATH%")"), "launcher executable and arguments");
        checkEqual(createEmulatorLauncher(root, downloadsFolder, manifest), "; a launcher called DuckStation already exists, so it was left as it is.", "existing launcher message");
        check(readFile(launcher) == content, "existing launcher untouched");
        Manifest retroarch = retroarchManifest();
        checkEqual(createEmulatorLauncher(root, downloadsFolder, retroarch), " Add a core next from Cores.", "RetroArch explains cores");
        writeFile(root + "/emulators/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe", "self-updated");
        package.version = "0.1.12084";
        check(installer.install(package, { zip }, error), "update: " + error);
        checkEqual(readFile(root + "/emulators/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe"), "program", "changed program replaced during update");
        check(contains(installer.installMessage(), "Replaced 1 changed file"), "changed replacement reported");
        checkEqual(readFile(downloadsFolder + "/backups/" + package.id + "/0.1.12083/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe"), "self-updated", "changed file backed up");
        manifest = manifestOf(installer, package.id);
        check(manifest.version == "0.1.12084" && !manifest.launcherSha256.empty() && fs::exists(root + "/emulators/DuckStation/portable.txt"), "update keeps launcher and portable ownership");
        writeFile(launcher, content + "# edited\n");
        check(contains(removeEmulatorLauncher(root, manifest), "changed") && fs::exists(launcher), "changed launcher kept");
        writeFile(launcher, content);
        writeFile(root + "/collections/PlayStation/settings.conf", "launcher = DuckStation\nlist.extensions = bin\n");
        check(contains(removeEmulatorLauncher(root, manifest), "PlayStation") && fs::exists(launcher), "used launcher kept with system name");
        fs::remove_all(root + "/collections/PlayStation");
        check(contains(removeEmulatorLauncher(root, manifest), "Removed launcher") && !fs::exists(launcher), "unused launcher removed");
        Installer::Removal removal;
        check(installer.remove(package.id, removal, error) && removal.left.empty(), "remove: " + error);
        check(!fs::exists(root + "/emulators/DuckStation/portable.txt") && !fs::exists(root + "/emulators/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe"), "program and empty portable file removed");
        check(!fs::exists(root + "/emulators/DuckStation") && !fs::exists(installer.manifestPath(package.id)), "folder and manifest gone");
        check(!installer.remove(package.id, removal, error), "removing again fails");
        // Settings it leaves behind keep the folder this module's
        check(installer.install(package, { zip }, error), "install again: " + error);
        writeFile(root + "/emulators/DuckStation/settings.ini", "settings");
        writeFile(root + "/emulators/DuckStation/data/readme.txt", "changed");
        check(installer.remove(package.id, removal, error) && removal.kept == 1, "remove keeps the changed file");
        check(removal.left == std::vector<std::string>({ "DuckStation/data/readme.txt", "DuckStation/settings.ini" }), "says what was left");
        Manifest left;
        check(readManifest(installer.manifestPath(package.id), left, error) && left.removed && left.files.empty(), "a removed record");
        check(installer.installed().count(package.id) == 0, "not listed as installed");
        check(installer.install(package, { zip }, error), "reinstall into what it left: " + error);
        checkEqual(readFile(root + "/emulators/DuckStation/settings.ini"), "settings", "settings untouched");
        checkEqual(readFile(root + "/emulators/DuckStation/data/readme.txt"), "data", "shipped file installed again");
        check(manifestOf(installer, package.id).version == "0.1.12084", "installed again");
        checkEqual(readFile(downloadsFolder + "/backups/" + package.id + "/before-0.1.12084/DuckStation/data/readme.txt"), "changed", "the changed file it replaced is backed up");
        check(installer.remove(package.id, removal, error) && removal.restored == 1, "remove puts it back");
        checkEqual(readFile(root + "/emulators/DuckStation/data/readme.txt"), "changed", "put back");
        fs::remove_all(root + "/emulators/DuckStation");
        fs::remove(installer.manifestPath(package.id));
        writeFile(root + "/emulators/DuckStation/settings.ini", "someone else's");
        check(!installer.install(package, { zip }, error) && contains(error, "already there"), "without the record the folder isn't this module's");
    });

    run("emulator updates replace shipped files and preserve unrelated files", [&] {
        auto root = makeRoot("emulator-update-tree");
        auto downloadsFolder = Service::downloadsFolder(root);
        Installer installer(downloadsFolder, root + "/emulators");
        auto zip = root + "/archive.zip";
        check(makeZip(zip, { { "pcsx2-qt.exe", "v1" }, { "qt.dll", "v1" }, { "dropped.dll", "old" } }), "v1 archive");
        Package package = emulatorPackage("pcsx2", "1");
        check(installer.install(package, { zip }, error), "v1 installed: " + error);
        writeFile(root + "/emulators/PCSX2/qt.dll", "self-updated");
        writeFile(root + "/emulators/PCSX2/settings.ini", "settings");
        check(makeZip(zip, { { "pcsx2-qt.exe", "v2" }, { "qt.dll", "v2" } }), "v2 archive");
        package.version = "2";
        check(installer.install(package, { zip }, error), "v2 installed: " + error);
        checkEqual(readFile(root + "/emulators/PCSX2/pcsx2-qt.exe"), "v2", "unchanged shipped program replaced");
        checkEqual(readFile(root + "/emulators/PCSX2/qt.dll"), "v2", "changed shipped library replaced");
        check(!fs::exists(root + "/emulators/PCSX2/dropped.dll"), "dropped file removed");
        checkEqual(readFile(root + "/emulators/PCSX2/settings.ini"), "settings", "settings stay");
        checkEqual(readFile(downloadsFolder + "/backups/emulator-pcsx2/1/PCSX2/qt.dll"), "self-updated", "changed library backed up");
        writeFile(root + "/emulators/PCSX2/qt.dll", "changed again");
        Installer::Removal removal;
        check(installer.remove(package.id, removal, error) && removal.kept == 1, "remove keeps changed file");
        check(std::find(removal.left.begin(), removal.left.end(), "PCSX2/settings.ini") != removal.left.end(), "settings left");
        check(installer.install(package, { zip }, error), "reinstall with changed leftovers succeeds: " + error);
        checkEqual(readFile(root + "/emulators/PCSX2/settings.ini"), "settings", "reinstall preserves settings");
    });

    run("updates prune old backups", [&] {
        auto root = makeRoot("backup-prune");
        auto downloadsFolder = Service::downloadsFolder(root);
        Installer installer(downloadsFolder, root + "/emulators");
        for (const char* version : { "1", "2", "3", "4" }) {
            check(makeZip(root + "/dl/p.zip", { { "flycast.exe", std::string("v") + version } }), "zip");
            check(installer.install(emulatorPackage("flycast", version), { root + "/dl/p.zip" }, error), std::string("v") + version + ": " + error);
        }
        int folders = 0;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(downloadsFolder + "/backups/emulator-flycast", ec)) {
            (void)entry;
            folders++;
        }
        check(folders == 1 && fs::exists(downloadsFolder + "/backups/emulator-flycast/3"), "only the last update's backup");
    });

    run("RetroArch strips its top folder and refuses outside entries", [&] {
        auto root = makeRoot("retroarch-install");
        Installer installer(Service::downloadsFolder(root), root + "/emulators");
        auto zip = root + "/archive.zip";
        check(makeZip(zip, { { "RetroArch-Win64/retroarch.exe", "program" }, { "RetroArch-Win64/info/a.info", "x" } }), "archive");
        Package package = emulatorPackage("retroarch", "1.22.2");
        check(installer.install(package, { zip }, error), "install: " + error);
        check(fs::exists(root + "/emulators/RetroArch/retroarch.exe") && fs::exists(root + "/emulators/RetroArch/info/a.info")
              && !fs::exists(root + "/emulators/RetroArch/RetroArch-Win64"), "top folder removed");
        for (const auto& name : { "RetroArch-Win64/../escape.txt", "RetroArch-Win64", "RetroArch-Win64/dir\\..\\..\\escape.txt", "RetroArch-Win64/" }) {
            check(makeZip(zip, { { name, "bad" } }), "hostile archive");
            check(!installer.install(package, { zip }, error), std::string("hostile entry refused: ") + name);
        }
        writeFile(zip, "not an archive");
        check(!installer.install(package, { zip }, error) && contains(error, "isn't an archive RetroFE can unpack"), "a download must unpack");
        check(makeZip(zip, { { "outside.txt", "bad" } }), "outside archive");
        check(!installer.install(package, { zip }, error) && contains(error, "outside"), "outside entry refused");
        check(makeZip(zip, { { "RetroArch-Win64/readme.txt", "no program" } }), "archive without the program");
        check(!installer.install(package, { zip }, error) && contains(error, "does not contain retroarch.exe"), "the program must be there");
        check(fs::exists(root + "/emulators/RetroArch/retroarch.exe") && !fs::exists(root + "/escape.txt") && !fs::exists(root + "/emulators/escape.txt"), "nothing changed or escaped");
    });

    run("7z and self-extracting archives install; unsafe or damaged ones are refused", [&] {
        auto root = makeRoot("emulator-seven");
        auto downloadsFolder = Service::downloadsFolder(root);
        Installer installer(downloadsFolder, root + "/emulators");
        std::string fixtures = std::string(RETROFE_TEST_FIXTURES) + "/7z/";
        auto expected = nlohmann::json::parse(readFile(fixtures + "expected.json"));
        auto zip = root + "/program.zip";
        check(makeZip(zip, { { "pcsx2-qt.exe", "program" } }), "program zip");
        Package package = emulatorPackage("pcsx2", "2.8.2", "pcsx2-v2.8.2-windows-x64-Qt.7z");
        package.files.push_back(emulatorPackage("pcsx2", "2.8.2", "program.zip").files[0]);
        check(installer.install(package, { fixtures + "plain.7z", zip }, error), "7z unpacks: " + error);
        Manifest manifest = manifestOf(installer, package.id);
        check(manifest.files.size() == expected["files"].size() + 2, "7z content, program and portable marker recorded");
        for (const auto& file : manifest.files) {
            if (file.path.rfind("PCSX2/Top/", 0) != 0) continue;
            std::string name = file.path.substr(6);
            check(expected["files"].contains(name), "manifest name " + name);
            if (!expected["files"].contains(name)) continue;
            check(file.size == expected["files"][name]["size"] && file.sha256 == expected["files"][name]["sha256"], "manifest size and hash");
            check(sha256File(fs::u8path(root + "/emulators/" + file.path)) == file.sha256, "installed bytes");
        }
        writeFile(root + "/emulators/PCSX2/portable.txt", "changed");
        Installer::Removal removal;
        check(installer.remove(package.id, removal, error), "remove");
        check(!fs::exists(root + "/emulators/PCSX2/pcsx2-qt.exe") && readFile(root + "/emulators/PCSX2/portable.txt") == "changed" && removal.kept == 1, "program removed and changed portable file kept");

        auto mameZip = root + "/mame.zip";
        check(makeZip(mameZip, { { "mame.exe", "program" } }), "MAME program zip");
        Package mame = emulatorPackage("mame", "0.289", "mame0289b_x64.exe");
        mame.files.push_back(emulatorPackage("mame", "0.289", "mame.zip").files[0]);
        check(installer.install(mame, { fixtures + "sfx.exe", mameZip }, error), "a self-extracting archive is read, not run: " + error);
        check(fs::exists(root + "/emulators/MAME/Top/readme.txt") && fs::exists(root + "/emulators/MAME/mame.exe"), "SFX content installed");
        check(installer.remove(mame.id, removal, error), "MAME removed");

        for (const char* bad : { "unsafe.7z", "corrupt.7z" }) {
            error.clear();
            check(!installer.install(mame, { fixtures + bad, mameZip }, error), "refused " + std::string(bad));
            check(contains(error, std::string(bad) == "unsafe.7z" ? "safely" : "Damaged"), "clear error: " + error);
            check(!fs::exists(root + "/emulators/MAME") && !fs::exists(installer.manifestPath(mame.id)), "no files outside staging and no manifest");
        }
    });

    run("archives that reach outside their folder are refused", [&] {
        auto root = makeRoot("zip-slip");
        Installer installer(Service::downloadsFolder(root), root + "/emulators");
        check(makeZip(root + "/dl/evil.zip", { { "flycast.exe", "fine" }, { "../../escaped.txt", "bad" } }), "zip");
        check(!installer.install(emulatorPackage("flycast", "2.7"), { root + "/dl/evil.zip" }, error), "refused");
        check(!error.empty(), "says why: " + error);
        check(!fs::exists(root + "/emulators/Flycast/flycast.exe"), "nothing installed");
        check(!fs::exists(fs::path(root).parent_path() / "escaped.txt") && !fs::exists(root + "/escaped.txt"), "nothing escaped");
        check(installer.installed().empty(), "no manifest");
    });

    run("an install that can't be completed changes nothing", [&] {
        auto root = makeRoot("rollback");
        auto downloadsFolder = Service::downloadsFolder(root);
        Installer installer(downloadsFolder, root + "/emulators");
        Manifest left;
        left.id = "emulator-flycast";
        left.name = "Flycast";
        left.kind = "emulator";
        left.removed = true;
        check(writeManifest(installer.manifestPath(left.id), left, error), "a folder this module left");
        writeFile(root + "/emulators/Flycast/a.txt", "the user's a");
        fs::create_directories(root + "/emulators/Flycast/b.txt");   // a folder where a file should go
        check(makeZip(root + "/dl/p.zip", { { "flycast.exe", "program" }, { "a.txt", "theirs" }, { "b.txt", "theirs" } }), "zip");
        check(!installer.install(emulatorPackage("flycast", "2.7"), { root + "/dl/p.zip" }, error), "failed: " + error);
        checkEqual(readFile(root + "/emulators/Flycast/a.txt"), "the user's a", "untouched");
        check(!fs::exists(root + "/emulators/Flycast/flycast.exe"), "nothing moved in");
        check(installer.installed().empty() && !fs::exists(downloadsFolder + "/staging/emulator-flycast"), "no manifest, nothing left staged");
    });

    run("a recovery only moves this install's staged files into its own folder", [&] {
        auto root = makeRoot("recover-staged");
        auto downloadsFolder = Service::downloadsFolder(root);
        writeFile(root + "/settings.conf", "the user's settings");
        Installer installer(downloadsFolder, root + "/emulators");
        std::string sha = sha256File(root + "/settings.conf");
        auto plan = [&](const std::string& id, const std::string& kind, const std::string& path, const std::string& staged) {
            std::string folder = downloadsFolder + "/staging/" + id;
            writeFile(folder + "/plan.json", nlohmann::json{ { "id", id }, { "name", id }, { "kind", kind }, { "version", "1" },
                { "files", nlohmann::json::array({ { { "path", path }, { "size", 19 }, { "sha256", sha }, { "staged", staged } } }) } }.dump());
            writeFile(folder + "/.ready", "");
            if (staged.rfind("files/", 0) == 0) writeFile(folder + "/" + staged, "the user's settings");
        };
        plan("emulator-duckstation", "emulator", "DuckStation/stolen.txt", "../../../../settings.conf");
        installer.recover();
        checkEqual(readFile(root + "/settings.conf"), "the user's settings", "not moved");
        check(!fs::exists(root + "/emulators/DuckStation/stolen.txt") && !fs::exists(downloadsFolder + "/staging/emulator-duckstation"), "refused and not retried");
        plan("emulator-duckstation", "emulator", "MAME/x.txt", "files/0/x.txt");
        plan("emulator-evil", "emulator", "Evil/x.txt", "files/0/x.txt");
        plan("emulator-pcsx2", "emulator", "../retrofe/x.txt", "files/0/x.txt");
        plan("core-a", "core", "RetroArch/cores/b_libretro.dll", "files/0/x.txt");
        plan("emulator-flycast", "core", "Flycast/x.txt", "files/0/x.txt");
        installer.recover();
        check(!fs::exists(root + "/emulators/MAME/x.txt") && !fs::exists(root + "/emulators/Evil/x.txt") && !fs::exists(root + "/retrofe/x.txt")
              && !fs::exists(root + "/emulators/RetroArch/cores/b_libretro.dll") && !fs::exists(root + "/emulators/Flycast/x.txt"), "no path outside an entry's own folder");
        check(installer.installed().empty() && fs::is_empty(downloadsFolder + "/staging"), "nothing recorded; staging cleared");
    });

    run("an interrupted install is finished or discarded before downloads are used", [&] {
        auto root = makeRoot("recover");
        auto downloadsFolder = Service::downloadsFolder(root);
        {
            Installer installer(downloadsFolder, root + "/emulators");
            check(makeZip(root + "/dl/p.zip", { { "flycast.exe", "a" } }), "zip");
            check(installer.install(emulatorPackage("flycast", "2.7"), { root + "/dl/p.zip" }, error), "installed: " + error);
        }
        // Put the install back into staging as if it stopped right after being marked ready
        std::string staging = downloadsFolder + "/staging/emulator-flycast";
        fs::create_directories(staging + "/files/0");
        fs::rename(root + "/emulators/Flycast/flycast.exe", staging + "/files/0/flycast.exe");
        fs::remove(downloadsFolder + "/installed/emulator-flycast.json");
        std::string sha = sha256File(staging + "/files/0/flycast.exe");
        writeFile(staging + "/plan.json",
                  R"({"id":"emulator-flycast","name":"Flycast","kind":"emulator","version":"2.7","files":[{"path":"Flycast/flycast.exe","size":1,"sha256":")" + sha + R"(","staged":"files/0/flycast.exe"}]})");
        writeFile(staging + "/.ready", "");
        // And one that never got that far
        writeFile(downloadsFolder + "/staging/emulator-mame/files/0/mame.exe", "b");

        Service service(root, nullptr);
        auto entries = service.list();
        check(fs::exists(root + "/emulators/Flycast/flycast.exe"), "ready install finished");
        auto flycast = std::find_if(entries.begin(), entries.end(), [](const EmulatorStatus& status) { return status.id == "emulator-flycast"; });
        check(flycast != entries.end() && flycast->installedVersion == "2.7", "and listed as installed");
        check(!fs::exists(downloadsFolder + "/staging/emulator-mame") && !fs::exists(root + "/emulators/MAME"), "unfinished one discarded");
        check(entries.size() == emulators().size() && entries[0].launcher.empty() && entries[2].launcher == "DuckStation", "the built-in list");
    });

    // --- Hashes, versions and paths ---------------------------------------------

    run("SHA-256 and CRC-32 match the standard test vectors", [&] {
        checkEqual(sha256Of(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
        checkEqual(sha256Of("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
        checkEqual(sha256Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                   "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 448 bits");
        checkEqual(sha256Of(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 million a");
        check(crc32Of("") == 0, "crc32 empty");
        check(crc32Of("123456789") == 0xcbf43926u, "crc32 check value");
        check(crc32Of("hello") == 0x3610a686u, "crc32 hello");
        check(crc32Of("The quick brown fox jumps over the lazy dog") == 0x414fa339u, "crc32 fox");
    });

    run("hashing in pieces matches hashing at once", [&] {
        std::string data;
        for (int i = 0; i < 5000; i++) data += static_cast<char>(i * 7);
        std::string expected = sha256Of(data);
        Sha256 pieces;
        Crc32 crcPieces;
        for (size_t i = 0; i < data.size(); i += 61) {
            pieces.update(data.data() + i, std::min<size_t>(61, data.size() - i));
            crcPieces.update(data.data() + i, std::min<size_t>(61, data.size() - i));
        }
        checkEqual(pieces.hex(), expected, "sha256");
        check(crcPieces.value() == crc32Of(data), "crc32");
        std::string root = makeRoot("hash-file");
        writeFile(root + "/f.bin", data);
        checkEqual(sha256File(root + "/f.bin"), expected, "sha256File");
        checkEqual(sha256File(root + "/missing.bin"), "", "missing file");
        checkEqual(parseSha256File("sha256:" + std::string(64, 'B')), std::string(64, 'b'), "GitHub's digest");
        checkEqual(parseSha256File(std::string(64, 'c') + "  a.zip\n" + std::string(64, 'd') + "  b.zip\n", "b.zip"), std::string(64, 'd'), "the named file's line");
        checkEqual(parseSha256File("sha256:1234"), "", "too short");
    });

    run("versions compare by their numbers", [&] {
        check(compareVersions("1.10", "1.9") > 0, "1.10 > 1.9");
        check(compareVersions("1.0", "1") == 0, "1.0 == 1");
        check(compareVersions("v2.0b13", "v2.0b9") > 0, "v2.0b13 > v2.0b9");
        check(compareVersions("2.1.0", "2.1.1") < 0, "2.1.0 < 2.1.1");
        check(compareVersions("", "1") < 0, "no version is older");
        check(compareVersions("2026-10-29", "2026-10-28") > 0 && compareVersions("2026-11-01", "2026-10-31") > 0, "nightly dates");
    });

    run("ids, paths and addresses that could escape are refused", [&] {
        check(isValidId("emulator-duckstation") && isValidId("core-genesis_plus_gx"), "normal ids");
        check(!isValidId("") && !isValidId("../x") && !isValidId("a/b") && !isValidId(".hidden"), "bad ids");
        check(isSafeRelativePath("") && isSafeRelativePath("DuckStation/data"), "safe paths");
        check(!isSafeRelativePath("../x") && !isSafeRelativePath("a/../../x") && !isSafeRelativePath("/x")
              && !isSafeRelativePath("C:\\x") && !isSafeRelativePath("a\\..\\b"), "unsafe paths");
        for (const char* path : { "CON", "nul.txt", "a/Aux", "COM1.dat", "lpt9", "name.", "dir /x", "a|b", "x\ty", "a:stream" }) {
            check(!isSafeRelativePath(path), std::string("refused: ") + path);
        }
        for (const char* path : { "console.txt", "com10", "a/./b.txt", "Sega Genesis/roms/a.zip" }) {
            check(isSafeRelativePath(path), std::string("allowed: ") + path);
        }
        check(!isValidId("con") && !isValidId("pack.") && isValidId("pack.v2"), "ids");
        check(isHttpsUrl("https://github.com/a/b") && isHttpsUrl("HTTPS://github.com/a/b"), "https");
        check(!isHttpsUrl("http://github.com/a/b") && !isHttpsUrl("https://") && !isHttpsUrl("https://a b") && !isHttpsUrl("E:/a.zip"), "nothing else");
        check(isCoreName("genesis_plus_gx") && !isCoreName("../x") && !isCoreName("A") && !isCoreName(""), "core names");
        check(isCoreId("core-mame2003_plus") && !isCoreId("core-") && !isCoreId("emulator-mame"), "core ids");
    });

    // --- Downloading -----------------------------------------------------------

    run("partial downloads are continued, kept or started again", [&] {
        check(planResume(false, 0, 100) == Resume::Fresh, "nothing there");
        check(planResume(true, 0, 100) == Resume::Fresh, "empty");
        check(planResume(true, 40, 100) == Resume::Continue, "part of it");
        check(planResume(true, 100, 100) == Resume::Complete, "all of it");
        check(planResume(true, 140, 100) == Resume::Discard, "too much");
        check(planResume(true, 40, 0) == Resume::Continue, "size unknown");
    });

    run("retries wait 2 s, doubling to 60 s", [&] {
        check(backoffDelay(1) == 2000 && backoffDelay(2) == 4000 && backoffDelay(5) == 32000, "doubling");
        check(backoffDelay(6) == 60000 && backoffDelay(20) == 60000, "capped");
        uint64_t mb = 1024 * 1024;
        check(spaceNeeded(100 * mb, 0) == 264 * mb, "download, extract and margin");
        check(spaceNeeded(100 * mb, 40 * mb) == 224 * mb, "less what's downloaded");
        checkEqual(describeSize(512), "512 bytes", "bytes");
        checkEqual(describeSize(48 * mb + mb / 5), "48.2 MB", "megabytes");
    });

    run("a download is verified and kept for the next try", [&] {
        std::string root = makeRoot("download");
        std::string content = "a package's bytes";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        Package package = remotePackage("p", "https://h.org/p.zip", content);

        std::vector<std::pair<uint64_t, uint64_t>> reports;
        Downloader::Result result;
        check(downloader.fetch(package, [&](uint64_t received, uint64_t total) {
            reports.push_back({ received, total });
            return true;
        }, result), "fetched: " + result.error);
        check(result.files.size() == 1, "one file");
        if (result.files.size() == 1) {
            checkEqual(readFile(result.files[0]), content, "content");
            check(fs::path(result.files[0]).extension() != ".part", "no longer partial");
            check(fs::path(result.files[0]).parent_path() == fs::path(root + "/partial/p"), "in the partial folder");
        }
        check(!reports.empty() && reports.back().first == content.size() && reports.back().second == content.size(), "progress reaches the total");

        Downloader::Result again;
        check(downloader.fetch(package, nullptr, again) && http.offsets.size() == 1, "not downloaded twice");
        downloader.discard("p");
        check(!fs::exists(root + "/partial/p"), "discarded");
    });

    run("a dropped connection resumes where it stopped", [&] {
        std::string root = makeRoot("download-resume");
        std::string content = "0123456789abcdefghij";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        http.dropAfter = { 5, 4 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        Downloader::Result result;
        check(downloader.fetch(remotePackage("p", "https://h.org/p.zip", content), nullptr, result), "fetched: " + result.error);
        check(http.offsets == std::vector<uint64_t>({ 0, 5, 9 }), "continued from 5, then 9");
        check(delays == std::vector<int>({ 2000, 4000 }), "waited between tries");
        check(result.files.size() == 1 && readFile(result.files[0]) == content, "whole file");
    });

    run("after five tries it gives up, keeping what it has", [&] {
        std::string root = makeRoot("download-give-up");
        std::string content = "0123456789";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        http.dropAfter = { 1, 1, 1, 1, 1 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        Package package = remotePackage("p", "https://h.org/p.zip", content);
        Downloader::Result result;
        check(!downloader.fetch(package, nullptr, result), "failed");
        check(http.offsets.size() == 5 && delays.size() == 4, "five tries");
        check(contains(result.error, "connection was lost"), "says why: " + result.error);

        Downloader::Result later;
        check(downloader.fetch(package, nullptr, later), "finished later: " + later.error);
        check(http.offsets.back() == 5, "from where it stopped");
    });

    run("servers that can't resume start again", [&] {
        std::string root = makeRoot("download-no-ranges");
        std::string content = "0123456789";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        http.ranges = false;
        http.dropAfter = { 4 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        Downloader::Result result;
        check(downloader.fetch(remotePackage("p", "https://h.org/p.zip", content), nullptr, result), "fetched: " + result.error);
        check(result.files.size() == 1 && readFile(result.files[0]) == content, "whole file, not doubled");
    });

    run("a damaged download is never used", [&] {
        std::string root = makeRoot("download-damaged");
        ServingHttp http;
        http.files["https://h.org/p.zip"] = "not what was published";
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        Package package = remotePackage("p", "https://h.org/p.zip", "what was published!!!");
        package.files[0].size = 0;   // only the hash can tell
        Downloader::Result result;
        check(!downloader.fetch(package, nullptr, result), "refused");
        check(contains(result.error, "checksum"), "says why: " + result.error);
        check(http.offsets.size() == 1, "not retried: a fresh download that's wrong stays wrong");
        check(fs::is_empty(root + "/partial/p"), "nothing left behind");

        Package sized = remotePackage("q", "https://h.org/p.zip", "too short");
        Downloader::Result wrongSize;
        check(!downloader.fetch(sized, nullptr, wrongSize) && contains(wrongSize.error, "should be"), "wrong size: " + wrongSize.error);
    });

    run("a partial joined to a changed file is downloaded again", [&] {
        std::string root = makeRoot("download-changed");
        std::string oldContent = "OLDOLDOLDO";
        std::string newContent = "new bytes!";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = oldContent;
        http.dropAfter = { 5 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        downloader.tries = 1;
        Package package = remotePackage("p", "https://h.org/p.zip", newContent);
        Downloader::Result first;
        check(!downloader.fetch(package, nullptr, first), "first try dropped");

        http.files["https://h.org/p.zip"] = newContent;
        downloader.tries = 5;
        Downloader::Result result;
        check(downloader.fetch(package, nullptr, result), "fetched: " + result.error);
        check(http.offsets == std::vector<uint64_t>({ 0, 5, 0 }), "continued, found wrong, started again");
        check(result.files.size() == 1 && readFile(result.files[0]) == newContent, "the new file");
    });

    run("a file changed on the server isn't joined to the old partial", [&] {
        std::string root = makeRoot("download-if-range");
        std::string oldContent = "OLDOLDOLDO";
        std::string newContent = "new bytes!";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = oldContent;
        http.etags["https://h.org/p.zip"] = "\"v1\"";
        http.dropAfter = { 5 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        downloader.tries = 1;
        Package package = remotePackage("p", "https://h.org/p.zip", newContent);
        package.files[0].sha256.clear();   // no hash to catch it: only the size, which is the same
        Downloader::Result first;
        check(!downloader.fetch(package, nullptr, first), "first try dropped");

        http.files["https://h.org/p.zip"] = newContent;
        http.etags["https://h.org/p.zip"] = "\"v2\"";
        Downloader::Result result;
        check(downloader.fetch(package, nullptr, result), "fetched: " + result.error);
        check(result.files.size() == 1 && readFile(result.files[0]) == newContent, "the whole new file");
    });

    run("downloads check for space, can be stopped and need https", [&] {
        std::string root = makeRoot("download-space");
        std::string content = "0123456789";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        downloader.freeSpace = [](const std::string&, uint64_t& available) {
            available = 1000;
            return true;
        };
        Package package = remotePackage("p", "https://h.org/p.zip", content);
        Downloader::Result full;
        check(!downloader.fetch(package, nullptr, full), "refused");
        check(contains(full.error, "Not enough free space") && http.offsets.empty(), "before downloading: " + full.error);

        Downloader stoppable = testDownloader(&http, root + "/partial", delays);
        Downloader::Result stopped;
        check(!stoppable.fetch(package, [](uint64_t, uint64_t) { return false; }, stopped) && stopped.cancelled, "stopped");

        http.files["http://h.org/p.zip"] = content;
        Downloader::Result plain;
        check(!stoppable.fetch(remotePackage("plain", "http://h.org/p.zip", content), nullptr, plain) && contains(plain.error, "https://"), "plain http refused");
        Downloader::Result local;
        check(!stoppable.fetch(remotePackage("local", root + "/p.zip", content), nullptr, local), "local files refused");
        Downloader none = testDownloader(nullptr, root + "/partial", delays);
        Downloader::Result unsupported;
        check(!none.fetch(package, nullptr, unsupported) && contains(unsupported.error, "isn't supported"), "no Http: " + unsupported.error);
    });

    run("old versions' downloads are cleared out", [&] {
        std::string root = makeRoot("download-stale");
        writeFile(root + "/partial/p/0-oldversion.part", "stale");
        ServingHttp http;
        http.files["https://h.org/p.zip"] = "fresh";
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/partial", delays);
        Downloader::Result result;
        check(downloader.fetch(remotePackage("p", "https://h.org/p.zip", "fresh"), nullptr, result), "fetched: " + result.error);
        check(!fs::exists(root + "/partial/p/0-oldversion.part"), "stale partial removed");
    });

    // --- The Service -------------------------------------------------------------

    run("installing through the service creates the launcher; removal keeps a used one", [&] {
        auto root = makeRoot("service-emulator");
        auto downloadsFolder = Service::downloadsFolder(root);
        auto zip = makeRoot("service-emulator-server") + "/duck.zip";
        check(makeZip(zip, { { "duckstation-qt-x64-ReleaseLTCG.exe", "program" } }), "archive");
        const Emulator& duckstation = *findEmulator("emulator-duckstation");
        auto url = githubUrl(duckstation, "v0.1-12083", "duckstation-windows-x64-release.zip");
        auto http = std::make_unique<ServingHttp>();
        ServingHttp* fake = http.get();
        fake->files[url] = readFile(zip);
        for (const auto& entry : emulators()) fake->documents["https://api.github.com/repos/" + entry.repo + "/releases?per_page=30"] = "[]";
        fake->documents["https://api.github.com/repos/stenzek/duckstation/releases?per_page=30"] = nlohmann::json::array({ { { "tag_name", "v0.1-12083" },
            { "assets", nlohmann::json::array({ { { "name", "duckstation-windows-x64-release.zip" }, { "browser_download_url", url },
                                                  { "size", readFile(zip).size() }, { "digest", "sha256:" + sha256File(zip) } } }) } } }).dump();
        Service service(root, std::move(http));
        int64_t seconds = 1000;
        service.now = [&] { return seconds; };
        check(!service.supported() || fs::path::preferred_separator == '\\', "downloads are for Windows");

        std::atomic<bool> stop{ true };
        Outcome cancelled = service.install("emulator-duckstation", {}, &stop);
        check(!cancelled.ok && cancelled.cancelled && fake->offsets.empty(), "stopped before downloading");

        std::vector<std::pair<std::string, uint64_t>> reports;
        Outcome installed = service.install("emulator-duckstation", [&](const std::string& text, uint64_t done, uint64_t) { reports.push_back({ text, done }); });
        check(installed.ok, "installed: " + installed.message);
        checkEqual(installed.message, R"(DuckStation is installed in emulators\DuckStation, with the launcher DuckStation.)", "install names folder and launcher");
        check(installed.libraryChanged && fs::exists(root + "/launchers.windows/DuckStation.conf"), "launcher written; rescan asked for");
        check(std::any_of(reports.begin(), reports.end(), [&](const auto& report) { return report.first == "Downloading DuckStation 0.1.12083" && report.second == readFile(zip).size(); }), "download progress in bytes");
        check(!fs::exists(downloadsFolder + "/partial/emulator-duckstation"), "finished download discarded");
        auto index = windowsIndex(root);
        check(index && index->findLauncher(Library::LauncherScope::Global, "DuckStation") >= 0, "the library sees the launcher");
        auto entries = service.list();
        check(entries.size() == 6 && entries[2].installedVersion == "0.1.12083" && entries[2].launcherCreated == "DuckStation", "listed as installed with its launcher");
        seconds += 120;
        Outcome again = service.install("emulator-duckstation");
        check(again.ok && contains(again.message, "up to date") && fake->offsets.size() == 1, "nothing to do: " + again.message);
        check(!service.install("emulator-evil").ok && !service.install("duckstation").ok, "only built-in package ids");

        writeFile(root + "/emulators/DuckStation/settings.ini", "settings");
        Outcome removed = service.remove("emulator-duckstation");
        check(removed.ok && contains(removed.message, "Removed launcher DuckStation") && removed.libraryChanged, "service removes launcher: " + removed.message);
        check(contains(removed.message, R"(Left in emulators\DuckStation: settings.ini.)"), "says what was left: " + removed.message);
        check(!fs::exists(root + "/launchers.windows/DuckStation.conf") && !fs::exists(root + "/emulators/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe"), "files gone");
        check(!service.remove("emulator-duckstation").ok, "not installed any more");

        seconds += 120;
        Outcome reinstalled = service.install("emulator-duckstation");
        check(reinstalled.ok && fs::exists(root + "/launchers.windows/DuckStation.conf"), "reinstall recreates the launcher: " + reinstalled.message);
        checkEqual(readFile(root + "/emulators/DuckStation/settings.ini"), "settings", "settings kept across");
        writeFile(root + "/collections/PlayStation/settings.conf", "launcher = Other, DuckStation\n");
        Outcome kept = service.remove("emulator-duckstation");
        check(kept.ok && contains(kept.message, "PlayStation") && !kept.libraryChanged, "names the alternative launcher's user: " + kept.message);
        check(fs::exists(root + "/launchers.windows/DuckStation.conf"), "keeps a used launcher");

        // Nothing written outside manage/, emulators/ and launchers.windows/ (and the test's own collection)
        std::set<std::string> top;
        for (const auto& entry : fs::directory_iterator(root)) top.insert(entry.path().filename().string());
        check(top == std::set<std::string>({ "collections", "emulators", "launchers.windows", "manage" }), "only those folders");
        std::set<std::string> manage;
        for (const auto& entry : fs::directory_iterator(root + "/manage")) manage.insert(entry.path().filename().string());
        check(manage.count("downloads") && (manage.size() == 1 || (manage.size() == 2 && manage.count("backups"))), "manage/downloads, and the launcher editor's backups");
    });

    run("the service refuses a folder it didn't install, and GitHub downloads without a digest", [&] {
        auto root = makeRoot("service-refusals");
        auto http = std::make_unique<ServingHttp>();
        ServingHttp* fake = http.get();
        const Emulator& mame = *findEmulator("emulator-mame");
        auto url = githubUrl(mame, "mame0289", "mame0289b_x64.exe");
        fake->files[url] = "anything";
        fake->documents["https://api.github.com/repos/mamedev/mame/releases?per_page=30"] = nlohmann::json::array({ { { "tag_name", "mame0289" },
            { "assets", nlohmann::json::array({ { { "name", "mame0289b_x64.exe" }, { "browser_download_url", url }, { "digest", "sha256:" + std::string(64, 'a') } } }) } } }).dump();
        fake->documents["https://api.github.com/repos/flyinghead/flycast/releases?per_page=30"] = nlohmann::json::array({ { { "tag_name", "v2.7" },
            { "assets", nlohmann::json::array({ { { "name", "flycast-win64-2.7.zip" }, { "browser_download_url", githubUrl(*findEmulator("emulator-flycast"), "v2.7", "flycast-win64-2.7.zip") } } }) } } }).dump();
        writeFile(root + "/emulators/MAME/mame.exe", "the user's own");
        Service service(root, std::move(http));
        Outcome refused = service.install("emulator-mame");
        checkEqual(refused.message, "emulators/MAME is already there. Rename it, or add a launcher for it instead.", "existing folder refused");
        check(!refused.ok && fake->offsets.empty(), "before downloading anything");
        Outcome unchecked = service.install("emulator-flycast");
        check(!unchecked.ok && contains(unchecked.message, "No checksum") && fake->offsets.empty(), "no digest, no download: " + unchecked.message);
        Service none(makeRoot("service-no-http"), nullptr);
        check(!none.supported() && !none.install("emulator-mame").ok && !none.installCore("genesis_plus_gx").ok, "nothing without an Http");
    });

    run("cores install through the service with their launcher; RetroArch waits for them", [&] {
        auto root = makeRoot("service-cores");
        auto downloadsFolder = Service::downloadsFolder(root);
        writeFile(root + "/emulators/RetroArch/retroarch.exe", "program");
        writeFile(root + "/emulators/RetroArch/info/genesis_plus_gx_libretro.info", "corename = \"Genesis Plus GX\"\nsystemname = \"Sega\"\n");
        Manifest retroarch = retroarchManifest();
        InstalledFile program;
        program.path = "RetroArch/retroarch.exe";
        program.size = 7;
        program.sha256 = sha256Of("program");
        retroarch.files.push_back(program);
        check(writeManifest(downloadsFolder + "/installed/emulator-retroarch.json", retroarch, error), "RetroArch installed");
        auto zip = makeRoot("service-cores-server") + "/core.zip";
        check(makeZip(zip, { { "genesis_plus_gx_libretro.dll", "hello" } }), "core zip");
        auto http = std::make_unique<ServingHttp>();
        ServingHttp* fake = http.get();
        fake->documents[coreIndexUrl] = "2026-10-28 3610a686 genesis_plus_gx_libretro.dll.zip\n";
        fake->files[coreUrl("genesis_plus_gx")] = readFile(zip);
        Service service(root, std::move(http));
        Outcome installed = service.installCore("genesis_plus_gx");
        check(installed.ok && installed.libraryChanged, "core installed: " + installed.message);
        checkEqual(installed.message, "Genesis Plus GX is installed, with the launcher RetroArch Genesis Plus GX.", "core install names launcher");
        check(fs::exists(root + "/launchers.windows/RetroArch Genesis Plus GX.conf"), "core launcher written");
        Outcome parent = service.remove("emulator-retroarch");
        check(!parent.ok && parent.message == "Remove its cores first: Genesis Plus GX", "RetroArch refused while cores are installed: " + parent.message);
        check(contains(service.installCore("genesis_plus_gx").message, "up to date"), "nothing newer");
        fake->documents[coreIndexUrl] = "2026-10-29 3610a686 genesis_plus_gx_libretro.dll.zip\n";
        CoreList cores = service.cores("Sega");
        check(cores.cores.size() == 1 && cores.cores[0].updateAvailable && cores.cores[0].name == "Genesis Plus GX", "a newer nightly");
        Outcome updated = service.installCore("genesis_plus_gx");
        checkEqual(updated.message, "Updated to Genesis Plus GX 2026-10-29.", "core update");
        check(!service.installCore("../evil").ok && !service.installCore("missing").ok, "only listed cores");
        Outcome removed = service.removeCore("genesis_plus_gx");
        checkEqual(removed.message, "Removed Genesis Plus GX. Removed launcher RetroArch Genesis Plus GX.", "core removal with its launcher");
        check(!fs::exists(root + "/emulators/RetroArch/cores/genesis_plus_gx_libretro.dll") && fs::exists(root + "/emulators/RetroArch/retroarch.exe"), "only the core went");
        check(!service.removeCore("genesis_plus_gx").ok, "not installed any more");
        Outcome parentNow = service.remove("emulator-retroarch");
        check(parentNow.ok && !fs::exists(root + "/emulators/RetroArch/retroarch.exe"), "RetroArch removable without cores: " + parentNow.message);
        check(contains(parentNow.message, "Left in emulators\\RetroArch: info/genesis_plus_gx_libretro.info"), "says what was left: " + parentNow.message);
        checkEqual(service.installCore("genesis_plus_gx").message, "Install RetroArch first.", "cores need RetroArch");
    });
});

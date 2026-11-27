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

// Tests for emulator downloads on Linux and macOS (Source/Downloads,
// Manager/Downloads): every platform's table, cores per platform, rolling
// releases, the libcurl Http's text handling, AppImages, disk images (through
// a fake image opener: no hdiutil), the Unix modes and links archives carry,
// and the screens' "No ... build". Each test picks the platform whose table
// it checks, so they run on every system; the ones about permissions and
// links on disk need Unix, and Windows checks that links are refused. No
// network.

#include "TestSupport.h"
#include "DownloadFakes.h"
#include "SevenZipWriter.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Downloads/Emulators.h"
#include "../Source/Downloads/GitHub.h"
#include "../Source/Downloads/Hash.h"
#include "../Source/Downloads/Http.h"
#include "../Source/Downloads/Installer.h"
#include "../Source/Downloads/Package.h"
#include "../Source/Downloads/Service.h"
#include "../Source/Forms/Form.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Manager/Downloads/DownloadProvider.h"
#include "../Source/Manager/Emulators/KnownEmulators.h"
#include "../Source/Manager/ManagerModel.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace Downloads;
using namespace DownloadFakes;

namespace
{
    // Uses a platform's table until the end of the scope
    struct OnPlatform
    {
        explicit OnPlatform(Platform platform) : before(Downloads::platform()) { usePlatform(platform); }
        ~OnPlatform() { usePlatform(before); }
        Platform before;
    };

    const std::vector<Platform> allPlatforms = { Platform::WindowsX64, Platform::LinuxX64, Platform::LinuxArm64,
                                                 Platform::MacArm64, Platform::MacX64, Platform::Other };

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    std::string sha256Of(const std::string& text)
    {
        Sha256 hash;
        hash.update(text.data(), text.size());
        return hash.hex();
    }

    // What an AppImage starts with
    std::string elf(const std::string& rest)
    {
        return std::string("\x7f" "ELF", 4) + rest;
    }

    // A file a disk image could be: its UDIF trailer 512 bytes from the end
    std::string diskImage()
    {
        std::string image(2048, '\0');
        image.replace(image.size() - 512, 4, "koly");
        return image;
    }

    std::string githubUrl(const Emulator& entry, const std::string& tag, const std::string& asset)
    {
        return "https://github.com/" + entry.repo + "/releases/download/" + tag + "/" + asset;
    }

    const Emulator& entryOf(Platform platform, const std::string& id)
    {
        for (const Emulator& entry : emulators(platform)) if (entry.id == id) return entry;
        static const Emulator none;
        return none;
    }

    // The package platform()'s list offers for an entry's asset
    Package packageFor(const std::string& id, const std::string& version, const std::string& url)
    {
        const Emulator* entry = findEmulator("emulator-" + id);
        Package package;
        package.id = "emulator-" + id;
        package.name = entry ? entry->name : id;
        package.kind = "emulator";
        package.version = version;
        PackageFile file;
        file.url = url;
        if (id == "retroarch") package.noChecksum = true;
        else file.sha256 = std::string(64, 'a');
        file.folder = entry ? entry->folder : id;
        package.files.push_back(file);
        return package;
    }

    Manifest retroArchManifest()
    {
        Manifest manifest;
        manifest.id = "emulator-retroarch";
        manifest.name = "RetroArch";
        manifest.kind = "emulator";
        manifest.version = "1.22.2";
        return manifest;
    }

#ifndef _WIN32
    // A file's permission bits
    unsigned modeOf(const std::string& path)
    {
        std::error_code ec;
        return static_cast<unsigned>(fs::status(path, ec).permissions()) & 07777;
    }

    bool isLinkTo(const std::string& path, const std::string& target)
    {
        std::error_code ec;
        return fs::is_symlink(fs::symlink_status(path, ec)) && fs::read_symlink(path, ec).generic_string() == target;
    }
#endif

    // A disk image "attached" by copying a prepared folder to the mount point,
    // and "detached" by emptying it; counts both
    struct FakeImages
    {
        std::string contents;   // the folder the image holds
        int attached = 0, detached = 0;
        bool attachFails = false;
        std::string lastMount;

        ImageOpener opener()
        {
            ImageOpener result;
            result.attach = [this](const std::string& image, const std::string& mountPoint, std::string& error) {
                (void)image;
                attached++;
                lastMount = mountPoint;
                if (attachFails) { error = "Could not open the disk image"; return false; }
                std::error_code ec;
                fs::copy(contents, mountPoint, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
                if (ec) { error = ec.message(); return false; }
                return true;
            };
            result.detach = [this](const std::string& mountPoint) {
                detached++;
                std::error_code ec;
                for (auto it = fs::directory_iterator(mountPoint, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
                    fs::remove_all(it->path(), ec);
                }
            };
            return result;
        }
    };

    std::string releasesUrl(const std::string& repo, bool latest)
    {
        return "https://api.github.com/repos/" + repo + (latest ? "/releases/latest" : "/releases?per_page=30");
    }

    // GitHub's rolling DuckStation release, as /releases/latest gives it
    std::string rollingRelease(const std::string& published, const std::string& asset, const std::string& content)
    {
        const Emulator& entry = entryOf(Platform::LinuxX64, "duckstation");
        return nlohmann::json({ { "tag_name", "latest" }, { "published_at", published }, { "prerelease", false },
            { "assets", nlohmann::json::array({ { { "name", asset }, { "browser_download_url", githubUrl(entry, "latest", asset) },
                { "size", content.size() }, { "digest", "sha256:" + sha256Of(content) } } }) } }).dump();
    }

    // The manager with the download screens, as DownloadScreensTests has it
    struct Screens
    {
        Screens(const std::string& root, std::unique_ptr<Http> http, std::optional<bool> supported = true)
            : indexer(options(root))
            , editor(root)
        {
            Configuration::absolutePath = root;
            config = std::make_unique<Configuration>();
            model = std::make_unique<ManagerModel>(*config);
            Manager::Services services{ *config };
            services.library = &indexer;
            services.editor = &editor;
            Manager::DownloadProvider::Hooks hooks;
            auto owned = std::make_shared<std::unique_ptr<Http>>(std::move(http));
            hooks.http = [owned] { return std::move(*owned); };
            hooks.supported = supported;
            hooks.prepare = [](Service& service) {
                service.now = [] { return static_cast<int64_t>(1000); };
                service.freeSpace = [](const std::string&, uint64_t& available) { available = 1ull << 40; return true; };
            };
            model->addProvider(std::make_unique<Manager::DownloadProvider>(services, hooks));
            indexer.rebuild();
            indexer.wait(10000);
        }

        static Library::ScanOptions options(const std::string& root)
        {
            Library::ScanOptions scan;
            scan.root = root;
            scan.os = launcherOs(platform());
            scan.metadata = false;
            return scan;
        }

        bool waitForJob()
        {
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (model->get("download:running") == "yes" && std::chrono::steady_clock::now() < until) {
                model->update(0.01f);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            model->update(0.01f);
            return model->get("download:running") == "no";
        }

        std::string detail(const std::string& id)
        {
            Forms::Rows rows = model->rows("download:emulators", 100);
            for (const Forms::Row& row : rows.items) if (row.id == id) return row.detail;
            return "(no row)";
        }

        std::string confirmation(const std::string& form, const std::string& action)
        {
            std::string error;
            auto parsed = Forms::parse(Forms::builtinForm(form), error);
            const Forms::Field* button = parsed ? Forms::findField(*parsed, action) : nullptr;
            return button ? Forms::confirmText(*button, *model) : "(no button)";
        }

        Library::Indexer indexer;
        Library::Editor editor;
        std::unique_ptr<Configuration> config;
        std::unique_ptr<ManagerModel> model;
    };
}

static Suite downloadsPlatforms("downloads on Linux and macOS", [] {
    std::string error;

    // --- The tables ------------------------------------------------------------

    run("platform tables: what each emulator publishes, per platform", [&] {
        struct Build { std::string asset, archive, program, topFolder, appFolder; };
        const std::map<Platform, std::map<std::string, Build>> expected = {
            { Platform::WindowsX64, {
                { "retroarch", { "https://buildbot.libretro.com/stable/<version>/windows/x86_64/RetroArch.7z", "7z", "retroarch.exe", "RetroArch-Win64/", "" } },
                { "mame", { "<tag>b_x64.exe", "sfx", "mame.exe", "", "" } },
                { "duckstation", { "duckstation-windows-x64-release.zip", "zip", "duckstation-qt-x64-ReleaseLTCG.exe", "", "" } },
                { "pcsx2", { "pcsx2-v<version>-windows-x64-Qt.7z", "7z", "pcsx2-qt.exe", "", "" } },
                { "ppsspp", { "PPSSPP-v<version>-Windows-x64.zip", "zip", "PPSSPPWindows64.exe", "", "" } },
                { "flycast", { "flycast-win64-<version>.zip", "zip", "flycast.exe", "", "" } } } },
            { Platform::LinuxX64, {
                { "retroarch", { "https://buildbot.libretro.com/stable/<version>/linux/x86_64/RetroArch.7z", "7z", "RetroArch-Linux-x86_64.AppImage", "RetroArch-Linux-x86_64/", "" } },
                { "mame", {} },
                { "duckstation", { "DuckStation-x64.AppImage", "appimage", "DuckStation.AppImage", "", "" } },
                { "pcsx2", { "pcsx2-v<version>-linux-appimage-x64-Qt.AppImage", "appimage", "PCSX2.AppImage", "", "" } },
                { "ppsspp", { "PPSSPP-v<version>-anylinux-x86_64.AppImage", "appimage", "PPSSPP.AppImage", "", "" } },
                { "flycast", { "flycast-x86_64-<version>.AppImage", "appimage", "Flycast.AppImage", "", "" } } } },
            { Platform::LinuxArm64, {
                { "retroarch", {} }, { "mame", {} }, { "pcsx2", {} }, { "flycast", {} },
                { "duckstation", { "DuckStation-arm64.AppImage", "appimage", "DuckStation.AppImage", "", "" } },
                { "ppsspp", { "PPSSPP-v<version>-anylinux-aarch64.AppImage", "appimage", "PPSSPP.AppImage", "", "" } } } },
        };
        std::map<std::string, Build> mac = {
            { "retroarch", { "https://buildbot.libretro.com/stable/<version>/apple/osx/universal/RetroArch_Metal.dmg", "dmg", "RetroArch.app/Contents/MacOS/RetroArch", "", "RetroArch.app" } },
            { "mame", {} }, { "pcsx2", {} },
            { "duckstation", { "duckstation-mac-release.zip", "zip", "DuckStation.app/Contents/MacOS/DuckStation", "", "" } },
            { "ppsspp", { "PPSSPPSDL-macOS-v<version>.zip", "zip", "PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL", "", "" } },
            { "flycast", { "flycast-macOS-<version>.zip", "zip", "Flycast.app/Contents/MacOS/Flycast", "", "" } } };
        auto all = expected;
        all[Platform::MacArm64] = mac;
        all[Platform::MacX64] = mac;
        for (const auto& [platform, builds] : all) {
            const auto& entries = emulators(platform);
            check(entries.size() == 6, platformName(platform) + ": six entries");
            for (size_t i = 0; i < entries.size(); i++) {
                const Emulator& entry = entries[i];
                checkEqual(entry.id, emulators(Platform::WindowsX64)[i].id, platformName(platform) + ": the same order");
                checkEqual(entry.folder, emulators(Platform::WindowsX64)[i].folder, platformName(platform) + ": the same folder");
                auto build = builds.find(entry.id);
                check(build != builds.end(), platformName(platform) + " lists " + entry.id);
                if (build == builds.end()) continue;
                const std::string what = platformName(platform) + " " + entry.id;
                checkEqual(entry.assetRule, build->second.asset, what + ": asset");
                checkEqual(entry.archive, build->second.archive, what + ": form");
                checkEqual(entry.program, build->second.program, what + ": program");
                checkEqual(entry.topFolder, build->second.topFolder, what + ": top folder");
                checkEqual(entry.appFolder, build->second.appFolder, what + ": application folder");
                check(entry.available() == !build->second.asset.empty(), what + ": available only with a build");
                check(entry.available() || (entry.program.empty() && entry.portableFiles.empty()), what + ": nothing else without a build");
            }
        }
        for (const Emulator& entry : emulators(Platform::Other)) check(!entry.available(), "nothing elsewhere: " + entry.id);
        // Portable rules and arguments: the Windows ones where it's the same program's command line
        check(entryOf(Platform::LinuxX64, "duckstation").portableFiles == std::vector<std::string>{ "portable.txt" }
              && entryOf(Platform::LinuxX64, "pcsx2").portableFiles == std::vector<std::string>{ "portable.txt" }
              && entryOf(Platform::MacArm64, "duckstation").portableFiles.empty(), "portable files per platform");
        for (Platform platform : allPlatforms) {
            for (const Emulator& entry : emulators(platform)) {
                checkEqual(entry.arguments, entryOf(Platform::WindowsX64, entry.id).arguments, platformName(platform) + " " + entry.id + ": arguments");
                checkEqual(entry.launcher, entryOf(Platform::WindowsX64, entry.id).launcher, platformName(platform) + " " + entry.id + ": launcher");
                // Add a system knows each downloaded program, to suggest its system
                if (entry.available() && !entry.launcher.empty()) {
                    check(::Emulators::findKnownEmulator("%RETROFE_PATH%/emulators/" + entry.folder + "/" + entry.program) != nullptr,
                          platformName(platform) + ": Add a system knows " + entry.program);
                }
            }
        }
        const Emulator& rolling = entryOf(Platform::LinuxArm64, "duckstation");
        check(rolling.rolling && rolling.latestOnly && rolling.tagRule == "^(latest)$", "Linux DuckStation is the rolling release");
        check(!entryOf(Platform::MacArm64, "duckstation").rolling && entryOf(Platform::MacArm64, "duckstation").tagRule == entryOf(Platform::WindowsX64, "duckstation").tagRule,
              "macOS DuckStation is the tagged release, as on Windows");
        check(entryOf(Platform::LinuxX64, "pcsx2").latestOnly, "PCSX2 reads /releases/latest, as on Windows");
        checkEqual(launcherOs(Platform::WindowsX64), "windows", "launchers.windows");
        checkEqual(launcherOs(Platform::LinuxArm64), "linux", "launchers.linux");
        checkEqual(launcherOs(Platform::MacX64), "apple", "launchers.apple");
        checkEqual(platformName(Platform::LinuxArm64), "Linux arm64", "a platform's name");
    });

    run("platform tables: cores and their index per platform", [&] {
        checkEqual(coreUrl("snes9x", Platform::WindowsX64), "https://buildbot.libretro.com/nightly/windows/x86_64/latest/snes9x_libretro.dll.zip", "Windows core");
        checkEqual(coreUrl("snes9x", Platform::LinuxX64), "https://buildbot.libretro.com/nightly/linux/x86_64/latest/snes9x_libretro.so.zip", "Linux core");
        checkEqual(coreUrl("snes9x", Platform::MacArm64), "https://buildbot.libretro.com/nightly/apple/osx/arm64/latest/snes9x_libretro.dylib.zip", "macOS arm64 core");
        checkEqual(coreUrl("snes9x", Platform::MacX64), "https://buildbot.libretro.com/nightly/apple/osx/x86_64/latest/snes9x_libretro.dylib.zip", "macOS x86_64 core");
        checkEqual(coreIndexUrlFor(Platform::WindowsX64), coreIndexUrl, "Windows index");
        checkEqual(coreIndexUrlFor(Platform::LinuxX64), "https://buildbot.libretro.com/nightly/linux/x86_64/latest/.index-extended", "Linux index");
        checkEqual(coreIndexUrlFor(Platform::MacArm64), "https://buildbot.libretro.com/nightly/apple/osx/arm64/latest/.index-extended", "macOS arm64 index");
        checkEqual(coreIndexUrlFor(Platform::MacX64), "https://buildbot.libretro.com/nightly/apple/osx/x86_64/latest/.index-extended", "macOS x86_64 index");
        check(coreIndexUrlFor(Platform::LinuxArm64).empty() && coreUrl("snes9x", Platform::LinuxArm64).empty() && !coresOffered(Platform::LinuxArm64),
              "no cores without a stable RetroArch (Linux arm64)");
        check(coresOffered(Platform::WindowsX64) && coresOffered(Platform::LinuxX64) && coresOffered(Platform::MacArm64) && coresOffered(Platform::MacX64)
              && !coresOffered(Platform::Other), "cores where RetroArch is");
        checkEqual(coreLibrary("a", Platform::LinuxX64), "a_libretro.so", "a Linux core's library");
        checkEqual(coreLibrary("a", Platform::MacX64), "a_libretro.dylib", "a macOS core's library");

        auto root = makeRoot("platform-core-index");
        const std::string index = "2026-10-28 3fa4ba35 genesis_plus_gx_libretro.dll.zip\n2026-10-28 11111111 genesis_plus_gx_libretro.so.zip\n"
                                  "2026-10-28 22222222 snes9x_libretro.dylib.zip\n2026-10-28 33333333 ../x_libretro.so.zip\n";
        writeFile(root + "/emulators/RetroArch/RetroArch-Linux-x86_64.AppImage.home/.config/retroarch/info/genesis_plus_gx_libretro.info",
                  "corename = \"Genesis Plus GX\"\nsystemname = \"Sega\"\n");
        {
            OnPlatform onLinux(Platform::LinuxX64);
            std::vector<Package> packages;
            parseCoreIndex(index, root, packages);
            check(packages.size() == 1 && packages[0].files[0].crc32 == 0x11111111u, "Linux reads only .so cores");
            if (packages.size() == 1) {
                checkEqual(packages[0].files[0].url, coreUrl("genesis_plus_gx", Platform::LinuxX64), "the Linux core's URL");
                checkEqual(packages[0].name, "Genesis Plus GX", "named from the AppImage's portable home");
                check(checkPackage(packages[0]).empty(), "passes the safety rules: " + checkPackage(packages[0]));
                Package other = packages[0];
                other.files[0].url = coreUrl("genesis_plus_gx", Platform::WindowsX64);
                check(!checkPackage(other).empty(), "another platform's core refused");
            }
        }
        {
            OnPlatform mac(Platform::MacArm64);
            std::vector<Package> packages;
            parseCoreIndex(index, root, packages);
            check(packages.size() == 1 && packages[0].id == "core-snes9x", "macOS reads only .dylib cores");
        }
        {
            OnPlatform arm(Platform::LinuxArm64);
            std::vector<Package> packages;
            parseCoreIndex(index, root, packages);
            check(packages.empty(), "Linux arm64 has none");
            FakeHttp http;
            CoreIndex none;
            readCores(root + "/cache", root, &http, none);
            check(none.packages.empty() && contains(none.error, "no Linux arm64 build") && http.requested.empty(), "nothing asked for: " + none.error);
        }
        {
            OnPlatform onLinux(Platform::LinuxX64);
            FakeHttp http;
            http.documents[coreIndexUrlFor(Platform::LinuxX64)] = index;
            CoreIndex list;
            readCores(root + "/cache", root, &http, list);
            check(list.packages.size() == 1 && http.requested.size() == 1 && http.requested[0] == coreIndexUrlFor(Platform::LinuxX64), "the Linux list is read");
            check(fs::exists(root + "/cache/cores-linux-x86_64.index") && !fs::exists(root + "/cache/cores.index"), "and kept apart from Windows'");
        }
    });

    run("platform tables: each platform's release picks its own asset", [&] {
        // One release of each with every platform's assets, as the projects publish them
        const std::map<std::string, std::pair<std::string, std::vector<std::string>>> releases = {
            { "duckstation", { "v0.1-12083", { "duckstation-windows-x64-release.zip", "duckstation-mac-release.zip", "DuckStation-x64.AppImage" } } },
            { "pcsx2", { "v2.8.2", { "pcsx2-v2.8.2-windows-x64-Qt.7z", "pcsx2-v2.8.2-linux-appimage-x64-Qt.AppImage", "pcsx2-v2.8.2-macos-Qt.tar.xz" } } },
            { "ppsspp", { "v1.20.4", { "PPSSPP-v1.20.4-Windows-x64.zip", "PPSSPP-v1.20.4-anylinux-x86_64.AppImage", "PPSSPP-v1.20.4-anylinux-aarch64.AppImage", "PPSSPPSDL-macOS-v1.20.4.zip" } } },
            { "flycast", { "v2.7", { "flycast-win64-2.7.zip", "flycast-x86_64-2.7.AppImage", "flycast-macOS-2.7.zip" } } },
            { "mame", { "mame0289", { "mame0289b_x64.exe" } } },
        };
        const std::map<std::pair<Platform, std::string>, std::string> picked = {
            { { Platform::LinuxX64, "pcsx2" }, "pcsx2-v2.8.2-linux-appimage-x64-Qt.AppImage" },
            { { Platform::LinuxX64, "ppsspp" }, "PPSSPP-v1.20.4-anylinux-x86_64.AppImage" },
            { { Platform::LinuxX64, "flycast" }, "flycast-x86_64-2.7.AppImage" },
            { { Platform::LinuxArm64, "ppsspp" }, "PPSSPP-v1.20.4-anylinux-aarch64.AppImage" },
            { { Platform::MacArm64, "duckstation" }, "duckstation-mac-release.zip" },
            { { Platform::MacArm64, "ppsspp" }, "PPSSPPSDL-macOS-v1.20.4.zip" },
            { { Platform::MacX64, "flycast" }, "flycast-macOS-2.7.zip" },
            { { Platform::WindowsX64, "flycast" }, "flycast-win64-2.7.zip" },
        };
        for (Platform platform : allPlatforms) {
            OnPlatform scope(platform);
            for (const auto& [id, release] : releases) {
                const Emulator& entry = entryOf(platform, id);
                nlohmann::json json = { { "tag_name", release.first }, { "assets", nlohmann::json::array() } };
                for (const auto& name : release.second) {
                    json["assets"].push_back({ { "name", name }, { "browser_download_url", githubUrl(entry, release.first, name) },
                                               { "size", 100 }, { "digest", "sha256:" + std::string(64, 'b') } });
                }
                std::vector<Package> packages;
                std::vector<std::string> warnings;
                check(parseEmulatorReleases(nlohmann::json::array({ json }).dump(), entry, packages, warnings, error), "parses");
                auto expected = picked.find({ platform, id });
                const std::string what = platformName(platform) + " " + id;
                if (expected != picked.end()) {
                    check(packages.size() == 1 && packages[0].files[0].url == githubUrl(entry, release.first, expected->second), what + " picks " + expected->second);
                    if (!packages.empty()) check(checkPackage(packages[0]).empty(), what + " passes the safety rules: " + checkPackage(packages[0]));
                }
                else if (!entry.available()) {
                    check(packages.empty(), what + ": nothing without a build");
                    Package package = packageFor(id, "1", githubUrl(entry, release.first, release.second[0]));
                    check(contains(checkPackage(package), "no " + platformName(platform) + " build"), what + ": refused: " + checkPackage(package));
                }
            }
            // RetroArch from libretro's stable builds for this platform
            const Emulator& retroArch = entryOf(platform, "retroarch");
            std::vector<Package> packages;
            std::vector<std::string> warnings;
            parseEmulatorReleases(R"([{"tag_name":"v1.22.2"}])", retroArch, packages, warnings, error);
            if (retroArch.available()) {
                std::string url = retroArch.assetRule;
                url.replace(url.find("<version>"), 9, "1.22.2");
                check(packages.size() == 1 && packages[0].files[0].url == url && packages[0].noChecksum, platformName(platform) + ": RetroArch " + url);
                if (!packages.empty()) check(checkPackage(packages[0]).empty(), platformName(platform) + ": RetroArch passes: " + checkPackage(packages[0]));
            }
            else {
                check(packages.empty(), platformName(platform) + ": no RetroArch");
            }
        }
    });

    run("rolling releases: DuckStation's Linux version is its publication date", [&] {
        const Emulator& entry = entryOf(Platform::LinuxX64, "duckstation");
        OnPlatform scope(Platform::LinuxX64);
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        check(parseEmulatorReleases(rollingRelease("2026-11-27T08:30:00Z", "DuckStation-x64.AppImage", "x"), entry, packages, warnings, error)
              && packages.size() == 1, "the rolling release is offered");
        if (packages.size() == 1) {
            checkEqual(packages[0].version, "2026.11.27", "YYYY.MM.DD");
            checkEqual(packages[0].files[0].url, githubUrl(entry, "latest", "DuckStation-x64.AppImage"), "its asset");
            check(checkPackage(packages[0]).empty(), "passes the safety rules");
        }
        check(compareVersions("2026.11.28", "2026.11.27") > 0 && compareVersions("2026.12.01", "2026.11.30") > 0, "later builds compare newer");
        for (const std::string& published : { std::string("2026-11-27"), std::string("yesterday"), std::string("") }) {
            packages.clear();
            check(parseEmulatorReleases(rollingRelease(published, "DuckStation-x64.AppImage", "x"), entry, packages, warnings, error) && packages.empty(),
                  "no usable date, no version: " + published);
        }
        packages.clear();
        auto tagged = nlohmann::json::parse(rollingRelease("2026-11-27T08:30:00Z", "DuckStation-x64.AppImage", "x"));
        tagged["tag_name"] = "v0.1-12083";
        check(parseEmulatorReleases(tagged.dump(), entry, packages, warnings, error) && packages.empty(), "only the rolling tag on Linux");
        auto pre = nlohmann::json::parse(rollingRelease("2026-11-27T08:30:00Z", "DuckStation-x64.AppImage", "x"));
        pre["prerelease"] = true;
        check(parseEmulatorReleases(pre.dump(), entry, packages, warnings, error) && packages.empty(), "never a pre-release");
        packages.clear();
        check(parseEmulatorReleases(rollingRelease("2026-11-27T08:30:00Z", "DuckStation-arm64.AppImage", "x"), entry, packages, warnings, error) && packages.empty(),
              "another architecture's AppImage isn't this one's");
        auto noDigest = nlohmann::json::parse(rollingRelease("2026-11-27T08:30:00Z", "DuckStation-x64.AppImage", "x"));
        noDigest["assets"][0]["digest"] = nullptr;
        warnings.clear();
        check(parseEmulatorReleases(noDigest.dump(), entry, packages, warnings, error) && packages.empty() && !warnings.empty(), "GitHub's SHA-256 still required");
    });

    // --- libcurl's Http, the parts without a network ----------------------------

    run("curl Http: status lines, headers, Range, validators and redirects", [&] {
        using namespace HttpText;
        check(statusOf("HTTP/1.1 206 Partial Content") == 206 && statusOf("HTTP/2 200") == 200 && statusOf("HTTP/1.0 304") == 304, "status lines");
        check(statusOf("Content-Length: 5") == 0 && statusOf("HTTP/1.1 2x0 OK") == 0 && statusOf("") == 0 && statusOf("HTTP/1.1 2000") == 0, "not status lines");
        std::string name, value;
        check(header("ETag: \"abc\"", name, value) && name == "etag" && value == "\"abc\"", "a header, lower-case name");
        check(header("X-RateLimit-Reset:   1700000000  ", name, value) && name == "x-ratelimit-reset" && value == "1700000000", "blanks trimmed");
        check(header("Location:", name, value) && name == "location" && value.empty(), "an empty value");
        check(!header("HTTP/1.1 200 OK", name, value) || name != "http/1.1 200 ok", "the status line isn't a header");
        check(!header("", name, value) && !header("No colon here", name, value) && !header("Bad Name: x", name, value)
              && !header(": x", name, value) && !header(std::string("Name: caf\xc3\xa9"), name, value), "only printable ASCII headers");
        check(resumeHeaders(0, "\"v\"").empty(), "nothing to continue from the start");
        check(resumeHeaders(1024, "") == std::vector<std::string>{ "Range: bytes=1024-" }, "Range alone without a validator");
        check(resumeHeaders(1024, "\"v1\"") == std::vector<std::string>({ "Range: bytes=1024-", "If-Range: \"v1\"" }), "If-Range: only the same file");
        check(resumeHeaders(5, "bad\r\nX: y") == std::vector<std::string>{ "Range: bytes=5-" }, "a validator can't carry a header");
        checkEqual(validator({ { "etag", "\"strong\"" }, { "last-modified", "Wed, 28 Oct 2026 12:00:00 GMT" } }), "\"strong\"", "a strong ETag");
        checkEqual(validator({ { "etag", "W/\"weak\"" }, { "last-modified", "Wed, 28 Oct 2026 12:00:00 GMT" } }), "Wed, 28 Oct 2026 12:00:00 GMT", "a weak ETag: Last-Modified");
        checkEqual(validator({}), "", "neither");
        check(redirectAllowed("https://github.com/a/b/releases/download/v1/x.zip", "https://objects.githubusercontent.com/x?sig=1"), "https to https");
        check(redirectAllowed("https://buildbot.libretro.com/stable/1/x.7z", "/mirror/x.7z") && redirectAllowed("https://a.example/x", "//b.example/x")
              && redirectAllowed("https://a.example/x/y", "z"), "relative to https stays https");
        check(!redirectAllowed("https://github.com/x", "http://github.com/x") && !redirectAllowed("https://github.com/x", "HTTP://evil.example/")
              && !redirectAllowed("https://github.com/x", "ftp://evil.example/x") && !redirectAllowed("https://github.com/x", "file:///etc/passwd")
              && !redirectAllowed("https://github.com/x", "javascript:alert(1)"), "never off https");
        check(!redirectAllowed("http://a.example/x", "/y") && redirectAllowed("http://a.example/x", "https://a.example/y"), "from http only to https");
        check(!redirectAllowed("https://a.example/x", "") && !redirectAllowed("https://a.example/x", "https://a.example/\r\nX: y"), "nothing unprintable");
        check(answer(100, 206) == Answer::Resume && answer(100, 416) == Answer::Complete && answer(100, 200) == Answer::Restart
              && answer(0, 200) == Answer::Restart && answer(0, 206) == Answer::Fail && answer(0, 416) == Answer::Fail
              && answer(100, 404) == Answer::Fail, "answers read as WinHTTP's are");
        check(retryable(503) && retryable(429) && retryable(408) && !retryable(404) && !retryable(403), "which failures may clear up");
#ifndef _WIN32
        check(createHttp() != nullptr, "libcurl's Http is made on Linux and macOS");
#endif
    });

    // --- AppImages ---------------------------------------------------------------

    run("AppImage: installed as the program, mode 0755, only if it's ELF", [&] {
        OnPlatform scope(Platform::LinuxX64);
        auto root = makeRoot("appimage-install");
        auto downloadsFolder = Service::downloadsFolder(root);
        Installer installer(downloadsFolder, root + "/emulators");
        const Emulator& entry = *findEmulator("emulator-duckstation");
        Package package = packageFor("duckstation", "2026.11.27", githubUrl(entry, "latest", "DuckStation-x64.AppImage"));
        const std::string download = root + "/server/DuckStation-x64.AppImage";
        writeFile(download, "PK\003\004 a zip, not a program");
        check(!installer.install(package, { download }, error) && contains(error, "isn't an AppImage"), "not ELF: refused: " + error);
        check(!fs::exists(root + "/emulators/DuckStation"), "nothing installed");
        writeFile(download, elf("one"));
        check(installer.install(package, { download }, error), "installed: " + error);
        const std::string program = root + "/emulators/DuckStation/DuckStation.AppImage";
        checkEqual(readFile(program), elf("one"), "the AppImage itself, under the program's name");
        check(fs::exists(root + "/emulators/DuckStation/portable.txt"), "its portable file");
        Manifest manifest;
        check(readManifest(installer.manifestPath(package.id), manifest, error), "recorded");
        auto recorded = std::find_if(manifest.files.begin(), manifest.files.end(), [](const InstalledFile& file) { return file.path == "DuckStation/DuckStation.AppImage"; });
        check(recorded != manifest.files.end() && recorded->sha256 == sha256Of(elf("one")), "the program in the manifest");
#ifndef _WIN32
        checkEqual(std::to_string(modeOf(program)), std::to_string(0755), "mode 0755");
        check(recorded != manifest.files.end() && recorded->mode == 0755, "its mode recorded");
#endif
        // An update replaces it, a removal takes it away
        writeFile(download, elf("two"));
        package.version = "2026.11.28";
        check(installer.install(package, { download }, error), "updated: " + error);
        checkEqual(readFile(program), elf("two"), "the new build");
#ifndef _WIN32
        checkEqual(std::to_string(modeOf(program)), std::to_string(0755), "still 0755");
#endif
        Installer::Removal removal;
        check(installer.remove(package.id, removal, error) && !fs::exists(program) && !fs::exists(root + "/emulators/DuckStation"), "removed");
        // Its launcher, in launchers.linux with %RETROFE_PATH%/ paths
        check(installer.install(package, { download }, error), "installed again");
        check(readManifest(installer.manifestPath(package.id), manifest, error), "manifest");
        checkEqual(createEmulatorLauncher(root, downloadsFolder, manifest), ", with the launcher DuckStation.", "the launcher is made");
        std::string launcher = readFile(root + "/launchers.linux/DuckStation.conf");
        check(contains(launcher, "%RETROFE_PATH%/emulators/DuckStation/DuckStation.AppImage") && contains(launcher, R"(-batch -fullscreen -- "%ITEM_FILEPATH%")"),
              "launchers.linux/DuckStation.conf: " + launcher);
        checkEqual(manifest.launcherPath, "launchers.linux/DuckStation.conf", "recorded");
        check(contains(removeEmulatorLauncher(root, manifest), "Removed launcher") && !fs::exists(root + "/launchers.linux/DuckStation.conf"), "and removed");
        check(!fs::exists(root + "/launchers.windows"), "nothing for Windows");
        check(isElfFile(download) && !isElfFile(root + "/nothing") && !isDiskImage(download), "the form checks");
    });

    run("service: a Linux install end to end, from the rolling release", [&] {
        OnPlatform scope(Platform::LinuxX64);
        auto root = makeRoot("linux-service");
        auto http = std::make_unique<ServingHttp>();
        ServingHttp* fake = http.get();
        const std::string content = elf("duck");
        for (const Emulator& entry : emulators()) fake->documents[releasesUrl(entry.repo, entry.latestOnly)] = "[]";
        fake->documents[releasesUrl("stenzek/duckstation", true)] = rollingRelease("2026-11-27T08:30:00Z", "DuckStation-x64.AppImage", content);
        fake->files[githubUrl(entryOf(Platform::LinuxX64, "duckstation"), "latest", "DuckStation-x64.AppImage")] = content;
        Service service(root, std::move(http));
        service.now = [] { return static_cast<int64_t>(1000); };
        service.freeSpace = [](const std::string&, uint64_t& available) { available = 1ull << 40; return true; };
        check(service.supported(), "supported on Linux with an Http");
        auto releases = service.check();
        check(releases.size() == 6, "a row per entry");
        if (releases.size() == 6) {
            check(!releases[1].available && releases[1].error == "No Linux x86_64 build", "MAME: no build, nothing asked: " + releases[1].error);
            check(releases[2].version == "2026.11.27" && releases[2].available, "DuckStation's rolling build");
        }
        check(fake->requested.size() == 5 && std::count(fake->requested.begin(), fake->requested.end(), releasesUrl("stenzek/duckstation", true)) == 1,
              "five asked, DuckStation at /releases/latest");
        check(fs::exists(root + "/manage/downloads/cache/duckstation-linux-x86_64/releases.json") && !fs::exists(root + "/manage/downloads/cache/duckstation"),
              "its cache kept apart from Windows'");
        auto statuses = service.list();
        check(statuses.size() == 6 && !statuses[1].available && statuses[1].unavailable == "No Linux x86_64 build" && statuses[2].available, "list says which have builds");
        checkEqual(service.install("emulator-mame").message, "No Linux x86_64 build of MAME.", "MAME can't be installed");
        Outcome installed = service.install("emulator-duckstation");
        check(installed.ok, "installed: " + installed.message);
        checkEqual(installed.message, "DuckStation is installed in emulators/DuckStation, with the launcher DuckStation.", "names the folder and launcher");
        check(installed.libraryChanged && fs::exists(root + "/launchers.linux/DuckStation.conf"), "launchers.linux");
        checkEqual(readFile(root + "/emulators/DuckStation/DuckStation.AppImage"), content, "the AppImage");
        Outcome removed = service.remove("emulator-duckstation");
        check(removed.ok && !fs::exists(root + "/emulators/DuckStation/DuckStation.AppImage") && !fs::exists(root + "/launchers.linux/DuckStation.conf"),
              "removed with its launcher: " + removed.message);

        OnPlatform arm(Platform::LinuxArm64);
        auto armRoot = makeRoot("linux-arm-service");
        auto armHttp = std::make_unique<FakeHttp>();
        FakeHttp* armFake = armHttp.get();
        Service armService(armRoot, std::move(armHttp));
        armService.now = [] { return static_cast<int64_t>(1000); };
        auto armReleases = armService.check();
        check(armReleases.size() == 6 && armFake->requested.size() == 2, "Linux arm64 asks only for DuckStation and PPSSPP");
        CoreList cores = armService.cores();
        check(cores.cores.empty() && contains(cores.error, "no Linux arm64 build"), "no cores: " + cores.error);
        check(contains(armService.installCore("snes9x").message, "no Linux arm64 build"), "no core install");
        OnPlatform other(Platform::Other);
        check(!Service(makeRoot("other-service"), std::make_unique<FakeHttp>()).supported(), "not supported elsewhere");
    });

    // --- Disk images (macOS), through a fake opener -------------------------------

    run("DMG: the .app is copied out of the image, which is always detached", [&] {
        OnPlatform scope(Platform::MacArm64);
        auto root = makeRoot("dmg-install");
        auto downloadsFolder = Service::downloadsFolder(root);
        FakeImages images;
        images.contents = root + "/image";
        writeFile(images.contents + "/RetroArch.app/Contents/MacOS/RetroArch", "program");
        writeFile(images.contents + "/RetroArch.app/Contents/Info.plist", "plist");
        writeFile(images.contents + "/RetroArch.app/Contents/Resources/info/snes9x_libretro.info", "corename = \"Snes9x\"\nsystemname = \"SNES\"\n");
        writeFile(images.contents + "/ReadMe.txt", "not part of the app");
#ifndef _WIN32
        fs::permissions(images.contents + "/RetroArch.app/Contents/MacOS/RetroArch", fs::perms(0755), fs::perm_options::replace);
        fs::create_directories(images.contents + "/RetroArch.app/Contents/Frameworks/Lib.framework/Versions/A");
        writeFile(images.contents + "/RetroArch.app/Contents/Frameworks/Lib.framework/Versions/A/Lib", "library");
        fs::create_directory_symlink("A", images.contents + "/RetroArch.app/Contents/Frameworks/Lib.framework/Versions/Current");
        fs::create_symlink("Versions/Current/Lib", images.contents + "/RetroArch.app/Contents/Frameworks/Lib.framework/Lib");
        fs::create_directory_symlink("/Applications", images.contents + "/Applications");   // as DMGs have, outside the app
#endif
        const std::string image = root + "/server/RetroArch_Metal.dmg";
        writeFile(image, diskImage());
        Package package = packageFor("retroarch", "1.22.2", "https://buildbot.libretro.com/stable/1.22.2/apple/osx/universal/RetroArch_Metal.dmg");
        check(checkPackage(package).empty(), "libretro's DMG passes the safety rules: " + checkPackage(package));
        {
            Installer installer(downloadsFolder, root + "/emulators");
            installer.setImageOpener(ImageOpener());
            check(!installer.install(package, { image }, error) && error == "A disk image can only be installed on macOS", "no opener, no install: " + error);
        }
        Installer installer(downloadsFolder, root + "/emulators");
        installer.setImageOpener(images.opener());
        writeFile(root + "/server/not.dmg", "plain");
        check(!installer.install(package, { root + "/server/not.dmg" }, error) && contains(error, "isn't a disk image") && images.attached == 0, "not a disk image: " + error);
        images.attachFails = true;
        check(!installer.install(package, { image }, error) && contains(error, "Could not open") && images.detached == 0, "attaching fails: " + error);
        images.attachFails = false;
        check(installer.install(package, { image }, error), "installed: " + error);
        check(images.attached == 2 && images.detached == 1, "attached and detached once");
        check(fs::path(images.lastMount).parent_path() == fs::path(downloadsFolder) / "staging" / "emulator-retroarch", "mounted in staging");
        checkEqual(readFile(root + "/emulators/RetroArch/RetroArch.app/Contents/MacOS/RetroArch"), "program", "the program");
        check(fs::exists(root + "/emulators/RetroArch/RetroArch.app/Contents/Resources/info/snes9x_libretro.info"), "its info files");
        check(!fs::exists(root + "/emulators/RetroArch/ReadMe.txt") && !fs::exists(root + "/emulators/RetroArch/Applications"), "only the .app");
        check(!fs::exists(downloadsFolder + "/staging/emulator-retroarch"), "staging, mount point and all, cleaned up");
#ifndef _WIN32
        checkEqual(std::to_string(modeOf(root + "/emulators/RetroArch/RetroArch.app/Contents/MacOS/RetroArch")), std::to_string(0755), "the program's mode");
        check(isLinkTo(root + "/emulators/RetroArch/RetroArch.app/Contents/Frameworks/Lib.framework/Versions/Current", "A")
              && isLinkTo(root + "/emulators/RetroArch/RetroArch.app/Contents/Frameworks/Lib.framework/Lib", "Versions/Current/Lib"), "a framework's links");
        checkEqual(readFile(root + "/emulators/RetroArch/RetroArch.app/Contents/Frameworks/Lib.framework/Lib"), "library", "which lead inside it");
#endif
        // Cores on macOS: a launcher in launchers.apple for RetroArch.app's program
        Manifest core;
        core.id = "core-snes9x";
        core.kind = "core";
        core.name = "Snes9x";
        core.version = "2026-10-28";
        checkEqual(createEmulatorLauncher(root, downloadsFolder, core), ", with the launcher RetroArch Snes9x.", "a core launcher");
        fs::remove(installer.manifestPath(core.id));   // it recorded the launcher; RetroArch's removal would wait for the core
        std::string launcher = readFile(root + "/launchers.apple/RetroArch Snes9x.conf");
        check(contains(launcher, "%RETROFE_PATH%/emulators/RetroArch/RetroArch.app/Contents/MacOS/RetroArch")
              && contains(launcher, R"(-L "%RETROFE_PATH%/emulators/RetroArch/cores/snes9x_libretro.dylib" "%ITEM_FILEPATH%")"), "launchers.apple: " + launcher);
        Installer::Removal removal;
        check(installer.remove(package.id, removal, error) && !fs::exists(root + "/emulators/RetroArch/RetroArch.app"), "removed: " + error);

        // An image without the app, or with a link leading out of it: refused, and still detached
        auto bare = makeRoot("dmg-refused");
        Installer refusing(Service::downloadsFolder(bare), bare + "/emulators");
        FakeImages empty;
        empty.contents = bare + "/image";
        writeFile(empty.contents + "/Other.app/Contents/MacOS/Other", "x");
        refusing.setImageOpener(empty.opener());
        check(!refusing.install(package, { image }, error) && contains(error, "has no RetroArch.app") && empty.detached == 1, "no app: " + error);
#ifndef _WIN32
        writeFile(empty.contents + "/RetroArch.app/Contents/MacOS/RetroArch", "program");
        fs::create_symlink("../../../../outside", empty.contents + "/RetroArch.app/Contents/escape");
        check(!refusing.install(package, { image }, error) && contains(error, "leads outside") && empty.detached == 2, "a link leading out: " + error);
        check(!fs::exists(bare + "/emulators/RetroArch"), "nothing installed");
#endif
    });

    // --- Permissions and links in archives ----------------------------------------

    run("zip and 7z: executable bits kept, never setuid", [&] {
        auto root = makeRoot("archive-modes");
        Installer installer(Service::downloadsFolder(root), root + "/emulators");
        {
            OnPlatform scope(Platform::MacArm64);
            const Emulator& entry = *findEmulator("emulator-ppsspp");
            const std::string zip = root + "/server/ppsspp.zip";
            check(makeUnixZip(zip, { { "PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL", "program", 0100755 },
                                     { "PPSSPPSDL.app/Contents/Info.plist", "plist", 0100644 },
                                     { "PPSSPPSDL.app/Contents/Resources/tool", "tool", 0106777 } }), "zip");
            Package package = packageFor("ppsspp", "1.20.4", githubUrl(entry, "v1.20.4", "PPSSPPSDL-macOS-v1.20.4.zip"));
            check(installer.install(package, { zip }, error), "installed: " + error);
            checkEqual(readFile(root + "/emulators/PPSSPP/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL"), "program", "unpacked");
#ifndef _WIN32
            checkEqual(std::to_string(modeOf(root + "/emulators/PPSSPP/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL")), std::to_string(0755), "0755 kept");
            checkEqual(std::to_string(modeOf(root + "/emulators/PPSSPP/PPSSPPSDL.app/Contents/Info.plist")), std::to_string(0644), "0644 kept");
            checkEqual(std::to_string(modeOf(root + "/emulators/PPSSPP/PPSSPPSDL.app/Contents/Resources/tool")), std::to_string(0755),
                       "setuid, setgid and others' write dropped");
            Manifest manifest;
            check(readManifest(installer.manifestPath(package.id), manifest, error)
                  && std::all_of(manifest.files.begin(), manifest.files.end(), [](const InstalledFile& file) { return file.mode <= 0777; }), "modes recorded, none special");
#endif
        }
        {
            OnPlatform scope(Platform::LinuxX64);
            using SevenZipWriter::unixAttributes;
            const std::string archive = root + "/server/RetroArch.7z";
            check(SevenZipWriter::write(archive, {
                { "RetroArch-Linux-x86_64", "", unixAttributes(040755), true },
                { "RetroArch-Linux-x86_64/RetroArch-Linux-x86_64.AppImage", elf("retroarch"), unixAttributes(0100755) },
                { "RetroArch-Linux-x86_64/RetroArch-Linux-x86_64.AppImage.home/.config/retroarch/retroarch.cfg", "cfg", unixAttributes(0100644) },
                { "RetroArch-Linux-x86_64/RetroArch-Linux-x86_64.AppImage.home/.config/retroarch/tool", "tool", unixAttributes(0104755) },
                { "RetroArch-Linux-x86_64/readme.txt", "no Unix mode", 0x20 } }), "7z");
            Package package = packageFor("retroarch", "1.22.2", "https://buildbot.libretro.com/stable/1.22.2/linux/x86_64/RetroArch.7z");
            check(installer.install(package, { archive }, error), "installed: " + error);
            const std::string folder = root + "/emulators/RetroArch/";
            checkEqual(readFile(folder + "RetroArch-Linux-x86_64.AppImage"), elf("retroarch"), "top folder removed");
            check(fs::exists(folder + "RetroArch-Linux-x86_64.AppImage.home/.config/retroarch/retroarch.cfg"), "its portable home kept");
#ifndef _WIN32
            checkEqual(std::to_string(modeOf(folder + "RetroArch-Linux-x86_64.AppImage")), std::to_string(0755), "7z: 0755 kept");
            checkEqual(std::to_string(modeOf(folder + "RetroArch-Linux-x86_64.AppImage.home/.config/retroarch/retroarch.cfg")), std::to_string(0644), "7z: 0644 kept");
            check((modeOf(folder + "RetroArch-Linux-x86_64.AppImage.home/.config/retroarch/tool") & 07000) == 0, "7z: never setuid");
#endif
            // The program runs even when the archive stored no mode for it
            check(SevenZipWriter::write(archive, {
                { "RetroArch-Linux-x86_64/RetroArch-Linux-x86_64.AppImage", elf("v2"), 0x20 } }), "7z without modes");
            package.version = "1.22.3";
            check(installer.install(package, { archive }, error), "updated: " + error);
#ifndef _WIN32
            check((modeOf(folder + "RetroArch-Linux-x86_64.AppImage") & 0111) == 0111, "the program is made executable");
#endif
        }
    });

    run("links: only relative ones that stay inside the emulator's folder", [&] {
        check(isSafeLink("a/b/link", "c") && isSafeLink("a/b/link", "../c") && isSafeLink("a/b/link", "../../c") && isSafeLink("link", "./a/../b"),
              "relative links inside");
        check(!isSafeLink("a/link", "../../x") && !isSafeLink("link", "..") && !isSafeLink("link", ".") && !isSafeLink("a/link", "..")
              && !isSafeLink("link", "/etc/passwd") && !isSafeLink("link", "C:\\x") && !isSafeLink("link", "a\\..\\..\\x")
              && !isSafeLink("link", "") && !isSafeLink("link", "a\nb") && !isSafeLink("../link", "a"), "escaping, absolute and odd links refused");
        OnPlatform scope(Platform::MacArm64);
        const Emulator& entry = *findEmulator("emulator-ppsspp");
        const std::string url = githubUrl(entry, "v1.20.4", "PPSSPPSDL-macOS-v1.20.4.zip");
        const std::string program = "PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL";
        const std::string framework = "PPSSPPSDL.app/Contents/Frameworks/SDL2.framework/";
        auto root = makeRoot("archive-links");
        auto downloadsFolder = Service::downloadsFolder(root);
        Installer installer(downloadsFolder, root + "/emulators");
        const std::string zip = root + "/server/ppsspp.zip";
        std::vector<UnixEntry> bundle = {
            { program, "program", 0100755 },
            { framework + "Versions/A/SDL2", "sdl", 0100755 },
            { framework + "Versions/Current", "A", 0120777 },
            { framework + "SDL2", "Versions/Current/SDL2", 0120777 },
            { "PPSSPPSDL.app/Contents/settings", "../../settings.ini", 0120777 } };
        check(makeUnixZip(zip, bundle), "zip with links");
        Package package = packageFor("ppsspp", "1.20.4", url);
#ifdef _WIN32
        check(!installer.install(package, { zip }, error) && contains(error, "doesn't install on Windows"), "Windows refuses links: " + error);
        check(!fs::exists(root + "/emulators/PPSSPP"), "nothing installed");
#else
        check(installer.install(package, { zip }, error), "installed: " + error);
        const std::string folder = root + "/emulators/PPSSPP/";
        check(isLinkTo(folder + framework + "Versions/Current", "A") && isLinkTo(folder + framework + "SDL2", "Versions/Current/SDL2"), "relative links recreated");
        checkEqual(readFile(folder + framework + "SDL2"), "sdl", "and lead where they did");
        Manifest manifest;
        check(readManifest(installer.manifestPath(package.id), manifest, error), "manifest");
        auto link = std::find_if(manifest.files.begin(), manifest.files.end(), [&](const InstalledFile& file) { return file.path == "PPSSPP/" + framework + "Versions/Current"; });
        check(link != manifest.files.end() && link->link == "A", "a link is recorded as a link");

        // Removal deletes a link, never what it leads to
        writeFile(folder + "settings.ini", "the user's");
        checkEqual(readFile(folder + "PPSSPPSDL.app/Contents/settings"), "the user's", "the link leads to the user's file");
        Installer::Removal removal;
        check(installer.remove(package.id, removal, error), "removed: " + error);
        check(!fs::exists(fs::symlink_status(folder + "PPSSPPSDL.app/Contents/settings")) && !fs::exists(fs::symlink_status(folder + framework + "SDL2")), "the links went");
        checkEqual(readFile(folder + "settings.ini"), "the user's", "what one led to stayed");
        check(removal.left == std::vector<std::string>{ "PPSSPP/settings.ini" }, "and is what's left");
        fs::remove(folder + "settings.ini");

        // A link changed since it was installed stays, as a changed file does
        check(installer.install(package, { zip }, error), "installed again: " + error);
        fs::remove(folder + framework + "SDL2");
        fs::create_symlink("Versions/A/SDL2", folder + framework + "SDL2");
        check(installer.remove(package.id, removal, error) && removal.kept == 1 && isLinkTo(folder + framework + "SDL2", "Versions/A/SDL2"), "a changed link kept");
        fs::remove_all(root + "/emulators/PPSSPP");
        fs::remove(installer.manifestPath(package.id));

        // An update that points a link elsewhere replaces the link
        check(installer.install(package, { zip }, error), "v1: " + error);
        bundle[2].content = "B";
        bundle.push_back({ framework + "Versions/B/SDL2", "sdl 2", 0100755 });
        check(makeUnixZip(zip, bundle), "v2 zip");
        package.version = "1.20.5";
        check(installer.install(package, { zip }, error), "updated: " + error);
        check(isLinkTo(folder + framework + "Versions/Current", "B"), "the link now leads to B");
        checkEqual(readFile(folder + framework + "SDL2"), "sdl 2", "through it, the new library");
#endif

        // Links that lead out, or that something goes through, are refused (everywhere)
        auto refusedRoot = makeRoot("archive-links-refused");
        Installer refusing(Service::downloadsFolder(refusedRoot), refusedRoot + "/emulators");
        const std::vector<std::pair<std::string, std::vector<UnixEntry>>> unsafe = {
            { "../../x", { { program, "p", 0100755 }, { "PPSSPPSDL.app/escape", "../../x", 0120777 } } },
            { "absolute", { { program, "p", 0100755 }, { "PPSSPPSDL.app/escape", "/etc/passwd", 0120777 } } },
            { "the folder itself", { { program, "p", 0100755 }, { "PPSSPPSDL.app/up", "..", 0120777 } } },
            { "through another link", { { program, "p", 0100755 }, { "PPSSPPSDL.app/a/b/L", "../../c", 0120777 },
                                        { "PPSSPPSDL.app/M", "a/b/L/../../../x", 0120777 } } },
            { "a file inside a link", { { program, "p", 0100755 }, { "PPSSPPSDL.app/lib", "Contents", 0120777 },
                                        { "PPSSPPSDL.app/lib/x", "x", 0100644 } } },
            { "a link over a folder", { { program, "p", 0100755 }, { "PPSSPPSDL.app/Contents/MacOS", "../x", 0120777 } } },
        };
        for (const auto& [what, entries] : unsafe) {
            check(makeUnixZip(zip, entries), "zip: " + what);
            check(!refusing.install(package, { zip }, error), "refused, " + what + ": " + error);
#ifndef _WIN32
            check(contains(error, "safely"), what + " says why: " + error);
#endif
        }
        check(!fs::exists(refusedRoot + "/emulators/PPSSPP"), "nothing installed");

        // The checks hold for what a manifest or plan says, too: a link leading out is never removed through
        InstalledFile hostile;
        hostile.path = "PPSSPP/escape";
        hostile.link = "../../x";
        Manifest record;
        record.id = "emulator-ppsspp";
        record.kind = "emulator";
        record.name = "PPSSPP";
        record.files = { hostile };
        check(writeManifest(refusing.manifestPath("emulator-ppsspp"), record, error), "a hostile manifest");
        writeFile(refusedRoot + "/x", "outside");
        Installer::Removal removed;
        check(refusing.remove("emulator-ppsspp", removed, error), "removal runs");
        checkEqual(readFile(refusedRoot + "/x"), "outside", "and leaves what's outside alone");
    });

    run("links: a recovered install still checks them", [&] {
#ifndef _WIN32
        OnPlatform scope(Platform::MacArm64);
        auto root = makeRoot("links-recover");
        auto downloadsFolder = Service::downloadsFolder(root);
        // A staged install, ready, whose plan was changed to lead a link out
        const std::string staging = downloadsFolder + "/staging/emulator-ppsspp";
        writeFile(staging + "/files/0/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL", "p");
        fs::create_symlink("../../../../../x", staging + "/files/0/PPSSPPSDL.app/escape");
        nlohmann::json plan = { { "id", "emulator-ppsspp" }, { "name", "PPSSPP" }, { "kind", "emulator" }, { "version", "1" },
            { "files", nlohmann::json::array({
                { { "path", "PPSSPP/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL" }, { "size", 1 }, { "sha256", sha256Of("p") }, { "staged", "files/0/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL" } },
                { { "path", "PPSSPP/PPSSPPSDL.app/escape" }, { "size", 1 }, { "sha256", "" }, { "link", "../../../../../x" }, { "staged", "files/0/PPSSPPSDL.app/escape" } } }) } };
        writeFile(staging + "/plan.json", plan.dump());
        writeFile(staging + "/.ready", "");
        Installer installer(downloadsFolder, root + "/emulators");
        installer.recover();
        check(!fs::exists(fs::symlink_status(root + "/emulators/PPSSPP/PPSSPPSDL.app/escape")) && !fs::exists(root + "/emulators/PPSSPP/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL"),
              "nothing of it installed");
        check(!fs::exists(staging) && !fs::exists(installer.manifestPath("emulator-ppsspp")), "and it's gone");
        // A mode above 0777 in a plan is refused too
        writeFile(staging + "/files/0/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL", "p");
        plan["files"] = nlohmann::json::array({ { { "path", "PPSSPP/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL" }, { "size", 1 }, { "sha256", sha256Of("p") },
                                                  { "mode", 04755 }, { "staged", "files/0/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL" } } });
        writeFile(staging + "/plan.json", plan.dump());
        writeFile(staging + "/.ready", "");
        installer.recover();
        check(!fs::exists(root + "/emulators/PPSSPP/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL"), "no setuid from a plan");
#else
        check(true, "links are refused on Windows");
#endif
    });

    // --- The screens ---------------------------------------------------------------

    run("screens: an emulator with no build says so, and has no Install", [&] {
        OnPlatform scope(Platform::LinuxArm64);
        auto root = makeRoot("screens-arm64");
        auto http = std::make_unique<ServingHttp>();
        ServingHttp* fake = http.get();
        for (const Emulator& entry : emulators()) fake->documents[releasesUrl(entry.repo, entry.latestOnly)] = "[]";
        fake->documents[releasesUrl("stenzek/duckstation", true)] = rollingRelease("2026-11-27T08:30:00Z", "DuckStation-arm64.AppImage", elf("x"));
        Screens s(root, std::move(http));
        s.model->setScreen("download-emulators", {});
        check(s.waitForJob(), "checked");
        checkEqual(s.model->get("download:unsupported"), "", "no note: downloads work here");
        for (const std::string id : { "emulator-retroarch", "emulator-mame", "emulator-pcsx2", "emulator-flycast" }) {
            checkEqual(s.detail(id), "No Linux arm64 build", id + " in the list");
        }
        checkEqual(s.detail("emulator-duckstation"), "Not installed, 2026.11.27 available", "DuckStation's build");
        checkEqual(s.detail("emulator-ppsspp"), "Not installed, no Linux release", "PPSSPP checked");
        check(fake->requested.size() == 2, "only the two with builds were asked for");
        checkEqual(s.model->get("download:retroArchInstalled"), "no", "no cores link");

        s.model->setScreen("download-emulator", { { "id", "emulator-mame" } });
        checkEqual(s.model->get("download:available"), "No Linux arm64 build", "on its screen");
        check(s.model->get("download:canInstall") == "no" && s.model->get("download:canUpdate") == "no", "no Install or Update");
        checkEqual(s.model->actionProblem("download.install"), "No Linux arm64 build of MAME.", "and why not");
        check(s.model->severity("download:available") == Forms::Severity::None, "not a warning: it's how it is");
        s.model->leaving(s.model->screen());

        s.model->setScreen("download-emulator", { { "id", "emulator-duckstation" } });
        checkEqual(s.model->get("download:canInstall"), "yes", "DuckStation can be installed");
        checkEqual(s.confirmation("download-emulator", "download.install"),
                   "Download DuckStation from github.com/stenzek/duckstation? RetroFE checks the published checksum, but not what it contains.",
                   "the confirmation names the source");
        check(contains(s.confirmation("download-emulator", "download.remove"), "stay in emulators/DuckStation"), "folders as Linux writes them");
        s.model->leaving(s.model->screen());

        for (const std::string& form : { std::string("download-cores"), std::string("download-core") }) {
            s.model->setScreen(form, form == "download-core" ? Forms::Params{ { "core", "snes9x" } } : Forms::Params{});
            checkEqual(s.model->get("download:unsupported"), "RetroArch has no Linux arm64 build, so its cores aren't offered here.", form + ": says why");
            check(s.model->get("download:supported") == "no" && s.model->get("download:needsRetroArch") == "no"
                  && s.model->get("download:hasRetroArch") == "no", form + ": nothing else");
            check(s.model->rows("download:retroArchLink", 10).items.empty() && s.model->rows("download:cores", 10).items.empty(), form + ": no rows");
            check(!s.model->actionProblem("download.install").empty(), form + ": no install");
            s.model->update(0.01f);
            check(s.model->get("download:running") == "no", form + ": nothing read");
            s.model->leaving(s.model->screen());
        }
    });

    run("screens: Linux x86_64 offers cores where RetroArch is", [&] {
        OnPlatform scope(Platform::LinuxX64);
        auto root = makeRoot("screens-linux");
        Manifest retroArch = retroArchManifest();
        check(writeManifest(Service::downloadsFolder(root) + "/installed/emulator-retroarch.json", retroArch, error), "RetroArch installed");
        auto http = std::make_unique<ServingHttp>();
        ServingHttp* fake = http.get();
        for (const Emulator& entry : emulators()) fake->documents[releasesUrl(entry.repo, entry.latestOnly)] = "[]";
        fake->documents[coreIndexUrlFor(Platform::LinuxX64)] = "2026-10-28 3610a686 genesis_plus_gx_libretro.so.zip\n";
        Screens s(root, std::move(http));
        s.model->setScreen("download-emulators", {});
        check(s.waitForJob(), "checked");
        checkEqual(s.detail("emulator-mame"), "No Linux x86_64 build", "MAME");
        checkEqual(s.model->get("download:retroArchInstalled"), "yes", "the cores link");
        s.model->setScreen("download-cores", {});
        check(s.waitForJob(), "cores read");
        Forms::Rows cores = s.model->rows("download:cores", 100);
        check(cores.items.size() == 1 && cores.items[0].id == "genesis_plus_gx", "Linux's cores listed");
        checkEqual(s.model->get("download:unsupported"), "", "no note");
    });

    run("screens: elsewhere, only the note", [&] {
        OnPlatform scope(Platform::Other);
        auto root = makeRoot("screens-other");
        Screens s(root, std::make_unique<ServingHttp>(), std::nullopt);
        s.model->setScreen("download-emulators", {});
        checkEqual(s.model->get("download:unsupported"), "Emulator downloads aren't available on this system.", "the note");
        checkEqual(s.model->get("download:supported"), "no", "nothing else");
        s.model->update(0.01f);
        check(s.model->get("download:running") == "no", "nothing asked");
    });

    usePlatform(hostPlatform());
});

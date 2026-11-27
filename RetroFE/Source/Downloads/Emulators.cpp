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
#include "Emulators.h"
#include "GitHub.h"
#include "Hash.h"
#include "Http.h"
#include "JsonFields.h"
#include "../Library/Edits.h"
#include "../Library/RetroArch.h"
#include "../Manager/Emulators/Launchers.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>

using nlohmann::json;
namespace fs = std::filesystem;
namespace J = Downloads::JsonFields;

namespace
{
    const char* const libretroStable = "https://buildbot.libretro.com/stable/";
    const char* const libretroNightly = "https://buildbot.libretro.com/nightly/windows/x86_64/latest/";

    std::string readText(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    bool writeText(const fs::path& path, const std::string& text)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path temp = path;
        temp += ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            file << text;
            if (!file) return false;
        }
        fs::rename(temp, path, ec);
        if (ec) fs::remove(temp, ec);
        return !ec;
    }

    bool isSha256(const std::string& hex)
    {
        return hex.size() == 64 && hex.find_first_not_of("0123456789abcdef") == std::string::npos;
    }

    // Where an entry's downloads may come from
    std::string originOf(const Downloads::Emulator& entry)
    {
        return entry.id == "retroarch" ? libretroStable : "https://github.com/" + entry.repo + "/releases/download/";
    }

    // A URL on origin that doesn't climb out of it
    bool fromOrigin(const std::string& url, const std::string& origin)
    {
        return Downloads::isHttpsUrl(url) && url.rfind(origin, 0) == 0 && url.size() > origin.size()
            && url.find("..") == std::string::npos && url.find('\\') == std::string::npos;
    }

    Library::ScanOptions windowsLibrary(const std::string& root, bool games)
    {
        Library::ScanOptions options;
        options.root = root;
        options.os = "windows";
        options.metadata = false;
        options.games = games;
        return options;
    }
}

namespace Downloads
{

bool isCoreName(const std::string& core)
{
    return core.size() <= 80 && std::regex_match(core, std::regex("[a-z0-9_][a-z0-9_-]*"));
}

bool isCoreId(const std::string& id)
{
    return id.rfind("core-", 0) == 0 && isCoreName(id.substr(5));
}

std::string coreUrl(const std::string& core)
{
    return std::string(libretroNightly) + core + "_libretro.dll.zip";
}

std::string coreLauncherName(const std::string& name)
{
    std::string result = "RetroArch";
    bool space = true;
    for (unsigned char c : name) {
        if (c <= 32 || c > 126 || std::string("\\/:*?\"<>|#=,;%").find(c) != std::string::npos) { space = true; continue; }
        if (space && result.size() < 64) result += ' ';
        if (result.size() < 64) result += static_cast<char>(c);
        space = false;
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) result.pop_back();
    return result;
}

void parseCoreIndex(const std::string& text, const std::string& root, std::vector<Package>& packages)
{
    std::istringstream lines(text);
    std::string line;
    std::set<std::string> seen;
    const std::regex pattern(R"(^([0-9]{4})-([0-9]{2})-([0-9]{2}) ([0-9a-fA-F]{8}) ([a-z0-9_][a-z0-9_-]*)_libretro\.dll\.zip\r?$)");
    while (std::getline(lines, line)) {
        std::smatch m;
        if (!std::regex_match(line, m, pattern)) continue;
        int year = std::stoi(m[1]), month = std::stoi(m[2]), day = std::stoi(m[3]);
        const int days[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
        if (year < 1 || month < 1 || month > 12 || day < 1 || day > days[month-1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) continue;
        std::string stem = m[5];
        if (!isCoreName(stem) || !seen.insert(stem).second) continue;
        auto info = Library::RetroArch::readConfigFile((fs::path(root) / "emulators/RetroArch/info" / (stem + "_libretro.info")).string());
        Package package;
        package.id = "core-" + stem;
        package.kind = "core";
        package.name = info["corename"].empty() ? stem : info["corename"];
        package.system = info["systemname"];
        package.description = info["display_name"];
        package.version = m[1].str() + "-" + m[2].str() + "-" + m[3].str();
        PackageFile file;
        file.url = coreUrl(stem);
        file.folder = "RetroArch/cores";
        file.crc32 = static_cast<uint32_t>(std::stoul(m[4], nullptr, 16));
        package.files.push_back(file);
        packages.push_back(package);
    }
}

void readCores(const std::string& cacheFolder, const std::string& root, Http* http, CoreIndex& index)
{
    index = CoreIndex();
    fs::path folder(cacheFolder);
    std::string text = readText(folder / "cores.index");
    std::string modified = readText(folder / "cores.modified");
    Http::Response response;
    std::vector<std::string> headers;
    if (!text.empty() && !modified.empty()) headers.push_back("If-Modified-Since: " + modified);
    bool answered = http && http->request(coreIndexUrl, headers, response);
    if (answered && response.status == 200) {
        text = response.body;
        writeText(folder / "cores.index", text);
        std::string lastModified = response.headers["last-modified"];
        if (lastModified.find_first_of("\r\n") != std::string::npos) lastModified.clear();
        writeText(folder / "cores.modified", lastModified);
    }
    else if (!(answered && response.status == 304 && !text.empty())) {
        if (text.empty()) {
            index.error = !http ? "Downloading isn't supported on this system yet"
                        : "Could not read libretro's core list" + (response.error.empty() ? std::string() : ": " + response.error);
            return;
        }
        index.fromCache = true;
        index.warnings.push_back("Could not read libretro's core list; using the last copy");
    }
    parseCoreIndex(text, root, index.packages);
}

const std::vector<Emulator>& emulators()
{
    static const std::vector<Emulator> entries = {
        { "retroarch", "RetroArch", "RetroArch", "libretro/RetroArch", R"(^v([0-9]+\.[0-9]+\.[0-9]+)$)",
          "https://buildbot.libretro.com/stable/<version>/windows/x86_64/RetroArch.7z", "7z", "RetroArch-Win64/",
          "retroarch.exe", {}, "", "", "Runs games through libretro cores." },
        { "mame", "MAME", "MAME", "mamedev/mame", R"(^mame0([0-9]+)$)", "<tag>b_x64.exe", "sfx", "",
          "mame.exe", {}, "MAME", R"(%ITEM_NAME% -rompath "%ITEM_DIRECTORY%.")", "Arcade machine emulator." },
        { "duckstation", "DuckStation", "DuckStation", "stenzek/duckstation", R"(^v(0\.1-[0-9]+)$)",
          "duckstation-windows-x64-release.zip", "zip", "", "duckstation-qt-x64-ReleaseLTCG.exe", { "portable.txt" },
          "DuckStation", R"(-batch -fullscreen -- "%ITEM_FILEPATH%")", "Sony PlayStation emulator." },
        { "pcsx2", "PCSX2", "PCSX2", "PCSX2/pcsx2", R"(^v([0-9]+\.[0-9]+\.[0-9]+)$)",
          "pcsx2-v<version>-windows-x64-Qt.7z", "7z", "", "pcsx2-qt.exe", { "portable.txt" },
          "PCSX2", R"(-batch -fullscreen -- "%ITEM_FILEPATH%")", "Sony PlayStation 2 emulator.", true },
        { "ppsspp", "PPSSPP", "PPSSPP", "hrydgard/ppsspp", R"(^v([0-9]+\.[0-9]+\.[0-9]+)$)",
          "PPSSPP-v<version>-Windows-x64.zip", "zip", "", "PPSSPPWindows64.exe", {},
          "PPSSPP", R"(--fullscreen "%ITEM_FILEPATH%")", "Sony PSP emulator." },
        { "flycast", "Flycast", "Flycast", "flyinghead/flycast", R"(^v([0-9]+\.[0-9]+(?:\.[0-9]+)?)$)",
          "flycast-win64-<version>.zip", "zip", "", "flycast.exe", {},
          "Flycast", R"("%ITEM_FILEPATH%")", "Sega Dreamcast and NAOMI emulator." }
    };
    return entries;
}

const std::vector<Emulator>& emulators(Platform platform)
{
    static const auto makeEntries = [](Platform platform) {
        std::vector<Emulator> entries = emulators();
        if (platform == Platform::WindowsX64) return entries;
        const bool isLinux = platform == Platform::LinuxX64 || platform == Platform::LinuxArm64;
        const bool arm = platform == Platform::LinuxArm64;
        for (auto& entry : entries) {
            entry.assetRule.clear();
            entry.archive.clear();
            entry.topFolder.clear();
            entry.program.clear();
            entry.portableFiles.clear();
            if (entry.id == "retroarch" && !arm) {
                if (isLinux) {
                    entry.assetRule = "https://buildbot.libretro.com/stable/<version>/linux/x86_64/RetroArch.7z";
                    entry.archive = "7z";
                    entry.topFolder = "RetroArch-Linux-x86_64/";
                    entry.program = "RetroArch-Linux-x86_64.AppImage";
                } else {
                    entry.assetRule = "https://buildbot.libretro.com/stable/<version>/apple/osx/universal/RetroArch_Metal.dmg";
                    entry.archive = "dmg";
                    entry.program = "RetroArch.app/Contents/MacOS/RetroArch";
                }
            } else if (entry.id == "duckstation") {
                if (isLinux) {
                    entry.tagRule = "^(latest)$";
                    entry.latestOnly = true;
                    entry.assetRule = arm ? "DuckStation-arm64.AppImage" : "DuckStation-x64.AppImage";
                    entry.archive = "appimage";
                    entry.program = entry.assetRule;
                } else {
                    entry.assetRule = "duckstation-mac-release.zip";
                    entry.archive = "zip";
                    entry.program = "DuckStation.app/Contents/MacOS/DuckStation";
                }
            } else if (entry.id == "pcsx2" && isLinux && !arm) {
                entry.assetRule = "pcsx2-v<version>-linux-appimage-x64-Qt.AppImage";
                entry.archive = "appimage";
                entry.program = "pcsx2.AppImage";
                entry.portableFiles = { "portable.txt" };
            } else if (entry.id == "ppsspp") {
                if (isLinux) {
                    entry.assetRule = arm ? "PPSSPP-v<version>-anylinux-aarch64.AppImage" : "PPSSPP-v<version>-anylinux-x86_64.AppImage";
                    entry.archive = "appimage";
                    entry.program = "PPSSPP.AppImage";
                } else {
                    entry.assetRule = "PPSSPPSDL-macOS-v<version>.zip";
                    entry.archive = "zip";
                    entry.program = "PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL";
                }
            } else if (entry.id == "flycast" && !arm) {
                entry.assetRule = isLinux ? "flycast-x86_64-<version>.AppImage" : "flycast-macOS-<version>.zip";
                entry.archive = isLinux ? "appimage" : "zip";
                entry.program = isLinux ? "flycast.AppImage" : "Flycast.app/Contents/MacOS/Flycast";
            }
        }
        return entries;
    };
    static const auto windows = makeEntries(Platform::WindowsX64);
    static const auto linuxX64 = makeEntries(Platform::LinuxX64);
    static const auto linuxArm64 = makeEntries(Platform::LinuxArm64);
    static const auto macArm64 = makeEntries(Platform::MacArm64);
    static const auto macX64 = makeEntries(Platform::MacX64);
    switch (platform) {
    case Platform::WindowsX64: return windows;
    case Platform::LinuxX64: return linuxX64;
    case Platform::LinuxArm64: return linuxArm64;
    case Platform::MacArm64: return macArm64;
    case Platform::MacX64: return macX64;
    }
    return windows;
}

const Emulator* findEmulator(const std::string& packageId)
{
    for (const auto& entry : emulators()) if (packageId == "emulator-" + entry.id) return &entry;
    return nullptr;
}

bool parseEmulatorReleases(const std::string& text, const Emulator& entry,
                           std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error)
{
    json releases = json::parse(text, nullptr, false);
    if (releases.is_object() && releases.contains("tag_name")) releases = json::array({ releases });
    if (!releases.is_array()) {
        std::string message = J::text(releases, "message");
        error = message.empty() ? "GitHub's answer isn't a list of releases" : "GitHub says: " + message;
        return false;
    }
    Package best;
    std::vector<std::pair<std::string, std::string>> problems;
    const std::regex tagRule(entry.tagRule);
    for (const auto& release : releases) {
        if (!release.is_object() || J::flag(release, "draft") || J::flag(release, "prerelease")) continue;
        std::string tag = J::text(release, "tag_name");
        std::smatch match;
        if (!std::regex_match(tag, match, tagRule)) continue;
        std::string version = match[1];
        if (entry.id == "mame") version = "0." + version;
        if (entry.id == "duckstation" && tag == "latest") {
            std::string published = J::text(release, "published_at");
            if (!std::regex_match(published, std::regex(R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$)"))) continue;
            version = published.substr(0, 10);
            std::replace(version.begin(), version.end(), '-', '.');
        } else if (entry.id == "duckstation") version.replace(version.find('-'), 1, ".");
        std::string wanted = entry.assetRule;
        for (const auto& replacement : { std::make_pair(std::string("<version>"), version), std::make_pair(std::string("<tag>"), tag) }) {
            size_t at = wanted.find(replacement.first);
            if (at != std::string::npos) wanted.replace(at, replacement.first.size(), replacement.second);
        }
        PackageFile file;
        bool libretro = entry.id == "retroarch";
        if (libretro) file.url = wanted;
        else for (const auto& asset : J::list(release, "assets")) {
            if (J::text(asset, "name") != wanted || (asset.contains("state") && J::text(asset, "state") != "uploaded")) continue;
            std::string digest = J::text(asset, "digest");
            if (digest.rfind("sha256:", 0) == 0) file.sha256 = parseSha256File(digest);
            if (file.sha256.empty()) { problems.emplace_back(version, entry.name + " " + tag + ": no SHA-256 digest"); continue; }
            file.url = J::text(asset, "browser_download_url");
            J::count(asset, "size", file.size);
            break;
        }
        if (!file.url.empty() && !fromOrigin(file.url, originOf(entry))) {
            problems.emplace_back(version, entry.name + " " + tag + ": download URL is outside its release origin");
            continue;
        }
        if (file.url.empty()) continue;
        if (!best.version.empty() && compareVersions(version, best.version) <= 0) continue;
        file.folder = entry.folder;
        best = Package();
        best.id = "emulator-" + entry.id;
        best.name = entry.name;
        best.kind = "emulator";
        best.version = version;
        best.description = entry.description;
        best.noChecksum = libretro;
        best.files = { file };
    }
    if (!best.id.empty()) packages.push_back(best);
    for (const auto& problem : problems) {
        if (best.id.empty() || compareVersions(problem.first, best.version) > 0) warnings.push_back(problem.second);
    }
    return true;
}

std::string checkPackage(const Package& package)
{
    if (!isValidId(package.id) || package.files.empty()) return "That isn't a download RetroFE knows.";
    if (package.kind == "emulator") {
        const Emulator* entry = findEmulator(package.id);
        if (!entry) return "Only emulators of RetroFE's built-in list can be installed.";
        for (const PackageFile& file : package.files) {
            if (file.folder != entry->folder) return entry->name + " may only be installed in emulators/" + entry->folder + ".";
            if (file.crc32) return "Only cores are checked by CRC-32.";
            if (!fromOrigin(file.url, originOf(*entry))) return entry->name + " is only downloaded from " + originOf(*entry);
            if (entry->id == "retroarch") {
                // libretro publishes no checksum for RetroArch.7z: only its own server, over HTTPS
                if (!package.noChecksum && file.sha256.empty()) return "This release has no checksum, so RetroFE won't install it.";
                if (!file.sha256.empty() && !isSha256(file.sha256)) return "This release's checksum isn't a SHA-256.";
            }
            else if (!isSha256(file.sha256)) {
                return "This release has no checksum, so RetroFE won't install it.";
            }
        }
        return "";
    }
    if (package.kind == "core") {
        if (!isCoreId(package.id)) return "That isn't a RetroArch core.";
        const std::string core = package.id.substr(5);
        if (package.files.size() != 1) return "A core is one download.";
        const PackageFile& file = package.files[0];
        if (file.url != coreUrl(core)) return "Cores are only downloaded from libretro's nightly builds.";
        if (!file.crc32) return "libretro's list has no CRC-32 for this core, so RetroFE won't install it.";
        if (file.folder != "RetroArch/cores") return "Cores may only be installed in emulators/RetroArch/cores.";
        return "";
    }
    return "Only emulators and RetroArch cores can be downloaded.";
}

std::string createEmulatorLauncher(const std::string& root, const std::string& downloadsFolder, Manifest& manifest)
{
    std::lock_guard<std::recursive_mutex> lock(Library::libraryWriteMutex());
    const Emulator* entry = manifest.kind == "emulator" ? findEmulator(manifest.id) : nullptr;
    Emulator core;
    if (manifest.kind == "core" && isCoreId(manifest.id)) {
        core.folder = "RetroArch";
        core.program = "retroarch.exe";
        core.launcher = coreLauncherName(manifest.name);
        core.arguments = "-L \"%RETROFE_PATH%\\emulators\\RetroArch\\cores\\" + manifest.id.substr(5) + "_libretro.dll\" \"%ITEM_FILEPATH%\"";
        entry = &core;
    }
    if (!entry) return "";
    if (entry->launcher.empty()) return " Add a core next from Cores.";
    auto index = Library::buildIndex(windowsLibrary(root, false));
    if (!index) return " Could not read the library to create the launcher.";
    if (index->findLauncher(Library::LauncherScope::Global, entry->launcher) >= 0)
        return "; a launcher called " + entry->launcher + " already exists, so it was left as it is.";
    Library::Editor editor(root);
    Library::Editor::NewLauncher launcher;
    launcher.name = entry->launcher;
    launcher.executable = Emulators::portablePath(root, (fs::path(root) / "emulators" / entry->folder / entry->program).string(), "windows");
    launcher.arguments = entry->arguments;
    auto result = editor.addLauncher(*index, launcher);
    if (!result.ok) return " Launcher " + entry->launcher + " was left alone: " + result.message;
    manifest.launcherPath = "launchers.windows/" + entry->launcher + ".conf";
    manifest.launcherSha256 = sha256File(fs::path(root) / manifest.launcherPath);
    manifest.launcherName = entry->launcher;
    std::string error;
    if (!writeManifest((fs::path(downloadsFolder) / "installed" / (manifest.id + ".json")).string(), manifest, error)) return " " + error;
    return ", with the launcher " + entry->launcher + ".";
}

std::string removeEmulatorLauncher(const std::string& root, const Manifest& manifest)
{
    if (manifest.launcherPath.empty()) return "";
    std::lock_guard<std::recursive_mutex> lock(Library::libraryWriteMutex());
    const Emulator* entry = manifest.kind == "emulator" ? findEmulator(manifest.id) : nullptr;
    Emulator core;
    if (manifest.kind == "core" && isCoreId(manifest.id) &&
        (manifest.launcherName == "RetroArch" || manifest.launcherName.rfind("RetroArch ", 0) == 0) &&
        Library::Editor::checkLauncherName(manifest.launcherName).empty()) {
        core.launcher = manifest.launcherName;
        entry = &core;
    }
    // Only the launcher the built-in list says it created, never a path from the manifest alone
    if (!entry || entry->launcher.empty() || manifest.launcherPath != "launchers.windows/" + entry->launcher + ".conf") return "";
    auto path = fs::path(root) / manifest.launcherPath;
    std::error_code ec;
    if (!fs::exists(path, ec)) return "";
    if (sha256File(path) != manifest.launcherSha256) return " Kept launcher " + entry->launcher + " because it has changed.";
    auto index = Library::buildIndex(windowsLibrary(root, true));
    if (!index) return " Kept launcher " + entry->launcher + ": could not read its users.";
    int launcher = index->findLauncher(Library::LauncherScope::Global, entry->launcher);
    if (launcher < 0) return "";
    std::string users;
    for (const auto& use : index->usesOf(launcher)) {
        if (use.collection < 0 || static_cast<size_t>(use.collection) >= index->collections.size()) continue;
        if (!users.empty()) users += ", ";
        users += index->collections[static_cast<size_t>(use.collection)].name;
        if (!use.game.empty()) users += "/" + use.game;
    }
    if (!users.empty()) return " Kept launcher " + entry->launcher + ": used by " + users + ".";
    Library::Editor editor(root);
    auto result = editor.removeLauncher(*index, Library::LauncherScope::Global, entry->launcher);
    return result.ok ? " Removed launcher " + entry->launcher + "." : " Kept launcher " + entry->launcher + ": " + result.message;
}

}

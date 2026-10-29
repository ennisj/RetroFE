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
#include "Index.h"
#include "Hash.h"
#include "../Library/Edits.h"
#include "../Manager/Emulators/Launchers.h"
#include <filesystem>
#include "JsonFields.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <ctime>
#include <fstream>
#include <sstream>
#include "Http.h"
#include "../Library/RetroArch.h"

using nlohmann::json;
namespace J = Catalog::JsonFields;

namespace Catalog
{
bool isCoreId(const std::string& id)
{
    return std::regex_match(id, std::regex("core-[a-z0-9_][a-z0-9_-]*"));
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

void parseCoreIndex(const std::string& text, const std::string& source, const std::string& root, std::vector<Package>& packages)
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
        if (!seen.insert(stem).second) continue;
        auto info = Library::RetroArch::readConfigFile((std::filesystem::path(root) / "emulators/RetroArch/info" / (stem + "_libretro.info")).string());
        Package package;
        package.id = "core-" + stem;
        package.kind = "core";
        package.source = source;
        package.name = info["corename"].empty() ? stem : info["corename"];
        package.system = info["systemname"];
        package.description = info["display_name"];
        package.version = m[1].str() + "-" + m[2].str() + "-" + m[3].str();
        package.requirements.push_back({ "emulator-retroarch", "" });
        PackageFile file;
        file.url = "https://buildbot.libretro.com/nightly/windows/x86_64/latest/" + stem + "_libretro.dll.zip";
        file.target = "emulators";
        file.path = "RetroArch/cores";
        file.crc32 = static_cast<uint32_t>(std::stoul(m[4], nullptr, 16));
        package.files.push_back(file);
        packages.push_back(package);
    }
}

void readCores(const SourceConfig& source, const std::string& cacheFolder, const std::string& root, Http* http, SourceContents& contents)
{
    Manifest manifest;
    std::string error;
    if (!readManifest((std::filesystem::path(cacheFolder).parent_path() / "installed/emulator-retroarch.json").string(), manifest, error)) {
        contents.warnings.push_back("Install RetroArch first");
        return;
    }
    auto folder = std::filesystem::path(cacheFolder) / source.name;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    auto read = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    std::string text = read(folder / "cores.index"), modified = read(folder / "cores.modified");
    Http::Response response;
    std::vector<std::string> headers;
    if (!modified.empty()) headers.push_back("If-Modified-Since: " + modified);
    if (http && http->request("https://buildbot.libretro.com/nightly/windows/x86_64/latest/.index-extended", headers, response) && response.status == 200) {
        text = response.body;
        std::ofstream(folder / "cores.index", std::ios::binary) << text;
        std::ofstream(folder / "cores.modified", std::ios::binary) << response.headers["last-modified"];
    }
    else if (response.status != 304) {
        contents.fromCache = true;
        contents.warnings.push_back("Could not read libretro's core list; using the last copy");
    }
    parseCoreIndex(text, source.name, root, contents.packages);
}

const std::vector<Emulator>& emulators()
{
    static const std::vector<Emulator> entries = {
        { "retroarch", "RetroArch", "RetroArch", "libretro/RetroArch", R"(^v([0-9]+\.[0-9]+\.[0-9]+)$)",
          "https://buildbot.libretro.com/stable/<version>/windows/x86_64/RetroArch.7z", "7z", "RetroArch-Win64/",
          "retroarch.exe", {}, "", "", "Runs games through libretro cores.", "", "" },
        { "mame", "MAME", "MAME", "mamedev/mame", R"(^mame0([0-9]+)$)", "<tag>b_x64.exe", "sfx", "",
          "mame.exe", {}, "MAME", R"(%ITEM_NAME% -rompath "%ITEM_DIRECTORY%.")", "Arcade machine emulator.", "Arcade", "zip,7z,chd" },
        { "duckstation", "DuckStation", "DuckStation", "stenzek/duckstation", R"(^v(0\.1-[0-9]+)$)",
          "duckstation-windows-x64-release.zip", "zip", "", "duckstation-qt-x64-ReleaseLTCG.exe", { "portable.txt" },
          "DuckStation", R"(-batch -fullscreen -- "%ITEM_FILEPATH%")", "Sony PlayStation emulator.", "PlayStation", "cue,chd,bin,img,iso,m3u,pbp,ecm,mds,psexe,psf,minipsf" },
        { "pcsx2", "PCSX2", "PCSX2", "PCSX2/pcsx2", R"(^v([0-9]+\.[0-9]+\.[0-9]+)$)",
          "pcsx2-v<version>-windows-x64-Qt.7z", "7z", "", "pcsx2-qt.exe", { "portable.txt" },
          "PCSX2", R"(-batch -fullscreen -- "%ITEM_FILEPATH%")", "Sony PlayStation 2 emulator.", "PlayStation 2", "iso,chd,cso,zso,gz,elf,irx,m3u,cue,bin,img,mdf,nrg,dump" },
        { "ppsspp", "PPSSPP", "PPSSPP", "hrydgard/ppsspp", R"(^v([0-9]+\.[0-9]+\.[0-9]+)$)",
          "PPSSPP-v<version>-Windows-x64.zip", "zip", "", "PPSSPPWindows64.exe", {},
          "PPSSPP", R"(--fullscreen "%ITEM_FILEPATH%")", "Sony PSP emulator.", "PSP", "iso,cso,chd,pbp,elf,prx" },
        { "flycast", "Flycast", "Flycast", "flyinghead/flycast", R"(^v([0-9]+\.[0-9]+(?:\.[0-9]+)?)$)",
          "flycast-win64-<version>.zip", "zip", "", "flycast.exe", {},
          "Flycast", R"("%ITEM_FILEPATH%")", "Sega Dreamcast and NAOMI emulator.", "Dreamcast", "chd,cdi,gdi,cue,m3u,elf,dat,zip,7z,lst" }
    };
    return entries;
}

const Emulator* findEmulator(const std::string& packageId)
{
    for (const auto& entry : emulators()) if (packageId == "emulator-" + entry.id) return &entry;
    return nullptr;
}

bool parseEmulatorReleases(const std::string& text, const Emulator& entry, const std::string& source,
                           std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error)
{
    json releases = json::parse(text, nullptr, false);
    if (!releases.is_array()) { error = "GitHub's answer isn't a list of releases"; return false; }
    Package best;
    for (const auto& release : releases) {
        if (!release.is_object() || J::flag(release, "draft") || J::flag(release, "prerelease")) continue;
        std::string tag = J::text(release, "tag_name");
        std::smatch match;
        if (!std::regex_match(tag, match, std::regex(entry.tagRule))) continue;
        std::string version = match[1];
        if (entry.id == "mame") version = "0." + version;
        if (entry.id == "duckstation") version.replace(version.find('-'), 1, ".");
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
            if (file.sha256.empty()) { warnings.push_back(entry.name + " " + tag + ": no SHA-256 digest"); continue; }
            file.url = J::text(asset, "browser_download_url");
            J::count(asset, "size", file.size);
            break;
        }
        std::string origin = libretro ? "https://buildbot.libretro.com/stable/"
            : "https://github.com/" + entry.repo + "/releases/download/";
        if (!file.url.empty() && (file.url.rfind(origin, 0) != 0 || !isAllowedLocation(file.url))) {
            warnings.push_back(entry.name + " " + tag + ": download URL is outside its release origin");
            continue;
        }
        if (file.url.empty()) continue;
        if (!best.version.empty() && compareVersions(version, best.version) <= 0) continue;
        file.target = "emulators";
        file.path = entry.folder;
        best = Package();
        best.source = source;
        best.id = "emulator-" + entry.id;
        best.name = entry.name;
        best.kind = "emulator";
        best.version = version;
        best.description = entry.description;
        best.noChecksum = libretro;
        best.files = { file };
    }
    if (!best.id.empty()) packages.push_back(best);
    return true;
}

std::string createEmulatorLauncher(const std::string& root, const std::string& catalog, Manifest& manifest)
{
    std::lock_guard<std::recursive_mutex> lock(Library::libraryWriteMutex());
    const Emulator* entry = findEmulator(manifest.id);
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
    Library::ScanOptions options;
    options.root = root;
    options.os = "windows";
    options.metadata = false;
    auto index = Library::buildIndex(options);
    if (!index) return " Could not read the library to create the launcher.";
    if (index->findLauncher(Library::LauncherScope::Global, entry->launcher) >= 0)
        return "; a launcher called " + entry->launcher + " already exists, so it was left as it is.";
    Library::Editor editor(root);
    Library::Editor::NewLauncher launcher;
    launcher.name = entry->launcher;
    launcher.executable = Emulators::portablePath(root, (std::filesystem::path(root) / "emulators" / entry->folder / entry->program).string(), "windows");
    launcher.arguments = entry->arguments;
    auto result = editor.addLauncher(*index, launcher);
    if (!result.ok) return " Launcher " + entry->launcher + " was left alone: " + result.message;
    manifest.launcherPath = "launchers.windows/" + entry->launcher + ".conf";
    manifest.launcherSha256 = sha256File(std::filesystem::path(root) / manifest.launcherPath);
    manifest.launcherName = entry->launcher;
    std::string error;
    if (!writeManifest((std::filesystem::path(catalog) / "installed" / (manifest.id + ".json")).string(), manifest, error)) return " " + error;
    return ", with the launcher " + entry->launcher + ".";
}

std::string removeEmulatorLauncher(const std::string& root, const Manifest& manifest)
{
    if (manifest.launcherPath.empty()) return "";
    std::lock_guard<std::recursive_mutex> lock(Library::libraryWriteMutex());
    const Emulator* entry = findEmulator(manifest.id);
    Emulator core;
    if (manifest.kind == "core" && isCoreId(manifest.id) &&
        (manifest.launcherName == "RetroArch" || manifest.launcherName.rfind("RetroArch ", 0) == 0) &&
        Library::Editor::checkLauncherName(manifest.launcherName).empty()) {
        core.launcher = manifest.launcherName;
        entry = &core;
    }
    if (!entry || entry->launcher.empty() || manifest.launcherPath != "launchers.windows/" + entry->launcher + ".conf") return "";
    auto path = std::filesystem::path(root) / manifest.launcherPath;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return "";
    if (sha256File(path) != manifest.launcherSha256) return " Kept launcher " + entry->launcher + " because it has changed.";
    Library::ScanOptions options;
    options.root = root;
    options.os = "windows";
    options.metadata = false;
    auto index = Library::buildIndex(options);
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

void readEmulators(const SourceConfig& source, const std::string& cacheFolder, Http* http, SourceContents& contents, const std::string& root)
{
#ifndef _WIN32
    (void)source; (void)cacheFolder; (void)http; (void)root;
    contents.error = "Emulator downloads are Windows-only for now";
#else
    readCores(source, cacheFolder, root.empty() ? std::filesystem::path(cacheFolder).parent_path().parent_path().string() : root, http, contents);
    bool limitWarning = false;
    for (const auto& entry : emulators()) {
        SourceConfig github;
        github.name = source.name;
        github.repo = entry.repo;
        SourceContents one;
        readGitHubReleases(github, cacheFolder, http, static_cast<int64_t>(std::time(nullptr)), one,
            [&](const std::string& text, std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error) {
                return parseEmulatorReleases(text, entry, source.name, packages, warnings, error);
            }, entry.id);
        contents.packages.insert(contents.packages.end(), one.packages.begin(), one.packages.end());
        auto warn = [&](const std::string& message) {
            if (message.find("GitHub's limit") != std::string::npos) {
                if (limitWarning) return;
                limitWarning = true;
            }
            contents.warnings.push_back(message);
        };
        for (const auto& warning : one.warnings) warn(warning);
        if (!one.error.empty()) warn(entry.name + ": " + one.error);
        contents.fromCache = contents.fromCache || one.fromCache;
    }
#endif
}
}

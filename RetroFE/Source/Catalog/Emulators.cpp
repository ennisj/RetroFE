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
#include <ctime>

using nlohmann::json;
namespace J = Catalog::JsonFields;

namespace Catalog
{
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
          "PCSX2", R"(-batch -fullscreen -- "%ITEM_FILEPATH%")", "Sony PlayStation 2 emulator." },
        { "ppsspp", "PPSSPP", "PPSSPP", "hrydgard/ppsspp", R"(^v([0-9]+\.[0-9]+\.[0-9]+)$)",
          "PPSSPP-v<version>-Windows-x64.zip", "zip", "", "PPSSPPWindows64.exe", {},
          "PPSSPP", R"(--fullscreen "%ITEM_FILEPATH%")", "Sony PSP emulator." },
        { "flycast", "Flycast", "Flycast", "flyinghead/flycast", R"(^v([0-9]+\.[0-9]+(?:\.[0-9]+)?)$)",
          "flycast-win64-<version>.zip", "zip", "", "flycast.exe", {},
          "Flycast", R"("%ITEM_FILEPATH%")", "Sega Dreamcast and NAOMI emulator." }
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
        bool libretro = entry.id == "retroarch" && wanted.rfind("https://buildbot.libretro.com/", 0) == 0;
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
        if (file.url.rfind("https://", 0) != 0 || !isAllowedLocation(file.url)) continue;
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
    const Emulator* entry = findEmulator(manifest.id);
    if (!entry || entry->launcher.empty()) return "";
    Library::ScanOptions options;
    options.root = root;
    options.os = "windows";
    options.metadata = false;
    auto index = Library::buildIndex(options);
    if (!index) return " Could not read the library to create the launcher.";
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
    return " Created the launcher " + entry->launcher + ".";
}

std::string removeEmulatorLauncher(const std::string& root, const Manifest& manifest)
{
    if (manifest.launcherPath.empty()) return "";
    const Emulator* entry = findEmulator(manifest.id);
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

void readEmulators(const SourceConfig& source, const std::string& cacheFolder, Http* http, SourceContents& contents)
{
#ifndef _WIN32
    (void)source; (void)cacheFolder; (void)http;
    contents.error = "Emulator downloads are Windows-only for now";
#else
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
        contents.warnings.insert(contents.warnings.end(), one.warnings.begin(), one.warnings.end());
        if (!one.error.empty()) contents.warnings.push_back(entry.name + ": " + one.error);
        contents.fromCache = contents.fromCache || one.fromCache;
    }
#endif
}
}

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
#include "Service.h"
#include "Download.h"
#include "Emulators.h"
#include "GitHub.h"
#include "../Utility/Log.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    Downloads::Outcome failed(const std::string& message)
    {
        Downloads::Outcome outcome;
        outcome.message = message;
        return outcome;
    }

    Downloads::Outcome stopped()
    {
        Downloads::Outcome outcome;
        outcome.cancelled = true;
        outcome.message = "Stopped. Install again to continue where it left off.";
        return outcome;
    }

    bool stopping(const std::atomic<bool>* stop)
    {
        return stop && *stop;
    }

    void report(const Downloads::Progress& progress, const std::string& text, uint64_t done = 0, uint64_t total = 0)
    {
        if (progress) progress(text, done, total);
    }

    // The latest release an entry's GitHub list offers
    void readLatest(const Downloads::Emulator& entry, const std::string& cacheFolder, Downloads::Http* http, int64_t now,
                    Downloads::ReleaseList& list, int64_t maxAge)
    {
        Downloads::readGitHubReleases(entry.repo, cacheFolder, entry.id, http, now,
            [&](const std::string& text, std::vector<Downloads::Package>& packages, std::vector<std::string>& warnings, std::string& error) {
                return Downloads::parseEmulatorReleases(text, entry, packages, warnings, error);
            }, list, entry.latestOnly, maxAge);
    }

    // Why a list offered nothing, for the screen
    std::string nothingOffered(const Downloads::Emulator& entry, const Downloads::ReleaseList& list)
    {
        if (!list.error.empty()) return list.error;
        for (const std::string& warning : list.warnings) {
            if (warning.find("no SHA-256 digest") != std::string::npos) {
                return "No checksum on this release, so RetroFE won't install it";
            }
            if (warning.find("outside its release origin") != std::string::npos) return warning;
        }
        return "No release of " + entry.name + " for Windows x64 was found";
    }

    // "a, b and 3 more"
    std::string someNames(const std::vector<std::string>& names)
    {
        std::string text;
        const size_t shown = std::min<size_t>(names.size(), 5);
        for (size_t i = 0; i < shown; i++) {
            if (i) text += ", ";
            text += names[i];
        }
        if (names.size() > shown) text += " and " + std::to_string(names.size() - shown) + " more";
        return text;
    }
}

namespace Downloads
{

std::string Service::downloadsFolder(const std::string& root)
{
    return (fs::path(root) / "manage" / "downloads").string();
}

Service::Service(const std::string& root, std::unique_ptr<Http> http)
    : root_(root)
    , folder_(downloadsFolder(root))
    , http_(std::move(http))
    , installer_(downloadsFolder(root), (fs::path(root) / "emulators").string())
{
    // RetroFE's program folder, whatever emulators/ turns out to be
    installer_.protect((fs::path(root) / "retrofe").string());
}

bool Service::supported() const
{
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return http_ != nullptr;
#else
    return false;
#endif
}

std::string Service::cacheFolder() const
{
    return (fs::path(folder_) / "cache").string();
}

void Service::recover()
{
    std::lock_guard<std::mutex> lock(mutex_);
    installer_.recover();
}

std::vector<EmulatorStatus> Service::list(const Progress& progress, const std::atomic<bool>* stop)
{
    (void)stop;
    std::lock_guard<std::mutex> lock(mutex_);
    report(progress, "Reading what's installed...");
    installer_.recover();
    auto installed = installer_.installed();
    std::vector<EmulatorStatus> result;
    for (const Emulator& entry : emulators()) {
        EmulatorStatus status;
        status.id = "emulator-" + entry.id;
        status.name = entry.name;
        status.folder = entry.folder;
        status.description = entry.description;
        status.launcher = entry.launcher;
        auto found = installed.find(status.id);
        if (found != installed.end()) {
            status.installedVersion = found->second.version;
            status.launcherCreated = found->second.launcherName;
        }
        result.push_back(status);
    }
    return result;
}

Release Service::checkOne(size_t index, const std::map<std::string, Manifest>& installed, int64_t maxAge)
{
    const Emulator& entry = emulators()[index];
    Release release;
    release.id = "emulator-" + entry.id;
    release.name = entry.name;
    auto found = installed.find(release.id);
    if (found != installed.end()) release.installedVersion = found->second.version;

    ReleaseList list;
    readLatest(entry, cacheFolder(), http_.get(), now ? now() : static_cast<int64_t>(std::time(nullptr)), list, maxAge);
    release.warnings = list.warnings;
    release.fromCache = list.fromCache;
    release.retryAt = list.retryAt;
    release.checked = list.checked;
    if (list.packages.empty()) {
        release.error = nothingOffered(entry, list);
        return release;
    }
    const Package& package = list.packages[0];
    release.version = package.version;
    release.noChecksum = package.noChecksum;
    if (!package.files.empty()) {
        release.url = package.files[0].url;
        release.size = package.files[0].size;
        release.sha256 = package.files[0].sha256;
    }
    release.updateAvailable = !release.installedVersion.empty() && compareVersions(release.version, release.installedVersion) > 0;
    return release;
}

std::vector<Release> Service::check(const Progress& progress, const std::atomic<bool>* stop, bool fresh)
{
    std::lock_guard<std::mutex> lock(mutex_);
    installer_.recover();
    auto installed = installer_.installed();
    std::vector<Release> releases;
    const size_t count = emulators().size();
    for (size_t i = 0; i < count && !stopping(stop); i++) {
        report(progress, "Checking " + emulators()[i].name + "...", i, count);
        releases.push_back(checkOne(i, installed, fresh ? gitHubFreshMaxAge : gitHubAutomaticMaxAge));
    }
    report(progress, "", releases.size(), count);
    return releases;
}

Outcome Service::installPackage(const Package& package, bool updating, const Progress& progress, const std::atomic<bool>* stop)
{
    std::string problem = checkPackage(package);
    if (problem.empty()) problem = installer_.folderRefusal(package);
    if (!problem.empty()) return failed(problem);
    if (stopping(stop)) return stopped();

    LOG_NOTICE("Downloads", (updating ? "Updating " : "Installing ") + package.id + " " + package.version);
    Downloader downloader(http_.get(), (fs::path(folder_) / "partial").string());
    if (freeSpace) downloader.freeSpace = freeSpace;
    const std::string text = "Downloading " + package.name + " " + package.version;
    report(progress, text, 0, package.totalSize());
    Downloader::Result result;
    bool fetched = downloader.fetch(package, [&](uint64_t received, uint64_t total) {
        report(progress, text, received, total);
        return !stopping(stop);
    }, result);
    if (!fetched) {
        LOG_WARNING("Downloads", package.id + ": " + result.error);
        if (!result.cancelled) downloader.discard(package.id);
        return result.cancelled ? stopped() : failed(result.error);
    }

    report(progress, "Installing " + package.name + "...");
    std::string error;
    installer_.setStopFlag(stop);
    bool installed = installer_.install(package, result.files, error);
    installer_.setStopFlag(nullptr);
    if (!installed) {
        LOG_ERROR("Downloads", package.id + ": " + error);
        if (error != "Stopped") downloader.discard(package.id);
        return error == "Stopped" ? stopped() : failed(error);
    }
    downloader.discard(package.id);

    Outcome outcome;
    outcome.ok = true;
    std::string launcherMessage;
    if (!updating) {
        report(progress, "Creating the launcher...");
        Manifest manifest;
        if (readManifest(installer_.manifestPath(package.id), manifest, error)) {
            launcherMessage = createEmulatorLauncher(root_, folder_, manifest);
            outcome.libraryChanged = !manifest.launcherPath.empty();
        }
    }
    LOG_NOTICE("Downloads", "Installed " + package.id + " " + package.version);
    const Emulator* emulator = !updating && package.kind == "emulator" ? findEmulator(package.id) : nullptr;
    std::string message = emulator ? package.name + " is installed in emulators\\" + emulator->folder
                                   : (updating ? "Updated to " : "Installed ") + package.name + " " + package.version;
    if (!emulator || launcherMessage.empty() || launcherMessage.front() == ' ') message += ".";
    if (!updating && package.kind == "core") {
        message = package.name + " is installed";
        if (launcherMessage.empty() || launcherMessage.front() == ' ') message += ".";
    }
    outcome.message = message + launcherMessage + installer_.installMessage();
    return outcome;
}

Outcome Service::install(const std::string& id, const Progress& progress, const std::atomic<bool>* stop)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const Emulator* entry = findEmulator(id);
    if (!entry) return failed("That isn't an emulator RetroFE can download.");
    installer_.recover();
    if (!http_) return failed("Downloading isn't supported on this system yet.");

    report(progress, "Checking " + entry->name + "...");
    auto installed = installer_.installed();
    auto current = installed.find(id);
    const bool updating = current != installed.end();
    ReleaseList list;
    readLatest(*entry, cacheFolder(), http_.get(), now ? now() : static_cast<int64_t>(std::time(nullptr)), list, gitHubAutomaticMaxAge);
    if (list.packages.empty()) return failed(nothingOffered(*entry, list) + ".");
    const Package& package = list.packages[0];
    if (updating && compareVersions(package.version, current->second.version) <= 0) {
        Outcome outcome;
        outcome.ok = true;
        outcome.message = entry->name + " " + current->second.version + " is installed and up to date.";
        return outcome;
    }
    return installPackage(package, updating, progress, stop);
}

Outcome Service::remove(const std::string& id, const Progress& progress, const std::atomic<bool>* stop)
{
    (void)stop;
    std::lock_guard<std::mutex> lock(mutex_);
    const Emulator* entry = findEmulator(id);
    if (!entry) return failed("That isn't an emulator RetroFE can download.");
    installer_.recover();
    Manifest manifest;
    std::string error;
    if (!readManifest(installer_.manifestPath(id), manifest, error) || manifest.removed) {
        return failed(entry->name + " isn't installed.");
    }
    report(progress, "Removing " + entry->name + "...");
    Installer::Removal removal;
    if (!installer_.remove(id, removal, error)) return failed(error);

    Outcome outcome;
    outcome.ok = true;
    std::string message = "Removed " + entry->name + ".";
    std::string launcher = removeEmulatorLauncher(root_, manifest);
    outcome.libraryChanged = launcher.find(" Removed launcher ") != std::string::npos;
    message += launcher;
    if (removal.restored > 0) {
        message += " Put back " + std::to_string(removal.restored) + (removal.restored == 1 ? " file" : " files") + " it had replaced.";
    }
    if (removal.kept > 0) {
        message += " Kept " + std::to_string(removal.kept) + (removal.kept == 1 ? " file" : " files") + " that changed since it was installed.";
    }
    if (!removal.left.empty()) {
        std::vector<std::string> names;
        for (const std::string& path : removal.left) names.push_back(path.substr(entry->folder.size() + 1));
        message += " Left in emulators\\" + entry->folder + ": " + someNames(names) + ".";
    }
    LOG_NOTICE("Downloads", "Removed " + id);
    outcome.message = message;
    return outcome;
}

void Service::readCoreList(CoreList& list, std::vector<Package>& packages)
{
    auto installed = installer_.installed();
    if (!installed.count("emulator-retroarch")) {
        list.error = "Install RetroArch first";
        return;
    }
    CoreIndex index;
    readCores(cacheFolder(), root_, http_.get(), index);
    list.error = index.error;
    list.warnings = index.warnings;
    list.fromCache = index.fromCache;
    packages = index.packages;
    for (const Package& package : packages) {
        Core core;
        core.id = package.id;
        core.core = package.id.substr(5);
        core.name = package.name;
        core.system = package.system;
        core.description = package.description;
        core.version = package.version;
        if (!package.files.empty()) {
            core.url = package.files[0].url;
            core.crc32 = package.files[0].crc32.value_or(0);
        }
        auto found = installed.find(package.id);
        if (found != installed.end()) {
            core.installedVersion = found->second.version;
            core.updateAvailable = compareVersions(core.version, core.installedVersion) > 0;
        }
        list.cores.push_back(core);
    }
}

CoreList Service::cores(const std::string& system, const Progress& progress, const std::atomic<bool>* stop)
{
    (void)stop;
    std::lock_guard<std::mutex> lock(mutex_);
    installer_.recover();
    report(progress, "Reading libretro's core list...");
    CoreList list;
    std::vector<Package> packages;
    readCoreList(list, packages);
    if (!system.empty() && system != "all") {
        list.cores.erase(std::remove_if(list.cores.begin(), list.cores.end(), [&](const Core& core) { return core.system != system; }),
                         list.cores.end());
    }
    // Installed ones first, then by name
    std::stable_sort(list.cores.begin(), list.cores.end(), [](const Core& a, const Core& b) {
        bool ai = !a.installedVersion.empty(), bi = !b.installedVersion.empty();
        return ai != bi ? ai : lower(a.name) < lower(b.name);
    });
    return list;
}

Outcome Service::installCore(const std::string& core, const Progress& progress, const std::atomic<bool>* stop)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isCoreName(core)) return failed("That isn't a RetroArch core.");
    installer_.recover();
    if (!http_) return failed("Downloading isn't supported on this system yet.");
    report(progress, "Reading libretro's core list...");
    CoreList list;
    std::vector<Package> packages;
    readCoreList(list, packages);
    if (!list.error.empty()) return failed(list.error + ".");
    auto package = std::find_if(packages.begin(), packages.end(), [&](const Package& p) { return p.id == "core-" + core; });
    if (package == packages.end()) return failed("libretro's list has no core called " + core + ".");
    auto installed = installer_.installed();
    auto current = installed.find(package->id);
    const bool updating = current != installed.end();
    if (updating && compareVersions(package->version, current->second.version) <= 0) {
        Outcome outcome;
        outcome.ok = true;
        outcome.message = package->name + " " + current->second.version + " is installed and up to date.";
        return outcome;
    }
    return installPackage(*package, updating, progress, stop);
}

Outcome Service::removeCore(const std::string& core, const Progress& progress, const std::atomic<bool>* stop)
{
    (void)stop;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isCoreName(core)) return failed("That isn't a RetroArch core.");
    installer_.recover();
    const std::string id = "core-" + core;
    Manifest manifest;
    std::string error;
    if (!readManifest(installer_.manifestPath(id), manifest, error) || manifest.removed) {
        return failed("That core isn't installed.");
    }
    report(progress, "Removing " + manifest.name + "...");
    Installer::Removal removal;
    if (!installer_.remove(id, removal, error)) return failed(error);
    Outcome outcome;
    outcome.ok = true;
    std::string launcher = removeEmulatorLauncher(root_, manifest);
    outcome.libraryChanged = launcher.find(" Removed launcher ") != std::string::npos;
    std::string message = "Removed " + manifest.name + "." + launcher;
    if (removal.restored > 0) {
        message += " Put back " + std::to_string(removal.restored) + (removal.restored == 1 ? " file" : " files") + " it had replaced.";
    }
    if (removal.kept > 0) {
        message += " Kept " + std::to_string(removal.kept) + (removal.kept == 1 ? " file" : " files") + " that changed since it was installed.";
    }
    LOG_NOTICE("Downloads", "Removed " + id);
    outcome.message = message;
    return outcome;
}

}

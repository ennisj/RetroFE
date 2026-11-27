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
#include "DownloadProvider.h"
#include "../../Database/Configuration.h"
#include "../../Downloads/Download.h"
#include "../../Downloads/Emulators.h"
#include "../../Library/Indexer.h"
#include "../../Utility/Log.h"
#include <algorithm>
#include <ctime>
#include <set>

namespace
{
    const char* const notAvailableText = "Emulator downloads aren't available on this system.";
    const char* const libretroServer = "libretro's server (buildbot.libretro.com)";
    const char* const busyText = "Wait for the current download to finish, or stop it.";

    std::string yesNo(bool value) { return value ? "yes" : "no"; }

    // "No Linux arm64 build" -> "no Linux arm64 build", after a comma
    std::string lowered(std::string text)
    {
        if (!text.empty() && text[0] == 'N') text[0] = 'n';
        return text;
    }

    bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

    bool isEmulatorScreen(const Manager::Screen& screen) { return screen.form == "download-emulator"; }
    bool isCoreScreen(const Manager::Screen& screen) { return screen.form == "download-core"; }
    bool isMine(const Manager::Screen& screen) { return screen.form.rfind("download-", 0) == 0; }
    bool isCoresScreen(const Manager::Screen& screen) { return screen.form == "download-cores" || isCoreScreen(screen); }

    // Where this platform has RetroArch's cores
    bool coresHere() { return Downloads::coresOffered(Downloads::platform()); }

    // "No Linux arm64 build"
    std::string noBuild() { return "No " + Downloads::platformName(Downloads::platform()) + " build"; }

    // What the cores screens say where there are none
    std::string noCoresText()
    {
        return "RetroArch has no " + Downloads::platformName(Downloads::platform()) + " build, so its cores aren't offered here.";
    }

    // emulators\DuckStation on Windows, emulators/DuckStation elsewhere
    std::string folderPath(const std::string& folder)
    {
        return (Downloads::platform() == Downloads::Platform::WindowsX64 ? "emulators\\" : "emulators/") + folder;
    }

    // What a job on this screen is about: the emulator's package id, the core, or "" for a list
    std::string targetOf(const Manager::Screen& screen)
    {
        return isEmulatorScreen(screen) ? screen.param("id") : isCoreScreen(screen) ? screen.param("core") : "";
    }

    // The package id a screen's messages are kept under
    std::string packageOf(const Manager::Screen& screen)
    {
        return isCoreScreen(screen) ? "core-" + screen.param("core") : screen.param("id");
    }

    // 14:05, local time
    std::string clockText(int64_t when)
    {
        std::time_t time = static_cast<std::time_t>(when);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        char text[16] = "";
        std::strftime(text, sizeof(text), "%H:%M", &local);
        return text;
    }

    // "Downloading 45 of 120 MB"
    std::string downloadText(uint64_t done, uint64_t total)
    {
        const uint64_t mb = 1024 * 1024;
        if (total >= mb) {
            return "Downloading " + std::to_string(done / mb) + " of " + std::to_string((total + mb / 2) / mb) + " MB";
        }
        if (total > 0) {
            return "Downloading " + Downloads::describeSize(done) + " of " + Downloads::describeSize(total);
        }
        return "Downloading " + Downloads::describeSize(done);
    }

    // Where an emulator's downloads come from, as the screens say it
    std::string sourceOf(const Downloads::Emulator& entry)
    {
        return entry.id == "retroarch" ? std::string(libretroServer) : "github.com/" + entry.repo;
    }
}

namespace Manager
{

DownloadProvider::DownloadProvider(const Services& services, Hooks hooks)
    : services_(services)
    , hooks_(std::move(hooks))
{
}

const std::set<std::string>& DownloadProvider::keys()
{
    static const std::set<std::string> served = {
        // values
        "supported", "unsupported", "running", "runningHere", "installingHere", "checkStatus", "hasWarnings",
        "retroArchInstalled", "needsRetroArch", "hasRetroArch", "name", "description", "folder", "folderPath", "system",
        "installed", "available", "size", "source", "launcher", "checks", "checksumNote", "progress", "message",
        "isInstalled", "canInstall", "canUpdate", "isRetroArchInstalled", "coreSystem", "coresStatus", "hasCoreWarnings", "coresReady",
        // lists, rows and options
        "warnings", "coreWarnings", "emulators", "retroArchLink", "cores", "coreSystems",
        // actions
        "check", "checkCores", "install", "update", "remove", "stop",
    };
    return served;
}

DownloadProvider::~DownloadProvider()
{
    stop_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

Downloads::Service& DownloadProvider::service()
{
    if (!service_) {
        std::unique_ptr<Downloads::Http> http = hooks_.http ? hooks_.http() : Downloads::createHttp();
        service_ = std::make_unique<Downloads::Service>(Configuration::absolutePath, std::move(http));
        if (hooks_.prepare) {
            hooks_.prepare(*service_);
        }
    }
    return *service_;
}

bool DownloadProvider::supported()
{
    return hooks_.supported ? *hooks_.supported : service().supported();
}

bool DownloadProvider::supportedOn(const Screen& screen)
{
    return supported() && (!isCoresScreen(screen) || coresHere());
}

std::string DownloadProvider::unsupportedText(const Screen& screen)
{
    if (!supported()) return notAvailableText;
    return isCoresScreen(screen) && !coresHere() ? noCoresText() : "";
}

bool DownloadProvider::isRunningHere(const Screen& screen) const
{
    return busy() && screen.form == jobForm_ && targetOf(screen) == jobTarget_;
}

// --- Jobs ------------------------------------------------------------------------------

void DownloadProvider::start(Job job, const std::string& target, const Screen& screen)
{
    // Callers only start when nothing runs; never replace a live thread (that would terminate)
    if (worker_.joinable()) {
        stop_ = true;
        collect();
    }
    Downloads::Service* downloads = &service();
    stop_ = false;
    finished_ = false;
    job_ = job;
    jobForm_ = screen.form;
    jobTarget_ = target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = (job == Job::Check || job == Job::FreshCheck) ? "Checking for releases..."
                  : job == Job::Cores ? "Reading libretro's core list..." : "Starting...";
        done_ = total_ = 0;
        if (job == Job::Install || job == Job::Remove) messages_.erase(target);
        if (job == Job::InstallCore || job == Job::RemoveCore) messages_.erase("core-" + target);
    }
    generation_++;
    worker_ = std::thread([this, downloads, job, target] {
        try {
            work(*downloads, job, target);
        }
        catch (const std::exception& e) {
            Downloads::Outcome outcome;
            outcome.message = std::string("Something went wrong: ") + e.what();
            LOG_ERROR("Downloads", outcome.message);
            finish(job == Job::InstallCore || job == Job::RemoveCore ? "core-" + target : target, outcome);
            if (job == Job::Cores) { std::lock_guard<std::mutex> lock(mutex_); coresRead_ = true; cores_.error = outcome.message; }
        }
        catch (...) {
            Downloads::Outcome outcome;
            outcome.message = "Something went wrong: unknown error";
            LOG_ERROR("Downloads", outcome.message);
            finish(job == Job::InstallCore || job == Job::RemoveCore ? "core-" + target : target, outcome);
            if (job == Job::Cores) { std::lock_guard<std::mutex> lock(mutex_); coresRead_ = true; cores_.error = outcome.message; }
        }
        finished_ = true;
        generation_++;
    });
}

void DownloadProvider::stopJob()
{
    if (!busy()) {
        return;
    }
    stop_ = true;
    generation_++;
}

void DownloadProvider::collect()
{
    if (worker_.joinable()) {
        worker_.join();
    }
    bool changed = false;
    std::string what;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed = libraryChanged_;
        what = changedWhat_;
        libraryChanged_ = false;
        progress_.clear();
    }
    job_ = Job::None;
    jobForm_.clear();
    jobTarget_.clear();
    stop_ = false;
    if (changed) {
        // As every section does after a write: launcher lists refresh, and
        // RetroFE reloads when Manage closes
        if (services_.library) {
            services_.library->changed(what);
        }
        else {
            services_.requestReload();
        }
    }
    generation_++;
}

void DownloadProvider::update(float dt)
{
    (void)dt;
    if (busy() && finished_) {
        collect();
    }
    if (!busy() && (coresOpen_ || coreOpen_) && !coresRead_ && retroArchInstalled()) {
        Screen screen = coreOpen_ ? coreScreen_ : Screen{};
        if (!coreOpen_) screen.form = "download-cores";
        start(Job::Cores, targetOf(screen), screen);
    }
}

void DownloadProvider::finish(const std::string& id, const Downloads::Outcome& outcome)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Message message;
    message.text = outcome.message;
    message.severity = outcome.ok ? Forms::Severity::Ok
                     : outcome.cancelled ? Forms::Severity::Warning : Forms::Severity::Error;
    messages_[id] = message;
    if (outcome.libraryChanged) {
        libraryChanged_ = true;
        changedWhat_ = "launchers";
    }
}

void DownloadProvider::work(Downloads::Service& downloads, Job job, const std::string& target)
{
    Downloads::Progress progress = [this](const std::string& text, uint64_t done, uint64_t total) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!text.empty()) progress_ = text;
        done_ = done;
        total_ = total;
        generation_++;
    };
    switch (job) {
    case Job::None:
        break;
    case Job::Check:
    case Job::FreshCheck: {
        std::vector<Downloads::Release> releases = downloads.check(progress, &stop_, job == Job::FreshCheck);
        std::vector<Downloads::EmulatorStatus> statuses = downloads.list();
        std::lock_guard<std::mutex> lock(mutex_);
        statuses_ = statuses;
        if (releases.size() == Downloads::emulators().size()) {
            releases_ = releases;
            checked_ = true;
        }
        break;
    }
    case Job::Install:
    case Job::Remove: {
        Downloads::Outcome outcome = job == Job::Install ? downloads.install(target, progress, &stop_)
                                                         : downloads.remove(target, progress, &stop_);
        finish(target, outcome);
        std::vector<Downloads::EmulatorStatus> statuses = downloads.list();
        std::lock_guard<std::mutex> lock(mutex_);
        statuses_ = statuses;
        // The release check stays; what's installed now decides what it offers
        for (Downloads::Release& offered : releases_) {
            const Downloads::EmulatorStatus* now = status(offered.id);
            offered.installedVersion = now ? now->installedVersion : "";
            offered.updateAvailable = !offered.installedVersion.empty() && !offered.version.empty()
                                   && Downloads::compareVersions(offered.version, offered.installedVersion) > 0;
        }
        break;
    }
    case Job::Cores: {
        Downloads::CoreList list = downloads.cores("", progress, &stop_);
        std::lock_guard<std::mutex> lock(mutex_);
        cores_ = list;
        coresRead_ = true;
        break;
    }
    case Job::InstallCore:
    case Job::RemoveCore: {
        Downloads::Outcome outcome = job == Job::InstallCore ? downloads.installCore(target, progress, &stop_)
                                                             : downloads.removeCore(target, progress, &stop_);
        finish("core-" + target, outcome);
        std::lock_guard<std::mutex> lock(mutex_);
        // Without asking libretro again: the core's version is what was installed
        for (Downloads::Core& listed : cores_.cores) {
            if (listed.core != target || !outcome.ok) continue;
            listed.installedVersion = job == Job::InstallCore ? listed.version : "";
            listed.updateAvailable = false;
        }
        break;
    }
    }
}

void DownloadProvider::listNow()
{
    // Only while no job runs, so the service is free and this doesn't wait
    if (busy()) {
        return;
    }
    std::vector<Downloads::EmulatorStatus> statuses = service().list();
    std::lock_guard<std::mutex> lock(mutex_);
    statuses_ = statuses;
}

// --- Screens -----------------------------------------------------------------------------

void DownloadProvider::enter(const Screen& screen)
{
    if (!isMine(screen) || !supportedOn(screen)) {
        return;
    }
    if (screen.form == "download-emulators") {
        listNow();
        if (!emulatorsOpen_) {
            // Opened (not come back to): versions are checked now, and only now
            emulatorsOpen_ = true;
            if (!busy()) start(Job::Check, "", screen);
        }
        generation_++;
        return;
    }
    bool listed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listed = !statuses_.empty();
    }
    if (!listed) listNow();
    bool hasRetroArch;
    bool coresRead;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hasRetroArch = retroArchInstalled();
        coresRead = coresRead_;
    }
    if (isCoreScreen(screen)) { coreOpen_ = true; coreScreen_ = screen; }
    if (screen.form == "download-cores" && !coresOpen_) {
        coresOpen_ = true;
        if (hasRetroArch && !busy()) start(Job::Cores, "", screen);
    }
    else if (isCoreScreen(screen) && hasRetroArch && !coresRead && !busy()) {
        start(Job::Cores, targetOf(screen), screen);   // opened by itself: it needs the list
    }
    generation_++;
}

std::string DownloadProvider::leaving(const Screen& screen)
{
    if (!isMine(screen)) {
        return "";
    }
    if (isRunningHere(screen)) {
        stopJob();
    }
    if (screen.form == "download-emulators") emulatorsOpen_ = false;
    if (screen.form == "download-cores") coresOpen_ = false;
    if (isCoreScreen(screen)) coreOpen_ = false;
    return "";
}

const Downloads::EmulatorStatus* DownloadProvider::status(const std::string& id) const
{
    for (const auto& entry : statuses_) if (entry.id == id) return &entry;
    return nullptr;
}

const Downloads::Release* DownloadProvider::release(const std::string& id) const
{
    for (const auto& entry : releases_) if (entry.id == id) return &entry;
    return nullptr;
}

const Downloads::Core* DownloadProvider::core(const std::string& name) const
{
    for (const auto& entry : cores_.cores) if (entry.core == name) return &entry;
    return nullptr;
}

bool DownloadProvider::retroArchInstalled() const
{
    // Only where it has cores to offer
    if (!coresHere()) return false;
    const Downloads::EmulatorStatus* retroArch = status("emulator-retroarch");
    return retroArch && !retroArch->installedVersion.empty();
}

std::string DownloadProvider::emulatorState(const Downloads::EmulatorStatus& entry, Forms::Severity& severity) const
{
    severity = Forms::Severity::None;
    if (!entry.available) {
        return entry.installedVersion.empty() ? noBuild() : "Installed " + entry.installedVersion + ", " + lowered(noBuild());
    }
    std::string text = entry.installedVersion.empty() ? "Not installed" : "Installed " + entry.installedVersion;
    const Downloads::Release* offered = release(entry.id);
    if (!offered) {
        return text;
    }
    if (!offered->version.empty()) {
        if (entry.installedVersion.empty() || offered->updateAvailable) {
            text += ", " + offered->version + " available";
        }
        return text;
    }
    if (!offered->error.empty()) {
        severity = Forms::Severity::Warning;
        text += offered->retryAt > 0 ? ", not checked (GitHub's limit)"
              : contains(offered->error, "checksum") ? ", no checked download"
              : contains(offered->error, "No release") ? ", no " + Downloads::osName(Downloads::platform()) + " release" : ", couldn't check";
    }
    return text;
}

std::string DownloadProvider::checkStatus(Forms::Severity& severity) const
{
    severity = Forms::Severity::None;
    if ((job_ == Job::Check || job_ == Job::FreshCheck)) {
        return stop_ ? "Stopping..." : "Checking for releases...";
    }
    if (!checked_ || releases_.empty()) {
        auto error = messages_.find("");
        if (error != messages_.end()) { severity = error->second.severity; return error->second.text; }
        return "";
    }
    severity = Forms::Severity::Warning;
    int64_t retryAt = 0;
    for (const auto& offered : releases_) retryAt = std::max(retryAt, offered.retryAt);
    if (retryAt > 0) {
        return "GitHub's limit of 60 requests an hour has been reached. Check again after " + clockText(retryAt) + ".";
    }
    bool any = std::any_of(releases_.begin(), releases_.end(), [](const Downloads::Release& r) { return !r.version.empty(); });
    if (!any) {
        return "Couldn't check for releases (" + releases_.front().error
             + "). RetroFE may be offline: check the connection, then Check again.";
    }
    severity = Forms::Severity::None;
    int64_t checked = 0;
    for (const auto& offered : releases_) {
        if (offered.checked > 0 && (checked == 0 || offered.checked < checked)) checked = offered.checked;
    }
    return checked > 0 ? "Versions checked at " + clockText(checked) + "." : "";
}

std::vector<std::string> DownloadProvider::warnings() const
{
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const auto& offered : releases_) {
        for (const auto& warning : offered.warnings) {
            if (seen.insert(warning).second) result.push_back(warning);
        }
    }
    return result;
}

std::string DownloadProvider::progressText() const
{
    if (stop_) return "Stopping...";
    if (contains(progress_, "Downloading") && (done_ > 0 || total_ > 0)) {
        return downloadText(done_, total_);
    }
    return progress_;
}

std::string DownloadProvider::emulatorValue(const std::string& key, const Screen& screen) const
{
    const std::string id = screen.param("id");
    const Downloads::Emulator* entry = Downloads::findEmulator(id);
    const Downloads::EmulatorStatus* entryStatus = status(id);
    const Downloads::Release* offered = release(id);
    const bool installed = entryStatus && !entryStatus->installedVersion.empty();
    const bool retroArch = id == "emulator-retroarch";
    if (key == "name") return entry ? entry->name : "Emulator";
    if (!entry) return key == "description" ? "That isn't an emulator RetroFE can download." : "";
    if (key == "description") return entry->description;
    if (key == "folder") return entry->folder;
    if (key == "folderPath") return folderPath(entry->folder);
    if (key == "installed") return installed ? entryStatus->installedVersion : "Not installed";
    if (key == "isInstalled") return yesNo(installed);
    // No build for this platform: nothing to install or update
    if (!entry->available()) {
        if (key == "canInstall" || key == "canUpdate" || key == "isRetroArchInstalled") return "no";
        if (key == "available") return noBuild();
        if (key == "size" || key == "checksumNote" || (key == "launcher" && !installed)) return "";
    }
    if (key == "canInstall") return yesNo(!installed);
    if (key == "canUpdate") return yesNo(installed && offered && offered->updateAvailable);
    if (key == "available") {
        if (offered && !offered->version.empty()) {
            return offered->version + (offered->fromCache ? " (the last copy RetroFE found)" : "");
        }
        if (offered && !offered->error.empty()) return offered->error;
        return (job_ == Job::Check || job_ == Job::FreshCheck) ? "Checking..." : "";
    }
    if (key == "size") return offered && offered->size > 0 ? Downloads::describeSize(offered->size) : "";
    if (key == "source") return sourceOf(*entry);
    if (key == "launcher") {
        if (retroArch) return "One per core, made when you install the core";
        if (installed) return entryStatus->launcherCreated;
        return entry->launcher.empty() ? "" : entry->launcher + ", made when it's installed";
    }
    if (key == "checks") {
        return retroArch ? "libretro doesn't publish a checksum for this download, so RetroFE can't check it."
                         : "RetroFE checks the published checksum, but not what it contains.";
    }
    if (key == "checksumNote") return retroArch ? "libretro doesn't publish a checksum for this download." : "";
    if (key == "isRetroArchInstalled") return yesNo(retroArch && installed && coresHere());
    return "";
}

std::string DownloadProvider::coreValue(const std::string& key, const Screen& screen) const
{
    const std::string name = screen.param("core");
    const Downloads::Core* listed = core(name);
    const bool installed = listed && !listed->installedVersion.empty();
    if (key == "name") return listed ? listed->name : name;
    if (key == "description") {
        if (!retroArchInstalled()) return "Install RetroArch first.";
        if (!listed) return coresRead_ ? "libretro's list has no core called " + name + "." : "";
        return listed->description;
    }
    if (!listed) return key == "canInstall" || key == "canUpdate" || key == "isInstalled" ? "no" : "";
    if (key == "system") return listed->system;
    if (key == "installed") return installed ? listed->installedVersion : "Not installed";
    if (key == "available") return listed->version;
    if (key == "isInstalled") return yesNo(installed);
    if (key == "canInstall") return yesNo(!installed);
    if (key == "canUpdate") return yesNo(installed && listed->updateAvailable);
    if (key == "source") return libretroServer;
    if (key == "launcher") return Downloads::coreLauncherName(listed->name) + (installed ? "" : ", made when it's installed");
    if (key == "checks") return "RetroFE checks the CRC-32 libretro publishes, but not what it contains.";
    return "";
}

std::string DownloadProvider::get(const std::string& key, const Screen& screen)
{
    if (key == "supported") return yesNo(supportedOn(screen));
    if (key == "unsupported") return unsupportedText(screen);
    if (key == "running") return yesNo(busy());
    if (key == "runningHere") return yesNo(isRunningHere(screen));
    if (key == "installingHere") return yesNo(isRunningHere(screen) && (job_ == Job::Install || job_ == Job::InstallCore));
    if (key == "coreSystem") return coreSystem_;
    if (!supportedOn(screen) && (key == "canInstall" || key == "canUpdate" || key == "isInstalled" || key == "isRetroArchInstalled")) {
        return "no";
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (key == "checkStatus") {
        Forms::Severity severity;
        return checkStatus(severity);
    }
    if (key == "hasWarnings") return yesNo(!warnings().empty());
    if (key == "retroArchInstalled") return yesNo(supported() && retroArchInstalled());
    if (key == "needsRetroArch") return yesNo(supported() && coresHere() && !retroArchInstalled());
    if (key == "hasRetroArch") return yesNo(supported() && retroArchInstalled());
    if (key == "progress") return isRunningHere(screen) ? progressText() : "";
    if (key == "message") {
        auto found = messages_.find(packageOf(screen));
        return found != messages_.end() && !isRunningHere(screen) ? found->second.text : "";
    }
    if (key == "coresStatus") {
        if (stop_ && busy()) return "Stopping...";
        if (job_ == Job::Cores) return progressText();
        if (!coresRead_ && busy()) return "Waiting for the release check...";
        if (!coresRead_) {
            auto error = messages_.find("");
            return error != messages_.end() ? error->second.text : "";
        }
        if (cores_.error.empty() || cores_.error == "Install RetroArch first") return "";
        return cores_.error + ".";
    }
    if (key == "hasCoreWarnings") return yesNo(!cores_.warnings.empty());
    if (key == "coresReady") return yesNo(retroArchInstalled() && coresRead_);
    if (isEmulatorScreen(screen)) return emulatorValue(key, screen);
    if (isCoreScreen(screen)) return coreValue(key, screen);
    return "";
}

Forms::Severity DownloadProvider::severity(const std::string& key, const Screen& screen)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (key == "checkStatus") {
        Forms::Severity severity;
        checkStatus(severity);
        return severity;
    }
    if (key == "message") {
        auto found = messages_.find(packageOf(screen));
        return found != messages_.end() ? found->second.severity : Forms::Severity::None;
    }
    if (key == "coresStatus") {
        return job_ != Job::Cores && coresRead_ && !cores_.error.empty() ? Forms::Severity::Warning : Forms::Severity::None;
    }
    if (key == "available" && isEmulatorScreen(screen)) {
        const Downloads::Release* offered = release(screen.param("id"));
        return offered && offered->available && offered->version.empty() && !offered->error.empty() ? Forms::Severity::Warning : Forms::Severity::None;
    }
    return Forms::Severity::None;
}

void DownloadProvider::set(const std::string& key, const std::string& value, const Screen& screen)
{
    (void)screen;
    if (key == "coreSystem") {
        coreSystem_ = value.empty() ? "all" : value;
        generation_++;
    }
}

std::vector<std::string> DownloadProvider::list(const std::string& key, const Screen& screen)
{
    (void)screen;
    std::lock_guard<std::mutex> lock(mutex_);
    if (key == "warnings") return warnings();
    if (key == "coreWarnings") return cores_.warnings;
    return {};
}

Forms::Rows DownloadProvider::rows(const std::string& key, size_t limit, const Screen& screen)
{
    (void)screen;
    Forms::Rows result;
    if (!supportedOn(screen)) {
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (key == "emulators") {
        for (const auto& entry : statuses_) {
            Forms::Row row;
            row.id = entry.id;
            row.label = entry.name;
            row.detail = emulatorState(entry, row.severity);
            result.items.push_back(row);
        }
    }
    else if (key == "retroArchLink" && !retroArchInstalled()) {
        Forms::Row row;
        row.id = "emulator-retroarch";
        row.label = "RetroArch";
        row.link = true;
        if (const Downloads::EmulatorStatus* retroArch = status("emulator-retroarch")) {
            row.detail = emulatorState(*retroArch, row.severity);
        }
        result.items.push_back(row);
    }
    else if (key == "cores" && retroArchInstalled()) {
        for (const auto& listed : cores_.cores) {
            if (coreSystem_ != "all" && listed.system != coreSystem_) continue;
            Forms::Row row;
            row.id = listed.core;
            row.label = listed.name;
            row.detail = listed.installedVersion.empty() ? "Not installed"
                       : "Installed " + listed.installedVersion + (listed.updateAvailable ? ", " + listed.version + " available" : "");
            result.items.push_back(row);
        }
    }
    result.total = result.items.size();
    if (result.items.size() > limit) {
        result.items.resize(limit);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> DownloadProvider::options(const std::string& key, const Screen& screen)
{
    (void)screen;
    std::vector<std::pair<std::string, std::string>> result;
    if (key != "coreSystems") {
        return result;
    }
    result.emplace_back("all", "All systems");
    std::set<std::string> systems;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& listed : cores_.cores) {
            if (!listed.system.empty()) systems.insert(listed.system);
        }
    }
    for (const std::string& system : systems) {
        result.emplace_back(system, system);
    }
    return result;
}

// --- Actions -----------------------------------------------------------------------------

std::string DownloadProvider::actionProblem(const std::string& action, const Screen& screen)
{
    if (!supportedOn(screen)) {
        return unsupportedText(screen);
    }
    if (action == "stop") {
        return busy() ? "" : "Nothing is running.";
    }
    if (action != "check" && action != "checkCores" && action != "install" && action != "update" && action != "remove") {
        return Provider::actionProblem(action, screen);
    }
    if (busy()) {
        if (stop_) return "Stopping...";
        return (job_ == Job::Check || job_ == Job::FreshCheck) ? "Still checking for releases..."
             : job_ == Job::Cores ? "Still reading libretro's core list..." : busyText;
    }
    if (action == "check") {
        return screen.form == "download-emulators" ? "" : "Open Download an emulator to check.";
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (action == "checkCores") {
        if (!coresHere()) return noCoresText();
        return retroArchInstalled() ? "" : "Install RetroArch first.";
    }
    if (isEmulatorScreen(screen)) {
        const std::string id = screen.param("id");
        const Downloads::Emulator* known = Downloads::findEmulator(id);
        if (!known) return "That isn't an emulator RetroFE can download.";
        const Downloads::EmulatorStatus* entry = status(id);
        const bool installed = entry && !entry->installedVersion.empty();
        if (action != "remove" && !known->available()) return noBuild() + " of " + known->name + ".";
        if (action == "install") return installed ? "It's installed already." : "";
        if (!installed) return "It isn't installed.";
        if (action == "update") {
            const Downloads::Release* offered = release(id);
            return offered && !offered->version.empty() && !offered->updateAvailable ? "It's up to date." : "";
        }
        return "";
    }
    if (isCoreScreen(screen)) {
        if (!coresHere()) return noCoresText();
        if (!retroArchInstalled()) return "Install RetroArch first.";
        const Downloads::Core* listed = core(screen.param("core"));
        if (!listed) return coresRead_ ? "libretro's list has no core called " + screen.param("core") + "." : "Still reading libretro's core list...";
        const bool installed = !listed->installedVersion.empty();
        if (action == "install") return installed ? "It's installed already." : "";
        if (!installed) return "It isn't installed.";
        if (action == "update") return listed->updateAvailable ? "" : "It's up to date.";
        return "";
    }
    return "Open an emulator or a core first.";
}

Forms::Model::Result DownloadProvider::runAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) {
        return result;
    }
    result.ok = true;
    if (action == "stop") {
        stopJob();
        result.info = true;
        result.message = "Stopping...";
    }
    else if (action == "check") {
        start(Job::FreshCheck, "", screen);
    }
    else if (action == "checkCores") {
        start(Job::Cores, "", screen);
    }
    else if (isEmulatorScreen(screen)) {
        start(action == "remove" ? Job::Remove : Job::Install, screen.param("id"), screen);
    }
    else {
        start(action == "remove" ? Job::RemoveCore : Job::InstallCore, screen.param("core"), screen);
    }
    return result;
}

}

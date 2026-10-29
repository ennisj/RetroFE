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
#include "Emulators.h"
#include "Download.h"
#include "GitHub.h"
#include "Index.h"
#include "../Utility/Log.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace
{
    // Artwork is small; anything bigger isn't kept
    const uint64_t maxArtworkSize = 5ull * 1024 * 1024;

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // The image extension artwork is published with, or "" if it isn't an image
    std::string imageExtension(const std::string& location)
    {
        std::string path = location.substr(0, location.find_first_of("?#"));
        std::string extension = lower(fs::path(path).extension().string());
        static const std::set<std::string> images = { ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp" };
        return images.count(extension) ? extension : "";
    }
}

namespace Catalog
{
    // The collections/ folders a package's files are in
    std::set<std::string> collectionsOf(const Manifest& manifest)
    {
        std::set<std::string> names;
        for (const InstalledFile& file : manifest.files) {
            size_t slash = file.path.find('/');
            if (file.target == "collections" && slash != std::string::npos && slash > 0) {
                names.insert(file.path.substr(0, slash));
            }
        }
        return names;
    }

    Service::Service(const std::string& catalogFolder, const Targets& targets, std::unique_ptr<Http> http)
        : folder_(catalogFolder)
        , targets_(targets)
        , http_(std::move(http))
        , installer_(catalogFolder, targets)
    {
        // Leaving RetroFE stops an install that hasn't started moving files
        installer_.setStopFlag(&stopping_);
    }

    Service::~Service()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            cancelRequested_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void Service::setApp(const AppInstall& app)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        app_ = app;
    }

    AppInstall Service::app() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return app_;
    }

    void Service::start()
    {
        if (worker_.joinable()) {
            return;
        }
        std::vector<SourceConfig> sources;
        std::string error;
        if (!loadSources((fs::path(folder_) / "sources.conf").string(), sources, error)) {
            LOG_ERROR("Catalog", error);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            installed_ = installer_.installed();
            sources_ = sources;
            rebuildEntries();
        }
        worker_ = std::thread(&Service::run, this);
        enqueue({ Job::Kind::Recover, "" });
        if (!sources.empty()) {
            refresh();
        }
    }

    void Service::rebuildEntries()
    {
        entries_.clear();
        for (const Package& package : packages_) {
            Entry entry;
            entry.package = package;
            auto installed = installed_.find(package.id);
            if (package.kind == "app") {
                // RetroFE itself: what's installed is what's running. Any GitHub
                // source may update it, since it can't be removed to switch.
                entry.installedVersion = !app_.version.empty() ? app_.version
                                       : installed != installed_.end() ? installed->second.version : "";
                entry.installedSource = installed != installed_.end() ? installed->second.source : "";
                entry.updateAvailable = !entry.installedVersion.empty() && compareVersions(package.version, entry.installedVersion) > 0;
            }
            else if (installed != installed_.end()) {
                entry.installedVersion = installed->second.version;
                entry.installedSource = installed->second.source;
                // Only the source it came from can update it
                bool sameSource = entry.installedSource.empty() || entry.installedSource == package.source;
                entry.updateAvailable = sameSource && compareVersions(package.version, entry.installedVersion) > 0;
            }
            entries_.push_back(std::move(entry));
        }
        std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            if (a.package.kind != b.package.kind) return a.package.kind < b.package.kind;
            return lower(a.package.name) < lower(b.package.name);
        });
        entryIndex_.clear();
        for (size_t i = 0; i < entries_.size(); ++i) {
            entryIndex_[entries_[i].package.id] = i;
        }
    }

    std::vector<Entry> Service::entries() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_;
    }

    bool Service::entry(const std::string& id, Entry& entry) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entryIndex_.find(id);
        if (it == entryIndex_.end()) return false;
        entry = entries_[it->second];
        return true;
    }

    size_t Service::packageCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    bool Service::isBusy() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_ || std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) { return job.isPackageJob(); });
    }

    std::vector<Manifest> Service::installed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Manifest> result;
        for (const auto& [id, manifest] : installed_) {
            result.push_back(manifest);
        }
        std::sort(result.begin(), result.end(), [](const Manifest& a, const Manifest& b) { return lower(a.name) < lower(b.name); });
        return result;
    }

    std::vector<SourceConfig> Service::sources() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sources_;
    }

    std::vector<std::string> Service::warnings() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> all = appWarnings_;
        all.insert(all.end(), warnings_.begin(), warnings_.end());
        return all;
    }

    bool Service::refreshing() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (refreshingNow_) return true;
        return std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) { return job.kind == Job::Kind::Refresh; });
    }

    std::string Service::artworkFolder() const
    {
        return (fs::path(folder_) / "artwork").string();
    }

    std::string Service::cacheFolder() const
    {
        return (fs::path(folder_) / "cache").string();
    }

    std::vector<std::string> Service::artworkTypes() const
    {
        std::vector<std::string> types;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(artworkFolder(), ec)) {
            if (entry.is_directory()) {
                types.push_back(entry.path().filename().string());
            }
        }
        return types;
    }

    bool Service::activity(Activity& activity) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return false;
        activity = activity_;
        return true;
    }

    bool Service::isQueued(const std::string& id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ && activity_.id == id) return true;
        return std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.isPackageJob() && job.id == id; });
    }

    bool Service::outcome(const std::string& id, bool& ok, std::string& message) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = outcomes_.find(id);
        if (it == outcomes_.end()) return false;
        ok = it->second.first;
        message = it->second.second;
        return true;
    }

    void Service::enqueue(const Job& job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (job.isPackageJob()) {
                if ((active_ && activity_.id == job.id)
                    || std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.isPackageJob() && j.id == job.id; })) {
                    return;
                }
                outcomes_.erase(job.id);
                // Installs go ahead of artwork, which can wait
                auto artwork = std::find_if(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.kind == Job::Kind::Artwork; });
                jobs_.insert(artwork, job);
            }
            else {
                // One waiting refresh or artwork job is enough
                if (std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.kind == job.kind; })) return;
                jobs_.push_back(job);
            }
        }
        wake_.notify_all();
    }

    void Service::refresh() { enqueue({ Job::Kind::Refresh, "" }); }
    void Service::install(const std::string& id) { enqueue({ Job::Kind::Install, id }); }
    void Service::remove(const std::string& id) { enqueue({ Job::Kind::Remove, id }); }

    void Service::cancel(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ && activity_.id == id) {
            cancelRequested_ = true;
            return;
        }
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.isPackageJob() && job.id == id; }),
                    jobs_.end());
    }

    bool Service::addSource(const SourceConfig& source, std::string& error)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            error = validateSource(source, sources_);
            if (!error.empty()) return false;
            std::vector<SourceConfig> sources = sources_;
            sources.push_back(source);
            if (!saveSources((fs::path(folder_) / "sources.conf").string(), sources, error)) return false;
            sources_ = sources;
        }
        // A cached index from an earlier source of the same name isn't this one's
        std::error_code ec;
        fs::remove_all(fs::path(cacheFolder()) / source.name, ec);
        LOG_NOTICE("Catalog", "Added source " + source.name);
        refresh();
        return true;
    }

    bool Service::removeSource(const std::string& name, std::string& error)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<SourceConfig> sources = sources_;
            auto it = std::find_if(sources.begin(), sources.end(), [&](const SourceConfig& s) { return s.name == name; });
            if (it == sources.end()) {
                error = "There's no source called " + name;
                return false;
            }
            sources.erase(it);
            if (!saveSources((fs::path(folder_) / "sources.conf").string(), sources, error)) return false;
            sources_ = sources;
        }
        std::error_code ec;
        fs::remove_all(fs::path(cacheFolder()) / name, ec);
        LOG_NOTICE("Catalog", "Removed source " + name);
        refresh();
        return true;
    }

    bool Service::takeChanges(Changes& changes)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!changed_ || active_ || std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) { return job.isPackageJob(); })) {
            return false;
        }
        changes = changes_;
        changes_ = Changes();
        changed_ = false;
        return true;
    }

    bool Service::hasChanges() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return changed_;
    }

    bool Service::sourceStatus(const std::string& name, SourceStatus& status) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = sourceStatus_.find(name);
        if (found == sourceStatus_.end()) return false;
        status = found->second;
        return true;
    }

    bool Service::takeRestartRequest()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!restartRequested_ || active_ || std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) { return job.isPackageJob(); })) {
            return false;
        }
        restartRequested_ = false;
        return true;
    }

    void Service::waitUntilIdle()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [&] { return stopping_ || (jobs_.empty() && !working_); });
    }

    void Service::run()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            wake_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
            if (stopping_) break;
            Job job = jobs_.front();
            jobs_.pop_front();
            working_ = true;
            refreshingNow_ = job.kind == Job::Kind::Refresh;
            // Shown as active from the moment it's taken, so a Stop can't fall between
            if (job.isPackageJob()) {
                activity_ = Activity();
                activity_.id = job.id;
                activity_.name = job.id;
                auto known = entryIndex_.find(job.id);
                if (known != entryIndex_.end()) activity_.name = entries_[known->second].package.name;
                activity_.removing = job.kind == Job::Kind::Remove;
                active_ = true;
                cancelRequested_ = false;
            }
            lock.unlock();

            // Nothing a source or a damaged file holds may take RetroFE down
            std::string failure;
            try {
                switch (job.kind) {
                case Job::Kind::Recover: doRecover(); break;
                case Job::Kind::Refresh: doRefresh(); break;
                case Job::Kind::Install: doInstall(job.id); break;
                case Job::Kind::Remove:  doRemove(job.id); break;
                case Job::Kind::Artwork: doArtwork(); break;
                }
            }
            catch (const std::exception& e) {
                failure = e.what();
            }
            catch (...) {
                failure = "unknown error";
            }
            if (!failure.empty()) {
                LOG_ERROR("Catalog", "A Catalog job failed: " + failure);
                if (job.isPackageJob()) {
                    finish(job.id, false, "Something went wrong: " + failure);
                }
                else if (job.kind == Job::Kind::Refresh) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    warnings_.push_back("Reading the sources failed: " + failure);
                }
            }

            lock.lock();
            working_ = false;
            refreshingNow_ = false;
            if (jobs_.empty()) {
                idle_.notify_all();
            }
        }
        working_ = false;
        idle_.notify_all();
    }

    void Service::doRecover()
    {
        installer_.recover();
        AppInstall app;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            app = app_;
        }
        if (!app.program.empty()) {
            // Undoing an unfinished update of RetroFE needs a restart to run the version put back
            SelfUpdater updater(folder_, app);
            if (updater.recover()) {
                std::lock_guard<std::mutex> lock(mutex_);
                restartRequested_ = true;
            }
            // An update recovery gave up on is shown with the Catalog's problems rather than blocking the start
            std::ifstream damaged(fs::u8path(app.program + ".update.damaged.txt"), std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(damaged)), std::istreambuf_iterator<char>());
            std::lock_guard<std::mutex> lock(mutex_);
            appWarnings_.clear();
            if (!text.empty()) appWarnings_.push_back("RetroFE update: " + text.substr(0, 2000));
        }
        std::lock_guard<std::mutex> lock(mutex_);
        installed_ = installer_.installed();
        rebuildEntries();
    }

    void Service::doRefresh()
    {
        std::vector<SourceConfig> sources = this->sources();
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        std::set<std::string> ids;
        std::map<std::string, SourceStatus> statuses;

        for (const SourceConfig& source : sources) {
            if (!source.enabled) continue;
            SourceContents contents = readSource(source, cacheFolder(), http_.get());
            SourceStatus& status = statuses[source.name];
            status.checked = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            status.fromCache = contents.fromCache;
            status.error = contents.error;
            if (!contents.error.empty()) {
                warnings.push_back(source.name + ": " + contents.error);
                LOG_WARNING("Catalog", source.name + ": " + contents.error);
            }
            for (const std::string& warning : contents.warnings) {
                warnings.push_back(source.name + ": " + warning);
            }
            for (Package& package : contents.packages) {
                if (!ids.insert(package.id).second) {
                    warnings.push_back(source.name + ": " + package.id + " is also in an earlier source, which is used instead");
                    continue;
                }
                packages.push_back(std::move(package));
            }
        }
        LOG_INFO("Catalog", std::to_string(packages.size()) + " packages from " + std::to_string(sources.size()) + " sources");

        {
            std::lock_guard<std::mutex> lock(mutex_);
            packages_ = packages;
            warnings_ = warnings;
            sourceStatus_ = statuses;
            rebuildEntries();
        }
        enqueue({ Job::Kind::Artwork, "" });
    }

    void Service::doArtwork()
    {
        std::vector<Package> packages;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            packages = packages_;
        }
        for (const Package& package : packages) {
            // Give way to installs; carry on afterwards
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return;
                if (std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) { return job.isPackageJob(); })) {
                    jobs_.push_back({ Job::Kind::Artwork, "" });
                    return;
                }
            }
            for (const auto& [type, location] : package.artwork) {
                std::string extension = imageExtension(location);
                if (!isValidId(type) || extension.empty() || !isAllowedLocation(location)) continue;
                fs::path folder = fs::path(artworkFolder()) / type;
                fs::path file = folder / (package.id + extension);
                std::error_code ec;
                if (fs::exists(file, ec)) continue;
                fs::create_directories(folder, ec);

                fs::path part = file;
                part += ".part";
                bool ok = false;
                if (isUrl(location)) {
                    Http::Transfer transfer;
                    ok = http_ && http_->download(location, part.string(), 0, [&](uint64_t received, uint64_t total) {
                        return !stopping_ && received <= maxArtworkSize && total <= maxArtworkSize;
                    }, transfer);
                }
                else if (fs::file_size(location, ec) <= maxArtworkSize && !ec) {
                    ok = fs::copy_file(location, part, fs::copy_options::overwrite_existing, ec);
                }
                if (ok) {
                    fs::rename(part, file, ec);
                }
                else {
                    fs::remove(part, ec);
                    LOG_INFO("Catalog", "Could not get " + type + " artwork for " + package.id);
                }
            }
        }
    }

    void Service::doInstall(const std::string& id)
    {
        Entry entry;
        if (!this->entry(id, entry)) {
            finish(id, false, "This package isn't offered by any source any more.");
            return;
        }
        const Package& package = entry.package;
        if (package.kind == "app" || package.id == appPackageId) {
            doUpdateApp(package);
            return;
        }

        Manifest previous;
        bool updating = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto installed = installed_.find(id);
            if (installed != installed_.end()) {
                previous = installed->second;
                updating = true;
            }
        }
        // Another source offering the same id doesn't get to replace it
        if (updating && !previous.source.empty() && previous.source != package.source) {
            finish(id, false, "This was installed from " + previous.source + ", and " + package.source
                              + " offers it too. Remove it first to install it from " + package.source + ".");
            return;
        }

        // Requirements aren't installed automatically yet
        for (const Requirement& requirement : package.requirements) {
            bool have = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto installed = installed_.find(requirement.id);
                have = installed != installed_.end() && satisfies(installed->second.version, requirement.version);
            }
            if (!have) {
                Entry needed;
                std::string name = this->entry(requirement.id, needed) ? needed.package.name : requirement.id;
                finish(id, false, "Install " + name + (requirement.version.empty() ? "" : " " + requirement.version) + " first.");
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            activity_.name = package.name;
            activity_.total = package.totalSize();
        }
        LOG_NOTICE("Catalog", "Installing " + package.id + " " + package.version);

        Downloader downloader(http_.get(), (fs::path(folder_) / "downloads").string());
        Downloader::Result result;
        bool fetched = downloader.fetch(package, [&](uint64_t received, uint64_t total) {
            std::lock_guard<std::mutex> lock(mutex_);
            activity_.received = received;
            if (total) activity_.total = total;
            return !cancelRequested_ && !stopping_;
        }, result);

        if (!fetched) {
            LOG_WARNING("Catalog", package.id + ": " + result.error);
            finish(id, false, result.cancelled ? "Stopped. Install again to continue where it left off." : result.error);
            return;
        }

        std::string error;
        if (!installer_.install(package, result.files, error)) {
            LOG_ERROR("Catalog", package.id + ": " + error);
            finish(id, false, error);
            return;
        }
        downloader.discard(package.id);
        std::string launcherMessage;
        if (!updating && package.kind == "emulator") {
            bool trusted = false;
            for (const auto& source : sources()) if (source.name == package.source && source.type == "emulators") trusted = true;
            Manifest manifest;
            if (trusted && readManifest((fs::path(folder_) / "installed" / (package.id + ".json")).string(), manifest, error)) {
                launcherMessage = createEmulatorLauncher(targets_.root, folder_, manifest);
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            installed_ = installer_.installed();
            rebuildEntries();
            changed_ = true;
            std::set<std::string> now;
            auto manifest = installed_.find(package.id);
            if (manifest != installed_.end()) {
                now = collectionsOf(manifest->second);
            }
            for (const std::string& name : now) {
                changes_.addedCollections.insert(name);
                changes_.removedCollections.erase(name);
            }
            // An update that moved to another collection folder leaves the old one
            for (const std::string& name : collectionsOf(previous)) {
                std::error_code ec;
                if (!now.count(name) && !fs::exists(fs::path(targets_.collections) / fs::u8path(name), ec)) {
                    changes_.removedCollections.insert(name);
                    changes_.addedCollections.erase(name);
                }
            }
        }
        LOG_NOTICE("Catalog", "Installed " + package.id + " " + package.version);
        finish(id, true, (updating ? "Updated to " : "Installed ") + package.name + " " + package.version + "." + launcherMessage);
    }

    void Service::doUpdateApp(const Package& original)
    {
        Package package = original;
        // Only a GitHub source the user added may replace RetroFE's program: its
        // official repository, or one the user allowed with app = yes
        AppInstall app;
        const SourceConfig* source = nullptr;
        SourceConfig config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            app = app_;
            for (const SourceConfig& s : sources_) {
                if (s.name == package.source && s.type == "github") {
                    config = s;
                    source = &config;
                }
            }
        }
        if (package.kind != "app" || package.id != appPackageId || !source) {
            finish(package.id, false, "Updates of RetroFE itself only come from GitHub sources.");
            return;
        }
        if (!isOfficialRepository(source->repo) && !source->app) {
            finish(package.id, false, source->repo + " isn't RetroFE's own repository, and its source doesn't say app = yes.");
            return;
        }
        // Never back to the same or an older version
        if (!app.version.empty() && compareVersions(package.version, app.version) <= 0) {
            finish(package.id, false, "RetroFE " + app.version + " is already this version or newer.");
            return;
        }
        SelfUpdater updater(folder_, app);
        std::string problem = updater.problem();
        if (!problem.empty()) {
            finish(package.id, false, problem);
            return;
        }
        if (package.files.size() != 1) {
            finish(package.id, false, "An update of RetroFE is a single archive.");
            return;
        }

        // A program is only installed when it can be checked
        PackageFile& file = package.files[0];
        if (file.sha256.empty() && !file.sha256Url.empty() && http_ && isAllowedLocation(file.sha256Url)) {
            std::string text;
            std::string error;
            if (http_->get(file.sha256Url, text, error)) {
                std::string name = file.url.substr(0, file.url.find_first_of("?#"));
                file.sha256 = parseSha256File(text.substr(0, 65536), name.substr(name.rfind('/') + 1));
            }
            else {
                LOG_WARNING("Catalog", "Could not read " + file.sha256Url + ": " + error);
            }
        }
        if (file.sha256.empty()) {
            finish(package.id, false, "This release has no checksum, so RetroFE won't install it.");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            activity_.name = package.name;
            activity_.total = package.totalSize();
        }
        LOG_NOTICE("Catalog", "Updating RetroFE to " + package.version + " from " + package.source);
        Downloader downloader(http_.get(), (fs::path(folder_) / "downloads").string());
        Downloader::Result result;
        bool fetched = downloader.fetch(package, [&](uint64_t received, uint64_t total) {
            std::lock_guard<std::mutex> lock(mutex_);
            activity_.received = received;
            if (total) activity_.total = total;
            return !cancelRequested_ && !stopping_;
        }, result);
        if (!fetched) {
            LOG_WARNING("Catalog", package.id + ": " + result.error);
            finish(package.id, false, result.cancelled ? "Stopped. Install again to continue where it left off." : result.error);
            return;
        }

        // Stop and leaving RetroFE give up until files start moving
        updater.shouldStop = [this] { return stopping_ || cancelRequested_; };
        std::string error;
        if (!updater.apply(package, result.files[0], error)) {
            LOG_ERROR("Catalog", "RetroFE update: " + error);
            finish(package.id, false, error == "Stopped" ? "Stopped. Install again to continue." : error);
            // Both folder renames kept failing, leaving no program folder: keep trying while RetroFE runs
            // (main() tries once more as RetroFE exits, and again when it starts)
            while (updater.programFolderMissing() && !stopping_) {
                for (int i = 0; i < 10 && !stopping_; i++) std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (stopping_) break;
                if (updater.repairProgramFolder()) {
                    LOG_NOTICE("Catalog", (updater.finishedForward() ? "RetroFE's new program folder is in place after all"
                                                                      : "RetroFE's program folder is back in place"));
                    std::lock_guard<std::mutex> lock(mutex_);
                    installed_ = installer_.installed();
                    if (updater.finishedForward()) {
                        app_.version = package.version;
                        restartRequested_ = true;
                    }
                    rebuildEntries();
                }
            }
            return;
        }
        downloader.discard(package.id);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            installed_ = installer_.installed();
            // What's in place now, so the same update isn't offered again before the restart
            app_.version = package.version;
            appWarnings_.clear();
            rebuildEntries();
            restartRequested_ = true;
        }
        finish(package.id, true, "Updated RetroFE to " + package.version + ". Restarting...");
    }

    void Service::doRemove(const std::string& id)
    {
        std::string name = id;
        std::set<std::string> collections;
        bool isApp = id == appPackageId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto installed = installed_.find(id);
            if (installed != installed_.end()) {
                if (!installed->second.name.empty()) name = installed->second.name;
                collections = collectionsOf(installed->second);
                isApp = isApp || installed->second.kind == "app";
            }
            activity_.name = name;
        }
        // Its files are the running program
        if (isApp) {
            finish(id, false, "RetroFE can't remove itself.");
            return;
        }
        Manifest manifest;
        std::string ignored;
        readManifest((fs::path(folder_) / "installed" / (id + ".json")).string(), manifest, ignored);
        Installer::Removal removal;
        std::string error;
        if (!installer_.remove(id, removal, error)) {
            finish(id, false, error);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            installed_ = installer_.installed();
            rebuildEntries();
            changed_ = true;
            // Only collections that are gone; kept files keep the collection
            for (const std::string& collection : collections) {
                std::error_code ec;
                if (!fs::exists(fs::path(targets_.collections) / fs::u8path(collection), ec)) {
                    changes_.removedCollections.insert(collection);
                    changes_.addedCollections.erase(collection);
                }
            }
        }
        LOG_NOTICE("Catalog", "Removed " + id);
        std::string message = "Removed " + name + ".";
        if (manifest.kind == "emulator") message += removeEmulatorLauncher(targets_.root, manifest);
        if (removal.restored > 0) {
            message += " Put back " + std::to_string(removal.restored) + (removal.restored == 1 ? " file" : " files") + " it had replaced.";
        }
        if (removal.kept > 0) {
            message += " Kept " + std::to_string(removal.kept) + (removal.kept == 1 ? " file" : " files") + " you changed.";
        }
        finish(id, true, message);
    }

    void Service::finish(const std::string& id, bool ok, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        outcomes_[id] = { ok, message };
        active_ = false;
        activity_ = Activity();
        cancelRequested_ = false;
    }
}

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
#include "Index.h"
#include "../Utility/Log.h"
#include <algorithm>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace
{
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

    // The collections/ folders a package's files are in
    std::set<std::string> collectionsOf(const Catalog::Manifest& manifest)
    {
        std::set<std::string> names;
        for (const Catalog::InstalledFile& file : manifest.files) {
            size_t slash = file.path.find('/');
            if (file.target == "collections" && slash != std::string::npos && slash > 0) {
                names.insert(file.path.substr(0, slash));
            }
        }
        return names;
    }
}

namespace Catalog
{
    Service::Service(const std::string& catalogFolder, const Targets& targets, std::unique_ptr<Http> http)
        : folder_(catalogFolder)
        , targets_(targets)
        , http_(std::move(http))
        , installer_(catalogFolder, targets)
    {
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

    void Service::start()
    {
        if (worker_.joinable()) {
            return;
        }
        installer_.recover();
        std::vector<SourceConfig> sources;
        std::string error;
        if (!loadSources((fs::path(folder_) / "sources.conf").string(), sources, error)) {
            LOG_ERROR("Catalog", error);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            installed_ = installer_.installed();
            sources_ = sources;
        }
        worker_ = std::thread(&Service::run, this);
        if (!sources.empty()) {
            refresh();
        }
    }

    std::vector<Entry> Service::entries() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Entry> result;
        for (const Package& package : packages_) {
            Entry entry;
            entry.package = package;
            auto installed = installed_.find(package.id);
            if (installed != installed_.end()) {
                entry.installedVersion = installed->second.version;
                entry.updateAvailable = compareVersions(package.version, entry.installedVersion) > 0;
            }
            result.push_back(entry);
        }
        std::stable_sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) {
            if (a.package.kind != b.package.kind) return a.package.kind < b.package.kind;
            return lower(a.package.name) < lower(b.package.name);
        });
        return result;
    }

    bool Service::entry(const std::string& id, Entry& entry) const
    {
        for (const Entry& candidate : entries()) {
            if (candidate.package.id == id) {
                entry = candidate;
                return true;
            }
        }
        return false;
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
        return warnings_;
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
        return std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.kind != Job::Kind::Refresh && job.id == id; });
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
            if (job.kind == Job::Kind::Refresh) {
                // One waiting refresh is enough
                if (std::any_of(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.kind == Job::Kind::Refresh; })) return;
            }
            else {
                if ((active_ && activity_.id == job.id)
                    || std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.kind != Job::Kind::Refresh && j.id == job.id; })) {
                    return;
                }
                outcomes_.erase(job.id);
            }
            jobs_.push_back(job);
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
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.kind != Job::Kind::Refresh && job.id == id; }),
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
        LOG_NOTICE("Catalog", "Removed source " + name);
        refresh();
        return true;
    }

    bool Service::takeChanges(Changes& changes)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!changed_ || working_ || !jobs_.empty()) return false;
        changes = changes_;
        changes_ = Changes();
        changed_ = false;
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
            lock.unlock();

            switch (job.kind) {
            case Job::Kind::Refresh: doRefresh(); break;
            case Job::Kind::Install: doInstall(job.id); break;
            case Job::Kind::Remove:  doRemove(job.id); break;
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

    void Service::doRefresh()
    {
        std::vector<SourceConfig> sources = this->sources();
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        std::set<std::string> ids;
        std::string cache = (fs::path(folder_) / "cache").string();

        for (const SourceConfig& source : sources) {
            if (!source.enabled) continue;
            SourceContents contents = readSource(source, cache, http_.get());
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
        }
        fetchArtwork(packages);
    }

    void Service::fetchArtwork(const std::vector<Package>& packages)
    {
        for (const Package& package : packages) {
            for (const auto& [type, location] : package.artwork) {
                if (stopping_) return;
                std::string extension = imageExtension(location);
                if (!isValidId(type) || extension.empty()) continue;
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
                    ok = http_ && http_->download(location, part.string(), 0, [&](uint64_t, uint64_t) { return !stopping_; }, transfer);
                }
                else {
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
            activity_ = Activity();
            activity_.id = id;
            activity_.name = package.name;
            activity_.total = package.totalSize();
            active_ = true;
            cancelRequested_ = false;
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            installed_ = installer_.installed();
            changed_ = true;
            auto manifest = installed_.find(package.id);
            if (manifest != installed_.end()) {
                for (const std::string& name : collectionsOf(manifest->second)) {
                    changes_.addedCollections.insert(name);
                    changes_.removedCollections.erase(name);
                }
            }
        }
        LOG_NOTICE("Catalog", "Installed " + package.id + " " + package.version);
        finish(id, true, (entry.installedVersion.empty() ? "Installed " : "Updated to ") + package.name + " " + package.version + ".");
    }

    void Service::doRemove(const std::string& id)
    {
        std::string name = id;
        std::set<std::string> collections;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto installed = installed_.find(id);
            if (installed != installed_.end() && !installed->second.name.empty()) name = installed->second.name;
            if (installed != installed_.end()) collections = collectionsOf(installed->second);
            activity_ = Activity();
            activity_.id = id;
            activity_.name = name;
            activity_.removing = true;
            active_ = true;
        }
        int kept = 0;
        std::string error;
        if (!installer_.remove(id, kept, error)) {
            finish(id, false, error);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            installed_ = installer_.installed();
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
        if (kept > 0) {
            message += " Kept " + std::to_string(kept) + (kept == 1 ? " file" : " files") + " you changed.";
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

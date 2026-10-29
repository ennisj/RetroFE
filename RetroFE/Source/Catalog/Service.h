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
#pragma once

#include "Http.h"
#include "Installer.h"
#include "Package.h"
#include "SelfUpdate.h"
#include "Sources.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// The Catalog as the screens see it: the packages every source offers, what's
// installed, and installs and removals run one at a time on a worker thread so
// the frontend keeps drawing. Everything it keeps is under the catalog folder:
//   sources.conf   the sources
//   cache/         the last copy of each index
//   artwork/<type>/<id>.<ext>   package artwork, shown like collection artwork
//   downloads/, staging/, backups/, installed/   see Downloader and Installer
// All public functions can be called from any thread.
namespace Catalog
{
    struct Entry
    {
        Package package;
        std::string installedVersion;   // "" if not installed
        std::string installedSource;    // the source it was installed from
        bool updateAvailable = false;   // a newer version, from the same source
    };

    // The collections/ folders an installed package put files in
    std::set<std::string> collectionsOf(const Manifest& manifest);

    class Service
    {
    public:
        // http may be null, in which case only folder sources and file indexes work
        Service(const std::string& catalogFolder, const Targets& targets, std::unique_ptr<Http> http);
        ~Service();

        // Where RetroFE itself is, so GitHub sources can update it; call before start().
        // Without it, updates of RetroFE are refused.
        void setApp(const AppInstall& app);
        AppInstall app() const;

        // Starts the worker, which first finishes interrupted installs, then reads the sources
        void start();

        // Packages from every enabled source, by kind then name
        std::vector<Entry> entries() const;
        bool entry(const std::string& id, Entry& entry) const;
        size_t packageCount() const;
        // Whether an install or removal is queued or running (reading sources
        // and fetching artwork don't count: they can be picked up again later)
        bool isBusy() const;
        std::vector<Manifest> installed() const;          // by name
        std::vector<SourceConfig> sources() const;
        std::vector<std::string> warnings() const;        // from reading the sources
        bool refreshing() const;                          // sources are being read

        std::string artworkFolder() const;
        std::vector<std::string> artworkTypes() const;    // artwork/ subfolders

        // The package being installed or removed, and how far along it is
        struct Activity
        {
            std::string id;
            std::string name;
            bool removing = false;
            uint64_t received = 0;
            uint64_t total = 0;     // 0 if unknown
        };
        bool activity(Activity& activity) const;

        bool isQueued(const std::string& id) const;       // waiting or in progress
        // The last install or removal of a package: ok and a message for the user
        bool outcome(const std::string& id, bool& ok, std::string& message) const;

        void refresh();
        void install(const std::string& id);              // installs or updates
        void remove(const std::string& id);
        void cancel(const std::string& id);               // keeps what's downloaded, to continue later

        bool addSource(const SourceConfig& source, std::string& error);
        bool removeSource(const std::string& name, std::string& error);

        // What installs and removals changed
        struct Changes
        {
            bool libraryFiles = false;
            std::set<std::string> addedCollections;     // collections/ folders installed into
            std::set<std::string> removedCollections;   // collections/ folders that are now gone
        };

        // True once after packages were installed or removed, when nothing
        // else is queued: RetroFE should reload to show them
        bool takeChanges(Changes& changes);
        // Whether there are such changes, without taking them
        bool hasChanges() const;

        // How the last reading of a source went
        struct SourceStatus
        {
            int64_t checked = 0;      // when, in seconds since 1970; 0 if it hasn't been read yet
            bool fromCache = false;   // it couldn't be reached: its packages are the last copy
            std::string error;        // nothing could be read
        };
        // False if the source hasn't been read since RetroFE started (or is off)
        bool sourceStatus(const std::string& name, SourceStatus& status) const;

        // True once after RetroFE updated itself (or an unfinished update was
        // undone), when nothing else is queued: RetroFE should start the
        // program now in place and exit
        bool takeRestartRequest();

        // For tests: blocks until everything queued has finished
        void waitUntilIdle();

    private:
        struct Job
        {
            enum class Kind { Recover, Refresh, Install, Remove, Artwork } kind;
            std::string id;
            bool isPackageJob() const { return kind == Kind::Install || kind == Kind::Remove; }
        };

        void run();
        void doRecover();
        void doRefresh();
        void doInstall(const std::string& id);
        void doUpdateApp(const Package& package);
        void doRemove(const std::string& id);
        void doArtwork();
        void finish(const std::string& id, bool ok, const std::string& message);
        void enqueue(const Job& job);
        void rebuildEntries();   // call with mutex_ held, after packages_ or installed_ change
        std::string cacheFolder() const;

        std::string folder_;
        Targets targets_;
        std::unique_ptr<Http> http_;
        Installer installer_;

        mutable std::mutex mutex_;
        std::condition_variable wake_;
        std::condition_variable idle_;
        std::deque<Job> jobs_;
        bool working_ = false;
        bool refreshingNow_ = false;
        std::atomic<bool> stopping_{ false };
        std::thread worker_;

        std::vector<SourceConfig> sources_;
        std::vector<Package> packages_;
        std::map<std::string, Manifest> installed_;
        std::vector<Entry> entries_;               // packages_ and installed_ combined, sorted
        std::map<std::string, size_t> entryIndex_; // id -> position in entries_
        std::vector<std::string> sourceWarnings_;
        std::vector<std::string> warnings_;
        std::vector<std::string> appWarnings_;     // about RetroFE's own last update, shown first
        std::map<std::string, std::pair<bool, std::string>> outcomes_;
        Activity activity_;
        bool active_ = false;
        bool changed_ = false;
        Changes changes_;
        std::map<std::string, SourceStatus> sourceStatus_;   // by source name
        std::atomic<bool> cancelRequested_{ false };
        AppInstall app_;
        bool restartRequested_ = false;
    };
}

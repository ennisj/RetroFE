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

#include "Index.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Library
{
    // Builds Index snapshots on a worker thread. Screens call index() every
    // frame: it returns the latest finished snapshot (nullptr before the first
    // one), which never changes after it's published, so it can be read
    // without locks for as long as the shared_ptr is held.
    //
    // After a change (an Editor call), call changed(): it marks changes as
    // pending, for the Manage hub to reload RetroFE when it closes, and starts
    // a fresh build; the old snapshot stays available until the new one is ready.
    class Indexer
    {
    public:
        enum class State { Idle, Building, Ready, Cancelled };

        struct Status
        {
            State state = State::Idle;
            float progress = 0;         // 0..1 of the current build
            std::string phase;          // "Listing MAME", ...
            unsigned generation = 0;    // bumps with every published snapshot
        };

        explicit Indexer(ScanOptions options);
        ~Indexer();  // cancels a running build and waits for it

        Indexer(const Indexer&) = delete;
        Indexer& operator=(const Indexer&) = delete;

        // Starts a build; one already running is cancelled and restarted
        void rebuild();
        void cancel();
        Status status() const;
        std::shared_ptr<const Index> index() const;

        // Blocks until no build is running (tests, shutdown); false on timeout
        bool wait(int timeoutMs = -1);

        // Marks the install as changed (RetroFE needs a reload) and rebuilds
        void changed(const std::string& what = "");
        bool changesPending() const;
        std::vector<std::string> pendingChanges() const;
        void clearPendingChanges();

        const ScanOptions& options() const { return options_; }

    private:
        struct Job
        {
            std::thread thread;
            std::shared_ptr<std::atomic<bool>> cancel;
            std::shared_ptr<std::atomic<bool>> done;
        };
        void run(unsigned ticket, std::shared_ptr<std::atomic<bool>> cancel, std::shared_ptr<std::atomic<bool>> done);
        void joinFinished();

        ScanOptions options_;
        mutable std::mutex mutex_;
        std::condition_variable idle_;
        std::vector<Job> jobs_;  // the current build last; earlier ones are cancelled and finishing
        std::shared_ptr<const Index> index_;
        Status status_;
        unsigned ticket_ = 0;
        bool running_ = false;
        std::vector<std::string> pending_;
        bool pendingFlag_ = false;
    };
}

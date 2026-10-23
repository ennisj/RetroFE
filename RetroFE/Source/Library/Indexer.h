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
    // pending, for the Manage hub to reload RetroFE when it closes, and asks
    // for a fresh build; the old snapshot stays available until the new one
    // is ready. A build that's running is never cancelled by a change: it
    // finishes and publishes, and one more build then starts, so a burst of
    // edits still gets snapshots out (each picking up every change so far).
    class Indexer
    {
    public:
        enum class State { Idle, Building, Ready, Cancelled, Failed };

        struct Status
        {
            State state = State::Idle;
            float progress = 0;         // 0..1 of the current build
            std::string phase;          // "Listing MAME", ...
            unsigned generation = 0;    // bumps with every published snapshot
            std::string error;          // when Failed
        };

        explicit Indexer(ScanOptions options);
        ~Indexer();  // cancels a running build and waits for it

        Indexer(const Indexer&) = delete;
        Indexer& operator=(const Indexer&) = delete;

        // Starts a build, or, while one runs, asks for another right after it
        void rebuild();
        // Stops the running build without publishing it (shutdown); a later rebuild() starts afresh
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
        void run();

        ScanOptions options_;
        mutable std::mutex mutex_;
        std::condition_variable idle_;
        std::thread worker_;          // one at a time; it loops while rebuilds are asked for
        std::atomic<bool> cancel_{ false };
        std::shared_ptr<const Index> index_;
        Status status_;
        bool running_ = false;
        bool again_ = false;          // another build was asked for while one ran
        std::vector<std::string> pending_;
        bool pendingFlag_ = false;
    };
}

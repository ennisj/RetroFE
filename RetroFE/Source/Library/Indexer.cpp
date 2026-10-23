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
#include "Indexer.h"
#include "../Utility/Log.h"
#include <chrono>
#include <sstream>
#include <system_error>

namespace Library
{

Indexer::Indexer(ScanOptions options)
    : options_(std::move(options))
{
}

Indexer::~Indexer()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancel_ = true;
        again_ = false;
        worker = std::move(worker_);
    }
    if (worker.joinable()) {
        worker.join();
    }
}

void Indexer::rebuild()
{
    std::thread finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            // Don't cancel: the running build publishes, then one more runs
            again_ = true;
            return;
        }
        // The last worker has stopped (running_ is false); collect it outside the lock
        finished = std::move(worker_);
    }
    if (finished.joinable()) {
        finished.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        again_ = true;  // another caller started one meanwhile
        return;
    }
    cancel_ = false;
    again_ = false;
    running_ = true;
    status_.state = State::Building;
    status_.progress = 0;
    status_.phase = "Starting";
    status_.error.clear();
    try {
        worker_ = std::thread(&Indexer::run, this);
    }
    catch (const std::system_error& e) {
        running_ = false;
        status_.state = State::Failed;
        status_.error = std::string("Could not start the index build: ") + e.what();
        LOG_ERROR("Library", status_.error);
        idle_.notify_all();
    }
}

void Indexer::run()
{
    for (;;) {
        BuildCallbacks callbacks;
        callbacks.cancel = &cancel_;
        callbacks.progress = [this](float progress, const std::string& phase) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.progress = progress;
            status_.phase = phase;
        };
        std::unique_ptr<Index> built;
        std::string failure;
        try {
            built = buildIndex(options_, callbacks);
        }
        catch (const std::exception& e) {
            failure = std::string("Building the library index failed: ") + e.what();
            LOG_ERROR("Library", failure);
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if (built) {
            std::ostringstream ss;
            ss << "Indexed " << built->collections.size() << " collections and " << built->games.size()
               << " games in " << built->seconds << " s";
            LOG_INFO("Library", ss.str());
            index_ = std::shared_ptr<const Index>(built.release());
            status_.state = State::Ready;
            status_.progress = 1;
            status_.phase = "Ready";
            status_.generation++;
        }
        else if (!failure.empty()) {
            status_.state = State::Failed;
            status_.error = failure;
        }
        else {
            status_.state = State::Cancelled;
        }
        if (again_ && !cancel_) {
            // Changes came in while this build ran: one more, which sees them all
            again_ = false;
            status_.state = State::Building;
            status_.progress = 0;
            status_.phase = "Starting";
            lock.unlock();
            idle_.notify_all();  // a snapshot was published
            continue;
        }
        running_ = false;
        again_ = false;
        lock.unlock();
        idle_.notify_all();
        return;
    }
}

void Indexer::cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_ = true;
    again_ = false;
}

Indexer::Status Indexer::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::shared_ptr<const Index> Indexer::index() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return index_;
}

bool Indexer::wait(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeoutMs < 0) {
        idle_.wait(lock, [this] { return !running_; });
        return true;
    }
    return idle_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !running_; });
}

void Indexer::changed(const std::string& what)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFlag_ = true;
        if (!what.empty()) {
            pending_.push_back(what);
        }
    }
    rebuild();
}

bool Indexer::changesPending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingFlag_;
}

std::vector<std::string> Indexer::pendingChanges() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_;
}

void Indexer::clearPendingChanges()
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFlag_ = false;
    pending_.clear();
}

}

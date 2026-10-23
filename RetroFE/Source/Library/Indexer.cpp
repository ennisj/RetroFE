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

namespace Library
{

Indexer::Indexer(ScanOptions options)
    : options_(std::move(options))
{
}

Indexer::~Indexer()
{
    std::vector<Job> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Job& job : jobs_) {
            *job.cancel = true;
        }
        jobs = std::move(jobs_);
        jobs_.clear();
    }
    for (Job& job : jobs) {
        if (job.thread.joinable()) {
            job.thread.join();
        }
    }
}

void Indexer::joinFinished()
{
    // Called with mutex_ held: join builds that have already stopped
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (*it->done && it->thread.joinable()) {
            it->thread.join();
            it = jobs_.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Indexer::rebuild()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // A running build is told to stop; it finishes on its own and its result is dropped
    for (Job& job : jobs_) {
        *job.cancel = true;
    }
    joinFinished();
    unsigned ticket = ++ticket_;
    running_ = true;
    status_.state = State::Building;
    status_.progress = 0;
    status_.phase = "Starting";
    Job job;
    job.cancel = std::make_shared<std::atomic<bool>>(false);
    job.done = std::make_shared<std::atomic<bool>>(false);
    job.thread = std::thread(&Indexer::run, this, ticket, job.cancel, job.done);
    jobs_.push_back(std::move(job));
}

void Indexer::run(unsigned ticket, std::shared_ptr<std::atomic<bool>> cancel, std::shared_ptr<std::atomic<bool>> done)
{
    BuildCallbacks callbacks;
    callbacks.cancel = cancel.get();
    callbacks.progress = [this, ticket](float progress, const std::string& phase) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ticket == ticket_) {
            status_.progress = progress;
            status_.phase = phase;
        }
    };
    std::unique_ptr<Index> built;
    try {
        built = buildIndex(options_, callbacks);
    }
    catch (const std::exception& e) {
        LOG_ERROR("Library", std::string("Building the library index failed: ") + e.what());
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ticket == ticket_) {
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
            else {
                status_.state = State::Cancelled;
            }
            running_ = false;
        }
        *done = true;
    }
    idle_.notify_all();
}

void Indexer::cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (Job& job : jobs_) {
        *job.cancel = true;
    }
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

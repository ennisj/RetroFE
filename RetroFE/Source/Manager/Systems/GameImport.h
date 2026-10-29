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

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Manager
{
    class GameImport
    {
    public:
        struct Request
        {
            std::filesystem::path from, into;
            std::vector<std::string> extensions;
            std::string destinationLabel;
            bool subfolders = true;
            bool hierarchy = false;
        };
        struct Status
        {
            bool running = false, copying = false, changed = false;
            size_t found = 0, copied = 0, already = 0, conflicts = 0, missing = 0;
            unsigned generation = 0;
            std::string message;
        };
        using Space = std::function<uintmax_t(const std::filesystem::path&)>;
        using Progress = std::function<void(const std::string&)>;
        ~GameImport();
        void start(Request request, bool copy);
        void stop();
        void reset();
        Status status() const;
        // Also used by tests: progress is called between chunks, before publishing a file.
        static Status run(const Request& request, bool copy, std::atomic<bool>& stop,
                          Space space = {}, Progress progress = {});
    private:
        mutable std::mutex mutex_;
        std::thread worker_;
        std::atomic<bool> stop_{ false };
        Status status_;
    };
}

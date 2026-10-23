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

#include <cstdint>
#include <filesystem>
#include <string>

// A file's size and last write time, to tell when a file was replaced under
// the same name (e.g. a game's logo replaced from Manage), so anything
// loaded from it can be loaded again
struct FileStamp
{
    bool exists = false;
    uintmax_t size = 0;
    int64_t written = 0;   // last write time, in the file clock's ticks

    static FileStamp of(const std::string& path)
    {
        FileStamp stamp;
        std::error_code ec;
        std::filesystem::path file(path);
        uintmax_t size = std::filesystem::file_size(file, ec);
        if (ec) {
            return stamp;
        }
        auto written = std::filesystem::last_write_time(file, ec);
        if (ec) {
            return stamp;
        }
        stamp.exists = true;
        stamp.size = size;
        stamp.written = static_cast<int64_t>(written.time_since_epoch().count());
        return stamp;
    }

    bool operator==(const FileStamp& other) const
    {
        return exists == other.exists && size == other.size && written == other.written;
    }
    bool operator!=(const FileStamp& other) const { return !(*this == other); }
};

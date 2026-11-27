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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// Checksums for verifying downloads and recording installed files.
// Both work incrementally, so large files are hashed as they're read.
namespace Downloads
{
    class Sha256
    {
    public:
        Sha256();
        void update(const void* data, size_t size);
        std::string hex();  // finishes the hash; lowercase hex

    private:
        void block(const uint8_t* data);
        uint32_t state_[8];
        uint8_t buffer_[64];
        size_t buffered_ = 0;
        uint64_t length_ = 0;
    };

    // CRC-32 as zip archives and libretro's core list record it
    class Crc32
    {
    public:
        void update(const void* data, size_t size);
        uint32_t value() const { return value_; }

    private:
        uint32_t value_ = 0;
    };

    // SHA-256 of a whole file, or "" if it can't be read
    // Opened by path, so names outside the system code page work on Windows
    std::string sha256File(const std::filesystem::path& path);
}

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
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Catalog
{
    class Archive
    {
    public:
        enum class Form { None, Zip, SevenZip, SelfExtracting };
        // Names are UTF-8 with forward slashes. No entry is written by the reader.
        struct Entry
        {
            std::string name;
            uint64_t size;
            bool directory;
        };
        using Writer = std::function<bool(const void*, size_t)>;

        Archive();
        ~Archive();
        Archive(const Archive&) = delete;
        Archive& operator=(const Archive&) = delete;
        // SFX offsets refer to a CRC-validated 7-Zip start header.
        static Form detect(const std::string& path, uint64_t& offset);
        bool open(const std::string& path, const std::string& publishedName, std::string& error);
        const std::vector<Entry>& entries() const;
        // Delivers one file in chunks; a false writer result stops extraction.
        bool extract(size_t index, const Writer& writer, const std::atomic<bool>* stop, std::string& error);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

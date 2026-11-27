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

// Writes small 7-Zip archives for the download tests: every file stored
// (the Copy method) in one folder, with Windows attributes, which can carry a
// Unix mode in their high 16 bits (0x8000, as p7zip and 7-Zip on Linux write
// them). Only what the LZMA SDK's reader needs; no compression.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace SevenZipWriter
{
    struct Entry
    {
        std::string name;        // UTF-8 (ASCII in the tests), '/' separated
        std::string content;     // a link's target, for a link
        uint32_t attributes = 0; // e.g. 0x20 | 0x8000 | (0100755 << 16)
        bool directory = false;
    };

    // The attributes of a file with this Unix mode (type bits included)
    inline uint32_t unixAttributes(uint32_t mode)
    {
        const uint32_t directory = (mode & 0170000) == 0040000 ? 0x10 : 0x20;
        return directory | 0x8000 | (mode << 16);
    }

    namespace detail
    {
        inline uint32_t crc32(const std::string& data)
        {
            uint32_t crc = 0xffffffffu;
            for (unsigned char c : data) {
                crc ^= c;
                for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
            }
            return ~crc;
        }

        inline void number(std::string& out, uint64_t value)
        {
            int extra = 0;
            while (extra < 8 && value >= (uint64_t(1) << (7 * (extra + 1)))) extra++;
            unsigned char first = extra == 0 ? 0 : static_cast<unsigned char>(0xff << (8 - extra));
            if (extra < 8) first |= static_cast<unsigned char>(value >> (8 * extra));
            out += static_cast<char>(first);
            for (int i = 0; i < extra; i++) out += static_cast<char>((value >> (8 * i)) & 0xff);
        }

        inline void fixed(std::string& out, uint64_t value, int bytes)
        {
            for (int i = 0; i < bytes; i++) out += static_cast<char>((value >> (8 * i)) & 0xff);
        }

        inline std::string bits(const std::vector<bool>& values)
        {
            std::string out((values.size() + 7) / 8, '\0');
            for (size_t i = 0; i < values.size(); i++) {
                if (values[i]) out[i / 8] = static_cast<char>(out[i / 8] | (0x80 >> (i % 8)));
            }
            return out;
        }

        inline void property(std::string& out, unsigned char id, const std::string& data)
        {
            out += static_cast<char>(id);
            number(out, data.size());
            out += data;
        }
    }

    inline bool write(const std::string& path, const std::vector<Entry>& entries)
    {
        using namespace detail;
        std::string packed;
        std::vector<const Entry*> streams;
        std::vector<bool> empty, emptyFile;
        for (const Entry& entry : entries) {
            bool none = entry.directory || entry.content.empty();
            empty.push_back(none);
            if (none) emptyFile.push_back(!entry.directory);
            else {
                streams.push_back(&entry);
                packed += entry.content;
            }
        }

        std::string header;
        header += '\x01';                       // kHeader
        if (!streams.empty()) {
            header += '\x04';                   // kMainStreamsInfo
            header += '\x06';                   // kPackInfo
            number(header, 0);                  // pack position
            number(header, 1);                  // one packed stream
            header += '\x09';                   // kSize
            number(header, packed.size());
            header += '\x00';                   // kEnd
            header += '\x07';                   // kUnPackInfo
            header += '\x0b';                   // kFolder
            number(header, 1);                  // one folder
            header += '\x00';                   // not external
            number(header, 1);                  // one coder
            header += '\x01';                   // a one-byte codec id, simple, no properties
            header += '\x00';                   // Copy
            header += '\x0c';                   // kCodersUnPackSize
            number(header, packed.size());
            header += '\x00';                   // kEnd
            header += '\x08';                   // kSubStreamsInfo
            header += '\x0d';                   // kNumUnPackStream
            number(header, streams.size());
            if (streams.size() > 1) {
                header += '\x09';               // kSize: all but the last
                for (size_t i = 0; i + 1 < streams.size(); i++) number(header, streams[i]->content.size());
            }
            header += '\x0a';                   // kCRC
            header += '\x01';                   // all defined
            for (const Entry* stream : streams) fixed(header, crc32(stream->content), 4);
            header += '\x00';                   // kEnd
            header += '\x00';                   // kEnd of the streams info
        }
        header += '\x05';                       // kFilesInfo
        number(header, entries.size());
        if (std::find(empty.begin(), empty.end(), true) != empty.end()) {
            property(header, 0x0e, bits(empty));       // kEmptyStream
            property(header, 0x0f, bits(emptyFile));   // kEmptyFile
        }
        std::string names(1, '\0');             // not external
        for (const Entry& entry : entries) {
            for (unsigned char c : entry.name) {
                names += static_cast<char>(c);
                names += '\0';
            }
            names += std::string(2, '\0');
        }
        property(header, 0x11, names);          // kName
        std::string attributes;
        attributes += '\x01';                   // all defined
        attributes += '\x00';                   // not external
        for (const Entry& entry : entries) fixed(attributes, entry.attributes, 4);
        property(header, 0x15, attributes);     // kWinAttributes
        header += '\x00';                       // kEnd
        header += '\x00';                       // kEnd

        std::string start;
        fixed(start, packed.size(), 8);         // the header follows the packed data
        fixed(start, header.size(), 8);
        fixed(start, crc32(header), 4);
        std::string signature = std::string("7z\xbc\xaf\x27\x1c", 6) + std::string("\x00\x04", 2);
        fixed(signature, crc32(start), 4);

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << signature << start << packed << header;
        return static_cast<bool>(file);
    }
}

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
#include "Archive.h"
#include "minizip/unzip.h"
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>

namespace
{
    const ISzAlloc allocator = { SzAlloc, SzFree };
    void initCrc()
    {
        static std::once_flag once;
        std::call_once(once, [] { CrcGenerateTable(); });
    }

    // SDK seeks are relative to the archive, after any self-extracting stub.
    struct OffsetStream : ISeekInStream
    {
        std::ifstream file;
        uint64_t base = 0;
        const std::atomic<bool>* stop = nullptr;

        OffsetStream()
        {
            Read = [](ISeekInStreamPtr p, void* data, size_t* size) -> SRes {
                auto& s = *static_cast<OffsetStream*>(const_cast<ISeekInStream*>(p));
                if (s.stop && *s.stop) return SZ_ERROR_FAIL;
                s.file.read(static_cast<char*>(data), static_cast<std::streamsize>(*size));
                *size = static_cast<size_t>(s.file.gcount());
                return s.file.bad() ? SZ_ERROR_READ : SZ_OK;
            };
            Seek = [](ISeekInStreamPtr p, Int64* pos, ESzSeek origin) -> SRes {
                auto& s = *static_cast<OffsetStream*>(const_cast<ISeekInStream*>(p));
                s.file.clear();
                if (origin == SZ_SEEK_SET) {
                    if (*pos < 0 || static_cast<uint64_t>(*pos) > static_cast<uint64_t>(INT64_MAX) - s.base) return SZ_ERROR_READ;
                    s.file.seekg(static_cast<std::streamoff>(s.base + *pos));
                } else s.file.seekg(*pos, origin == SZ_SEEK_CUR ? std::ios::cur : std::ios::end);
                auto at = s.file.tellg();
                if (at < 0 || static_cast<uint64_t>(at) < s.base) return SZ_ERROR_READ;
                *pos = static_cast<Int64>(static_cast<uint64_t>(at) - s.base);
                return SZ_OK;
            };
        }
    };
}

struct Catalog::Archive::Impl
{
    OffsetStream stream;
    CLookToRead2 look{};
    std::array<Byte, 1 << 16> input{};
    CSzArEx db;
    unzFile zip = nullptr;
    std::vector<unz64_file_pos> zipPositions;
    std::vector<Entry> entries;
    std::string name;
    UInt32 block = UINT32_MAX;
    Byte* buffer = nullptr;
    size_t bufferSize = 0;

    Impl() { SzArEx_Init(&db); }
    ~Impl()
    {
        if (zip) unzClose(zip);
        ISzAlloc_Free(&allocator, buffer);
        SzArEx_Free(&db, &allocator);
    }
    bool fail(SRes result, std::string& error)
    {
        error = (result == SZ_ERROR_UNSUPPORTED ? "Unsupported archive method: " :
                 result == SZ_ERROR_NO_ARCHIVE ? "Not an archive: " : "Damaged archive: ") + name;
        return false;
    }
};

Catalog::Archive::Archive() : impl_(new Impl) {}
Catalog::Archive::~Archive() = default;

Catalog::Archive::Form Catalog::Archive::detect(const std::string& path, uint64_t& offset)
{
    offset = 0;
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    std::array<unsigned char, 32> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    size_t count = static_cast<size_t>(file.gcount());
    if (count >= 4 && (!std::memcmp(header.data(), "PK\003\004", 4) || !std::memcmp(header.data(), "PK\005\006", 4))) return Form::Zip;
    const unsigned char signature[] = { '7', 'z', 0xbc, 0xaf, 0x27, 0x1c };
    if (count >= 6 && !std::memcmp(header.data(), signature, 6)) return Form::SevenZip;
    if (count < 2 || header[0] != 'M' || header[1] != 'Z') return Form::None;
    file.clear();
    file.seekg(0);
    std::vector<unsigned char> scan((16 << 20) + 31);
    file.read(reinterpret_cast<char*>(scan.data()), scan.size());
    scan.resize(static_cast<size_t>(file.gcount()));
    initCrc();
    for (size_t i = 2; i < (16u << 20) && i + 32 <= scan.size(); ++i) {
        if (std::memcmp(scan.data() + i, signature, 6)) continue;
        UInt32 crc = 0;
        for (unsigned j = 0; j < 4; ++j) crc |= static_cast<UInt32>(scan[i + 8 + j]) << (8 * j);
        if (CrcCalc(scan.data() + i + 12, 20) == crc) {
            offset = i;
            return Form::SelfExtracting;
        }
    }
    return Form::None;
}

bool Catalog::Archive::open(const std::string& path, const std::string& publishedName, std::string& error)
{
    impl_.reset(new Impl);
    auto& p = *impl_;
    p.name = publishedName;
    uint64_t offset;
    Form form = detect(path, offset);
    if (form == Form::None) return p.fail(SZ_ERROR_NO_ARCHIVE, error);
    if (form == Form::Zip) {
        p.zip = unzOpen64(path.c_str());
        if (!p.zip) {
            // Some minizip versions cannot open an empty ZIP at offset zero.
            std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
            auto length = file.tellg();
            std::array<unsigned char, 22> end{};
            file.seekg(0);
            file.read(reinterpret_cast<char*>(end.data()), end.size());
            if (file.gcount() == 22 && !std::memcmp(end.data(), "PK\005\006", 4)
                && std::all_of(end.begin() + 4, end.begin() + 20, [](unsigned char c) { return c == 0; })
                && length == 22 + end[20] + (end[21] << 8)) return true;
            return p.fail(SZ_ERROR_ARCHIVE, error);
        }
        int result = unzGoToFirstFile(p.zip);
        while (result == UNZ_OK) {
            unz_file_info64 info{};
            if (unzGetCurrentFileInfo64(p.zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) return p.fail(SZ_ERROR_ARCHIVE, error);
            std::vector<char> name(info.size_filename + 1);
            if (unzGetCurrentFileInfo64(p.zip, &info, name.data(), static_cast<uLong>(name.size()), nullptr, 0, nullptr, 0) != UNZ_OK) return p.fail(SZ_ERROR_ARCHIVE, error);
            std::string entry(name.data(), info.size_filename);
            if (!(info.flag & (1 << 11))) entry = std::filesystem::path(entry).generic_u8string();
            std::replace(entry.begin(), entry.end(), '\\', '/');
            unz64_file_pos position{};
            if (unzGetFilePos64(p.zip, &position) != UNZ_OK) return p.fail(SZ_ERROR_ARCHIVE, error);
            p.zipPositions.push_back(position);
            p.entries.push_back({ entry, info.uncompressed_size, !entry.empty() && entry.back() == '/' });
            result = unzGoToNextFile(p.zip);
        }
        return result == UNZ_END_OF_LIST_OF_FILE || p.fail(SZ_ERROR_ARCHIVE, error);
    }
    initCrc();
    p.stream.base = offset;
    p.stream.file.open(std::filesystem::u8path(path), std::ios::binary);
    p.stream.file.seekg(static_cast<std::streamoff>(offset));
    LookToRead2_CreateVTable(&p.look, False);
    p.look.realStream = &p.stream;
    p.look.buf = p.input.data();
    p.look.bufSize = p.input.size();
    LookToRead2_INIT(&p.look)
    SRes result = SzArEx_Open(&p.db, &p.look.vt, &allocator, &allocator);
    if (result != SZ_OK) return p.fail(result, error);
    for (UInt32 i = 0; i < p.db.db.NumFolders; ++i) {
        if (SzAr_GetFolderUnpackSize(&p.db.db, i) > (1ull << 30)) {
            error = "This archive needs more memory to unpack than RetroFE allows: " + p.name;
            return false;
        }
    }
    for (UInt32 i = 0; i < p.db.NumFiles; ++i) {
        std::vector<UInt16> name(SzArEx_GetFileNameUtf16(&p.db, i, nullptr));
        SzArEx_GetFileNameUtf16(&p.db, i, name.data());
        std::string entry;
        for (size_t at = 0; at + 1 < name.size(); ++at) {
            uint32_t code = name[at];
            if (code >= 0xd800 && code <= 0xdbff) {
                if (at + 2 >= name.size() || name[at + 1] < 0xdc00 || name[at + 1] > 0xdfff) return p.fail(SZ_ERROR_ARCHIVE, error);
                code = 0x10000 + ((code - 0xd800) << 10) + (name[++at] - 0xdc00);
            } else if (code >= 0xdc00 && code <= 0xdfff) return p.fail(SZ_ERROR_ARCHIVE, error);
            if (code == 0) return p.fail(SZ_ERROR_ARCHIVE, error);
            if (code < 0x80) entry += static_cast<char>(code);
            else {
                if (code < 0x800) entry += static_cast<char>(0xc0 | (code >> 6));
                else {
                    if (code < 0x10000) entry += static_cast<char>(0xe0 | (code >> 12));
                    else {
                        entry += static_cast<char>(0xf0 | (code >> 18));
                        entry += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
                    }
                    entry += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                }
                entry += static_cast<char>(0x80 | (code & 0x3f));
            }
        }
        std::replace(entry.begin(), entry.end(), '\\', '/');
        p.entries.push_back({ entry, SzArEx_GetFileSize(&p.db, i), SzArEx_IsDir(&p.db, i) != 0 });
    }
    return true;
}

const std::vector<Catalog::Archive::Entry>& Catalog::Archive::entries() const { return impl_->entries; }

bool Catalog::Archive::extract(size_t index, const Writer& writer, const std::atomic<bool>* stop, std::string& error)
{
    auto& p = *impl_;
    auto stopped = [&] { if (stop && *stop) { error = "Stopped"; return true; } return false; };
    if (stopped()) return false;
    if (index >= p.entries.size()) return p.fail(SZ_ERROR_ARCHIVE, error);
    if (p.entries[index].directory) return true;
    if (p.zip) {
        int result = unzGoToFilePos64(p.zip, &p.zipPositions[index]);
        if (result != UNZ_OK || unzOpenCurrentFile(p.zip) != UNZ_OK) return p.fail(SZ_ERROR_ARCHIVE, error);
        std::array<char, 1 << 16> bytes;
        bool ok = true;
        while ((result = unzReadCurrentFile(p.zip, bytes.data(), static_cast<unsigned>(bytes.size()))) > 0) {
            if (stopped()) { ok = false; break; }
            if (!writer(bytes.data(), static_cast<size_t>(result))) {
                error = "Could not write file from " + p.name;
                ok = false;
                break;
            }
        }
        int closed = unzCloseCurrentFile(p.zip);
        if (!ok) return false;
        return (result >= 0 && closed == UNZ_OK) || p.fail(SZ_ERROR_CRC, error);
    }
    UInt32 folder = p.db.FileToFolder[index];
    if (folder != p.block) {
        ISzAlloc_Free(&allocator, p.buffer);
        p.buffer = nullptr;
        p.bufferSize = 0;
        p.block = UINT32_MAX;
    }
    size_t offset = 0, size = 0;
    p.stream.stop = stop;
    SRes result = SzArEx_Extract(&p.db, &p.look.vt, static_cast<UInt32>(index), &p.block,
        &p.buffer, &p.bufferSize, &offset, &size, &allocator, &allocator);
    p.stream.stop = nullptr;
    if (result != SZ_OK) {
        ISzAlloc_Free(&allocator, p.buffer);
        p.buffer = nullptr;
        p.bufferSize = 0;
        p.block = UINT32_MAX;
        if (stopped()) return false;
        return p.fail(result, error);
    }
    if (stopped()) return false;
    for (size_t at = 0; at < size;) {
        if (stopped()) return false;
        size_t count = std::min(size - at, size_t(1 << 16));
        if (!writer(p.buffer + offset + at, count)) { error = "Could not write file from " + p.name; return false; }
        at += count;
    }
    return !stopped();
}

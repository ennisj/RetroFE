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
#include "Hash.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

// SHA-256 (FIPS 180-4) and MD5 (RFC 1321), written from the specifications.

namespace
{
    std::string toHex(const uint8_t* bytes, size_t count)
    {
        static const char* digits = "0123456789abcdef";
        std::string result;
        result.reserve(count * 2);
        for (size_t i = 0; i < count; i++) {
            result += digits[bytes[i] >> 4];
            result += digits[bytes[i] & 0x0F];
        }
        return result;
    }

    inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    const uint32_t sha256K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    const uint32_t md5K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };

    const int md5Shift[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };

    template <typename Hasher>
    std::string hashFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return "";
        }
        Hasher hasher;
        std::vector<char> buffer(1 << 16);
        while (file) {
            file.read(buffer.data(), buffer.size());
            hasher.update(buffer.data(), static_cast<size_t>(file.gcount()));
        }
        return hasher.hex();
    }
}

namespace Catalog
{

// --- SHA-256 -------------------------------------------------------------------

Sha256::Sha256()
{
    const uint32_t initial[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    std::memcpy(state_, initial, sizeof(state_));
}

void Sha256::block(const uint8_t* data)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t(data[i * 4]) << 24) | (uint32_t(data[i * 4 + 1]) << 16) | (uint32_t(data[i * 4 + 2]) << 8) | data[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + choose + sha256K[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + majority;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    length_ += size;
    while (size > 0) {
        size_t take = std::min(size, sizeof(buffer_) - buffered_);
        std::memcpy(buffer_ + buffered_, bytes, take);
        buffered_ += take;
        bytes += take;
        size -= take;
        if (buffered_ == sizeof(buffer_)) {
            block(buffer_);
            buffered_ = 0;
        }
    }
}

std::string Sha256::hex()
{
    uint64_t bits = length_ * 8;
    uint8_t padding = 0x80;
    update(&padding, 1);
    uint8_t zero = 0;
    while (buffered_ != 56) {
        update(&zero, 1);
    }
    uint8_t lengthBytes[8];
    for (int i = 0; i < 8; i++) {
        lengthBytes[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
    }
    update(lengthBytes, 8);

    uint8_t digest[32];
    for (int i = 0; i < 8; i++) {
        digest[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
    }
    return toHex(digest, sizeof(digest));
}

// --- MD5 -----------------------------------------------------------------------

Md5::Md5()
{
    const uint32_t initial[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    std::memcpy(state_, initial, sizeof(state_));
}

void Md5::block(const uint8_t* data)
{
    uint32_t m[16];
    for (int i = 0; i < 16; i++) {
        m[i] = uint32_t(data[i * 4]) | (uint32_t(data[i * 4 + 1]) << 8) | (uint32_t(data[i * 4 + 2]) << 16) | (uint32_t(data[i * 4 + 3]) << 24);
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) % 16; }
        else             { f = c ^ (b | ~d);       g = (7 * i) % 16; }
        f = f + a + md5K[i] + m[g];
        a = d; d = c; c = b;
        b = b + rotl(f, md5Shift[i]);
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
}

void Md5::update(const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    length_ += size;
    while (size > 0) {
        size_t take = std::min(size, sizeof(buffer_) - buffered_);
        std::memcpy(buffer_ + buffered_, bytes, take);
        buffered_ += take;
        bytes += take;
        size -= take;
        if (buffered_ == sizeof(buffer_)) {
            block(buffer_);
            buffered_ = 0;
        }
    }
}

std::string Md5::hex()
{
    uint64_t bits = length_ * 8;
    uint8_t padding = 0x80;
    update(&padding, 1);
    uint8_t zero = 0;
    while (buffered_ != 56) {
        update(&zero, 1);
    }
    uint8_t lengthBytes[8];
    for (int i = 0; i < 8; i++) {
        lengthBytes[i] = static_cast<uint8_t>(bits >> (i * 8));
    }
    update(lengthBytes, 8);

    uint8_t digest[16];
    for (int i = 0; i < 4; i++) {
        digest[i * 4] = static_cast<uint8_t>(state_[i]);
        digest[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 8);
        digest[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 16);
        digest[i * 4 + 3] = static_cast<uint8_t>(state_[i] >> 24);
    }
    return toHex(digest, sizeof(digest));
}

std::string sha256File(const std::string& path)
{
    return hashFile<Sha256>(path);
}

std::string md5File(const std::string& path)
{
    return hashFile<Md5>(path);
}

}

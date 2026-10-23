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

#include "LetterTiles.h"
#include <zlib.h>
#include <cstdint>
#include <cstring>

// A private static copy of the rasteriser ImGui ships (imgui_draw.cpp builds
// its own, also static, so the two never clash)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4456 4457 4701 4702 6011 6385 6386 28182)
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wcast-qual"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace ThemeEditor
{
    const char LETTER_TILE_ROWS[28] = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    namespace
    {
        void put32(std::string& out, uint32_t v)
        {
            out += static_cast<char>((v >> 24) & 0xFF);
            out += static_cast<char>((v >> 16) & 0xFF);
            out += static_cast<char>((v >> 8) & 0xFF);
            out += static_cast<char>(v & 0xFF);
        }

        void chunk(std::string& out, const char* type, const std::string& data)
        {
            put32(out, static_cast<uint32_t>(data.size()));
            std::string body(type, 4);
            body += data;
            out += body;
            uLong crc = crc32(0L, Z_NULL, 0);
            crc = crc32(crc, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()));
            put32(out, static_cast<uint32_t>(crc));
        }

        // Writes one glyph's coverage into the RGBA tile as colour x coverage,
        // clipped to the tile; where glyphs touch, the brighter pixel wins
        void blitGlyph(std::vector<unsigned char>& rgba, int tw, int th, const unsigned char* cov, int gw, int gh,
                       int dstX, int dstY, unsigned char r, unsigned char g, unsigned char b)
        {
            for (int y = 0; y < gh; ++y) {
                const int ty = dstY + y;
                if (ty < 0 || ty >= th) continue;
                for (int x = 0; x < gw; ++x) {
                    const int tx = dstX + x;
                    if (tx < 0 || tx >= tw) continue;
                    const unsigned char a = cov[static_cast<size_t>(y) * static_cast<size_t>(gw) + static_cast<size_t>(x)];
                    if (!a) continue;
                    const size_t o = (static_cast<size_t>(ty) * static_cast<size_t>(tw) + static_cast<size_t>(tx)) * 4;
                    if (a > rgba[o + 3]) {
                        rgba[o + 0] = r;
                        rgba[o + 1] = g;
                        rgba[o + 2] = b;
                        rgba[o + 3] = a;
                    }
                }
            }
        }
    }

    std::string encodePng(const unsigned char* rgba, int width, int height)
    {
        if (!rgba || width <= 0 || height <= 0) return std::string();
        const size_t stride = static_cast<size_t>(width) * 4;
        std::string raw;
        raw.reserve((stride + 1) * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y) {
            raw += '\0';   // filter: none
            raw.append(reinterpret_cast<const char*>(rgba + stride * static_cast<size_t>(y)), stride);
        }
        uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
        std::string packed(packedSize, '\0');
        if (compress2(reinterpret_cast<Bytef*>(&packed[0]), &packedSize, reinterpret_cast<const Bytef*>(raw.data()),
                      static_cast<uLong>(raw.size()), 9) != Z_OK) {
            return std::string();
        }
        packed.resize(packedSize);

        std::string png("\x89PNG\r\n\x1a\n", 8);
        std::string ihdr;
        put32(ihdr, static_cast<uint32_t>(width));
        put32(ihdr, static_cast<uint32_t>(height));
        ihdr += static_cast<char>(8);   // bit depth
        ihdr += static_cast<char>(6);   // colour type: RGBA
        ihdr += '\0';                   // compression
        ihdr += '\0';                   // filter
        ihdr += '\0';                   // interlace
        chunk(png, "IHDR", ihdr);
        chunk(png, "IDAT", packed);
        chunk(png, "IEND", std::string());
        return png;
    }

    bool renderLetterTiles(const std::string& ttfBytes, const LetterTileStyle& style,
                           std::vector<std::pair<std::string, std::string>>& out, std::string& error)
    {
        out.clear();
        error.clear();
        if (style.tileW < 8 || style.tileH < 54) { error = "degenerate tile size"; return false; }
        if (ttfBytes.size() < 12) { error = "font bytes empty or too short"; return false; }
        const unsigned char* data = reinterpret_cast<const unsigned char*>(ttfBytes.data());
        const int offset = stbtt_GetFontOffsetForIndex(data, 0);
        stbtt_fontinfo font;
        if (offset < 0 || !stbtt_InitFont(&font, data, offset)) { error = "can't parse the font"; return false; }

        const int rows = 27;
        const float cellH = static_cast<float>(style.tileH) / static_cast<float>(rows);
        const float dimPx = cellH * 0.66f;   // resting glyph height
        const float hiPx = cellH * 0.92f;    // the highlighted letter is bigger

        for (int hi = 0; hi < rows; ++hi) {
            std::vector<unsigned char> rgba(static_cast<size_t>(style.tileW) * static_cast<size_t>(style.tileH) * 4, 0);
            for (int row = 0; row < rows; ++row) {
                const bool isHi = row == hi;
                const float scale = stbtt_ScaleForPixelHeight(&font, isHi ? hiPx : dimPx);
                int gw = 0, gh = 0, xo = 0, yo = 0;
                unsigned char* cov = stbtt_GetCodepointBitmap(&font, scale, scale, static_cast<int>(LETTER_TILE_ROWS[row]),
                                                              &gw, &gh, &xo, &yo);
                if (!cov) continue;   // a missing glyph leaves its row blank
                const int cx = (style.tileW - gw) / 2;
                const int cy = static_cast<int>(cellH * static_cast<float>(row) + (cellH - static_cast<float>(gh)) * 0.5f);
                blitGlyph(rgba, style.tileW, style.tileH, cov, gw, gh, cx, cy,
                          isHi ? style.hiR : style.dimR, isHi ? style.hiG : style.dimG, isHi ? style.hiB : style.dimB);
                stbtt_FreeBitmap(cov, nullptr);
            }
            std::string png = encodePng(rgba.data(), style.tileW, style.tileH);
            if (png.empty()) { error = "PNG encoding failed"; out.clear(); return false; }
            out.emplace_back(std::string(1, LETTER_TILE_ROWS[hi]) + ".png", std::move(png));
        }
        // Digits highlight the '#' row
        const std::string hash = out[0].second;
        for (char d = '0'; d <= '9'; ++d) out.emplace_back(std::string(1, d) + ".png", hash);
        return true;
    }
}

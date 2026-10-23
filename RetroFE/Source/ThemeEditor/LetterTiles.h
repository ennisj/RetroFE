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

// The Game Letters tiles: the alphabet-column art the firstLetter media slot
// swaps per selection. Each tile is the full column (#, A..Z) with one letter
// highlighted, rendered from a TrueType font.
//
// This engine looks a tile up by the first character of the game's title
// exactly as it is (the file-name match is case-sensitive on every platform),
// so digits get their own tiles (0.png..9.png, each highlighting '#') next to
// #.png and A.png..Z.png, and default.png (nothing highlighted) is what the
// engine falls back to for any other character, lowercase letters included.
// Lowercase copies (a.png) aren't written: on Windows they'd be the same
// file as A.png.
//
// Rasterised with the stb_truetype that ships with Dear ImGui
// (imstb_truetype.h, a private static copy) and written as PNG with zlib.
// Bytes in, bytes out: no SDL, no files.

#include <string>
#include <utility>
#include <vector>

namespace ThemeEditor
{
    struct LetterTileStyle
    {
        int tileW = 54, tileH = 1080;
        unsigned char dimR = 0x8a, dimG = 0x8d, dimB = 0xa0;   // resting letters
        unsigned char hiR = 0xe7, hiG = 0xc8, hiB = 0x4c;      // the highlighted letter
    };

    // The column's rows, top to bottom: '#' then A..Z
    extern const char LETTER_TILE_ROWS[28];

    // Renders the set: ("#.png", png), ("A.png", ...) .. ("Z.png", ...), then
    // ("0.png", ...) .. ("9.png", ...), then ("default.png", ...): 38 files.
    // Deterministic. False with a reason
    // when the font can't be parsed or the style is degenerate.
    bool renderLetterTiles(const std::string& ttfBytes, const LetterTileStyle& style,
                           std::vector<std::pair<std::string, std::string>>& out, std::string& error);

    // An 8-bit RGBA image as a PNG file's bytes; empty on failure
    std::string encodePng(const unsigned char* rgba, int width, int height);
}

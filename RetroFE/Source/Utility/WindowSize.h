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

#include <algorithm>

// horizontal = auto / vertical = auto (a bare install's built-in settings):
// a window as large as fits comfortably on the desktop rather than a fixed
// 1280 x 720, so the first screens a new owner sees aren't drawn at two
// thirds of their size on a 1080p screen.
namespace WindowSize
{
    struct Size
    {
        int width = 0;
        int height = 0;
    };

    // The largest 16:9 window that fits in share of the display's usable
    // area (without the taskbar), keeping room for a title bar of titleBar
    // pixels; never smaller than 640 x 360, nor larger than the area.
    inline Size automatic(int usableWidth, int usableHeight, int titleBar = 32, float share = 0.9f)
    {
        Size size;
        if (usableWidth <= 0 || usableHeight <= 0) {
            size.width = 1280;
            size.height = 720;
            return size;
        }
        int roomWidth = static_cast<int>(usableWidth * share);
        int roomHeight = static_cast<int>((usableHeight - titleBar) * share);
        size.width = std::min(roomWidth, roomHeight * 16 / 9);
        size.width -= size.width % 16;   // whole 16:9 steps, so the height is whole too
        size.width = std::max(size.width, 640);
        size.height = size.width * 9 / 16;
        size.width = std::min(size.width, usableWidth);
        size.height = std::min(size.height, usableHeight);
        return size;
    }
}

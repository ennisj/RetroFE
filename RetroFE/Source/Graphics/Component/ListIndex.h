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

// Index arithmetic of a menu that wraps around (ScrollingList), kept apart
// from it (no SDL) so retrofe_tests checks it. i may be larger than size: a
// menu's selected slot can lie past the end of a short list (Aeon Nox's video
// list selects its 5th slot and may hold 3 videos).
inline size_t loopIncrement(size_t offset, size_t i, size_t size)
{
    if (size == 0) return 0;
    return (offset % size + i % size) % size;
}

inline size_t loopDecrement(size_t offset, size_t i, size_t size)
{
    if (size == 0) return 0;
    return (offset % size + size - i % size) % size;
}

// The item `offset` places from `index` (negative: before it), wrapping
// round; what a reloadable's selectedOffset shows
inline size_t itemAtOffset(size_t index, int offset, size_t size)
{
    if (offset >= 0) return loopIncrement(index, static_cast<size_t>(offset), size);
    // -offset without overflow for INT_MIN
    return loopDecrement(index, static_cast<size_t>(-(static_cast<long long>(offset))), size);
}

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

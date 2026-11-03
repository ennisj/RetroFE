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
#include "MousePolicy.h"

namespace MousePolicy
{
    bool grabsPointer(bool fullscreen, bool captureMouse, bool staysInBackground)
    {
        return fullscreen && captureMouse && !staysInBackground;
    }

    bool mayMovePointer(bool fullscreen, bool staysInBackground, int x, int y, const Bounds& bounds)
    {
        return fullscreen && !staysInBackground && bounds.width > 0 && bounds.height > 0
            && x >= bounds.x && y >= bounds.y
            && static_cast<long long>(x) < static_cast<long long>(bounds.x) + bounds.width
            && static_cast<long long>(y) < static_cast<long long>(bounds.y) + bounds.height;
    }

    std::pair<std::string, std::string> displayOption(int index, const Bounds& bounds)
    {
        return { std::to_string(index), std::to_string(index + 1) + ": "
            + std::to_string(bounds.width) + " x " + std::to_string(bounds.height)
            + (bounds.x == 0 && bounds.y == 0 ? " (main)" : "") };
    }
}

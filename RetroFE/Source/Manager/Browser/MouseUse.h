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

#include <cstdint>

// Whether someone is using a mouse in Manage, so the file browser may offer
// the system's file dialog (which needs one: a controller can't close it).
// The manager feeds it mouse events only while it's open. A mouse counts once
// a button was pressed in Manage, and stays counted while it's been moved or
// pressed in the last little while. Motion just after RetroFE moved the
// pointer itself (it does when its window opens and after every game) is
// ignored, and touch never counts. A trackball, spinner or light gun that
// SDL sees as a mouse still looks like one when its button is pressed in
// Manage (docs/VERIFY.md B2).
namespace Browser
{
    class MouseUse
    {
    public:
        static constexpr std::uint32_t warpQuietMs = 1000;    // motion ignored after a warp
        static constexpr std::uint32_t recentMs = 15000;      // how long a real event keeps it present

        // RetroFE moved the pointer (SDL_WarpMouseInWindow), at now (ms)
        void warped(std::uint32_t now) { warpedAt_ = now; warpedEver_ = true; }

        void motion(std::uint32_t now, int dx, int dy, bool touch)
        {
            if (touch || (dx == 0 && dy == 0) || afterWarp(now)) {
                return;
            }
            lastReal_ = now;
            realEver_ = true;
        }

        void press(std::uint32_t now, bool touch)
        {
            if (touch) {
                return;
            }
            pressed_ = true;
            lastReal_ = now;
            realEver_ = true;
        }

        bool present(std::uint32_t now) const
        {
            return pressed_ && realEver_ && now - lastReal_ <= recentMs;
        }

        void reset() { *this = MouseUse{}; }

    private:
        bool afterWarp(std::uint32_t now) const { return warpedEver_ && now - warpedAt_ < warpQuietMs; }

        bool pressed_ = false;
        bool realEver_ = false;
        bool warpedEver_ = false;
        std::uint32_t lastReal_ = 0;
        std::uint32_t warpedAt_ = 0;
    };
}

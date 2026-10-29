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

namespace Manager
{
    // Remember the hub's first-run state across any number of deeper screens.
    class HubProgress
    {
    public:
        bool focusSystem(bool onHub, bool hasLaunchers)
        {
            if (!onHub) return false;
            bool focus = seen_ && !hadLaunchers_ && hasLaunchers;
            seen_ = true;
            hadLaunchers_ = hasLaunchers;
            return focus;
        }
        void reset() { seen_ = false; hadLaunchers_ = false; }
    private:
        bool seen_ = false;
        bool hadLaunchers_ = false;
    };
}

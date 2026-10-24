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

#include "../../../Control/InputToken.h"
#include <SDL2/SDL.h>
#include <map>
#include <utility>

namespace Manager
{
    // SDL events as inputs going down or up, for InputCapture. Inputs are
    // "any joystick" (joyA, not joy0A), as a cabinet's controls.conf usually
    // says. Sticks and axes count past half way (from where they rest, for
    // triggers that rest at one end) and are let go below a quarter.
    class CaptureEvents
    {
    public:
        struct Event
        {
            enum class Type { None, Down, Up } type = Type::None;
            InputToken::Input input;
        };

        Event read(const SDL_Event& e);

        // Events a capture keeps from the screen: keys, typing, joystick,
        // controller and mouse buttons (the mouse isn't captured)
        static bool isInput(const SDL_Event& e);

        void reset() { axes_.clear(); hats_.clear(); }

    private:
        Event axis(InputToken::Kind kind, SDL_JoystickID which, int axis, int delta);

        std::map<std::pair<int, int>, int> axes_;   // (kind * 1000 + joystick, axis) -> -1, 0, +1
        std::map<std::pair<int, int>, int> hats_;   // (joystick, hat) -> direction held
    };
}

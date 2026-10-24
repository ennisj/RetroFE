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
#include "ControllerHandlers.h"

ControllerButtonHandler::ControllerButtonHandler(SDL_JoystickID joyid, Uint8 button)
    : joyid_(joyid)
    , button_(button)
{
}

bool ControllerButtonHandler::update(SDL_Event& e)
{
    if (e.type != SDL_CONTROLLERBUTTONDOWN && e.type != SDL_CONTROLLERBUTTONUP) return false;
    if ((joyid_ != -1 && e.cbutton.which != joyid_) || e.cbutton.button != button_) return false;
    pressed_ = e.type == SDL_CONTROLLERBUTTONDOWN;
    return true;
}

ControllerAxisHandler::ControllerAxisHandler(SDL_JoystickID joyid, Uint8 axis, Sint16 min, Sint16 max)
    : joyid_(joyid)
    , axis_(axis)
    , min_(min)
    , max_(max)
{
}

bool ControllerAxisHandler::update(SDL_Event& e)
{
    if (e.type != SDL_CONTROLLERAXISMOTION) return false;
    if ((joyid_ != -1 && e.caxis.which != joyid_) || e.caxis.axis != axis_) return false;
    pressed_ = min_ <= e.caxis.value && e.caxis.value <= max_;
    return true;
}

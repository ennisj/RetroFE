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
#include "PadInputs.h"
#include "../../../Control/InputToken.h"
#include <SDL2/SDL.h>
#include <algorithm>

namespace Manager
{

namespace
{
    // The joystick input a controller binding reads, or Invalid
    InputToken::Input bound(const SDL_GameControllerButtonBind& bind, int sign)
    {
        InputToken::Input input;
        if (bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON) {
            input.kind = InputToken::Kind::JoyButton;
            input.index = bind.value.button;
        }
        else if (bind.bindType == SDL_CONTROLLER_BINDTYPE_HAT) {
            input.kind = InputToken::Kind::JoyHat;
            input.index = bind.value.hat.hat;
            input.value = bind.value.hat.hat_mask;
        }
        else if (bind.bindType == SDL_CONTROLLER_BINDTYPE_AXIS && sign != 0) {
            input.kind = InputToken::Kind::JoyAxis;
            input.index = bind.value.axis;
            input.value = sign;
        }
        return input;
    }

    bool isTrigger(int axis)
    {
        return axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    }
}

std::vector<std::string> padSameInputs(const std::string& token)
{
    InputToken::Input in = InputToken::parse(token);
    in.joystick = -1;   // compared as "any pad"
    std::vector<std::string> names;
    auto add = [&](InputToken::Input input) {
        input.joystick = -1;
        std::string name = InputToken::format(input);
        if (input.kind != InputToken::Kind::Invalid && !name.empty() && !InputToken::same(input, in)
            && std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    };

    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        SDL_GameController* pad = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (!pad) {
            continue;   // not opened (UserInput opens the ones it has a slot for)
        }
        switch (in.kind) {
        case InputToken::Kind::ControllerButton:
            add(bound(SDL_GameControllerGetBindForButton(pad, static_cast<SDL_GameControllerButton>(in.index)), 0));
            break;
        case InputToken::Kind::ControllerAxis:
            add(bound(SDL_GameControllerGetBindForAxis(pad, static_cast<SDL_GameControllerAxis>(in.index)), in.value));
            break;
        case InputToken::Kind::JoyButton:
        case InputToken::Kind::JoyHat:
        case InputToken::Kind::JoyAxis:
            // Every controller button or axis bound to this joystick input
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
                InputToken::Input raw = bound(SDL_GameControllerGetBindForButton(pad, static_cast<SDL_GameControllerButton>(b)), in.value);
                if (raw.kind == in.kind && raw.index == in.index && (in.kind != InputToken::Kind::JoyHat || raw.value == in.value)) {
                    InputToken::Input named;
                    named.kind = InputToken::Kind::ControllerButton;
                    named.index = b;
                    add(named);
                }
            }
            for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX && in.kind == InputToken::Kind::JoyAxis; a++) {
                InputToken::Input raw = bound(SDL_GameControllerGetBindForAxis(pad, static_cast<SDL_GameControllerAxis>(a)), in.value);
                if (raw.kind == InputToken::Kind::JoyAxis && raw.index == in.index && (!isTrigger(a) || in.value > 0)) {
                    InputToken::Input named;
                    named.kind = InputToken::Kind::ControllerAxis;
                    named.index = a;
                    named.value = in.value;
                    add(named);
                }
            }
            break;
        default:
            break;
        }
    }
    return names;
}

}

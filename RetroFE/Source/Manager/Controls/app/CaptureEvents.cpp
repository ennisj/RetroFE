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
#include "CaptureEvents.h"
#include "../InputCapture.h"
#include <cstdlib>

namespace Manager
{

bool CaptureEvents::isInput(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_KEYDOWN: case SDL_KEYUP: case SDL_TEXTINPUT: case SDL_TEXTEDITING:
    case SDL_JOYAXISMOTION: case SDL_JOYBALLMOTION: case SDL_JOYHATMOTION:
    case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP:
    case SDL_CONTROLLERAXISMOTION: case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL:
        return true;
    default:
        return false;
    }
}

CaptureEvents::Event CaptureEvents::axis(InputToken::Kind kind, SDL_JoystickID which, int axis, int delta)
{
    Event event;
    int& held = axes_[{ static_cast<int>(kind) * 1000 + which, axis }];
    int now = InputCapture::axisState(held, delta);
    if (now == held) {
        return event;
    }
    event.input.kind = kind;
    event.input.index = axis;
    event.input.value = held != 0 ? held : now;
    event.type = held != 0 ? Event::Type::Up : Event::Type::Down;
    held = now;
    return event;
}

int CaptureEvents::restOf(SDL_JoystickID which, int axis, Sint16 value)
{
    auto found = rests_.find({ which, axis });
    if (found != rests_.end()) {
        return found->second;
    }
    // Where SDL saw it at first; without that (some drivers), a value at
    // either end when first seen is a trigger resting there
    Sint16 initial = 0;
    SDL_Joystick* joystick = SDL_JoystickFromInstanceID(which);
    int rest = 0;
    if (joystick && SDL_JoystickGetAxisInitialState(joystick, axis, &initial)) {
        rest = initial;
    }
    else if (value >= 24000 || value <= -24000) {
        rest = value;
    }
    rests_[{ which, axis }] = rest;
    return rest;
}

void CaptureEvents::start()
{
    axes_.clear();
    rests_.clear();
    hats_.clear();
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
        SDL_Joystick* joystick = id >= 0 ? SDL_JoystickFromInstanceID(id) : nullptr;
        if (!joystick) {
            continue;   // not opened: its events don't come
        }
        for (int a = 0; a < SDL_JoystickNumAxes(joystick); a++) {
            Sint16 value = SDL_JoystickGetAxis(joystick, a);
            axes_[{ static_cast<int>(InputToken::Kind::JoyAxis) * 1000 + id, a }] = InputCapture::axisState(0, value - restOf(id, a, value));
        }
        for (int h = 0; h < SDL_JoystickNumHats(joystick); h++) {
            hats_[{ id, h }] = SDL_JoystickGetHat(joystick, h);
        }
        if (SDL_GameController* pad = SDL_GameControllerFromInstanceID(id)) {
            for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) {
                Sint16 value = SDL_GameControllerGetAxis(pad, static_cast<SDL_GameControllerAxis>(a));
                axes_[{ static_cast<int>(InputToken::Kind::ControllerAxis) * 1000 + id, a }] = InputCapture::axisState(0, value);
            }
        }
    }
}
CaptureEvents::Event CaptureEvents::read(const SDL_Event& e)
{
    Event event;
    switch (e.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (e.key.repeat) {
            break;
        }
        const char* name = SDL_GetScancodeName(e.key.keysym.scancode);
        if (!name || !*name) {
            break;
        }
        event.input.kind = InputToken::Kind::Keyboard;
        event.input.key = name;
        event.type = e.type == SDL_KEYDOWN ? Event::Type::Down : Event::Type::Up;
        break;
    }
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        event.input.kind = InputToken::Kind::JoyButton;
        event.input.index = e.jbutton.button;
        event.type = e.type == SDL_JOYBUTTONDOWN ? Event::Type::Down : Event::Type::Up;
        break;
    case SDL_JOYHATMOTION: {
        int& held = hats_[{ e.jhat.which, e.jhat.hat }];
        event.input.kind = InputToken::Kind::JoyHat;
        event.input.index = e.jhat.hat;
        if (e.jhat.value == SDL_HAT_CENTERED) {
            if (held == 0) {
                break;
            }
            event.input.value = held;
            event.type = Event::Type::Up;
            held = 0;
        }
        else if (held == 0) {
            event.input.value = e.jhat.value;
            event.type = Event::Type::Down;
            held = e.jhat.value;
        }
        break;
    }
    case SDL_JOYAXISMOTION: {
        // Measured from where it rests: a trigger read as a joystick axis rests at one end
        event = axis(InputToken::Kind::JoyAxis, e.jaxis.which, e.jaxis.axis, e.jaxis.value - restOf(e.jaxis.which, e.jaxis.axis, e.jaxis.value));

        break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        event.input.kind = InputToken::Kind::ControllerButton;
        event.input.index = e.cbutton.button;
        event.type = e.type == SDL_CONTROLLERBUTTONDOWN ? Event::Type::Down : Event::Type::Up;
        break;
    case SDL_CONTROLLERAXISMOTION:
        event = axis(InputToken::Kind::ControllerAxis, e.caxis.which, e.caxis.axis, e.caxis.value);
        break;
    default:
        break;
    }
    // Something InputToken can't write (a key without a usable name, a button SDL added later)
    if (event.type != Event::Type::None && InputToken::format(event.input).empty()) {
        event.type = Event::Type::None;
    }
    return event;
}

}

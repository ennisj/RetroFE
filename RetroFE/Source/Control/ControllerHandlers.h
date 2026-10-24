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

#include "InputHandler.h"

// SDL game controller inputs (joyA, joyDPUp, joyLeftY-, joyRightTrigger...):
// the same button whatever the pad, through SDL's controller mappings.
// joyid is the slot UserInput maps the controller to (joy0..), or -1 for any.

class ControllerButtonHandler : public InputHandler
{
public:
    ControllerButtonHandler(SDL_JoystickID joyid, Uint8 button);
    bool update(SDL_Event& e) override;
    bool pressed() override { return pressed_; }
    void reset() override { pressed_ = false; }
    void updateKeystate() override {}

private:
    SDL_JoystickID joyid_;
    Uint8 button_;
    bool pressed_ = false;
};

// Pressed while the axis is between min and max (a stick past the dead zone
// one way, or a trigger past it)
class ControllerAxisHandler : public InputHandler
{
public:
    ControllerAxisHandler(SDL_JoystickID joyid, Uint8 axis, Sint16 min, Sint16 max);
    bool update(SDL_Event& e) override;
    bool pressed() override { return pressed_; }
    void reset() override { pressed_ = false; }
    void updateKeystate() override {}

private:
    SDL_JoystickID joyid_;
    Uint8 axis_;
    Sint16 min_;
    Sint16 max_;
    bool pressed_ = false;
};

// A joystick input from the built-in defaults' joyUnmapped... form: it acts
// only on joysticks SDL has no controller mapping for (arcade encoders),
// never on a pad that also reports a controller button, whose numbering
// differs (joyButton1 is A on some). UserInput says whether the event being
// handled comes from such a pad. Owns inner.
class UnmappedOnlyHandler : public InputHandler
{
public:
    UnmappedOnlyHandler(InputHandler* inner, const bool* fromGameController)
        : inner_(inner), fromGameController_(fromGameController) {}
    ~UnmappedOnlyHandler() override { delete inner_; }
    bool update(SDL_Event& e) override
    {
        bool joystick = e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP || e.type == SDL_JOYHATMOTION
            || e.type == SDL_JOYAXISMOTION;
        if (joystick && *fromGameController_) return false;
        return inner_->update(e);
    }
    bool pressed() override { return inner_->pressed(); }
    void reset() override { inner_->reset(); }
    void updateKeystate() override { inner_->updateKeystate(); }

private:
    InputHandler* inner_;
    const bool* fromGameController_;
};
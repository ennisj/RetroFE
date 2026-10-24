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

#include <functional>
#include <string>
#include <vector>

// The inputs a controls.conf value lists, e.g. "up = Up,joyDPUp,joyLeftY-,joyHat0Up",
// as RetroFE reads them (docs/CONTROLS.md). SDL-free, so the Manage screens and
// the tests use the same rules as UserInput.
//
//   Keyboard     an SDL scancode name: "Up", "Return", "Keypad 4", "F10"
//   Mouse        mouseButtonLeft / Middle / Right / X1 / X2
//   Joystick     joy[N]ButtonB, joy[N]HatH<Dir>, joy[N]AxisA+ / -
//                (Dir: Up, Down, Left, Right, LeftUp, LeftDown, RightUp, RightDown)
//   Controller   joy[N]<SDL game controller name>: buttons A, B, X, Y, Back,
//                Guide, Start, LeftStick, RightStick, LeftShoulder,
//                RightShoulder, DPUp, DPDown, DPLeft, DPRight, Misc1,
//                Paddle1-4, Touchpad; sticks LeftX, LeftY, RightX, RightY
//                with + or -; triggers LeftTrigger, RightTrigger (or with +)
//
// N (one digit) picks the Nth joystick connected; without it any one counts.
// The joy/mouse part is case-insensitive. The controller names are SDL's
// (SDL_GameControllerGetStringForButton / ForAxis) spelt in CamelCase.
namespace InputToken
{
    enum class Kind
    {
        Invalid,
        Keyboard,
        Mouse,
        JoyButton,
        JoyHat,
        JoyAxis,
        ControllerButton,
        ControllerAxis
    };

    // SDL's values, without needing SDL here
    enum Hat { HatUp = 1, HatRight = 2, HatDown = 4, HatLeft = 8,
               HatRightUp = 3, HatRightDown = 6, HatLeftUp = 9, HatLeftDown = 12 };
    enum Mouse { MouseLeft = 1, MouseMiddle = 2, MouseRight = 3, MouseX1 = 4, MouseX2 = 5 };
    // SDL_GameControllerAxis: 0-3 sticks, 4-5 triggers
    enum { AxisLeftX = 0, AxisLeftY = 1, AxisRightX = 2, AxisRightY = 3, AxisLeftTrigger = 4, AxisRightTrigger = 5 };

    struct Input
    {
        Kind kind = Kind::Invalid;
        int joystick = -1;    // joy0..joy9; -1 for any
        int index = 0;        // button, hat, axis or mouse button number
        int value = 0;        // JoyHat: the Hat direction; axes: +1 or -1
        std::string key;      // Keyboard: the name as written
        std::string problem;  // Invalid: why, as a sentence
    };

    // Whether a keyboard name is one RetroFE knows. RetroFE sets this to
    // SDL_GetScancodeFromName; until then (tests) any other token is taken as a key.
    void setKeyNameCheck(std::function<bool(const std::string&)> check);

    Input parse(const std::string& token);

    // The token for an input, as RetroFE writes and reads it ("" for Invalid)
    std::string format(const Input& input);

    // A short description for the screens, e.g. "Keyboard Return",
    // "Controller A", "Controller left stick up", "Joystick 2 button 3"
    std::string describe(const Input& input);

    // True when both name the same physical input (case and spelling aside)
    bool same(const Input& a, const Input& b);

    // A controls.conf value split into tokens, as UserInput reads it: at
    // commas, trimmed; an empty piece in a non-empty value is the "," key
    std::vector<std::string> split(const std::string& value);
    // Tokens back to a value that split() reads the same way
    std::string join(const std::vector<std::string>& tokens);
}

// The controls RetroFE reads from controls*.conf, for the Controls screens
namespace ControlList
{
    struct Control
    {
        std::string name;     // the controls.conf key, e.g. "letterUp"
        std::string label;    // e.g. "Letter up"
        std::string group;    // e.g. "Navigation"
        std::string help;     // what it does
        bool required = false;      // the Controls screen never saves it without an input
        bool combo = false;         // two inputs: hold the first, press the second
        bool keyboardOnly = false;  // only keyboard keys work (manager)
    };

    // Every control, in the order the screens list them
    const std::vector<Control>& all();
    const Control* find(const std::string& name);

    // What RetroFE uses when no file sets the control: the built-in controls
    // (Builtin::controls; "F10" for manager), "" for none
    std::string builtinDefault(const std::string& name);
}

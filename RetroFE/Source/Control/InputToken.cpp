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
#include "InputToken.h"
#include "../Builtin/Builtin.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
    std::function<bool(const std::string&)>& keyNameCheck()
    {
        static std::function<bool(const std::string&)> check;
        return check;
    }

    std::string lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string trim(const std::string& text)
    {
        size_t start = text.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            return "";
        }
        size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(start, end - start + 1);
    }

    bool allDigits(const std::string& text)
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    // SDL_GameControllerButton order, as SDL names them (CamelCase) and as the screens say them
    struct Name
    {
        const char* token;
        const char* words;
    };
    const Name controllerButtons[] = {
        { "A", "A" }, { "B", "B" }, { "X", "X" }, { "Y", "Y" },
        { "Back", "Back" }, { "Guide", "Guide" }, { "Start", "Start" },
        { "LeftStick", "left stick press" }, { "RightStick", "right stick press" },
        { "LeftShoulder", "left shoulder" }, { "RightShoulder", "right shoulder" },
        { "DPUp", "D-pad up" }, { "DPDown", "D-pad down" }, { "DPLeft", "D-pad left" }, { "DPRight", "D-pad right" },
        { "Misc1", "Misc" },
        { "Paddle1", "paddle 1" }, { "Paddle2", "paddle 2" }, { "Paddle3", "paddle 3" }, { "Paddle4", "paddle 4" },
        { "Touchpad", "touchpad" },
    };
    // SDL_GameControllerAxis order
    const char* const controllerAxes[] = { "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };

    const Name hats[] = {
        { "Up", "up" }, { "Down", "down" }, { "Left", "left" }, { "Right", "right" },
        { "LeftUp", "up-left" }, { "LeftDown", "down-left" }, { "RightUp", "up-right" }, { "RightDown", "down-right" },
    };
    const int hatValues[] = { InputToken::HatUp, InputToken::HatDown, InputToken::HatLeft, InputToken::HatRight,
                              InputToken::HatLeftUp, InputToken::HatLeftDown, InputToken::HatRightUp, InputToken::HatRightDown };

    const char* const mouseButtons[] = { "", "Left", "Middle", "Right", "X1", "X2" };

    InputToken::Input invalid(const std::string& problem)
    {
        InputToken::Input input;
        input.problem = problem;
        return input;
    }

    bool isTrigger(int axis)
    {
        return axis == InputToken::AxisLeftTrigger || axis == InputToken::AxisRightTrigger;
    }

    InputToken::Input parseJoy(const std::string& token, std::string rest)
    {
        InputToken::Input input;
        if (!rest.empty() && std::isdigit(static_cast<unsigned char>(rest[0]))) {
            input.joystick = rest[0] - '0';
            rest.erase(0, 1);
        }
        if (rest.empty()) {
            return invalid("\"" + token + "\" doesn't say which button.");
        }

        if (rest.compare(0, 6, "button") == 0) {
            std::string number = rest.substr(6);
            if (!allDigits(number) || number.size() > 3) {
                return invalid("\"" + token + "\" needs a button number, e.g. joyButton0.");
            }
            input.kind = InputToken::Kind::JoyButton;
            input.index = std::stoi(number);
            return input;
        }
        if (rest.compare(0, 3, "hat") == 0) {
            std::string hat = rest.substr(3);
            if (hat.empty() || !std::isdigit(static_cast<unsigned char>(hat[0]))) {
                return invalid("\"" + token + "\" needs a hat number and direction, e.g. joyHat0Up.");
            }
            input.index = hat[0] - '0';
            std::string direction = hat.substr(1);
            for (size_t i = 0; i < sizeof(hats) / sizeof(hats[0]); i++) {
                if (direction == lower(hats[i].token)) {
                    input.kind = InputToken::Kind::JoyHat;
                    input.value = hatValues[i];
                    return input;
                }
            }
            return invalid("\"" + token + "\" needs a direction: Up, Down, Left, Right, LeftUp, LeftDown, RightUp or RightDown.");
        }
        if (rest.compare(0, 4, "axis") == 0) {
            std::string axis = rest.substr(4);
            int sign = 0;
            // The sign comes last (joyAxis1-); RetroFE has always read it anywhere
            if (!axis.empty() && (axis.back() == '+' || axis.back() == '-')) {
                sign = axis.back() == '+' ? 1 : -1;
                axis.pop_back();
            }
            else if (!axis.empty() && (axis.front() == '+' || axis.front() == '-')) {
                sign = axis.front() == '+' ? 1 : -1;
                axis.erase(0, 1);
            }
            if (!allDigits(axis) || axis.size() > 2) {
                return invalid("\"" + token + "\" needs an axis number and + or -, e.g. joyAxis1-.");
            }
            if (sign == 0) {
                return invalid("\"" + token + "\" needs + or - after the axis number.");
            }
            input.kind = InputToken::Kind::JoyAxis;
            input.index = std::stoi(axis);
            input.value = sign;
            return input;
        }

        // A game controller's button or axis, by SDL's name
        int sign = 0;
        if (rest.back() == '+' || rest.back() == '-') {
            sign = rest.back() == '+' ? 1 : -1;
            rest.pop_back();
        }
        for (size_t i = 0; i < sizeof(controllerButtons) / sizeof(controllerButtons[0]); i++) {
            if (rest == lower(controllerButtons[i].token)) {
                if (sign != 0) {
                    return invalid("\"" + token + "\" is a button: it has no + or -.");
                }
                input.kind = InputToken::Kind::ControllerButton;
                input.index = static_cast<int>(i);
                return input;
            }
        }
        for (int i = 0; i < 6; i++) {
            if (rest == lower(controllerAxes[i])) {
                if (isTrigger(i)) {
                    if (sign < 0) {
                        return invalid("\"" + token + "\" is a trigger: it only goes one way (no -).");
                    }
                    sign = 1;
                }
                else if (sign == 0) {
                    return invalid("\"" + token + "\" needs + or - for the direction, e.g. joyLeftY-.");
                }
                input.kind = InputToken::Kind::ControllerAxis;
                input.index = i;
                input.value = sign;
                return input;
            }
        }
        return invalid("\"" + token + "\" isn't a joystick or controller input RetroFE knows.");
    }
}

namespace InputToken
{

void setKeyNameCheck(std::function<bool(const std::string&)> check)
{
    keyNameCheck() = std::move(check);
}

Input parse(const std::string& raw)
{
    std::string token = trim(raw);
    if (token.empty()) {
        return invalid("An empty input.");
    }
    std::string low = lower(token);

    if (low.compare(0, 5, "mouse") == 0) {
        std::string rest = low.substr(5);
        if (rest.compare(0, 6, "button") == 0) {
            std::string which = rest.substr(6);
            for (int i = 1; i <= 5; i++) {
                if (which == lower(mouseButtons[i])) {
                    Input input;
                    input.kind = Kind::Mouse;
                    input.index = i;
                    return input;
                }
            }
        }
        return invalid("\"" + token + "\" isn't a mouse button: mouseButtonLeft, Middle, Right, X1 or X2.");
    }
    if (low.compare(0, 3, "joy") == 0) {
        return parseJoy(token, low.substr(3));
    }

    if (token.find('#') != std::string::npos) {
        return invalid("\"#\" starts a comment in controls.conf, so it can't be used.");
    }
    const auto& check = keyNameCheck();
    if (check && !check(token)) {
        return invalid("\"" + token + "\" isn't a key name RetroFE knows.");
    }
    Input input;
    input.kind = Kind::Keyboard;
    input.key = token;
    return input;
}

std::string format(const Input& input)
{
    std::string joy = "joy" + (input.joystick >= 0 ? std::to_string(input.joystick) : std::string());
    switch (input.kind) {
    case Kind::Keyboard:
        return input.key;
    case Kind::Mouse:
        return input.index >= 1 && input.index <= 5 ? std::string("mouseButton") + mouseButtons[input.index] : "";
    case Kind::JoyButton:
        return joy + "Button" + std::to_string(input.index);
    case Kind::JoyHat:
        for (size_t i = 0; i < sizeof(hats) / sizeof(hats[0]); i++) {
            if (hatValues[i] == input.value) {
                return joy + "Hat" + std::to_string(input.index) + hats[i].token;
            }
        }
        return "";
    case Kind::JoyAxis:
        return joy + "Axis" + std::to_string(input.index) + (input.value < 0 ? "-" : "+");
    case Kind::ControllerButton:
        if (input.index < 0 || input.index >= static_cast<int>(sizeof(controllerButtons) / sizeof(controllerButtons[0]))) {
            return "";
        }
        return joy + controllerButtons[input.index].token;
    case Kind::ControllerAxis:
        if (input.index < 0 || input.index > 5) {
            return "";
        }
        return joy + controllerAxes[input.index] + (isTrigger(input.index) ? "" : input.value < 0 ? "-" : "+");
    case Kind::Invalid:
        break;
    }
    return "";
}

std::string describe(const Input& input)
{
    std::string number = input.joystick >= 0 ? " " + std::to_string(input.joystick) : "";
    switch (input.kind) {
    case Kind::Keyboard:
        return "Keyboard " + input.key;
    case Kind::Mouse:
        if (input.index < 1 || input.index > 5) {
            return "Mouse button " + std::to_string(input.index);
        }
        return std::string("Mouse ") + lower(mouseButtons[input.index]) + " button";
    case Kind::JoyButton:
        return "Joystick" + number + " button " + std::to_string(input.index);
    case Kind::JoyHat:
        for (size_t i = 0; i < sizeof(hats) / sizeof(hats[0]); i++) {
            if (hatValues[i] == input.value) {
                return "Joystick" + number + " hat " + std::to_string(input.index) + " " + hats[i].words;
            }
        }
        return "Joystick" + number + " hat " + std::to_string(input.index);
    case Kind::JoyAxis:
        return "Joystick" + number + " axis " + std::to_string(input.index) + (input.value < 0 ? " -" : " +");
    case Kind::ControllerButton:
        if (input.index < 0 || input.index >= static_cast<int>(sizeof(controllerButtons) / sizeof(controllerButtons[0]))) {
            return "Controller" + number + " button " + std::to_string(input.index);
        }
        return "Controller" + number + " " + controllerButtons[input.index].words;
    case Kind::ControllerAxis: {
        if (input.index < 0 || input.index > 5) {
            return "Controller" + number + " axis " + std::to_string(input.index);
        }
        static const char* const words[6][2] = {
            { "left stick left", "left stick right" }, { "left stick up", "left stick down" },
            { "right stick left", "right stick right" }, { "right stick up", "right stick down" },
            { "left trigger", "left trigger" }, { "right trigger", "right trigger" },
        };
        return "Controller" + number + " " + words[input.index][input.value < 0 ? 0 : 1];
    }
    case Kind::Invalid:
        break;
    }
    return "Not recognised";
}

bool same(const Input& a, const Input& b)
{
    if (a.kind != b.kind || a.kind == Kind::Invalid) {
        return false;
    }
    if (a.kind == Kind::Keyboard) {
        return lower(a.key) == lower(b.key);
    }
    return a.joystick == b.joystick && a.index == b.index && a.value == b.value;
}

std::vector<std::string> split(const std::string& value)
{
    // UserInput's reading: std::getline at ',' (so no piece after a final
    // comma), each piece trimmed, an empty one standing for the comma key
    std::vector<std::string> tokens;
    std::istringstream stream(value);
    std::string piece;
    while (std::getline(stream, piece, ',')) {
        piece = trim(piece);
        if (piece.empty() && !value.empty()) {
            piece = ",";
        }
        tokens.push_back(piece);
    }
    return tokens;
}

std::string join(const std::vector<std::string>& tokens)
{
    // The comma key is written as an empty piece first, the only place
    // split() reads it back as one comma
    std::vector<std::string> rest;
    bool comma = false;
    for (const std::string& token : tokens) {
        if (token == ",") {
            comma = true;
        }
        else if (!token.empty()) {
            rest.push_back(token);
        }
    }
    // ",Up" reads as the comma key and Up; "," alone as the comma key
    std::string value = comma ? "," : "";
    for (size_t i = 0; i < rest.size(); i++) {
        if (i > 0) {
            value += ",";
        }
        value += rest[i];
    }
    return value;
}

}

namespace ControlList
{

const std::vector<Control>& all()
{
    static const std::vector<Control> controls = {
        { "up", "Up", "Essential", "Moves up in vertical menus, and in Manage.", true },
        { "down", "Down", "Essential", "Moves down in vertical menus, and in Manage.", true },
        { "left", "Left", "Essential", "Moves left in horizontal menus, and changes values in Manage. Without it, Up is used." },
        { "right", "Right", "Essential", "Moves right in horizontal menus, and changes values in Manage. Without it, Down is used." },
        { "select", "Select", "Essential", "Starts a game or opens the item.", true },
        { "back", "Back", "Essential", "Goes back a menu, and back in Manage.", true },
        { "manager", "Open Settings", "Essential", "Opens this Settings screen. Only keyboard keys work here; the first one is used.", true, false, true },
        { "quit", "Quit", "Essential", "Exits RetroFE. Without it, Q is used." },
        { "pageUp", "Page up", "Navigation", "Jumps back a page; the previous region in the theme editor." },
        { "pageDown", "Page down", "Navigation", "Jumps forward a page; the next region in the theme editor." },
        { "letterUp", "Letter up", "Navigation", "Jumps to the previous letter; the previous widget in the theme editor." },
        { "letterDown", "Letter down", "Navigation", "Jumps to the next letter; the next widget in the theme editor." },
        { "nextPlaylist", "Next playlist", "Playlists", "Switches to the next playlist." },
        { "prevPlaylist", "Previous playlist", "Playlists", "Switches to the previous playlist." },
        { "nextCyclePlaylist", "Next cycle playlist", "Playlists", "The next playlist in cyclePlaylist." },
        { "prevCyclePlaylist", "Previous cycle playlist", "Playlists", "The previous playlist in cyclePlaylist." },
        { "cyclePlaylist", "Cycle playlist", "Playlists", "The next playlist in cyclePlaylist (older name of Next cycle playlist)." },
        { "favPlaylist", "Favourites", "Playlists", "Switches to the favourites playlist." },
        { "addPlaylist", "Add to favourites", "Playlists", "Adds the item to favourites." },
        { "removePlaylist", "Remove from favourites", "Playlists", "Takes the item out of favourites." },
        { "togglePlaylist", "Toggle favourite", "Playlists", "Adds the item to favourites, or takes it out." },
        { "quickPlaylist", "Quick list", "Playlists", "Jumps to the quick list playlist." },
        { "random", "Random", "Playlists", "Picks a random item." },
        { "menu", "Menu", "Admin", "Opens the menu." },
        { "settings", "Settings playlist", "Admin", "Switches to the settings playlist (settingsCollectionPlaylist)." },
        { "reboot", "Reload", "Admin", "Reloads RetroFE and its settings." },
        { "kiosk", "Kiosk mode", "Admin", "Turns kiosk mode on or off (held while idle)." },
        { "saveFirstPlaylist", "Save first playlist", "Admin", "Makes the current playlist the one RetroFE starts on." },
        { "toggleGameInfo", "Game info", "Admin", "Shows or hides the game info." },
        { "toggleCollectionInfo", "Collection info", "Admin", "Shows or hides the collection info." },
        { "toggleBuildInfo", "Build info", "Admin", "Shows or hides the build info." },
        { "playlistUp", "Playlist up", "Advanced", "The previous playlist, in vertical layouts." },
        { "playlistDown", "Playlist down", "Advanced", "The next playlist, in vertical layouts." },
        { "playlistLeft", "Playlist left", "Advanced", "The previous playlist, in horizontal layouts." },
        { "playlistRight", "Playlist right", "Advanced", "The next playlist, in horizontal layouts." },
        { "collectionUp", "Collection up", "Advanced", "The previous collection, in vertical layouts." },
        { "collectionDown", "Collection down", "Advanced", "The next collection, in vertical layouts." },
        { "collectionLeft", "Collection left", "Advanced", "The previous collection, in horizontal layouts." },
        { "collectionRight", "Collection right", "Advanced", "The next collection, in horizontal layouts." },
        { "cycleCollection", "Cycle collection", "Advanced", "The next collection in cycleCollection." },
        { "prevCycleCollection", "Previous cycle collection", "Advanced", "The previous collection in cycleCollection." },
        { "quitCombo", "Quit combo", "Combos", "Hold the first input and press the second to quit (controllerComboExit).", false, true },
        { "settingsCombo", "Settings combo", "Combos", "Hold the first and press the second for the settings playlist (controllerComboSettings).", false, true },
        { "gameInfoCombo", "Game info combo", "Combos", "Hold the first and press the second to show the game info.", false, true },
        { "collectionInfoCombo", "Collection info combo", "Combos", "Hold the first and press the second to show the collection info.", false, true },
        { "buildInfoCombo", "Build info combo", "Combos", "Hold the first and press the second to show the build info.", false, true },
        { "jbFastForward1m", "Jukebox: forward 1 minute", "Jukebox", "Only read when jukebox is on." },
        { "jbFastRewind1m", "Jukebox: back 1 minute", "Jukebox", "Only read when jukebox is on." },
        { "jbFastForward5p", "Jukebox: forward 5%", "Jukebox", "Only read when jukebox is on." },
        { "jbFastRewind5p", "Jukebox: back 5%", "Jukebox", "Only read when jukebox is on." },
        { "jbPause", "Jukebox: pause", "Jukebox", "Only read when jukebox is on." },
        { "jbRestart", "Jukebox: restart", "Jukebox", "Only read when jukebox is on." },
    };
    return controls;
}

const Control* find(const std::string& name)
{
    for (const Control& control : all()) {
        if (control.name == name) {
            return &control;
        }
    }
    return nullptr;
}

std::string builtinDefault(const std::string& name)
{
    if (name == "manager") {
        return "F10";
    }
    std::istringstream lines(Builtin::controls);
    std::string line;
    while (std::getline(lines, line)) {
        size_t equals = line.find('=');
        if (equals != std::string::npos && trim(line.substr(0, equals)) == name) {
            return trim(line.substr(equals + 1));
        }
    }
    return "";
}

}

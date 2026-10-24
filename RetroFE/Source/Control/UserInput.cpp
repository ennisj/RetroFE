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

#include "UserInput.h"
#include "../Database/Configuration.h"
#include "../Database/GlobalOpts.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include "ControllerHandlers.h"
#include "InputToken.h"
#include "JoyAxisHandler.h"
#include "JoyButtonHandler.h"
#include "JoyHatHandler.h"
#include "KeyboardHandler.h"
#include "MouseButtonHandler.h"

UserInput::UserInput(Configuration &c)
    : config_(c)
{
    for(unsigned int i = 0; i < KeyCodeMax; ++i) {
        currentKeyState_[i] = false;
        lastKeyState_[i] = false;
    }
    for ( unsigned int i = 0; i < cMaxJoy; i++ ) {
        joysticks_[i] = -1;
        controllers_[i] = nullptr;
    }
}

UserInput::~UserInput()
{
    for (unsigned int i = 0; i < keyHandlers_.size(); ++i) {
        if (keyHandlers_[i].first) {
            delete keyHandlers_[i].first;
        }
    }
}

bool UserInput::initialize()
{
    // Keyboard names in controls.conf are SDL's (the Manage screens check them the same way)
    InputToken::setKeyNameCheck([](const std::string& name) {
        return SDL_GetScancodeFromName(name.c_str()) != SDL_SCANCODE_UNKNOWN;
    });

    // Optional keys
    MapKey("playlistUp", KeyCodePlaylistUp, false );
    MapKey("playlistDown", KeyCodePlaylistDown, false );
    MapKey("playlistLeft", KeyCodePlaylistLeft, false );
    MapKey("playlistRight", KeyCodePlaylistRight, false );
    MapKey("collectionUp", KeyCodeCollectionUp, false );
    MapKey("collectionDown", KeyCodeCollectionDown, false );
    MapKey("collectionLeft", KeyCodeCollectionLeft, false );
    MapKey("collectionRight", KeyCodeCollectionRight, false );
    MapKey("pageDown", KeyCodePageDown, false );
    MapKey("pageUp", KeyCodePageUp, false );
    MapKey("letterDown", KeyCodeLetterDown, false);
    MapKey("letterUp", KeyCodeLetterUp, false);
    MapKey("favPlaylist", KeyCodeFavPlaylist, false);
    MapKey("nextPlaylist", KeyCodeNextPlaylist, false);
    MapKey("prevPlaylist", KeyCodePrevPlaylist, false);
    MapKey("cyclePlaylist", KeyCodeCyclePlaylist, false);
    MapKey("nextCyclePlaylist", KeyCodeNextCyclePlaylist, false);
    MapKey("prevCyclePlaylist", KeyCodePrevCyclePlaylist, false);
    MapKey("addPlaylist", KeyCodeAddPlaylist, false);
    MapKey("removePlaylist", KeyCodeRemovePlaylist, false);
    MapKey("togglePlaylist", KeyCodeTogglePlaylist, false);
    MapKey("random", KeyCodeRandom, false);
    MapKey("menu", KeyCodeMenu, false);
    MapKey("reboot", KeyCodeReboot, false);
    MapKey("saveFirstPlaylist", KeyCodeSaveFirstPlaylist, false);
    MapKey("kiosk", KeyCodeKisok, false);
    MapKey("cycleCollection", KeyCodeCycleCollection, false);
    MapKey("prevCycleCollection", KeyCodePrevCycleCollection, false);
    MapKey("toggleGameInfo", KeyCodeToggleGameInfo, false);
    MapKey("toggleCollectionInfo", KeyCodeToggleCollectionInfo, false);
    MapKey("toggleBuildInfo", KeyCodeToggleBuildInfo, false);
    MapKey("settings", KeyCodeSettings, false);
	MapKey("quickPlaylist", KeyCodeQuickList, false);

    std::string jbKey;
    if(config_.getProperty(OPTION_JUKEBOX, jbKey)) {
        MapKey("jbFastForward1m", KeyCodeSkipForward, false);
        MapKey("jbFastRewind1m", KeyCodeSkipBackward, false);
        MapKey("jbFastForward5p", KeyCodeSkipForwardp, false);
        MapKey("jbFastRewind5p", KeyCodeSkipBackwardp, false);
        MapKey("jbPause", KeyCodePause, false);
        MapKey("jbRestart", KeyCodeRestart, false);
    }

	MapKeyCombo("quitCombo", KeyCodeQuitCombo1, KeyCodeQuitCombo2, false);
    MapKeyCombo("settingsCombo", KeyCodeSettingsCombo1, KeyCodeSettingsCombo2, false);
    MapKeyCombo("gameInfoCombo", KeyCodeGameInfoCombo1, KeyCodeGameInfoCombo2, false);
    MapKeyCombo("collectionInfoCombo", KeyCodeCollectionInfoCombo1, KeyCodeCollectionInfoCombo2, false);
    MapKeyCombo("buildInfoCombo", KeyCodeBuildInfoCombo1, KeyCodeBuildInfoCombo2, false);

    // At least have controls for either a vertical or horizontal menu: each
    // direction falls back on its partner, then on RetroFE's built-in default
    MapRequired("up", KeyCodeUp, "left");
    MapRequired("left", KeyCodeLeft, "up");
    MapRequired("down", KeyCodeDown, "right");
    MapRequired("right", KeyCodeRight, "down");

    // These are needed to use RetroFE at all. A control with no input that
    // works gets the built-in default rather than stopping RetroFE: an
    // unknown token (another fork's, a typo) never locks anyone out.
    MapRequired("select", KeyCodeSelect);
    MapRequired("back", KeyCodeBack);
    MapRequired("quit", KeyCodeQuit);

    return true;
}

bool UserInput::MapKey(const std::string& keyDescription, KeyCode_E key)
{
    return MapKey(keyDescription, key, true);
}

bool UserInput::MapKey(const std::string& keyDescription, KeyCode_E key, bool required)
{
    std::string description;

    std::string configKey = "controls." + keyDescription;

    if (!config_.getProperty(configKey, description)) {
        if (required) {
            LOG_WARNING("Input", "Missing required property: " + configKey);
        }
        else {
            LOG_INFO("Input", "Missing optional property: " + configKey);
        }
        return false;
    }
    return MapInputs(configKey, description, key) > 0;
}

int UserInput::MapInputs(const std::string& configKey, const std::string& description, KeyCode_E key)
{
    int mapped = 0;
    for (const std::string& token : InputToken::split(description)) {
        if (HandleInputMapping(token, key, configKey)) {
            mapped++;
        }
    }
    return mapped;
}

void UserInput::MapRequired(const std::string& keyDescription, KeyCode_E key, const std::string& alternative)
{
    if (MapKey(keyDescription, key, false)) {
        return;
    }
    if (!alternative.empty() && MapKey(alternative, key, false)) {
        LOG_INFO("Input", "controls." + keyDescription + " isn't set: using controls." + alternative);
        return;
    }
    std::string fallback = ControlList::builtinDefault(keyDescription);
    LOG_WARNING("Input", "controls." + keyDescription + " has no input that works: using RetroFE's default (" + fallback + ")");
    MapInputs("controls." + keyDescription, fallback, key);
}

bool UserInput::HandleInputMapping(const std::string& token, KeyCode_E key, const std::string& configKey) {
    InputToken::Input input = InputToken::parse(token);
    InputHandler* handler = nullptr;

    // Axes count as pressed past the dead zone (percent of the way)
    int deadZone = 3;
    if (!config_.getProperty("controls.deadZone", deadZone)) {
        deadZone = 3;
    }
    Sint16 axisMin = 0;
    Sint16 axisMax = 0;
    if (input.value < 0) {
        axisMin = -32768;
        axisMax = static_cast<Sint16>(-32768 / 100 * deadZone);
    }
    else {
        axisMin = static_cast<Sint16>(32767 / 100 * deadZone);
        axisMax = 32767;
    }

    switch (input.kind) {
    case InputToken::Kind::Keyboard:
        handler = new KeyboardHandler(SDL_GetScancodeFromName(input.key.c_str()));
        break;
    case InputToken::Kind::Mouse:
        handler = new MouseButtonHandler(static_cast<Uint8>(input.index));
        break;
    case InputToken::Kind::JoyButton:
        handler = new JoyButtonHandler(input.joystick, static_cast<Uint8>(input.index));
        break;
    case InputToken::Kind::JoyHat:
        handler = new JoyHatHandler(input.joystick, static_cast<Uint8>(input.index), static_cast<Uint8>(input.value));
        break;
    case InputToken::Kind::JoyAxis:
        handler = new JoyAxisHandler(input.joystick, static_cast<Uint8>(input.index), axisMin, axisMax);
        break;
    case InputToken::Kind::ControllerButton:
        handler = new ControllerButtonHandler(input.joystick, static_cast<Uint8>(input.index));
        break;
    case InputToken::Kind::ControllerAxis:
        handler = new ControllerAxisHandler(input.joystick, static_cast<Uint8>(input.index), axisMin, axisMax);
        break;
    case InputToken::Kind::Invalid:
        break;
    }

    if (!handler) {
        LOG_WARNING("Input", "Skipping an input of " + configKey + ": " + input.problem + " See docs/CONTROLS.md for valid inputs.");
        return false;
    }
    LOG_INFO("Input", "Binding " + configKey + " to " + InputToken::describe(input));
    keyHandlers_.push_back(std::pair<InputHandler*, KeyCode_E>(handler, key));
    return true;
}

bool UserInput::MapKeyCombo(const std::string& keyDescription, KeyCode_E key1, KeyCode_E key2, bool required) {
    std::string description;

    std::string configKey = "controls." + keyDescription;
    if (!config_.getProperty(configKey, description)) {
        if (required) {
            LOG_ERROR("Input", "Missing required combo property: " + configKey);
        }
        else {
            LOG_INFO("Input", "Missing optional combo property: " + configKey);
        }
        return false;
    }

    // The first input is held, the second pressed; anything after is ignored
    std::vector<std::string> tokens = InputToken::split(description);
    bool success = tokens.size() >= 2;
    if (!tokens.empty() && !HandleInputMapping(tokens[0], key1, configKey)) {
        success = false;
    }
    if (tokens.size() >= 2 && !HandleInputMapping(tokens[1], key2, configKey)) {
        success = false;
    }
    if (tokens.size() > 2) {
        LOG_WARNING("Input", configKey + " lists more than two inputs: only the first two are used");
    }
    return success;
}

void UserInput::resetStates()
{
    for (unsigned int i = 0; i < keyHandlers_.size(); ++i) {
        if (keyHandlers_[i].first) {
            keyHandlers_[i].first->reset();
        }
        currentKeyState_[keyHandlers_[i].second] = false;
        lastKeyState_[keyHandlers_[i].second] = false;
    }
}


int UserInput::slotOf(SDL_JoystickID id) const
{
    for (int i = 0; i < cMaxJoy; i++) {
        if (joysticks_[i] == id) {
            return i;
        }
    }
    return -1;
}

void UserInput::openController(int deviceIndex, int slot)
{
    // Pads SDL has a mapping for also send controller events (joyA, joyDPUp...)
    if (slot < 0 || controllers_[slot] || !SDL_IsGameController(deviceIndex)) {
        return;
    }
    controllers_[slot] = SDL_GameControllerOpen(deviceIndex);
    if (controllers_[slot]) {
        const char* name = SDL_GameControllerName(controllers_[slot]);
        LOG_INFO("Input", "Controller connected as joy" + std::to_string(slot) + ": " + (name ? name : "unnamed"));
    }
}

bool UserInput::update( SDL_Event &e )
{
    if (updated_) {
        memcpy(lastKeyState_, currentKeyState_, sizeof(lastKeyState_));
    }
    updated_ = false;
    memset( currentKeyState_, 0, sizeof( currentKeyState_ ) );

    // Handle adding a joystick (also sent for those connected at start)
    if ( e.type == SDL_JOYDEVICEADDED ) {
        SDL_Joystick* joystick = SDL_JoystickOpen( e.jdevice.which );
        SDL_JoystickID id = joystick ? SDL_JoystickInstanceID( joystick ) : -1;
        int slot = id >= 0 ? slotOf( id ) : -1;
        if ( id >= 0 && slot < 0 ) {
            for ( int i = 0; i < cMaxJoy; i++ ) {
                if ( joysticks_[i] == -1 ) {
                    joysticks_[i] = id;
                    slot = i;
                    break;
                }
            }
        }
        openController( e.jdevice.which, slot );
    }

    // A controller mapping can arrive after its joystick
    if ( e.type == SDL_CONTROLLERDEVICEADDED ) {
        openController( e.cdevice.which, slotOf( SDL_JoystickGetDeviceInstanceID( e.cdevice.which ) ) );
    }

    // Handle removing a joystick
    if ( e.type == SDL_JOYDEVICEREMOVED ) {
        int slot = slotOf( e.jdevice.which );
        if ( slot >= 0 ) {
            joysticks_[slot] = -1;
            if ( controllers_[slot] ) {
                SDL_GameControllerClose( controllers_[slot] );
                controllers_[slot] = nullptr;
            }
        }
        SDL_JoystickClose( SDL_JoystickFromInstanceID( e.jdevice.which ) );
    }

    // Remap joystick and controller events to joy0, joy1... in the order they were connected
    if ( e.type == SDL_JOYAXISMOTION ||
         e.type == SDL_JOYBUTTONUP   ||
         e.type == SDL_JOYBUTTONDOWN ||
         e.type == SDL_JOYHATMOTION ) {
        for ( unsigned int i = 0; i < cMaxJoy; i++ ) {
            if ( joysticks_[i] == e.jdevice.which ) {
                e.jdevice.which = i;
                e.jaxis.which   = i;
                e.jbutton.which = i;
                e.jhat.which    = i;
                break;
            }
        }
    }
    if ( e.type == SDL_CONTROLLERAXISMOTION ||
         e.type == SDL_CONTROLLERBUTTONDOWN ||
         e.type == SDL_CONTROLLERBUTTONUP ) {
        int slot = slotOf( e.type == SDL_CONTROLLERAXISMOTION ? e.caxis.which : e.cbutton.which );
        if ( slot >= 0 ) {
            e.caxis.which   = slot;
            e.cbutton.which = slot;
        }
    }

    for ( unsigned int i = 0; i < keyHandlers_.size( ); ++i ) {
        InputHandler *h = keyHandlers_[i].first;
        if ( h ) {
            if ( h->update( e ) ) updated_ = true;
            currentKeyState_[keyHandlers_[i].second] |= h->pressed( );
        }
    }

    return updated_;
}


bool UserInput::keystate(KeyCode_E code) const
{
    return currentKeyState_[code];
}

bool UserInput::lastKeyPressed(KeyCode_E code) const
{
    if (lastKeyState_[code]) {
        return true;
    }
    return false;
}

bool UserInput::newKeyPressed(KeyCode_E code) const
{
    return currentKeyState_[code] && !lastKeyState_[code];
}


void UserInput::clearJoysticks( )
{
    // SDL is going down: it closes them itself
    for ( unsigned int i = 0; i < cMaxJoy; i++ ) {
        joysticks_[i] = -1;
        controllers_[i] = nullptr;
    }
}


void UserInput::reconfigure()
{
    LOG_INFO("Input", "Reconfigure Inputs");

    for (unsigned int i = 0; i < keyHandlers_.size(); ++i) {
        if (keyHandlers_[i].first) {
            delete keyHandlers_[i].first;
        }
    }
    keyHandlers_.clear();
    initialize( );
    resetStates( );
}


void UserInput::updateKeystate( )
{
    for ( unsigned int i = 0; i < keyHandlers_.size( ); ++i ) {
        InputHandler *h = keyHandlers_[i].first;
        if ( h ) {
			h->updateKeystate( );
            currentKeyState_[keyHandlers_[i].second] |= h->pressed( );
        }
    }
}

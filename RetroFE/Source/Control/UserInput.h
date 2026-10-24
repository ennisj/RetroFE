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
#include <SDL2/SDL.h>
#include <SDL2/SDL_gamecontroller.h>
#include <SDL2/SDL_joystick.h>
#include <map>
#include <string>
#include <vector>

const int cMaxJoy = 4;

class Configuration;
class InputHandler;

class UserInput
{
public:
    enum KeyCode_E
    {
        KeyCodeNull,
        KeyCodeUp,
        KeyCodeDown,
        KeyCodeLeft,
        KeyCodeRight,
        KeyCodePlaylistUp,
        KeyCodePlaylistDown,
        KeyCodePlaylistLeft,
        KeyCodePlaylistRight,
        KeyCodeCollectionUp,
        KeyCodeCollectionDown,
        KeyCodeCollectionLeft,
        KeyCodeCollectionRight,
        KeyCodeSelect,
        KeyCodeBack,
        KeyCodePageDown,
        KeyCodePageUp,
        KeyCodeLetterDown,
        KeyCodeLetterUp,
        KeyCodeFavPlaylist,
        KeyCodeNextPlaylist,
        KeyCodePrevPlaylist,
        KeyCodeCyclePlaylist,
        KeyCodeNextCyclePlaylist,
        KeyCodePrevCyclePlaylist,
        KeyCodeRandom,
        KeyCodeMenu,
        KeyCodeAddPlaylist,
        KeyCodeRemovePlaylist,
        KeyCodeTogglePlaylist,
        KeyCodeAdminMode,
        KeyCodeHideItem,
        KeyCodeQuit,
        KeyCodeReboot,
        KeyCodeSaveFirstPlaylist,
        KeyCodeSkipForward,
        KeyCodeSkipBackward,
        KeyCodeSkipForwardp,
        KeyCodeSkipBackwardp,
        KeyCodePause,
        KeyCodeRestart,
        KeyCodeKisok,
        KeyCodeSettingsCombo1,
        KeyCodeSettingsCombo2,
        KeyCodeQuitCombo1,
        KeyCodeQuitCombo2,
        KeyCodeGameInfoCombo1,
        KeyCodeGameInfoCombo2,
        KeyCodeCollectionInfoCombo1,
        KeyCodeCollectionInfoCombo2,
        KeyCodeBuildInfoCombo1,
        KeyCodeBuildInfoCombo2,
        KeyCodeCycleCollection,
        KeyCodePrevCycleCollection,
        KeyCodeToggleGameInfo,
        KeyCodeToggleCollectionInfo,
        KeyCodeToggleBuildInfo,
        KeyCodeSettings,
        KeyCodeQuickList,
        // leave KeyCodeMax at the end
        KeyCodeMax,
    };

    explicit UserInput(Configuration& c);
    virtual ~UserInput();
    // Maps controls.* to inputs (InputToken.h). Always succeeds: unknown
    // inputs are skipped with a warning, and up/down/left/right, select, back
    // and quit fall back on the built-in defaults when nothing else works.
    bool initialize();
    void resetStates();
    bool update(SDL_Event& e);
    bool keystate(KeyCode_E) const;
    bool lastKeyPressed(KeyCode_E code) const;
    bool newKeyPressed(KeyCode_E code) const;
    void clearJoysticks();
    void reconfigure();
    void updateKeystate();

private:
    bool MapKey(const std::string& keyDescription, KeyCode_E key);
    bool MapKey(const std::string& keyDescription, KeyCode_E key, bool required);
    // Every input of a value that works; returns how many. Unknown ones are skipped with a warning.
    int MapInputs(const std::string& configKey, const std::string& description, KeyCode_E key);
    // A control RetroFE needs: else alternative's inputs, else the built-in default
    void MapRequired(const std::string& keyDescription, KeyCode_E key, const std::string& alternative = "");
    bool HandleInputMapping(const std::string& token, KeyCode_E key, const std::string& configKey);
    bool MapKeyCombo(const std::string& keyDescription, KeyCode_E key1, KeyCode_E key2, bool required);
    int slotOf(SDL_JoystickID id) const;
    void openController(int deviceIndex, int slot);
    Configuration& config_;
    bool updated_{ false };
    SDL_JoystickID joysticks_[cMaxJoy];
    SDL_GameController* controllers_[cMaxJoy];   // by slot, for pads SDL has a mapping for
    std::vector<std::pair<InputHandler *, KeyCode_E> > keyHandlers_;
    bool lastKeyState_[KeyCodeMax]; 
    bool currentKeyState_[KeyCodeMax]; 
};

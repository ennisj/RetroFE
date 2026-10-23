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

#include "FormView.h"
#include "ManagerModel.h"
#include "../Forms/Form.h"
#include <SDL2/SDL.h>
#include <map>
#include <memory>
#include <string>

class Configuration;
struct ImGuiContext;

// In-app management panels (Settings, Manage, Catalog), drawn with Dear ImGui
// on top of the current page on screen 0. Each panel is a form (see Forms/Form.h).
// While a panel is open it takes all input.
class ManagerUI
{
public:
    // The panels opened from the Home menu
    enum class Screen { Settings, Manage, Catalog };

    explicit ManagerUI(Configuration& config);
    ~ManagerUI();

    bool initialize(SDL_Window* window, SDL_Renderer* renderer);
    void deInitialize();

    bool isOpen() const { return open_; }
    void open(Screen screen);
    void close();

    // Returns true if the UI used the event, including the key that opens it
    bool handleEvent(const SDL_Event& e);

    // Work that can't happen while rendering, such as native file dialogs.
    // Call once per frame, outside SDL::getMutex().
    void update();

    // Draws the UI onto the window's render target. Call while holding SDL::getMutex().
    void render();

    // True once after changes were saved that RetroFE has to reload to show
    bool takeReloadRequest();

private:
    void drawWindow();
    void loadForms();

    Configuration& config_;
    ManagerModel model_;
    FormView view_;
    std::map<Screen, std::unique_ptr<Forms::Form>> forms_;

    ImGuiContext* context_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Scancode toggleKey_ = SDL_SCANCODE_F10;
    float scale_ = 1.0f;
    bool open_ = false;
    bool focusNext_ = false;   // give the panel keyboard/gamepad focus on the next frame
    Screen screen_ = Screen::Settings;
    bool reloadRequested_ = false;
    int cursorWasShown_ = SDL_ENABLE;

    std::string status_;
    bool statusIsError_ = false;
};

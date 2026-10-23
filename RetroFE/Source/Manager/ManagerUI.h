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
#include "../Graphics/Component/FormWidget.h"
#include "../Graphics/Component/KeyboardWidget.h"
#include <SDL2/SDL.h>
#include <array>
#include <map>
#include <memory>
#include <string>

class Configuration;
class FontCache;
class Page;
struct ImGuiContext;

// The management screens (Settings, Manage, Catalog), shown over the current page.
// Each screen is a form (see Forms/Form.h). It's drawn as a RetroFE page with a
// <form> element, styled by layouts/<layout>/forms/<id>/layout.xml or
// forms/layout.xml, or the built-in form page, and driven by RetroFE's
// controls. formsRenderer = imgui draws the forms with Dear ImGui instead.
// While a text field is edited an on-screen keyboard page is shown
// (forms/keyboard/layout.xml or the built-in one) unless onScreenKeyboard = no;
// typing on a real keyboard puts it away.
// While a screen is open it takes all input.
class ManagerUI
{
public:
    // The screens opened from the Home menu
    enum class Screen { Settings, Manage, Catalog };

    explicit ManagerUI(Configuration& config);
    ~ManagerUI();

    bool initialize(SDL_Window* window, SDL_Renderer* renderer, FontCache* fontCache);
    void deInitialize();

    bool isOpen() const { return open_; }
    void open(Screen screen);
    void close();

    // Returns true if the UI used the event, including the key that opens Settings.
    // Unused events should go to RetroFE's input so navigate() gets them.
    bool handleEvent(const SDL_Event& e);

    // Whether the open screen is driven by RetroFE's controls through navigate()
    bool wantsNavigation() const { return open_ && page_; }

    // RetroFE's up, down, left, right, select and back controls, as currently held.
    // Presses act once, and up/down/left/right repeat while held.
    void navigate(const std::array<bool, 6>& held, float now);

    // Work that can't happen while rendering, such as native file dialogs.
    // Call once per frame, outside SDL::getMutex().
    void update(float dt);

    // Draws a page-based screen onto the main screen's render target, after the current page
    void drawPage();

    // Draws an ImGui screen onto the window. Call while holding SDL::getMutex().
    void render();

    // True once after changes were saved that RetroFE has to reload to show
    bool takeReloadRequest();

private:
    void drawWindow();
    void loadForms();
    bool buildPage();
    void destroyPage();
    void handleWidgetResults();
    void syncKeyboard();
    bool openKeyboard();
    void closeKeyboard();

    FontCache* fontCache_ = nullptr;
    Page* page_ = nullptr;             // the page-based screen, when open
    FormWidget* widget_ = nullptr;     // owned by page_
    Page* keyboardPage_ = nullptr;     // the on-screen keyboard, while a field is edited
    KeyboardWidget* keyboard_ = nullptr;
    bool wasEditing_ = false;
    bool typedOnKeyboard_ = false;     // this edit uses a real keyboard, so no on-screen one
    std::array<bool, 6> navHeld_{};
    std::array<float, 6> navRepeatAt_{};

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

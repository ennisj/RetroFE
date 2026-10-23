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
#include "Navigator.h"
#include "../Catalog/Service.h"
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

// The management screens (Settings, Manage and its sections, Catalog sources
// and packages), shown over the current page. Each screen is a form (see
// Forms/Form.h) opened by id, e.g. "settings", "manage", "sources"; screens
// open further screens over themselves, and Back returns (see Navigator.h).
// A screen is drawn as a RetroFE page with a <form> element, styled by
// layouts/<layout>/forms/<id>/layout.xml or forms/layout.xml, or the built-in
// form page, and driven by RetroFE's controls. formsRenderer = imgui draws the
// forms with Dear ImGui instead.
// While a text field is edited an on-screen keyboard page is shown
// (forms/keyboard/layout.xml or the built-in one) unless onScreenKeyboard = no;
// typing on a real keyboard puts it away.
// While a screen is open it takes all input.
class ManagerUI
{
public:
    explicit ManagerUI(Configuration& config);
    ~ManagerUI();

    bool initialize(SDL_Window* window, SDL_Renderer* renderer, FontCache* fontCache);
    void deInitialize();

    bool isOpen() const { return open_; }

    // Opens a form by id with its parameters, as the only screen: Back closes it.
    // Does nothing (and logs why) if there's no such form.
    void open(const std::string& form, const Forms::Params& params = {});

    // Opens a form over the current screen; Back returns to it. False, with
    // an error shown on the current screen, if there's no such form.
    bool push(const std::string& form, const Forms::Params& params = {});

    // Returns to the previous screen, or closes the manager from the first
    // one, without asking providers (see requestBack)
    void back();

    void close();

    // The Catalog, which keeps working while the screens are closed; null before initialize()
    Catalog::Service* catalog() { return catalog_.get(); }

    // Opens the details of a package from the Catalog list
    void openPackage(const std::string& id) { open("package", { { "id", id } }); }

    // True once after Catalog installs or removals finished, having added
    // collections they installed to the Main menu: RetroFE should reload
    bool takeCatalogChanges();

    // True once after RetroFE updated itself: RetroFE should exit, and main()
    // starts the new version (Catalog::takeRelaunch)
    bool takeRestartRequest();

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

    // True once after changes were saved that RetroFE has to reload to show:
    // straight away for Result::reload, after the manager closes for
    // Services::requestReload. Waits while a Catalog install is running.
    bool takeReloadRequest();

    // Kiosk mode: the Settings key does nothing, and an open screen closes
    void setLocked(bool locked);

    // The page RetroFE draws collections with ("layout", or a randomLayout one
    // such as "layout 4x3"); screens use the layout's forms/.../<page>.xml when it has one
    void setLayoutPage(const std::string& page) { layoutPage_ = page; }

private:
    void drawWindow();
    void startCatalog();
    const Forms::Form* form(const std::string& id);
    void show();   // shows the navigator's current screen

    // The user asked to leave the current screen (or every screen): asks the
    // providers, and warns once if they object (Provider::leaving)
    bool mayLeave(bool everything);
    bool requestBack();
    bool requestClose();

    void handleResult(const Forms::Model::Result& result);
    void setStatus(const std::string& message, bool isError);
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
    std::unique_ptr<Catalog::Service> catalog_;   // destroyed after model_, which points to it
    ManagerModel model_;
    bool providersAdded_ = false;
    FormView view_;
    Navigator nav_;
    std::map<std::string, std::unique_ptr<Forms::Form>> forms_;   // by id, loaded when first opened; null if it can't be

    ImGuiContext* context_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Scancode toggleKey_ = SDL_SCANCODE_F10;
    float scale_ = 1.0f;
    bool open_ = false;
    bool locked_ = false;
    bool focusNext_ = false;   // give the panel keyboard/gamepad focus on the next frame
    bool reloadRequested_ = false;
    bool reloadWhenClosed_ = false;   // Services::requestReload was called
    bool leaveWarned_ = false;        // a provider objected to leaving; doing it again leaves
    int cursorWasShown_ = SDL_ENABLE;

    std::string status_;
    bool statusIsError_ = false;
    std::string layoutPage_ = "layout";
};

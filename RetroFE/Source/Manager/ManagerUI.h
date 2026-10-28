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

#include "AfterReload.h"
#include "FormView.h"
#include "Browser/MouseUse.h"
#include "ImGuiScreen.h"
#include "LaunchQueue.h"
#include "LeaveWarning.h"
#include "ManagerModel.h"
#include "Navigator.h"
#include "Controls/InputCapture.h"
#include "Controls/app/CaptureEvents.h"
#include "../Catalog/Service.h"
#include "../Forms/Form.h"
#include "../Library/Edits.h"
#include "../Library/Indexer.h"
#include "../Graphics/Component/FormWidget.h"
#include "../Graphics/Component/KeyboardWidget.h"
#include <SDL2/SDL.h>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <string>

class CollectionInfo;
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

    // RetroFE's window was closed: asks the screens as closing Manage does
    // (Provider::leaving; a screen with its own unsaved-changes dialog shows
    // it). True when Manage closed and RetroFE may quit (a reload the changes
    // asked for is dropped: they're saved, and RetroFE is quitting); false
    // when it warned instead, and closing the window again leaves, dropping them.
    // A close after another kind of warning (Back's) warns again first.
    bool requestQuit();

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

    // Whether the open screen is driven by RetroFE's controls through navigate():
    // a layout-drawn screen, or an ImGui screen that asks for them (the theme editor)
    bool wantsNavigation() const { return open_ && (page_ || (screen_ && screen_->wantsControls())); }

    // RetroFE's up, down, left, right, select, back, page up, page down,
    // letter up and letter down controls, as currently held (Controls'
    // order). Presses act once; directions, page and letter up/down repeat
    // while held. Layout-drawn screens use all ten; an ImGui screen
    // gets them all (ImGuiScreen::control).
    using Controls = Manager::ImGuiScreen::ControlsHeld;
    void navigate(const Controls& held, float now);

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
    bool takeReloadRequest(bool backHome = false);

    // Kiosk mode: the Settings key does nothing, and an open screen closes
    void setLocked(bool locked);

    // A game Manage asked to start (Services::requestLaunch, "Start this game")
    using LaunchRequest = Manager::LaunchQueue::Request;
    // True once, with the request, after the manager closed for it. Not while
    // a reload is due: the request outlives RetroFE's reload (it's kept for
    // the next ManagerUI), so the game starts with the changes made in Manage.
    // A reload that can't happen (a Catalog install) drops it (LaunchQueue).
    bool takeLaunchRequest(LaunchRequest& request);
    // After the game: Manage opens again on its screen (Manage > Games > the
    // game), saying it's back, or problem when it couldn't be started
    void returnFromLaunch(const LaunchRequest& request, const std::string& problem);

    // Once RetroFE is back up after a reload an action asked for with
    // Result::open ("Apply changes now"): opens Manage on that screen with
    // the action's message. False (nothing opened) otherwise.
    bool reopenAfterReload();

    // A collection an action asked RetroFE to show (Result::browse: one just
    // installed, the Catalog list): true once, when the manager has closed
    // and any reload it needed is done
    bool takeBrowseRequest(std::string& collection);

    // The page RetroFE draws collections with ("layout", or a randomLayout one
    // such as "layout 4x3"); screens use the layout's forms/.../<page>.xml when it has one
    void setLayoutPage(const std::string& page) { layoutPage_ = page; }

    // For screens that preview a collection (the theme editor): builds one the
    // way RetroFE does (the caller owns it), and names the one RetroFE shows now
    void setCollectionSource(std::function<CollectionInfo*(const std::string&)> load, std::function<std::string()> current)
    {
        loadCollection_ = std::move(load);
        currentCollection_ = std::move(current);
    }

    // How RetroFE reads controls.conf and controls1..9.conf again and remaps
    // its controls (Settings > Controls calls it after Save)
    void setControlsReload(std::function<void()> reload) { reloadControls_ = std::move(reload); }

    // Waiting for "press it now" (Forms::Model::Result::capture)
    bool isCapturing() const { return capture_.active(); }

private:
    // Screens that draw themselves with ImGui (Manager::ImGuiScreen), by form id
    bool showImGuiScreen(const Navigator::Frame& frame);
    void retireScreen();
    void drawWindow();
    void startCatalog();
    void startLibrary();
    std::string gameTitle(const std::string& collection, const std::string& game) const;   // its title, else game
    const Forms::Form* form(const std::string& id);
    void show();   // shows the navigator's current screen

    // The user asked to leave the current screen (or every screen) that way:
    // asks the providers, and warns once per way of leaving if they object
    // (Provider::leaving, LeaveWarning)
    bool mayLeave(bool everything, Manager::Leave how);
    bool requestBack();
    bool requestClose(Manager::Leave how = Manager::Leave::Close);

    void handleResult(const Forms::Model::Result& result);
    Manager::PendingChanges pendingChanges() const;
    // The file browser (Manager/Browser): opened by Select on a folder or file
    // field; a pick returns past its screens and sets the field
    void openBrowser(const FormWidget::Browse& browse);
    void applyPick(const Forms::Model::Result& result);
    void setStatus(const std::string& message, Forms::StatusKind kind);
    bool buildPage();
    void destroyPage();
    void handleWidgetResults();
    void syncKeyboard();
    void readToggleKey();   // controls.manager
    // Press it now: feeds an event to the capture (false: RetroFE's input still sees it)
    bool captureEvent(const SDL_Event& e);
    void updateCapture();
    void endCapture();
    bool openKeyboard();
    void closeKeyboard();

    FontCache* fontCache_ = nullptr;
    Page* page_ = nullptr;             // the page-based screen, when open
    FormWidget* widget_ = nullptr;     // owned by page_
    Page* keyboardPage_ = nullptr;     // the on-screen keyboard, while a field is edited
    KeyboardWidget* keyboard_ = nullptr;
    bool wasEditing_ = false;
    bool typedOnKeyboard_ = false;     // this edit uses a real keyboard, so no on-screen one
    Controls navHeld_{};
    std::array<float, std::tuple_size<Controls>::value> navRepeatAt_{};

    Configuration& config_;
    std::unique_ptr<Catalog::Service> catalog_;   // destroyed after model_, which points to it
    std::unique_ptr<Library::Indexer> library_;   // the same for the library, which providers use
    std::unique_ptr<Library::Editor> libraryEditor_;
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
    Manager::LeaveWarning leave_;     // a provider objected to leaving one way; leaving that way again leaves
    int cursorWasShown_ = SDL_ENABLE;
    // Static: a request waiting for the reload outlives this ManagerUI (RetroFE reloads by starting again)
    inline static Manager::LaunchQueue launches_;
    inline static Manager::AfterReload afterReload_;   // likewise: where to go once it's back

    std::string status_;
    Forms::StatusKind statusKind_ = Forms::StatusKind::Success;
    std::string layoutPage_ = "layout";

    std::unique_ptr<Manager::ImGuiScreen> screen_;                 // the current screen, when it draws itself
    std::vector<std::unique_ptr<Manager::ImGuiScreen>> retired_;   // left; shut down and freed in update()
    std::function<CollectionInfo*(const std::string&)> loadCollection_;
    std::function<std::string()> currentCollection_;
    std::function<void()> reloadControls_;

    Manager::InputCapture capture_;
    Manager::CaptureEvents captureEvents_;
    std::string captureBind_;
    std::string captureAction_;
    std::string capturePrompt_;

    // The file browser's "Browse with Windows...", opened from update()
    std::optional<Manager::Services::SystemPick> systemPick_;
    Browser::MouseUse mouseUse_;
    struct BrowserOpening
    {
        std::mutex mutex;
        Forms::Params places;
        Forms::Params folder;
        bool ready = false;
    };
    std::shared_ptr<BrowserOpening> browserOpening_;
};

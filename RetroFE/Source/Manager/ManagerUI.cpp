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
#include "ManagerUI.h"
#include "../SDL.h"
#include <thread>
#include "FileDialog.h"
#include "Setup.h"
#include "Browser/BrowserProvider.h"
#include "Controls/app/PadInputs.h"
#include "Themes/ThemeProvider.h"
#include "Themes/app/ThemeEditorScreen.h"
#include "../Builtin/Builtin.h"
#include "../Database/Configuration.h"
#include "../Database/GlobalOpts.h"
#include "../Graphics/LayoutPages.h"
#include "../Graphics/Page.h"
#include "../Graphics/PageBuilder.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "versioning.h" // Created at build time
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
    // The form the Settings key opens
    constexpr const char* settingsForm = "settings";
}

ManagerUI::ManagerUI(Configuration& config)
    : config_(config)
    , model_(config)
{
    // Setup reads firstCollection as RetroFE uses it (command-line overrides included)
    Setup::setConfiguration(&config_);
    afterReload_.started();   // what the last RetroFE kept for after its reload is this one's
}

ManagerUI::~ManagerUI()
{
    // RetroFE is going: a game waiting to start goes too, unless it waits for this reload
    launches_.shutdown();
    afterReload_.shutdown();
    deInitialize();
    Setup::setConfiguration(nullptr);
}

bool ManagerUI::initialize(SDL_Window* window, SDL_Renderer* renderer, FontCache* fontCache)
{
    if (context_) {
        return true;
    }
    if (!window || !renderer) {
        return false;
    }
    window_ = window;
    renderer_ = renderer;
    fontCache_ = fontCache;

    readToggleKey();

    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    // Size everything for the screen: 1.0 at 720p, larger on bigger displays
    int outputHeight = 0;
    SDL_GetRendererOutputSize(renderer_, nullptr, &outputHeight);
    scale_ = std::max(1.0f, static_cast<float>(outputHeight) / 720.0f);
    view_.setScale(scale_);
    view_.setRenderer(renderer_);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.WindowPadding = ImVec2(18.0f, 16.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ScaleAllSizes(scale_);

    // The atlas only reads the font, so the embedded data can be shared
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(retrofe_builtin_font),
                                   static_cast<int>(retrofe_builtin_font_size),
                                   std::round(18.0f * scale_), &fontConfig);

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_)) {
        LOG_ERROR("Manager", "Could not initialize the SDL backend");
        ImGui::DestroyContext(context_);
        context_ = nullptr;
        return false;
    }
    if (!ImGui_ImplSDLRenderer2_Init(renderer_)) {
        LOG_ERROR("Manager", "Could not initialize the renderer backend");
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(context_);
        context_ = nullptr;
        return false;
    }

    // Forms are loaded when first opened, from the layout in use now
    forms_.clear();
    startCatalog();
    startLibrary();
    if (!providersAdded_) {
        Manager::Services services{ config_, catalog_.get() };
        services.library = library_.get();
        services.editor = libraryEditor_.get();
        services.requestReload = [this] { reloadWhenClosed_ = true; };
        services.requestLaunch = [this](const std::string& collection, const std::string& game) {
            LaunchRequest request{ collection, game, gameTitle(collection, game) };
            request.returning.frames = nav_.frames();
            if (!request.returning.frames.empty() && widget_) {
                request.returning.frames.back().focus = widget_->focus();
            }
            request.returning.providers = model_.saveView();
            launches_.want(request);   // handleResult closes the manager for it
        };
        services.launchProblem = [this] {
            bool reloadDue = reloadRequested_ || reloadWhenClosed_ || (library_ && library_->changesPending());
            return Manager::LaunchQueue::problem(reloadDue, catalog_ && catalog_->isBusy());
        };
        services.sameInputs = Manager::padSameInputs;
        if (FileDialog::isSupported()) {
            // Not now: the press that asked is still being handled; update() opens it
            services.pickWithSystem = [this](const Manager::Services::SystemPick& pick) { systemPick_ = pick; };
        }
        services.mousePresent = [this] { return mouseUse_.present(SDL_GetTicks()); };
        services.reloadControls = [this] {
            if (reloadControls_) {
                reloadControls_();
            }
            readToggleKey();
            navHeld_.fill(true);   // the Select that saved is still held
        };
        Manager::registerProviders(model_, services);
        model_.setPendingChanges([this] { return pendingChanges(); });
        providersAdded_ = true;
    }
    LOG_INFO("Manager", "Press " + std::string(SDL_GetScancodeName(toggleKey_)) + " to open Settings");
    return true;
}

void ManagerUI::readToggleKey()
{
    // controls.manager picks the key that opens Settings: the first keyboard key it lists
    toggleKey_ = SDL_SCANCODE_F10;
    std::string toggle;
    if (!config_.getProperty("controls.manager", toggle)) {
        return;
    }
    std::vector<std::string> keys;
    Utils::listToVector(toggle, keys, ',');
    for (const std::string& name : keys) {
        SDL_Scancode key = SDL_GetScancodeFromName(Utils::trimEnds(name).c_str());
        if (key != SDL_SCANCODE_UNKNOWN) {
            toggleKey_ = key;
            return;
        }
    }
    LOG_WARNING("Manager", "controls.manager lists no keyboard key, using " + std::string(SDL_GetScancodeName(toggleKey_)));
}

const Forms::Form* ManagerUI::form(const std::string& id)
{
    auto found = forms_.find(id);
    if (found == forms_.end()) {
        // A layout can provide its own versions in layouts/<layout>/forms/
        std::string layoutName;
        config_.getProperty(OPTION_LAYOUT, layoutName);
        found = forms_.emplace(id, Forms::load(id, layoutName)).first;
    }
    return found->second.get();
}

void ManagerUI::startLibrary()
{
    if (library_) {
        return;
    }
    // Paths as RetroFE resolved them, so command-line and settings overrides count
    Library::ScanOptions options;
    options.root = Configuration::absolutePath;
    options.firstCollection = Setup::firstCollection();
    config_.getPropertyAbsolutePath(OPTION_BASEITEMPATH, options.baseItemPath);
    config_.getPropertyAbsolutePath(OPTION_BASEMEDIAPATH, options.baseMediaPath);
    options.layoutsPath = Configuration::layoutsPath();
    library_ = std::make_unique<Library::Indexer>(options);
    libraryEditor_ = std::make_unique<Library::Editor>(Configuration::absolutePath);
}

void ManagerUI::startCatalog()
{
    if (catalog_) {
        return;
    }
    // catalogPath, relative to the root like other paths; downloads and installs all happen under it
    std::string folder;
    if (!config_.getPropertyAbsolutePath(OPTION_CATALOGPATH, folder) || folder.empty()) {
        folder = Utils::combinePath(Configuration::absolutePath, "catalog");
    }
    Catalog::Targets targets;
    targets.root = Configuration::absolutePath;
    targets.collections = Utils::combinePath(Configuration::absolutePath, "collections");
    targets.layouts = Configuration::layoutsPath();   // baseLayoutPath when set (OneSauce)
    targets.emulators = Utils::combinePath(Configuration::absolutePath, "emulators");
    targets.meta = Utils::combinePath(Configuration::absolutePath, "meta");
    if (!config_.getPropertyAbsolutePath(OPTION_BASEITEMPATH, targets.items)) {
        targets.items = targets.collections;
    }
    if (!config_.getPropertyAbsolutePath(OPTION_BASEMEDIAPATH, targets.media)) {
        targets.media = targets.collections;
    }
    // RetroFE's own program folder, when it's a folder of its own (retrofe/ under the root)
    if (char* base = SDL_GetBasePath()) {
        std::filesystem::path program = std::filesystem::path(base).lexically_normal();
        SDL_free(base);
        std::filesystem::path root = std::filesystem::path(Configuration::absolutePath).lexically_normal();
        while (!program.empty() && !program.has_filename()) program = program.parent_path();   // drop the trailing separator
        if (program != root && program.parent_path() == root) {
            targets.protectedFolders.push_back(program.string());
        }
    }

    catalog_ = std::make_unique<Catalog::Service>(folder, targets, Catalog::createHttp());
    // GitHub sources can update RetroFE itself (SelfUpdater checks it's in a folder of its own). UTF-8 paths.
    Catalog::AppInstall app;
    app.executable = Catalog::currentExecutable();
    if (!app.executable.empty()) {
        app.program = std::filesystem::u8path(app.executable).parent_path().u8string();
    }
    app.root = std::filesystem::path(Configuration::absolutePath).u8string();
    app.version = RETROFE_VERSION_DESCRIBE;
    catalog_->setApp(app);
    catalog_->start();
    model_.setCatalog(catalog_.get());
}

bool ManagerUI::takeCatalogChanges()
{
    Catalog::Service::Changes changes;
    if (!catalog_ || !catalog_->takeChanges(changes)) {
        return false;
    }
    // Installed collections show under Browse, like ones made in Manage
    std::string error;
    if (!changes.addedCollections.empty() && !model_.ensureSettingsFile(error)) {
        LOG_ERROR("Manager", error);
    }
    for (const std::string& name : changes.addedCollections) {
        // (addToMainMenu skips the top menu itself)
        if (!Setup::addToMainMenu(name, error)) {
            LOG_ERROR("Manager", error);
        }
    }
    for (const std::string& name : changes.removedCollections) {
        if (!Setup::removeFromMainMenu(name, error)) {
            LOG_ERROR("Manager", error);
        }
    }
    return true;
}

bool ManagerUI::takeRestartRequest()
{
    // The path found at start: the running file may have been renamed since
    std::string executable = catalog_ ? catalog_->app().executable : "";
    if (!catalog_ || !catalog_->takeRestartRequest() || executable.empty()) {
        return false;
    }
    Catalog::requestRelaunch(executable);
    return true;
}

void ManagerUI::deInitialize()
{
    if (!context_) {
        return;
    }
    if (open_) {
        close();
    }
    retireScreen();
    for (auto& screen : retired_) {
        screen->shutdown();
    }
    retired_.clear();
    destroyPage();
    view_.freeImages();
    view_.setRenderer(nullptr);
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(context_);
    context_ = nullptr;
    window_ = nullptr;
    renderer_ = nullptr;
}

void ManagerUI::open(const std::string& form, const Forms::Params& params)
{
    browserOpening_.reset();
    if (!context_) {
        return;
    }
    if (!this->form(form)) {
        LOG_ERROR("Manager", "Can't open " + form + ": there's no such form");
        return;
    }
    nav_.open(form, params);
    show();
}

bool ManagerUI::push(const std::string& form, const Forms::Params& params)
{
    launchReturn_.reset();
    browserOpening_.reset();
    if (!context_) {
        return false;
    }
    // A form that doesn't exist (e.g. a typo in open="") is reported where the user is
    if (!this->form(form)) {
        LOG_ERROR("Manager", "Can't open " + form + ": there's no such form");
        setStatus("This screen isn't available (" + form + "). Check the log for details.", Forms::StatusKind::Error);
        return false;
    }
    nav_.push(form, params, widget_ ? widget_->focus() : Forms::Focus{});
    show();
    return true;
}

void ManagerUI::back()
{
    launchReturn_.reset();
    browserOpening_.reset();
    if (nav_.back()) {
        show();
    }
    else {
        close();
    }
}

bool ManagerUI::mayLeave(bool everything, Manager::Leave how)
{
    // The current screen, or every screen when the manager is closing
    std::vector<Manager::Screen> leaving;
    const std::vector<Navigator::Frame>& frames = nav_.frames();
    for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
        leaving.push_back(Manager::Screen{ frame->form, frame->params });
        if (!everything) {
            break;
        }
    }
    // A screen with its own dialog for unsaved changes asks there
    if (screen_ && screen_->askToLeave()) {
        leave_.reset();
        return false;
    }
    std::string warning;
    for (const Manager::Screen& screen : leaving) {
        // leaving[0] is the current screen, the only one that can draw itself
        if (screen_ && &screen == &leaving.front()) {
            warning = screen_->leaving();
        }
        if (warning.empty()) {
            warning = model_.leaving(screen);
        }
        if (!warning.empty()) {
            break;
        }
    }
    switch (leave_.ask(how, warning)) {
    case Manager::LeaveWarning::Verdict::Leave:
        return true;
    case Manager::LeaveWarning::Verdict::LeaveDropping:
        // Warned about leaving this way already: leave, dropping the changes
        if (screen_) {
            screen_->discard();
        }
        for (const Manager::Screen& screen : leaving) {
            model_.discard(screen);
        }
        return true;
    case Manager::LeaveWarning::Verdict::Warn:
        break;
    }
    setStatus(leave_.message(), Forms::StatusKind::Error);
    return false;
}

bool ManagerUI::requestBack()
{
    if (!mayLeave(nav_.depth() <= 1, Manager::Leave::Back)) {
        return false;
    }
    back();
    return true;
}

bool ManagerUI::requestClose(Manager::Leave how)
{
    if (!mayLeave(true, how)) {
        return false;
    }
    close();
    return true;
}

bool ManagerUI::requestQuit()
{
    if (!requestClose(Manager::Leave::Quit)) {
        return false;
    }
    reloadRequested_ = false;
    afterReload_.clear();   // there's no reload to come back from
    return true;
}

Manager::PendingChanges ManagerUI::pendingChanges() const
{
    Manager::PendingChanges pending;
    if (library_) {
        pending.library = library_->pendingChanges();
        pending.libraryFlag = library_->changesPending();
    }
    pending.reloadAsked = reloadWhenClosed_ || reloadRequested_;
    pending.catalog = catalog_ && catalog_->hasChanges();
    return pending;
}

bool ManagerUI::reopenAfterReload()
{
    Manager::AfterReload::Screen screen;
    if (!context_ || locked_ || !afterReload_.takeReopen(screen)) {
        return false;
    }
    // The hub as the first screen, so Back goes there from a deeper one
    open("manage");
    if (open_ && screen.form != "manage") {
        push(screen.form, screen.params);
    }
    if (open_) {
        setStatus(screen.message, Forms::StatusKind::Success);
    }
    return open_;
}

bool ManagerUI::takeBrowseRequest(std::string& collection)
{
    return afterReload_.takeBrowse(open_, reloadRequested_, collection);
}

void ManagerUI::show()
{
    browserOpening_.reset();
    const Navigator::Frame* frame = nav_.current();
    if (!frame) {
        close();
        return;
    }
    // The old screen goes first: a text field still being edited there is cancelled on its own screen
    destroyPage();
    view_.discardImages();
    retireScreen();

    bool wasOpen = open_;
    status_.clear();
    capture_.clear();   // a capture belongs to the screen that asked for it
    leave_.reset();
    launches_.screenChanged();
    view_.reset();
    focusNext_ = true;
    open_ = true;
    if (!wasOpen) {
        model_.refresh();
        cursorWasShown_ = SDL_ShowCursor(SDL_QUERY);
        // Files may have changed since the last look (games added, a Catalog install)
        if (library_) {
            library_->rebuild();
        }
    }
    model_.setScreen(frame->form, frame->params);
    const Forms::Form* shown = form(frame->form);
    if (shown) {
        Forms::applyDefaults(*shown, model_);
    }

    // A screen that draws itself (the theme editor) does so whatever formsRenderer says
    if (showImGuiScreen(*frame)) {
        navHeld_.fill(true);
        return;
    }

    // Draw it as a layout page unless asked for ImGui, or the page can't be built
    std::string renderer;
    config_.getProperty("formsRenderer", renderer);
    if (Utils::toLower(renderer) == "imgui" || !buildPage()) {
        SDL_ShowCursor(SDL_ENABLE);
    }
    else {
        widget_->restoreFocus(frame->focus);
    }

    // Controls held while opening (e.g. the Select that chose it) must be released first
    navHeld_.fill(true);
}

void ManagerUI::close()
{
    launchReturn_.reset();
    systemPick_.reset();
    browserOpening_.reset();
    mouseUse_.reset();
    nav_.clear();
    leave_.reset();
    capture_.clear();
    if (!open_) {
        return;
    }
    open_ = false;
    destroyPage();
    view_.discardImages();
    retireScreen();
    SDL_ShowCursor(cursorWasShown_);

    bool reload = pendingChanges().needsReloadOnClose();
    // Changes that asked for a reload once the user was done, including library edits
    if (library_ && library_->changesPending()) {
        library_->clearPendingChanges();
    }
    if (reload) {
        reloadWhenClosed_ = false;
        reloadRequested_ = true;
        // A bare install that's now set up (e.g. its first system added in
        // Systems) keeps its built-in defaults in a settings.conf, as after a
        // Catalog install; without one, RetroFE would open Manage at every start
        std::string error;
        if (!model_.ensureSettingsFile(error)) {
            LOG_ERROR("Manager", error);
        }
    }
}

bool ManagerUI::showImGuiScreen(const Navigator::Frame& frame)
{
    if (frame.form != Themes::editorForm && frame.form != Themes::layoutEditorForm) {
        return false;
    }
    ThemeEditorScreen::Context context;
    context.config = &config_;
    context.window = window_;
    context.renderer = renderer_;
    context.fontCache = fontCache_;
    context.scale = scale_;
    context.root = Configuration::absolutePath;
    context.layouts = Configuration::layoutsPath();
    context.loadCollection = loadCollection_;
    context.currentCollection = currentCollection_;
    context.requestReload = [this] { reloadWhenClosed_ = true; };
    context.library = library_.get();
    screen_ = Themes::makeEditorScreen(frame.form, context, frame.params);
    SDL_ShowCursor(SDL_ENABLE);
    return true;
}

void ManagerUI::retireScreen()
{
    // Not deleted here: this can run inside the render pass (a button's
    // result), and a screen frees textures and pages. update() does it.
    if (screen_) {
        screen_->retire();
        retired_.push_back(std::move(screen_));
    }
}

bool ManagerUI::buildPage()
{
    const Navigator::Frame* frame = nav_.current();
    const Forms::Form* shown = frame ? form(frame->form) : nullptr;
    if (!shown || !fontCache_) {
        return false;
    }

    // The layout styles a screen with forms/<id>/<page>.xml, or all of them with
    // forms/<page>.xml, for the page collections are drawn with (LayoutPages::formFile)
    std::string layoutName;
    config_.getProperty(OPTION_LAYOUT, layoutName);
    std::string formsPath = Utils::combinePath(Configuration::layoutsPath(), layoutName, "forms");
    std::string file = layoutName.empty() ? "" : LayoutPages::formFile(formsPath, frame->form, layoutPage_);

    PageBuilder builder(layoutName, "layout", config_, fontCache_);
    if (!file.empty()) {
        builder.setLayoutFile(file);
    }
    else {
        builder.setLayoutXml(Builtin::layoutXml("form"), formsPath);
    }

    Page* page = builder.buildPage();
    if (!page || builder.formWidgets().empty()) {
        LOG_ERROR("Manager", "The " + frame->form + " page has no <form>, using ImGui instead");
        if (page) {
            page->deInitialize();
            delete page;
        }
        return false;
    }

    widget_ = builder.formWidgets().front();
    widget_->bind(shown, &model_);
    std::string letterUp, letterDown;
    config_.getProperty("controls.letterUp", letterUp);
    config_.getProperty("controls.letterDown", letterDown);
    widget_->setLetterControls(!letterUp.empty() && !letterDown.empty());
    widget_->setCanBrowse(true);   // the file browser, on every platform
    widget_->setBackCloses(nav_.depth() <= 1);
    page_ = page;
    page_->allocateGraphicsMemory();
    page_->start();
    return true;
}

void ManagerUI::syncKeyboard()
{
    bool editing = widget_ && widget_->isEditing();
    if (editing && !wasEditing_) {
        typedOnKeyboard_ = false;   // a new edit
    }
    wasEditing_ = editing;

    if (!editing) {
        closeKeyboard();
        return;
    }
    std::string enabled = "yes";
    config_.getProperty("onScreenKeyboard", enabled);
    if (!keyboardPage_ && !typedOnKeyboard_ && Forms::isTruthy(enabled)) {
        openKeyboard();
    }
}

bool ManagerUI::openKeyboard()
{
    std::string layoutName;
    config_.getProperty(OPTION_LAYOUT, layoutName);
    std::string formsPath = Utils::combinePath(Configuration::layoutsPath(), layoutName, "forms");
    std::string file = layoutName.empty() ? "" : LayoutPages::keyboardFile(formsPath, layoutPage_);

    PageBuilder builder(layoutName, "layout", config_, fontCache_);
    if (!file.empty()) {
        builder.setLayoutFile(file);
    }
    else {
        builder.setLayoutXml(Builtin::layoutXml("keyboard"), formsPath);
    }

    Page* page = builder.buildPage();
    if (!page || builder.keyboardWidgets().empty()) {
        LOG_ERROR("Manager", "The keyboard page has no <keyboard>");
        if (page) {
            page->deInitialize();
            delete page;
        }
        typedOnKeyboard_ = true;   // don't try again for this edit
        return false;
    }

    const Forms::Field* field = widget_->editingField();
    keyboard_ = builder.keyboardWidgets().front();
    std::string letterUp, letterDown;
    config_.getProperty("controls.letterUp", letterUp);
    config_.getProperty("controls.letterDown", letterDown);
    keyboard_->setLetterControls(!letterUp.empty() && !letterDown.empty());
    keyboard_->attach(&widget_->editText(), field ? field->label : "", field && field->type == Forms::FieldType::Number,
                      field && field->multiline);
    keyboardPage_ = page;
    keyboardPage_->allocateGraphicsMemory();
    keyboardPage_->start();

    // Controls still held from starting the edit must be released first
    navHeld_.fill(true);
    return true;
}

void ManagerUI::closeKeyboard()
{
    if (!keyboardPage_) {
        return;
    }
    keyboard_->detach();
    keyboardPage_->stop();
    keyboardPage_->deInitialize();
    delete keyboardPage_;
    keyboardPage_ = nullptr;
    keyboard_ = nullptr;
}

void ManagerUI::destroyPage()
{
    closeKeyboard();
    wasEditing_ = false;
    if (!page_) {
        return;
    }
    page_->stop();
    page_->deInitialize();
    delete page_;
    page_ = nullptr;
    widget_ = nullptr;
}

bool ManagerUI::captureEvent(const SDL_Event& e)
{
    if (!Manager::CaptureEvents::isInput(e)) {
        return false;
    }
    Manager::CaptureEvents::Event event = captureEvents_.read(e);
    float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    if (event.type == Manager::CaptureEvents::Event::Type::Down) {
        capture_.press(event.input, now);
    }
    else if (event.type == Manager::CaptureEvents::Event::Type::Up) {
        capture_.release(event.input, now);
    }
    // Nothing reaches the screen meanwhile (navigate() waits too), but
    // RetroFE's input still follows keys and buttons going up and down, so
    // a control held when the capture started doesn't stay pressed
    return e.type == SDL_TEXTINPUT || e.type == SDL_TEXTEDITING || e.type == SDL_MOUSEBUTTONDOWN
        || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL;
}

void ManagerUI::updateCapture()
{
    if (capture_.state() == Manager::InputCapture::State::Idle) {
        return;
    }
    float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    capture_.update(now);
    switch (capture_.state()) {
    case Manager::InputCapture::State::Waiting: {
        std::string prompt = capture_.prompt(now);
        if (prompt != capturePrompt_) {
            capturePrompt_ = prompt;
            setStatus(prompt, Forms::StatusKind::Info);
        }
        break;
    }
    case Manager::InputCapture::State::Captured: {
        std::string token = capture_.token();
        std::string bind = captureBind_;
        std::string action = captureAction_;
        endCapture();
        LOG_INFO("Manager", "Captured input " + token);
        model_.set(bind, token);
        bool changed = false;
        if (!action.empty()) {
            auto result = model_.runAction(action);
            changed = result.ok && !result.info;
            handleResult(result);
        }
        if (widget_) {
            if (changed) widget_->markEdited();
            widget_->invalidate();
        }
        break;
    }
    case Manager::InputCapture::State::Cancelled:
    case Manager::InputCapture::State::TimedOut: {
        std::string message = capture_.endMessage();
        endCapture();
        setStatus(message, Forms::StatusKind::Info);   // cancelled or timed out: nothing was added
        break;
    }
    case Manager::InputCapture::State::Idle:
        break;
    }
}

void ManagerUI::endCapture()
{
    capture_.clear();
    capturePrompt_.clear();
    // What was pressed for the capture mustn't also act on the screen
    navHeld_.fill(true);
}

bool ManagerUI::handleEvent(const SDL_Event& e)
{
    // Back from a game, the screens it was started from come back once the
    // library is read again, unless the user does something first
    if (launchReturn_) {
        bool cancel = false;
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            cancel = launchReturnWait_.input(0, e.key.keysym.scancode, e.type == SDL_KEYDOWN, e.key.repeat != 0);
        }
        else if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
            int64_t id = static_cast<int64_t>(e.cbutton.which) * 256 + e.cbutton.button;
            cancel = launchReturnWait_.input(1, id, e.type == SDL_CONTROLLERBUTTONDOWN);
        }
        else if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) {
            int64_t id = static_cast<int64_t>(e.jbutton.which) * 256 + e.jbutton.button;
            cancel = launchReturnWait_.input(2, id, e.type == SDL_JOYBUTTONDOWN);
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL) {
            cancel = true;
        }
        if (cancel) {
            cancelLaunchReturn();
        }
        else if (e.type == SDL_KEYDOWN || e.type == SDL_TEXTINPUT || e.type == SDL_CONTROLLERBUTTONDOWN
                 || e.type == SDL_JOYBUTTONDOWN) {
            return true;   // a held input mustn't leave the waiting screen either
        }
    }
    if (!context_) {
        return false;
    }
    if (open_) {
        if (SDL::hasWarped()) mouseUse_.warped(SDL::lastWarp());
        if (e.type == SDL_MOUSEMOTION) {
            mouseUse_.motion(SDL_GetTicks(), e.motion.xrel, e.motion.yrel, e.motion.which == SDL_TOUCH_MOUSEID);
        }
        if (e.type == SDL_MOUSEBUTTONDOWN) mouseUse_.press(SDL_GetTicks(), e.button.which == SDL_TOUCH_MOUSEID);
    }
    // Press it now takes every input first, the Settings key and Escape included
    if (open_ && capture_.active()) {
        return captureEvent(e);
    }
    // Kiosk mode keeps people out of Settings, where they could turn it off
    if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == toggleKey_ && !locked_) {
        // Opens Settings, or closes it; from another screen it switches to
        // Settings. Either way the screens open now are left.
        if (!e.key.repeat && mayLeave(true, Manager::Leave::Close)) {
            nav_.toggle(settingsForm) ? show() : close();
        }
        return true;
    }
    if (!open_) {
        return false;
    }

    if (page_) {
        // Typing on a real keyboard puts the on-screen one away for this edit, so keys
        // that are also mapped controls (e.g. back = B) just type
        if (keyboardPage_) {
            SDL_Keycode key = e.type == SDL_KEYDOWN ? e.key.keysym.sym : 0;
            bool ctrl = e.type == SDL_KEYDOWN && (e.key.keysym.mod & KMOD_CTRL);
            bool printable = key >= 32 && key < 127 && !(e.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI));
            if (printable || e.type == SDL_TEXTINPUT) {
                typedOnKeyboard_ = true;
                closeKeyboard();
            }
            // On the on-screen keyboard Enter is Select, which presses the focused key
            // (Ctrl+Enter still keeps a multi-line text), and the arrows move between
            // keys; Home, End and Page Up/Down still move in a multi-line text
            else if (((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !ctrl)
                     || key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT) {
                return false;
            }
        }
        // Typing goes to the field being edited; everything else is left for RetroFE's controls
        if (widget_->handleTextEvent(e)) {
            return true;
        }
        // Escape always goes back, whatever the controls say
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !e.key.repeat) {
            // Escape is usually the Back control too: this press is dealt with here, not again in navigate()
            navHeld_[5] = true;
            if (!widget_->navigate(FormWidget::Nav::Back)) {
                requestBack();
            }
            return true;
        }
        return false;
    }

    ImGui::SetCurrentContext(context_);

    // Escape goes back, unless it's being used to leave a text field
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !e.key.repeat && !ImGui::GetIO().WantTextInput) {
        // Escape is usually the Back control too: this press is dealt with here, not again in navigate()
        navHeld_[static_cast<size_t>(Manager::ImGuiScreen::Control::Back)] = true;
        if (screen_ && screen_->escape()) {
            return true;   // it closed a dialog or cancelled a drag
        }
        if (!view_.cancelConfirm()) {
            requestBack();
        }
        return true;
    }
    ImGui_ImplSDL2_ProcessEvent(&e);
    // A screen driven by RetroFE's controls: RetroFE's input sees the event
    // too (joysticks are read from their events), and navigate() follows.
    // Not a mouse click: the mouse is ImGui's there, and a control mapped to
    // a mouse button would turn every click into a press. (Releases still
    // go through, so a button held when the screen opened doesn't stick.)
    return e.type == SDL_MOUSEBUTTONDOWN || !(screen_ && screen_->wantsControls());
}

void ManagerUI::navigate(const Controls& held, float now)
{
    // A press before the screens a game was started from come back keeps the
    // user where they are (controls held since Manage opened don't count)
    for (size_t i = 0; launchReturn_ && i < held.size(); i++) {
        if (launchReturnWait_.input(3, static_cast<int64_t>(i), held[i])) {
            cancelLaunchReturn();
        }
    }
    if (launchReturn_) {
        navHeld_ = held;
        return;
    }
    using Control = Manager::ImGuiScreen::Control;
    // Controls' order; page up/down jump to a form's pinned action bar and back
    static constexpr FormWidget::Nav navs[] = {
        FormWidget::Nav::Up, FormWidget::Nav::Down, FormWidget::Nav::Left,
        FormWidget::Nav::Right, FormWidget::Nav::Select, FormWidget::Nav::Back,
        FormWidget::Nav::PageUp, FormWidget::Nav::PageDown, FormWidget::Nav::LetterUp, FormWidget::Nav::LetterDown
    };
    static_assert(sizeof(navs) / sizeof(navs[0]) == std::tuple_size<Controls>::value, "a Nav per control");
    const float repeatDelay = 0.4f;
    const float repeatInterval = 0.09f;

    // Waiting for "press it now": the press is the capture's, not the screen's
    if (capture_.active()) {
        return;
    }

    // Whether control i acts now: once when pressed, and while held for the
    // ones that repeat (everything but select and back)
    auto acts = [&](size_t i, bool& repeat) {
        bool repeats = i != static_cast<size_t>(Control::Select) && i != static_cast<size_t>(Control::Back);
        // On a form, page up/down jump into the bar and back: once a press
        if (page_ && (i == static_cast<size_t>(Control::PageUp) || i == static_cast<size_t>(Control::PageDown))) {
            repeats = false;
        }
        bool act = false;
        repeat = false;
        if (held[i] && !navHeld_[i]) {
            act = true;
            navRepeatAt_[i] = now + repeatDelay;
        }
        else if (held[i] && navHeld_[i] && repeats && now >= navRepeatAt_[i]) {
            act = true;
            repeat = true;
            navRepeatAt_[i] = now + repeatInterval;
        }
        navHeld_[i] = held[i];
        return act;
    };

    // An ImGui screen that asked for RetroFE's controls gets every press
    if (screen_ && !page_) {
        if (!screen_->wantsControls()) {
            return;
        }
        screen_->controlsHeld(held);
        for (size_t i = 0; i < held.size() && screen_; i++) {
            bool repeat = false;
            if (acts(i, repeat)) {
                // As on a layout-drawn screen: anything but Back after a warning starts over
                if (static_cast<Control>(i) != Control::Back) {
                    leave_.reset();
                }
                screen_->control(static_cast<Control>(i), repeat);
            }
        }
        return;
    }

    syncKeyboard();
    for (size_t i = 0; i < held.size() && widget_; i++) {
        FormWidget::Nav nav = navs[i];
        bool repeat = false;
        if (!acts(i, repeat)) {
            continue;
        }
        // While a field is edited the controls type on the on-screen keyboard
        // (page up/down mean nothing there)
        if (keyboard_ && (navs[i] == FormWidget::Nav::PageUp || navs[i] == FormWidget::Nav::PageDown)) {
            continue;
        }
        if (keyboard_) {
            KeyboardWidget::Result result = keyboard_->navigate(nav);
            if (result == KeyboardWidget::Result::LineUp || result == KeyboardWidget::Result::LineDown) {
                // A line as the field draws it, which only the field knows
                widget_->moveEditLine(result == KeyboardWidget::Result::LineUp ? -1 : 1);
            }
            else if (result != KeyboardWidget::Result::None) {
                widget_->finishEditing(result == KeyboardWidget::Result::Done);
                syncKeyboard();
            }
            else {
                widget_->sendLiveText();   // a key was pressed
            }
            continue;
        }
        // Back after a warning about leaving leaves; anything else and it warns again
        if (nav != FormWidget::Nav::Back) {
            leave_.reset();
        }
        if (!widget_->navigate(nav)) {
            requestBack();
        }
        else {
            syncKeyboard();   // a Select may have started editing
        }
    }
}

void ManagerUI::update(float dt)
{
    // Screens left since the last frame free their pages and textures here, outside the render pass
    for (auto& screen : retired_) {
        screen->shutdown();
    }
    retired_.clear();
    if (!open_) {
        return;
    }
    if (launchReturn_ && Manager::LaunchReturnWait::ready(library_ && library_->index(),
        library_ && library_->status().state == Library::Indexer::State::Building)) {
        auto returning = std::move(*launchReturn_);
        launchReturn_.reset();
        finishLaunchReturn(returning.first, returning.second);
    }
    if (browserOpening_) {
        auto opening = browserOpening_;
        std::lock_guard<std::mutex> lock(opening->mutex);
        if (opening->ready) {
            if (form(Browser::formId)) {
                nav_.push(Browser::formId, opening->places, widget_ ? widget_->focus() : Forms::Focus{});
                if (!opening->folder.empty()) nav_.push(Browser::formId, opening->folder, {});
                show();
            }
            browserOpening_.reset();
        }
    }
    updateCapture();
    if (systemPick_) {
        // The file browser's "Browse with Windows...": it blocks until closed
        Manager::Services::SystemPick pick = std::move(*systemPick_);
        systemPick_.reset();
        std::string path = pick.path;
        bool chosen = pick.folder ? FileDialog::pickFolder(window_, pick.title, path)
                                  : FileDialog::pickFile(window_, pick.title, pick.filterName, pick.pattern, path);
        navHeld_.fill(true);   // whatever was pressed meanwhile was the dialog's
        if (chosen && pick.chosen && open_) {
            handleResult(pick.chosen(path));
        }
    }
    model_.update(dt);
    if (screen_) {
        screen_->update(dt);
        if (screen_->takeBack()) {
            requestBack();
        }
    }
    else if (page_) {
        page_->update(dt);
        if (keyboardPage_) {
            keyboardPage_->update(dt);
        }
        handleWidgetResults();
        if (widget_) {
            syncKeyboard();
        }
    }
    else {
        view_.update(window_, model_);
    }
}

void ManagerUI::handleWidgetResults()
{
    if (auto browse = widget_->takeBrowse()) {
        openBrowser(*browse);
    }
    if (widget_) {
        if (auto result = widget_->takeResult()) {
            handleResult(*result);
        }
    }
}

void ManagerUI::openBrowser(const FormWidget::Browse& browse)
{
    if (browserOpening_) return;
    // Never the system's dialog from a press: the browser is a screen like the others
    Browser::Request request;
    request.bind = browse.bind;
    request.folder = browse.folder;
    request.label = browse.title;
    request.filterName = browse.filterName;
    request.pattern = browse.pattern;
    request.value = model_.get(browse.bind);
    request.start = browse.start.empty() ? "" : model_.get(browse.start);
    request.matching = browse.matching.empty() ? "" : model_.get(browse.matching);
    auto opening = std::make_shared<BrowserOpening>();
    opening->places = Browser::screenParams(request);
    browserOpening_ = opening;
    auto root = std::filesystem::path(Configuration::absolutePath);
    std::thread([opening, root] {
        Forms::Params folder;
        try { Browser::openingParams(opening->places, root, folder); }
        catch (const std::exception&) {}
        std::lock_guard<std::mutex> lock(opening->mutex);
        opening->folder = std::move(folder);
        opening->ready = true;
    }).detach();
}

void ManagerUI::applyPick(const Forms::Model::Result& result)
{
    // Back past the browser's screens to the field's, without asking (the
    // browser has nothing to lose), and give the field its value there
    bool moved = false;
    while (nav_.depth() > 1 && nav_.current() && nav_.current()->form == Browser::formId) {
        nav_.back();
        moved = true;
    }
    if (moved) {
        show();
    }
    const std::string before = model_.get(result.pickFor);
    model_.set(result.pickFor, result.pick);
    if (widget_) {
        if (model_.get(result.pickFor) != before) {
            widget_->markEdited();
        }
        widget_->invalidate();
    }
    setStatus(result.message, Forms::statusKind(result));
}

void ManagerUI::handleResult(const Forms::Model::Result& result)
{
    // Something else was done after a warning about leaving (an action, a
    // link; with either renderer): the next attempt to leave warns again
    leave_.reset();
    // A game to start: close (asking about unsaved changes first, like Close;
    // asking again leaves them), and RetroFE starts it once it's reloaded if it has to
    if (launches_.handle(result.ok, [this](bool discard) {
            if (discard) {
                leave_.arm(Manager::Leave::Close);   // warned last time: this time the changes go
            }
            return requestClose();
        })) {
        return;
    }
    if (result.ok && result.reload) {
        // Where to go once RetroFE is back: Manage on a screen, a collection
        if (!result.open.empty()) {
            const Navigator::Frame* frame = nav_.current();
            afterReload_.reopen({ result.open, Forms::openParams(frame ? frame->params : Forms::Params{}, result), result.message });
        }
        if (!result.browse.empty()) {
            afterReload_.browse(result.browse);
        }
        reloadRequested_ = true;
        close();
        return;
    }
    // A collection to show (the Catalog list): close, asking about unsaved
    // changes as Close does, and RetroFE goes there
    if (result.ok && !result.browse.empty()) {
        if (requestClose()) {
            afterReload_.browse(result.browse);
        }
        return;
    }

    if (result.ok && !result.pickFor.empty()) {
        applyPick(result);
        return;
    }
    // After success an action can move to another screen, which then shows its message
    if (result.ok && result.back) {
        if (!requestBack()) {
            return;   // it says why
        }
    }
    else if (result.ok && result.replace && !result.open.empty() && this->form(result.open)) {
        // The screen the action ran on is done with (e.g. an add form): the
        // result takes its place, so Back goes to the screen before it
        const Navigator::Frame* frame = nav_.current();
        Forms::Params params = Forms::openParams(frame ? frame->params : Forms::Params{}, result);
        if (result.open == Browser::formId) Browser::replaceFolder(nav_, params);
        else nav_.replace(result.open, params);
        show();
    }
    else if (result.ok && !result.open.empty()) {
        const Navigator::Frame* frame = nav_.current();
        if (!push(result.open, Forms::openParams(frame ? frame->params : Forms::Params{}, result))) {
            return;
        }
    }
    // Focus where the action says, now or once that field shows; on the screen it ended on
    if (open_ && result.ok && !result.focus.empty() && widget_) {
        widget_->focusOn(result.focus);
    }
    if (open_) {
        setStatus(result.message, Forms::statusKind(result));
    }
    // Press it now: the next input becomes a token for result.capture
    if (open_ && result.ok && !result.capture.empty()) {
        float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        captureEvents_.start();   // sticks held now must come back first
        capture_.start(now);
        captureBind_ = result.capture;
        captureAction_ = result.captureAction;
        capturePrompt_ = capture_.prompt(now);
        setStatus(capturePrompt_, Forms::StatusKind::Info);
    }
}

void ManagerUI::setStatus(const std::string& message, Forms::StatusKind kind)
{
    status_ = message;
    statusKind_ = kind;
    if (widget_) {
        widget_->setStatus(message, kind);
    }
    if (screen_ && !message.empty()) {
        screen_->setStatus(message, kind == Forms::StatusKind::Error, kind == Forms::StatusKind::Info);
    }
}

void ManagerUI::drawPage()
{
    if (open_ && page_) {
        page_->draw();
        if (keyboardPage_) {
            keyboardPage_->draw();
        }
    }
    if (open_ && screen_) {
        screen_->drawTargets();   // e.g. the theme editor's preview page, into its own texture
    }
}

void ManagerUI::render()
{
    if (!context_ || !open_ || page_) {
        return;
    }
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    if (screen_) {
        screen_->beforeFrame();
    }
    ImGui::NewFrame();

    if (screen_) {
        screen_->draw();
    }
    else {
        drawWindow();
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

bool ManagerUI::takeReloadRequest(bool backHome)
{
    // Reloading destroys the Catalog, so it waits for installs to finish
    if (backHome && catalog_ && catalog_->hasChanges()) {
        reloadRequested_ = true;
    }
    if (!reloadRequested_ || (catalog_ && catalog_->isBusy())) {
        return false;
    }
    reloadRequested_ = false;
    launches_.reloading();   // a game waiting for this reload starts once RetroFE is back
    afterReload_.reloading();   // and Manage, or a collection, is shown once it's back
    // Catalog installs finished meanwhile go on the Main menu now: the
    // Catalog that knows about them doesn't survive the reload
    takeCatalogChanges();
    return true;
}

bool ManagerUI::takeLaunchRequest(LaunchRequest& request)
{
    Manager::LaunchQueue::Take take = launches_.take(open_, reloadRequested_, catalog_ && catalog_->isBusy(), request);
    if (take == Manager::LaunchQueue::Take::Dropped) {
        LOG_WARNING("Manager", "Not starting the game Manage asked for: RetroFE can't reload while the Catalog installs");
    }
    return take == Manager::LaunchQueue::Take::Start;
}

void ManagerUI::returnFromLaunch(const LaunchRequest& request, const std::string& problem)
{
    if (!context_ || locked_) {
        return;
    }
    // A published index can validate the old stack while its refresh builds.
    launchReturn_.reset();
    // Where the user was, as far as the screens go: Manage > Games > the game
    open("manage");
    if (!open_ || !push("games", { { "collection", request.collection } })
        || !push("game", { { "collection", request.collection }, { "id", request.game } })) {
        return;
    }
    launchReturnWait_ = {};
    if (Manager::LaunchReturnWait::ready(library_ && library_->index(),
        library_ && library_->status().state == Library::Indexer::State::Building)) {
        finishLaunchReturn(request, problem);
        return;
    }
    launchReturn_ = std::make_pair(request, problem);
    // Back on the button that started it, so it can be started again
    if (widget_) {
        widget_->focusOn("game.launch");
    }
    setStatus("Getting your screens back...", Forms::StatusKind::Info);
}

void ManagerUI::finishLaunchReturn(const LaunchRequest& request, const std::string& problem)
{
    auto index = library_ ? library_->index() : nullptr;
    if (index && request.returning.restore(nav_, *index, [this](const std::string& id) { return form(id) != nullptr; })) {
        model_.restoreView(request.returning.providers);
        show();
    }
    // Back on the button that started it, so it can be started again
    if (widget_) {
        widget_->focusOn("game.launch");
    }
    setStatus(problem.empty() ? "Back from " + (request.title.empty() ? request.game : request.title) + "." : problem,
              problem.empty() ? Forms::StatusKind::Info : Forms::StatusKind::Error);
}

void ManagerUI::cancelLaunchReturn()
{
    // The user moved first: they stay on the usual screens, with the message
    // they'd have had anyway
    if (!launchReturn_) {
        return;
    }
    LaunchRequest request = launchReturn_->first;
    std::string problem = launchReturn_->second;
    launchReturn_.reset();
    setStatus(problem.empty() ? "Back from " + (request.title.empty() ? request.game : request.title) + "." : problem,
              problem.empty() ? Forms::StatusKind::Info : Forms::StatusKind::Error);
}

std::string ManagerUI::gameTitle(const std::string& collection, const std::string& game) const
{
    // The library's title (metadata), as the Games screens show it
    std::shared_ptr<const Library::Index> idx = library_ ? library_->index() : nullptr;
    int g = idx ? idx->findGame(collection, game) : -1;
    if (g >= 0 && !idx->games[static_cast<size_t>(g)].title.empty()) {
        return idx->games[static_cast<size_t>(g)].title;
    }
    return game;
}

void ManagerUI::setLocked(bool locked)
{
    if (locked && !locked_ && open_) {
        close();
    }
    locked_ = locked;
}

void ManagerUI::drawWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    const Navigator::Frame* frame = nav_.current();
    if (!frame) {
        return;
    }
    const Forms::Form* shown = form(frame->form);

    // Dim the page behind the window
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));

    float width = std::min(io.DisplaySize.x - 40.0f * scale_, 760.0f * scale_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, io.DisplaySize.y - 40.0f * scale_));

    // "###manager" keeps one window while the title changes with the form
    std::string title = (shown ? Forms::title(*shown, model_) : frame->form) + "###manager";
    if (focusNext_) {
        ImGui::SetNextWindowFocus();
        focusNext_ = false;
    }
    bool keepOpen = true;
    ImGui::Begin(title.c_str(), &keepOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    // Changes waiting for a reload, as the page's title row shows them
    std::string pending = model_.get("info:pendingChanges");
    if (!pending.empty()) {
        ImGui::TextDisabled("%s", pending.c_str());
    }

    if (shown) {
        view_.draw(*shown, model_);
    }
    else {
        ImGui::TextWrapped("This form couldn't be loaded. Check the log for details.");
    }

    std::optional<Forms::Model::Result> result = view_.takeResult();

    if (!status_.empty()) {
        ImGui::Spacing();
        // Red for errors, green for successes, the plain text colour when nothing happened
        ImVec4 color = statusKind_ == Forms::StatusKind::Error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                     : statusKind_ == Forms::StatusKind::Info  ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                               : ImVec4(0.5f, 0.9f, 0.5f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }

    bool goBack = false;
    bool deeper = nav_.depth() > 1;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s: Settings    Esc: %s", SDL_GetScancodeName(toggleKey_), deeper ? "back" : "close");
    float closeWidth = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float backWidth = ImGui::CalcTextSize("Back").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    if (deeper) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - closeWidth - backWidth - ImGui::GetStyle().ItemSpacing.x);
        goBack = ImGui::Button("Back");
        ImGui::SameLine();
    }
    else {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - closeWidth);
    }
    if (ImGui::Button("Close")) {
        keepOpen = false;
    }

    ImGui::End();

    // Navigating replaces the form being drawn, so only once the window is done
    if (!keepOpen) {
        requestClose();
    }
    else if (goBack) {
        requestBack();
    }
    else if (result) {
        handleResult(*result);
    }
}

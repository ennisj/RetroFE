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
#include "FileDialog.h"
#include "Setup.h"
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
}

ManagerUI::~ManagerUI()
{
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

    // controls.manager picks the key that opens Settings; the first keyboard key listed is used
    std::string toggle;
    if (config_.getProperty("controls.manager", toggle)) {
        std::vector<std::string> keys;
        Utils::listToVector(toggle, keys, ',');
        SDL_Scancode key = keys.empty() ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(Utils::trimEnds(keys[0]).c_str());
        if (key != SDL_SCANCODE_UNKNOWN) {
            toggleKey_ = key;
        }
        else {
            LOG_WARNING("Manager", "controls.manager doesn't start with a keyboard key, using " + std::string(SDL_GetScancodeName(toggleKey_)));
        }
    }

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
    if (!providersAdded_) {
        Manager::Services services{ config_, catalog_.get() };
        services.requestReload = [this] { reloadWhenClosed_ = true; };
        Manager::registerProviders(model_, services);
        providersAdded_ = true;
    }
    LOG_INFO("Manager", "Press " + std::string(SDL_GetScancodeName(toggleKey_)) + " to open Settings");
    return true;
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
    targets.layouts = Utils::combinePath(Configuration::absolutePath, "layouts");
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
    if (!context_) {
        return false;
    }
    // A form that doesn't exist (e.g. a typo in open="") is reported where the user is
    if (!this->form(form)) {
        LOG_ERROR("Manager", "Can't open " + form + ": there's no such form");
        setStatus("This screen isn't available (" + form + "). Check the log for details.", true);
        return false;
    }
    nav_.push(form, params, widget_ ? widget_->focus() : Forms::Focus{});
    show();
    return true;
}

void ManagerUI::back()
{
    if (nav_.back()) {
        show();
    }
    else {
        close();
    }
}

bool ManagerUI::mayLeave(bool everything)
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
    std::string warning;
    for (const Manager::Screen& screen : leaving) {
        warning = model_.leaving(screen);
        if (!warning.empty()) {
            break;
        }
    }
    if (warning.empty()) {
        leaveWarned_ = false;
        return true;
    }
    // Asked once already: leave, dropping the changes
    if (leaveWarned_) {
        leaveWarned_ = false;
        for (const Manager::Screen& screen : leaving) {
            model_.discard(screen);
        }
        return true;
    }
    leaveWarned_ = true;
    setStatus(warning, true);
    return false;
}

bool ManagerUI::requestBack()
{
    if (!mayLeave(nav_.depth() <= 1)) {
        return false;
    }
    back();
    return true;
}

bool ManagerUI::requestClose()
{
    if (!mayLeave(true)) {
        return false;
    }
    close();
    return true;
}

void ManagerUI::show()
{
    const Navigator::Frame* frame = nav_.current();
    if (!frame) {
        close();
        return;
    }
    // The old screen goes first: a text field still being edited there is cancelled on its own screen
    destroyPage();
    view_.discardImages();

    bool wasOpen = open_;
    status_.clear();
    leaveWarned_ = false;
    view_.reset();
    focusNext_ = true;
    open_ = true;
    if (!wasOpen) {
        model_.refresh();
        cursorWasShown_ = SDL_ShowCursor(SDL_QUERY);
    }
    model_.setScreen(frame->form, frame->params);
    const Forms::Form* shown = form(frame->form);
    if (shown) {
        Forms::applyDefaults(*shown, model_);
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
    nav_.clear();
    leaveWarned_ = false;
    if (!open_) {
        return;
    }
    open_ = false;
    destroyPage();
    view_.discardImages();
    SDL_ShowCursor(cursorWasShown_);

    // Changes that asked for a reload once the user was done
    if (reloadWhenClosed_) {
        reloadWhenClosed_ = false;
        reloadRequested_ = true;
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
    std::string formsPath = Utils::combinePath(Configuration::absolutePath, "layouts", layoutName, "forms");
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
    widget_->setCanBrowse(FileDialog::isSupported());
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
    std::string formsPath = Utils::combinePath(Configuration::absolutePath, "layouts", layoutName, "forms");
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
    keyboard_->attach(&widget_->editText(), field ? field->label : "", field && field->type == Forms::FieldType::Number);
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

bool ManagerUI::handleEvent(const SDL_Event& e)
{
    if (!context_) {
        return false;
    }
    // Kiosk mode keeps people out of Settings, where they could turn it off
    if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == toggleKey_ && !locked_) {
        // Opens Settings, or closes it; from another screen it switches to
        // Settings. Either way the screens open now are left.
        if (!e.key.repeat && mayLeave(true)) {
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
            bool printable = key >= 32 && key < 127 && !(e.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI));
            if (printable || e.type == SDL_TEXTINPUT) {
                typedOnKeyboard_ = true;
                closeKeyboard();
            }
            // On the on-screen keyboard Enter is Select, which presses the focused key
            else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
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
        if (!view_.cancelConfirm()) {
            requestBack();
        }
        return true;
    }
    ImGui_ImplSDL2_ProcessEvent(&e);
    return true;
}

void ManagerUI::navigate(const std::array<bool, 6>& held, float now)
{
    static constexpr FormWidget::Nav navs[] = {
        FormWidget::Nav::Up, FormWidget::Nav::Down, FormWidget::Nav::Left,
        FormWidget::Nav::Right, FormWidget::Nav::Select, FormWidget::Nav::Back
    };
    const float repeatDelay = 0.4f;
    const float repeatInterval = 0.09f;

    syncKeyboard();
    for (size_t i = 0; i < held.size() && widget_; i++) {
        bool repeats = i < 4;  // up/down/left/right
        bool act = false;
        if (held[i] && !navHeld_[i]) {
            act = true;
            navRepeatAt_[i] = now + repeatDelay;
        }
        else if (held[i] && repeats && now >= navRepeatAt_[i]) {
            act = true;
            navRepeatAt_[i] = now + repeatInterval;
        }
        navHeld_[i] = held[i];
        if (!act) {
            continue;
        }

        // While a field is edited the controls type on the on-screen keyboard
        if (keyboard_) {
            KeyboardWidget::Result result = keyboard_->navigate(navs[i]);
            if (result != KeyboardWidget::Result::None) {
                widget_->finishEditing(result == KeyboardWidget::Result::Done);
                syncKeyboard();
            }
            else {
                widget_->sendLiveText();   // a key was pressed
            }
            continue;
        }
        // Back after a warning about leaving leaves; anything else and it warns again
        if (navs[i] != FormWidget::Nav::Back) {
            leaveWarned_ = false;
        }
        if (!widget_->navigate(navs[i])) {
            requestBack();
        }
        else {
            syncKeyboard();   // a Select may have started editing
        }
    }
}

void ManagerUI::update(float dt)
{
    if (!open_) {
        return;
    }
    model_.update(dt);
    if (page_) {
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
        std::string path = model_.get(browse->bind);
        bool chosen = browse->folder
            ? FileDialog::pickFolder(window_, browse->title, path)
            : FileDialog::pickFile(window_, browse->title, browse->filterName, browse->pattern, path);
        if (chosen) {
            model_.set(browse->bind, path);
            widget_->invalidate();
        }
    }
    if (auto result = widget_->takeResult()) {
        handleResult(*result);
    }
}

void ManagerUI::handleResult(const Forms::Model::Result& result)
{
    if (result.ok && result.reload) {
        reloadRequested_ = true;
        close();
        return;
    }
    // After success an action can move to another screen, which then shows its message
    if (result.ok && result.back) {
        if (!requestBack()) {
            return;   // it says why
        }
    }
    else if (result.ok && !result.open.empty()) {
        const Navigator::Frame* frame = nav_.current();
        if (!push(result.open, Forms::openParams(frame ? frame->params : Forms::Params{}, result))) {
            return;
        }
    }
    if (open_) {
        setStatus(result.message, !result.ok);
    }
}

void ManagerUI::setStatus(const std::string& message, bool isError)
{
    status_ = message;
    statusIsError_ = isError;
    if (widget_) {
        widget_->setStatus(message, isError);
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
}

void ManagerUI::render()
{
    if (!context_ || !open_ || page_) {
        return;
    }
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    drawWindow();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

bool ManagerUI::takeReloadRequest()
{
    // Reloading destroys the Catalog, so it waits for installs to finish
    if (!reloadRequested_ || (catalog_ && catalog_->isBusy())) {
        return false;
    }
    reloadRequested_ = false;
    return true;
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

    if (shown) {
        view_.draw(*shown, model_);
    }
    else {
        ImGui::TextWrapped("This form couldn't be loaded. Check the log for details.");
    }

    std::optional<Forms::Model::Result> result = view_.takeResult();

    if (!status_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
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

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
#include "../Graphics/Page.h"
#include "../Graphics/PageBuilder.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
    const char* formId(ManagerUI::Screen screen)
    {
        switch (screen) {
        case ManagerUI::Screen::Settings: return "settings";
        case ManagerUI::Screen::Manage:   return "manage";
        case ManagerUI::Screen::Sources:  return "sources";
        case ManagerUI::Screen::Package:  return "package";
        }
        return "";
    }

    constexpr ManagerUI::Screen screens[] = {
        ManagerUI::Screen::Settings, ManagerUI::Screen::Manage, ManagerUI::Screen::Sources, ManagerUI::Screen::Package
    };
}

ManagerUI::ManagerUI(Configuration& config)
    : config_(config)
    , model_(config)
{
}

ManagerUI::~ManagerUI()
{
    deInitialize();
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

    loadForms();
    startCatalog();
    LOG_INFO("Manager", "Press " + std::string(SDL_GetScancodeName(toggleKey_)) + " to open Settings");
    return true;
}

void ManagerUI::loadForms()
{
    // A layout can provide its own versions in layouts/<layout>/forms/
    std::string layoutName;
    config_.getProperty(OPTION_LAYOUT, layoutName);
    for (Screen screen : screens) {
        forms_[screen] = Forms::load(formId(screen), layoutName);
    }
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
        if (name != "Main" && !Setup::addToMainMenu(name, error)) {
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

void ManagerUI::openPackage(const std::string& id)
{
    model_.selectPackage(id);
    open(Screen::Package);
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
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(context_);
    context_ = nullptr;
    window_ = nullptr;
    renderer_ = nullptr;
}

void ManagerUI::open(Screen screen)
{
    if (!context_) {
        return;
    }
    bool wasOpen = open_;
    screen_ = screen;
    status_.clear();
    focusNext_ = true;
    open_ = true;
    if (!wasOpen) {
        model_.refresh();
        cursorWasShown_ = SDL_ShowCursor(SDL_QUERY);
    }
    if (forms_[screen_]) {
        Forms::applyDefaults(*forms_[screen_], model_);
    }

    // Draw it as a layout page unless asked for ImGui, or the page can't be built
    std::string renderer;
    config_.getProperty("formsRenderer", renderer);
    destroyPage();
    if (Utils::toLower(renderer) == "imgui" || !buildPage()) {
        SDL_ShowCursor(SDL_ENABLE);
    }

    // Controls held while opening (e.g. the Select that chose it) must be released first
    navHeld_.fill(true);
}

void ManagerUI::close()
{
    if (!open_) {
        return;
    }
    open_ = false;
    destroyPage();
    SDL_ShowCursor(cursorWasShown_);
}

bool ManagerUI::buildPage()
{
    const Forms::Form* form = forms_[screen_].get();
    if (!form || !fontCache_) {
        return false;
    }

    // The layout styles a screen with forms/<id>/layout.xml, or all of them with forms/layout.xml
    std::string layoutName;
    config_.getProperty(OPTION_LAYOUT, layoutName);
    std::string formsPath = Utils::combinePath(Configuration::absolutePath, "layouts", layoutName, "forms");
    std::string screenFile = Utils::combinePath(formsPath, formId(screen_), "layout.xml");
    std::string sharedFile = Utils::combinePath(formsPath, "layout.xml");

    PageBuilder builder(layoutName, "layout", config_, fontCache_);
    if (!layoutName.empty() && std::filesystem::exists(screenFile)) {
        builder.setLayoutFile(screenFile);
    }
    else if (!layoutName.empty() && std::filesystem::exists(sharedFile)) {
        builder.setLayoutFile(sharedFile);
    }
    else {
        builder.setLayoutXml(Builtin::layoutXml("form"), formsPath);
    }

    Page* page = builder.buildPage();
    if (!page || builder.formWidgets().empty()) {
        LOG_ERROR("Manager", "The " + std::string(formId(screen_)) + " page has no <form>, using ImGui instead");
        if (page) {
            page->deInitialize();
            delete page;
        }
        return false;
    }

    widget_ = builder.formWidgets().front();
    widget_->bind(form, &model_);
    widget_->setCanBrowse(FileDialog::isSupported());
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
    std::string file = Utils::combinePath(formsPath, "keyboard", "layout.xml");

    PageBuilder builder(layoutName, "layout", config_, fontCache_);
    if (!layoutName.empty() && std::filesystem::exists(file)) {
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
        // Opens Settings, or closes it; from another screen it switches to Settings
        if (!e.key.repeat) {
            open_ && screen_ == Screen::Settings ? close() : open(Screen::Settings);
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
        // Escape always leaves, whatever the controls say
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !e.key.repeat) {
            if (!widget_->navigate(FormWidget::Nav::Back)) {
                close();
            }
            return true;
        }
        return false;
    }

    ImGui::SetCurrentContext(context_);

    // Escape closes the panel, unless it's being used to leave a text field
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !ImGui::GetIO().WantTextInput) {
        close();
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
            continue;
        }
        if (!widget_->navigate(navs[i])) {
            close();
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
        }
    }
    if (auto result = widget_->takeResult()) {
        widget_->setStatus(result->message, !result->ok);
        handleResult(*result);
    }
}

void ManagerUI::handleResult(const Forms::Model::Result& result)
{
    if (result.ok && result.reload) {
        reloadRequested_ = true;
        close();
    }
    else if (model_.takeOpenRequest() == "sources") {
        open(Screen::Sources);
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
    const Forms::Form* form = forms_[screen_].get();

    // Dim the page behind the window
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));

    float width = std::min(io.DisplaySize.x - 40.0f * scale_, 760.0f * scale_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, io.DisplaySize.y - 40.0f * scale_));

    // "###manager" keeps one window while the title changes with the form
    std::string title = (form ? Forms::title(*form, model_) : std::string(formId(screen_))) + "###manager";
    if (focusNext_) {
        ImGui::SetNextWindowFocus();
        focusNext_ = false;
    }
    bool keepOpen = true;
    ImGui::Begin(title.c_str(), &keepOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    if (form) {
        view_.draw(*form, model_);
    }
    else {
        ImGui::TextWrapped("This form couldn't be loaded. Check the log for details.");
    }

    std::optional<Forms::Model::Result> result = view_.takeResult();
    if (result) {
        status_ = result->message;
        statusIsError_ = !result->ok;
    }

    if (!status_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s: Settings    Esc: close", SDL_GetScancodeName(toggleKey_));
    float closeWidth = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - closeWidth);
    if (ImGui::Button("Close")) {
        keepOpen = false;
    }

    ImGui::End();

    if (!keepOpen) {
        close();
    }
    else if (result) {
        handleResult(*result);
    }
}

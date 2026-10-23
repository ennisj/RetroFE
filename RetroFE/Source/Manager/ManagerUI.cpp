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
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <algorithm>
#include <cmath>

namespace
{
    template <size_t N>
    void copyTo(char (&buffer)[N], const std::string& value)
    {
        SDL_strlcpy(buffer, value.c_str(), N);
    }
}

ManagerUI::ManagerUI(Configuration& config)
    : config_(config)
{
}

ManagerUI::~ManagerUI()
{
    deInitialize();
}

bool ManagerUI::initialize(SDL_Window* window, SDL_Renderer* renderer)
{
    if (context_) {
        return true;
    }
    if (!window || !renderer) {
        return false;
    }
    window_ = window;
    renderer_ = renderer;

    // controls.manager picks the key that opens the UI; the first keyboard key listed is used
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

    LOG_INFO("Manager", "Press " + std::string(SDL_GetScancodeName(toggleKey_)) + " to open the manager");
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
    screen_ = screen;
    status_.clear();
    if (open_) {
        return;
    }
    open_ = true;
    hasSettingsFile_ = Setup::hasSettingsFile();
    refreshCollections();

    // With settings.conf in place, show what it configures
    if (hasSettingsFile_) {
        std::string horizontal;
        std::string vertical;
        config_.getProperty("fullscreen", fullscreen_);
        if (config_.getProperty("horizontal", horizontal) && horizontal != "stretch") {
            width_ = Utils::convertInt(horizontal);
        }
        if (config_.getProperty("vertical", vertical) && vertical != "stretch") {
            height_ = Utils::convertInt(vertical);
        }
    }

    cursorWasShown_ = SDL_ShowCursor(SDL_QUERY);
    SDL_ShowCursor(SDL_ENABLE);
}

void ManagerUI::close()
{
    if (!open_) {
        return;
    }
    open_ = false;
    SDL_ShowCursor(cursorWasShown_);
}

bool ManagerUI::handleEvent(const SDL_Event& e)
{
    if (!context_) {
        return false;
    }
    if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == toggleKey_) {
        if (!e.key.repeat) {
            open_ ? close() : open(Screen::Settings);
        }
        return true;
    }
    if (!open_) {
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

void ManagerUI::update()
{
    if (browse_ == Browse::None) {
        return;
    }
    Browse browse = browse_;
    browse_ = Browse::None;

    if (browse == Browse::RomFolder) {
        std::string path = romPath_;
        if (FileDialog::pickFolder(window_, "Choose the folder your games are in", path)) {
            copyTo(romPath_, path);
        }
    }
    else if (browse == Browse::Executable) {
        std::string path = executable_;
        if (FileDialog::pickFile(window_, "Choose the emulator that runs these games", "Programs", "*.exe;*.bat;*.cmd", path)) {
            copyTo(executable_, path);
        }
    }
}

void ManagerUI::render()
{
    if (!context_ || !open_) {
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
    bool requested = reloadRequested_;
    reloadRequested_ = false;
    return requested;
}

void ManagerUI::drawWindow()
{
    ImGuiIO& io = ImGui::GetIO();

    // Dim the page behind the window
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));

    float width = std::min(io.DisplaySize.x - 40.0f * scale_, 760.0f * scale_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, io.DisplaySize.y - 40.0f * scale_));

    // "###manager" keeps one window while the title changes with the screen
    const char* title = screen_ == Screen::Settings ? "Settings###manager"
                      : screen_ == Screen::Manage   ? "Manage###manager"
                      : "Catalog###manager";
    bool keepOpen = true;
    ImGui::Begin(title, &keepOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    switch (screen_) {
    case Screen::Settings: drawSettings(); break;
    case Screen::Manage:   drawManage();   break;
    case Screen::Catalog:  drawCatalog();  break;
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
}

void ManagerUI::drawManage()
{
    if (collections_.empty()) {
        ImGui::TextWrapped("Welcome! Nothing is set up yet. Add your first collection below: "
                           "point RetroFE at a folder of games and the emulator that runs them, "
                           "and it will reload with the collection under Browse.");
    }
    else {
        // Real installs can have well over a hundred collections, so keep the list compact
        std::string heading = std::to_string(collections_.size()) + (collections_.size() == 1 ? " collection" : " collections");
        if (ImGui::TreeNode("collections", "%s", heading.c_str())) {
            float rows = static_cast<float>(std::min<size_t>(collections_.size(), 6));
            ImGui::BeginChild("##collectionList", ImVec2(0.0f, rows * ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_None);
            for (const std::string& name : collections_) {
                ImGui::TextUnformatted(name.c_str());
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
    }

    drawAddCollection();
}

void ManagerUI::drawCatalog()
{
    ImGui::TextWrapped("The catalog isn't built yet. It will list content you can download "
                       "from sources you configure, and install it into your collections.");
}

void ManagerUI::drawAddCollection()
{
    ImGui::SeparatorText("Add a collection");

    const float labelWidth = 150.0f * scale_;
    const char* browseLabel = "Browse...";
    const float browseWidth = ImGui::CalcTextSize(browseLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const bool canBrowse = FileDialog::isSupported();

    auto label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(labelWidth);
    };
    auto fieldWidth = [&](bool withBrowse) {
        ImGui::SetNextItemWidth(withBrowse && canBrowse ? -(browseWidth + ImGui::GetStyle().ItemSpacing.x) : -FLT_MIN);
    };
    auto hint = [&](const std::string& text) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelWidth);
        ImGui::TextDisabled("%s", text.c_str());
    };

    label("Name");
    fieldWidth(false);
    ImGui::InputTextWithHint("##name", "e.g. Arcade", name_, sizeof(name_));

    label("Games folder");
    fieldWidth(true);
    ImGui::InputText("##roms", romPath_, sizeof(romPath_));
    if (canBrowse) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...##roms")) {
            browse_ = Browse::RomFolder;
        }
    }

    // Recount only when the folder or extensions change
    if (countedPath_ != romPath_ || countedExtensions_ != extensions_) {
        countedPath_ = romPath_;
        countedExtensions_ = extensions_;
        romCount_ = Setup::countRoms(countedPath_, countedExtensions_);
    }
    if (romPath_[0] != '\0') {
        hint(romCount_ < 0 ? "Folder not found" : std::to_string(romCount_) + (romCount_ == 1 ? " matching file" : " matching files"));
    }

    label("File extensions");
    fieldWidth(false);
    ImGui::InputTextWithHint("##extensions", "e.g. zip,7z", extensions_, sizeof(extensions_));

    label("Emulator");
    fieldWidth(true);
    ImGui::InputText("##executable", executable_, sizeof(executable_));
    if (canBrowse) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...##executable")) {
            browse_ = Browse::Executable;
        }
    }

    label("Arguments");
    fieldWidth(false);
    ImGui::InputText("##arguments", arguments_, sizeof(arguments_));
    hint("%ITEM_FILEPATH% is replaced with the game's file");

    Setup::NewCollection collection{ name_, romPath_, extensions_, executable_, arguments_ };
    std::string problem = Setup::validate(collection);

    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelWidth);
    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button("Create collection")) {
        createCollection();
    }
    ImGui::EndDisabled();
    if (!problem.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", problem.c_str());
    }
}

void ManagerUI::drawSettings()
{
    ImGui::SeparatorText("Display");

    // Editing an existing settings.conf isn't supported yet, so only show it
    ImGui::BeginDisabled(hasSettingsFile_);
    ImGui::Checkbox("Fullscreen", &fullscreen_);
    if (!fullscreen_) {
        ImGui::SameLine(0.0f, 24.0f * scale_);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Window size");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * scale_);
        ImGui::InputInt("##width", &width_, 0);
        ImGui::SameLine();
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * scale_);
        ImGui::InputInt("##height", &height_, 0);
        width_ = std::clamp(width_, 320, 7680);
        height_ = std::clamp(height_, 240, 4320);
    }
    ImGui::EndDisabled();

    if (hasSettingsFile_) {
        ImGui::TextDisabled("From settings.conf. Changing it from here is coming next.");
        return;
    }

    ImGui::TextDisabled("Takes effect the next time RetroFE starts.");
    if (ImGui::Button("Save")) {
        std::string error;
        if (Setup::writeSettings(Setup::DisplaySettings{ fullscreen_, width_, height_ }, error)) {
            hasSettingsFile_ = true;
            setStatus("Saved to settings.conf. Restart RetroFE to apply it.", false);
        }
        else {
            setStatus(error, true);
        }
    }
}

void ManagerUI::refreshCollections()
{
    collections_ = Setup::listCollections();
}

void ManagerUI::createCollection()
{
    Setup::NewCollection collection{ name_, romPath_, extensions_, executable_, arguments_ };
    std::string error;

    if (!hasSettingsFile_) {
        Setup::DisplaySettings display{ fullscreen_, width_, height_ };
        if (!Setup::writeSettings(display, error)) {
            setStatus(error, true);
            return;
        }
        hasSettingsFile_ = true;
    }

    if (!Setup::createCollection(collection, error)) {
        setStatus(error, true);
        return;
    }

    LOG_NOTICE("Manager", "Created collection " + Utils::trimEnds(collection.name));
    copyTo(name_, "");
    reloadRequested_ = true;
    close();
}

void ManagerUI::setStatus(const std::string& message, bool isError)
{
    status_ = message;
    statusIsError_ = isError;
}

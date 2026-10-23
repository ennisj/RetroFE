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
#include "../Builtin/Builtin.h"
#include "../Database/Configuration.h"
#include "../Database/GlobalOpts.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <algorithm>
#include <cmath>

namespace
{
    const char* formId(ManagerUI::Screen screen)
    {
        switch (screen) {
        case ManagerUI::Screen::Settings: return "settings";
        case ManagerUI::Screen::Manage:   return "manage";
        case ManagerUI::Screen::Catalog:  return "catalog";
        }
        return "";
    }
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
    LOG_INFO("Manager", "Press " + std::string(SDL_GetScancodeName(toggleKey_)) + " to open Settings");
    return true;
}

void ManagerUI::loadForms()
{
    // A layout can provide its own versions in layouts/<layout>/forms/
    std::string layoutName;
    config_.getProperty(OPTION_LAYOUT, layoutName);
    for (Screen screen : { Screen::Settings, Screen::Manage, Screen::Catalog }) {
        forms_[screen] = Forms::load(formId(screen), layoutName);
    }
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
    focusNext_ = true;
    if (forms_[screen_]) {
        Forms::applyDefaults(*forms_[screen_], model_);
    }
    if (open_) {
        return;
    }
    open_ = true;
    model_.refresh();

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
    if (open_) {
        view_.update(window_, model_);
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
    const Forms::Form* form = forms_[screen_].get();

    // Dim the page behind the window
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));

    float width = std::min(io.DisplaySize.x - 40.0f * scale_, 760.0f * scale_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, io.DisplaySize.y - 40.0f * scale_));

    // "###manager" keeps one window while the title changes with the form
    std::string title = (form ? form->title : std::string(formId(screen_))) + "###manager";
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

    if (auto result = view_.takeResult()) {
        status_ = result->message;
        statusIsError_ = !result->ok;
        if (result->ok && result->reload) {
            reloadRequested_ = true;
            keepOpen = false;
        }
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

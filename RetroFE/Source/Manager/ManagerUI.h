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
#include <string>
#include <vector>

class Configuration;
struct ImGuiContext;

// In-app management UI, drawn with Dear ImGui on top of the current page on screen 0.
// While it's open it takes all input.
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
    enum class Browse { None, RomFolder, Executable };

    void drawWindow();
    void drawSettings();
    void drawManage();
    void drawCatalog();
    void drawAddCollection();
    void refreshCollections();
    void createCollection();
    void setStatus(const std::string& message, bool isError);

    Configuration& config_;
    ImGuiContext* context_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Scancode toggleKey_ = SDL_SCANCODE_F10;
    float scale_ = 1.0f;
    bool open_ = false;
    Screen screen_ = Screen::Settings;
    bool reloadRequested_ = false;
    int cursorWasShown_ = SDL_ENABLE;
    Browse browse_ = Browse::None;

    // Add collection form
    char name_[128] = "";
    char romPath_[1024] = "";
    char extensions_[128] = "zip";
    char executable_[1024] = "";
    char arguments_[1024] = "\"%ITEM_FILEPATH%\"";
    std::string countedPath_;
    std::string countedExtensions_;
    int romCount_ = -1;

    // Display; only editable while settings.conf doesn't exist, otherwise shows what's configured
    bool fullscreen_ = false;
    int width_ = 1280;
    int height_ = 720;

    bool hasSettingsFile_ = false;
    std::vector<std::string> collections_;
    std::string status_;
    bool statusIsError_ = false;
};

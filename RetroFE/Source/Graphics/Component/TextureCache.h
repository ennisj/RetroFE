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
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

// Pictures for the forms (results' thumbnails, <image> fields), loaded from
// files and scaled down to the size they're drawn at. Only a few new ones
// load per frame so paging through a list doesn't stall, the least recently
// drawn are dropped past a pixel budget, and files that can't be loaded are
// tried again a few seconds later, so artwork added meanwhile shows up.
class TextureCache
{
public:
    enum class State { Ready, Loading, Missing };

    explicit TextureCache(size_t pixelBudget = 8 * 1024 * 1024);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // Where textures are made; changing it drops everything
    void setRenderer(SDL_Renderer* renderer);

    // Call once per frame: how many pictures not loaded yet may load this frame
    void beginFrame(int loads);

    // The picture at path, no bigger than maxWidth x maxHeight pixels (0: any
    // size). Unless urgent it waits for the frame's allowance, and is null
    // with state Loading until then. Null with Missing when it can't be loaded.
    SDL_Texture* get(const std::string& path, int maxWidth, int maxHeight, bool urgent, State* state = nullptr);

    void clear();

private:
    struct Entry
    {
        SDL_Texture* texture = nullptr;
        size_t pixels = 0;
        uint64_t used = 0;       // the frame it was last asked for
        uint32_t failedAt = 0;   // SDL ticks when loading it last failed
    };

    SDL_Texture* load(const std::string& path, int maxWidth, int maxHeight, size_t& pixels);
    void trim();

    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<std::string, Entry> entries_;
    size_t budget_;
    size_t pixels_ = 0;
    uint64_t frame_ = 0;
    int loadsLeft_ = 0;
};

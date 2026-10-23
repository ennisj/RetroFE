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
#include "TextureCache.h"
#include "../../SDL.h"
#include "../../Utility/Log.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

#if __has_include(<SDL2/SDL_image.h>)
#include <SDL2/SDL_image.h>
#elif __has_include(<SDL2_image/SDL_image.h>)
#include <SDL2_image/SDL_image.h>
#else
#include <SDL_image.h>
#endif

namespace
{
    // How long a picture that couldn't be loaded waits before another try
    constexpr uint32_t retryMs = 3000;

    // Entries kept at most, failures included
    constexpr size_t maxEntries = 512;

    // A copy of surface at width x height
    SDL_Surface* resized(SDL_Surface* surface, int width, int height)
    {
        SDL_Surface* target = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!target) {
            return nullptr;
        }
#if SDL_VERSION_ATLEAST(2, 0, 16)
        if (SDL_SoftStretchLinear(surface, nullptr, target, nullptr) == 0) {
            return target;
        }
#endif
        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
        if (SDL_BlitScaled(surface, nullptr, target, nullptr) != 0) {
            SDL_FreeSurface(target);
            return nullptr;
        }
        return target;
    }

    // Scales down to fit, halving first so big reductions don't alias
    SDL_Surface* fitted(SDL_Surface* image, int maxWidth, int maxHeight)
    {
        float scale = 1.0f;
        if (maxWidth > 0) scale = std::min(scale, static_cast<float>(maxWidth) / static_cast<float>(image->w));
        if (maxHeight > 0) scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(image->h));
        if (scale >= 1.0f) {
            return nullptr;   // already small enough
        }
        int width = std::max(1, static_cast<int>(std::lround(image->w * scale)));
        int height = std::max(1, static_cast<int>(std::lround(image->h * scale)));

        SDL_Surface* current = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ARGB8888, 0);
        while (current && current->w / 2 >= width * 2 && current->h / 2 >= height * 2) {
            SDL_Surface* half = resized(current, current->w / 2, current->h / 2);
            SDL_FreeSurface(current);
            current = half;
        }
        if (!current) {
            return nullptr;
        }
        SDL_Surface* result = resized(current, width, height);
        SDL_FreeSurface(current);
        return result;
    }
}

TextureCache::TextureCache(size_t pixelBudget)
    : budget_(pixelBudget)
{
}

TextureCache::~TextureCache()
{
    clear();
}

void TextureCache::setRenderer(SDL_Renderer* renderer)
{
    if (renderer != renderer_) {
        clear();
        renderer_ = renderer;
    }
}

void TextureCache::beginFrame(int loads)
{
    frame_++;
    loadsLeft_ = loads;
}

SDL_Texture* TextureCache::get(const std::string& path, int maxWidth, int maxHeight, bool urgent, State* state)
{
    auto report = [&](State s, SDL_Texture* texture) {
        if (state) *state = s;
        return texture;
    };
    if (path.empty() || !renderer_) {
        return report(State::Missing, nullptr);
    }

    std::string key = path + '\n' + std::to_string(maxWidth) + 'x' + std::to_string(maxHeight);
    auto found = entries_.find(key);
    if (found != entries_.end()) {
        Entry& entry = found->second;
        entry.used = frame_;
        if (entry.texture) {
            return report(State::Ready, entry.texture);
        }
        if (SDL_GetTicks() - entry.failedAt < retryMs) {
            return report(State::Missing, nullptr);
        }
    }
    if (!urgent && loadsLeft_ <= 0) {
        return report(State::Loading, nullptr);
    }
    if (!urgent) {
        loadsLeft_--;
    }

    bool firstTry = found == entries_.end();
    Entry& entry = entries_[key];
    entry.used = frame_;
    entry.texture = load(path, maxWidth, maxHeight, entry.pixels);
    if (!entry.texture) {
        entry.failedAt = SDL_GetTicks();
        if (firstTry && std::filesystem::exists(path)) {
            LOG_WARNING("Forms", "Can't load " + path + ": " + IMG_GetError());
        }
        trim();
        return report(State::Missing, nullptr);
    }
    pixels_ += entry.pixels;
    SDL_Texture* texture = entry.texture;
    trim();
    return report(State::Ready, texture);
}

SDL_Texture* TextureCache::load(const std::string& path, int maxWidth, int maxHeight, size_t& pixels)
{
    pixels = 0;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return nullptr;
    }
    SDL_Surface* image = IMG_Load(path.c_str());
    if (!image) {
        return nullptr;
    }
    SDL_Surface* small = fitted(image, maxWidth, maxHeight);
    SDL_Surface* source = small ? small : image;

    SDL_LockMutex(SDL::getMutex());
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, source);
    SDL_UnlockMutex(SDL::getMutex());
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        pixels = static_cast<size_t>(source->w) * static_cast<size_t>(source->h);
    }
    if (small) {
        SDL_FreeSurface(small);
    }
    SDL_FreeSurface(image);
    return texture;
}

// Drops the least recently drawn pictures past the budget, never one drawn this frame
void TextureCache::trim()
{
    while (pixels_ > budget_ || entries_.size() > maxEntries) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.used < frame_ && (oldest == entries_.end() || it->second.used < oldest->second.used)) {
                oldest = it;
            }
        }
        if (oldest == entries_.end()) {
            return;
        }
        if (oldest->second.texture) {
            SDL_LockMutex(SDL::getMutex());
            SDL_DestroyTexture(oldest->second.texture);
            SDL_UnlockMutex(SDL::getMutex());
            pixels_ -= oldest->second.pixels;
        }
        entries_.erase(oldest);
    }
}

void TextureCache::clear()
{
    if (entries_.empty()) {
        return;   // e.g. at exit, when SDL may be gone
    }
    SDL_LockMutex(SDL::getMutex());
    for (auto& [key, entry] : entries_) {
        if (entry.texture) {
            SDL_DestroyTexture(entry.texture);
        }
    }
    SDL_UnlockMutex(SDL::getMutex());
    entries_.clear();
    pixels_ = 0;
}

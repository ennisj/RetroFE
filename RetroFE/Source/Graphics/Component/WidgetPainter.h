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
#include <memory>
#include <string>
#include <vector>

class FontManager;
class Page;
class Text;
class ViewInfo;

// Drawing helpers for components that lay out their own text and boxes, such
// as <form> and <keyboard>. Positions are in layout coordinates; everything
// goes through SDL::renderCopyF so scaling, rotation and mirroring match
// other components. Alpha, monitor, layout and clipping come from the
// owning component's ViewInfo.
class WidgetPainter
{
public:
    WidgetPainter(Page& page, FontManager* font, FontManager* focusFont);
    ~WidgetPainter();

    void allocateGraphicsMemory(int monitor);
    void freeGraphicsMemory();

    // Call at the start of every draw
    void begin(const ViewInfo& owner);

    void text(const std::string& text, float x, float y, float maxWidth, float fontSize, bool focused, float alpha);

    // The same in another font of the same face, e.g. another color; null means the normal font
    void textWith(FontManager* font, const std::string& text, float x, float y, float maxWidth, float fontSize, float alpha);

    void rect(float x, float y, float w, float h, SDL_Color color, float alpha);

    // Draws a texture the caller owns, stretched to the box
    void texture(SDL_Texture* texture, float x, float y, float w, float h, float alpha);

    float textWidth(const std::string& text, float fontSize) const;

    // Splits text into lines no wider than width, at spaces
    std::vector<std::string> wrap(const std::string& text, float width, float fontSize) const;

    // The end of text that fits in width, after "...", or text itself if it fits
    std::string tail(const std::string& text, float width, float fontSize) const;

    bool hasFont() const { return font_ != nullptr; }

private:
    Page& page_;
    FontManager* font_;
    FontManager* focusFont_;
    const ViewInfo* owner_ = nullptr;
    SDL_Texture* solid_ = nullptr;
    std::vector<std::unique_ptr<Text>> texts_;
    size_t textsUsed_ = 0;
};

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
#include "WidgetPainter.h"
#include "Text.h"
#include "../Font.h"
#include "../Page.h"
#include "../ViewInfo.h"
#include "../../SDL.h"
#include "../../Forms/Form.h"

WidgetPainter::WidgetPainter(Page& page, FontManager* font, FontManager* focusFont)
    : page_(page)
    , font_(font)
    , focusFont_(focusFont ? focusFont : font)
{
}

WidgetPainter::~WidgetPainter()
{
    freeGraphicsMemory();
}

void WidgetPainter::allocateGraphicsMemory(int monitor)
{
    if (solid_) {
        return;
    }
    // A white texture stretched and tinted to draw boxes
    SDL_Surface* surface = SDL_CreateRGBSurface(0, 4, 4, 32, 0, 0, 0, 0);
    SDL_FillRect(surface, nullptr, SDL_MapRGB(surface->format, 255, 255, 255));
    solid_ = SDL_CreateTextureFromSurface(SDL::getRenderer(monitor), surface);
    SDL_FreeSurface(surface);
    SDL_SetTextureBlendMode(solid_, SDL_BLENDMODE_BLEND);
}

void WidgetPainter::freeGraphicsMemory()
{
    if (solid_) {
        SDL_LockMutex(SDL::getMutex());
        SDL_DestroyTexture(solid_);
        SDL_UnlockMutex(SDL::getMutex());
        solid_ = nullptr;
    }
}

void WidgetPainter::begin(const ViewInfo& owner)
{
    owner_ = &owner;
    textsUsed_ = 0;
}

void WidgetPainter::text(const std::string& text, float x, float y, float maxWidth, float fontSize, bool focused, float alpha)
{
    textWith(focused ? focusFont_ : font_, text, x, y, maxWidth, fontSize, alpha);
}

void WidgetPainter::textWith(FontManager* font, const std::string& text, float x, float y, float maxWidth, float fontSize, float alpha)
{
    if (text.empty() || !owner_ || !font_) {
        return;
    }
    font = font ? font : font_;
    // Too long: cut at a whole character with "..." rather than wherever Text stops
    std::string shown = fit(font, text, maxWidth, fontSize);
    if (shown.empty()) {
        return;
    }
    if (textsUsed_ >= texts_.size()) {
        texts_.push_back(std::make_unique<Text>("", page_, font_, owner_->Monitor));
    }
    Text* component = texts_[textsUsed_++].get();
    component->setText(shown);

    ViewInfo& view = component->baseViewInfo;
    view.font = font;
    view.X = x;
    view.Y = y;
    view.FontSize = fontSize;
    // Text adds up glyph widths in another order than textWidth, so text measured
    // to fit exactly can come out a hair wider and lose its last letter
    view.MaxWidth = maxWidth + fitSlack;
    view.Alpha = alpha * owner_->Alpha;
    view.Monitor = owner_->Monitor;
    view.Layout = owner_->Layout;
    view.ContainerX = owner_->ContainerX;
    view.ContainerY = owner_->ContainerY;
    view.ContainerWidth = owner_->ContainerWidth;
    view.ContainerHeight = owner_->ContainerHeight;
    component->draw();
}

void WidgetPainter::rect(float x, float y, float w, float h, SDL_Color color, float alpha)
{
    if (!solid_ || !owner_) {
        return;
    }
    SDL_SetTextureColorMod(solid_, color.r, color.g, color.b);
    SDL_FRect rect{ x, y, w, h };
    ViewInfo view = *owner_;
    SDL::renderCopyF(solid_, alpha * owner_->Alpha, nullptr, &rect, view,
                     page_.getLayoutWidthByMonitor(owner_->Monitor), page_.getLayoutHeightByMonitor(owner_->Monitor));
}

void WidgetPainter::texture(SDL_Texture* texture, float x, float y, float w, float h, float alpha, const SDL_Rect* source)
{
    if (!texture || !owner_) {
        return;
    }
    SDL_FRect rect{ x, y, w, h };
    ViewInfo view = *owner_;
    SDL::renderCopyF(texture, alpha * owner_->Alpha, source, &rect, view,
                     page_.getLayoutWidthByMonitor(owner_->Monitor), page_.getLayoutHeightByMonitor(owner_->Monitor));
}

float WidgetPainter::textWidth(const std::string& text, float fontSize) const
{
    return textWidthWith(font_, text, fontSize);
}

float WidgetPainter::textWidthWith(FontManager* font, const std::string& text, float fontSize) const
{
    font = font ? font : font_;
    if (!font || font->getHeight() <= 0) {
        return 0.0f;
    }
    return static_cast<float>(font->getWidth(text)) * fontSize / static_cast<float>(font->getHeight());
}

std::string WidgetPainter::fit(FontManager* font, const std::string& text, float width, float fontSize) const
{
    return Forms::ellipsize(text, width, [&](const std::string& s) { return textWidthWith(font, s, fontSize); });
}

std::vector<std::string> WidgetPainter::wrap(const std::string& text, float width, float fontSize) const
{
    return Forms::wrapText(text, width, [&](const std::string& s) { return textWidth(s, fontSize); });
}

std::string WidgetPainter::tail(const std::string& text, float width, float fontSize) const
{
    if (textWidth(text, fontSize) <= width) {
        return text;
    }
    size_t start = 0;
    while (start < text.size() && textWidth("..." + text.substr(start), fontSize) > width) {
        start++;
        while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
            start++;   // whole characters
        }
    }
    return "..." + text.substr(start);
}

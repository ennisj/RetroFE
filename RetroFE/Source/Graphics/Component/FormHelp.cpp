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
#include "FormHelp.h"
#include "FormWidget.h"
#include "../Page.h"
#include "../../SDL.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    // New pictures loaded per frame (one is shown at a time)
    constexpr int picturesPerFrame = 1;
    constexpr size_t maxWrapped = 256;
    constexpr float lineSpacing = 1.25f;
}

FormHelp::FormHelp(Page& p, FontManager* font, int monitor)
    : Component(p)
    , painter_(p, font, font)
    , pictures_(4 * 1024 * 1024)
{
    baseViewInfo.Monitor = monitor;
    baseViewInfo.Layout = page.getCurrentLayout();
}

FormHelp::~FormHelp()
{
    FormHelp::freeGraphicsMemory();
}

void FormHelp::setFont(FontRole role, FontManager* font)
{
    switch (role) {
    case FontRole::Heading: headingFont_ = font; break;
    case FontRole::Detail:  detailFont_ = font; break;
    case FontRole::Warning: warningFont_ = font; break;
    case FontRole::Error:   errorFont_ = font; break;
    case FontRole::Success: successFont_ = font; break;
    }
}

void FormHelp::allocateGraphicsMemory()
{
    Component::allocateGraphicsMemory();
    painter_.allocateGraphicsMemory(baseViewInfo.Monitor);
    pictures_.setRenderer(SDL::getRenderer(baseViewInfo.Monitor));
}

void FormHelp::freeGraphicsMemory()
{
    Component::freeGraphicsMemory();
    painter_.freeGraphicsMemory();
    pictures_.clear();
}

float FormHelp::fontSize() const
{
    return baseViewInfo.FontSize > 0 ? baseViewInfo.FontSize : 28.0f;
}

float FormHelp::pixelScale() const
{
    int width = 0;
    int height = 0;
    SDL_Renderer* renderer = SDL::getRenderer(baseViewInfo.Monitor);
    int layoutWidth = page.getLayoutWidthByMonitor(baseViewInfo.Monitor);
    int layoutHeight = page.getLayoutHeightByMonitor(baseViewInfo.Monitor);
    if (!renderer || SDL_GetRendererOutputSize(renderer, &width, &height) != 0 || layoutWidth <= 0 || layoutHeight <= 0) {
        return 1.0f;
    }
    return std::max(1.0f, std::max(static_cast<float>(width) / layoutWidth, static_cast<float>(height) / layoutHeight));
}

std::string FormHelp::defaultImage() const
{
    std::string id = form_ ? form_->formId() : "";
    for (const DefaultImage& image : style_.defaultImages) {
        if (Forms::matchesFormId(image.forms, id)) {
            return image.path;
        }
    }
    return "";
}

const std::vector<std::string>& FormHelp::wrap(FontManager* font, const std::string& text, float width, float size)
{
    std::string key = std::to_string(reinterpret_cast<uintptr_t>(font)) + '/' + std::to_string(std::lround(width)) + '/'
                    + std::to_string(std::lround(size * 10.0f)) + '\x1f' + text;
    auto found = wrapped_.find(key);
    if (found != wrapped_.end()) {
        return found->second;
    }
    if (wrapped_.size() >= maxWrapped) {
        wrapped_.clear();
    }
    auto measure = [&](const std::string& s) { return painter_.textWidthWith(font, s, size); };
    return wrapped_.emplace(key, Forms::wrapText(text, width, measure)).first->second;
}

float FormHelp::drawLines(FontManager* font, const std::vector<std::string>& lines, float x, float y, float width, float size,
                          float lineHeight, float bottom, float alpha)
{
    for (size_t i = 0; i < lines.size(); i++) {
        if (y + size > bottom) {
            break;
        }
        bool more = i + 1 < lines.size() && y + lineHeight + size > bottom;
        // The last line that fits says there's more
        std::string line = more ? painter_.fit(font, lines[i] + " ...", width, size) : lines[i];
        if (more && line.size() < 4) {
            line = lines[i];
        }
        painter_.textWith(font, line, x, y, width, size, alpha);
        y += lineHeight;
        if (more) {
            break;
        }
    }
    return y;
}

void FormHelp::draw()
{
    Component::draw();
    if (!form_ || !painter_.hasFont() || baseViewInfo.Alpha <= 0.0f) {
        return;
    }
    const FormWidget::Help& help = form_->help();
    pictures_.beginFrame(picturesPerFrame);
    painter_.begin(baseViewInfo);

    const float size = fontSize();
    const float padding = size * 0.6f;
    const float left = baseViewInfo.XRelativeToOrigin() + padding;
    const float top = baseViewInfo.YRelativeToOrigin() + padding;
    const float width = (baseViewInfo.Width > 0 ? baseViewInfo.Width : 400.0f) - padding * 2.0f;
    const float bottom = baseViewInfo.YRelativeToOrigin() + (baseViewInfo.Height > 0 ? baseViewInfo.Height : 600.0f) - padding;
    const float height = bottom - top;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    // The picture: above the text, or on its left when the panel says how wide
    float textLeft = left;
    float textWidth = width;
    float textTop = top;
    // The row's picture, else the screen's default (the layout's, e.g. an icon)
    bool fallback = help.picture.empty();
    std::string path = fallback ? defaultImage() : help.picture;
    if (!path.empty()) {
        bool beside = style_.pictureWidth > 0.0f;
        float boxWidth = beside ? std::min(style_.pictureWidth, width) : width;
        float boxHeight = beside ? height : (style_.pictureHeight > 0.0f ? std::min(style_.pictureHeight, height) : height * 0.45f);
        float scale = pixelScale();
        TextureCache::State state = TextureCache::State::Missing;
        SDL_Texture* picture = pictures_.get(path, static_cast<int>(boxWidth * scale), static_cast<int>(boxHeight * scale), true, &state);
        int w = 0;
        int h = 0;
        if (picture && SDL_QueryTexture(picture, nullptr, nullptr, &w, &h) == 0 && w > 0 && h > 0) {
            if (style_.imageBackgroundAlpha > 0.0f && !fallback) {
                painter_.rect(left, top, boxWidth, boxHeight, style_.imageBackground, style_.imageBackgroundAlpha);
            }
            float fit = std::min(boxWidth / static_cast<float>(w), boxHeight / static_cast<float>(h));
            if (fallback) {
                fit = std::min(fit, 1.0f / scale);   // an icon: never larger than its file
            }
            float drawnWidth = w * fit;
            float drawnHeight = h * fit;
            painter_.texture(picture, left + (boxWidth - drawnWidth) / 2.0f, top + (boxHeight - drawnHeight) / 2.0f,
                             drawnWidth, drawnHeight, 1.0f);
            if (beside) {
                textLeft = left + boxWidth + padding;
                textWidth = width - boxWidth - padding;
            }
            else {
                textTop = top + boxHeight + padding;
            }
        }
    }
    if (textWidth <= 0.0f) {
        return;
    }

    // The row's name, what it says about itself, then its help
    float y = textTop;
    if (!help.heading.empty()) {
        float headingSize = size * style_.headingScale;
        y = drawLines(headingFont_, wrap(headingFont_, help.heading, textWidth, headingSize), textLeft, y, textWidth, headingSize,
                      headingSize * lineSpacing, bottom, 1.0f);
        y += size * 0.2f;
    }
    if (!help.detail.empty()) {
        FontManager* font = help.severity == Forms::Severity::Ok      ? (successFont_ ? successFont_ : detailFont_)
                          : help.severity == Forms::Severity::Warning ? (warningFont_ ? warningFont_ : errorFont_ ? errorFont_ : detailFont_)
                          : help.severity == Forms::Severity::Error   ? (errorFont_ ? errorFont_ : detailFont_)
                                                                      : detailFont_;
        y = drawLines(font, wrap(font, help.detail, textWidth, size), textLeft, y, textWidth, size, size * lineSpacing, bottom,
                      help.severity == Forms::Severity::None ? style_.detailAlpha : 1.0f);
        y += size * 0.35f;
    }
    if (!help.text.empty()) {
        drawLines(nullptr, wrap(nullptr, help.text, textWidth, size), textLeft, y, textWidth, size, size * lineSpacing, bottom, style_.noteAlpha);
    }
}

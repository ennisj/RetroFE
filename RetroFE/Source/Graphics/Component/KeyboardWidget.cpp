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
#include "KeyboardWidget.h"
#include "Image.h"
#include <algorithm>
#include <cstddef>

KeyboardWidget::KeyboardWidget(Page& p, FontManager* font, FontManager* focusFont, int monitor)
    : Component(p)
    , painter_(p, font, focusFont)
{
    baseViewInfo.Monitor = monitor;
    baseViewInfo.Layout = page.getCurrentLayout();
}

KeyboardWidget::~KeyboardWidget()
{
    KeyboardWidget::freeGraphicsMemory();
}

void KeyboardWidget::attach(Forms::TextEdit* text, const std::string& title, bool numeric, bool multiline)
{
    text_ = text;
    title_ = title;
    keys_.reset(numeric, multiline);
}

void KeyboardWidget::detach()
{
    text_ = nullptr;
}

KeyboardWidget::Result KeyboardWidget::navigate(FormWidget::Nav nav)
{
    if (!text_) {
        return Result::Cancel;
    }
    using KNav = Forms::KeyboardKeys::Nav;
    KNav k = KNav::Back;
    switch (nav) {
    case FormWidget::Nav::Up: k = KNav::Up; break;
    case FormWidget::Nav::Down: k = KNav::Down; break;
    case FormWidget::Nav::Left: k = KNav::Left; break;
    case FormWidget::Nav::Right: k = KNav::Right; break;
    case FormWidget::Nav::Select: k = KNav::Select; break;
    case FormWidget::Nav::Back: k = KNav::Back; break;
    }
    return keys_.navigate(k, *text_);
}

void KeyboardWidget::allocateGraphicsMemory()
{
    Component::allocateGraphicsMemory();
    painter_.allocateGraphicsMemory(baseViewInfo.Monitor);
    if (!style_.keyImage.empty() && !keyImage_) {
        keyImage_ = std::make_unique<Image>(style_.keyImage, "", page, baseViewInfo.Monitor, false);
        keyImage_->allocateGraphicsMemory();
    }
    if (!style_.focusKeyImage.empty() && !focusKeyImage_) {
        focusKeyImage_ = std::make_unique<Image>(style_.focusKeyImage, "", page, baseViewInfo.Monitor, false);
        focusKeyImage_->allocateGraphicsMemory();
    }
}

void KeyboardWidget::freeGraphicsMemory()
{
    Component::freeGraphicsMemory();
    painter_.freeGraphicsMemory();
    for (auto* image : { &keyImage_, &focusKeyImage_ }) {
        if (*image) {
            (*image)->freeGraphicsMemory();
            image->reset();
        }
    }
}

float KeyboardWidget::fontSize() const
{
    return baseViewInfo.FontSize > 0 ? baseViewInfo.FontSize : 26.0f;
}

void KeyboardWidget::draw()
{
    Component::draw();
    if (!text_ || !painter_.hasFont() || baseViewInfo.Alpha <= 0.0f) {
        return;
    }
    painter_.begin(baseViewInfo);
    keys_.clampFocus();

    const float size = fontSize();
    const float padding = size * 0.6f;
    const float layoutWidth = static_cast<float>(page.getLayoutWidthByMonitor(baseViewInfo.Monitor));
    const float layoutHeight = static_cast<float>(page.getLayoutHeightByMonitor(baseViewInfo.Monitor));
    const float left = baseViewInfo.XRelativeToOrigin() + padding;
    const float top = baseViewInfo.YRelativeToOrigin() + padding;
    const float width = (baseViewInfo.Width > 0 ? baseViewInfo.Width : layoutWidth - baseViewInfo.X) - padding * 2.0f;
    const float height = (baseViewInfo.Height > 0 ? baseViewInfo.Height : layoutHeight - baseViewInfo.Y) - padding * 2.0f;

    // The field's name, then what's been typed: the line the cursor is on, up
    // to the cursor ("_") and as much after it as fits
    const std::string& text = text_->text;
    size_t cursor = std::min(text_->cursor, text.size());
    size_t lineStart = 0;
    if (cursor > 0) {
        size_t newline = text.rfind('\n', cursor - 1);
        lineStart = newline == std::string::npos ? 0 : newline + 1;
    }
    size_t lineEnd = text.find('\n', cursor);
    lineEnd = lineEnd == std::string::npos ? text.size() : lineEnd;
    std::string title = title_;
    if (keys_.multiline()) {
        size_t line = static_cast<size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(cursor), '\n')) + 1;
        size_t lines = static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
        title += "  (line " + std::to_string(line) + " of " + std::to_string(lines) + ")";
    }
    float y = top;
    painter_.text(title, left, y, width, size * 0.85f, false, style_.noteAlpha);
    y += size * 1.2f;
    float boxHeight = size * 1.6f;
    painter_.rect(left, y, width, boxHeight, style_.keyColor, style_.keyAlpha);
    std::string shown = painter_.tail(text.substr(lineStart, cursor - lineStart) + "_", width - padding, size);
    float rest = width - padding - painter_.textWidth(shown, size);
    if (lineEnd > cursor && rest > 0.0f) {
        shown += painter_.fit(nullptr, text.substr(cursor, lineEnd - cursor), rest, size);
    }
    painter_.text(shown, left + padding / 2.0f, y + (boxHeight - size) / 2.0f, width - padding, size, true, 1.0f);
    y += boxHeight + padding;

    // The keys fill what's left above the hints
    const float hintsHeight = style_.showHints ? size * 1.4f : 0.0f;
    const auto& rows = keys_.rows();
    const float spacing = style_.keySpacing > 0 ? style_.keySpacing : size * 0.25f;
    const float gridHeight = top + height - y - hintsHeight;
    const float keyHeight = (gridHeight - spacing * (rows.size() - 1)) / rows.size();

    auto drawBox = [&](float x, float boxY, float w, float h, bool focused, bool active) {
        Image* image = focused && focusKeyImage_ ? focusKeyImage_.get() : (!focused && keyImage_ ? keyImage_.get() : nullptr);
        if (image) {
            ViewInfo& view = image->baseViewInfo;
            view.X = x;
            view.Y = boxY;
            view.Width = w;
            view.Height = h;
            view.Alpha = baseViewInfo.Alpha;
            view.Monitor = baseViewInfo.Monitor;
            view.Layout = baseViewInfo.Layout;
            image->draw();
        }
        else if (focused) {
            painter_.rect(x, boxY, w, h, style_.focusKeyColor, style_.focusKeyAlpha);
        }
        else {
            painter_.rect(x, boxY, w, h, style_.keyColor, active ? style_.keyAlpha * 3.0f : style_.keyAlpha);
        }
    };

    for (size_t r = 0; r < rows.size(); r++) {
        int units = 0;
        for (const Key& key : rows[r]) {
            units += key.units;
        }
        const float unitWidth = (width - spacing * (units - 1)) / units;
        float x = left;
        float keyY = y + r * (keyHeight + spacing);
        for (size_t c = 0; c < rows[r].size(); c++) {
            const Key& key = rows[r][c];
            float keyWidth = key.units * unitWidth + (key.units - 1) * spacing;
            bool focused = static_cast<int>(r) == keys_.row() && static_cast<int>(c) == keys_.column();
            drawBox(x, keyY, keyWidth, keyHeight, focused, key.action == Action::Shift && keys_.shift());

            float labelWidth = painter_.textWidth(key.label, size);
            painter_.text(key.label, x + (keyWidth - labelWidth) / 2.0f, keyY + (keyHeight - size) / 2.0f, keyWidth, size, focused, 1.0f);
            x += keyWidth + spacing;
        }
    }

    if (style_.showHints) {
        std::string hint = "Select: type   Back: cancel";
        if (keys_.multiline()) {
            hint = "Select: type   Done: keep   Back: cancel";
        }
        if (!keys_.numeric()) {
            hint += "   Shift: capitals and symbols";
        }
        painter_.text(hint, left, top + height - hintsHeight + (hintsHeight - size * 0.85f) / 2.0f, width, size * 0.85f, false, style_.noteAlpha);
    }
}

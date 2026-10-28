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

#include "Component.h"
#include "FormWidget.h"
#include "WidgetPainter.h"
#include "../../Forms/KeyboardKeys.h"
#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <vector>

class FontManager;
class Image;

// The <keyboard> layout element: an on-screen keyboard for typing with a
// gamepad or arcade controls. It types into text it's attached to, here the
// form field being edited. Its look comes from the layout:
//
//   <keyboard x="center" y="690" xOrigin="center" yOrigin="bottom" width="900" height="380"
//             fontSize="26" fontColor="c8c8c8" focusColor="ffffff"
//             keyColor="ffffff" keyAlpha="0.07" focusKeyColor="3d6fb6" focusKeyAlpha="0.7"
//             keySpacing="6" keyImage="images/key.png" focusKeyImage="images/key_focus.png"
//             backgroundColor="0d1014" backgroundAlpha="1"/>
//
// Arrows move between keys, Select presses one, Back cancels. For a
// multi-line field it has a New line key and a row of cursor keys. The keys
// and how the controls move over them are Forms::KeyboardKeys, shared with
// the theme editor's keyboard.
class KeyboardWidget : public Component
{
public:
    void setLetterControls(bool bound) { letterControls_ = bound; }

    // LineUp / LineDown: the owner moves the cursor a line as the field draws it
    using Result = Forms::KeyboardKeys::Result;
    using Action = Forms::KeyboardKeys::Action;
    using Key = Forms::KeyboardKeys::Key;

    struct Style
    {
        SDL_Color keyColor{ 255, 255, 255, 255 };
        float keyAlpha = 0.07f;
        SDL_Color focusKeyColor{ 61, 111, 182, 255 };
        float focusKeyAlpha = 0.7f;
        float keySpacing = 0.0f;       // 0: proportional to the key size
        std::string keyImage;          // absolute paths; drawn instead of the boxes
        std::string focusKeyImage;
        float noteAlpha = 0.55f;
        bool showHints = true;
    };

    KeyboardWidget(Page& p, FontManager* font, FontManager* focusFont, int monitor);
    ~KeyboardWidget() override;

    void setStyle(const Style& style) { style_ = style; }

    // Types into text, at its cursor, until detached. numeric shows a number
    // pad; multiline adds a New line key and cursor keys.
    void attach(Forms::TextEdit* text, const std::string& title, bool numeric, bool multiline = false);
    void detach();

    Result navigate(FormWidget::Nav nav);

    void allocateGraphicsMemory() override;
    void freeGraphicsMemory() override;
    void draw() override;

private:
    bool letterControls_ = false;

    float fontSize() const;

    WidgetPainter painter_;
    Style style_;
    Forms::TextEdit* text_ = nullptr;
    std::string title_;
    Forms::KeyboardKeys keys_;

    std::unique_ptr<Image> keyImage_;
    std::unique_ptr<Image> focusKeyImage_;
};

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
#include "TextureCache.h"
#include "WidgetPainter.h"
#include "../../Forms/Form.h"
#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>

class FontManager;
class FormWidget;

// The <formHelp> layout element: a side panel beside a page's <form> that
// shows the focused row's picture, name, status and help (FormWidget::help()).
// With it, the form keeps its rows compact: fields' help, "Set in ..." and
// the form's own <image> move here. A result's picture (a game's art) and a
// field's preview binding show while they're focused, else the form's image.
//
//   <formHelp x="1290" y="90" width="580" height="930" fontSize="32" fontColor="dedede"
//             headingColor="ffffff" detailColor="a9d8f5" warningColor="f2c46d" errorColor="ff9a8a"
//             successColor="9fe3a6" noteAlpha="0.85" pictureHeight="330" pictureWidth="0"
//             imageBackgroundColor="ffffff" imageBackgroundAlpha="0.05"/>
//
// pictureHeight is the picture's box above the text (0: 45% of the panel);
// pictureWidth > 0 puts the picture on the left that wide instead, the text
// beside it (for a wide, low panel). Without a picture the text starts at the
// top. Text that doesn't fit ends in "...". It follows the page's first <form>.
//
// defaultImage="..." is shown when the row has no picture of its own (never
// larger than the file, so an icon stays crisp), and child elements pick
// another for some screens by form id, a trailing * matching the start:
//
//   <formHelp ... defaultImage="../collections/Home/medium_artwork/icon/Manage.png">
//     <defaultImage forms="settings control*" src="../collections/Home/medium_artwork/icon/Settings.png"/>
//   </formHelp>
class FormHelp : public Component
{
public:
    // A default picture for the screens whose form ids match one of forms ("control*": any starting so)
    struct DefaultImage
    {
        std::vector<std::string> forms;   // empty: every screen
        std::string path;                 // absolute
    };

    struct Style
    {
        float pictureHeight = 0.0f;
        float pictureWidth = 0.0f;
        std::vector<DefaultImage> defaultImages;   // the first that matches the screen; one without forms last
        SDL_Color imageBackground{ 0, 0, 0, 255 };
        float imageBackgroundAlpha = 0.0f;
        float noteAlpha = 0.85f;      // the help text
        float detailAlpha = 1.0f;     // the status line (detailColor)
        float headingScale = 1.2f;    // the heading, relative to fontSize
    };

    enum class FontRole { Heading, Detail, Warning, Error, Success };

    FormHelp(Page& p, FontManager* font, int monitor);
    ~FormHelp() override;

    void setStyle(const Style& style) { style_ = style; }
    void setFont(FontRole role, FontManager* font);

    // The form whose focused row it shows; owned by the same page
    void attach(const FormWidget* form) { form_ = form; }

    void allocateGraphicsMemory() override;
    void freeGraphicsMemory() override;
    void draw() override;

private:
    float fontSize() const;
    float pixelScale() const;
    std::string defaultImage() const;   // for the screen shown, or ""
    const std::vector<std::string>& wrap(FontManager* font, const std::string& text, float width, float size);
    // Draws lines from y while they fit above bottom; the last one that fits ends in "..." when more is left
    float drawLines(FontManager* font, const std::vector<std::string>& lines, float x, float y, float width, float size,
                    float lineHeight, float bottom, float alpha);

    WidgetPainter painter_;
    Style style_;
    FontManager* headingFont_ = nullptr;
    FontManager* detailFont_ = nullptr;
    FontManager* warningFont_ = nullptr;
    FontManager* errorFont_ = nullptr;
    FontManager* successFont_ = nullptr;
    const FormWidget* form_ = nullptr;
    TextureCache pictures_;
    std::unordered_map<std::string, std::vector<std::string>> wrapped_;
};

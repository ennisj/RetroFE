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

// Template expansion and layout XML generation: turns a project page into the
// layout.xml PageBuilder builds. The preview and the saved theme use the same
// bytes, so what the editor shows is what the theme does.
//
// Template sites inside a widget fragment:
//   {{ID}} {{X}} {{Y}} {{W}} {{H}} {{LAYER}}   the instance's own values
//   {{CX}} {{CY}} {{X2}} {{Y2}}                centre and far edges
//   {{FS}}                                     a font size for the box (72% of H)
//   {{ALPHA}}                                  opacity as the layout writes it ("1", "0.60")
//   {{FONT}} {{FONTCOLOR}}                     the instance's font and colour
//   {{SCROLLTIME}} {{SCROLLACCEL}}             a wheel's scroll feel, in seconds
//   {{THEMEDIR}}                               the theme's folder under layouts/
//   {{PAGEROOT}}                               the theme folder relative to the page's own
//                                              folder: "" for layout.xml and splash.xml,
//                                              "../../../" for collections/<c>/layout/layout.xml
//                                              (PageBuilder resolves src= against the page's
//                                              folder first, so theme files are written
//                                              src="{{PAGEROOT}}{{SRC}}")
//   {{TOKEN}}                bool var -> "true"/"false"; text var -> value, XML-escaped
//   {{TOKEN:a:b}}            bool branch: a when true, b when false
//   {{?TOKEN}}...{{/TOKEN}}  kept only when bool TOKEN is true
//   {{!TOKEN}}...{{/TOKEN}}  kept only when bool TOKEN is false
//   {{ANIM_DEFAULTS}}        the definition's canned animations
//   {{MENU_ITEMS}}           menu definitions: the computed slots
// Built-in bools: HASFONT, HASFONTCOLOR, HASACCEL, and WARP (always false
// here: this engine has no transform=).
//
// Every unresolved site is reported, never written into a theme.
//
// Dialect: the stock widgets are written for this fork's PageBuilder (see
// docs/design/theme-editor.md). Projects made with One Saucier are translated
// as they generate: a warped widget is drawn as its corners' bounding box,
// and mode="layout_preferred" becomes no mode (the game's own media). Each
// translation is reported as a warning.
//
// Ported from One Saucier's theme editor (app/editor/theme_generate.*). Pure.

#include "ThemeProject.h"
#include "WidgetDefs.h"
#include <string>
#include <vector>

namespace ThemeEditor
{
    // Line 1 of every generated page. It's what marks a theme as editor-made:
    // saving refuses to overwrite a layout.xml without it.
    std::string editorMarker(const std::string& projectSlug);
    // True for this editor's marker and for One Saucier's (both are editor-made)
    bool isEditorMarker(const std::string& firstLine);

    struct GenOptions
    {
        // The canvas's Show toggles: media off drops static/media/menu
        // widgets, text off drops text widgets. Never applied when saving.
        bool includeMedia = true;
        bool includeText = true;
        bool includeAnims = true;   // off = still previews and tests
        // Canvas preview only: skips widgets hidden on the canvas and the
        // ghost wheel (which only shows one menu level down)
        bool canvasPreview = false;
        // The page's design space when it isn't the project's canvas; 0 = canvas
        int spaceW = 0;
        int spaceH = 0;
        // The theme's folder under layouts/ ({{THEMEDIR}}); "" = themeFolderName(project name)
        std::string themeDir;
        // {{PAGEROOT}}; generateLayoutXml sets it from the page
        std::string pageRoot;
    };

    struct ExpandResult
    {
        std::string text;
        std::vector<std::string> unknown;   // unresolved sites, in order
        bool ok() const { return unknown.empty(); }
    };

    // Expands `text` for one instance of `def`. Unknown sites are left in
    // place and reported.
    ExpandResult expandText(const std::string& text, const WidgetDef& def, const WidgetInstance& inst,
                            const GenOptions& opt);

    // expandText over the definition's fragment with {{ANIM_DEFAULTS}} spliced in
    ExpandResult expandFragment(const WidgetDef& def, const WidgetInstance& inst, const GenOptions& opt);

    struct GenResult
    {
        std::string xml;
        std::vector<std::string> errors;     // any error -> xml is empty
        std::vector<std::string> warnings;
        bool ok() const { return errors.empty(); }
    };

    // A complete layout.xml for one page. Checks that every definition exists,
    // ids are valid and unique, sizes aren't degenerate and no site is left
    // unresolved. The root carries the canvas size and the theme's font.
    GenResult generateLayoutXml(const ThemeProject& p, const ThemePage& page, const WidgetLibrary& lib,
                                const GenOptions& opt = GenOptions());

    // Menu slot rows, exposed for tests
    static const int MENU_ITEM_SLOTS = 9;
    static const int MENU_SLOTS_MIN = 3;
    static const int MENU_SLOTS_MAX = 15;

    // Where the selected slot of a vertical wheel lands (for the pulse overlay)
    struct MenuSelGeom
    {
        int x = 0, y = 0, w = 0, h = 0;
        bool valid = false;
    };

    // An <itemDefaults> plus `slots` <item> rows for a wheel in the box, the
    // centre slot selected and enlarged. arcPct bulges the centre toward the
    // canvas centre; curvePx (theme units) overrides it; tiltDeg rotates the
    // ends; perspDeg foreshortens the ladder.
    std::string menuItemsXml(int x, int y, int w, int h, int layer, bool vertical,
                             int arcPct = 0, int canvasW = 1920, int slots = MENU_ITEM_SLOTS,
                             int fadePct = 0, int curvePx = 0, int tiltDeg = 0, int perspDeg = 0,
                             int itemHPx = 0, MenuSelGeom* selOut = nullptr, bool ghostStyle = false,
                             int idleDimPct = -1, const std::string& fontSizePx = std::string(),
                             const std::string& fontColor = std::string(),
                             const std::string& selFontColor = std::string(),
                             int ghostItemPct = -1, int ghostAlphaPct = -1);

    // The selected-logo pulse: a logo parked on the selected slot that glows
    // when the menu goes idle
    std::string menuPulseXml(const MenuSelGeom& g, int layer);

    // An art strip's slots: neighbours on an xOffset ladder, the selected slot
    // hidden. pitchPx > 0 lays them out left to right at that pitch instead.
    std::string menuStripItemsXml(int x, int y, int w, int h, int layer, int slots = 9, int pitchPx = 0,
                                  int itemMaxW = 0, int dimPct = -1, int selW = 0, int selH = 0);
}

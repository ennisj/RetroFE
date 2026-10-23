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

// What the layout-native editor can add to a hand-made page, and the names
// its animation editor offers. Pure: no SDL, no filesystem.
//
// The palette is the elements PageBuilder builds at the top of a page, each
// as the raw element with the attributes it needs (LayoutCheck's
// vocabulary; a test checks every entry passes the layout check on a page of
// its own): a fixed image or video (src), fixed text, the status text, a
// container, the game's art, video, text and scrolling text (each with its
// `type`), hiscores, and menus with their <itemDefaults>.

#include <string>
#include <vector>

namespace ThemeEditor
{
    struct PaletteEntry
    {
        std::string id;          // "image", "reloadableText", "menu-custom"
        std::string label;       // "Image (a file of the theme)"
        std::string element;     // the element written: "image"
        std::string summary;     // one line for the palette
        std::string file;        // "image" / "video": it shows a file of the theme (src), chosen first
        std::string typeAttr;    // the attribute that says what it shows ("type", "imageType"), or ""
        std::vector<std::string> types;   // choices for it, the default first (any other value can be typed later)
        int w = 0, h = 0;        // its default size, for placing it
        bool sized = true;       // width and height are written (text sizes itself: they aren't)
        std::string body;        // the template: {X} {Y} {W} {H} {CX} {CY} {LAYER} {TYPE} {SRC}
    };

    const std::vector<PaletteEntry>& layoutPalette();
    const PaletteEntry* paletteEntry(const std::string& id);

    // The element's text (a fragment for withChild: lines nested by 4
    // spaces) with its top-left corner at x, y. `type` replaces the default
    // type ("" keeps it); `src` is the file for an image or video ("" keeps
    // a placeholder name). `layer` is the layer it's drawn on.
    std::string paletteFragment(const PaletteEntry& entry, const std::string& type, const std::string& src,
                                int x, int y, int layer);

    // A new animation: an event with one set and one step, a set with one
    // step, a step. One line each, as most hand-made layouts write them.
    std::string eventFragment(const std::string& event);
    std::string setFragment();
    std::string animateFragment();

    // <animate type=...> as the engine reads them (Tween::getTweenProperty,
    // matched in any case), written the way layouts do: x, y, alpha,
    // xOffset, fontSize... A test checks them against LayoutCheck's list.
    const std::vector<std::string>& tweenTypeNames();
    // algorithm= values (Tween::getTweenType), in camel case; linear first.
    // "easeOnOutQuintic" is the engine's own spelling.
    const std::vector<std::string>& tweenAlgorithmNames();

    // The engine's name for an event element: onMenuEnter -> "menuEnter"
    std::string engineEventName(const std::string& element);
}

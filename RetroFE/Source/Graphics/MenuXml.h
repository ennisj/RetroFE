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

#include <rapidxml.hpp>

// What PageBuilder::buildMenu needs from a <menu> element before it builds
// anything. No SDL here, so retrofe_tests can check it.
namespace MenuXml
{
    // The menu's <itemDefaults>. A hand-made layout may leave it out, and
    // PageBuilder reads it unconditionally (the font, every slot's defaults),
    // which crashed RetroFE; such a menu gets an empty one, as if the layout
    // had written <itemDefaults/>. `added` says it was missing.
    inline rapidxml::xml_node<>* itemDefaults(rapidxml::xml_node<>* menu, bool* added = nullptr)
    {
        if (added) *added = false;
        if (!menu) return nullptr;
        rapidxml::xml_node<>* defaults = menu->first_node("itemDefaults");
        if (defaults) return defaults;
        rapidxml::xml_document<>* doc = menu->document();
        if (!doc) return nullptr;
        defaults = doc->allocate_node(rapidxml::node_element, "itemDefaults");
        menu->prepend_node(defaults);
        if (added) *added = true;
        return defaults;
    }

    // A vertical menu adds slots until the next one would reach the menu's
    // height. A slot that adds no height (no height on the item or its
    // defaults, no spacing) would never get there and the loop would run
    // until memory ran out, so that ends the menu too.
    inline bool verticalMenuFull(float height, float nextHeight, float menuHeight)
    {
        return nextHeight >= menuHeight || nextHeight <= height;
    }
}

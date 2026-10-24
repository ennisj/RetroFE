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

#include <cstddef>
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

    // The most slots a vertical menu gets, whatever its sizes say
    static const size_t MAX_VERTICAL_SLOTS = 1024;

    // A vertical menu adds slots until the next one would reach the menu's
    // height. When the slots from here on all take <itemDefaults> (no item
    // overrides an index past this one) and the defaults add no height (no
    // height, no spacing), it would never get there and the loop would run
    // until memory ran out: that ends the menu. A single overridden slot of
    // height 0, or negative spacing on one, is left alone. `slots` is how
    // many the menu has with this one; MAX_VERTICAL_SLOTS is the backstop.
    inline bool verticalMenuFull(float height, float nextHeight, float menuHeight, bool onlyDefaultsFromHere, size_t slots)
    {
        if (nextHeight >= menuHeight) return true;
        if (onlyDefaultsFromHere && nextHeight <= height) return true;
        return slots >= MAX_VERTICAL_SLOTS;
    }
}

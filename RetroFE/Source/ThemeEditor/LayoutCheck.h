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

// Checks layout XML against what this fork's PageBuilder actually reads,
// without SDL. PageBuilder never fails on a layout it doesn't understand: an
// unknown element or attribute is silently ignored, and a missing required
// attribute logs an error and drops the component. So "it built" proves
// little; this check reports each of those cases instead:
//   - the XML doesn't parse with rapidxml (the parser PageBuilder uses)
//   - no <layout> root, or no width/height on it
//   - an element PageBuilder doesn't build, at the top level or inside a
//     component (only animation events, and <itemDefaults>/<item> in menus)
//   - an attribute PageBuilder doesn't read on that element
//   - a required attribute PageBuilder logs an error for (image src, text
//     value, reloadable type, type="file" location, sound src/type, ...)
//   - a <menu> without <itemDefaults> (PageBuilder dereferences it)
//   - a mode value PageBuilder doesn't recognise for that element
//   - an animation set without duration, an animate without type, with an
//     unknown tween type, or without "to"
//
// The vocabulary is kept in step with PageBuilder by a test that checks every
// name here appears in PageBuilder.cpp (and every tween type in Tween.cpp).
// It doesn't check asset files exist or that values are well-formed numbers.

#include <string>
#include <vector>

namespace ThemeEditor
{
    // Problems found, each naming the element (and its id when it has one)
    std::vector<std::string> checkLayoutXml(const std::string& xml);

    // The names the check accepts, for the drift test
    struct LayoutVocabulary
    {
        std::vector<std::string> elements;     // element names
        std::vector<std::string> attributes;   // attribute names, any element
        std::vector<std::string> events;       // animation event elements
        std::vector<std::string> values;       // mode and sound type values
        std::vector<std::string> tweens;       // <animate type=> and algorithm values, lower case
    };
    LayoutVocabulary layoutVocabulary();
}

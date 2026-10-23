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
#include <string>

// Where an element is in a layout file, as a path from the <layout> root:
// each step is the element's name and its index among its parent's
// children of that name ("image[3]", "menu[0]/item[2]"); the root is "".
// PageBuilder records it for the components it builds when asked
// (PageBuilder::setTrace), and the theme editor's layout-native mode names
// elements the same way when it reads the file as text
// (ThemeEditor/LayoutText.h), so a box on the preview finds its element.
// No SDL here, so retrofe_tests can check both agree.
namespace LayoutNodePath
{
    inline std::string of(const rapidxml::xml_node<>* node)
    {
        std::string path;
        // Up to (not including) the top-level element: the <layout> root
        while (node && node->parent() && node->parent()->type() == rapidxml::node_element) {
            int index = 0;
            for (const rapidxml::xml_node<>* s = node->previous_sibling(node->name(), node->name_size()); s;
                 s = s->previous_sibling(node->name(), node->name_size())) {
                ++index;
            }
            std::string step = std::string(node->name(), node->name_size()) + "[" + std::to_string(index) + "]";
            path = path.empty() ? step : step + "/" + path;
            node = node->parent();
        }
        return path;
    }
}

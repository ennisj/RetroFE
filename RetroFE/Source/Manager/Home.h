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

// The Home menu: RetroFE's top level when homeMenu is enabled. It's built in
// memory rather than read from collections/, but layouts style it like any
// collection, e.g. layouts/<layout>/collections/Home/layout/layout.xml and
// artwork named after its items.
namespace Home
{
    constexpr const char* collectionName = "Home";

    // Items, in menu order
    constexpr const char* settings = "Settings";
    constexpr const char* manage = "Manage";
    constexpr const char* browse = "Browse";   // goes to firstCollection

    constexpr const char* items[] = { settings, manage, browse };

    // The item Home starts on, as a position in items: Browse once there's
    // something to browse (collections), Manage on a bare install, where the
    // first system is added.
    inline size_t landing(bool hasContent)
    {
        const char* wanted = hasContent ? browse : manage;
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            if (items[i] == wanted) {
                return i;
            }
        }
        return 0;
    }
}

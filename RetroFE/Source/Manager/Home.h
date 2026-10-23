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

// The Home menu: RetroFE's top level when homeMenu is enabled. It's built in
// memory rather than read from collections/, but layouts style it like any
// collection, e.g. layouts/<layout>/collections/Home/layout/layout.xml and
// artwork named after its items.
namespace Home
{
    constexpr const char* collectionName = "Home";

    // Items, in menu order
    constexpr const char* catalog = "Catalog";
    constexpr const char* settings = "Settings";
    constexpr const char* manage = "Manage";
    constexpr const char* browse = "Browse";   // goes to firstCollection

    constexpr const char* items[] = { catalog, settings, manage, browse };

    // The Catalog list, opened from Home's Catalog item: also built in memory,
    // one item per package, named by package id, so artwork is <id>.png.
    // Layouts style it as the "Catalog" collection.
    constexpr const char* catalogCollectionName = "Catalog";
    // Its last item, which opens the Sources screen (ids can't contain spaces)
    constexpr const char* catalogSourcesItem = "Catalog Sources";
}

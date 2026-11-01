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

#include "ThemeProject.h"
#include <set>

namespace ThemeEditor
{
    struct CollectionRow
    {
        std::string name;
        int depth = 0;
        bool menu = false;
        bool children = false;
        std::string otherLayout;
        bool covered = false;
        bool videos = false;
        std::string layoutFrom;
        std::string label() const { return depth < 0 ? "Not in a menu" : name.empty() ? "<root> (Top Level)" : name; }
    };

    // Input is the index's depth-first menu tree followed by its unattached group.
    inline std::vector<CollectionRow> collectionRows(const std::vector<CollectionRow>& tree,
        const std::set<std::string>& collapsed, const ThemeProject& project, const ThemePage& edited)
    {
        std::vector<CollectionRow> out(1);
        out[0].covered = &project.pages[0] == &edited;
        int hiddenBelow = -1;
        for (auto row : tree) {
            if (hiddenBelow >= 0 && row.depth > hiddenBelow) continue;
            hiddenBelow = -1;
            row.covered = row.depth >= 0 && &pageShownFor(project, row.name, row.menu, nullptr, row.videos, row.layoutFrom) == &edited;
            out.push_back(row);
            if (row.children && collapsed.count(row.name)) hiddenBelow = row.depth;
        }
        return out;
    }

    inline std::string templateChangeLine(const std::string& collection, const std::string& target, size_t under)
    {
        return "Changes \"" + collection + "\"" + (under ? " and its " + std::to_string(under) + " collections" : "") + " to " + target + ".";
    }
}

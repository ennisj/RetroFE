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

#include <set>
#include <string>
#include <vector>

namespace Manager
{
    // Saved changes RetroFE shows only after it reloads (when Manage closes,
    // or "Apply changes now" on the hub), for the "changes waiting" line on
    // every Manage screen (info:pendingChanges). Unsaved edits on a screen
    // don't count: they aren't changes yet.
    //
    // What counts as one change:
    //   - each different thing the library was told changed
    //     (Indexer::changed("game metadata"), "systems: Arcade", ...): saving the
    //     same screen twice is still one change
    //   - a library change with no description, when there's none with one
    //   - Services::requestReload (e.g. "Use this theme"), once
    //   - Catalog installs or removals that finished and aren't shown yet, once
    struct PendingChanges
    {
        std::vector<std::string> library;   // Indexer::pendingChanges()
        bool libraryFlag = false;           // Indexer::changesPending()
        bool reloadAsked = false;           // A reload was asked for or is queued
        bool catalog = false;               // Catalog::Service::hasChanges()

        bool needsReloadOnClose() const
        {
            return libraryFlag || reloadAsked || catalog;
        }

        size_t count() const
        {
            std::set<std::string> distinct;
            for (const std::string& what : library) {
                if (!what.empty()) {
                    distinct.insert(what);
                }
            }
            size_t n = distinct.size();
            if (n == 0 && libraryFlag) {
                n = 1;
            }
            return n + (reloadAsked ? 1 : 0) + (catalog ? 1 : 0);
        }

        // "2 changes apply when you close Manage", or "" when there are none
        std::string text() const
        {
            size_t n = count();
            if (n == 0) {
                return "";
            }
            return n == 1 ? "1 change applies when you close Manage"
                          : std::to_string(n) + " changes apply when you close Manage";
        }

        // What the hub says once RetroFE is back after "Apply changes now"
        static std::string appliedText(size_t n)
        {
            return n == 1 ? "Applied 1 change." : "Applied " + std::to_string(n) + " changes.";
        }
    };
}

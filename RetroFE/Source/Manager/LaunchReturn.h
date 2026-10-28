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

#include "Navigator.h"
#include "../Library/Index.h"
#include "../Collection/VideoCollections.h"
#include <algorithm>
#include <map>

namespace Manager
{
    // Owned by the launch request, including while LaunchQueue waits across a
    // reload. Only view state: records and caches come from the new library.
    struct LaunchReturn
    {
        std::vector<Navigator::Frame> frames;
        std::map<std::string, Forms::Params> providers;

        static bool available(const Navigator::Frame& frame, const Library::Index& index)
        {
            auto param = [&](const std::string& key) {
                auto found = frame.params.find(key);
                return found == frame.params.end() ? std::string() : found->second;
            };
            const std::string& form = frame.form;
            std::string collection = param("collection"), id = param("id");
            auto hasCollection = [&](const std::string& name) {
                int c = index.findCollection(name);
                return c >= 0 && index.collections[static_cast<size_t>(c)].name == name;
            };
            if (!collection.empty() && !hasCollection(collection)) {
                return false;
            }
            if (form == "game" || form.compare(0, 5, "game-") == 0) {
                return index.findGame(collection, id) >= 0;
            }
            bool system = form == "system" || form.compare(0, 7, "system-") == 0
                       || form == "video" || form.compare(0, 6, "video-") == 0;
            bool menu = form == "menu" || form.compare(0, 5, "menu-") == 0;
            if ((system || menu) && !id.empty()) {
                int c = index.findCollection(id);
                if (c < 0 || !hasCollection(id)) {
                    return false;
                }
                const auto& record = index.collections[static_cast<size_t>(c)];
                if (!param("playlist").empty() && std::none_of(record.playlists.begin(), record.playlists.end(),
                    [&](const auto& playlist) { return playlist.name == param("playlist"); })) {
                    return false;
                }
                if (system) {
                    auto type = record.info.find("type");
                    bool video = type != record.info.end() && VideoCollections::isVideoType(type->second);
                    if (!(record.kinds & Library::KindSystem) || video != (form.compare(0, 5, "video") == 0)) {
                        return false;
                    }
                }
                if (form == "menu-source" && std::none_of(record.subs.begin(), record.subs.end(),
                    [&](const auto& source) { return source.source == param("source"); })) {
                    return false;
                }
                if (form == "menu-entry" && std::none_of(record.children.begin(), record.children.end(),
                    [&](const auto& entry) { return entry.entry == param("entry"); })) {
                    return false;
                }
            }
            if (form == "emulator" || form.compare(0, 9, "emulator-") == 0) {
                Library::LauncherScope scope;
                std::string value = param("scope");
                if (value.empty() || value == "global") scope = Library::LauncherScope::Global;
                else if (value == "collection") scope = Library::LauncherScope::Collection;
                else if (value == "local") scope = Library::LauncherScope::Local;
                else return false;
                return index.findLauncher(scope, id) >= 0;
            }
            return true;
        }

        bool restore(Navigator& navigator, const Library::Index& index,
                     const std::function<bool(const std::string&)>& hasForm) const
        {
            // A filter naming a removed collection can't show the same results.
            for (const auto& provider : { std::string("game"), std::string("system"), std::string("video") }) {
                bool shown = std::any_of(frames.begin(), frames.end(), [&](const auto& frame) {
                    return provider == "game" ? frame.form == "games" || frame.form == "games-filters"
                                              : frame.form == provider + "s";
                });
                if (!shown) {
                    continue;
                }
                auto state = providers.find(provider);
                if (state == providers.end()) {
                    continue;
                }
                auto filter = state->second.find(provider == "game" ? "filter" : "menu");
                if (filter != state->second.end() && !filter->second.empty()) {
                    int c = index.findCollection(filter->second);
                    if (c < 0 || index.collections[static_cast<size_t>(c)].name != filter->second) {
                        return false;
                    }
                }
            }
            return navigator.restore(frames, [&](const auto& frame) {
                return hasForm(frame.form) && available(frame, index);
            });
        }
    };
}

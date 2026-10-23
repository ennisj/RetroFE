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

#include "../Forms/Form.h"
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class Configuration;
class ManagerModel;
namespace Catalog { class Service; }

// Entity modules for the Manage screens (games, menus, systems...). A provider
// serves every binding under its prefix, so a "game" provider answers:
//
//   bind="game:title"           get / set / setDefault("title")
//   <list bind="game:tags">     list("tags")
//   <results bind="game:search"> rows("search", limit)
//   options="game:genres"       options("genres")
//   action="game.save"          actionProblem / runAction("save")
//
// Keys and actions arrive without the prefix. Every call gets the screen
// being shown, whose parameters say which record it's about (param "id" for
// a row chosen from a results list). Register providers in Providers.cpp.
namespace Manager
{
    // The screen a call is made for
    struct Screen
    {
        std::string form;        // form id, e.g. "game"
        Forms::Params params;    // e.g. { "id": "pacman" }

        // A parameter, or "" when it isn't set
        std::string param(const std::string& name) const
        {
            auto found = params.find(name);
            return found != params.end() ? found->second : "";
        }
    };

    class Provider
    {
    public:
        virtual ~Provider() = default;

        // The prefix served, e.g. "game". Built-in prefixes (settings, draft,
        // info, param, package, collection, source, catalog, open, back) can't be used.
        virtual std::string prefix() const = 0;

        // The manager is opening: re-read anything cached, it may have changed while closed
        virtual void refresh() {}

        // A screen became current, opened or returned to with Back. Every
        // provider is told about every screen, so check screen.form. Typically:
        // load the record param:id names into an edit buffer, keeping unsaved
        // edits when it's the record already loaded (see EditBuffer.h).
        virtual void enter(const Screen& screen) { (void)screen; }

        // The user is leaving screen (Back, Escape, the Settings key, closing).
        // Return "" to let them, or a warning such as "You have unsaved
        // changes. Save them, or leave again to discard them." to stop them
        // once: doing it again leaves, after calling discard(screen). When
        // the manager closes, every open screen is asked, newest first.
        virtual std::string leaving(const Screen& screen) { (void)screen; return ""; }
        virtual void discard(const Screen& screen) { (void)screen; }

        // Called every frame while the manager is open, for work done a bit at
        // a time (e.g. scanning a folder). Keep each call short.
        virtual void update(float dt) { (void)dt; }

        // Bump this when values change by themselves (e.g. update() found
        // more), so the screen shows them straight away
        virtual unsigned generation() const { return 0; }

        virtual std::string get(const std::string& key, const Screen& screen) = 0;

        virtual void set(const std::string& key, const std::string& value, const Screen& screen)
        {
            (void)key; (void)value; (void)screen;
        }

        // A field's default attribute; what it means is up to the provider
        virtual void setDefault(const std::string& key, const std::string& value, const Screen& screen)
        {
            (void)key; (void)value; (void)screen;
        }

        virtual std::vector<std::string> list(const std::string& key, const Screen& screen)
        {
            (void)key; (void)screen;
            return {};
        }

        // At most limit rows, with the total there would be. Called each time
        // the screen is rebuilt (on input, and a few times a second): cache
        // the matches, e.g. by search text.
        virtual Forms::Rows rows(const std::string& key, size_t limit, const Screen& screen)
        {
            (void)key; (void)limit; (void)screen;
            return {};
        }

        // (value, label) pairs for a choice
        virtual std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Screen& screen)
        {
            (void)key; (void)screen;
            return {};
        }

        // Why an action can't run right now, or "" if it can. Buttons are greyed
        // out with this as the reason.
        virtual std::string actionProblem(const std::string& action, const Screen& screen)
        {
            (void)screen;
            return "Unknown action " + prefix() + "." + action;
        }

        // Set Result::open / params / back to move to another screen after
        // success. The opened screen gets Result::params, plus this screen's
        // when Result::keepParams is set.
        virtual Forms::Model::Result runAction(const std::string& action, const Screen& screen)
        {
            Forms::Model::Result result;
            result.message = actionProblem(action, screen);
            return result;
        }
    };

    // What providers can use; add to it as providers need more
    struct Services
    {
        Configuration& config;
        Catalog::Service* catalog = nullptr;   // may be null

        // RetroFE should reload to show a change (e.g. a game's new title),
        // once the user closes the manager rather than straight away
        std::function<void()> requestReload = [] {};
    };

    // Adds every provider to the model (Providers.cpp)
    void registerProviders(ManagerModel& model, const Services& services);
}

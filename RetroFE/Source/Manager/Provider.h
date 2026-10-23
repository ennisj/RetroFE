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

        // A screen became current, opened or returned to with Back. Typically:
        // load the record param:id names into an edit buffer, keeping unsaved
        // edits when it's the record already loaded.
        virtual void enter(const Screen& screen) { (void)screen; }

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

        // At most limit rows, with the total there would be. Called every
        // frame while shown: cache the matches, e.g. by search text.
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

        // Set Result::open / params / back to move to another screen after success
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
    };

    // Adds every provider to the model (Providers.cpp)
    void registerProviders(ManagerModel& model, const Services& services);
}

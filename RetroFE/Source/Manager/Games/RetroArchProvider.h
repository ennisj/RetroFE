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

#include "../EditBuffer.h"
#include "../Provider.h"
#include "../../Library/Backups.h"
#include "../../Library/Index.h"
#include "../../Library/RetroArch.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Manager
{
    // The RetroArch screens of a game and of a system (prefix "retroarch"),
    // modelled on One Saucier's RETROARCH / CORE OPTIONS / CONTROLS tiles. A
    // game's screens (collection + id) edit its own <rom>.cfg / .opt / .rmp;
    // a system's (id) edit the core-wide <Core>.cfg / .opt / .rmp of the core
    // its default launcher runs, and a launcher's (Emulators: id + scope) the
    // same core-wide files of the core it runs. The logic is Library/RetroArch.h.
    //
    // Forms (RetroArchForms.cpp, {p} = game or system):
    //   {p}-retroarch      the core, the RetroArch settings rows, links to the other two
    //   {p}-core-options   the core's curated options (rows), each opening
    //   {p}-core-option    one option (param key)
    //   {p}-controls       the stick and a row per button
    // Keys:
    //   runs, status                       on game / system: the link's visibility and status
    //   screenTitle, why, isReady, isGame, core, coreNote, launcher, intro, reaches,
    //   file, disabledNote, isDirty, hasOwnFile, canUseCore, canRestore,
    //   optionsStatus, controlsStatus      on the RetroArch screens
    //   row:<id> (options choices:<id>)    a RetroArch settings or controls row
    //   rows coreOptions; opt (options optChoices), optionTitle, optionNote
    // Actions: save, reset, useCore (a game's own file goes), restore (the original back)
    class RetroArchProvider : public Provider
    {
    public:
        explicit RetroArchProvider(const Services& services,
                                   Library::RetroArch::Environment environment = Library::RetroArch::Environment::current());

        std::string prefix() const override { return "retroarch"; }
        void refresh() override;
        void enter(const Screen& screen) override;
        std::string leaving(const Screen& screen) override;
        void discard(const Screen& screen) override;
        unsigned generation() const override { return generation_; }

        std::string get(const std::string& key, const Screen& screen) override;
        // Warnings: RetroArch or its core not found, or not for this game
        Forms::Severity severity(const std::string& key, const Screen& screen) override;
        void set(const std::string& key, const std::string& value, const Screen& screen) override;
        std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Screen& screen) override;
        std::string actionProblem(const std::string& action, const Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Screen& screen) override;

    private:
        using IndexPtr = std::shared_ptr<const Library::Index>;
        using Tile = Library::RetroArch::Tile;

        enum class Kind { None, GameLink, SystemLink, LauncherLink, Hub, CoreOptions, CoreOption, Controls };
        static Kind kindOf(const std::string& form);
        static bool isGameForm(const std::string& form);
        // A launcher's screens (Emulators: "emulator", "emulator-retroarch"...; id + scope)
        static bool isLauncherForm(const std::string& form);
        static bool tileOf(Kind kind, Tile& tile);

        // What the screen's game or system runs on
        struct Context
        {
            std::string key;
            IndexPtr index;
            bool game = false;
            bool found = false;
            std::string title;
            std::string why;                 // not found, or why its launcher isn't usable
            std::string launcher;
            Library::RetroArch::Setup setup;
            Library::RetroArch::Target target;
            std::vector<std::string> reaches;   // a system: the systems sharing the core's files
            bool reachesDone = false;
        };

        struct TileView
        {
            Library::RetroArch::TileState state;
            EditBuffer edits;
            bool loaded = false;
        };

        IndexPtr index() const;
        Library::Backups& backups();
        void sync(const Screen& screen);
        void loadContext(const Screen& screen, const std::string& key, const IndexPtr& idx);
        const Library::RetroArch::Setup& setupFor(const Library::Index& index, int launcher);
        TileView& tile(Tile tile);
        void loadTile(Tile tile, bool keepEdits);
        std::map<std::string, int> pendingChoices(Tile tile);
        static std::string contextKey(const Screen& screen);
        const Library::RetroArch::Row* findRow(Tile tile, const std::string& id);

        Services services_;
        Library::RetroArch::Environment environment_;
        std::unique_ptr<Library::Backups> ownBackups_;   // when there's no editor (tests)
        Context context_;
        std::map<int, TileView> tiles_;                  // by Tile
        IndexPtr setupsIndex_;
        std::map<int, Library::RetroArch::Setup> setups_;   // by launcher, for setupsIndex_
        unsigned generation_ = 0;
    };
}

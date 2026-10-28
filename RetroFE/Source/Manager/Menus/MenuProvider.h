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
#include "../../Library/Edits.h"
#include "../../Library/Index.h"
#include "../../Library/Indexer.h"
#include "../../Library/Media.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The Menus section (prefix "menu"): the menu tree from the top menu (what
// Browse opens) down, a menu's or merged collection's details, its entries,
// its sources, its info and artwork, and creating and deleting menus.
// Forms are in MenusForms.cpp; the screens and their parameters:
//
//   menus                      the tree, search, collections no menu lists
//   menu          id           details (a menu, merged collection or system)
//   menu-entries  id           a menu's entries; add one
//   menu-entry    id, entry    one entry: move, remove, fix its spelling, open it
//   menu-sources  id           a merged collection's .sub sources; add one
//   menu-source   id, source   one source: remove it
//   menu-info     id           info.conf title, manufacturer, year, genre (Save/Reset)
//   menu-media    id           system_artwork slots
//   menu-medium   id, type     one slot: preview, replace from a file, clear, put back;
//                              a media folder's files, add one, clear the folder
//   menu-medium-file id, type, file  one file of the slot's media folder: keep only it, take it out
//   menu-actions  id           new menu here, delete
//   menu-new      [id]         a new menu or merged collection (parent: id or the top menu)
//
// Every write goes through Services::editor and then library->changed(); while
// the index rebuilds after a write, further writes wait ("Updating...").
namespace Manager
{
    class MenuProvider : public Provider
    {
    public:
        explicit MenuProvider(const Services& services);

        std::string prefix() const override { return "menu"; }
        Forms::Params saveView() const override;
        void restoreView(const Forms::Params& state) override;
        void refresh() override;
        void enter(const Screen& screen) override;
        std::string leaving(const Screen& screen) override;
        void discard(const Screen& screen) override;
        unsigned generation() const override;

        std::string get(const std::string& key, const Screen& screen) override;
        // Warnings: an entry with a problem (no such folder, misspelt, a loop)
        Forms::Severity severity(const std::string& key, const Screen& screen) override;
        void set(const std::string& key, const std::string& value, const Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Screen& screen) override;
        std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Screen& screen) override;
        std::string actionProblem(const std::string& action, const Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Screen& screen) override;

        // The status line for an index state (a failed re-read says so, and that
        // changes wait), and why writes can't run in that state, or ""
        static std::string statusText(const Library::Indexer::Status& status, bool hasIndex);
        static std::string writeProblemFor(const Library::Indexer::Status& status, bool hasIndex);

    private:
        using Options = std::vector<std::pair<std::string, std::string>>;

        // The latest snapshot; drops what was worked out from an older one
        const Library::Index* index();
        bool building() const;
        const Library::CollectionRecord* record(const Screen& screen);
        int collectionOf(const Screen& screen);

        std::string summary(int collection);
        std::string kindName(int collection);
        std::string pageName(int collection);
        // What's wrong with a menu's entry, or ""; brief for a row's detail
        std::string entryProblem(int menu, size_t entry, bool brief = false);
        int entryPosition(const Screen& screen);
        const Library::MenuEntry* entry(const Screen& screen);

        void buildTree();
        Forms::Rows treeRows(size_t limit);
        const Options& addableEntries(const Screen& screen);
        const Options& addableSources(const Screen& screen);
        const Options& parentMenus();
        const Options& systems();
        std::string choice(const std::string& value, const Options& options);

        struct MediaInfo
        {
            Library::MediaSlot slot;
            bool canRestore = false;
            std::string shownInstead;   // a parent menu's own art for this entry, which wins
            std::string text;           // a text slot's first lines
        };
        const std::vector<MediaInfo>& media(const Screen& screen);
        const MediaInfo* medium(const Screen& screen);
        std::string newMediumFile(const Screen& screen);

        std::map<std::string, std::string> readInfo(const Library::CollectionRecord& record);
        std::string writeProblem();
        Forms::Model::Result finish(const Library::EditResult& edit, const std::string& what);

        Services services_;
        std::shared_ptr<const Library::Index> index_;
        unsigned local_ = 0;               // bumps after a write, for generation()

        std::string query_;
        std::vector<Forms::Row> tree_;     // the whole tree, indented
        std::vector<std::string> paths_;   // each tree row as "Main > Consoles > NES", for search
        std::vector<Forms::Row> unlisted_; // menus and merged collections no menu reaches
        bool treeBuilt_ = false;

        // Choices, worked out once per snapshot and screen
        std::string optionsKey_;
        Options addable_, sources_;
        bool parentsBuilt_ = false, systemsBuilt_ = false;
        Options parents_, systems_;

        // Drafts
        std::string addEntry_, addSource_;
        std::string newKind_ = "menu", newName_, newParent_, newSource_;
        std::string newFile_;
        EditBuffer edits_;
        std::set<std::string> deleted_;    // deleted this session, for the screen left behind

        // Media of one collection
        std::string mediaKey_;
        std::vector<MediaInfo> media_;

        // Worked out once per snapshot: a menu's entry rows (loop checks walk
        // the tree) and whether a collection can be deleted (reads its folder)
        std::string entriesKey_;
        std::vector<Forms::Row> entryRows_;
        std::string deleteKey_;
        std::string deleteProblem_;
        const std::string& deleteProblem(const std::string& name);
        void forget();
    };
}

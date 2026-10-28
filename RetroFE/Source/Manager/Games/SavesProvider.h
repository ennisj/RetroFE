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

#include "../Provider.h"
#include "../../Library/Index.h"
#include "../../Library/RetroArch.h"
#include "../../Library/Saves.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Manager
{
    // A game's saves (prefix "saves"): its save files and save states, where
    // RetroArch keeps them, and backups under manage/saves/<collection>/<game>/.
    // The logic is Library/Saves.h. Screens (GamesForms.cpp), params
    // collection + id:
    //   game               the "Saves" link: saves:status
    //   game-saves         where they are, a link to the files, Back up now, Delete, the backups
    //   game-saves-files   the game's files, one line each
    //   game-saves-backup  one backup (param backup): its files, Restore, Delete
    // Keys: screenTitle, why, isKnown, savesFolder, statesFolder, notes, hasFiles,
    //   filesStatus ("7 files"), filesText, backupsFolder (Saved in), backupsNote;
    //   list files; rows backups; backupTitle, backupWhen,
    //   backupReason, backupFolder, backupProblem; list backupFiles
    // Actions: backup, delete, restore, deleteBackup
    class SavesProvider : public Provider
    {
    public:
        explicit SavesProvider(const Services& services,
                               Library::RetroArch::Environment environment = Library::RetroArch::Environment::current());

        std::string prefix() const override { return "saves"; }
        void refresh() override;
        void enter(const Screen& screen) override;
        unsigned generation() const override { return generation_; }

        std::string get(const std::string& key, const Screen& screen) override;
        std::vector<std::string> list(const std::string& key, const Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Screen& screen) override;
        std::string actionProblem(const std::string& action, const Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Screen& screen) override;

    private:
        using IndexPtr = std::shared_ptr<const Library::Index>;

        static bool isSavesForm(const std::string& form);
        // Works out the screen's game again when it or the snapshot changed
        void sync(const Screen& screen);
        // Reads the folders: the files and backups now
        void scan();

        struct Context
        {
            std::string key;
            IndexPtr index;
            bool found = false;
            std::string why;                 // the game isn't there
            std::string root;
            std::string collection;          // the game's own system, as on disk
            std::string game;                // its name
            std::string title;
            Library::Saves::Folders folders;
            std::vector<Library::Saves::File> files;
            std::vector<Library::Saves::Backup> backups;
        };

        Services services_;
        Library::RetroArch::Environment environment_;
        Context context_;
        IndexPtr setupsIndex_;
        std::map<int, Library::RetroArch::Setup> setups_;   // by launcher, for setupsIndex_
        unsigned generation_ = 0;
    };
}

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

// A game's save files and save states, for the Manage screens: where
// RetroArch keeps them (worked out from retroarch.cfg the way RetroArch's
// runloop_path_set_redirect does), which files are this game's, and backups
// of them under <root>/manage/saves/<collection>/<game>/<id>/, kept out of
// the content folders. Only RetroArch launchers are known: other emulators
// each have their own rules (MAME keeps a folder per game), so their saves
// are "not known". No SDL: built into retrofe_tests too.

#include "Edits.h"
#include "Index.h"
#include "RetroArch.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Library
{
    namespace Saves
    {
        // Where one game's saves are
        struct Folders
        {
            bool known = false;
            std::string why;            // when not known: a sentence for the screen
            std::string saves;          // save files (.srm ...)
            std::string states;         // save states (.state, .state1 ..., thumbnails, replays)
            std::string stem;           // the name RetroArch gives the game's files
            std::string contentFile;    // the game's own file, never counted as a save
            bool ignoreCase = false;    // the folders' file system ignores case (Windows; most macOS
                                        // volumes): Sonic.srm is then also sonic.md's
            std::vector<std::string> notes;
        };
        // setup: RetroArch::detect() for the launcher the game starts with
        Folders forGame(const RetroArch::Setup& setup, const GameRecord& game,
                        const RetroArch::Environment& env = RetroArch::Environment::current());

        enum class Kind { Save, State };

        // Whether a file name is one of the game's: exactly <stem> followed by a
        // save suffix (.srm, .sav, .rtc, .ldci, .eep, .sra, .fla, .mpk, .dsv,
        // .mcr, .<n>.mcr) or a state suffix (.state, .state<n>, .state.auto,
        // any of those + .png, .bsv, .replay, .replay<n>: replays go with the
        // states, as RetroArch writes them). "Sonic 2.srm" is not Sonic's. The
        // ending ignores case; the stem does only with ignoreCase (a file
        // system that ignores case, where RetroArch's sonic.srm and Sonic.srm
        // are one file).
        bool belongsTo(const std::string& fileName, const std::string& stem, Kind& kind, bool ignoreCase = false);

        // Whether the file system holding folder ignores case: always on
        // Windows, else tested by looking the folder up with its case swapped
        bool ignoresCase(const std::string& folder);

        struct File
        {
            std::string path;
            std::string name;
            Kind kind = Kind::Save;
            std::uintmax_t size = 0;
        };
        // The game's files now, saves first, by name. Reads the folders.
        std::vector<File> list(const Folders& folders);

        struct Backup
        {
            std::string id;             // the folder name: 20261024-101530 (-2 ...)
            std::string folder;
            std::string when;           // "2026-10-24 10:15:30"
            std::string reason;         // "Backed up by hand", "Before restoring ...", "Before deleting"
            std::vector<File> files;    // saves/ and states/ inside it
            std::uintmax_t size = 0;
        };
        // <root>/manage/saves/<collection>/<game>; "" when either name can't be a folder name there
        std::string backupRoot(const std::string& root, const std::string& collection, const std::string& game);
        // Why a game's backups can't be kept (its or its system's name isn't a
        // usable folder name: a path, a trailing dot or space, a Windows device
        // name such as con or com1), or ""
        std::string backupProblem(const std::string& collection, const std::string& game);
        // Newest first
        std::vector<Backup> backups(const std::string& root, const std::string& collection, const std::string& game);
        bool findBackup(const std::string& root, const std::string& collection, const std::string& game,
                        const std::string& id, Backup& backup);

        // Copies the game's files into a new backup; id gets its name. Fails
        // when there's nothing to back up.
        EditResult backUp(const std::string& root, const std::string& collection, const std::string& game,
                          const Folders& folders, const std::string& reason, std::string* id = nullptr);
        // Puts a backup's files back: the game's current files are backed up
        // first, then removed, then the backup's copied in, so the game ends up
        // with exactly the backup's files. Every file must be the game's.
        EditResult restore(const std::string& root, const std::string& collection, const std::string& game,
                           const Folders& folders, const std::string& id);
        // Deletes the game's files, backed up first
        EditResult removeAll(const std::string& root, const std::string& collection, const std::string& game,
                             const Folders& folders);
        // Deletes one backup
        EditResult removeBackup(const std::string& root, const std::string& collection, const std::string& game,
                                const std::string& id);

        // "12 KB", "3.4 MB"
        std::string sizeText(std::uintmax_t bytes);
    }
}

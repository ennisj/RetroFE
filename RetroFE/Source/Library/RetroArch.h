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

// RetroArch settings for the Manage screens: which launchers run RetroArch,
// the core they load and the folder RetroArch keeps that core's settings in,
// and a few curated settings per game or per core, written the way RetroArch
// reads them:
//
//   RetroArch     <config>/<Core>/<rom>.cfg    only the changed keys; stacks on
//                                              <Core>.cfg (and <folder>.cfg) and retroarch.cfg
//   core options  <config>/<Core>/<rom>.opt    one file wins: a game's starts as a copy of
//                                              the folder's or the core's .opt
//   controls      <remaps>/<Core>/<rom>.rmp    one file wins, the same way
//
// <Core> is the core's library_name (the folder RetroArch uses), from a
// hand-checked table, else the core's .info file (flagged when RetroArch's
// own folder doesn't confirm it). Core-wide files (rom = "") are the
// <Core>.cfg / .opt / .rmp themselves. See docs/design/manage-entities.md
// ("RetroArch settings") and docs/design/manage-library.md.
// No SDL: built into retrofe_tests too.

#include "Edits.h"
#include "Index.h"
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Library
{
    class Backups;

    namespace RetroArch
    {
        // Where the platform keeps RetroArch's settings when its program
        // folder has none. Tests pass their own.
        struct Environment
        {
            std::string os;          // "windows", "linux" or "apple"
            std::string appData;     // %APPDATA%
            std::string home;        // %USERPROFILE% / $HOME
            std::string xdgConfig;   // $XDG_CONFIG_HOME
            static Environment current();
        };

        // A launcher's arguments split as a command line would split them (quotes removed)
        std::vector<std::string> splitArguments(const std::string& arguments);

        // True when a launcher runs RetroArch: its program is retroarch(.exe),
        // its arguments load a core (-L / --libretro), or it hands RetroArch a
        // *_libretro core through another program (flatpak, a script). No disk access.
        bool runsRetroArch(const LauncherDef& launcher);

        // "cores/genesis_plus_gx_libretro.dll" -> "genesis_plus_gx"
        std::string coreStem(const std::string& coreArgument);

        // The core's library_name (its settings folder) from the hand-checked
        // table, or "" when the table doesn't know the core
        std::string libraryForStem(const std::string& stem);

        enum class State
        {
            NotRetroArch,   // the launcher runs another program
            NotFound,       // RetroArch's program or its retroarch.cfg isn't there
            CoreUnknown,    // no core, or one whose settings folder can't be worked out
            Ready
        };

        // A launcher, read as RetroArch reads its setup. Reads the disk
        // (retroarch.cfg, the core's .info): call it when a screen opens.
        struct Setup
        {
            State state = State::NotRetroArch;
            std::string why;               // when not Ready: a sentence for the screen
            std::string launcher;          // the launcher's id
            std::string program;           // RetroArch's program ("" when another program starts it)
            std::string workingFolder;     // where RetroFE starts it
            std::string coreArgument;      // as the launcher writes it
            std::string coreFile;          // found on disk ("" when not found)
            std::string coreStem;          // "genesis_plus_gx"
            std::string library;           // "Genesis Plus GX"
            bool libraryCertain = false;   // from the table, or confirmed by RetroArch's folder of that name
            std::string librarySource;     // where the name came from, as a phrase
            std::string configFile;        // retroarch.cfg
            std::string configDir;         // rgui_config_directory
            std::string remapDir;          // input_remapping_directory
            std::string globalOptionsFile; // core_options_path, else retroarch-core-options.cfg
            bool globalCoreOptions = false;   // global_core_options = true: one options file for every core
            bool overrides = true;            // auto_overrides_enable
            bool gameOptions = true;          // game_specific_options
            bool remaps = true;               // auto_remaps_enable
            bool passesName = false;          // the launcher passes %ITEM_NAME% rather than the file
            std::vector<std::string> notes;   // anything else worth saying

            bool ready() const { return state == State::Ready; }
            // Launchers with the same key share every core-wide file
            std::string coreKey() const { return configDir + "|" + library; }
        };
        Setup detect(const std::string& root, const LauncherDef& launcher,
                     const Environment& env = Environment::current());

        enum class Tile { Settings, CoreOptions, Controls };

        // Whose files: a game's (rom set) or the core's own (rom "")
        struct Target
        {
            Setup setup;
            std::string rom;              // the name RetroArch gives the game's files
            std::string contentFolder;    // the name of the folder the game's file is in

            bool game() const { return !rom.empty(); }
        };
        // A game's files: named after its file without the extension (RetroArch's
        // rule), or after its name when the launcher passes %ITEM_NAME%
        Target gameTarget(const Setup& setup, const GameRecord& game);
        Target coreTarget(const Setup& setup);

        struct TileFiles
        {
            std::string own;      // the file this tile writes
            std::string below;    // core options / controls: the one file that applies without own
                                  // (the folder's, else the core's, if it exists); "" none
            std::vector<std::string> layers;  // RetroArch settings: what stacks under own, lowest first (existing files)
        };
        TileFiles files(const Target& target, Tile tile);

        // A curated setting: a row of fixed choices, each setting one or more keys
        struct Choice
        {
            std::string label;
            std::vector<std::pair<std::string, std::string>> assign;   // key = value
        };
        struct RowDef
        {
            std::string id;           // "aspect", "btn_a", or the core option's key
            std::string label;
            std::vector<Choice> choices;
        };
        const std::vector<RowDef>& settingsRows();
        const std::vector<RowDef>& controlRows();
        // The curated options of a core (by library name); empty when there are none
        std::vector<RowDef> coreOptionRows(const std::string& library);
        std::vector<RowDef> rowDefs(const Target& target, Tile tile);
        // Every core the option table covers
        std::vector<std::string> coresWithOptions();

        struct Row
        {
            std::string id;
            std::string label;
            std::vector<std::string> choices;   // [0] = "Default (<what applies without this file>)"
            int current = 0;                    // index into choices; -1 = a value no choice names
            std::string currentText;            // the choice's label, or the raw value
            bool own = false;                   // this tile's file sets it
            bool differs = false;               // and it's not what applies without the file
        };
        struct TileState
        {
            TileFiles files;
            std::vector<Row> rows;
            bool ownExists = false;
            bool canRestore = false;            // an original is kept and the file differs from it
            std::string disabledNote;           // RetroArch is set not to read these files
        };
        TileState read(const Target& target, Tile tile, const Backups& backups);

        // Writes the tile's file: a choice per row id (0 = Default). Only the
        // curated keys and values are written. A file left with nothing, or
        // equal to what it shadows, is removed. Backed up before the first change.
        EditResult apply(Backups& backups, const Target& target, Tile tile, const std::map<std::string, int>& choices);
        // A game gives up its own file for this tile, so it follows the core's again
        EditResult useCoreSettings(Backups& backups, const Target& target, Tile tile);
        // Puts back the file as it was before Manage first changed it
        EditResult restoreOriginal(Backups& backups, const Target& target, Tile tile);

        // RetroArch's key = "value" files: the first line for a key wins
        std::map<std::string, std::string> parseConfig(const std::string& text);
        // Sets (value) or removes (nullopt) keys, keeping every other line as it was
        std::string editConfig(const std::string& text, const std::map<std::string, std::optional<std::string>>& edits);
        // Equal as RetroArch would read them: the same text ignoring case, or the same number
        bool sameValue(const std::string& a, const std::string& b);
    }
}

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

// The changes the Manage screens make, each one small, validated, written
// atomically, with the original kept under <root>/manage/backups before its
// first change. They take the Index the screen is showing for validation;
// none of them updates it: after a change, call Indexer::changed() (which
// rebuilds the index in the background and marks "changes pending").
// RetroFE itself only sees changes after it reloads.
//
// Never written here: list.path or any other path key of a collection, and
// menu lines are only added, removed or moved one at a time. A launcher's
// executable, arguments, currentDirectory and reboot are written only by
// addLauncher (a new file, whole) and editLauncher (the Emulators edit form,
// validated there and here, in place).

#include "Backups.h"
#include "Index.h"
#include "Media.h"
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Library
{
    struct EditResult
    {
        bool ok = false;
        std::string message;               // a sentence for the screen, success or failure
        std::vector<std::string> files;    // files written or removed

        static EditResult fail(const std::string& message) { return EditResult{ false, message, {} }; }
    };

    class Editor
    {
    public:
        explicit Editor(const std::string& root);

        Backups& backups() { return backups_; }

        // --- Games -------------------------------------------------------------
        // Starts a game with another launcher: collections/<c>/launchers/<game>.conf
        // ("" or the collection's own launcher removes the file). collection is
        // the game's own (source) collection.
        EditResult setGameLauncher(const Index& index, const std::string& collection, const std::string& game,
                                   const std::string& launcher);

        // Metadata, by field (hyperlist element: description, year, genre, ...; see
        // MetadataOverrides::fields()). Kept in manage/metadata/<type>.xml and
        // written into meta.db straight away.
        EditResult setMetadata(const Index& index, const std::string& collection, const std::string& game,
                               const std::string& field, const std::string& value);
        // Back to the shipped value (drops the override)
        EditResult clearMetadata(const Index& index, const std::string& collection, const std::string& game,
                                 const std::string& field);
        // The override values for a game ("" type = the collection's metadata.type)
        std::map<std::string, std::string> metadataOverrides(const Index& index, const std::string& collection,
                                                             const std::string& game) const;

        // Hides a game of a system from RetroFE's lists, or shows it again, with
        // the system's exclude list (exclude.txt, or the file list.excludeFile
        // names, LB12). game is the item name; the lines are its files' names
        // without the extension, exactly as spelt (what RetroFE matches), or the
        // name itself for a listed game with no file. Hiding needs a game shown
        // now; showing removes every line naming it.
        EditResult setGameHidden(const Index& index, const std::string& collection, const std::string& game, bool hidden);

        // --- Collection settings ----------------------------------------------------
        // Keys a screen may change, and nothing else
        static const std::vector<std::string>& editableSettings();   // settings.conf keys
        static const std::vector<std::string>& editableInfo();       // info.conf keys
        // Empty when value is valid for key, otherwise why not
        static std::string checkSetting(const Index& index, const std::string& collection, const std::string& key,
                                        const std::string& value);

        // Writes key where its effective value comes from (settings*.conf or
        // info.conf, which wins at load), else settings.conf / info.conf
        EditResult setCollectionSetting(const Index& index, const std::string& collection, const std::string& key,
                                        const std::string& value);
        EditResult removeCollectionSetting(const Index& index, const std::string& collection, const std::string& key);

        // --- Menus ------------------------------------------------------------------
        // A menu lists children in menu.txt, or as menu/<child>.txt files when it
        // has a menu folder and no menu.txt (RetroFE then sorts them by name).
        // OneSauce's menu_supported.txt, if present, is kept in step.
        EditResult addMenuChild(const Index& index, const std::string& menu, const std::string& child, int position = -1);
        EditResult removeMenuChild(const Index& index, const std::string& menu, const std::string& child);
        EditResult moveMenuChild(const Index& index, const std::string& menu, const std::string& child, int position);

        // New collections that hold no games of their own, listed under parent
        // (a menu, or the top menu even before it exists; never a collection of
        // games, LB9). Both write settings.conf (list.menuSort = yes) and
        // info.conf (title = name); a menu gets an empty menu.txt, a merged
        // collection an empty <source>.sub (every game of that system).
        // Empty when parent may get menu entries (a menu.txt or menu/ menu, a
        // collection with no games, entries or sources yet, or the top menu
        // before its folder exists), otherwise why not
        static std::string checkMenuParent(const Index& index, const std::string& parent);
        // Naming the top menu itself (when it doesn't exist yet) makes it, listed nowhere.
        // A failure after writing undoes what was written; files lists what was touched.
        EditResult createMenu(const Index& index, const std::string& name, const std::string& parent);
        EditResult createMerged(const Index& index, const std::string& name, const std::string& parent,
                                const std::string& source);
        // A merged collection's sources: adding writes an empty <source>.sub
        // (every game), also to a collection with nothing yet; removing takes
        // the .sub out (kept in the backups); respelling renames a .sub spelt
        // differently from its system's folder, keeping its lines
        EditResult addSubSource(const Index& index, const std::string& merged, const std::string& source);
        EditResult removeSubSource(const Index& index, const std::string& merged, const std::string& source);
        EditResult respellSubSource(const Index& index, const std::string& merged, const std::string& source);
        // Deletes a menu, merged collection or one with nothing yet: no games,
        // only settings, info, menu files, .sub files, artwork, lastplayed and
        // an empty favorites list. Its entries move up into each menu that
        // listed it (in its place); it's taken out of those menus; every file
        // is moved to the backups. The top menu, reserved names and anything
        // listed by a menu.xml or collectionLaunchers menu can't go.
        // checkDeleteMenu says why not, or "". A failure part way says which
        // files were touched (EditResult::files).
        static std::string checkDeleteMenu(const Index& index, const std::string& name);
        EditResult deleteMenu(const Index& index, const std::string& name);

        // --- Playlists and merged collections ------------------------------------------
        // A playlist of collection (playlists/<name>.txt). The game is named by its
        // own collection; entries for another collection's game are "_<C>:<name>".
        EditResult addToPlaylist(const Index& index, const std::string& collection, const std::string& playlist,
                                 const std::string& gameCollection, const std::string& game);
        EditResult removeFromPlaylist(const Index& index, const std::string& collection, const std::string& playlist,
                                      const std::string& gameCollection, const std::string& game);

        // Whole playlists of a collection. Names RetroFE gives a meaning (all,
        // favorites/favourites, lastplayed) are refused. The folder is read
        // afresh, so a playlist made since the index was built counts. A rename
        // may change only the case; a deleted or renamed file's original is
        // kept in the backups.
        static bool isReservedPlaylist(const std::string& name);
        EditResult createPlaylist(const Index& index, const std::string& collection, const std::string& playlist);
        EditResult renamePlaylist(const Index& index, const std::string& collection, const std::string& playlist,
                                  const std::string& newName);
        EditResult deletePlaylist(const Index& index, const std::string& collection, const std::string& playlist);

        // A merged collection's <source>.sub. An empty .sub shows every game of the
        // source, so adding to it is refused, and removing the last entry removes the file.
        EditResult addToSub(const Index& index, const std::string& merged, const std::string& source, const std::string& game);
        EditResult removeFromSub(const Index& index, const std::string& merged, const std::string& source, const std::string& game);

        // --- Media --------------------------------------------------------------------
        // Copies file into the slot's own folder as <basename>.<ext>, replacing
        // the slot's files of the same kind with any extension. Never writes into
        // a fallback's folder.
        EditResult replaceMedia(const MediaSlot& slot, const std::string& file);
        // Removes the slot's own file; refused when the slot only shows a fallback
        EditResult clearMedia(const MediaSlot& slot);
        // Writes a text slot's own <basename>.txt (a game's story, say) with
        // text as given. Refused for image and video slots and for empty text
        // (clearMedia removes the file).
        EditResult writeMediaText(const MediaSlot& slot, const std::string& text);

        // --- Launchers ---------------------------------------------------------------
        // A new launcher is written whole, as launchers.<os>/<name>.conf (os =
        // the index's). Existing launcher files are never changed: a name
        // that's taken (in any case) is refused.
        struct NewLauncher
        {
            std::string name;
            std::string executable;
            std::string arguments;          // may be empty
            std::string currentDirectory;   // empty: RetroFE uses the executable's folder
            bool reboot = false;            // reboot = yes: RetroFE restarts after the program exits
        };
        // Empty when name can be a new launcher's name, otherwise why not:
        // the rules for the name itself (a plain-ASCII file name, no = , ; %,
        // not a Windows device name such as con or aux.v2) ...
        static std::string checkLauncherName(const std::string& name);
        // ... and no clash, ignoring case, with any launcher's id in any scope
        // (a collection or local launcher of that name would be found first)
        // or with a "<collection>.<x>" local launcher name
        static std::string checkNewLauncherName(const Index& index, const std::string& name);
        EditResult addLauncher(const Index& index, const NewLauncher& launcher);
        // Removes a launcher's file, only when no system (default or
        // alternative) and no game uses it. The original is kept in the backups.
        EditResult removeLauncher(const Index& index, LauncherScope scope, const std::string& id);

        // Changes an existing launcher file in place: only the fields set, as
        // executable / arguments / currentDirectory / reboot lines, keeping
        // every other line, comment and line ending (ConfFile). An empty
        // arguments or currentDirectory, or reboot = false, removes the line.
        // currentDirectory and reboot are refused for collection and local
        // launchers (RetroFE reads them for global launchers only). Values
        // are one line without '#'. The original is kept in the backups.
        struct LauncherChange
        {
            std::optional<std::string> executable;
            std::optional<std::string> arguments;
            std::optional<std::string> currentDirectory;
            std::optional<bool> reboot;
            bool empty() const { return !executable && !arguments && !currentDirectory && !reboot; }
        };
        EditResult editLauncher(const Index& index, LauncherScope scope, const std::string& id, const LauncherChange& change);

        // Renaming a global launcher: its file in every launchers.<os> folder
        // (windows, linux, apple: a system's "launcher =" is shared by all of
        // them), the "launcher =" lines of collections' settings*.conf /
        // info.conf naming it (defaults and alternatives), and the first line
        // of every game's launcher file that names it (unless its collection
        // has a local launcher of that name, which a game's pick means). The
        // plan lists each change as a sentence for a preview; problem says why
        // it can't be done (a name that's taken, a reference set outside a
        // collection's folder...). A failure part way puts back what was written.
        struct RenamePlan
        {
            std::string problem;
            std::vector<std::string> changes;   // for the screen
            std::vector<std::pair<std::string, std::string>> renames;   // old file -> new file
            std::vector<std::string> settingsFiles;   // files whose launcher = line changes
            std::vector<std::string> gameFiles;       // collections/<c>/launchers/<game>.conf
        };
        // The part that doesn't depend on the new name (reads every
        // collection's settings files): a screen keeps it per launcher and
        // snapshot and plans each name typed from it
        struct RenameScan
        {
            struct Setting
            {
                std::string collection, fileName, path, value;
            };
            std::string id;
            std::string problem;
            std::vector<std::pair<std::string, std::string>> files;   // ("launchers.<os>", folder) holding its file
            std::vector<std::pair<std::string, std::string>> taken;   // ("launchers.<os>", file name) of every file there
            std::vector<Setting> settings;
            std::vector<std::pair<std::string, std::string>> games;   // (collection, launcher file)
            std::vector<std::string> gameNames;
        };
        RenameScan scanLauncherRename(const Index& index, const std::string& id) const;
        RenamePlan planLauncherRename(const Index& index, const RenameScan& scan, const std::string& newName) const;
        RenamePlan planLauncherRename(const Index& index, const std::string& id, const std::string& newName) const;
        EditResult renameLauncher(const Index& index, const std::string& id, const std::string& newName);

        // Puts a file changed here back as it was before the first change
        EditResult restoreOriginal(const std::string& file);

        // --- Controls (ControlEdits.cpp) ------------------------------------------------
        // controls.conf, then controls1..9.conf, as RetroFE loads them (later files win)
        static std::vector<std::string> controlsFileNames();
        // Empty when changes (name -> new value) leave every required control
        // they change (ControlList: up, down, select, back) an input that
        // works, otherwise why not. Controls not changed aren't checked, so a
        // value left empty on purpose in the files doesn't block other saves.
        static std::string checkControls(const std::map<std::string, std::string>& changes);
        // Writes controls (ControlList names, or deadZone) where each value
        // comes from now, else to controls.conf (made from the built-in
        // controls when missing). Every token must parse (InputToken), and
        // the result must pass checkControls. Comments, order and line
        // endings are kept; originals go to the backups.
        EditResult setControls(const std::map<std::string, std::string>& values);

    private:
        // Makes RetroFE re-import meta/ (and the overrides) on its next start
        void requestMetadataImport();
        // createMenu ("" source) and createMerged
        EditResult createListed(const Index& index, const std::string& name, const std::string& parent,
                                const std::string& source);

        std::string root_;
        Backups backups_;
    };

    // Low-level menu helpers that need no Index (used by Setup): add or remove a
    // child of a menu, choosing menu.txt or the menu/ folder as described above.
    namespace MenuFiles
    {
        // firstCollection from the root's settings (as RetroFE reads it), default "Main"
        std::string topMenu(const std::string& root);

        // Where new collections get listed: firstCollection when it's a menu,
        // or could become one (no games of its own: no list.extensions,
        // list.path, roms folder or .sub files); otherwise "Main", so a games
        // collection never gets a menu.txt that would mix collections into its games
        std::string menuForNewCollections(const std::string& root, const std::string& firstCollection);

        // Empty when name can be a child entry (a collection folder name), otherwise why not
        std::string checkChildName(const std::string& name);

        // Adds child unless it's listed already (ignoring case). createSettings writes a
        // minimal settings.conf when the menu folder is new, as Setup always did.
        bool add(const std::string& root, const std::string& menu, const std::string& child, int position,
                 Backups* backups, std::string& error);
        // removed (if given) says whether the menu listed child at all
        bool remove(const std::string& root, const std::string& menu, const std::string& child,
                    Backups* backups, std::string& error, bool* removed = nullptr);
        bool move(const std::string& root, const std::string& menu, const std::string& child, int position,
                  Backups* backups, std::string& error);
        // Re-spells an entry as the folder is spelt (removes entry, adds folderName at its position)
        bool respell(const std::string& root, const std::string& menu, const std::string& entry,
                     const std::string& folderName, int position, Backups* backups, std::string& error);
    }
}

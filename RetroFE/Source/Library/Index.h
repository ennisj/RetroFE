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

// A read-only snapshot of a RetroFE install, built the way RetroFE reads it:
// collections (menus, systems, merged collections), their games and
// metadata, launchers, and layouts. Built by Library::Indexer on a worker
// thread; once built it never changes, so screens can share it freely.
// See docs/design/manage-library.md.

#include "Properties.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Library
{
    struct ScanOptions
    {
        std::string root;              // RetroFE's root (Configuration::absolutePath)
        std::string os;                // "windows", "linux" or "apple"; "" = this platform's
        // Leave empty to read them from the root's settings, as RetroFE does
        std::string baseItemPath;
        std::string baseMediaPath;
        std::string firstCollection;
        bool games = true;             // list every system's games (the slow part)
        bool metadata = true;          // read titles etc. from meta.db
    };

    // What a collection is; a collection can be several at once
    enum Kind : unsigned
    {
        KindMenu = 1,     // has menu.txt, a menu/ folder or menu.xml: lists other collections
        KindSystem = 2,   // has games of its own (list.path / list.extensions / a roms folder)
        KindMerged = 4,   // has <source>.sub files: shows other collections' games
    };

    enum class MenuSource { None, MenuTxt, MenuFolder, MenuXml, CollectionLaunchers };

    struct MenuEntry
    {
        std::string entry;        // the name as the menu lists it
        int collection = -1;      // the collection folder it opens, or -1 if there's none
        bool exactCase = true;    // false: the folder is spelt differently, so RetroFE finds the
                                  // folder but not its settings (they're keyed by the folder's spelling)
    };

    struct SubSource
    {
        std::string source;       // <source> from <source>.sub, as RetroFE uses it
        int collection = -1;      // the collection it names, or -1
        bool exactCase = true;    // false: spelt differently from the folder; RetroFE then finds no settings
        std::string file;
        std::vector<std::string> entries;  // empty = every game of the source
    };

    struct PlaylistInfo
    {
        std::string name;         // file stem
        std::string file;
        std::vector<std::string> entries;  // "name", "_<Collection>:name" or "*"
        bool reserved = false;    // favorites / lastplayed, which RetroFE itself writes
    };

    enum class LauncherScope { Global, Collection, Local };

    struct LauncherDef
    {
        std::string id;           // the name a launch looks it up by: <name> (global),
                                  // <collection> (collection launcher), <collection>.<name> (local)
        std::string name;         // file stem, or the collection for a collection launcher
        LauncherScope scope = LauncherScope::Global;
        std::string collection;   // owner of a collection or local launcher
        std::string file;
        std::string executable;
        std::string arguments;
        std::string currentDirectory;
        bool reboot = false;
    };

    enum class LauncherVia
    {
        None,            // no launcher named
        Setting,         // the collection's "launcher =" (settings.conf / info.conf)
        CollectionFile,  // the collection's own launcher.<os>.conf / launcher.conf, which wins over the setting
        GameOverride,    // collections/<c>/launchers/<game>.conf
    };

    // Which launcher a launch actually uses, worked out as Launcher::run does
    struct LauncherResolution
    {
        std::string configured;    // collections.<c>.launcher
        std::string overrideName;  // first line of the game's override file ("" if none)
        std::string overrideFile;
        std::string name;          // the launcher name Launcher::run ends up with
        LauncherVia via = LauncherVia::None;
        int launcher = -1;         // index into Index::launchers, -1 when unresolved
        std::string problem;       // a sentence when it won't launch, else ""
    };

    struct Metadata
    {
        std::string title, year, manufacturer, developer, genre, cloneOf;
        std::string players, ctrlType, buttons, joyWays, rating, score;
    };

    struct CollectionRecord
    {
        std::string name;          // folder name, exactly as on disk
        std::string folder;        // <root>/collections/<name>
        unsigned kinds = 0;        // Kind flags
        bool reserved = false;     // Home, Catalog or Favorites: names RetroFE gives a meaning
        std::string title;         // info.conf title, else the name
        std::map<std::string, std::string> info;      // info.conf keys
        std::vector<std::string> settingsFiles;       // settings*.conf and info.conf that exist

        // Menu
        MenuSource menuSource = MenuSource::None;
        std::vector<MenuEntry> children;              // in display order
        std::vector<int> parents;                     // menus listing this collection
        bool hasMenuSupported = false;                // OneSauce/RetroFE Plus menu_supported.txt

        // Merged
        std::vector<SubSource> subs;

        std::vector<PlaylistInfo> playlists;

        // System (what RetroFE reads for its own games)
        std::string listPath;                         // as resolved; ';' separates folders
        std::vector<std::string> listPaths;
        std::vector<std::string> extensions;          // as RetroFE splits list.extensions
        std::string metadataType;
        std::optional<bool> includeMissingItems;      // unset = not in the files
        bool romHierarchy = false;
        bool emuarc = false;
        bool menuSort = true;
        std::string layout;                           // collections.<c>.layout
        bool videos = false;                          // every extension is a video type
        std::vector<std::string> includeList;         // include.txt entries
        std::vector<std::string> excludeList;         // exclude.txt entries
        size_t gameBegin = 0;                         // its own games: Index::games[gameBegin, gameBegin + gameCount)
        size_t gameCount = 0;
        size_t caseMismatchCount = 0;                 // files whose extension matches only ignoring case
        std::vector<std::string> caseMismatches;      // a few of them, for messages
        LauncherResolution launcher;                  // for its games without an override
        // collections/<c>/launchers/<game>.conf files: game (file stem) -> first line
        // (the launcher name). An empty file has no first line, so RetroFE ignores it.
        std::map<std::string, std::optional<std::string>> launcherOverrides;

        // Things worth telling the user: fork-only keys, spelling problems, ...
        std::vector<std::string> notes;

        bool is(Kind kind) const { return (kinds & kind) != 0; }
    };

    struct GameRecord
    {
        int collection = -1;       // the system it belongs to (index into Index::collections)
        std::string name;          // file name without extension: the key for metadata, media, lists
        std::string file;          // the file found ("" for a listed-but-missing game)
        std::string directory;     // folder it was found in
        std::string title;         // metadata title, else the name
        bool missing = false;      // listed by include.txt with list.includeMissingItems, no file
        bool hasMetadata = false;
        Metadata meta;
        std::string searchText;    // lower-case name + title, for search
    };

    // A game as a page lists it (own game or via a .sub). game is -1 when the
    // entry has no GameRecord (e.g. a .sub names a game the system's own
    // include.txt hides).
    struct ItemRef
    {
        int game = -1;
        int collection = -1;       // the game's own (source) collection
        std::string name;
    };

    struct LauncherUse
    {
        int collection = -1;
        std::string game;          // "" = the collection's default
        LauncherVia via = LauncherVia::None;
    };

    struct LayoutRecord
    {
        std::string name;
        std::string folder;
        bool hasLayout = false;    // layout.xml
        bool hasSplash = false;    // splash.xml
        int width = 0;             // from layout.xml's <layout width= height=>
        int height = 0;
        bool active = false;       // the global "layout ="
        std::vector<std::string> extraLayouts;       // "layout - N.xml", "<WxH>layout.xml" and other pages
        std::vector<std::string> collectionLayouts;  // collections/<c>/layout/ folders
        std::vector<std::string> mediaCollections;   // collections/<c> with layout media
        std::vector<std::string> forms;              // forms/<id>.xml (this fork)
        std::vector<std::string> usedBy;             // collections whose "layout =" names it
    };

    struct MenuRow
    {
        int collection = -1;       // -1 when the entry names no folder
        std::string entry;
        int depth = 0;
        int parentRow = -1;
        bool cycle = false;        // already open further up: not expanded again
    };

    class Index
    {
    public:
        ScanOptions options;
        Properties properties;
        std::string topMenu;                       // firstCollection (Browse)
        std::string activeLayout;
        std::vector<CollectionRecord> collections; // sorted by name, ignoring case
        std::vector<GameRecord> games;             // grouped by collection, each group sorted by title
        std::vector<LauncherDef> launchers;        // global, then collection and local ones
        std::vector<LauncherUse> launcherUses;
        std::vector<std::string> unresolvedLaunchers;  // names a collection or game asks for that don't exist
        std::vector<LayoutRecord> layouts;
        std::vector<std::string> problems;         // anything that stopped part of the scan
        double seconds = 0;                        // how long the build took

        // Exact spelling first, then ignoring case; -1 if none
        int findCollection(const std::string& name) const;
        int findGame(const std::string& collection, const std::string& name) const;
        int findLauncher(const std::string& id) const;
        int findLayout(const std::string& name) const;

        // The games a collection's page lists, in RetroFE's order before sorting:
        // its own games, then each .sub source's (filtered by the .sub)
        std::vector<ItemRef> itemsOf(int collection) const;

        // The menu tree from a collection (default: topMenu), depth first, cycles marked
        std::vector<MenuRow> menuTree(const std::string& from = "") const;

        // For filters: which games are under a collection. A system has its own
        // games, a merged collection its sources', a menu everything below it.
        std::vector<char> gameMask(int collection) const;

        // Which launcher a collection's games (game "") or one game would use
        LauncherResolution resolveLauncher(int collection, const std::string& game = "") const;

        // Who uses a launcher (by index into launchers)
        std::vector<LauncherUse> usesOf(int launcher) const;

        // Merged collections whose page shows a game, and playlists listing it
        std::vector<int> mergedCollectionsShowing(int game) const;
        struct PlaylistRef
        {
            int collection = -1;
            std::string playlist;
        };
        std::vector<PlaylistRef> playlistsListing(int game) const;

        // Called by the builder
        struct Candidate
        {
            std::string name;
            std::string file;
            std::string directory;
        };
        std::vector<std::vector<Candidate>> candidates;  // per collection: files matching its extensions
        std::map<std::string, int> collectionByName;     // exact folder name -> index
        std::map<std::string, int> collectionByLower;    // lower-case name -> index
        std::map<std::pair<int, std::string>, int> gameByName;  // (collection, name) -> game
        std::map<std::string, int> launcherById;          // "<scope>:<id>" -> index

        int findLauncher(LauncherScope scope, const std::string& id) const;

    private:
        std::vector<ItemRef> itemsFromSource(int source, int merged, const SubSource* sub) const;
    };

    // Builds an Index. progress gets 0..1 and a short phase name; cancel stops it early.
    struct BuildCallbacks
    {
        std::function<void(float, const std::string&)> progress;
        const std::atomic<bool>* cancel = nullptr;
    };
    // nullptr when cancelled
    std::unique_ptr<Index> buildIndex(const ScanOptions& options, const BuildCallbacks& callbacks = {});

    // Names RetroFE treats specially, which Manage shouldn't offer as ordinary collections
    bool isReservedCollection(const std::string& name);

    // Media and video file extensions RetroFE plays (lower case)
    const std::vector<std::string>& videoExtensions();
    const std::vector<std::string>& imageExtensions();
}

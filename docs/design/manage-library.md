# Design: the Manage library (data layer)

Status: round 1, built. The plan is in [manage-entities.md](manage-entities.md);
decisions are summarised in [../DECISIONS.md](../DECISIONS.md) (LB1–LB12) and
argued in full at the end of this page.

`RetroFE/Source/Library/` (namespace `Library`) reads a RetroFE install the
way RetroFE reads it and writes changes the way RetroFE expects them. The
Games, Menus, Systems, Videos, Emulators and Themes screens are built on it.
It has no SDL or `Configuration` dependency: it takes the root folder and
reads everything itself, so it runs on a worker thread and in the unit tests.

| File | What it gives you |
|------|-------------------|
| `Indexer.h` | Builds `Index` snapshots on a worker: progress, cancel, "changes pending" |
| `Index.h` | The snapshot: collections, games, launchers, layouts; lookups and resolution |
| `Search.h` | Game and collection search with filters; a per-screen cache that's free to call every frame |
| `Media.h` | Where a game's, collection's or menu entry's media are, and what RetroFE would show |
| `Edits.h` | Every write a screen may make (`Editor`), plus `MenuFiles` for Setup |
| `MetadataOverrides.h` | The user metadata layer (`manage/metadata/*.xml`) and its fields |
| `ConfFile.h`, `LineList.h`, `Files.h`, `Backups.h` | Editing primitives: conf files, one-entry-per-line lists, atomic writes, originals |
| `Properties.h` | RetroFE's start-up key/value map, rebuilt without the singleton |
| `RetroArch.h` | Which launchers run RetroArch, their core's settings folder, and the curated per-game / per-core RetroArch settings, core options and controls (see [manage-entities.md](manage-entities.md#retroarch-settings)) |
| `Saves.h` | A game's save files and save states (where RetroArch keeps them, which files are the game's) and their backups under `manage/saves/` (see [manage-entities.md](manage-entities.md#saves)) |

## Threading and lifetime

- Create one `Library::Indexer` for the Manage hub, with `ScanOptions{ root = Configuration::absolutePath }`,
  and call `rebuild()` when Manage opens. It builds on its own thread.
- `indexer.status()` gives `state` (Idle, Building, Ready, Cancelled, Failed),
  `progress` (0..1), a `phase` ("Listing MAME"), `generation`, which
  increases with every new snapshot, and `error` when a build failed. Show the
  progress while there's no index yet.
- `rebuild()` and `changed()` never cancel a running build. The running build
  finishes and publishes, then exactly one more build runs and picks up every
  change made meanwhile. A burst of edits therefore still produces snapshots;
  it can't keep restarting a build that never finishes. Only the destructor and
  `cancel()` stop a build.
- `indexer.index()` returns a `std::shared_ptr<const Index>`. A snapshot never
  changes after it's published, so read it without locks. Hold the pointer for as long as a screen
  shows its data. A rebuild publishes a new snapshot and leaves yours alone.
- Give each screen its own `Library::Search` and call
  `search.games(indexer.index(), query)` every frame. It recomputes only when the
  query or the snapshot changes. On OneSauce-sized data, a full substring search
  over 30,817 games takes about 2.5 ms and a cached call about 0.0003 ms.
- Index positions (`int` game or collection numbers) are valid **only for the
  snapshot they came from**. Screen parameters should hold names
  (`collection`, `game`) and look them up again with `findCollection`, `findGame`.
- Games a system's exclude list hides are in `index->hiddenGames` (found with
  `findHiddenGame`), with their metadata, never in `games`, `findGame`,
  `itemsOf` or a search. Manage's "Only hidden games" lists them.
- `Editor` calls run on the calling thread. They touch one or two small files.
  The exception is `replaceMedia` with a large video, which copies the file;
  call it from a worker if that matters.

### What's safe to call every frame

| Per frame: fine | Once per screen or selection change |
|-----------------|-------------------------------------|
| `Search::games` / `Search::collections`, which are cached | `index->menuTree()`, which walks the tree |
| `findCollection`, `findGame`, `findHiddenGame`, `findLauncher`, `findLayout` | `index->gameMask()`, which is one pass over all games (Search caches it per filter) |
| `itemsOf`, `mergedCollectionsShowing`, `playlistsListing`, which are precomputed and return references | `index->usesOf()`, a filter over every use |
| `resolveLauncher`, which is a few map lookups | `gameMedia` / `collectionMedia` / `menuEntryMedia` (see below) |
| Reading any record field | `MetadataOverrides::shipped`, `Editor::metadataOverrides`, `ConfFile(...).load()`, all of which read files |

`gameMedia` is **not** per-frame safe. A game's slots cost up to
types × names × folders × extensions file checks: up to 2–4 thousand stat
calls for every slot of a game with a title and a clone. That's slow on
exFAT/USB drives and cold disks (330–510 ms measured on K: cold). Look media up
when the selection changes, keep the `MediaSlot`s, and refresh them after a
media edit.

Timings on the OneSauce-shaped data on K: (146 collections, 30,201 games once
OneSauce's include and exclude files are honoured, 12 MB meta.db, read only):

| Measurement | Time |
|-------------|------|
| First build, cold disk cache | 5–10 s |
| Builds after that | 0.5–0.9 s |
| Everything under `Main` (menu filter mask) | 0.2 ms |
| All 19 media slots of one game | 2–40 ms warm, 330–510 ms cold |

## Reading: what each screen calls

### Games
- **List:** `Search::games` with a `GameQuery`:
  - `text`: a case-insensitive substring of the name or title;
  - `collection`: a system, a merged collection, or a menu, which covers everything below it;
  - `includeMissing`;
  - `onlyWithoutMetadata`;
  - `offset` and `limit`.

  It returns `rows` (indexes into `index->games`) and `total`. Games are grouped
  by collection and sorted by title.
- **Details:** a `GameRecord` has:
  - `collection`, the system it belongs to;
  - `name`, the key for everything;
  - `file` and `directory`;
  - `title`, which is the metadata title or else the name;
  - `missing` and `hasMetadata`;
  - `meta`: year, manufacturer, developer, genre, cloneOf, players, ctrlType,
    buttons, joyWays, rating, score.
- **Where it appears:** `index->mergedCollectionsShowing(game)` and `index->playlistsListing(game)`.
- **Hidden games** (named by their system's exclude list) aren't `GameRecord`s.
  They're what the scan would list without the exclude list and doesn't:
  with `includeMissingItems`, `includeList` names that `excludeList` has
  (with or without a file); otherwise, or with no include list, the
  system's `index->candidates[c]` whose `basename` passes the include list
  and is in `excludeList`. The Games provider builds that list once per index.
- **Launcher:** `index->resolveLauncher(game.collection, game.name)` returns a `LauncherResolution`:
  - `name`, the launcher Launcher::run ends with;
  - `via`: Setting, CollectionFile or GameOverride;
  - `overrideName` and `overrideFile`;
  - `launcher`, an index into `index->launchers`, or -1;
  - `problem`, a sentence, or "".

  The choices to offer are `index->launchers`: global ones, the game's
  collection's own launcher, and its local launchers.
- **Metadata:** `MetadataOverrides::fields()` lists the editable fields
  (element, column and label). For each field, show three things:
  - the current value, from `GameRecord::meta`;
  - this user's override, from `Editor::metadataOverrides(index, collection, game)`;
  - the shipped value, from `MetadataOverrides::shipped(root, type, game, values)`.
    This parses that type's hyperlist files, so call it when the screen opens, not every frame.
- **Media:** `mediaTypes(index, collection)` lists the slot types.
  `gameMedia(index, game, type, defaultKind(type), pageCollection)` returns a `MediaSlot`, see [Media](#media).
  The same call with a `GameRecord` instead of its position works for a record
  outside `index.games`, such as one of `index.hiddenGames` (Games' screens
  for a hidden game keep its media).

### Menus
- **The tree:** `index->menuTree()` starts from `index->topMenu` (`firstCollection`, which is
  what Browse opens). It is depth first, in display order. Each row has:
  - `collection` (-1 for an entry with no folder);
  - `entry`;
  - `depth` and `parentRow`;
  - `cycle`, set when the collection already appears above it. A cycle row is not expanded.
- **A menu's `CollectionRecord`:**
  - `menuSource`: MenuTxt, MenuFolder (`menu/*.txt`, which RetroFE sorts by name), MenuXml or CollectionLaunchers;
  - `children`, as `MenuEntry{entry, collection, exactCase}`;
  - `parents`, the menus that list it;
  - `hasMenuSupported` (OneSauce);
  - `notes`.
- **Menus only:** `Search::collections` with `CollectionQuery{ kinds = KindMenu }`.
  Use `under = "<menu>"` for the collections below a menu.
- **Merged collections:** the `KindMerged` flag. `subs` are
  `SubSource{source, collection, exactCase, file, entries}`, and empty `entries`
  means every game of the source. Their games come from `index->itemsOf(collection)`.
- **Entry art:** `menuEntryMedia(index, menu, child, type, kind)`.

### Systems and Videos
- **Systems:** `Search::collections` with `kinds = KindSystem`.
  A collection can be a menu, a system and a merged collection at once.
- **Videos:** systems whose `videos` flag is set, meaning every `list.extensions`
  entry is a video type. RetroFE has no other notion of a video collection;
  see research §5 in [manage-entities.md](manage-entities.md).
- **The `CollectionRecord`:**
  - `title` and `info` (info.conf);
  - `settingsFiles`;
  - `listPaths` and `extensions`, split exactly as RetroFE splits them;
  - `metadataType`;
  - `includeMissingItems`, `romHierarchy`, `emuarc` and `menuSort`;
  - `layout`;
  - `includeFile` and `excludeFile`: `include.txt` and `exclude.txt`, or the files
    OneSauce's `list.includeFile` and `list.excludeFile` name (LB12);
  - `includeList` and `excludeList`, their entries;
  - `gameCount`;
  - `caseMismatchCount` and `caseMismatches`: files whose extension matches only
    if case is ignored, which RetroFE doesn't list;
  - `launcher`, the default `LauncherResolution`;
  - `launcherOverrides`;
  - `playlists`, each with `entries` and `reserved` (all, favorites and lastplayed, any case);
  - `cyclePlaylist` (the playlists RetroFE shows; empty = all with entries) and `cyclePlaylistOwn`;
  - `notes`, sentences for "Check this system".
- **Games, as the page lists them:** `index->itemsOf(collection)` returns its own
  games, then those from each `.sub`.
- **Art:** `collectionMedia(index, collection, type, kind)` reads `system_artwork/<type>.*`.

### Emulators and launchers
- **The launchers:** `index->launchers` holds a `LauncherDef` per launcher:
  - `id`, the name a launch looks up: `<name>`, `<collection>`, or `<collection>.<name>` for a local launcher;
  - `scope`: Global, Collection or Local;
  - `file`;
  - `executable`, `arguments`, `currentDirectory` and `reboot`.

  To show the conf verbatim, use `ConfFile(file).load()` and then `text()`.
- **Users:** `index->usesOf(launcher)` returns `LauncherUse{collection, game, via, launcher}`.
  An empty `game` means it's the collection's default.
- **Alternatives:** OneSauce writes `launcher = a, b, c`. RetroFE and the library
  use `a`. `LauncherResolution::alternatives` lists `b, c`, read-only (LB11).
- **Problems:** `index->unresolvedLaunchers` lists the names that something asks
  for but that don't exist. On Linux and macOS, that includes an override file
  whose first line ends in `\r`: `resolveLauncher` keeps the `\r` in `name` and
  says so in `problem`, because RetroFE's text-mode read keeps it there.
- **Platform:** `ScanOptions::os` picks which `launchers.<os>` folder to read.
  The default is this platform's. Set it to "linux" to inspect a cabinet's
  launchers from Windows.

### RetroArch
- `RetroArch::detect(root, launcher)` says whether a launcher runs RetroArch
  and finds its `retroarch.cfg`, folders and core (library name). It reads
  files: call it when a screen opens and keep the result per launcher and
  snapshot.
- `gameTarget(setup, game)` / `coreTarget(setup)` name whose files;
  `files(target, tile)` gives them; `read(target, tile, backups)` gives the
  curated rows with their choices and current values (a few small files).
- `apply`, `useCoreSettings` and `restoreOriginal` write through a `Backups`
  (the Editor's), with the rules in
  [manage-entities.md](manage-entities.md#retroarch-settings). None of these
  is per-frame safe; nothing in the index changes, so no `changed()`.
- `readConfigFile(path)`: a RetroArch file's values as RetroArch reads them
  (`#include` followed, first line wins).

### Saves
- `Saves::forGame(setup, game)` works out where RetroArch keeps a game's save
  files and states from `retroarch.cfg` (it reads it: call it when a screen
  opens, with the `RetroArch::detect` result kept per launcher). Not known for
  launchers that don't run RetroArch.
- `belongsTo(fileName, stem, kind, ignoreCase)` is the pure rule for "this
  file is the game's"; `list(folders)` reads the folders, ignoring the stem's
  case when `Folders::ignoreCase` (`ignoresCase(folder)`: Windows, or a folder
  found under its swapped case). `backupProblem(collection, game)` says why a
  name can't be a backup folder.
- `backUp`, `restore`, `removeAll`, `backups`, `findBackup`, `removeBackup`
  copy to and from `manage/saves/<collection>/<game>/<id>/` (`saves/`,
  `states/`, `backup.txt` with the reason). They read and write files; nothing
  in the index changes, so no `changed()`.

### Themes
`index->layouts` holds a `LayoutRecord` per layout:
- `name`;
- `hasLayout` and `hasSplash`;
- `width` and `height`, from `<layout>`;
- `active`;
- `extraLayouts`: `layout - N.xml`, `16x9layout.xml` and so on;
- `collectionLayouts`;
- `mediaCollections`;
- `forms`;
- `usedBy`: collections whose `layout =` names it.

### Media
A `MediaSlot` has these fields:
- `directory` and `basename`: the slot's own place. New files go there.
- `file`: what RetroFE would show, or "".
- `own`: true when `file` is the slot's own file, not a fallback.
- `fallback`: where a fallback comes from, for example "game's collection",
  "default" or "child's collection art".
- `probes`: every place looked, in RetroFE's order.

Folders follow `Configuration::getMediaPropertyAbsolutePath`. It tries
`collections.<c>.media.<type>`, then OneSauce's `media.basePath` (see LB4), then
`baseMediaPath/<c>/medium_artwork/<type>`. Collection art uses
`system_artwork` instead.

Lookup order, as in ReloadableMedia and ReloadableScrollingText:
- **A game:** for each of name, title, cloneOf and "default" (text has no
  default), RetroFE tries:
  1. the page collection's folder;
  2. the game's own collection's folder;
  3. the game's folder, with the type as the file name.

  On a merged page, the slot is the merged collection's folder and the source's art is a fallback.
- **A menu entry:** the menu's `medium_artwork/<type>/<child>`, then the child's
  `system_artwork/<type>`, then the same for "default". The
  `collections.<child>.<type>` rename is applied.

Matching:
- Images are gif, webp, png, jpg and jpeg. Video slots take mp4, avi, mkv, mp3,
  wav and flac. Text is `.txt`.
- On Windows, any letter case matches. Elsewhere, RetroFE's listed spellings are tried.

**Media folders** (fork compat, OneSauce; RetroFE's side is
`LayoutCompat::lookupMediaStep`, see ONESAUCE-COMPAT.md). At each step where
`<basename>.<ext>` is missing, a folder `<directory>/<basename>/` holding files
of the slot's kind (image or video extensions, any case, any names) stands for
it, and RetroFE shows one of its files at random. A file beside the folder
wins. Not for text slots, and not at a game's own ROM folder (step 3 above).
The slot then says:
- `folder` and `folderFiles`: the slot's **own** folder (its first place), when
  one exists, even when a file beside it wins or it holds nothing usable; its
  files in natural order (`LayoutCompat::naturalLess`), full paths;
- `shownFolderFiles` (`fromFolder()`): `file` is the first of a folder's files,
  standing for RetroFE's random pick; the folder is the slot's own (`own`) or
  a fallback's (a `default/`, the child's `system_artwork/video/`);
- `fileBesideFolder()`: the slot's own file wins over its own folder.

`mediaFolderFiles(directory, basename, kind)` lists a folder the way RetroFE
does; `mediaShownText` and `mediaFolderNote` are the screens' sentences ("a
folder of 9 files (one shown at random)", the file-wins warning). The folder
check is one `is_directory` per probe that found no file (and one for the
slot's own place), so a slot costs little more than before; a folder is
listed only when it exists. `findMediaFile` is unchanged: it still finds only
`<basename>.<ext>` files (Games' list thumbnails use it).

## Writing: `Library::Editor`

Create one `Editor(root)` and pass it the snapshot the screen shows, which it
uses for validation. Every call returns an `EditResult`:
- `ok`;
- `message`, a sentence to show;
- `files`, the files written or removed.

After a successful write, call `indexer.changed("what")`.

| Call | Writes | Rules |
|------|--------|-------|
| `setGameLauncher(index, collection, game, launcher)` | `collections/<c>/launchers/<game>.conf`, first line only, always ending in `\n` | The launcher must exist for that collection. Naming the collection's default, or passing "", removes the file, because Launcher::run treats them the same. `collection` is the game's own (source) collection. |
| `setGameHidden(index, collection, game, hidden)` | The system's exclude list: `CollectionRecord::excludeFile`, i.e. `exclude.txt` or the file `list.excludeFile` names (LB12) | `collection` must be a system. Lines name the game's files without their extension, exactly as spelt on disk (`Candidate::basename`, what ImportDirectory matches; with `list.emuarc` every file of the game's folder), or the name itself for a listed game with no file. Hiding needs a game the index lists and adds the lines that aren't there yet, in the file's own line-ending style; hiding a game that's hidden already is a no-op. Showing removes every line naming it and is refused when there's none. |
| `setMetadata(index, collection, game, field, value)` / `clearMetadata` | `manage/metadata/<metadata.type>.xml`, then meta.db straight away | A field from `MetadataOverrides::fields()`. One line, at most 200 bytes. A title can't be blank. Clearing puts back what RetroFE's import would give (see below). |
| `setCollectionSetting(index, collection, key, value)` / `removeCollectionSetting` | The settings*.conf or info.conf the effective value comes from. Otherwise settings.conf, or info.conf for info keys. | Settings keys: `list.extensions`, `list.menuSort`, `list.includeMissingItems`, `list.sortType`, `launcher`, `metadata.type`, `layout`, `cyclePlaylist`. Info keys: `title`, `manufacturer`, `developer`, `year`, `genre`, `type` (`videos` or empty: a video collection). A local launcher is `<collection>.<name>` here; the bare name only works in a game override. Each value is checked: extensions, yes/no, sort types, an existing launcher or layout. Setting `launcher` on an `a, b, c` list moves the pick to the front and keeps the rest. Remove takes the key out of every file. |
| `addMenuChild(index, menu, child, position)` / `removeMenuChild` / `moveMenuChild` | `menu.txt`. For a menu-folder menu with no menu.txt: `menu/<child>.txt`. Plus `menu_supported.txt` if present. | The child must be an existing collection; its folder spelling is used. Refused: Home or Catalog, the menu itself, a loop, a menu that `checkMenuParent` refuses (menu.xml, collectionLaunchers, a collection with games), reordering a folder menu, and removing a child that isn't listed. For a folder menu, the message says the position didn't apply. `MenuFiles::add` never writes a menu.txt beside a lone menu.xml (it would hide it). |
| `checkMenuParent(index, parent)` / `createMenu(index, name, parent)` / `createMerged(index, name, parent, source)` | `collections/<name>/settings.conf` (`list.menuSort = yes`), `info.conf` (`title = <name>`), and an empty `menu.txt` or `<Source>.sub`; then the parent's `menu.txt` (or `menu/<name>.txt`) | A new folder name (MenuFiles' rules: no device names such as `con`, no trailing dot or space; not reserved; unique ignoring case). The parent must be a menu.txt or menu/ menu, a collection with nothing yet (no games, entries or sources), or the top menu before its folder exists (LB9); `checkMenuParent` says why not. Naming the missing top menu itself makes it, listed nowhere. The source must be a system. New files get `.absent` markers; if listing fails, the new files are taken out again (to the backups), so a retry isn't blocked. |
| `addSubSource(index, merged, source)` / `removeSubSource` / `respellSubSource` | `collections/<merged>/<Source>.sub` | Adding writes an empty .sub (every game) to a merged collection or one with nothing yet (e.g. after its last .sub was removed); the source must be a system that isn't one yet. Removing moves the .sub to the backups, whatever it lists. Respelling renames a .sub spelt differently from its system's folder (RetroFE finds none of its games then), keeping its lines. |
| `checkDeleteMenu(index, name)` / `deleteMenu(index, name)` | Each parent menu's `menu.txt` (and `menu_supported.txt`), then every file of the collection, to the backups | Only menus, merged collections and collections with nothing yet, holding only settings*.conf, info.conf, menu.txt, menu/*.txt, menu_supported.txt, menu.xml, *.sub, system_artwork/, medium_artwork/, lastplayed.txt and an empty favorites.txt. Never the top menu, a reserved name, a system, or one a menu.xml or collectionLaunchers menu lists. Its entries move up into each parent in its place (skipping ones already there and loops); every line naming it goes, whatever its spelling. A failure part way lists the files touched. |
| `MenuFiles::respell(root, menu, entry, folderName, position, backups, error)` | The menu's line (or `menu/` file) and `menu_supported.txt` | Rewrites a wrongly-cased entry with the folder's spelling, in the same place. |
| `addToPlaylist(index, collection, playlist, gameCollection, game)` / `removeFromPlaylist` | `collections/<c>/playlists/<name>.txt` | A game of another collection is written as `_<C>:<name>`. The game must be on that page. `all` and `lastplayed` are refused. An entry covered by `*` can't be removed. |
| `createPlaylist(index, collection, name)` / `renamePlaylist(..., name, newName)` / `deletePlaylist` | `collections/<c>/playlists/<name>.txt` | `isReservedPlaylist` (= `isReservedPlaylistName`, also `PlaylistInfo::reserved`): `all`, `favorites` and `lastplayed`, any case, are refused. All the playlist calls find the file on disk, so they work before the index catches up. Names follow the file-name rules and must be new (ignoring case); a rename may change only the case. The folder is read afresh, not the index. A renamed or deleted file is kept in the backups. |
| `addToSub(index, merged, source, game)` / `removeFromSub` | `collections/<merged>/<source>.sub` | An empty .sub already shows everything, so adding is a no-op and removing one game is refused. Removing the last entry deletes the file. |
| `replaceMedia(slot, file)` / `clearMedia(slot)` | The slot's own folder, as `<basename>.<ext>`, with the extension lower-cased | The file kind must fit the slot. The slot's other files of that kind are moved aside. Clearing is refused when the slot only shows a fallback. Nothing is ever written into a fallback's folder. Neither ever touches a media folder: replacing puts the file beside the slot's folder (where it wins; the message says the folder stays), and clearing is refused when the slot is a folder (`fromFolder()`); clearing a file that wins over a folder says the folder shows now. |
| `addMediaFolderFile(slot, file)` / `removeMediaFolderFile(slot, name)` / `mediaFolderToFile(slot, name)` / `clearMediaFolder(slot)` | The slot's own media folder `<directory>/<basename>/`, only files of the slot's kind directly in it | Refused when the slot has no folder (or is text). Add copies the file in under its own name (refused when that name is there; the new file gets an `.absent` marker). Remove takes a name from the folder's listing (nothing else: no paths) to the backups. To-file copies that file beside the folder as `<basename>.<ext>` (as `replaceMedia`), then moves every file of the folder to the backups. Clear moves them all. An emptied folder is removed; other files in it (a README) stay, and so does the folder then. |
| `mediaFolderOriginals(slot)` | (reads) | The folder's files Manage changed that differ now (an added one still there, a removed one gone), from `manage/backups/.../<basename>/`, also once the folder is gone; Backups' numbered copies are skipped (`Backups::isNumberedCopy`: each backups folder lists the copies made in it in `.numbered`, so a real file named `x (2).mp4` is still put back). "Differs" is decided by size and modification time, reading both files only when the times differ, once per (size, times) (`differsFromOriginal`, also used by Games for the slot's own file); screens keep the result per slot rather than asking each frame. Restoring each, plus the single file's own originals, puts a folder back as it was. |
| `addLauncher(index, NewLauncher{name, executable, arguments, currentDirectory})` | A new `launchers.<os>/<name>.conf` (os = the index's), written whole | `checkLauncherName`: a plain-ASCII file name, no `= , ; %`, at most 64 characters, not a Windows device name (the part before the first dot: `con.txt`, `aux.v2`). `checkNewLauncherName` also refuses, ignoring case, any launcher's id in any scope (RetroFE looks up local, then collection, then global launchers, so the new one would never run) and `<collection>.<x>` names, and a file of that name on disk. Values are one line without `#`. An existing launcher file is never changed. |
| `removeLauncher(index, scope, id)` | Removes that launcher's file (global, collection or local) into the backups | Only when no system uses it as its default or an alternative and no game picks it. Refused for a collection's `launcher.<os>.conf` when a `launcher.conf` sits beside it, which would take over under the same name. |
| `editLauncher(index, scope, id, LauncherChange{executable, arguments, currentDirectory, reboot})` | That launcher's file, in place (`ConfFile`) | Only the fields set (each `optional`). Every other line, unknown key (`LEDBlinkyEmulator`), comment and line ending stays; a new key is appended in the file's style. Empty arguments or working folder, or reboot = false, removes the line. Working folder and reboot are refused for collection and local launchers (RetroFE reads them only as `launchers.<name>.*`). Values are one line without `#`; the screen checks the program exists and the `%VARIABLES%` are RetroFE's (`Emulators::checkLauncherValues`). |
| `scanLauncherRename(index, id)`, `planLauncherRename(index, scan or id, newName)` / `renameLauncher` | `launchers.<os>/<id>.conf` in every platform folder (windows, linux, apple) renamed to `<newName>.conf`, byte for byte; each collection's settings*.conf / info.conf `launcher =` line naming it (a token in an `a, b, c` list, the rest kept); the first line of each `collections/<c>/launchers/<game>.conf` naming it | Global launchers only; the new name passes `checkNewLauncherName` and no platform folder has that file; a case-only rename is refused. A game's pick of a name the collection also has as a local launcher means the local one and is left. A system whose effective `launcher` comes from a file outside its folder (`Properties::sourceFile`: the global settings' `collections.<c>.launcher`, which a collection's own files override) refuses the whole rename. The scan (every collection's settings files, the platform folders) doesn't depend on the name: keep it per launcher and snapshot, and plan each name from it; `renameLauncher` scans afresh. The plan's `changes` are sentences for a preview. Originals go to the backups; a failure part way writes back what each touched file held. |
| `writeMediaText(slot, text)` | A text slot's own `<basename>.txt` (a game's story), with the text as given | Text slots only; empty text is refused (use `clearMedia`). An existing own file keeps its spelling. |
| `restoreOriginal(file)` | Puts back the copy kept before the first change, or deletes a file that didn't exist then | |

**Clearing metadata.** `MetadataOverrides::shipped` reads the game's row the
way RetroFE's import builds it: `meta/hyperlist`, then `meta/mamelist`, then
`meta/emuarc`, each replacing the whole row. There are three outcomes:
- **Found:** that field's shipped value is written back.
- **Not found anywhere:** the row exists only because of the user's changes.
  It's deleted with the last of them; otherwise just that field is reset.
- **A shipped file can't be read:** meta.db is left alone and a
  `manage/metadata/.reimport` marker, newer than meta.db, makes RetroFE
  re-import on its next start. The message says so.

Safety rules that hold for all of these:
- **Never edited:** `list.path` and any other path key of a collection. A
  launcher's `executable`, `arguments`, `currentDirectory` and `reboot` are
  written only by `addLauncher` (a new file, whole) and `editLauncher` (the
  Emulators edit screen, validated). Menu lines are only ever added, removed or moved one at a time.
- **Atomic and durable:** writes go to a temp file. It's flushed to the disk
  (`FlushFileBuffers`, or `fsync`) and then renamed into place
  (`MOVEFILE_WRITE_THROUGH`, or rename plus an fsync of the folder). A power cut
  leaves the old file or the new one, never a short one.
- **Byte-for-byte:** every byte the edit doesn't change stays as it was. That
  includes comments, blank lines, spacing, a trailing comment on the edited
  line, the file's own line endings, and a missing final line break.
- **Originals:** before its first change, a file is copied to
  `<root>/manage/backups/<path relative to root>`. A file that didn't exist yet
  gets an `.absent` marker instead. Replaced media files are moved there, which
  is cheap for videos. Files outside the root go under `manage/backups/_external/<drive>/...`.
- **Nothing that differs is lost:** once an original is kept, a file is deleted
  or overwritten (by a later removal, or by a reset) only if it's identical to
  that original. Anything else is kept beside it first as `<name> (2).<ext>`,
  `(3)` and so on (listed in that folder's `.numbered`, so they're told apart from originals named like that). That covers a content update, a hand copy, or an earlier Manage change.

The editing primitives can be used directly for anything not covered above:
- `ConfFile`: one file, last-wins or first-wins;
- `ConfLayers`: a set of files where later ones win;
- `LineList`: entries read exactly as `ImportBasicList` reads them, untrimmed.

`SettingsFile`, used by the Settings form, is now a thin wrapper over `ConfLayers`.

## How changes take effect

RetroFE reads configuration, collections, metadata and media folder listings
once and caches them. Its media file caches (`Utils::findMatchingFile`'s
folder listings and the media-folder listings, `LayoutCompat::findMediaFolderFile`)
are cleared when it reloads (`main`'s loop, once the old `RetroFE` is gone),
so files added or removed in Manage show after the reload; before, a new
file name only showed after a restart. `%BASE_*_PATH%` is frozen at start-up. So:

- After a write, screens call `indexer.changed()`. Manage then shows the new
  state, and `indexer.changesPending()` is true.
- **When Manage closes with changes pending, reload RetroFE** (rebuild the
  pages and caches, or restart), then call `clearPendingChanges()`. Until then
  the frontend shows the old data.
- Metadata edits are written into meta.db at once, so a reload is enough; no
  re-import is needed. They also survive any later re-import (LB3).

## Faithfulness notes

These are modelled exactly:
- configuration import order (info.conf wins; `%ITEM_COLLECTION_NAME%` is
  replaced at import; launcher defaults are set);
- `list.path` with `;` separators and its defaults;
- untrimmed list entries;
- case-sensitive extension matching;
- include, exclude and `.sub` filtering, including how `.sub` lists join with
  the source's include.txt;
- `includeMissingItems`, with the merged collection's value first and the source's after;
- `romHierarchy` and `emuarc`;
- the menu.txt, menu/ and menu.xml order;
- launcher resolution: overrides, local launchers, a collection launcher file
  beating `launcher =`, and the executable lookup order. That includes the
  platform's line-ending behaviour when reading override files;
- metadata matched on the exact name per `metadata.type`. A busy meta.db (RetroFE
  importing) is retried, and other read errors go into `Index::problems`;
- emuarc: include and exclude match the file's basename, while the item is named after its folder.

These are simplified or not modelled:
- the `info/<item>.conf` media basename overrides;
- layout-mode and common-mode media (`layouts/<L>/collections/...`, `_common`);
- `randomSelect` art;
- `cyclePlaylist`;
- emuarc's duplicate items, which RetroFE can create for several files in one
  folder: they're collapsed to one per folder.

## Decisions

**LB1. A single conf editor, with SettingsFile built on it.**
- Options:
  - (a) copy SettingsFile's code;
  - (b) generalise it: one-file `ConfFile`, and multi-file `ConfLayers` with the
    effective source wins;
  - (c) a full parser model.
- Chosen: **(b)**. SettingsFile's public API is unchanged and its tests pass.
- Change: edits now keep each line's own ending and a missing final line break.
  Before, a file was rewritten with one line-ending style.
- Values are trimmed, as RetroFE trims them. `#` and line breaks are refused.

**LB2. Originals are kept in `<root>/manage/backups/`.**
- Options:
  - `.bak` files beside each file, which would clutter collections and layouts,
    and RetroFE lists `*.txt` in `menu/` and `*.sub` and `*.conf` in folders;
  - a backups folder inside each collection, which RetroFE would scan;
  - one tree under the root.
- Chosen: **one tree that mirrors relative paths**.
- Why: RetroFE never reads `manage/`. It's easy to find and to delete. A future
  "Reset" just calls `restoreOriginal`.
- After the review: a file that differs from the kept original is never deleted.
  It's kept as a numbered copy beside the original.

**LB3. Metadata edits live in a user override layer that RetroFE applies after meta/.**
- Options:
  1. Edit meta.db only. Lost at the next re-import, which happens whenever
     `meta/` changes.
  2. Edit the shipped `meta/hyperlist/<type>.xml`. A content update overwrites
     it, and rapidxml round trips drop comments and the declaration.
  3. A `meta/hyperlist/<type>.user.xml` sibling. Stock RetroFE would import it,
     but `directory_iterator` order isn't guaranteed, so it might not win.
     Whole-row replacement would also freeze every other field.
  4. A separate override folder imported by `MetadataDatabase` after the
     shipped files, field by field.
- Chosen: **(4)**: `<root>/manage/metadata/<metadata.type>.xml`, in hyperlist
  format, holding only the changed fields.
  - `MetadataDatabase::importDirectory` calls `MetadataOverrides::applyAll` last.
    A game without a row gets one, titled with its name.
  - `needsRefresh` also re-imports when the override folder is newer than
    meta.db, so hand edits are picked up.
  - The Editor writes meta.db straight away too.
  - Clearing an edit restores what the import of all three shipped formats
    would give. It never blanks a field it can't verify; see "Clearing metadata".
- Why not inside `meta/`: RetroFE re-imports when anything under `meta/` is
  newer than its executable. An edit there would trigger a full re-import on
  every start until the exe was updated.
- Covered by the "metadata changes survive a re-import" test.

**LB4. `media.basePath` is honoured (fork compat).**
- OneSauce data sets `media.basePath = %BASE_ITEM_PATH%/%ITEM_COLLECTION_NAME%`
  in 86 collections. Their game and system art live on the content side. Stock
  RetroFE ignores the key and finds no MAME art.
- `Configuration::getMediaPropertyAbsolutePath` and
  `Library::Properties::mediaPath` now apply it after `media.<type>` and before
  `baseMediaPath`. Collections without the key behave as before.

**LB5. Menus with a menu/ folder and no menu.txt.**
- Writing a menu.txt there would hide the whole folder, because menu.txt wins.
  So children are added and removed as `menu/<child>.txt`. Reordering is
  refused, because RetroFE sorts these menus by name.
- `menu_supported.txt` is kept in step when present. RetroFE Plus rebuilds
  menu.txt from it, so editing only menu.txt on a shared drive would be undone.

**LB6. .sub semantics.**
- An empty `.sub` means every game of the source.
  - Adding to it would narrow it to one game, so adding is a no-op.
  - Removing one game would need every other game listed, so it's refused.
- Removing the last entry deletes the file, so it doesn't turn into "every game".

**LB7. A launcher override that names the default is removed.**
- Launcher::run treats it exactly like no override. A collection's own launcher
  file still wins either way.

**LB8. Media lookups check files, not folder listings.**
- RetroFE lists and caches whole folders. A details screen needs about 20
  checks, and listing big folders cold took 450 ms per game on K:.
- `findMediaFile` checks each candidate name instead: 2–40 ms, with no cache to
  invalidate after writes.

**LB9. New systems are listed in the top menu, which is `firstCollection` only when it's a menu.**
- `Setup::addToMainMenu` and `removeFromMainMenu` delegate to `Library::MenuFiles`.
- `Setup::topMenu()` is `firstCollection` when it is a menu or could become one,
  meaning it has no games of its own: no `list.extensions`, `list.path`, roms
  folder or `.sub`. Otherwise it's "Main".
  - So `firstCollection = MAME` never gets a menu.txt mixing collections into its games.
- `firstCollection` is read from RetroFE's live `Configuration`
  (`Setup::setConfiguration`, set by ManagerUI), so a command-line override
  counts. Without it, the settings files are read.
- Consistency:
  - `validate` refuses the top menu's name;
  - `listCollections` leaves the top menu out, and the "Browse opens" choice lists it first;
  - `addToMainMenu` refuses names that can't be folder names, using `Setup::checkName`.

**LB10. Snapshots, not live models.**
- The index is immutable and rebuilt in the background after each change. A
  rebuild takes under a second when the disk cache is warm. Screens never see
  half-built state, and the library needs no locks beyond the snapshot swap.
- Changes never cancel a running build: it publishes, and one more build follows.
- Per-game and per-page lookups are precomputed at build time:
  - the page lists of games (`itemsOf`);
  - which merged collections show a game;
  - which playlists list a game, taking `*` and "must be on that page" into account.

**LB11. `launcher = a, b, c` uses `a` (fork compat, decided after review).**
- OneSauce/RetroFE Plus data names a default launcher and its alternatives. On
  K:, 25 collections do.
- What changed:
  - `CollectionInfoBuilder::buildCollection` and `Launcher::launcherName` take
    the first name, via `Library::firstLauncher`.
  - In the library, `LauncherResolution::configured` is that name and
    `alternatives` holds the rest, read-only.
  - Setting a collection's `launcher` in Manage moves the pick to the front of
    the list and keeps the others.
- Effect on K: with Linux launchers, 22 launcher names are unresolved instead of 39.

**LB12. `list.includeFile` / `list.excludeFile` are honoured (fork compat, decided after review).**
- When set, they name the collection's include and exclude lists *instead of*
  `collections/<c>/include.txt` and `exclude.txt`, with the same format and rules.
  - Values are resolved like other path keys: `%BASE_ITEM_PATH%` is expanded,
    `%ITEM_COLLECTION_NAME%` is replaced at import, and relative paths resolve
    against the root.
- Applied in `CollectionInfoBuilder::ImportDirectory` and in the library scanner.
- On K:, the content-side lists now apply: 30,201 games are listed instead of 30,817.

## Open items

- **Playlists on OneSauce's content side** (`content/.../<c>/playlists`) are
  still ignored. RetroFE reads `collections/<c>/playlists` only.
- **`media.<type>` keys are paths**, so they aren't in the editable set. A
  "media folder" picker would need its own rules.
- **Favorites editing:** `addToPlaylist` allows `favorites`. RetroFE rewrites
  that file when a favourite is toggled in the frontend, so a Manage edit made
  while RetroFE holds the old list is lost unless RetroFE reloads first.
- **The `needsRefresh` exe-time rule** (`<root>/retrofe/RetroFE.exe` older than
  `meta/`) re-imports on every start once anything in `meta/` is newer than the
  exe. It's left as it is; LB3 avoids triggering it.
- **Names Windows can't represent in the ANSI code page** are skipped, as RetroFE
  can't open them either. Moving the library to UTF-8 paths would only help once RetroFE does too.

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

## Threading and lifetime

- Create one `Library::Indexer` for the Manage hub, with `ScanOptions{ root = Configuration::absolutePath }`,
  and call `rebuild()` when Manage opens. It builds on its own thread.
- `indexer.status()` gives `state` (Idle, Building, Ready, Cancelled),
  `progress` (0..1), a `phase` ("Listing MAME") and `generation`, which
  increases with every new snapshot. Show the progress while there's no index yet.
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
- `Editor` calls run on the calling thread. They touch one or two small files.
  The exception is `replaceMedia` with a large video, which copies the file;
  call it from a worker if that matters.

Timings on the OneSauce-shaped data on K: (146 collections, 30,817 games,
12 MB meta.db, read only):

| Measurement | Time |
|-------------|------|
| First build, cold disk cache | 5.2 s |
| Builds after that | 0.5–0.9 s |
| Everything under `Main` (menu filter mask) | 15 ms |
| All 19 media slots of one game | 2–40 ms (144 ms cold) |

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
  - `includeList` and `excludeList`;
  - `gameCount`;
  - `caseMismatchCount` and `caseMismatches`: files whose extension matches only
    if case is ignored, which RetroFE doesn't list;
  - `launcher`, the default `LauncherResolution`;
  - `launcherOverrides`;
  - `playlists`, each with `entries` and `reserved` (favorites and lastplayed);
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
- **Users:** `index->usesOf(launcher)` returns `LauncherUse{collection, game, via}`.
  An empty `game` means it's the collection's default.
- **Problems:** `index->unresolvedLaunchers` lists the names that something asks
  for but that don't exist. That includes OneSauce's comma lists.
- **Platform:** `ScanOptions::os` picks which `launchers.<os>` folder to read.
  The default is this platform's. Set it to "linux" to inspect a cabinet's
  launchers from Windows.

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

## Writing: `Library::Editor`

Create one `Editor(root)` and pass it the snapshot the screen shows, which it
uses for validation. Every call returns an `EditResult`:
- `ok`;
- `message`, a sentence to show;
- `files`, the files written or removed.

After a successful write, call `indexer.changed("what")`.

| Call | Writes | Rules |
|------|--------|-------|
| `setGameLauncher(index, collection, game, launcher)` | `collections/<c>/launchers/<game>.conf`, first line only | The launcher must exist for that collection. Naming the collection's default, or passing "", removes the file, because Launcher::run treats them the same. `collection` is the game's own (source) collection. |
| `setMetadata(index, collection, game, field, value)` / `clearMetadata` | `manage/metadata/<metadata.type>.xml`, then meta.db straight away | A field from `MetadataOverrides::fields()`. One line, at most 200 bytes. A title can't be blank. Clearing restores the shipped hyperlist value in meta.db. |
| `setCollectionSetting(index, collection, key, value)` / `removeCollectionSetting` | The settings*.conf or info.conf the effective value comes from. Otherwise settings.conf, or info.conf for info keys. | Settings keys: `list.extensions`, `list.menuSort`, `list.includeMissingItems`, `list.sortType`, `launcher`, `metadata.type`, `layout`. Info keys: `title`, `manufacturer`, `developer`, `year`, `genre`. Each value is checked: extensions, yes/no, sort types, an existing launcher or layout. Remove takes the key out of every file. |
| `addMenuChild(index, menu, child, position)` / `removeMenuChild` / `moveMenuChild` | `menu.txt`. For a menu-folder menu with no menu.txt: `menu/<child>.txt`. Plus `menu_supported.txt` if present. | The child must be an existing collection; its folder spelling is used. Refused: Home or Catalog, the menu itself, a loop, and reordering a folder menu. |
| `addToPlaylist(index, collection, playlist, gameCollection, game)` / `removeFromPlaylist` | `collections/<c>/playlists/<name>.txt` | A game of another collection is written as `_<C>:<name>`. The game must be on that page. `all` and `lastplayed` are refused. An entry covered by `*` can't be removed. |
| `addToSub(index, merged, source, game)` / `removeFromSub` | `collections/<merged>/<source>.sub` | An empty .sub already shows everything, so adding is a no-op and removing one game is refused. Removing the last entry deletes the file. |
| `replaceMedia(slot, file)` / `clearMedia(slot)` | The slot's own folder, as `<basename>.<ext>`, with the extension lower-cased | The file kind must fit the slot. The slot's other files of that kind are moved aside. Clearing is refused when the slot only shows a fallback. Nothing is ever written into a fallback's folder. |
| `restoreOriginal(file)` | Puts back the copy kept before the first change, or deletes a file that didn't exist then | |

Safety rules that hold for all of these:
- **Never edited:** `executable`, `arguments`, `list.path` and any other path
  key. Menu lines are only ever added, removed or moved one at a time.
- **Atomic:** writes go to a temp file and are renamed into place.
- **Byte-for-byte:** every byte the edit doesn't change stays as it was. That
  includes comments, blank lines, spacing, a trailing comment on the edited
  line, the file's own line endings, and a missing final line break.
- **Originals:** before its first change, a file is copied to
  `<root>/manage/backups/<path relative to root>`. A file that didn't exist yet
  gets an `.absent` marker instead. Replaced media files are moved there, which
  is cheap for videos. Files outside the root go under `manage/backups/_external/<drive>/...`.

The editing primitives can be used directly for anything not covered above:
- `ConfFile`: one file, last-wins or first-wins;
- `ConfLayers`: a set of files where later ones win;
- `LineList`: entries read exactly as `ImportBasicList` reads them, untrimmed.

`SettingsFile`, used by the Settings form, is now a thin wrapper over `ConfLayers`.

## How changes take effect

RetroFE reads configuration, collections, metadata and media folder listings
once and caches them. Its media file cache is never cleared, and
`%BASE_*_PATH%` is frozen at start-up. So:

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
  beating `launcher =`, and the executable lookup order;
- metadata matched on the exact name per `metadata.type`.

These are simplified or not modelled:
- the `info/<item>.conf` media basename overrides;
- layout-mode and common-mode media (`layouts/<L>/collections/...`, `_common`);
- `randomSelect` art;
- `cyclePlaylist`;
- emuarc's duplicate items, which are collapsed to one per folder;
- shipped values for `clearMetadata`, which come from hyperlist files only (not mamelist or emuarc).

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

**LB9. Setup's top-menu helpers use `firstCollection`.**
- `Setup::addToMainMenu` and `removeFromMainMenu` delegate to
  `Library::MenuFiles`, aimed at `firstCollection` ("Main" by default). They
  never add the top menu to itself, keep originals, and handle menu-folder menus.
- `Setup::validate` and `listCollections` still single out "Main". They feed the
  Settings UI, which other work owns.

**LB10. Snapshots, not live models.**
- The index is immutable and rebuilt in the background after each change. A
  rebuild takes under a second when the disk cache is warm. Screens never see
  half-built state, and the library needs no locks beyond the snapshot swap.

## Open items

- **Comma launcher lists** (`launcher = lr-a, lr-b`, RetroFE Plus) are
  reported as unresolved. On K:, 25 collections use them; with Linux launchers,
  39 launcher names in all are unresolved. Options:
  - (a) leave as is, faithful to this RetroFE;
  - (b) make RetroFE and the library use the first name, a fork-compat change
    like LB4;
  - (c) let the Systems screen rewrite the list to one name.
  - Recommendation: **(b)**, if OneSauce data is meant to launch on this fork.
- **`list.includeFile` / `list.excludeFile`** (OneSauce) are ignored, as RetroFE
  ignores them. The include and exclude lists on the content side don't apply,
  and neither do playlists kept there. Options: honour them like
  `media.basePath`, or leave them ignored.
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

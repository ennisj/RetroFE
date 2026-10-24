# Design: Manage screens for games, menus, systems, videos, emulators and themes

Status: draft, round 1 in progress. Decisions and open questions are tracked
in [../DECISIONS.md](../DECISIONS.md); what each round delivered is in
[../FORK-CHANGELOG.md](../FORK-CHANGELOG.md).

## Goal

Manage becomes a hub for the things a RetroFE install is made of:

| Entry | What it manages |
|-------|-----------------|
| **Games** | Every installed game, searchable; a game's details (metadata, media, launcher, playlists) |
| **Menus** | The top menu (Browse) and every menu below it: menus of menus and sub-collections |
| **Systems** | Collections of games: where the games are, which extensions, which emulator, media |
| **Videos** | Collections of videos instead of games |
| **Emulators** | Installed emulators and the launchers that start them |
| **Themes** | Installed layouts; opens the theme editor (ported from One Saucier) |

The functionality is modelled on One Saucier's Manage and details screens,
adapted to RetroFE's layout-drawn forms: where One Saucier shows a details
screen as scrollable tiles, here each tile is a **link** row that opens its
own screen ([DECISIONS M2](../DECISIONS.md)).

Out of scope for now: packaging customizations and custom packs. The Catalog
gets one more source: this RetroFE fork itself, from GitHub.

## Building blocks (round 1)

### Navigation

A stack of screens. Each screen is a form id plus parameters (e.g.
`game=Arcade/pacman`); links and result rows push screens, Back pops them and
restores the previous selection. Actions can push a screen or go back after
they succeed.

### Providers

Each entity is a **provider** serving bindings under its own prefix
(`game:title`, actions `game.save`, result rows `game:search`, options
`game:genres`) and reading the current screen's parameters. Providers are
separate files registered in one place, so entity screens can be built in
parallel.

### New form elements

| Element | Use |
|---------|-----|
| `results` | Selectable rows from a search; Select opens a details screen |
| `link` | A row that opens another screen, with a status on the right |
| `value` | A read-only label and value |
| `image` | Artwork or other media from a path |
| `text live="yes"` | Search boxes that filter while typing |
| `text multiline="yes" rows="N"` | Longer text kept as lines, such as a game's story |

### Data

A shared **library** layer reads what RetroFE reads (collections, menus,
items and metadata, media folders, launchers, layouts) and writes changes the
way RetroFE expects them, preserving comments and formatting as
`SettingsFile` already does. Indexing runs off the UI thread. Its API, for
the screen authors, is in [manage-library.md](manage-library.md).

## Rules that apply to every screen

These carry over from One Saucier, where they were learned the hard way:

- **An explicit list of what each screen may change.** Paths, `executable` and
  `arguments` are shown, never edited (the one exception is the Emulators edit
  screen, which changes a launcher's own file, validated, warning who it
  reaches), and nothing outside the list is written.
- **Back up the original before the first change**, and offer **Reset**.
- **Write through a temporary file and rename it into place**, keeping
  comments, spacing and line endings in files people also edit by hand.
- **Choices instead of free text** wherever there's a fixed set of valid
  values (launchers, yes/no, sort fields); free text is one line, trimmed
  (texts that are lines, such as a story, use a multi-line field).
- **Ask before deleting**, and say what stays ("Its games stay on the drive").
- **Changes take effect on reload.** RetroFE reads configuration, collections,
  metadata and media once, and caches media folder listings for good. Screens
  mark changes as pending, and RetroFE reloads when you leave Manage (as the
  Catalog does when you're back on Home).
- **Names are case-sensitive** in RetroFE (folders, menu lines, metadata,
  playlists, launcher overrides, ROM extensions). Screens show and write the
  exact spelling found on disk.

## How RetroFE stores each entity

All collections are one kind of thing; what a collection *is* depends on the
files in `collections/<name>/`:

| Files | Makes it a |
|-------|------------|
| `settings.conf` with `list.path`/`list.extensions` (or a `roms/` folder) | **system**: games from disk |
| `menu.txt`, or a `menu/` folder of `*.txt` | **menu**: lists child collections, in file order |
| `<source>.sub` files | **merged collection**: another collection's games, filtered by the `.sub` lines (empty = all) |
| `info.conf` | descriptive keys (title, manufacturer, year, genre) that override `settings.conf` |
| `launcher.<os>.conf` / `launcher.conf` | the collection's own launcher, which beats `launcher =` |

A collection can be several of these at once. The top menu is
`firstCollection` (Browse). Games get metadata from `meta/hyperlist/*.xml`,
cached in `meta.db`; a game's launcher can be overridden by
`collections/<c>/launchers/<game>.conf` (first line = launcher name). Media is
found under `collections.<c>.media.<type>` or
`baseMediaPath/<c>/medium_artwork/<type>/`, and `system_artwork/` for the
collection itself. Launchers live in `launchers.<os>/<name>.conf` (plus
collection-local `launchers.<os>.local/`). Themes are `layouts/<name>/`.

OnesaUCE-shaped data adds keys stock RetroFE ignores (`media.basePath`,
`list.includeFile`/`excludeFile`, comma lists in `launcher =`,
`menu_supported.txt`, which its frontend regenerates `menu.txt` from). The
screens read these where it helps show the right thing, but write only what
stock RetroFE reads.

## Entities

### Games

**Find:** one search across every system: text (name and title), filters for
system or menu and "no metadata". Rows show the title, the system, and the
file. Results are capped with a "refine the search" note.

**Details** (each line is a link to its own screen, with a status on the right):

| Link | Shows / edits | Writes |
|------|---------------|--------|
| Details | Title, system(s), file, size, year, manufacturer, genre, players, play count, last played, favourite | read-only |
| Metadata | Title, year, manufacturer, developer, genre, players, rating, score, clone of | a per-user metadata override layer that RetroFE imports after the shipped hyperlist files (so content updates don't lose edits), plus `meta.db` for immediate effect |
| Launcher | The system's launcher (default) or another one for this game | `collections/<c>/launchers/<game>.conf` (removed when back to default) |
| Media | Each media type (logo, front, marquee, screenshot, video, bezel, story…) with a preview; replace from a file or clear | the folder RetroFE reads for that type; never clears a fallback from another folder |
| Playlists | Favourite on/off, add to or remove from playlists and merged collections | `playlists/*.txt`, `*.sub` |
| Description | The story text | `medium_artwork/story/<game>.txt` |
| Actions | Test-launch, hide (exclude) | `exclude.txt` |
| RetroArch | Only when the game's launcher runs RetroArch: curated RetroArch settings, core options and controls (One Saucier's tiles), see [RetroArch settings](#retroarch-settings) | `<config>/<Core>/<rom>.cfg` / `.opt`, `<remaps>/<Core>/<rom>.rmp` |
| Saves | The game's save files and save states, where they are; Back up now, Delete (confirmed, backed up first), the backups with Restore and Delete; see [Saves](#saves) | copies under `manage/saves/<collection>/<game>/<id>/`; the game's own files in RetroArch's save folders |

### Menus

**Find:** a tree from the top menu (Browse) down, plus search. Rows show the
name, kind (menu, merged collection, system) and item count.

**Details:**

| Link | Shows / edits | Writes |
|------|---------------|--------|
| Details | Name, title, parents, kind, children count, sources of a merged collection | read-only |
| Children | The menu's entries in order: add (pick an existing collection), remove, move up/down | `menu.txt` (order kept, line endings kept) |
| Info | Title, manufacturer, year, genre | `info.conf` |
| Media | Logo, marquee, device, video, story… | `system_artwork/` |
| Actions | New menu, new merged collection, delete (only empty ones made here, with a confirmation) | folders and the parent's `menu.txt` |

### Systems

**Find:** search, filter by parent menu. Rows show the name, game count and
launcher.

**Details:**

| Link | Shows / edits | Writes |
|------|---------------|--------|
| Details | Name, menus it's in, games folder, extensions, game count, launcher | read-only |
| Settings | Extensions, sort, include missing items, metadata type | `settings.conf` (allow-listed keys only) |
| Launcher | Default launcher (choice of installed ones), the collection's own launcher file shown | `settings.conf launcher =` |
| RetroArch | Only when its default launcher runs RetroArch: the core-wide settings, options and controls, with "reaches N systems" | `<config>/<Core>/<Core>.cfg` / `.opt`, `<remaps>/<Core>/<Core>.rmp` |
| Info and media | As for menus | `info.conf`, `system_artwork/` |
| Games | The system's games (a search screen filtered to this system) | — |
| Playlists | Create, rename, delete; edit entries | `playlists/*.txt` |
| Check this system | Warn-only findings: missing folder, no matching files (including wrong-case extensions), no launcher, missing launcher file, unreadable metadata, not in any menu | — |
| Actions | Add a system (the existing "Add a collection"), delete an empty one | as Setup does today, generalized to any parent menu |

### Videos

Stock RetroFE has no video collections. A **video collection** is a system
whose extensions are video types, marked `info.conf: type = videos`. Its
`info.conf: play` says how its videos play:

- `play = here`: **in RetroFE**. Select opens a full-screen player page
  instead of a launcher (`Graphics/VideoPlayback`, reusing the GStreamer
  video component). New video collections made in Manage play here.
- `play = launcher`, or no `play` line: **with an external player**, its
  launcher (e.g. the shared `VideoPlayer`, VLC or mpv), as before. Video
  collections made before the setting keep working this way.

Videos reuses the Systems screens with a video preset (extensions
`mp4,mkv,avi`). The Videos list shows only collections marked
`type = videos`.

| Link | Shows / edits | Writes |
|------|---------------|--------|
| Details | As for systems, plus **Plays** (in RetroFE / with an external player; which extensions RetroFE can't play) | read-only |
| Playback (the Launcher screen) | **Plays**: In RetroFE / With an external player; the video player (a launcher) when external | `info.conf play =`, `settings.conf launcher =` |
| Add a video collection | **Plays** (default In RetroFE: no player program needed); the player choice and program only for an external player | `settings.conf`, `info.conf type = videos` and `play =`, `launchers.<os>/VideoPlayer.conf` only for a new external player |
| Check | For one that plays here: no launcher findings; extensions RetroFE's player can't open (it plays mp4, avi, mkv, mp3, wav, flac) | — |

**Pages.** A video collection without a page of its own uses the layout's
`collections/_videos/layout/` (before `_menu`, the same rule:
`LayoutPages::collectionFolder`), else the layout's main page; with no layout
installed, the built-in video list. Its own files are its `video` media
(`collections.<c>.media.video` defaults to its first `list.path`, unless that
is set or it has a `medium_artwork/video` folder), so a `reloadableVideo
type="video"` previews the selected video. The player page is
`collections/<c>/layout/player.xml`, then `collections/_videos/layout/player.xml`
(`player 4x3.xml` for the page `layout 4x3`), else the built-in one; it needs a
`<menu>` (it can be invisible) that RetroFE fills with the list the collection's
page shows, and a `reloadableVideo type="video" jukebox="true"`; without
either RetroFE uses the built-in player page. A video that hasn't started
after 10 seconds is skipped, and each video that starts is counted like a
launch (last played and play count).

### Emulators (launchers)

RetroFE has no separate emulator entity: a launcher is an executable plus
arguments. The screen lists launchers from `launchers.<os>/`, collection
launcher files and collection-local launchers, plus names that are used but
not defined ("unresolved").

| Link | Shows / edits | Writes |
|------|---------------|--------|
| Details | Name, where it's defined, executable (and whether it exists), arguments, working folder, the file verbatim | read-only |
| Edit | Program, arguments (presets or custom), working folder and restart (global launchers only), with who it reaches | the launcher's own file, in place |
| Rename | A global launcher's new name, with a preview of every file that changes | `launchers.<os>/` (every platform), `launcher =` lines, games' launcher files |
| RetroArch | Only when it runs RetroArch: the core-wide settings, options and controls (the system screens') | `<config>/<Core>/<Core>.cfg` / `.opt`, `<remaps>/<Core>/<Core>.rmp` |
| Used by | Systems that use it by default, games that pick it | — |
| Actions | Add a launcher (executable via a file picker, arguments from presets), duplicate, delete when unused | `launchers.<os>/<name>.conf` |

**Built (round 2)**, in `Manager/Emulators/` (provider prefix `emulator`):

| Form | Shows |
|------|-------|
| `emulators` | Search over every launcher (name, program, system). Rows: the name; where it's defined (Global, `<C>`'s own file, `<C>`, local), the program's file name ("(missing)" when it isn't there) and "N systems, M games". Then **Used but not defined**: names a system (default or alternative) or game asks for, each opening the add form with that name. A link to add a launcher. |
| `emulator` (`id`, `scope` = global, collection or local) | Name, where it's defined, program and whether it's found (relative paths are relative to RetroFE's folder; `%RETROFE_PATH%` is expanded; a bare name is looked up on the system path; per-game programs and another platform's launchers aren't checked; a relative path with a folder in a Linux or macOS launcher is flagged "Windows only", since there RetroFE runs it from the working folder), arguments, working folder, restart (both marked "ignored" for collection and local launchers, as RetroFE only reads them for global ones). Links: **Used by** and **The file**. Actions: **Duplicate and edit**, **Delete launcher** (only when nothing uses it, and not a collection's `launcher.<os>.conf` with a `launcher.conf` beside it; confirmed). |
| `emulator-users` | The systems that use it by default (or through their own launcher file) or as an alternative, opening `system` with `id`; the games that pick it, opening `game` with `collection` + `id`. |
| `emulator-file` | The conf file as read-only notes, comments included. |
| `emulator-new` (`name`, or `from` + `fromScope`) | Name, program (file field: type with Right, or browse), arguments from a preset (`"%ITEM_FILEPATH%"`, RetroArch `-L "<core>" "%ITEM_FILEPATH%"` with a core file field, MAME `%ITEM_NAME% -rompath "%ITEM_DIRECTORY%."`: RetroFE ends `%ITEM_DIRECTORY%` with a separator, which on Windows would escape the closing quote) or custom text, working folder, whether RetroFE restarts after it (`reboot = yes`, carried over by Duplicate), and the command it will run. Paths inside RetroFE's folder are written as `%RETROFE_PATH%\...`. A name another launcher already answers to in any scope is refused, with why. Creates `launchers.<os>/<name>.conf` through `Editor::addLauncher`, shows it in the list at once ("Updating the list..." until the index catches up), then goes back. |

**Duplicate and edit** opens the add form filled in from the launcher
(arguments as custom text, its restart setting, name "`<name>` copy"), so the
copy can be changed before it exists. Round 2 kept existing launchers
read-only, as One Saucier does; they're now edited in place (below), because
changing a launcher every system uses meant duplicating it and repointing each
system.

**Edit, rename and the RetroArch link**, same provider:

| Form | Shows |
|------|-------|
| `emulator-edit` (`id`, `scope`) | Who it reaches ("Changes apply to everything that uses this launcher": the users summary), program (file field), arguments (the add form's presets, starting as custom text with the file's value; RetroArch's needs a core file), working folder and restart (global launchers only: RetroFE reads them as `launchers.<name>.*`), the command it will run, the file. **Save** writes only the fields changed, through `Editor::editLauncher` (in place: unknown keys such as `LEDBlinkyEmulator`, comments and line endings kept; empty arguments or working folder, or restart off, remove the line). Validated like Add: the program must exist (when changed), RetroArch's preset needs a core, no `#` or line break, and only RetroFE's `%VARIABLES%` (`%ITEM_FILEPATH%`, `%ITEM_NAME%`, `%ITEM_FILENAME%`, `%ITEM_DIRECTORY%`, `%ITEM_COLLECTION_NAME%`, `%RETROFE_PATH%`, `%COLLECTION_PATH%`, `%RETROFE_EXEC_PATH%`, `%CMD%`; `%1`, `%%` and `%~dp0` aren't variables; when the program is the command prompt or a batch file (`cmd`, `%CMD%`, `*.bat`, `*.cmd`), other `%NAME%`s in the arguments are its own variables, such as `%APPDATA%`, and allowed), which Add now checks too. **Reset**; **Put back the original** (confirmed) only while a copy of the file as it was is kept and differs: a launcher Manage created (Add, or the new name of a rename) has no copy, only an "absent" marker, and putting that back would delete a launcher systems use, so it isn't offered. Only this platform's file is edited; when `launchers.<other os>/` has one of the same name, the screen says it stays as it is. The buffer is read from the file, so a save shows at once; the list shows "Updating the list..." until the index catches up. Unsaved changes warn on leaving. |
| `emulator-rename` (`id`, `scope`), from the **Rename** button (global launchers only) | The new name (text), why it can't be (taken in any scope or platform folder, the same name, only the case, a system whose effective `launcher` comes from a file outside its folder, by `Properties::sourceFile`: RetroFE reads the global settings, then the collection's settings.conf, settings1..15.conf and info.conf, the last one winning), and **What changes**: one line per file (every collection file naming it is changed, also one a later file overrides, so it keeps working if that one goes). The collections are read once per launcher and snapshot (`Editor::scanLauncherRename`); each name typed is checked against that. **Rename** (confirmed) runs `Editor::renameLauncher`. The button replaces the launcher's screen with this one, and a rename replaces this one with the launcher's screen under its new name, so no open screen keeps the old name (Back from an unfinished rename returns to the list). |
| `emulator-retroarch`, `emulator-core-options`, `emulator-core-option`, `emulator-controls` (`id`, `scope`) | The system screens' core-wide RetroArch screens for the core this launcher runs (`RetroArchProvider`, a third set of the same templates), with "reaches N systems". The link on `emulator` shows only when the launcher runs RetroArch, with its core as the status. |

Deferred: payload checks beyond "the program exists"; renaming collection
and local launchers (a collection's own launcher is named by its folder; a
local one by its file, which games pick by its short name).

### RetroArch settings

**Built (round 2)**, modelled on One Saucier's RETROARCH / CORE OPTIONS /
CONTROLS tiles (rewritten, not shared). The logic is
`Library/RetroArch.{h,cpp}` (no SDL, unit-tested in `Tests/RetroArchTests.cpp`);
the screens are one provider, `Manager/Games/RetroArchProvider` (prefix
`retroarch`), and one set of templates, `Manager/Games/RetroArchForms.cpp`,
added to the Games table (`game-…`, params `collection` + `id`) and the
Systems table (`system-…`, param `id`).

**Detection** (`RetroArch::detect`, per launcher, cached per snapshot):

1. A launcher runs RetroArch when its program's file name is `retroarch`
   (`retroarch.exe`, `retroarch32`; not `retroarch.cfg`), an argument is such
   a program (`cmd /c start "" "...\retroarch.exe" -L ...`: used as the
   program when it's there, so its `retroarch.cfg` is found), it's the
   `org.libretro.RetroArch` flatpak, an argument is a `*_libretro.<ext>` file
   (a wrapper script), or `-L` / `--libretro` names something that looks like
   a core (`*_libretro*`, or a stem the table knows): `-L` means something
   else to other programs. Otherwise: "runs outside RetroArch".
2. RetroArch's program is resolved as RetroFE runs it (relative to RetroFE's
   folder, `%RETROFE_PATH%`, a bare name on the path); missing: "RetroArch
   not found". The working folder is the launcher's `currentDirectory`, else
   the program's folder.
3. `retroarch.cfg`: the launcher's `-c` / `--config`, else beside the
   program, else `%APPDATA%\RetroArch\` (then `%APPDATA%\`) on Windows,
   `$XDG_CONFIG_HOME/retroarch`, `~/.config/retroarch`, `~/.retroarch.cfg`
   on Linux (the flatpak's `~/.var/app/...` first), `~/Library/Application
   Support/RetroArch` on macOS. None: "RetroArch not found … start RetroArch
   once". From it: `rgui_config_directory`, `input_remapping_directory`,
   `libretro_directory`, `libretro_info_path`, `core_options_path`,
   `global_core_options`, `auto_overrides_enable`, `game_specific_options`,
   `auto_remaps_enable`. `:` is RetroArch's folder, `~` home, `default` or
   empty the default (`<base>/config`, `<base>/config/remaps`, where base is
   the program's folder on Windows and `retroarch.cfg`'s elsewhere).
4. The core: `-L`'s value (also `-L<core>`, `--libretro=`), resolved against
   the working folder, the program's folder, RetroFE's folder and the cores
   folder (a bare name gets `_libretro.<ext>`). A missing file is a note,
   not a stop.
5. The folder name is the core's **library_name**: a hand-checked table by
   file stem (desktop buildbot stems plus OnesaUCE's; a core whose name isn't
   sure, such as gw, fuse, bsnes or duckstation, is left out, because a wrong
   row would count as certain), else the `.info` file's `corename` (beside
   the core, in `libretro_info_path`, in the cores folder; it must be one
   plain path segment), "confirmed" only when `<config>/<corename>/` or
   `<remaps>/<corename>/` holds a file Manage didn't create (Manage's new
   files have an `.absent` original in the backups, so its own first save
   confirms nothing) and flagged on the screen otherwise. Neither: "core not
   known".

`retroarch.cfg` and every file read for what applies follow
`#include "file"` (relative to the including file, at most 16 deep, each
file once) at the point the line appears; writes change only the target
file, and a file that still has an `#include` line is never removed.

**Files** (`RetroArch::files`), with `<C>` the library name and `<rom>` the
game's file name without its extension (its name when the launcher passes
`%ITEM_NAME%`):

| Tile | A game's file | Applies under it | A system's file |
|------|---------------|------------------|-----------------|
| RetroArch settings | `<config>/<C>/<rom>.cfg` | `retroarch.cfg`, `<C>.cfg`, `<folder>.cfg` (stacked) | `<config>/<C>/<C>.cfg` |
| Core options | `<config>/<C>/<rom>.opt` | `<folder>.opt`, else `<C>.opt`, else the all-cores `retroarch-core-options.cfg` (one file wins) | `<config>/<C>/<C>.opt`, made from the all-cores file while it doesn't exist; with `global_core_options`, the all-cores file itself |
| Controls | `<remaps>/<C>/<rom>.rmp` | `<folder>.rmp`, else `<C>.rmp` (one file wins) | `<remaps>/<C>/<C>.rmp` |

`<folder>` is the name of the folder the game's file is in (RetroArch's
content-directory files), when it's a plain name. The all-cores options file
(`core_options_path`, else `retroarch-core-options.cfg` beside
`retroarch.cfg`) is what RetroArch takes a core's values from while that
core has no `<C>.opt`, and with `global_core_options`; only the core's own
entries count, picked by the key prefix its options share
(`coreOptionPrefix`: `genesis_plus_gx_`, `fbneo-`...; a key without it is
left to the core's default), and only those seed a new file.

A game's name must be one plain path segment (a name from `include.txt` such
as `../x` is refused) and not the core's name (a `MAME.zip` on core MAME would
make the game's file the core's): such a game says why and has no files.

**Rows**: RetroArch settings (aspect ratio, smooth picture, shader, bezel,
integer scaling, run-ahead, volume, resume = auto save/load state; One
Saucier's keys and values), controls (stick as D-pad, ten buttons sending a
RetroPad button or nothing: `input_player1_btn_<b>` with RetroArch's ids, -1
unmapped), and core options from a ported table keyed by library name (One
Saucier's generated table plus a few desktop cores). Each row offers
"Default (<what applies without this file>)" and the fixed choices; a value
no choice names shows as "(not one of these choices)" and is kept unless
changed. Core options are a list, each opening a one-option screen, because a
form's fields are fixed and each core has its own set.

**Writes** (`RetroArch::apply`, `useCoreSettings`, `restoreOriginal`):
only rows and values from the tables (an unknown row or choice is refused);
`key = "value"` lines changed in place, every other line, comment and line
ending kept (new files with `\n`, as RetroArch writes); originals through
`Library::Backups` before the first change (outside the root under
`manage/backups/_external/`), atomic writes. A game's `.cfg` keeps only keys
that differ from what stacks under it; a game's `.opt` / `.rmp` starts as a
copy of the file under it, and "Default" writes that file's value. A file
left empty, or equal to what it shadows, is removed. "Use the system's
settings" removes a game's own file (kept in the backups); "Reset to
original" restores the file as it was before Manage first changed it, and
shows only while the file differs from that. No reload is requested:
RetroArch reads these files at every start.

The system screen's "reaches" note lists the systems whose default launcher
resolves to the same library and config folder.

A launcher's own RetroArch screens (Emulators, `emulator-retroarch`...) are
the system's, keyed by the launcher (`id` + `scope`) instead of a system.

Not modelled: `--appendconfig`, One Saucier's button labels and
customization stack. Core option rows whose values differ between core
versions are left out (mGBA's frameskip).

### Saves

**Built**: `Library/Saves.{h,cpp}` (no SDL, `Tests/SavesTests.cpp`),
`Manager/Games/SavesProvider` (prefix `saves`), forms `game-saves` and
`game-saves-backup` (param `backup`) in the Games table, and a **Saves** link
on `game` with "N files, M backups" or "Not known for this emulator".

**Where** (`Saves::forGame`, RetroArch launchers only, from
`RetroArch::detect`'s `retroarch.cfg`), as RetroArch's
`runloop_path_set_redirect` and `config_load` do:

1. `savefile_directory` / `savestate_directory`: `:` is RetroArch's folder,
   `~` home, relative paths from the working folder. `config_set_defaults`
   starts both at the platform default (`<RetroArch's folder>/saves` and
   `/states` on Windows, `<config folder>/saves` / `states` elsewhere; desktop
   builds always have one), and `config_load_file` keeps it for `default`, and
   for an empty value or a path that isn't a folder (it logs "is not a
   directory, ignoring"); the screen notes those. The content-folder fallback
   in `runloop_path_set_redirect` is only for a build with no default.
2. `savefiles_in_content_dir` / `savestates_in_content_dir`: the game's
   folder instead.
3. `sort_savefiles_by_content_enable` / `sort_savestates_by_content_enable`
   add the name of the game's folder, then `sort_savefiles_enable` /
   `sort_savestates_enable` the core's library name, to whatever folder step 2
   left, the game's folder included: `runloop_path_set_redirect` replaces its
   intermediate folder with the content folder first, then joins
   `content_dir_name` and `library_name` to it. Sorting by core with a core
   that isn't known makes the saves "not known", rather than guessing.
4. The files are named after the game's file without its extension (an
   archive's own name), or its name when the launcher passes `%ITEM_NAME%`
   (known from the launcher even when its core isn't).
5. Replays (`.replay`, older `.bsv`) go with the states: RetroArch writes
   them in the state folder (`name.replay` follows `new_savestate_dir`).

**Which files** (`belongsTo`): exactly the stem, then a save ending (`.srm`,
`.sav`, `.rtc`, `.ldci`, `.eep`, `.sra`, `.fla`, `.mpk`, `.dsv`, `.mcr`,
`.<n>.mcr`) or a state ending (`.state`, `.state<n>`, `.state.auto`, any of
them + `.png`, `.bsv`, `.replay`, `.replay<n>`), the ending ignoring case.
The stem ignores case where the file system does (`Folders::ignoreCase`:
always on Windows, else found by looking the folder up with its case
swapped, as on most macOS volumes), because there `Sonic.md` and `sonic.sms`
write the same `Sonic.srm`. `Sonic 2.srm` is never Sonic's, and the game's
own file is never counted even in its own folder. Another game of the same
name (ignoring case where the file system does) whose saves resolve to the
same folders (the same core, unsorted) is named on the screen, since those
files are its too.

**Backups**: `<root>/manage/saves/<collection>/<game>/<yyyymmdd-hhmmss>[-n]/`
with `saves/`, `states/` and `backup.txt` (`reason = ...`), outside every
content and RetroArch folder and apart from `manage/backups/` (whose
per-file "original" model doesn't fit snapshots). **Back up now** copies the
current files (refused when there are none); **Restore** checks every file
in the backup is the game's (a backup holding anything else is refused,
nothing copied) and that every file it would write over is one of the
game's current files (never the game itself, or a file the file system
matches under another spelling), backs up the current files ("Before
restoring the backup from ..."), removes them and copies the backup's in, so
the game ends with exactly the backup's files; **Delete these saves** (confirmed) backs up
("Before deleting") and removes them; **Delete this backup** (confirmed)
removes one backup. Backups are kept until deleted. A game or system whose
name can't be a folder name there (a path, a trailing dot or space, a Windows
device name such as `con` or `com1.x`) can't be backed up, and the screen
says why. Two backups in one second get `-2`, `-3`...; they're listed newest
first. RetroFE doesn't read saves: no reload.

Not built: saves of other emulators. Each keeps its own (MAME a folder per
game under `nvram/`, `sta/`; standalone emulators their own layouts), so a
per-launcher "saves folder" setting matched by file stem would be wrong for
the commonest ones; such games say "Not known for this emulator".

### Themes

**Find:** installed layouts with facts (resolution, made with the editor or by
hand, collection-specific pages). **Details:** use this theme (sets
`layout`), edit (editor-made themes), new theme, duplicate, delete
(editor-made only).

The **theme editor** is ported from One Saucier: its pure core (project
model, widget definitions, XML generator, canvas maths, export with a guard
that never overwrites hand-made themes) is copied; its UI is rewritten as an
ImGui screen inside the manager, previewing with RetroFE's own page builder.
It edits themes made with the editor; editing hand-made themes such as Aeon
Nox needs an importer or a different mode (see DECISIONS).

## Catalog: this fork from GitHub

There are no releases yet: CI uploads build artifacts only. The plan:

1. A `github` source type that reads a repository's releases
   (`https://api.github.com/repos/<owner>/<repo>/releases`) and offers each
   platform's release asset as a package of kind `app`.
2. The Sources screen offers "RetroFE (this fork)" as a one-step source for
   `ennisj/RetroFE`, alongside adding any other source.
3. Installing an `app` package updates RetroFE itself. Windows can't overwrite
   a running exe, so the update is staged and applied by renaming the running
   files aside and restarting; leftovers are removed on the next start.
4. A release workflow that builds and attaches the Windows package when a tag
   is pushed. Publishing a release is your call (see DECISIONS).

## Rounds

| Round | Work (parallel where listed together) |
|-------|---------------------------------------|
| 1 | Forms foundation (navigation, providers, new elements, Manage hub) · Aeon Nox as the default layout · library data layer · Catalog GitHub source and self-update · theme editor core port (model, generator, widget audit, storage, export) |
| 2 | Games · Menus and Systems (Videos as a preset of Systems) · Emulators · Themes list and theme editor screen · Aeon Nox styling of the new elements |
| 3 | Theme editor editing (drag, properties, templates) · media replacement, playlists and other editing extras · end-to-end checks on OnesaUCE-shaped data (read-only copy) |
| 4+ | Later items from each entity, the open decisions you've answered |

Every round: implement → adversarial review (Fable) → fixes → merge into
`feature/management-ui` → build, tests, deploy to `E:\Storage\OneSauce` →
changelog and decisions updated.

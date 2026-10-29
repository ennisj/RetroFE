# Where everything goes

This is the reference for where a RetroFE install keeps each kind of thing: the program,
settings, themes, launchers, emulator programs, systems, games, their media, and what
Manage and the Catalog keep for themselves. Manage, the Catalog and new documentation
follow it. It describes what the code does; where the code is still being brought in line,
the [To do](#to-do) list says so.

Paths are written with `/`; on Windows they're the same with `\`.

## The root

Everything is under one folder, the **root**. Launchers and settings refer to it as
`%RETROFE_PATH%`, and the code calls it `Configuration::absolutePath`. RetroFE finds it
in this order:

1. The `RETROFE_PATH` environment variable.
2. On Linux and macOS, the first line of `~/.retrofe`.
3. Otherwise, from where the program is:
   - **Windows:** the folder *above* the one holding `retrofe.exe`. The program is
     expected in `<root>/retrofe/`, so `E:/Storage/OneSauce/retrofe/retrofe.exe` gives
     the root `E:/Storage/OneSauce`.
   - **macOS:** the folder holding `RetroFE.app`.
   - **Linux:** the program's own folder, or the AppImage's folder.

Relative paths in settings are relative to the root.

## A stock install

```
<root>/
├── retrofe/                      the program: retrofe.exe and its DLLs (Windows)
├── settings.conf                 global settings (settings1..15.conf and settings_saved.conf add to it)
├── controls.conf                 controls (controls1..9.conf, "controls - <type>.conf" add to it)
├── layouts/<theme>/              themes: layout.xml, splash.xml, artwork, fonts, sounds
│   └── collections/<system>/     a system's own pages and layout artwork, inside the theme
├── launchers.windows/<name>.conf global launchers (launchers.linux/, launchers.apple/ elsewhere)
├── emulators/<emulator>/         emulator programs, one folder each
├── collections/
│   ├── <system>/                 a system (a "collection")
│   │   ├── settings.conf         where its games are, their extensions, its launcher
│   │   ├── roms/                 its games
│   │   ├── medium_artwork/<type>/  per-game art and video: logo, screenshot, video, ...
│   │   ├── system_artwork/       the system's own art
│   │   ├── include.txt, exclude.txt   which games are listed
│   │   ├── info/                 per-game launch settings (info/<game>.conf)
│   │   ├── launchers/<game>.conf per-game launcher choice
│   │   ├── launcher.windows.conf the system's own launcher, if it has one
│   │   ├── launchers.windows.local/  launchers only this system can use
│   │   └── playlists/            favorites.txt, lastplayed.txt and other playlists
│   ├── <menu>/                   a menu of systems: menu.txt (or menu/, or menu.xml)
│   ├── _common/                  art shared by every system
│   └── playCount.txt, timeSpent.txt
├── menu/<name>.txt               menus of menus
├── meta/                         game databases RetroFE imports (hyperlist/, mamelist/, emuarc/)
├── meta.db                       the imported game database
├── hi2txt/                       hiscore reader
├── catalog/                      everything the Catalog keeps (see below)
├── manage/                       Manage's backups and saved game saves
├── start.bat, exit.bat           optional scripts run when RetroFE starts and exits
└── log.txt
```

Folders whose names start with `_` under `collections/` aren't systems.

### Emulator programs: `emulators/<emulator>/`

Each emulator gets a folder of its own under `<root>/emulators/`, named after it:
`emulators/RetroArch/`, `emulators/MAME/`, `emulators/DuckStation/`. Its program and
everything it ships go in that folder, and so do its own settings, when the emulator can
keep them there (its "portable" mode). The Catalog installs emulators here (target
`emulators`). Emulators you install yourself can go anywhere, but here keeps the install
self-contained and movable.

- **RetroArch** keeps its cores in `emulators/RetroArch/cores/` (its `libretro_directory`),
  their descriptions in `emulators/RetroArch/info/`, and BIOS files in
  `emulators/RetroArch/system/`.
- **MAME:** RetroFE reads MAME's hiscores from `emulators/mame/hi/`.

### Launchers: `launchers.windows/<name>.conf`

A launcher is a small text file naming a program and its arguments; a system says which
launcher starts its games. Manage writes new launchers to `<root>/launchers.windows/`, the
global launchers that any system can use, and writes paths inside the root as
`%RETROFE_PATH%/...`, so moving the whole install doesn't break them:

```ini
executable = %RETROFE_PATH%\emulators\RetroArch\retroarch.exe
arguments = -L "%RETROFE_PATH%\emulators\RetroArch\cores\genesis_plus_gx_libretro.dll" "%ITEM_FILEPATH%"
```

The folder is named for the system RetroFE runs on (`launchers.windows`,
`launchers.linux`, `launchers.apple`); a folder called just `launchers/` isn't read. A
system can also have its own launcher or local launchers, and a game its own launcher
choice (see [LAUNCHERS.md](LAUNCHERS.md)). A game's own choice beats its system's, and a
system's own `launcher.windows.conf` beats its `launcher =` setting. A launcher name is
looked up among the system's local launchers first, then the global ones.

### Systems, games and media

A system is a folder `collections/<system>/` with a `settings.conf`. RetroFE looks for
its games in, first to last:

1. `list.path` in the system's `settings.conf`;
2. `<baseItemPath>/<system>/` if `baseItemPath` is set (no `roms`);
3. `collections/<system>/roms/`.

And for a game's media of type `<type>` (logo, screenshot, video...):

1. `media.<type>` in the system's `settings.conf`;
2. `<media.basePath>/medium_artwork/<type>/` if the system sets `media.basePath`;
3. `<baseMediaPath>/<system>/medium_artwork/<type>/` if `baseMediaPath` is set;
4. `collections/<system>/medium_artwork/<type>/`.

`system_artwork/` follows the same rules. **Manage puts a new system's games and media in
the defaults** (3 and 4), unless you point it at a folder you already have.

### What Manage and the Catalog keep

| Folder | What |
|---|---|
| `manage/backups/` | Every file Manage changes or removes, before it does |
| `manage/saves/<system>/<game>/` | Game saves Manage has put aside |
| `catalog/` (`catalogPath`) | `sources.conf`, `cache/<source>/`, `artwork/`, `downloads/` (unfinished downloads), `staging/` (installs in progress), `installed/<id>.json` (what each package wrote), `backups/` (files a package replaced), `update/` (RetroFE's own updates) |
| `retrofe.update.json` | A RetroFE update in progress |

The Catalog installs into these targets, never into a full path:

| Target | Folder |
|---|---|
| `root` | the root |
| `collections` | `collections/` |
| `items` | `baseItemPath`, else `collections/` |
| `media` | `baseMediaPath`, else `collections/` |
| `layouts` | the layouts folder (`baseLayoutPath`, else `layouts/`) |
| `meta` | `meta/` |
| `emulators` | `emulators/` (built-in emulator downloads only) |

It never writes into `catalog/` itself or the `retrofe/` program folder, except when
updating RetroFE.

## A split install (OnesaUCE)

OnesaUCE keeps games, media and themes outside the root, with settings such as:

```ini
baseItemPath = %RETROFE_PATH%/../../base_items/collections
baseMediaPath = %RETROFE_PATH%/../../base_assets/collections
baseLayoutPath = %RETROFE_PATH%/../../base_assets/layouts
```

Then games are in `<baseItemPath>/<system>/` (with no `roms` folder), media in
`<baseMediaPath>/<system>/medium_artwork/<type>/`, and themes in `<baseLayoutPath>/`.
System definitions, launchers, playlists, per-game settings and menus stay under the
root. See [ONESAUCE-COMPAT.md](ONESAUCE-COMPAT.md).

## Launcher variables

A launcher's program, arguments and working folder can use:

| Variable | Is |
|---|---|
| `%RETROFE_PATH%` | the root |
| `%ITEM_FILEPATH%` | the game's file, full path |
| `%ITEM_NAME%` | the game's name, without extension |
| `%ITEM_FILENAME%` | the game's file name |
| `%ITEM_DIRECTORY%` | the folder holding the game, ending in a separator |
| `%ITEM_COLLECTION_NAME%` | the system's name |
| `%COLLECTION_PATH%` | `<root>/collections/<system>` |
| `%RETROFE_EXEC_PATH%` | RetroFE's program (`<root>/retrofe/retrofe.exe` on Windows) |

Without `currentDirectory`, a game starts in its program's folder. On Windows, a relative
program path is relative to the root; elsewhere, use `%RETROFE_PATH%`.

## To do

Places that don't follow this yet:

- Adding a system with a new emulator program writes the program's full path, not
  `%RETROFE_PATH%/...`, when it's inside the root.
- The Windows package ships sample launchers in `launchers/`, which RetroFE doesn't read.
- GETTING_STARTED.md, LAUNCHERS.md and COLLECTIONS.md disagree with this page in places
  (the `launchers/` folder, `%RETROFE_EXEC_PATH%`, `%ITEM_DIRECTORY%`, a missing
  `SETTINGS.md`). Where they do, this page is right.
- The Catalog's `items` target puts `<system>/roms/...` under `baseItemPath`, where
  RetroFE doesn't look (it expects `<baseItemPath>/<system>/`).
- On Linux and macOS, a relative program path isn't resolved against the root.

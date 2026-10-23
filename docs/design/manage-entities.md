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

### Data

A shared **library** layer reads what RetroFE reads (collections, menus,
items and metadata, media folders, launchers, layouts) and writes changes the
way RetroFE expects them, preserving comments and formatting as
`SettingsFile` already does. Indexing runs off the UI thread. Its API, for
the screen authors, is in [manage-library.md](manage-library.md).

## Rules that apply to every screen

These carry over from One Saucier, where they were learned the hard way:

- **An explicit list of what each screen may change.** Paths, `executable` and
  `arguments` are shown, never edited, and nothing outside the list is written.
- **Back up the original before the first change**, and offer **Reset**.
- **Write through a temporary file and rename it into place**, keeping
  comments, spacing and line endings in files people also edit by hand.
- **Choices instead of free text** wherever there's a fixed set of valid
  values (launchers, yes/no, sort fields); free text is one line, trimmed.
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

Later: RetroArch per-game settings, core options and controls (One Saucier's
curated tiles), saves backup/restore.

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
| Info and media | As for menus | `info.conf`, `system_artwork/` |
| Games | The system's games (a search screen filtered to this system) | — |
| Playlists | Create, rename, delete; edit entries | `playlists/*.txt` |
| Check this system | Warn-only findings: missing folder, no matching files (including wrong-case extensions), no launcher, missing launcher file, unreadable metadata, not in any menu | — |
| Actions | Add a system (the existing "Add a collection"), delete an empty one | as Setup does today, generalized to any parent menu |

### Videos

Stock RetroFE has no video collections. A **video collection** is a system
whose extensions are video types and whose launcher is a player (or, later, a
layout that plays the item in-page with jukebox mode). Videos reuses the
Systems screens with a video preset: extensions `mp4,mkv,avi`, a "Video
player" launcher, and video previews in Media. The Videos list shows only
collections marked as video collections (`info.conf: type = videos`).

### Emulators (launchers)

RetroFE has no separate emulator entity: a launcher is an executable plus
arguments. The screen lists launchers from `launchers.<os>/`, collection
launcher files and collection-local launchers, plus names that are used but
not defined ("unresolved").

| Link | Shows / edits | Writes |
|------|---------------|--------|
| Details | Name, where it's defined, executable (and whether it exists), arguments, working folder, the file verbatim | read-only in v1 |
| Used by | Systems that use it by default, games that pick it | — |
| Actions | Add a launcher (executable via a file picker, arguments from presets), duplicate, delete when unused | `launchers.<os>/<name>.conf` |

Later: RetroArch core detection and the core-wide settings tiles.

**Built (round 2)**, in `Manager/Emulators/` (provider prefix `emulator`):

| Form | Shows |
|------|-------|
| `emulators` | Search over every launcher (name, program, system). Rows: the name; where it's defined (Global, `<C>`'s own file, `<C>`, local), the program's file name ("(missing)" when it isn't there) and "N systems, M games". Then **Used but not defined**: names a system (default or alternative) or game asks for, each opening the add form with that name. A link to add a launcher. |
| `emulator` (`id`, `scope` = global, collection or local) | Name, where it's defined, program and whether it's found (relative paths are relative to RetroFE's folder; `%RETROFE_PATH%` is expanded; a bare name is looked up on the system path; per-game programs and another platform's launchers aren't checked; a relative path with a folder in a Linux or macOS launcher is flagged "Windows only", since there RetroFE runs it from the working folder), arguments, working folder, restart (both marked "ignored" for collection and local launchers, as RetroFE only reads them for global ones). Links: **Used by** and **The file**. Actions: **Duplicate and edit**, **Delete launcher** (only when nothing uses it, and not a collection's `launcher.<os>.conf` with a `launcher.conf` beside it; confirmed). |
| `emulator-users` | The systems that use it by default (or through their own launcher file) or as an alternative, opening `system` with `id`; the games that pick it, opening `game` with `collection` + `id`. |
| `emulator-file` | The conf file as read-only notes, comments included. |
| `emulator-new` (`name`, or `from` + `fromScope`) | Name, program (file field: type with Right, or browse), arguments from a preset (`"%ITEM_FILEPATH%"`, RetroArch `-L "<core>" "%ITEM_FILEPATH%"` with a core file field, MAME `%ITEM_NAME% -rompath "%ITEM_DIRECTORY%."`: RetroFE ends `%ITEM_DIRECTORY%` with a separator, which on Windows would escape the closing quote) or custom text, working folder, and the command it will run. Paths inside RetroFE's folder are written as `%RETROFE_PATH%\...`. A name another launcher already answers to in any scope is refused, with why. Creates `launchers.<os>/<name>.conf` through `Editor::addLauncher`, shows it in the list at once ("Updating the list..." until the index catches up), then goes back. |

Existing launchers stay read-only, as in One Saucier: the details screen says
so and offers **Duplicate and edit**, which opens the add form filled in from
the launcher (arguments as custom text, name "`<name>` copy"), so the copy can
be changed before it exists. Deferred: RetroArch core detection and settings
tiles, and payload checks beyond "the program exists".

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

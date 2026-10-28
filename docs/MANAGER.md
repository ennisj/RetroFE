# Home Menu, Catalog, Settings and Manage

RetroFE can start with nothing but its executable. With no `settings.conf`,
`controls.conf`, collections or layout it uses built-in defaults, and you set
everything else up from inside RetroFE.

## The Home menu

With `homeMenu = yes` RetroFE starts on **Home**:

| Item     | What it does |
|----------|--------------|
| Catalog  | Packages you can install from your sources (see [The Catalog](#the-catalog)) |
| Settings | Display, menu and video settings, and [Controls](#controls) |
| Manage   | Games, menus, systems, videos, emulators and themes, plus what the Catalog installed and its sources |
| Browse   | Opens `firstCollection`, your usual top menu. Back returns to Home. |

`homeMenu` is on for a bare install and in the `settings.conf` the setup
creates. Existing installs don't have it, so they start on `firstCollection`
as before. Kiosk mode always skips Home.

Home keeps that order, but starts on **Browse** once there's something to
browse (any collection), and on **Manage** on a bare install, where Manage
opens anyway to add the first system. That holds after every reload too
(closing Manage after a change, Settings' Save, a Catalog install), so Home
never comes back on the Catalog.

F10 opens Settings from anywhere, switches to it from another screen, and
closes it again; change the key with `manager` in `controls.conf` (the first
keyboard key it lists), or in Settings > Controls (Open Settings). Back or
Escape returns to the screen you came from, where you left it, and closes the
first one.

Home is a collection built into RetroFE, so a layout styles it like any other:
`layouts/<layout>/collections/Home/layout/layout.xml`, with artwork named after
the items (`Catalog`, `Settings`, `Manage`, `Browse`).

## The Catalog

The Catalog installs packages (collections, artwork, layouts and so on) from
**sources** you add: an index file on the web or on disk, or a folder of
package archives such as a USB stick or a NAS share. The design, including the
index format, is in [design/catalog-sources.md](design/catalog-sources.md).

- **Catalog** on Home lists every package your sources offer, followed by a
  **Sources** item. Until there's something to list it opens the Sources screen.
- Selecting a package shows its details, with **Install**, **Update**,
  **Remove** or **Stop** as they apply, and **Back to the Catalog**. Downloads
  continue in the background, so you can go back to the list and pick
  another; they run one at a time. Pressing Install leaves focus on Back to
  the Catalog, never on Remove (which takes Install's place), and the outcome
  ("Installed QA Pack 1.0.0.") is said once.
- Once a collection is installed, **Show it in Browse** reloads RetroFE and
  opens that collection; Back from it goes to Home. Focus moves to it when the
  install finishes, unless you've moved on.
- Once you're back on Home with nothing left to do, RetroFE reloads so new
  content shows up. Installed collections are added to the Main menu, so they
  appear under Browse; removed ones are taken off it. Any other reload (Show
  it in Browse, Settings' Save, Apply changes now) does the same first, and
  waits for an install that's running.
- **Sources** (from the Catalog or Manage) lists your sources, each with what
  it offers ("12 packages", "Can't be read", "Off"). Choosing one opens its
  screen: its type and address, when it was last read and how that went (or
  that it couldn't be reached, so its packages are the last copy), its
  problems, the packages it offers (each opens its package), **Check for
  updates**, and **Remove this source** (after a second Select; packages
  installed from it stay installed).
- Adding a source asks you to confirm that you trust it: RetroFE doesn't
  check what sources offer. Plain `http://` is only allowed on your own
  network (localhost, a machine name like `nas`, or a private address), for
  indexes and downloads alike. Once its packages have been read, **Open the
  Catalog** appears at the top of Sources and takes focus; it shows the
  Catalog list (with Home on).
- **Manage** lists what's installed from the Catalog; each package opens its
  screen, where Remove takes it off again, also for a package no source
  offers any more.
- A package can only be updated from the source it was installed from. If
  another source offers the same package, remove it first to switch.

What a package may install: content, such as collection settings, launchers,
artwork, layouts and ROMs, under its targets. Programs and scripts (`.exe`,
`.dll`, `.bat`, `.ps1`, `.sh` and the like) are refused, as is anything in the
catalog folder or RetroFE's program folder (RetroFE's own updates, below, are
the one exception). File names Windows treats as
devices (`CON`, `NUL`, `COM1`…) or that end in a dot or space are refused too.

Everything the Catalog keeps is in one folder, `catalogPath` in
`settings.conf` (default `catalog` under the RetroFE root): `sources.conf`,
cached indexes, downloads, artwork, backups, and a record of every installed
file in `installed/<id>.json`.

- **Installing** backs up every file it's about to replace first. If any part
  of the install fails, or RetroFE stops and can't finish it on the next start,
  everything is put back as it was.
- **Removing** a package deletes the files it installed, puts back files of
  yours it had replaced, and keeps any file you've changed since.
- **Backups** kept per package: your original files, and the previous version
  from the last update.

In kiosk mode the Settings key does nothing.

The Catalog list is a collection built into RetroFE like Home, so layouts
style it as `layouts/<layout>/collections/Catalog/layout/layout.xml`. Each
item is named by package id, so artwork follows the usual naming: the Catalog
saves package artwork as `catalog/artwork/<type>/<id>.<ext>` and points the
Catalog collection's `media.<type>` there. Items also carry the package's kind
as `genre`, its source as `manufacturer` and its version as `year`.

HTTP sources need Windows for now; on Linux and macOS folder sources and
index files on disk work.

### Updating RetroFE itself

**Sources → Add RetroFE from GitHub** (after confirming you trust it) adds a
source for this fork's releases (`ennisj/RetroFE`). The Catalog then lists
**RetroFE from ennisj/RetroFE**, with **Update** when a release is newer than
the running version. Any other repository can be added with the type "GitHub
releases" and `owner/name` (or its github.com address) as the address. Only
the official repository offers RetroFE by default: for any other, add
`sources.<name>.app = yes` to `catalog/sources.conf`. `prerelease = yes` there
offers pre-releases too.

Installing it updates RetroFE and restarts it:

- The download must have a SHA-256 (GitHub's digest for the release file, or a
  `.sha256` file beside it) and match it; a release without one isn't
  installed. The checksum only guards against a damaged download; what you're
  trusting is the repository you added. Only versions newer than the running
  one are installed.
- Only the program folder (`retrofe\`) and the layouts the release ships are
  touched; collections, settings, controls, launchers and the catalog never
  are.
- The files it replaces are copied to `catalog/backups/retrofe/<old version>/`
  first.
- Windows can't overwrite a running program, so the new program folder is
  prepared as `retrofe.new` beside the old one, then swapped in with two
  renames (`retrofe` → `retrofe.old`, `retrofe.new` → `retrofe`). If another
  program has a file in `retrofe\` open, the changed files are swapped one by
  one instead (`<file>` → `<file>.old`, `<file>.new` → `<file>`). If anything
  fails, everything is put back and nothing changes.
- RetroFE then restarts itself (without running `exit.bat`) and deletes what
  it set aside once the old process has closed.
- Files and folders of yours named `.old` (e.g. `retrofe.old`, `a.dll.old`)
  are never deleted; the update uses `.old2` and so on instead.
- If a folder rename is held up (a virus scanner looking at the new files, for
  example), RetroFE keeps trying. If no program folder can be put back, it
  keeps trying while it runs and as it exits, then shows a message pointing
  at the instructions below.
- If RetroFE is stopped part way through (a crash or power cut), the next
  start finishes or undoes the update before anything else, and restarts if
  it put files back. If it can't finish after three starts, it stops trying
  and lists where the backup is under **Problems** on the Sources screen
  (there's no message box at start, so a cabinet isn't held up). The file
  `retrofe-UPDATE-INTERRUPTED.txt` in the RetroFE folder exists only while an
  update is under way. If it's still there and RetroFE won't start, follow
  it: rename `retrofe.old` (or `retrofe.new`) to `retrofe`, or copy the backup
  folder over the RetroFE folder.
- RetroFE can't be removed from Manage. GitHub sources only offer RetroFE
  itself; index and folder sources can't.
- GitHub allows 60 checks an hour without an account. RetroFE checks at most
  once a minute and only asks whether anything changed; if the limit is
  reached it shows the last list, says when it will check again, and waits
  until then.

Releases are made by pushing a tag such as `v2504.3`
(`.github/workflows/release-windows.yml`), which attaches
`RetroFE-2504.3-windows-x64.zip` (`retrofe\` plus `RetroFE.lnk`) and its
`.sha256` to the release. Tags with a `-` (e.g. `v2504.3-beta.1`) become
pre-releases. A release build reports its tag's version; other builds report
the version in `RetroFE/CMake/Versioning.cmake` (2504.2), so tag releases above
it, or raise it, so that development builds aren't offered older releases.
## Settings and Manage screens

Each screen is a **form**: fields bound to settings or other values. Up and
Down move between fields; Left and Right change a switch, number or choice,
or page through a list of results; on note-only screens Up/Down scroll a row
and Left/Right scroll a page; RetroFE's **letterUp** and **letterDown**
controls jump through a list of results to the first row of the previous or
next letter; Select edits text, opens the
[file browser](#choosing-a-folder-or-a-file) on a folder or file field,
presses a button, or opens a link or a result (Right on a folder or file field
types the path instead). While editing text, type and press Enter to keep it
or Escape to cancel; Ctrl+V pastes; Left, Right, Home, End and Delete move and
delete in the text. In a multi-line field (a game's story), Enter starts a new
line and **Ctrl+Enter** keeps the text; Up, Down, Page Up and Page Down move
the cursor between lines too.

The screens use the same words throughout: **Save** writes what you changed;
**Undo changes** drops edits not saved yet; **Put back the original** restores
what a file was before Manage first changed it (from `manage/backups`); **Put
back the default** returns a setting to RetroFE's built-in default. Where a
screen writes is a **Saved in** row with the file or folder. Changes RetroFE
has to load show "after it reloads, when you close Manage".

**Settings** starts with **Controls** (see [Controls](#controls)), then
Display (Fullscreen; **Screen size**: the screen's size, 1920 x 1080,
1280 x 720 and other common sizes, or Custom, which shows Width and Height;
Hide mouse), Menus (**Theme**, which sets `layout`; Start on Home; Browse
opens) and Video, then Save. A size that isn't in the list shows as Custom.

### Choosing a folder or a file

Select on a folder or file field (a system's games folder, an emulator
program, a replacement logo or video...) opens RetroFE's own browser, a screen
like the others, driven by the controller:

- **The first screen lists places to start from**: where the field's value is
  now, the field's own suggestion (a game's or system's media slot suggests
  its media folder), RetroFE's folder, folders picked from before (since
  RetroFE started), your Downloads and Desktop, then every drive with its
  name and free space as they arrive from background workers (removable
  ones say so; a slow drive does not hold up the screen).
- A field with a value, or with a suggestion, **opens straight in that
  folder** (the media fields open in the slot's own folder, or the nearest
  folder that's there); Back from it shows the places.
- **Each folder is a screen**: `..` goes up a folder; folders end in `\` and
  say what they hold ("212 files matching zip" for a system's games folder,
  counted from the Extensions field; "3 folders", "Empty"), counted in a background batch for the first 50 folders. Counts stop at
  10,000 entries and show `+` when capped; unreadable folders say "Can't be read". A file field lists only the files
  its pattern allows, with their size and date, and **pictures as
  thumbnails** for files up to 8 MB; videos show their size and date.
- **Select on a folder opens it.** A folder field has **Use this folder** at
  the top; a file field takes the file Select is pressed on. Either way the
  field gets it as if it were typed (nothing is saved until the screen's own
  Save, Add or Use this file), and "Chose ..." says what it got.
- **Back goes back up** the way you came, and from the places it cancels,
  leaving the field as it was.
- **Left and Right jump to the next or previous first letter**, as do the
  letter up / letter down controls when they're mapped. A folder with more
  than 200 entries shows the first 200 and a **Search** field (part of the
  name) for the rest.
- Paths are shown and given back **the way the field keeps them**: a
  launcher's program written relative to RetroFE's folder (or with
  `%RETROFE_PATH%`) stays that way when the new one is inside RetroFE's
  folder too; everything else gets the full path, as Windows' dialog gave.
- **Browse with Windows...**, the system's own file dialog, is offered at the
  bottom after a mouse button press while Manage is open, with mouse use
  in the last 15 seconds. Touch and motion within a second of RetroFE moving
  the pointer do not count. Closing Manage resets this. A trackball or light
  gun button reported as a mouse button can also qualify; motion alone cannot.

The browser is the form `file-browser` (bindings `browse:`), so a layout can
style it with `forms/file-browser/layout.xml`; Aeon Nox has one for 16:9 and
4:3 with taller rows for thumbnails. The theme editors' file chooser lists
folders the same way.

**Manage** links to a screen for each part of your setup: Games, Menus,
Systems, Videos, Emulators and Themes, then the Catalog (its Sources, and
each package it installed).

- On a bare install it says so and starts on **Add your first system**. Once
  a system exists (added on another screen, before any reload) the welcome
  and that link are gone.
- Changes Manage saves show after RetroFE reloads, when you close Manage.
  Every screen says how many are waiting at the right of its title ("2
  changes apply when you close Manage"): each different thing saved counts
  once (saving the same screen twice, or adding then deleting the same
  system, is one change), plus a theme to switch
  to, and Catalog installs or removals not shown yet. A queued reload still
  counts while it waits for an install to finish. Unsaved edits don't count.
- While changes are waiting, **Apply changes now** at the top of Manage
  reloads RetroFE straight away and opens Manage again ("Applied 2
  changes."). It waits for a Catalog install to finish first.

After an action, focus stays where it was. When the button you pressed goes
away (Hide this game, Install, Undo changes), it moves to the nearest other
row, but never onto a button that has just appeared (usually the undo of
what you pressed: Show this game again, Remove) or one that asks for
confirmation, when there's anything else. If that's all there is, Select
there waits until you've moved ("Any direction, then Select: ..."), so a
second press from someone still holding Select does nothing. After **Start
this game**, Manage comes back with focus on Start this game.

**Games** searches every installed game by name or title. The results come
straight after Search (up to 300; letterUp/letterDown jump through them by
letter); **Filters**, between the two, says what the search looks through
("All games", "Arcade, without metadata", "Hidden games in Arcade") and opens
the filters: one system or merged collection or everything under a menu,
only games without metadata, or only hidden games, with how many games
match. Choosing a game shows its facts and artwork (with how
often it was played, when last, and for how long, from
`collections/playCount.txt` and `timeSpent.txt`; RetroFE counts plays only
when `lastPlayedSkipCollection` is set, and only of programs that started,
while time played is always kept; a game with no plays says when they aren't
counted),
with a screen for each thing you can change:

- **Metadata**: title, year, manufacturer, developer, genre, players, rating,
  score and clone of. Genre, players and rating are choices of the values the
  system's games already have (numbers in order), with **None** then **Other...** first to type
  one it doesn't. Changes are saved in `manage/metadata/<type>.xml` (the
  **Saved in** row), so a content update doesn't undo them; each changed field
  has **Put back the original title** (year, genre...), which returns it to
  the shipped value.
- **Launcher**: another installed launcher for this game only, or the
  system's (which removes `collections/<system>/launchers/<game>.conf`, the
  **Saved in** file). When the game won't start, one line says why, and each
  launcher name it asks for that isn't defined gets an **Add a launcher called
  <name>** link, which opens Emulators' Add a launcher with the name filled in.
- **RetroArch** (only for a game whose launcher runs RetroArch): see
  [RetroArch settings](#retroarch-settings).
- **Media**: every kind of art, video and text in two lists, **Shown** (with
  thumbnails of pictures, and whether it's the game's own file or folder or a
  fallback from its title, its parent or the collection's default) and
  **Missing**. Replace one from a file (the slot's **Saved in** row says
  where), clear the game's own file (a fallback can't be cleared), or put back
  what it had before. A slot can also be a [folder of files](#media-folders).
- **Playlists and collections**: a **Favourite** switch (it adds or removes
  the favourite straight away and says so under it), add to or take out of a
  playlist, and show it in a merged collection or take it out (`<system>.sub`).
  When the game's pages have no playlist yet, **New playlist...** opens the
  system's Playlists screen to make one.
- **Description**: the story text (`medium_artwork/story/<game>.txt`, the
  **Saved in** row) in a multi-line field, or replaced from a file. Lines you
  didn't change keep their exact bytes and line endings; new lines get the
  file's own.
- **Saves**: the game's save files and save states, for games that start in
  RetroArch (other emulators keep saves their own way, so those say **Not
  known for this emulator**). It shows where RetroArch keeps them, worked out
  from `retroarch.cfg` as RetroArch does (`savefile_directory`,
  `savestate_directory`, saving beside the game, a folder per content folder
  and per core), and lists the game's files: exactly its name with a save or
  state ending (`.srm`, `.state`, `.state1`..., `.state.auto`, their `.png`
  pictures, `.rtc`...), never another game's (on Windows, where `Sonic.srm`
  and `sonic.srm` are one file, ignoring case), as one **This game's files**
  link ("7 files") so the backups come next. **Back up now** copies them to
  `manage/saves/<system>/<game>/<date-time>/` (the Backups' **Saved in**
  row); **Delete these saves** backs
  them up first, then deletes them. Each backup opens on its own screen with
  its files: **Restore this backup** backs up what's there now, then puts the
  backup's files back (so the game has exactly those), and **Delete this
  backup** removes it. When another system's game of the same name shares the
  files, the screen says so.

The game's screen also has:

- **Start this game**: closes Manage and starts the game the way its system's
  page does: its own launcher choice included, and last played (the system's
  `playlists/lastplayed.txt`), play count and time played kept as for any
  launch. When changes made in Manage are waiting, RetroFE reloads first, so
  the game starts with them; while a Catalog install is running it can't
  reload, so the button says to wait. A missing program is pointed out
  before starting. When the game exits, Manage opens again on the game,
  saying why if it couldn't start. With unsaved changes on an open screen,
  the first press says so and the second leaves them and starts the game.
- **Hide this game**: adds it to its system's exclude list (`exclude.txt`,
  or the file `list.excludeFile` names), so RetroFE stops listing it. Hidden
  games are found with **Only hidden games** in Games' Filters. Their screen
  starts with **Hidden from RetroFE** and the list that names them, keeps the
  Metadata, Media and Description links (so a title or art can be fixed
  without showing the game first), and has **Show this game again**, which
  takes them out of the list. Launcher, Playlists, Saves and Start need the
  game listed.

Every change keeps the original in `manage/backups/` first, and RetroFE shows
it after it reloads, when you close Manage.

#### Media folders

OneSauce keeps some art as a folder of interchangeable files where one file
would be: `system_artwork/video/` holding `ARCADES 1.mp4` to `ARCADES 9.mp4`
instead of a `video.mp4`. RetroFE shows one of them at random (removePlaylist,
B on OneSauce's controls, moves to the next; see
[ONESAUCE-COMPAT.md](ONESAUCE-COMPAT.md)). Every media screen (a game's Media,
a system's or video collection's Media, a menu's artwork) shows such a slot
as **a folder of N files (one shown at random)**, says where the folder is,
and lists its files. From there:

- **Add it to the folder** copies the file chosen in the file field into the
  folder under its own name (a name that's taken is refused).
- Choosing a file opens it, with **Take it out of the folder** and **Keep only
  this file**, which copies it beside the folder as the slot's one file
  (`video.mp4`) and empties the folder.
- **Clear the folder** takes all its files out. A folder left empty is
  removed; other files in it (a README) stay.
- **Put back the original** puts the folder's files back as they were before
  Manage changed them, and removes a file Manage put beside it.

Files taken out are kept in `manage/backups`. A file beside a folder wins
(as in RetroFE): the screen says so and marks the folder's files "not shown".
**Use this file** puts the new file beside the folder, where it wins, and
leaves the folder as it is; **Clear** removes only a single file, never a
folder (clearing a file that wins over a folder shows the folder again).
Folders count as art the slot has. Folders aren't used for text (a story),
or in a game's own ROM folder. Menus (the scrolling list) use folders too.
A game named `default` has the collection's `default/` folder as its own,
which every game without art of its own shows, so its screen says so and
**Clear the folder** and **Keep only this file** are refused there.

**Systems** and **Videos** are described [below](#systems-and-videos).
On a bare install Manage opens by itself and says where to start.

### Menus

**Manage → Menus** shows the menus you browse, starting with the one
**Browse** opens (`firstCollection`; "Change what Browse opens", marked **In
Settings**, goes there). Each row is indented under the menu that lists it and
says in a few words how many entries or games it has ("3 entries", "622 games",
"merged, 40 games"; the top row also says "Browse").
Entries that name no folder, are spelt differently from their folder (RetroFE
then finds the folder but not its settings) or lead back up (a loop) say so.
Menus and merged collections no menu reaches are listed below the tree.
Search shows matches with their path ("Main > Consoles > SNES").

Choosing one shows its details (folder, kind, the menus that list it, the
systems a merged collection shows, which theme page draws it) and links to:

- **Entries** (menus): the entries in order. Choose one to move it up or
  down, open it, remove it from the menu (the collection stays), or rewrite a
  wrongly-cased entry with the folder's spelling. Add any collection that isn't
  listed yet and wouldn't make a loop. Menus that are a `menu/` folder are
  sorted by name by RetroFE, so new entries go in name order and can't be
  moved. `menu_supported.txt` (RetroFE Plus) is kept in step when it's there
  (said on the menu's details, not on every screen).
- **Sources** (merged collections): one `.sub` file per system, with what it
  shows. Adding a system writes an empty `.sub`, which shows all its games;
  removing one keeps the file in the backups. To show only some of a system's
  games, take games out on each game's **Playlists and collections** screen
  in Games.
- **Info and artwork**: title, manufacturer, year and genre (`info.conf`,
  Save/Undo), and the collection's own artwork in `system_artwork` (logo,
  marquee, device, LED marquee, video, story and any others there): preview,
  replace from a file, clear, or put back the original. When the menu that
  lists it has its own art for the entry, that's what Browse shows, and the
  screen says so. Art kept as a [folder of files](#media-folders) is listed
  and changed file by file.
- **Actions**: a new menu or merged collection inside this one, and Delete.
  Only menus and merged collections that hold nothing but menus, sources,
  settings and artwork can be deleted, never the menu Browse opens. Their
  entries move up into the menus that listed them, in their place; a merged
  collection's games stay in their systems; every file goes to
  `manage/backups`.

**New menu or merged collection** (on Menus, or a menu's Actions) makes
`collections/<name>/` with `settings.conf`, `info.conf` and an empty
`menu.txt`, or a `.sub` showing all of one system's games, and lists it in the
menu you pick. Changes show in RetroFE once you leave Manage and it reloads.
Not here yet: changing several at once, playlists on menus, `cyclePlaylist`.

Settings writes each value to the file RetroFE actually reads it from:
`settings.conf`, `settings1.conf` to `settings15.conf`, or
`settings_saved.conf`. It only changes that line, keeping comments and
layout. Values set in an override file say so ("Set in settings1.conf").

### Systems and Videos

**Systems** lists every collection with games of its own: search by name or
title, or pick a menu to see only the systems below it. Each row shows the
game count and the launcher. **Add a system** asks for a name, the menu to
list it in, the games folder (with a count of matching files, and a warning
when files differ only in the case of their extension), the extensions, and
either an installed launcher or a new emulator program (written as the
system's own `launcher.<os>.conf`, as before).

A system's screen shows the menus it's in, its games folder and extensions,
with a link to each part (Games shows its count; the Launcher link names the
launcher and carries the warning when there's none or it isn't installed):

- **Games** opens the Games screen for this system.
- **Settings**: extensions (tidied: no dots, no duplicates, case kept,
  because RetroFE compares it), sort by title, show missing games, and the
  metadata file. Folders are shown, never changed. Save writes only these
  keys, to the file each comes from, in that file's own style (`true`/`yes`).
- **Launcher**: the default launcher, from the installed ones. A system's own
  local launcher is offered as `<system>.<name>`, the name RetroFE looks it up
  by. A RetroFE Plus list (`launcher = a, b, c`) keeps its alternatives: the
  one you pick moves to the front. When the system has its own launcher file,
  the screen says that file decides.
- **RetroArch** (only when its default launcher runs RetroArch): the
  core-wide settings, core options and controls of that core; see
  [RetroArch settings](#retroarch-settings).
- **Info**: title, manufacturer, year and genre in `info.conf`; an emptied
  field is taken out. "This is a video collection" writes `type = videos`,
  which moves it to Videos.
- **Media**: the system's own art in `system_artwork` (logo, marquee,
  device, fanart, video, story and any other file there), each with a
  preview. Replace one from a file, clear it (only its own file, never art
  shown from elsewhere), or put back what it had before Manage changed it.
  A [folder of files](#media-folders) in `system_artwork` (OneSauce's
  `video/`) is a slot too, with its files listed.
- **Playlists**: create, rename and delete playlists, and add or take out
  games. `all`, `favorites` and `lastplayed` (any case) are RetroFE's own and
  are shown but not changed. RetroFE shows only playlists with games in them
  and, when `cyclePlaylist` is set, only the ones it names: a playlist screen
  says so and can add it to the system's `cyclePlaylist`. A playlist named
  like a sort field (`year`, `playCount`...) is sorted by it.
- **Check this system**: a one-line summary, then **Problems**, as
  sentences, for a system no menu lists, a games folder that isn't there, no
  files with its extensions (including ones that differ only in case),
  include and exclude lists that leave nothing, a missing or uninstalled
  launcher or its missing program, and no metadata; then **Notes**, which
  say how this fork reads a setting (a OneSauce key it honours, a launcher
  list) and aren't counted as problems. It never changes anything.
- **Delete this system**, under **Actions**, appears only for a system that is empty or was added
  here, isn't a menu or merged collection, isn't listed by a `menu.xml`, whose
  games folder isn't inside its own folder, and whose folder holds only
  settings, lists and art. It asks first, moves the whole folder to
  `manage/backups/deleted/<name> <time>`, then takes it out of its menus (if a
  menu can't be changed, everything is put back); its games aren't touched.

Settings, Launcher and Info each have a **Saved in** row naming the file they
write. Every change keeps the original file in `manage/backups` first and
takes effect when RetroFE reloads, after you close Manage.

**Videos** is the same for **video collections**: systems whose files are
videos, marked with `type = videos` in their `info.conf`. They're listed
under Videos, not Systems, and each row says how its videos play.

- **Plays in RetroFE** (`play = here` in `info.conf`): selecting a video in
  Browse plays it full screen inside RetroFE. Select pauses and resumes,
  Left and Right go to the previous or next video, Up and Down seek, the skip
  keys jump a minute, and Back returns to the list on the video you were
  watching. When a video ends the next one starts; after the last one you're
  back on the list. A video that doesn't start within 10 seconds (a file the
  player can't open) is skipped. Each video that starts counts as played in
  last played and the play count, as a launched game does (RetroFE keeps those
  only when `lastPlayedSkipCollection` is set). Attract mode doesn't start
  these videos. Sound follows RetroFE's video settings (`MuteVideo`, and
  the layout's `volume`). RetroFE's player opens mp4, avi, mkv, mp3, wav and
  flac files.
- **Plays with an external player** (`play = launcher`, or no `play` line, as
  for video collections made before): its launcher starts, e.g. VLC or mpv.

**Add a video collection** starts with the extensions `mp4,mkv,avi` and
**Plays: In RetroFE**, which needs no player program (file types RetroFE
can't play are refused there). Choose **With an external player** to pick the
launcher `VideoPlayer` or, when there's none yet, the player program (VLC or
mpv get their usual full-screen arguments); Manage then writes
`launchers.<os>/VideoPlayer.conf`.

A video collection's **Playback** screen (its Launcher screen) switches
between the two. Switching to an external player needs an installed player;
switching to RetroFE keeps the launcher for later. Its **Check** says which
extensions RetroFE can't play when it plays them itself.

In Browse a video collection's page shows its list and the selected video
playing beside it: Aeon Nox draws it with `collections/_videos/layout`, and
the player with `player.xml` there (`player 4x3.xml` for 4:3), and RetroFE has
built-in pages when no layout is installed. A layout can give one collection
its own pages in `collections/<name>/layout/` (`layout.xml`, `player.xml`).

### Emulators

**Manage → Emulators** starts with **Add a launcher**, then lists every
launcher (the global ones in `launchers.<os>`, a system's own launcher file, a
system's local launchers) with where it's defined, its program (flagged when
missing) and how many systems and games use it, then the names systems or
games ask for that no launcher defines: listed there when there are up to
ten, else one **Used but not defined** link with their count (OneSauce asks
for over a hundred) to a screen listing them. Each opens Add a launcher with
the name filled in. **Add a launcher** asks for a name, the program, the
arguments (the game's file, RetroArch with a core, MAME, or your own), the
working folder and whether RetroFE restarts afterwards.

A launcher's screen shows its program, arguments and working folder, with:

- **Used by**: the systems and games that use it, each opening its screen.
- **Edit**: change the program, the arguments (from the same presets, or your
  own text), and for global launchers the working folder and whether RetroFE
  restarts after it. The screen says who the change reaches, and that it
  applies after RetroFE reloads, when you close Manage (RetroFE reads launcher
  files when it loads). **Save** changes only those lines of the launcher's
  file (the **Saved in** row), keeping its comments and any other
  settings; the program must be there, and arguments may only use the
  variables RetroFE fills in (`%ITEM_FILEPATH%`, `%ITEM_NAME%`,
  `%ITEM_DIRECTORY%`, `%RETROFE_PATH%`...; for the command prompt or a batch
  file its own, such as `%APPDATA%`, too). **Put back the original** returns
  the file to what it was before Manage first changed it; a launcher made in
  Manage (added, or renamed) has no such copy, so it isn't offered. Only this
  platform's `launchers.<os>` file changes, and the screen says when another
  platform has one of the same name.
- **RetroArch** (only when it runs RetroArch): the core-wide settings, core
  options and controls of its core, as on a system's screen; see
  [RetroArch settings](#retroarch-settings).
- **The file**: the launcher's file as it is on disk.
- **Rename** (global launchers): type the new name to see every file that
  changes (its file in each `launchers.<os>` folder, the systems' `launcher =`
  lines, the games set to it), then **Rename**. If a step fails, everything is
  put back.
- **Duplicate and edit** and **Delete launcher** (only when nothing uses it).

Changes keep the original in `manage/backups` and take effect when RetroFE
reloads, after you close Manage.

### RetroArch settings

A game or system whose launcher runs RetroArch, and such a launcher on
Emulators, gets a **RetroArch** link.
A launcher runs RetroArch when its program is `retroarch` (`retroarch.exe`),
when its arguments load a core (`-L <core>`, `--libretro <core>`), or when it
hands RetroArch a `*_libretro` core through another program (a script, the
RetroArch flatpak). The link's status names the core, or says why there's
nothing to change: **RetroArch not found** (its program, or its
`retroarch.cfg`, isn't there) or **Core not known**.

Manage finds RetroArch's own settings file the way RetroArch does: the
launcher's `--config` (`-c`) file, else `retroarch.cfg` beside the program,
else `%APPDATA%\RetroArch` on Windows, `~/.config/retroarch` on Linux
(`~/.var/app/org.libretro.RetroArch/...` for the flatpak) and
`~/Library/Application Support/RetroArch` on macOS. From it come the folders
RetroArch keeps per-core files in (`rgui_config_directory`,
`input_remapping_directory`; `:` is RetroArch's own folder, as RetroArch
writes it). Each core's files live in a folder named after the core's
**library name** (`Genesis Plus GX`), which Manage takes from its list of
known cores, else from the core's `.info` file; a name from the info file
that RetroArch hasn't confirmed yet (by making that folder) is flagged.

A **game's** RetroArch screen shows the core and launcher and:

- **RetroArch settings**: aspect ratio, smooth picture, shader, bezel
  (overlay), integer scaling, run-ahead, volume and "resume where you left
  off" (saving and loading the game's state automatically). Each row offers
  **Default (…)**, naming what applies without the game's own setting, and a
  few fixed choices. Save writes only the changed keys to
  `<config>/<Core>/<game>.cfg`, which RetroArch reads on top of the core's
  `<Core>.cfg` and `retroarch.cfg`; a file left with nothing that differs is
  removed.
- **Core options**: the core's options Manage knows (a few per core: region,
  sprite limit, frameskip...). Choose one to change it. RetroArch reads one
  options file per game, so a game's own `<game>.opt` starts as a copy of the
  core's `<Core>.opt` (or, while the core has none, of its entries in
  RetroArch's all-cores `retroarch-core-options.cfg`), and is removed again
  when it ends up the same. A game whose name can't be a file name, or is the
  core's own name, can't have settings of its own, and its screen says so.
- **Controls**: whether the stick moves the D-pad, and what each of player
  1's buttons sends (another button, or nothing), in
  `<remaps>/<Core>/<game>.rmp`, with the same one-file rule.
- **Use the system's settings** removes the game's own file for that screen
  (it's kept in `manage/backups`), and **Put back the original** puts back the
  file as it was before Manage first changed it. Each screen's **Saved in** row
  names the file it writes; **Undo changes** drops edits not saved yet.

A **system's** (or a launcher's) RetroArch screen edits the core-wide `<Core>.cfg`, `.opt` and
`.rmp` instead, and says which systems it reaches: every system whose
launcher runs that core (and any game set to such a launcher), unless a game
has a file of its own. RetroArch reads these files each time it starts a
game, so nothing needs reloading. When `retroarch.cfg` turns off
`auto_overrides_enable`, `game_specific_options` or `auto_remaps_enable`, the
screen says those files won't be used.

### Themes

**Manage > Themes** lists the layouts in `layouts/`, each with what it is:
which one RetroFE uses, its size and orientation, whether it was made with
the theme editor or by hand, and which pages it has (the main page, other
main pages such as `layout 4x3`, a splash page, pages for particular
collections). Choosing one shows a picture of it (its `preview.png`, else the
first picture its splash page shows, else its main page's background), the
details and what you can do with it:

| Button | What it does |
|--------|--------------|
| Use this theme | Sets `layout` in your settings; RetroFE switches to it after it reloads, when you close Manage. Not for a theme without `splash.xml` (RetroFE wouldn't start); a theme made with the editor needs a wheel too. |
| Edit this theme | Opens the [theme editor](THEME_EDITOR.md): its widgets for an editor-made theme; for a hand-made one, the [layout editor](THEME_EDITOR.md#hand-made-themes-the-layout-editor), which changes the elements its pages already have in place. |
| Duplicate this theme | Copies the theme as "*name* Copy". Editor-made themes only. |
| Delete this theme | Under Actions. Deletes the theme's folder, after a second Select. Editor-made themes only, and not the one in use. |

**New theme** at the top of the list makes an editor-made theme: give it a
name and choose landscape (1920 x 1080) or portrait (1080 x 1920). It starts
empty; Edit it to place a wheel, titles, art and video.

Hand-made themes, Aeon Nox included, can be used and edited but not
duplicated or deleted here. Their details say how Edit works for them: the
layout editor moves, resizes and changes the attributes of a page's
elements, copies, deletes and adds them, and edits their animations,
rewriting only what changes. The first save of a page keeps its
original in `manage/backups/layouts/<theme>/`, and *Put back the original*
restores it.

### Controls

**Settings > Controls** lists every control RetroFE reads from
`controls.conf` (see [CONTROLS.md](CONTROLS.md)), grouped as Essential,
Navigation, Playlists and favourites, Admin, Advanced, Combos and Jukebox,
each with its inputs in short words ("Key Return, Pad A, D-pad up, Joy
button 0 (unmapped)": unmapped means only on joysticks SDL has no controller
mapping for, such as arcade encoders), plus the dead zone for sticks and
triggers. A control's own screen gives each input in full. A line at the top points out inputs RetroFE doesn't
recognise (it skips them).

Choosing a control shows its inputs, one per row (with the token as
`controls.conf` writes it), where its value comes from, and which other
controls share an input. From there:

- **Add an input (press it now)**: then press the key, joystick or
  controller button, d-pad, stick or trigger within 5 seconds. It's added when
  you let go. Every input can be added, Escape and Back's own included, so
  the ways out are to press nothing for 5 seconds or to hold an input down
  for 2 seconds. A pad SDL knows as a game controller is added by its
  controller name (`joyA`, `joyDPUp`, `joyLeftY-`), which means the same
  button on any pad; other joysticks by number (`joyButton3`, `joyHat0Up`,
  `joyAxis1-`). Inputs are added for any pad (no `joy0`). The mouse isn't
  captured. Open Settings takes keyboard keys only; a combo takes two inputs.
  An input another control already uses is added with a warning (it then
  does both), also when it's the same button under another name on a pad
  connected now (`joyA` and `joyButton1` on a DualShock read through
  DirectInput, from SDL's mapping). A key whose name has a comma
  (`Keypad ,`) can't be added: `controls.conf` couldn't hold it.
- Choosing an input opens it, with **Remove this input**.
- **Put back the default**: RetroFE's built-in inputs for the control
  (Return, A on a pad, and button 0 on a joystick SDL has no mapping for,
  for Select; F10 for Open Settings), or none for controls that have no
  default.

Changes to every control are kept together until **Save** (on the list or a
control's screen); **Undo changes** drops them, and leaving the list with
changes asks first (and closing the window after that warns again before
it quits). Save refuses a change that leaves Up, Down, Select or
Back with no input that works, so you can't lock yourself out; a control
already left empty in the file (`back =` on a kiosk) doesn't hold up other
changes. Open Settings without a keyboard key is F10, and the screen says so. Each value
is written where it comes from now: the `controlsN.conf` that sets it (the
last one wins, as RetroFE reads them), otherwise `controls.conf` (made from
the built-in controls when there isn't one). Only that line changes;
comments, order and line endings stay, and the original file is kept in
`manage/backups/` before the first change. RetroFE reads its controls again
straight after Save, so the new inputs work at once, in Manage too (the page
shown keeps its layout's `controls - <type>.conf` on top). The screens show
`controls.conf` and `controls1..9.conf`, not a layout's own file.

### Styling the screens

A screen is drawn as a page from your layout, so it can use backgrounds,
images, video, text and animations like any other page. RetroFE looks for:

1. `layouts/<layout>/forms/<screen>/layout.xml` for one screen, by form id:
   `settings`, `manage`, `sources`, `package`, `games`, `menus`, `systems`,
   `videos`, `emulators`, `themes`, or any screen those open
2. `layouts/<layout>/forms/layout.xml` for all of them
3. otherwise its built-in form page

When collections are drawn with another page, such as `layout 4x3` from
`randomLayout = layout 4x3`, RetroFE first looks for
`forms/<screen>/layout 4x3.xml`, then `forms/layout 4x3.xml`, then the two
`layout.xml` files above. The keyboard does the same in `forms/keyboard/`.

The page places the form with a `<form>` element:

```xml
<layout width="1920" height="1080" font="fonts/standard.ttf" loadFontSize="40">
  <image src="images/settings_background.png" x="0" y="0" width="1920" height="1080"/>
  <form x="center" y="center" xOrigin="center" yOrigin="center" width="1200" height="900"
        fontSize="32" fontColor="c8c8c8" focusColor="ffffff"
        rowHeight="50" labelWidth="400"
        focusBackgroundColor="3d6fb6" focusBackgroundAlpha="0.55"
        backgroundColor="14181e" backgroundAlpha="0.9" layer="2"/>
</layout>
```

| Attribute | Meaning |
|-----------|---------|
| `x`, `y`, `width`, `height`, `xOrigin`, `yOrigin`, `layer`, `alpha`, `monitor` | As for other components |
| `font`, `fontSize`, `fontColor`, `loadFontSize` | Text, as for `<text>` |
| `focusColor` | Text color of the focused row (default `ffffff`) |
| `rowHeight` | Height of each row (default 1.6 x `fontSize`) |
| `labelWidth` | Width of the label column (default 40% of the width) |
| `focusBackgroundColor`, `focusBackgroundAlpha` | Highlight behind the focused row |
| `focusImage` | Image drawn behind the focused row instead, relative to the page's folder |
| `backgroundColor`, `backgroundAlpha` | Panel behind the form |
| `noteAlpha` | Opacity of help text and notes (default 0.55) |
| `disabledAlpha` | Opacity of fields that can't be changed right now (default 0.35) |
| `onText`, `offText` | How switches read (default `On` / `Off`) |
| `rowDetailColor`, `rowDetailAlpha` | Color and opacity of a result's detail on the right, and of a link's status and marker (default `fontColor` at 0.7) |
| `linkMarker` | Text at the right of a link (default `>`; empty for none) |
| `headerColor`, `headerAlpha` | Section headers and results' labels (default `fontColor` at 0.7) |
| `separatorColor`, `separatorAlpha` | A thin line under section headers and between the rows of a results list (default alpha 0: none) |
| `imageBackgroundColor`, `imageBackgroundAlpha` | Backing behind pictures and results' thumbnail boxes (default alpha 0: none) |
| `statusColor` | The message an action that did something leaves above the hints (default `fontColor`) |
| `infoColor` | The message when nothing happened (cancelled, undone, nothing to do) and "press it now" prompts (default `fontColor`) |
| `errorColor` | Error messages, and the question of a button waiting for confirmation (default `fontColor`) |
| `confirmBackgroundColor`, `confirmBackgroundAlpha` | Tint over a button waiting for a second Select to confirm (default alpha 0: none) |
| `title` | `no` hides the screen's title |
| `titleDetail` | A value drawn at the right of the title row, smaller, in `infoColor`; nothing when it's empty. `titleDetail="info:pendingChanges"` shows the changes waiting for a reload ("2 changes apply when you close Manage"), as the built-in page and Aeon Nox do. Default: none. |
| `hints` | `no` hides the controls hint at the bottom |

Text that doesn't fit its column ends in `...`; a result's detail gets the
room it needs, up to about half the row when the name needs the rest. Long
notes and values wrap, breaking long paths where they reach the edge.

Set `formsRenderer = imgui` in `settings.conf` to draw the screens with Dear
ImGui instead, for example while developing.

### The on-screen keyboard

Editing a text, number or path field shows an on-screen keyboard, so you can
type with a gamepad or arcade controls: the directions move between keys,
Select presses one, Back cancels. Shift switches to capitals and symbols, and
number fields get a number pad. A one-line field has a row for paths and
addresses: `.` `/` `\` `:` `-` `_` (whatever Shift says) and the cursor keys
`<-`, `->`, **Home** and **End**, so the middle of a long value (a launcher's
arguments) can be changed; the box at the top shows the cursor (`_`) where it
is. A multi-line field adds a **New line** key and a row of cursor keys (`<-`,
`->`, **Line up**, **Line down**); **Done** keeps the text, and the box at the
top shows the line the cursor is on. RetroFE's letterUp and letterDown
controls move the cursor left and right on any keyboard. Each keyboard starts
on the key you last typed with on one of its kind (letters, lines, numbers),
only letters, digits, symbols and Space, until RetroFE quits. Typing on a real keyboard puts the
on-screen one away for that edit; finish with Enter (Ctrl+Enter in a
multi-line field) or cancel with Escape. Set `onScreenKeyboard = no` to never
show it. The theme editor draws its own keyboard with the same keys (it isn't
styled by the layout).

A layout styles it with `layouts/<layout>/forms/keyboard/layout.xml`, a page
with a `<keyboard>` element; it's drawn over the screen being edited:

```xml
<layout width="1920" height="1080" font="fonts/standard.ttf" loadFontSize="40">
  <keyboard x="center" y="1050" xOrigin="center" yOrigin="bottom" width="1400" height="660"
            fontSize="36" fontColor="c8c8c8" focusColor="ffffff"
            keyColor="ffffff" keyAlpha="0.07" focusKeyColor="3d6fb6" focusKeyAlpha="0.75"
            backgroundColor="0d1014" backgroundAlpha="1" layer="1"/>
</layout>
```

| Attribute | Meaning |
|-----------|---------|
| `x`, `y`, `width`, `height`, `xOrigin`, `yOrigin`, `layer`, `alpha`, `monitor` | As for other components |
| `font`, `fontSize`, `fontColor`, `loadFontSize` | Text, as for `<text>` |
| `focusColor` | Text color of the focused key (default `ffffff`) |
| `keyColor`, `keyAlpha` | Key background (default white at 0.07) |
| `focusKeyColor`, `focusKeyAlpha` | Focused key background |
| `keyImage`, `focusKeyImage` | Images drawn for keys instead, relative to the page's folder |
| `keySpacing` | Gap between keys (default a quarter of `fontSize`) |
| `backgroundColor`, `backgroundAlpha` | Panel behind the keyboard |
| `noteAlpha` | Opacity of the field name and hints (default 0.55) |
| `hints` | `no` hides the controls hint at the bottom |

### Changing what a screen contains

The fields themselves are also XML. RetroFE's built-in screens are in
`RetroFE/Source/Forms/BuiltinForms.cpp` and, for the Manage sections,
`RetroFE/Source/Manager/<Section>/<Section>Forms.cpp`; a layout can replace
one with `layouts/<layout>/forms/<screen>.xml`:

```xml
<form id="settings" title="Settings">
  <section label="Display">
    <toggle label="Fullscreen" bind="settings:fullscreen"/>
    <text label="Width" bind="settings:horizontal" hint="pixels, or stretch" enabledWhen="!settings:fullscreen"/>
  </section>
  <actions>
    <button label="Save" action="settings.save"/>
  </actions>
</form>
```

| Element | Field |
|---------|-------|
| `section` | A titled group of fields |
| `actions` | Buttons |
| `text`, `folder`, `file` | Text, a folder path, or a file path. Select on a `folder` or `file` opens the [file browser](#choosing-a-folder-or-a-file): `filter` names the kind of file ("Programs") and `pattern` (`*.exe;*.bat`) which files it lists; `start` is a value giving the folder it opens in when the field is empty (a folder, or a slot's place like `.../logo/Sonic.*`, e.g. `start="game:slot:place"`); `matching` on a `folder` is a value listing extensions (`zip,7z`) whose files each folder counts, e.g. `matching="system:addExtensions"`. `live="yes"` on `text` passes every change on while typing, e.g. for a search box. `multiline="yes"` makes `text` several lines, `rows` high (default 4), wrapped at spaces, with "..." when there's more than fits; the value keeps a `\n` between lines. |
| `toggle` | On or off |
| `number` | A whole number (`min`, `max`) |
| `choice` | One of a list (`options` is `layouts`, `collections`, `sources`, `sourceTypes`, `installed` or `screenSizes`) |
| `list` | A read-only list (`empty` is shown when there's nothing in it) |
| `note` | Text: `value`, or with `bind` a value from RetroFE; hidden when empty |
| `value` | A label and a read-only value, e.g. `<value label="Year" bind="game:year"/>`; hidden when empty |
| `image` | A picture whose file `bind` gives, `rows` high (default 4), keeping its shape; hidden when there's no file or it can't be loaded |
| `button` | Runs an `action`. With `confirm="Delete this game?"` the first Select asks that, and a second one runs it; anything else cancels. |
| `link` | Opens the screen `open` (a form id), with this screen's parameters; `status` is shown on the right |
| `results` | Rows to choose from, e.g. games matching a search. Select opens the screen `open` for that row: its id as the parameter `param` (default `id`), plus whatever else the row needs to find its record. `limit` rows at most (default 200), with a note saying how many there are in all; `empty` is shown when there are none. Left/Right page through them, or with `jump="letter"` jump to the previous or next letter; letterUp/letterDown do that in any list. A row that stands for a screen to go to rather than a record ("Add a launcher called lr-stella2014", a system's "Games  622 games") is drawn with the link marker, like a `link`. |

Every field can have `label`, `hint` (shown while empty), `help` (shown
under it), `status` (a value shown under it), `enabledWhen` (a value that must
be on, or `!value` for off; the field is greyed out otherwise), `visibleWhen`
(the same, but the field is hidden) and `default`. On `<form>`, `bind` gives
a value to use as the title when it has one, e.g. `bind="package:name"`.

#### Screens and parameters

A screen can open other screens: a `link`, a row of `results`, or an action.
Each opened screen goes on top of the one it came from, and Back returns.
Screens can have **parameters**: choosing a row from
`<results bind="game:search" open="game"/>` opens the `game` form with
`id` (and, say, `collection`) set to that game, and a `link` from there
passes them on. A row opens only its own parameters, never the current
screen's, so nothing carries over from an unrelated screen. Forms read them
as `param:<name>`, e.g. `<value label="Id" bind="param:id"/>`.

A screen with unsaved changes can ask before you leave it (Back, Escape,
F10, the Close button, or closing RetroFE's window): it says so once, and
leaving the same way again drops the changes. Each way of leaving gets its
own warning first: after Back warned, F10 or a window close warns again
rather than leaving, and a window close says that closing it again quits
("... or close the window again to quit and discard them."). Moving on the
screen in between starts over.

The message an action leaves is green when it did something, red (with a
`!`) for an error or a warning, and plain when nothing happened: a capture
cancelled or timed out, Undo changes, Reset, "is already ..." (the layout's
`infoColor`, below).
Removing a package or a source asks you to confirm first.

`bind` says what a field shows and changes:

- `settings:<key>`: a setting from `settings.conf`. Changes are kept until
  `settings.save`. `default` is the value RetroFE uses when no file sets it.
  `settings:screenSize` is `horizontal` and `vertical` as one choice
  (`options="screenSizes"`: `stretch`, `1920x1080`..., `custom`);
  `info:customScreenSize` is on while it's `custom`.
- `draft:<key>`: input for an action, such as a new system's name.
  `default` is its starting value.
- `param:<name>`: the screen's parameters (read-only).
- `info:<key>`: read-only: `info:collections`, `info:sources`,
  `info:installed` and `info:catalogWarnings` (lists; `info:sources` and
  `info:installed` are also `results`, a row per source or package, opening
  `source` or `package`), `info:romCount`, `info:catalogStatus`,
  `info:removalStatus`, `info:pendingChanges` (the changes waiting for a
  reload, or nothing), and `info:hasCollections` (counting systems added since
  Manage opened), `info:hasSources`, `info:hasInstalled`,
  `info:sourceIsFolder`, `info:hasPendingChanges` and `info:canOpenCatalog`
  for `visibleWhen`.
- `package:<key>`: the Catalog package the screen is about (`param:id`), also
  one that's installed but no source offers any more:
  `name`, `summary`, `description`, `installedStatus`, `requires`, `checksum`
  (a warning when the source gives none), `progress`, and `canInstall`,
  `canUpdate`, `canRemove`, `canShow` (an installed collection),
  `isInstalled` and `isBusy` for `visibleWhen`.
- `source:<key>`: the source the screen is about (`param:id`, its name):
  `name`, `type`, `location`, `status` (when it was read and how that went),
  the list `source:problems` and the rows `source:packages`.
- Other prefixes, such as `game:`, come from the Manage sections that provide
  them; see [design/manage-forms.md](design/manage-forms.md).

Actions:

| Action | Does |
|--------|------|
| `settings.save` | Writes the changed settings |
| `collection.create` | Creates a system (collection) from `draft:name`, `draft:romPath`, `draft:extensions`, `draft:executable` and `draft:arguments` |
| `package.install`, `package.remove`, `package.cancel` | Install or update, remove, or stop the package the screen is about |
| `package.removeInstalled` | Removes the installed package `draft:removePackage` |
| `package.showInBrowse` | Reloads RetroFE, then opens the collection the package installed |
| `source.add` | Adds `draft:sourceName` of `draft:sourceType` at `draft:sourceUrl` or `draft:sourcePath`, once `draft:sourceTrusted` is on. A layout's form can't give these a `default`. |
| `source.remove` | Removes `draft:removeSource` |
| `source.removeThis` | Removes the source the screen is about, then goes back |
| `catalog.refresh` | Reads the sources again |
| `catalog.open` | Closes the screens and shows the Catalog list |
| `changes.apply` | Reloads RetroFE now, then opens this screen again |
| `open.<form>` | Opens a screen with this screen's parameters, like a `link`, e.g. `open.sources` |
| `back` | Returns to the previous screen, e.g. for a Done button |

### For developers: providers

Each Manage section serves its own bindings through a **provider**
(`RetroFE/Source/Manager/Provider.h`): a `game` provider answers `game:title`,
`game:search` rows, `options="game:genres"` and actions like `game.save`, and
gets the screen's parameters with every call. An action can open a screen or
go back once it succeeds, or ask for the next input the user presses
(`Result::capture`, as Settings > Controls does), a provider can object to
leaving a screen with unsaved changes, and it can ask RetroFE to reload once
the manager closes (which the "changes waiting" line counts). An action can
also name the field to focus afterwards (`Result::focus`, by its action, bind
or open; focused as soon as it shows), reload now and come back to a screen
(`Result::reload` with `open`), or close the screens and show a collection
(`Result::browse`).
How to add a section, its forms and its provider is in
[design/manage-forms.md](design/manage-forms.md).

## The default layout: Aeon Nox

RetroFE's default layout is **Aeon Nox**, David Horowitz's theme after Kodi's
Aeon Nox skin. The built-in settings RetroFE runs on when there's no
`settings.conf` say `layout = Aeon Nox`, and so does the `settings.conf` it
creates from them (ManagerModel's `initialSettings`, when Settings or Manage
first saves something). An existing `settings.conf` without a `layout` line
gets RetroFE's built-in pages, as before, and so does any install where the
`layouts/Aeon Nox` folder isn't there. `layouts/Aeon Nox/NOTICE.txt` says
where the theme comes from and what this fork changed. It styles everything
above, so it's a working example:

| File in `layouts/Aeon Nox/` | Draws |
|-----------------------------|-------|
| `layout.xml` | A collection's games: the information wall and box art. Every collection of games without a page of its own uses it. |
| `collections/_menu/layout/layout.xml` | Menus of collections, such as Browse: one logo per collection, or its name when there's no logo (see below) |
| `collections/_videos/layout/layout.xml` | Video collections (`type = videos`): the list, and the selected video playing beside it |
| `collections/_videos/layout/player.xml` | Where a video collection that plays in RetroFE plays a video, full screen |
| `collections/Home/layout/layout.xml` | Home, in the same style |
| `collections/Home/medium_artwork/` | Home's artwork and text: `logo/`, `icon/`, `tagline/` and `description/`, each with a file per item (`Catalog.png`, `Catalog.txt`...) |
| `collections/Catalog/layout/layout.xml` | The Catalog: package logos, with kind, source and version in the info panel |
| `collections/Catalog/medium_artwork/` | An icon and text for the Sources item (`Catalog Sources.png`, `.txt`), which has no package artwork |
| `forms/layout.xml` | Settings, Manage, Sources and the other screens |
| `forms/package/layout.xml` | A package: the form takes the top of the screen, so the Catalog's info panel stays visible below it |
| `forms/keyboard/layout.xml` | The on-screen keyboard |
| `layout 4x3.xml` next to each `layout.xml` | 4:3 versions, used with `randomLayout = layout 4x3` |

The pages are generated from upstream's theme by `Scripts/aeon-nox/`.

The wheels of menus, Home and the Catalog have `repeatItems="no"` on their
`<menu>`: with fewer items than the wheel has places, each is drawn once
around the selected one (Browse's Main menu with one system shows it once,
not five times). Any layout can use it; without it a short list repeats to
fill the menu, as RetroFE always did.

### Which page a collection gets

Every collection, Home and the Catalog included, is a page of its own. RetroFE
takes it from the first of these that the layout has:

1. `collections/<collection>/layout/`, the collection's own pages
2. `collections/_videos/layout/`, when the collection is a **video collection**
   (`type = videos` in its `info.conf`)
3. `collections/_menu/layout/`, when the collection is a **menu**: it has items
   and they're all collections (from `menu.txt` or `menu/`), like Main, a
   "Consoles" menu or Home. A menu that's still empty is drawn like a collection
   of games.
4. the layout's own folder

A video collection's own files are its `video` media (unless `media.video` is
set or it has a `medium_artwork/video` folder), so `<reloadableVideo
type="video">` shows the selected video. Its **player page** is `player.xml`
(or `player 4x3.xml` for the page `layout 4x3`) in its own folder, then in
`collections/_videos/layout/`, else RetroFE's built-in one. RetroFE gives the
player page the list the collection's page shows: it needs a `<menu>` (it can
be invisible, as in Aeon Nox's), and a `<reloadableVideo type="video"
jukebox="true">` so Select can pause it and RetroFE knows when it ends; a page
without either is replaced by the built-in one (log.txt says why). The list
page's sounds are off while a video plays, so give the player page its own
`<sound type="load">` for a sound as it opens (Aeon Nox plays `select.wav`). Texts
of type `title`, `collectionIndexSize`, `isPaused`, `current` and `duration`
follow the video playing, and `onHighlightEnter` runs when it changes.

The collection's `layoutFromAnotherCollection` setting still wins over all of
these. In each folder RetroFE uses `layout.xml`, or the page `randomLayout`
picked (`layout 4x3.xml`...), falling back to the layout's own folder for a
page the folder doesn't have.

Things worth knowing when you make your own:

- A page's list is at `menuIndex` 0, and a page starts with `onEnter`. Tweens
  that only use `onMenuEnter` for a deeper menu never run.
- Home's items and the Sources item are built in, so their artwork comes from the
  layout: `mode="layout"` on a `<menu>`, `reloadableImage` or
  `reloadableScrollingText` reads
  `layouts/<layout>/collections/<collection>/medium_artwork/<type>/<item>`.
  `reloadableScrollingText` with `direction="vertical"` and `startPosition="0"`
  shows a text file as wrapped text that only scrolls when it doesn't fit.
- `src` paths are relative to the page's own folder. RetroFE falls back to the
  layout's folder but logs an error first, so pages in `collections/<name>/layout/`
  use `../../../images/...`. Fonts are always relative to the layout's folder.
- Form and keyboard pages have no selected item, so reloadable components show
  nothing there (not even `type="time"`). To show a package's artwork, leave part
  of the Catalog page visible behind the form, as `forms/package/layout.xml` does.
- A label that should only appear with its value can be the value's prefix
  (`singlePrefix` and `pluralPrefix`) padded with spaces and cut off by
  `maxWidth`; the Catalog's Kind, Source and Version and the games page's
  Publisher, Genre and the rest do this.
- Text stops at `maxWidth`, even mid-word, so leave names room: Aeon Nox draws
  names without a logo at 48 px in 460 px, its logos being 480 px apart.

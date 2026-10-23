# Home Menu, Catalog, Settings and Manage

RetroFE can start with nothing but its executable. With no `settings.conf`,
`controls.conf`, collections or layout it uses built-in defaults, and you set
everything else up from inside RetroFE.

## The Home menu

With `homeMenu = yes` RetroFE starts on **Home**:

| Item     | What it does |
|----------|--------------|
| Catalog  | Packages you can install from your sources (see [The Catalog](#the-catalog)) |
| Settings | Display, menu and video settings |
| Manage   | Games, menus, systems, videos, emulators and themes, plus what the Catalog installed and its sources |
| Browse   | Opens `firstCollection`, your usual top menu. Back returns to Home. |

`homeMenu` is on for a bare install and in the `settings.conf` the setup
creates. Existing installs don't have it, so they start on `firstCollection`
as before. Kiosk mode always skips Home.

F10 opens Settings from anywhere, switches to it from another screen, and
closes it again; change the key with `manager` in `controls.conf`. Back or
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
  **Remove** or **Stop** as they apply. Downloads continue in the background,
  so you can go back to the list and pick another; they run one at a time.
- Once you're back on Home with nothing left to do, RetroFE reloads so new
  content shows up. Installed collections are added to the Main menu, so they
  appear under Browse; removed ones are taken off it. Saving Settings while an
  install is running also waits for it before reloading.
- **Sources** (from the Catalog or Manage) adds and removes sources. Adding
  one asks you to confirm that you trust it: RetroFE doesn't check what sources
  offer. Plain `http://` is only allowed on your own network (localhost, a
  machine name like `nas`, or a private address), for indexes and downloads alike.
- **Manage** lists what's installed and can remove any of it, including
  packages no source offers any more.
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
or page through a list of results; Select edits text, picks a folder or file,
presses a button, or opens a link or a result. While editing text, type and
press Enter to keep it or Escape to cancel; Ctrl+V pastes.

**Manage** links to a screen for each part of your setup: Games, Menus,
Systems, Videos, Emulators and Themes, then the Catalog (what it installed,
its Sources, and removing a package).

**Games** searches every installed game by name or title, in one system or
merged collection or under a menu, optionally only games without metadata.
Choosing a game shows its facts and artwork, with a screen for each thing you
can change:

- **Metadata**: title, year, manufacturer, developer, genre, players, rating,
  score and clone of. Changes are kept in `manage/metadata/<type>.xml`, so a
  content update doesn't undo them; each changed field can be put back to the
  shipped value.
- **Launcher**: another installed launcher for this game only, or the
  system's (which removes `collections/<system>/launchers/<game>.conf`).
  Names that aren't installed are pointed out.
- **Media**: every kind of art, video and text, saying whether the game has its
  own file, shows a fallback (from its title, its parent or the collection's
  default) or has nothing. Replace one from a file, clear the game's own file
  (a fallback can't be cleared), or put back what it had before.
- **Playlists and collections**: favourite on or off, add to or take out of a
  playlist, and show it in a merged collection or take it out (`<system>.sub`).
- **Description**: the story text (`medium_artwork/story/<game>.txt`), edited
  on one line where ` | ` starts a new paragraph, or replaced from a file.

Every change keeps the original in `manage/backups/` first, and RetroFE shows
it after it reloads, when you leave Manage.

**Systems** and **Videos** are described [below](#systems-and-videos).
On a bare install Manage opens by itself and says where to start.

### Menus

**Manage → Menus** shows the menus you browse, starting with the one
**Browse** opens (`firstCollection`; "Change what Browse opens" goes to
Settings). Each row is indented under the menu that lists it and says what it
is (menu, merged collection, system) and how many entries or games it has.
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
  moved. `menu_supported.txt` (RetroFE Plus) is kept in step when it's there.
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
  screen says so.
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

A system's screen shows the menus it's in, its games folder, extensions and
the launcher in effect (and which file sets it), with a link to each part:

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
- **Info**: title, manufacturer, year and genre in `info.conf`; an emptied
  field is taken out. "This is a video collection" writes `type = videos`,
  which moves it to Videos.
- **Media**: the system's own art in `system_artwork` (logo, marquee,
  device, fanart, video, story and any other file there), each with a
  preview. Replace one from a file, clear it (only its own file, never art
  shown from elsewhere), or put back what it had before Manage changed it.
- **Playlists**: create, rename and delete playlists, and add or take out
  games. `all`, `favorites` and `lastplayed` (any case) are RetroFE's own and
  are shown but not changed. RetroFE shows only playlists with games in them
  and, when `cyclePlaylist` is set, only the ones it names: a playlist screen
  says so and can add it to the system's `cyclePlaylist`. A playlist named
  like a sort field (`year`, `playCount`...) is sorted by it.
- **Check this system**: warnings, as sentences, for a system no menu lists,
  a games folder that isn't there, no files with its extensions (including
  ones that differ only in case), include and exclude lists that leave
  nothing, a missing or uninstalled launcher or its missing program, and no
  metadata; then a one-line summary. It never changes anything.
- **Delete this system** appears only for a system that is empty or was added
  here, isn't a menu or merged collection, isn't listed by a `menu.xml`, whose
  games folder isn't inside its own folder, and whose folder holds only
  settings, lists and art. It asks first, moves the whole folder to
  `manage/backups/deleted/<name> <time>`, then takes it out of its menus (if a
  menu can't be changed, everything is put back); its games aren't touched.

Every change keeps the original file in `manage/backups` first and takes
effect when RetroFE reloads, after you close Manage.

**Videos** is the same for **video collections**: systems whose files are
videos, played by a video player, and marked with `type = videos` in their
`info.conf`. **Add a video collection** starts with the extensions
`mp4,mkv,avi` and the launcher `VideoPlayer`; when there's no such launcher
yet, choose the player program (VLC or mpv get their usual full-screen
arguments) and Manage writes `launchers.<os>/VideoPlayer.conf`. Video
collections are listed under Videos, not Systems. Playing videos inside a
page (jukebox mode) and previewing video art on these screens come later.

### Themes

**Manage > Themes** lists the layouts in `layouts/`, each with what it is:
which one RetroFE uses, its size and orientation, whether it was made with
the theme editor or by hand, and which pages it has (the main page, other
main pages such as `layout 4x3`, a splash page, pages for particular
collections). Choosing one shows the details and what you can do with it:

| Button | What it does |
|--------|--------------|
| Use this theme | Sets `layout` in your settings; RetroFE switches to it when you close the manager. Not for a theme without `splash.xml` (RetroFE wouldn't start); a theme made with the editor needs a wheel too. |
| Edit | Opens the [theme editor](THEME_EDITOR.md). Editor-made themes only. |
| Duplicate | Copies the theme as "*name* Copy". Editor-made themes only. |
| Delete | Deletes the theme's folder, after a second Select. Editor-made themes only, and not the one in use. |

**New theme** at the top of the list makes an editor-made theme: give it a
name and choose landscape (1920 x 1080) or portrait (1080 x 1920). It starts
empty; Edit it to place a wheel, titles, art and video.

Hand-made themes, Aeon Nox included, can be used but not edited, duplicated
or deleted here: the editor only changes themes it made, and the screen says
so.

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
| `statusColor` | The message an action leaves above the hints (default `fontColor`) |
| `errorColor` | Error messages, and the question of a button waiting for confirmation (default `fontColor`) |
| `confirmBackgroundColor`, `confirmBackgroundAlpha` | Tint over a button waiting for a second Select to confirm (default alpha 0: none) |
| `title` | `no` hides the screen's title |
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
number fields get a number pad. Typing on a real keyboard puts the on-screen
one away for that edit; finish with Enter or cancel with Escape. Set
`onScreenKeyboard = no` to never show it.

A layout styles it with `layouts/<layout>/forms/keyboard/layout.xml`, a page
with a `<keyboard>` element; it's drawn over the screen being edited:

```xml
<layout width="1920" height="1080" font="fonts/standard.ttf" loadFontSize="40">
  <keyboard x="center" y="1050" xOrigin="center" yOrigin="bottom" width="1400" height="600"
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
| `text`, `folder`, `file` | Text, a folder path, or a file path (`filter`, `pattern` like `*.exe`). `live="yes"` on `text` passes every change on while typing, e.g. for a search box. |
| `toggle` | On or off |
| `number` | A whole number (`min`, `max`) |
| `choice` | One of a list (`options` is `layouts`, `collections`, `sources`, `sourceTypes` or `installed`) |
| `list` | A read-only list (`empty` is shown when there's nothing in it) |
| `note` | Text: `value`, or with `bind` a value from RetroFE; hidden when empty |
| `value` | A label and a read-only value, e.g. `<value label="Year" bind="game:year"/>`; hidden when empty |
| `image` | A picture whose file `bind` gives, `rows` high (default 4), keeping its shape; hidden when there's no file or it can't be loaded |
| `button` | Runs an `action`. With `confirm="Delete this game?"` the first Select asks that, and a second one runs it; anything else cancels. |
| `link` | Opens the screen `open` (a form id), with this screen's parameters; `status` is shown on the right |
| `results` | Rows to choose from, e.g. games matching a search. Select opens the screen `open` for that row: its id as the parameter `param` (default `id`), plus whatever else the row needs to find its record. `limit` rows at most (default 200), with a note saying how many there are in all; `empty` is shown when there are none. |

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
F10 or closing): it says so once, and leaving again drops the changes.
Removing a package or a source asks you to confirm first.

`bind` says what a field shows and changes:

- `settings:<key>`: a setting from `settings.conf`. Changes are kept until
  `settings.save`. `default` is the value RetroFE uses when no file sets it.
- `draft:<key>`: input for an action, such as a new system's name.
  `default` is its starting value.
- `param:<name>`: the screen's parameters (read-only).
- `info:<key>`: read-only: `info:collections`, `info:sources`,
  `info:installed` and `info:catalogWarnings` (lists), `info:romCount`,
  `info:catalogStatus`, `info:removalStatus`, and `info:hasCollections`,
  `info:hasSources`, `info:hasInstalled` and `info:sourceIsFolder` for
  `visibleWhen`.
- `package:<key>`: the Catalog package the screen is about (`param:id`):
  `name`, `summary`, `description`, `installedStatus`, `requires`, `checksum`
  (a warning when the source gives none), `progress`, and `canInstall`,
  `canUpdate`, `canRemove` and `isBusy` for `visibleWhen`.
- Other prefixes, such as `game:`, come from the Manage sections that provide
  them; see [design/manage-forms.md](design/manage-forms.md).

Actions:

| Action | Does |
|--------|------|
| `settings.save` | Writes the changed settings |
| `collection.create` | Creates a system (collection) from `draft:name`, `draft:romPath`, `draft:extensions`, `draft:executable` and `draft:arguments` |
| `package.install`, `package.remove`, `package.cancel` | Install or update, remove, or stop the package the screen is about |
| `package.removeInstalled` | Removes the installed package `draft:removePackage` |
| `source.add` | Adds `draft:sourceName` of `draft:sourceType` at `draft:sourceUrl` or `draft:sourcePath`, once `draft:sourceTrusted` is on. A layout's form can't give these a `default`. |
| `source.remove` | Removes `draft:removeSource` |
| `catalog.refresh` | Reads the sources again |
| `open.<form>` | Opens a screen with this screen's parameters, like a `link`, e.g. `open.sources` |
| `back` | Returns to the previous screen, e.g. for a Done button |

### For developers: providers

Each Manage section serves its own bindings through a **provider**
(`RetroFE/Source/Manager/Provider.h`): a `game` provider answers `game:title`,
`game:search` rows, `options="game:genres"` and actions like `game.save`, and
gets the screen's parameters with every call. An action can open a screen or
go back once it succeeds, a provider can object to leaving a screen with
unsaved changes, and it can ask RetroFE to reload once the manager closes.
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
| `collections/Home/layout/layout.xml` | Home, in the same style |
| `collections/Home/medium_artwork/` | Home's artwork and text: `logo/`, `icon/`, `tagline/` and `description/`, each with a file per item (`Catalog.png`, `Catalog.txt`...) |
| `collections/Catalog/layout/layout.xml` | The Catalog: package logos, with kind, source and version in the info panel |
| `collections/Catalog/medium_artwork/` | An icon and text for the Sources item (`Catalog Sources.png`, `.txt`), which has no package artwork |
| `forms/layout.xml` | Settings, Manage, Sources and the other screens |
| `forms/package/layout.xml` | A package: the form takes the top of the screen, so the Catalog's info panel stays visible below it |
| `forms/keyboard/layout.xml` | The on-screen keyboard |
| `layout 4x3.xml` next to each `layout.xml` | 4:3 versions, used with `randomLayout = layout 4x3` |

The pages are generated from upstream's theme by `Scripts/aeon-nox/`.

### Which page a collection gets

Every collection, Home and the Catalog included, is a page of its own. RetroFE
takes it from the first of these that the layout has:

1. `collections/<collection>/layout/`, the collection's own pages
2. `collections/_menu/layout/`, when the collection is a **menu**: it has items
   and they're all collections (from `menu.txt` or `menu/`), like Main, a
   "Consoles" menu or Home. A menu that's still empty is drawn like a collection
   of games.
3. the layout's own folder

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

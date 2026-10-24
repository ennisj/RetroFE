# Fork changelog

What changed in this fork of CoinOPS RetroFE, round by round. Upstream's
changes are in [../CHANGELOG.md](../CHANGELOG.md). Open questions and the
reasoning behind choices are in [DECISIONS.md](DECISIONS.md).

## Round 10 — media folders in Manage, fourth QA (2026-10-24)

Unit tests: 6,540 → 6,718 checks (also run on Linux in WSL; CI is green on
all four platforms again).

- **Media folders in Manage.** Games, Systems/Videos and Menus media screens
  show a slot kept as a folder of files ("a folder of 9 files, one shown at
  random") and list its files: add one, take one out, keep only one, or
  clear the folder, all backed up and undone by "Put back the original".
  Replace puts a file beside the folder (where it wins) and Clear never
  removes a folder. Menu lists use media folders too, and folder art counts
  as present.
- **Fixes:** files added or removed in Manage show after RetroFE reloads
  instead of after a restart; "Put back the original" is offered only while
  the file differs; Linux/macOS CI failures in the emulator tests (a test
  fixture with Windows line endings) fixed.
- **Fourth QA pass** on both test installs: report in
  [QA-ROUND10.md](QA-ROUND10.md). Nothing high or medium; RetroFE starts on
  OneSauce's own controls.conf with no warnings. Fixed: two Settings →
  Controls wordings, and a blank `quit =` (the kiosk setting) no longer
  warns at every start.

## Round 9 — media folders, window close, editor polish (2026-10-24)

Unit tests: 6,329 → 6,540 checks.

- **Media folders (OnesaUCE).** When a collection or theme keeps its art or
  video as a folder of files instead of one file (52 OneSauce collections do
  for their menu video), RetroFE now plays one picked at random, keeps it
  while you scroll between entries that use the same folder, and moves to
  the next with removePlaylist (Cafe80s's "B: play different video"). A file
  beside the folder still wins; a game's own ROM folder is never used this
  way.
- **Closing the window quits RetroFE** (it did nothing before, as upstream),
  asking about unsaved changes first when Manage is open; ignored in kiosk
  mode and when `quit =` is blank. Quitting during the splash now waits for
  start-up to finish instead of tearing it down.
- **`--theme-check` on OnesaUCE themes** lists names RetroFE harmlessly
  ignores on a separate line instead of as problems (Cafe80s: 512 → 4).
- **Theme editor polish.** Align and space-out run on the 4:3 page with its
  own positions; duplicate, delete, add and animation edits reach the 4:3
  page too, only where the element surely matches; "All N at once" edits one
  attribute on every selected element; guides line up a dragged element with
  others' edges and centres (Alt turns them off); editor scripts wait for the
  preview instead of needing `wait` lines.
- Each missing sound is warned about once.

## Round 8 — controls, third QA (2026-10-24)

Unit tests: 6,103 → 6,329 checks.

- **Game-controller names in controls.conf.** `joyA`, `joyDPUp`,
  `joyLeftY-`, `joyRightTrigger` and the rest of SDL's controller names now
  work beside the old joystick, keyboard and mouse names, so RetroFE starts on
  OneSauce's controls.conf without a remap. An input RetroFE doesn't know is
  skipped with a warning instead of stopping RetroFE; pads plugged in while
  it runs work. A bare install gives Select/Back to Controller A/B, and to
  buttons 0/1 on joysticks SDL has no controller mapping for (arcade USB
  encoders), so one button never acts as both.
- **Settings → Controls.** Every control with its inputs; add one by
  pressing it (hold any input two seconds to cancel), remove one, put back
  the default. It warns when an input already belongs to another control
  (including the same button under another name on the pads connected now),
  and won't save without inputs for Up, Down, Select and Back. Saving writes
  only that line to the file the value comes from, with a backup, and the new
  controls work straight away.
- **Third QA pass** on copies of the OnesaUCE data, including one in
  OneSauce's real folder layout: report in [QA-ROUND8.md](QA-ROUND8.md).
  Fixed: Settings' Layout list offered folders RetroFE can't start with (and
  sorted by case); a `cmd.exe` launcher's working folder was described
  wrongly; a pageless theme folder showed the editor's note.

## Round 7 — launchers and saves, editor groups, OnesaUCE themes (2026-10-24)

Unit tests: 5,473 → 6,103 checks.

- **Edit launchers in place.** An emulator's screen gets Edit (program,
  arguments, working folder, restart), changing only the edited lines, with
  the systems and games it reaches and "Put back the original". Global
  launchers can be renamed: every reference is rewritten after a preview,
  all or nothing. A launcher that runs RetroArch links to its core-wide
  RetroArch settings.
- **Saves for RetroArch games.** A game's Saves screen lists its save files
  and states (found the way RetroArch names and sorts them), with Back up
  now, Restore (the current files are backed up first) and Delete. Backups
  are kept under `manage/saves/`.
- **Theme editors: several at once.** Ctrl/Shift-click, a marquee or Ctrl+A
  (Select held plus a direction with a controller); move, nudge, align,
  space out, duplicate or delete them together, one undo step each. Resize
  from any handle with a controller; lists scroll to what's picked.
- **Both page shapes at once.** In the layout editor, "Also apply to layout
  4x3.xml" copies each edit to the matching element on the 4:3 page, scaled
  to its size (wheel items paired by their place from the selected one);
  undo and save cover both. "Where animations end" works for any event.
- **OnesaUCE themes** ([ONESAUCE-COMPAT.md](ONESAUCE-COMPAT.md) has the
  survey): `layout.lua` pages (read without running Lua, so OnesaUCE's Aeon
  Nox draws instead of exiting), `transform` (video and art on angled
  screens), `layout_preferred`, `<scrollingText>`, image `textFormat`,
  `%RETROFE_PATH%` in path settings and `baseLayoutPath`, which Manage's
  theme screens, the library and Catalog theme installs now follow too.
  Base paths set in `settings1..15.conf` are no longer ignored.

## Round 6 — controller editing, RetroArch settings, second QA (2026-10-24)

Unit tests: 4,966 → 5,473 checks.

- **The theme editors work with a controller.** Both the widget editor and
  the layout editor can be used with only RetroFE's mapped controls: move
  between panels and the canvas, pick the nearest element in a direction,
  move and resize (holding a direction goes faster), step numbers and
  choices, type on an on-screen keyboard drawn in the editor, and use every
  dialog (Add widget/element, file chooser, Unsaved changes). Mouse and
  keyboard still work.
- **RetroArch settings in Manage.** A game whose launcher runs RetroArch gets
  a RetroArch link: eight common settings (aspect ratio, smoothing, shader,
  overlay, integer scaling, run-ahead, volume, auto save/load state), core
  options and controls, each "Default (…)" or a fixed choice, written to the
  game's own RetroArch files with only what differs; "Use the system's
  settings" removes them. A system gets the same screens for the whole core,
  with which systems they reach. Every file is backed up before the first
  change.
- **Second QA pass** on a copy of the OnesaUCE data: report in
  [QA-ROUND6.md](QA-ROUND6.md). Fixed: launchers with a bare program name
  (`cmd.exe`) never started and still recorded 40 s of play; short lists
  (fewer items than the menu's selected slot) opened on the wrong item; a
  failed launch counted as a play; Aeon Nox's video pages failed the theme
  check; false warnings on an empty root and for quick programs; "Played:
  Never" next to a time played now says why plays aren't counted.
- **CI runs the tests everywhere.** GitHub Actions builds and runs the unit
  tests on Windows, Linux x86_64, Linux arm64 and macOS for `master`,
  `feature/**` and `ci/**` (details in [CI.md](CI.md)). The macOS app is now
  built with CMake (the Xcode project lacked all the fork's code) as a
  universal, signed bundle that's checked to load nothing from outside it.
  The Windows GStreamer install (Chocolatey's package was taken down) now
  unpacks GStreamer 1.24.13's installer, checked against its published
  SHA-256, in the release workflow too. Two tests that only failed with
  GCC/Clang were fixed.

## Round 5 — videos in RetroFE, layout editor additions, clean logs (2026-10-23)

Unit tests: 4,430 → 4,966 checks.

- **Video collections play in RetroFE.** Selecting a video opens a full-screen
  player page (Select pauses, Left/Right previous/next, Up/Down seek, Back to
  the list on the same video; the next one starts when one ends). A video
  collection's list previews each video. New video collections play here by
  default (`play = here` in info.conf); existing ones keep their external
  player. Built-in and Aeon Nox pages (`collections/_videos/`, 4:3 too).
- **Layout editor:** add new elements from a palette (images, videos, text,
  game art and text, menus…), edit and copy animations, play an event on the
  preview, and see where an element's start animation leaves it.
- **Clean logs:** a normal start on OnesaUCE-shaped data no longer logs dozens
  of false errors (menus without settings, missing hiscore tools, the DPI
  hint, optional layout pages); real problems still warn.
- **Fixes:** a video reselected with the same file keeps playing instead of
  restarting (an upstream change had broken this); `<animate>` with an empty
  `from`/`to` no longer reads past the end of the value; hidden games keep
  their titles and metadata in Manage; "Back from" names the game's title.

## Round 4 — editing hand-made themes (2026-10-23)

- **Layout editor for hand-made themes.** Edit on Aeon Nox (or any theme not
  made with the widget editor) opens its pages — main and 4:3 layouts,
  splash, collection pages, `_menu`, form pages — previewed as RetroFE builds
  them. Select, move, resize and nudge elements and menu items, edit any
  attribute, duplicate or delete elements. Saving changes only the values you
  edited; the original is kept and can be put back (decision M10).
- Unit tests: 4,189 → 4,430 checks.

## Round 3 — editing extras, theme templates, QA, Linux (2026-10-23)

Unit tests: 3,879 → 4,189 checks; also built and run on Linux (WSL,
g++ 13).

- **Multi-line text fields** (`multiline="yes"`): a game's description is
  edited as real lines (Enter for a new line, Ctrl+Enter to keep; the
  on-screen keyboard gains New line and cursor keys).
- **Start this game** from its Manage screen: Manage closes (reloading first
  if changes are waiting), the game runs as from its page, and Manage
  reopens on it afterwards. Refused while a Catalog install runs.
- **Hide / show a game** (its system's exclude list), with an "Only hidden
  games" search; **play count and time played** on the game screen.
- **Theme editor:** collection templates (create, copy, rename, assign,
  apply to a whole menu branch), a file chooser for images, videos and fonts,
  the theme's font and sounds, splash text, and highlighted text wheels.
- **Engine fixes:** a layout `<menu>` without `<itemDefaults>` no longer
  crashes RetroFE; a vertical menu of zero-height items no longer hangs it; a
  page made only of a menu now draws.
- **QA on a copy of the OnesaUCE data** (146 collections, 6,467 games):
  report in [QA-ROUND3.md](QA-ROUND3.md). Fixed: a bare install's first
  system now writes settings.conf (Manage stopped reopening at every start);
  Settings warns before discarding changes; Back after adding a system or
  menu returns to the list; duplicated launchers keep "restarts RetroFE";
  bare program names such as `cmd.exe` are found on the system path, and
  `cmd /c` commands keep their quotes; a settings.conf written meanwhile by
  another screen is never overwritten.
- **Linux:** fixed a use-after-free in the Catalog's GitHub check and a log
  message that printed 1/0; tests made platform-aware.

## Round 2 — the Manage screens (2026-10-23)

Six agents in parallel, each followed by a Fable adversarial review and
fixes. Unit tests: 2,766 → 3,879 checks.

- **Games.** Search every game (by system, merged collection or menu; "only
  without metadata"), with thumbnails. A game's screen links to: Metadata
  (edit nine fields, each back to the shipped value), Launcher (a per-game
  launcher or the system's), Media (every art/video/text slot: own file,
  fallback or missing; replace, clear, put back), Playlists and collections
  (favourite per page, playlists, merged collections) and Description.
- **Systems and Videos.** Search, details, settings (extensions, sort,
  missing items, metadata type), default launcher (keeps alternatives),
  info, artwork, playlists (create, rename, delete, entries; points out
  playlists RetroFE won't show because of `cyclePlaylist`), "Check this
  system" warnings, add a system or a video collection, delete an empty one.
- **Menus.** The menu tree from Browse down with loops, missing and
  wrong-case entries flagged; reorder, add and remove entries; a merged
  collection's sources; info and artwork; new menu, new merged collection,
  delete (entries move up).
- **Emulators.** Every launcher (global, per collection, local) with what
  uses it and names used but not defined; add a launcher with presets
  (game file, RetroArch core, MAME), duplicate, delete unused ones.
- **Themes and the theme editor.** Installed layouts with their facts; use
  a theme, new, duplicate, delete (editor-made). The editor (ImGui) previews
  the page RetroFE builds, with select, move, resize and snap, an Add widget
  dialog, properties, undo/redo and save. `retrofe --theme-check <layout>`
  checks a theme from the command line.
- **Form rendering.** Long text ends in "..." instead of losing letters;
  pictures refresh after a replace; focus never jumps onto a file field;
  new `<form>` styling attributes, used by Aeon Nox (blue headers,
  separators, green/red messages, a tint on buttons awaiting confirmation).
- **Tests run in their own folders**, so several test runs (one per agent)
  no longer delete each other's files.

## Round 1 — foundations (2026-10-23)

Five agents in parallel, each followed by a Fable adversarial review and a
round of fixes before merging. Unit tests: 327 → 2,766 checks.

- **Aeon Nox is the default layout.** The official copy from RetroFE's
  original repository, adapted: a games page, a systems page for every menu
  (new theme-wide `collections/_menu/` page, decision L5), Home and Catalog
  pages with drawn logos and icons, styled Settings/Manage/Catalog screens and
  on-screen keyboard, and 4:3 versions of everything (screens now follow the
  layout variant, L6). Fonts with missing or non-redistributable licences were
  replaced; NOTICE.txt records provenance (open questions in L4).
- **Manage hub and form foundation.** Screens stack (Back returns with the
  selection kept); entity sections plug in as providers in their own folders;
  new form elements: `results` (searchable lists with thumbnails), `link`,
  `value`, `image`, live text; confirm-before-acting buttons; unsaved-changes
  warnings; a deferred reload when leaving Manage. The hub lists Games,
  Menus, Systems, Videos, Emulators and Themes (placeholders until round 2).
- **Library.** A shared index of collections, menus, systems, games,
  metadata, launchers, media and layouts, built off the UI thread (30,000
  games on the OnesaUCE test data in about half a second warm), with search,
  media and launcher resolution the way RetroFE does it, and validated,
  backed-up writes (metadata edits go to a per-user override layer, M6).
  OnesaUCE compatibility: `media.basePath`, comma launcher lists (first one
  wins) and `list.includeFile`/`excludeFile` are now honoured by RetroFE too.
- **Catalog: RetroFE from GitHub.** A `github` source type, a one-step "Add
  RetroFE from GitHub", and self-update: verified download (a checksum is
  required), backup, journaled swap of the program folder with recovery at
  the next start, restart. A release workflow builds and publishes when a
  version tag is pushed (not run yet — C1).
- **Theme editor core.** One Saucier's project model, 35 widgets (adapted to
  this engine), generator, layout checker and in-theme storage, ported with
  tests (also checked on Linux with g++).

## Before the rounds (branch `feature/management-ui`)

- **Bare boot.** RetroFE starts with nothing but its executable: built-in
  settings, controls, layout and font apply when files are missing.
- **Home menu.** Catalog, Settings, Manage and Browse; Browse opens the usual
  top menu. On by default for bare installs.
- **Forms.** Settings and Manage are XML forms bound to settings and other
  values, drawn as layout pages (`<form>` element) and driven by RetroFE's
  controls; an on-screen keyboard page (`<keyboard>`) for text.
- **Catalog.** Sources (an index file on the web or disk, or a folder of zip
  packages), package list and details, background installs with resume,
  verification, staged installs, manifests, backups and removal; hardened after
  an adversarial review (journaled installs with rollback, protected folders,
  no executables, Windows device names refused, same-source updates).

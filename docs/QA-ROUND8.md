# QA, round 8: round 7's launchers, saves, editor groups and OnesaUCE themes

Date: 2026-10-24. Build: `feature/management-ui` at 0a7af24c plus the fixes
below (RelWithDebInfo, Windows 11). Screenshots are in
`E:\Storage\Agents\qa3\screenshots\` (`r8-*.png`; `*-sheet-*.png` are
contact sheets of several steps; `r8-*-log-*.txt` are start logs).

Covers round 7 ([FORK-CHANGELOG.md](FORK-CHANGELOG.md) "Round 7",
[DECISIONS.md](DECISIONS.md) M18, [ONESAUCE-COMPAT.md](ONESAUCE-COMPAT.md)):
editing, putting back and renaming launchers, the RetroArch link on a
launcher, game saves, several elements at once in both theme editors, the
4:3 twin, the controller's held-Select add and any-handle resize, OnesaUCE
themes on a real-shaped install, and a regression sweep.

## Test installs

- **a: OnesaUCE-shaped copy**, `E:\Storage\Agents\qa3\a\` (a copy of round 6's
  `E:\Storage\Agents\qa2\`: 150 collections, 6,478 games, `appdata\retrofe`
  root, `settings1.conf` with relative base paths, windowed 1280x720, our
  Aeon Nox from `Package/Environment/Common`). Added fixtures
  (`appdata\retrofe\qa-fixture\`):
  - `launchers.windows/QAFix.conf`: `cmd.exe /c quick.bat`, with comments,
    a blank line, an unknown key, a trailing comment and CRLF endings; the
    batch file appends its argument to `qa-fixture/launched.txt` and exits.
  - `launchers.windows/QARetro.conf`: a fixture RetroArch
    (`qa-fixture/RetroArch/retroarch.exe` is a text file, never started) with
    a Genesis Plus GX core, `retroarch.cfg` (`savefile_directory = "default"`,
    `sort_savestates_enable = "true"`), save files and states for Sonic,
    Streets of Rage and Columns, and decoys (`Sonic 2.srm`,
    `Sonic (USA).srm`, `Sonic.srm.bak`, `Sonic 2.state`, an unsorted
    `states/Sonic.state`, `Sonic.State2` with a capital S).
  - Systems `QA Arcade` (launcher QAFix; `beta` picks QAQuick),
    `QA Genesis` (QARetro), `QA MegaDrive` (`launcher = QARetro, QAFix`;
    `Columns` picks QAFix): Sonic is in both Genesis and MegaDrive.
- **b: the real OneSauce shape**, `E:\Storage\Agents\qa3\b\`, copied
  read-only from `K:\retrofe-data`: `appdata\retrofe` (collections, meta,
  launchers, OneSauce's own `settings.conf` with `%RETROFE_PATH%` base paths
  and `baseLayoutPath`, `controls.conf`), `base_assets\layouts` (Aeon Nox,
  Renee, Amiga Memories, Cafe80s, `default`), `base_assets\collections`
  (Main, `_common`, 1 ARCADES, 1 CONSOLES, 2 ARCADE SYSTEMS, Cave, THEMES)
  and 30 ROMs each of Atari 2600 and GCE Vectrex. Our `settings1.conf` only
  sets the window, the log levels and the layout. **controls1.conf**: K:'s
  remap of OneSauce's game-controller names (see problem 6); game-controller
  names in `controls.conf` are being implemented by another agent.
- **Empty root** `E:\Storage\Agents\qa3\firstrun\` (only `retrofe\`).

Input went only to this QA's RetroFE windows (matched by exe path); no
native file picker or message box opened; no emulator was started; nothing
was written to K:, I:, E:\Storage\OneSauce or qa/qa2.

## What works

### Emulators (a)

- **Edit** (`r8-a-012` to `r8-a-018`): the screen shows who it reaches;
  Custom arguments typed on the keyboard; Save changed exactly the
  `arguments` line: comments, the blank line, `LEDBlinkyEmulator`,
  `customKey = keep me   # trailing comment`, CRLF everywhere and the missing
  final newline all kept (`r8-QAFix-original.conf` / `r8-QAFix-edited.conf`,
  one-line diff). The original went to `manage/backups/launchers.windows/`.
- **Put back the original** (`r8-a-021` to `r8-a-023`): asks first; the file
  is byte-identical to the original (hash checked); the button then
  disappears. A second edit/save/put-back cycle works the same (the replaced
  versions are kept as `QAFix (2).conf`... in the backups, as LB2 says).
- **Rename** (`r8-a-043` to `r8-a-047`): typing `QAFixRenamed` lists every
  file that changes; after the confirmation `QAFix.conf` became
  `QAFixRenamed.conf` (identical bytes), `QA Arcade/settings.conf`
  (`launcher = QAFixRenamed`), `QA MegaDrive/settings.conf`
  (`launcher = QARetro, QAFixRenamed`, the alternative kept in place) and
  `QA MegaDrive/launchers/Columns.conf` were rewritten, each original
  kept, and `QAFixRenamed.conf.absent` marks the new file. `beta`'s pick of
  QAQuick was left alone. **RetroFE still launches it**: after the reload,
  Browse > QA Arcade > alpha ran `cmd.exe /c ""...quick.bat" "...alpha.zip"
  "alpha""` (the edited arguments) in under a second; `launched.txt` got
  the line (`r8-a-078`, `r8-a-079`).
- **RetroArch link** on QARetro (`r8-a-sheet-retroarch-link.png`): names the
  core (Genesis Plus GX), the 2 systems it reaches, core options, controls
  and the eight settings.

### Saves (a)

QA Genesis > Sonic > Saves (`r8-a-058` to `r8-a-071`), with a file-by-file
comparison of the fixture RetroArch's `saves` and `states` after every step:

- Shows both folders (`states\Genesis Plus GX`, sorted per core) and says
  "A game of the same name in QA MegaDrive keeps its saves in the same
  place". Lists exactly Sonic's 7 files: `.srm`, `.rtc`, `.state`,
  `.state1`, `.state1.png`, `.state.auto` and `Sonic.State2` (case ignored
  on Windows, M18). Not listed and never touched: `Sonic 2.*`,
  `Sonic (USA).srm`, `Sonic.srm.bak`, the unsorted `states/Sonic.state`,
  Streets of Rage, Columns.
- **Back up now**: `manage/saves/QA Genesis/Sonic/<time>/saves|states`
  with the 7 files and `backup.txt`; nothing else changed.
- **Restore** after "playing" (one save changed, a state deleted, a new
  `.state3`): the current 7 files were backed up first ("before restoring"),
  then the game had exactly the backup's files again, `.state3` included in
  the pre-restore backup and removed; all 15 files identical to the start.
- **Delete these saves**: asks, backs up ("before deleting"), deletes only
  Sonic's 7 files; restoring that backup brings all 7 back.
- **Delete this backup** removes only that folder.

### Theme editors

- **Layout editor on our Aeon Nox, scripted** (`-editorScript`,
  `E:\Storage\Agents\qa3\layout-script2.txt`; results in
  `r8-a-log-*`/log.txt, `r8-a-093`): two picked and nudged together, copied
  to `layout 4x3.xml` scaled (+10 there is +8), one undo takes both pages
  back; align top; distribute down; group duplicate and group delete each
  one undo step (the twin note says added/removed elements aren't copied);
  select all; Show layout 4x3.xml keeps the selection and both pages'
  changes. Save wrote both pages: the diff against the shipped pages is one
  attribute each (`x="620"` to `640` on layout.xml, `x="510"` to `527` on
  the 4:3 page), CRLF kept (`r8-layout-saved.xml`,
  `r8-layout 4x3-saved.xml`). **Put back the original** on both pages
  (`layout-script3.txt`, `r8-a-111`) made both byte-identical to the
  shipped ones (hash checked).
- **Controller, held Select** (`r8-a-095`, `r8-a-096`): holding Select and
  pressing Up twice added the elements above, one per press ("3 elements
  selected", the properties panel shows Left/Centre/Right/Top/Middle/Bottom,
  Space across/down, Duplicate them, Delete them); letting go keeps them;
  Back keeps only the last one (`r8-a-097`).
- **Any-handle resize** (`r8-a-sheet-pad-resize1.png`, `r8-a-101`): Select
  twice goes Move then Resize (bottom-right handle enlarged); Letter down
  three times steps to the left edge ("arrows move its left edge"); Left
  twice moved only the left edge (x 620 to 610, width written 310, right
  edge in place) and the 4:3 page got its scaled copy (x 502, width 258);
  Ctrl+Z undid it.
- **Widget editor, scripted** (New Theme 1, `widget-script.txt`): select
  all, add to the selection, align, group duplicate and delete with undo,
  marquee over the page, toggle, nudge and undo all behave; leaving without
  saving left every file of the theme unchanged (hashes compared).

### OnesaUCE themes on the real-shaped install (b)

- **Start with OneSauce's settings**: with `controls.conf` alone RetroFE
  exits ("Could not initialize user controls", 25 errors:
  `r8-b-log-nocontrols1.txt`; known, being implemented). With K:'s
  `controls1.conf` remap it starts: "Layouts are in
  ...\base_assets\layouts (baseLayoutPath)", media and ROMs from
  `%RETROFE_PATH%/../../...`, 146 collections indexed.
- **Renee** draws Main with the cabinet, marquee art and the neon wheel
  (`r8-b-200`, `r8-b-201`); a collection whose system video is one file
  (`1 CONSOLES/system_artwork/video.mp4`) plays it in the cabinet. Start log:
  no ERROR or WARNING (`r8-b-log-renee.txt`).
- **Aeon Nox** (layout.lua): builds `collections/main.xml`; Home draws on
  it with the Catalog / Settings / Manage / Browse items in order
  (`r8-b-sheet-home-cycle.png`), and Manage opens from it (`r8-b-031`).
- **Amiga Memories** (transform): the collection video plays inside the
  angled Amiga monitor (`r8-b-sheet-amiga-cafe.png`, top).
- **Cafe80s** (scrollingText): the top bar's instructions scroll
  ("B: play different video (if available).", same sheet, bottom).
- **Manage > Themes** lists the base_assets themes (Aeon Nox in use,
  Amiga Memories, Cafe80s, default, Renee; `r8-b-032`); Aeon Nox shows 130
  collection pages from its lua files (`r8-b-034`).
- **Settings > Layout** now lists the same themes RetroFE can start with
  (Built-in, Aeon Nox, Amiga Memories, Cafe80s, Renee:
  `r8-b-sheet-layout-list-fixed.png`; before the fix it also offered
  `default`: `r8-b-sheet-layout-list.png`). Cycling through and closing
  wrote nothing.
- **`--theme-check`** works on all of them (`r8-themecheck-b-*.txt`): it
  reads the lua-built pages, builds every page and reports real missing
  files; see problem 5 for what else it reports.
- **Start logs**: every ERROR/WARNING at start on b is true (a file the
  theme names is missing on K: and I: as well): Aeon Nox
  `images/background.png` (splash), Amiga Memories `images/splash.png` and
  its four `sounds/*.wav`, Cafe80s `images/splash 16x9.png`. On a, the one
  WARNING (`baseLayoutPath ... isn't a folder`) is true for that copy
  (qa2's settings.conf names a folder it doesn't have). No false ones.

### Regression sweep and first run

- **16:9 and 4:3 on our Aeon Nox** (`r8-a-sweep169-sheet.png`,
  `r8-a43-sweep-sheet.png`, `r8-a43-sheet1..3.png`,
  `r8-a-sheet-catalog169.png`, `r8-a43-package.png`): Home, Settings (F10),
  Manage and every section (Games, Menus, Systems, Videos, Emulators,
  Themes, Sources), Catalog list and a package's page, Browse Main and a
  game list. Nothing cut off or overlapping at 960x720.
- **First run** on an empty root (`r8-fr-001`, `r8-fr-sheet.png`): Manage
  opens with the welcome note; Themes says none are installed; Emulators
  is empty with Add a launcher. Log: NOTICE lines only.

## Problems found

Severity: **High** breaks a task, **Medium** misleads or loses work,
**Low** wording, noise or polish.

| # | Sev | Where | Problem | Status |
|---|-----|-------|---------|--------|
| 1 | Medium | Settings > Layout (OneSauce install) | The list offered every folder under the layouts folder, including OneSauce's `default` (a README about wallpaper videos, no pages) (`r8-b-sheet-layout-list.png`). Choosing it and saving would make RetroFE stop at start-up (no splash page), and Settings is how you'd switch back. It also sorted case-sensitively, unlike Manage's Themes list. Steps: slice b, F10, Layout, Right. | Fixed |
| 2 | Medium | Browse, OnesaUCE themes (not in ONESAUCE-COMPAT.md) | 52 collections on K: keep their menu video as a **folder** of videos, `system_artwork/video/<name> 1.mp4`, `<name> 2.mp4`... (Cafe80s's top bar says "B: play different video (if available)"). RetroFE looks only for `video.mp4`, so those entries show a black screen: Renee's cabinet on ARCADE (`r8-b-200`) while CONSOLES, which has one `video.mp4`, plays (`r8-b-201`); Cafe80s's main video area. | Report (compat feature; meaning is clear from the data: pick one, B for the next) |
| 3 | Low | Layout editor, 4:3 twin | Align and distribute are copied to the twin as scaled moves, so what lines up on one page doesn't on the other: after *align top* of manufacturer (y 135) and rating (y 215), the 4:3 page has 138 and 134. As M18 specifies for moves, but for align the expected result is "aligned on both". | Fixed after QA: align/distribute run on the 4:3 page's own boxes (design/theme-editor.md, "Twin group edits") |
| 4 | Low | Emulators > launcher | "Working folder: The program's folder" for a `cmd.exe` launcher, which actually runs in RetroFE's folder (round 6's R2). | Fixed |
| 5 | Low | `--theme-check` on OnesaUCE themes | Fails on names ONESAUCE-COMPAT.md lists as ignored and harmless or unclear (`hwdec`, `algorithm`, `keepOldWhileLoading`, `scaleMode`, `delay`, `static`, `reload`, `miScrollTime`, `mode="system"`, `<stop>`, `<onEmpty>`, `pauseTime`, `uppercase`, `numLoops`, `Height`, `maxwidth`, `loadfontSize`): Aeon Nox 32 problems with 1 real, Cafe80s 512 with 4 real (a missing splash image and three `text/*.txt` its 1 HANDHELDS page names relative to the page's folder; the files are in `collections/_common/text`). The real ones are hard to find. | Report (checker vocabulary is a theme-editor decision) |
| 6 | Info | Start-up, OneSauce controls.conf | RetroFE exits without a `controls1.conf` remap (25 "Unsupported property value" errors). Known (ONESAUCE-COMPAT.md); game-controller names are being implemented by another agent. | Report only |
| 7 | Low | Manage > Themes > `default` | The screen said "This theme was made by hand. Edit opens its pages in the layout editor..." for a folder with no pages and no Edit button (`r8-b-033`). | Fixed |
| 8 | Low | Window | Closing the window (WM_CLOSE / the title-bar X) does nothing: RetroFE has no SDL_QUIT handling (upstream behaviour). Only the quit key (Q) closes it. | Report (input handling) |
| 9 | Low | `-editorScript`, layout editor | A command right after `undo`/`nudge` can run before the preview is rebuilt, when boxes aren't measured yet: in the first run `selectall` found nothing and the next `nudge` failed. With `wait 40` after such commands all passed. Users don't hit it (the rebuild takes a fraction of a second). | Fixed after QA: the runner waits for the preview before the next command |
| 10 | Low | Amiga Memories start log | Each missing sound is warned twice (the splash and the main page both load the layout's sounds). True, but repeated. | Report |

Not problems: a marquee that starts on an element (Aeon Nox's full-page
background) is refused, and `selectall` includes the background, as M18
decided; the widget editor's `distribute v` needs three widgets.

## Fixes made

| # | Change | Files | Test |
|---|--------|-------|------|
| 1 | Settings' Layout choices: only folders with a main page and a splash page (`.xml`, or `.lua` for fork compat), plus the layout set now whatever it is; sorted ignoring case. Checked live on b (`r8-b-sheet-layout-list-fixed.png`). | `Manager/ManagerModel.cpp` | CompatTests: "Settings' Layout list offers only folders RetroFE can draw, in Manage's order" |
| 4 | A launcher without `currentDirectory` whose program is a bare name RetroFE runs from its own folder (`Library::programFolder`, worked out once per snapshot as it may search PATH) says "RetroFE's folder (cmd.exe has no folder of its own)". Checked live (`r8-a-151`). | `Manager/Emulators/EmulatorProvider.*` | EmulatorEditTests: "a bare cmd.exe runs in RetroFE's folder, and the screen says so" |
| 7 | The hand-made note is shown only when the theme has pages to edit. | `Manager/Themes/ThemeProvider.cpp` | ThemesTests: the Empty folder has no note |

Tests: 6,107/6,107 checks pass (6,103 before).

Nothing was changed in `Control/`, input handling, or a Settings > Controls
screen.

## Recommendations

1. **Video folders** (2): support `system_artwork/video/` (and probably
   `medium_artwork/video/<game>/`) as "one of these, another on the
   change-video key"; add it to ONESAUCE-COMPAT.md. It's the most visible
   gap on OneSauce data after controller names.
2. **Theme check for OnesaUCE themes** (5): report names ONESAUCE-COMPAT.md
   lists as ignored as "notes" (or one line each with a count), so `FAILED`
   means something RetroFE draws differently or a missing file.
3. **Twin align** (3): run align/distribute on the twin's own boxes when
   every selected element has a counterpart there.
4. **Window close** (8): handle SDL_QUIT like the quit key (asking about
   unsaved Manage changes), in the input work under way.
5. **Script waits** (9): let the editor script runner wait for a pending
   preview rebuild before the next command.

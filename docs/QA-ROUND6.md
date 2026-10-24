# QA, round 6: what changed since round 3

Date: 2026-10-23. Build: `feature/management-ui` at d4e93b16 plus the fixes
below (RelWithDebInfo, Windows 11). Screenshots are in
`E:\Storage\Agents\qa2\screenshots\` (`r6-*.png`; `r6-sheet-*.png` are
contact sheets of several steps).

Covers what rounds 3 to 5 added after [QA-ROUND3.md](QA-ROUND3.md):
multi-line descriptions, Start this game, hide/show, play counts, the theme
editor's templates, file chooser and theme panel, the layout editor on
Aeon Nox, adding elements and animations, in-page video playback, the
cleaned-up start log, a regression sweep at 16:9 and 4:3, a first run on an
empty root, and the round-3 problems marked fixed.

## Test installs

- **Realistic install**, `E:\Storage\Agents\qa2\`: a copy of round 3's
  `E:\Storage\Agents\qa\` (OnesaUCE-shaped: 146 collections, 6,467 games,
  `appdata\retrofe` root with `settings1.conf` overrides, windowed 1280x720),
  with this build and the current `Package/Environment/Common/layouts/Aeon Nox`
  (the copy's Aeon Nox predated the `_videos` pages). Round 3's leftovers in
  it (a MAME metadata override, a favourite) were left alone.
- **Fixture launcher** `launchers.windows/QAQuick.conf`:
  `executable = cmd.exe`, `arguments = /c "%RETROFE_PATH%/qa-fixture/quick.bat" "%ITEM_FILEPATH%"`;
  the batch file appends its argument to `qa-fixture/launched.txt` and exits.
  It runs hidden (`CREATE_NO_WINDOW`); no emulator was ever started. Given to
  MAME's `1942` through Manage > Games > Launcher.
- **Video collection** "QA Videos": three local mp4s (Aeon Nox's
  `video/intro.mp4` and two MAME preview videos, copied), added with
  Manage > Videos > Add a video collection.
- **Empty root** `E:\Storage\Agents\qa2\firstrun\`: only `retrofe\` (exe and
  DLLs); three text "ROMs" in `E:\Storage\Agents\qa2\fixture-roms6\Arcade`.

Input went only to this QA's RetroFE window (matched by exe path); no native
file picker or message box opened; no launched process lingered (checked by
process list after each launch).

## What works

- **Start log** on the OnesaUCE copy: no ERROR or WARNING line at all
  (1,673 lines, all INFO/NOTICE). Round 3's ~40 false "Could not import any
  collection settings", the HiScores and DPI errors and the per-page
  `layout - N.xml` warnings are gone. Index 0.64 s.
- **Multi-line description** (`r6-11` to `r6-22`, `r6-sheet-games-extras.png`):
  the on-screen keyboard opens on Select with *New line*, *<-* / *->*,
  *Line up* / *Line down* and *Done*; typing on the real keyboard puts it
  away; Enter makes a new line, Up/Home/Ctrl+End move the cursor,
  Ctrl+Enter keeps the text, Escape with unsaved text warns. Save wrote
  `story/1942.txt` with the original 504 bytes untouched and the new lines
  with the file's own LF; the original went to
  `manage/backups/_external/...`; *Put back the original* restored it
  byte-identical. 4:3 too (`r6-120`, `r6-121`).
- **Start this game** (after fix 1): the fixture ran in under a second with
  the game's file as its argument; Manage came back on the game with "Back
  from 1942 (Revision B)." (`r6-39`); the cmd.exe `/c` quoting from round 3
  works for real. With `lastPlayedSkipCollection` set, Played reads "Once,
  on 2026-10-23 22:54" (`r6-68`) and `MAME/playlists/lastplayed.txt` names it.
  A launch that fails says so on the game screen
  ("It didn't start, or ended with an error: cmd.exe (see log.txt)",
  `r6-25`).
- **Hide / show** (`r6-29` to `r6-34`): confirm, then "Hid 1942: exclude.txt
  lists it now."; *Only hidden games* lists it with its title; *Show this
  game again* takes the line out.
- **Widget editor, scripted** (`r6-sheet-widget-editor-run1.png`,
  `r6-sheet-chooser-template-dialog.png`): new theme; wheel and title;
  `alpha 0.8` and `80%` accepted, `150`/`abc` refused with a reason (round 3's
  problem 13 is fixed); font import (Oswald), Select/Highlight sounds,
  `sound highlight none`, splash text and background; template from the
  Default page for MAME, renamed, THEMES assigned to it; the Collections
  panel; the file chooser on `layouts/Aeon Nox/images`; the template dialog
  ("THEMES uses that already"); save wrote 5 files and
  `--theme-check "New Theme 1"` is clean.
- **Layout editor on Aeon Nox, scripted**: nudging the top bar (`y -50` to
  `-40`), `alpha` added, a layer changed, undo/redo, *Where animations end*
  (`ends at 0,0 1920x50`). The first save changed exactly those two lines
  (diff against the shipped file) and kept CRLF everywhere; the original went
  to `manage/backups/layouts/Aeon Nox/layout.xml`. Adding a text, a title and
  an image (`images/bar.png`, already in the theme, used in place), an
  `onEnter` with a second step, editing `to`, playing the event, then adding
  and undoing a wheel: the second save inserted five CRLF lines after the top
  bar and nothing else. *Put back the original* made the page byte-identical
  to the shipped one (SHA-256 checked), keeping the edited one as
  `layout (2).xml`. Files: `layout-saved1.xml`, `layout-saved2.xml`,
  `layout-restored.xml` in the screenshots folder; `r6-100-layout-saved2.png`.
- **Video collection in RetroFE** (`r6-43` to `r6-66`): added with
  *Plays: In RetroFE* (`info.conf`: `type = videos`, `play = here`; listed in
  Main); the Aeon Nox list previews the selected video; Select plays it full
  screen with title, position and time; Select pauses ("PAUSED", frame frozen),
  Up/Down seek (also while paused), Left/Right previous/next (wrapping), the
  next video starts when one ends, Back returns to the list (after fix 2, on
  the video played). 4:3 pages too (`r6-112`, `r6-113`). Each video that
  starts is counted in `playCount.txt` and `QA Videos/playlists/lastplayed.txt`.
- **Regression sweep** (`r6-sheet-manage-sections.png`,
  `r6-sheet-systems-emulators.png`, `r6-sheet-sources-settings.png`,
  `r6-sheet-catalog.png`): Manage and every section (Games, Menus, Systems,
  Videos, Emulators, Themes, Sources); Settings (F10, "Set in settings1.conf");
  Catalog list and a package's page; Browse Main, `1 ARCADES`, `02 ARCADE ALL`
  games page. At 4:3 (`randomLayout = layout 4x3`, 960x720): Home, Main,
  games page, Settings, Manage, the game screen, the keyboard, Videos, the
  video list and player (`r6-sheet-43-*.png`). `--theme-check "Aeon Nox"`:
  all 13 pages clean (after fix 3).
- **First run** (`r6-130` to `r6-143`): Manage opens with the welcome note;
  Add a system with typed paths ("3 matching files"); Back goes to the
  Systems list; closing Manage writes `settings.conf`; Home and
  Browse > Main > Arcade work on the built-in pages; a launch of
  `cmd.exe /c exit 0` returns at once; the second start opens on Home with
  a log of NOTICE lines only (after fix 4).

### Round 3's fixed problems, re-checked

| # | Check | Result |
|---|-------|--------|
| 1 | First system on a bare install writes settings.conf; next start is Home | OK (`r6-138`, `r6-142`) |
| 2 | Settings warns before dropping a change | OK (`r6-sheet-settings-warn.png`; nothing written) |
| 3 | Back after Add system goes to the list | OK (`r6-137`) |
| 5 | cmd.exe found on the system path, `/c` quoting | OK in a real launch (after fix 1) |
| 7, 8 | Atari 2600 Check and Launcher wording | OK (`r6-sheet-a2600-check.png`) |
| 10 | "e.g. Arcade" hint | OK (`r6-132`) |
| 12 | Add a launcher above "Used but not defined" | OK (`r6-77`) |
| 13 | `alpha 0.8` in editor scripts | OK, with reasons for bad values |
| 17 | False errors in the start log | OK (none left on this data; see 4 below for a bare root) |

4, 6, 9 and 11 are covered by unit tests and weren't repeated in the GUI.

## Problems found

Severity: **High** breaks a task, **Medium** misleads or loses work,
**Low** wording, noise or polish.

| # | Sev | Where | Problem | Status |
|---|-----|-------|---------|--------|
| 1 | High | Launching (any launch, Start this game) | A launcher whose `executable` is a bare name found on the system path (`cmd.exe`, round 3's fix) and that sets no `currentDirectory` never starts: the default folder is "the executable's folder", which for `cmd.exe` came out as `<root>\cmd.exe`, so CreateProcess fails with error 267. RetroFE then sat 40 s in "fullscreen detection" (the window frozen) and recorded 40 s of play time for a game that never ran (`r6-24`, `r6-25`). Steps: launcher `executable = cmd.exe`, no `currentDirectory`; start a game. (OnesaUCE's THEMES sets `currentDirectory`, so it wasn't hit.) | Fixed |
| 2 | Medium | Video player, any short menu | With fewer items than the menu's selected slot (Aeon Nox's video list selects its 5th slot; 3 videos), `ScrollingList`'s wrap-around arithmetic underflowed: the list opened on the 2nd video (`r6-51`), and Back from the player after the 1st video landed on the 2nd (`r6-57` then `r6-58`; `r6-60a` then `r6-60b`). The same arithmetic serves every menu (page up/down, selecting by index), so any collection shorter than its layout's selected slot was affected. | Fixed |
| 3 | Medium | Play counts (stock) | RetroFE records plays (`playCount.txt`, `lastplayed.txt`) only when `lastPlayedSkipCollection` is set, for launches, Start this game and videos alike; time played is always kept. On a default install the game screen says "Played: Never" with "Time played: 40 s" (`r6-34`). Upstream's launch code wraps the update in `if (lastPlayedSkipCollection != "")`; round 5 kept that on purpose for videos. | Report: needs a decision (doc note added to MANAGER.md) |
| 4 | Low | Log | Still false warnings: (a) a bare root logs three `[WARNING] [MetadataDatabase] Could not read directory meta/hyperlist|mamelist|emuarc` at every start; (b) a program that ends within a second logs `[WARNING] Invalid item or gameplay duration`; (c) every open of the video list or player logs `Menu tag is missing <itemDefaults>` and `<reloadableImage> ... does not specify an imageType` (Aeon Nox's `_videos` pages and the built-in player), and `--theme-check` on Aeon Nox failed with 4 problems on `player.xml`. (d) Once, while skipping videos: `[ERROR] [GStreamerLog] libav: negotiation failed`; playback carried on. | (a)-(c) Fixed; (d) report |
| 5 | Low | Home (Aeon Nox) | Manage's tagline and description still described round 1's Manage ("Your collections, packages and sources"; "Add a collection from a folder of games... add or remove the sources") (`r6-02`). | Fixed |
| 6 | Low | Editor script (theme editor) | `importfont`, `importsound`, `importsplash` and `import` take paths relative to the process's current folder (`retrofe\`), unlike `browsedir`, which is relative to RetroFE's root: `importfont layouts/Aeon Nox/fonts/Oswald-Regular.ttf` fails with "can't read Oswald-Regular.ttf", sound imports with "not a readable file". Absolute paths work. `sound load none` fails when no load sound is set (nothing to clear) without saying why. | Report only (theme editor area) |
| 7 | Low | Widget editor | A placed Static Image keeps the placeholder `images/background.png` until a file is chosen; every rebuild then logs `[ERROR] [Layout] Failed to find image` (the editor itself warns properly). | Report only (theme editor area) |
| 8 | Low | Games > Hide / Show | Hiding then showing a game leaves the exclude list one byte longer: a file without a final newline gets one (`zokumahj` became `zokumahj\n`). Harmless for RetroFE. | Report (shared LineList) |
| 9 | Low | Games, hidden game | A hidden game's screen shows no artwork, size, played or time (`r6-33`), unlike a shown one. | Report (Games area) |

Not a problem: the one launch attempt of a real launcher (`lr-mame2003_plus`
on `02 ARCADE ALL`, a QA key slip) found no launcher file and logged it;
nothing ran.

## Fixes made

| # | Change | Files | Test |
|---|--------|-------|------|
| 1 | `Library::programFolder`: a launcher with no `currentDirectory` runs in the program's own folder, or in RetroFE's root for a bare name. When CreateProcess or ShellExecute fails, the launcher no longer waits 40 s for a fullscreen window and records no time played. Checked live: the fixture now starts and returns at once (`r6-38`, `r6-39`). | `Library/Files.*`, `Execute/Launcher.cpp`, `docs/LAUNCHERS.md` | LibraryTests: "a launcher's program runs in its own folder, a bare name in RetroFE's" |
| 2 | Wrap-around index arithmetic in a header of its own (`Graphics/Component/ListIndex.h`), reducing both operands modulo the size first; `ScrollingList`'s helpers use it. Checked live: the list opens on video 1, Back lands on the video played (`r6-63`, `r6-64b`, `r6-64c`). | `Graphics/Component/ListIndex.h`, `ScrollingList.cpp` | VideosTests: "a list shorter than the menu's selected slot opens on, and comes back to, the right video" |
| 3 | `<itemDefaults>` in the player pages' menu and `imageType="screenshot"` on the video pages' `reloadableVideo` (Aeon Nox 16:9 and 4:3, built-in). `--theme-check "Aeon Nox"` is clean again; no warnings on opening them. | `layouts/Aeon Nox/collections/_videos/layout/*.xml`, `Builtin/Builtin.cpp` | VideosTests: "Aeon Nox's video pages pass the layout check"; the built-in pages test checks both |
| 4 | A missing optional metadata folder logs INFO; a program that ran under a second logs INFO (time spent unchanged). Checked live on the empty root (`r6-142`: NOTICE lines only). | `Database/MetadataDatabase.cpp`, `Collection/CollectionInfoBuilder.cpp` | GUI (logging only) |
| 5 | Manage's Home tagline and description. | `layouts/Aeon Nox/collections/Home/medium_artwork/*/Manage.txt`, `Scripts/aeon-nox/gen_layouts.py` | GUI (`r6-92`) |
| 3 (doc) | MANAGER.md says plays are counted only with `lastPlayedSkipCollection`. | `docs/MANAGER.md` | - |

Tests: 4,986/4,986 checks pass (`retrofe_tests.exe`; 4,966 before).

Nothing was changed in ThemeEditor/EditorNav*, Manager/Themes/*Screen*,
Library/RetroArch* or the Games/Systems RetroArch forms.

## Recommendations

1. **Play counts** (3): count plays when `lastPlayedSkipCollection` is
   empty (empty = skip nothing, as its name says), or show "Not counted
   (lastPlayedSkipCollection isn't set)" on the game screen instead of
   "Never". The first changes what stock RetroFE writes (new
   `lastplayed.txt` files), so it's a decision.
2. **Editor script paths** (6): resolve `import*` paths against RetroFE's
   root like `browsedir`, and say why `sound KIND none` did nothing.
3. **Theme check in CI**: `--theme-check "Aeon Nox"` would have caught 4;
   running it (or the new unit test's `checkLayoutXml` over every shipped
   page, as LayoutEditTests already scans them) on each build keeps the
   shipped layouts clean.
4. **A launch smoke test**: a fixture like `QAQuick` (cmd.exe running a
   batch file that writes its argument) exercised launching end to end
   without opening a window; worth keeping as a documented QA fixture.
5. **Placeholders in the widget editor** (7): leave a Static Image's `src`
   empty (or skip it in the generated page) until a file is chosen, so the
   log stays free of errors.

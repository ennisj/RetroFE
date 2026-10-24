# QA, round 10: rounds 8–9's controls, media folders, window close and editor polish

Date: 2026-10-24. Build: `feature/management-ui` at f6504a94 plus the fixes
below (RelWithDebInfo, Windows 11). Screenshots, contact sheets (`*-sheet-*`),
logs and saved files are in `E:\Storage\Agents\qa4\screenshots\` (`r10-*`).

Covers round 8 (Settings > Controls, game-controller names in
controls.conf) and round 9 (media folders, closing the window, `--theme-check`
on OnesaUCE themes, theme editor polish): [FORK-CHANGELOG.md](FORK-CHANGELOG.md),
[DECISIONS.md](DECISIONS.md) M19–M20, the follow-ups in
[QA-ROUND8.md](QA-ROUND8.md), and a regression sweep.

## Test installs

All under `E:\Storage\Agents\qa4\`, copied (not moved) from round 8's:

- **a**: copy of `qa3\a` (OnesaUCE-shaped, 150 collections, the `qa-fixture`
  launchers, fixture RetroArch and saves; our Aeon Nox, byte-identical to
  `Package/Environment/Common`). Its `controls1.conf` (K:'s old remap of
  select/back/up... in raw joystick names) was kept, so a Save has two files
  to choose between.
- **b**: copy of `qa3\b` (OneSauce's real layout: `appdata\retrofe`,
  `base_assets`, `content`), Renee. **`controls1.conf` removed**: RetroFE runs
  on OneSauce's own `controls.conf` alone. 30 Atari 2600 ROMs were copied from
  K: into `content\...\Atari 2600\roms` (round 8's slice had copied only the
  folder's top-level files, so the system had no games).
- **firstrun**: only `retrofe\`.

Input went only to this QA's RetroFE windows (matched by exe path); RetroFE
was started with `SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN=1` so it never took the
foreground (see the note under Problems). No native picker or message box
opened (checked after every step); no emulator ran; nothing was written to
K:, I:, `E:\Storage\OneSauce` or the qa3 copies. Files changed for tests were
put back afterwards (controls files, settings1.conf and both Aeon Nox pages
hash-checked).

## What works

### OneSauce's controls.conf, no remap (b)

- RetroFE starts on Renee with **no ERROR or WARNING at all** in the log; every
  OneSauce token binds (`joyDPUp`, `joyLeftY-`, `joyRightShoulder`,
  `joyRightTrigger`, `joyA`, `joyStart`, `joyBack`, `joyGuide`, `joyB`...:
  "Binding controls.select to Controller A" etc.); the connected pad (a Saitek
  side panel) is reported as `joy0`. No unknown-token warnings: there are none
  in that file (`r10-b-001-start.png`, start log in the run logs).
- Settings > Controls on b lists OneSauce's values by name ("Controller D-pad
  up, Controller left stick up", `r10-b-112-controls.png`).
- **Blank `quit =`** (a `controls2.conf` with only that line): Q does nothing,
  closing the window is ignored ("Window close ignored: the quit control is
  bound to nothing"). See problem 3 for the log level.

### Settings > Controls (a, then b)

Each step checked by screenshot (`r10-a-004` … `r10-a-054`; status lines in
`r10-a-sheet-status1.png`, `r10-a-sheet-status2.png`):

- **List**: groups, each control with its inputs described; a control's
  screen shows one row per input with its token, where the value comes from
  ("Set in controls1.conf."), the default and the buttons.
- **Capture by pressing**: posted keys reach capture when posted in the same
  script run that starts it. "Press the input to add now (5 seconds left)…",
  then Space → "Added Keyboard Space. Save to keep it."
- **Duplicate warning**: Q on Select → "Note: it's also Quit's input, so it
  does both."; on b, O on Page down → warned it's Remove from favourites'
  input (wording: problem 2).
- **Hold to cancel**: X held 2.6 s → "Cancelled: nothing was added." while
  still held; nothing added after release (`r10-a-sheet-holdcancel.png`).
- **Remove**: choosing the Keyboard Q row and Remove → back on the control,
  "Removed Keyboard Q. Save to keep the change."
- **Put back the default**: "Back to RetroFE's default: Keyboard Return,
  Controller A, Joystick button 0 (joysticks with no controller mapping)."
- **Undo changes** drops everything; **Save refused** with Back's three
  inputs removed: the Save button says "Back needs at least one input, or you
  couldn't use RetroFE." and the files are unchanged (hash).
- **Save writes one line per value to the right file** (two values changed:
  Select in controls1.conf, Page down in controls.conf): "Saved to
  controls.conf and controls1.conf. RetroFE uses the new controls now." Diff
  against the originals: exactly `select = 1,Return,joyButton0,joyButton7,Space`
  in controls1.conf and `pageDown = PageDown,K` in controls.conf, nothing else
  (`r10-a-controls-after-save.conf`, `r10-a-controls1-after-save.conf`).
  On b (no controls1): one line in controls.conf, the 300+ comment lines and
  CRLF kept, size +2 bytes.
- **New controls work at once**: Space opened a control on the list
  (`r10-a-052`); K paged the Browse menu (`r10-a-sheet-K-pagedown.png`); on b
  the log shows "Binding controls.pageDown to Keyboard K" at Save.
- **Leaving with unsaved changes warns**: Back on the list → "! Your control
  changes aren't saved. Save them, or leave again to drop them."; closing the
  window from a control's screen warns the same, a second close quits and the
  files are unchanged (hash).

### Media folders (b, Renee)

- **ARCADE plays a video from `1 ARCADES/system_artwork/video/`** (round 8's
  black cabinet). With debug logging: `ARCADES 5.mp4` at start, **O
  (removePlaylist)** → `ARCADES 6.mp4` → `ARCADES 7.mp4`, each visibly
  different (`r10-b-sheet-arcade-O.png`).
- **A game list's removePlaylist still removes from favourites**: Atari 2600,
  I on two games wrote both to `playlists/favorites.txt`; O on the second in
  the full list removed it; F (favourites) then O removed the other (the file
  emptied; log "Saving favorites").
- **A file beside the folder wins**: with `system_artwork/video.mp4` added
  beside `video/`, the log plays `video.mp4` only, and O doesn't switch away
  from it (`r10-b-046`, `r10-b-047`). Removed afterwards.

### Closing the window

- From Browse (b): "Window closed: quitting", gone in 0.27 s. From the layout
  editor (a): quit at once (nothing unsaved).
- With Manage open and unsaved changes: warns first, second close quits
  (above).
- **`-kiosk true`**: ignored and logged ("kiosk mode is on (the quit control
  still quits)"); Q then quit.
- **Blank `quit =`**: ignored and logged (above).
- **During the splash** on a first import (b's `meta.db` moved away): closed
  2 s in, "Window closed: quitting once RetroFE has finished starting"; the
  import ran to the end (meta.db rebuilt, byte-identical to before), then
  "Exiting" 16 s later.

### `--theme-check` on OneSauce themes (b)

Each report has the "ignored (OneSauce)" line with counts, separate from
problems (`r10-themecheck-b-*.txt`):

| Theme | Ignored | Problems left (all real: the files are missing on K: too) |
|---|---|---|
| Aeon Nox | 31 uses | 1: `images/background.png` (splash) |
| Renee | 15 | 1: `collections/Jukebox/.../images/selected2.png` |
| Amiga Memories | 2 | 6: the same Jukebox image, its four `sounds/*.wav`, `images/splash.png` |
| Cafe80s | 508 | 4: `splash 16x9.png`, three `text/*.txt` its 1 HANDHELDS page names (see problem 6) |

Our Aeon Nox (a): "OK: nothing wrong found", exit 0.

### Theme editors

Layout editor on our Aeon Nox with *Also apply to layout 4x3.xml*, scripted
**without any `wait` lines** (`E:\Storage\Agents\qa4\layout-script10.txt`,
results in `r10-a-log-editor-script.txt`); every command ran in order:

- **Twin align**: manufacturer (x 145, 4:3 x 90) and rating (620 / 510),
  *align left* → both 145 here and **both 90 on the 4:3 page** ("Done on
  layout 4x3.xml too, with its own elements and boxes"); one undo back to
  620 / 510.
- **Twin distribute**: genre nudged down, then manufacturer, genre, year
  spaced: 16:9 135 / 217 / 298, 4:3 138 / 224 / 310 (each page's own middle).
- **All N at once** (`attrall fontColor ff0000` on two): `shared` went from
  `75c8f7` to `ff0000`, both elements on both pages changed ("Copied to
  layout 4x3.xml"), one undo put both back.
- **Guides** (`guides on`): rating dragged up 77 snapped to y 135
  (manufacturer's), without guides 140; the twin got the move in proportion
  (134), as decided.
- **Structural edits on sure matches only**: duplicate and delete of score
  reached the 4:3 page; deleting wheel item 0 or 8 stayed on layout.xml
  ("isn't surely the same element on layout 4x3.xml (another slot of a wheel
  of another length...)"); a new onEnter set, a new onMenuScroll event and an
  added image went to both (`layout-script10b.txt`); each undone in one step.
- **Save**: the diff of both saved pages against the shipped ones is one
  attribute each (`x="620"` → `640`; 4:3 `x="510"` → `527`), CRLF kept
  (`r10-layout-saved.xml`, `r10-layout 4x3-saved.xml`). **Put back the
  original** on both made them byte-identical to the shipped pages (hash).

Widget editor on New Theme 1 (`widget-script10.txt`, no waits): `shared`
shows *(several values)*, `layerall 7` / `alphaall 50` apply to both and undo
in one step each, duplicate/undo of the whole selection, `discard`: every
file of the theme unchanged (hashes).

### Regression sweep and first run

- **16:9** (a): Home, Settings, every Manage section (Games, Menus, Systems,
  Videos, Emulators, Themes, Sources: `r10-a-sweep169-sheet.png`), an emulator
  (QARetro) and its **RetroArch** screen (`r10-a-093`), a game's screen and
  **Saves** (Sonic: 7 files listed; Back up now made a snapshot with exactly
  those 7 and `backup.txt`), Catalog and a package page, Browse Main and a game
  list. Start log: one WARNING, true for this copy (`baseLayoutPath` names a
  folder qa2 never had).
- **4:3** (960x720, `randomLayout = layout 4x3`): Home, Manage sections
  (`r10-a43-sweep-sheet.png`), Settings, Controls and a control
  (`r10-a43-sheet-controls.png`), Catalog, Browse (`r10-a43-sheet-catalog-main.png`).
  Nothing cut off. (Without `randomLayout` the 16:9 pages are squeezed, as
  expected.)
- **First run** (empty root): Manage with the welcome note, Themes "No themes
  are installed…", Emulators with Add a launcher (`r10-fr-sheet.png`). Log:
  NOTICE lines only (`r10-fr-log.txt`). Window close quit it.

## Problems found

Severity: **High** breaks a task, **Medium** misleads or loses work,
**Low** wording, noise or polish.

| # | Sev | Where | Problem | Status |
|---|-----|-------|---------|--------|
| 1 | Low | Settings > Controls, a control emptied but not saved | After removing all of Back's inputs (not saved), the control's note said "Set in controls1.conf. Changed: not saved yet. **It's empty there, so nothing is bound to it.**" — it isn't empty there; that line is for a control left blank in the file (`quit =`). Steps: a, Settings > Controls > Back, remove its three inputs (`r10-a-026-save-refused.png`). | Fixed (`r10-a-143-removed3.png`) |
| 2 | Low | Settings > Controls, duplicate notes | "Keyboard O is also **Remove from favourites's** input" (and "Note: it's also … 's input, so it does both"). Steps: b, Controls > Page down, add O (`r10-b-114-pd-O-dup.png`). | Fixed: "is also an input of Remove from favourites" |
| 3 | Low | Start log with `quit =` | A deliberately blank quit (the documented kiosk setting) logged `[WARNING] controls.quit is empty` at every start, the only warning on b. | Fixed: a NOTICE for quit ("…and closing the window is ignored"); an empty up/down/left/right/select/back still warns |
| 4 | Low | Manage, unsaved changes, Back then window close | After Back on the Controls list warned "leave again to drop them", a window close quit at once, dropping the changes: the close counts as the "again". Consistent with CONTROLS.md ("closing again leaves"), but the warning was about Back. | Report only (recommendation 2) |
| 5 | Low | Capture cancelled | "Cancelled: nothing was added." and "Changes undone." are shown in the success (green) style; a cancel reads like a result. | Report only |
| 6 | Info | Cafe80s, 1 HANDHELDS | Three `<scrollingText src="text/…">` aren't found beside that page or in the theme; the files are in `collections/_common/text`. Either a broken page in Cafe80s or a lookup OneSauce does that RetroFE doesn't (unchanged since round 8). | Open question |
| 7 | Info | Test method | The first start of b (before the no-activation hint) took the foreground and later exited by itself with no "Window closed" line, most likely a Q typed by the user landing in it. RetroFE raises its window at start by design. All later runs used `SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN=1` and checked that RetroFE wasn't in front. | Harness fixed (`scratchpad\qa4-run.ps1`) |

Not problems: a guided widget-editor `move 3 3` that snaps back onto the page
centre where it started makes no change, so the next `undo` reports nothing
to undo; deleting a wheel slot stays on one page by design (M20).

Not tested here (another agent is working there): Manage's media screens and
the library's media resolution for folders (M20 says they don't show folders
yet).

## Fixes made

| # | Change | Files | Test |
|---|--------|-------|------|
| 1 | "It's empty there" only when the value saved in the file is empty | `Manager/Controls/ControlsProvider.cpp` | ControlsTests: "emptying a control here isn't called empty in the file, and shared inputs read plainly" |
| 2 | Shared-input notes say "is also an input of X" (three places) | `Manager/Controls/ControlsProvider.cpp` | the same test |
| 3 | `quit =` logs a NOTICE, not a WARNING | `Control/UserInput.cpp` | Live: b with `quit =` logs only that NOTICE (`r10-b-log-blankquit-fixed.txt`) |

Tests: 6,550/6,550 checks pass on Windows (6,541 before); 6,515/6,515 on Linux
(WSL, g++; some checks are Windows-only), the new test included.

## Recommendations

1. **Cafe80s text files** (6): check on a OneSauce cabinet whether its
   1 HANDHELDS top bar shows instructions; if it does, RetroFE should also try
   `collections/_common/` for a `scrollingText` src, else it's the theme's bug.
2. **Unsaved-changes warning per way of leaving** (4): reset the "warned once"
   state when the next attempt is a different kind (Back vs window close), so
   a close after a Back warning warns again.
3. **Neutral style for cancelled/undone** (5): a third status style, or the
   plain note style, for messages that report nothing happened.
4. **Test helpers**: start RetroFE with `SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN=1`
   in every GUI check, so a check never takes the user's keyboard.

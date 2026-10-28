# QA, round 12: the design review as built (press counts, cross-branch problems, parcel status)

Date: 2026-10-28. Branch `feature/management-ui` at 1170a08c. **Done by reading
the code only**: no build toolchain or display was available, so nothing here
was run. Every count and finding is traced to the forms
(`Forms/BuiltinForms.cpp`, `Manager/*/*Forms.cpp`), the engine
(`Forms/Form.*`, `Graphics/Component/FormWidget.*`, `FormHelp.*`,
`KeyboardWidget.*`, `Forms/KeyboardKeys.h`, `Manager/ManagerUI.*`), the
providers, the Aeon Nox form pages and the built-in layout. What only a GUI run
can tell is listed at the end.

Covers the four round-12 branches ([FORK-CHANGELOG.md](FORK-CHANGELOG.md),
[DECISIONS.md](DECISIONS.md) M22–M23) against [UX-REVIEW.md](UX-REVIEW.md)
sections 2 and 4.

## Summary

The round delivers what the review asked for: every path field opens the
controller browser, Save and the other buttons are pinned with Page Down
jumping to them, Aeon Nox is two panes with a help panel, statuses carry
severity colours, every Manage screen counts the changes waiting and the hub
can apply them, Games puts results straight after Search, and the keyboard
has a path row and cursor keys. **Press counts fall on nine of the ten tasks**
(the tenth, backing up saves, is level); the first-run "add a system" goes
from a task that needed a keyboard (~350–550 presses, most of them typing two
paths) to about 70, and "start a game from Manage" from ~17 to 9.

The cross-branch problems are all at the seams, and none is a dead end for a
controller-only user: Back always works, and no screen traps focus. The ones
that matter:

- **Letter jump is unreachable on a bare install with a pad** in Games: the
  Games list takes it only from `letterUp`/`letterDown`, which the built-in
  controls don't map; the file browser takes it from Left/Right. The hint
  names raw controls ("LetterUp/Down: letter") that don't exist (MEDIUM).
- **A wrong green hint on Sources**: typing a source name turns "Page down:
  Check for updates" green, because the pinned bar there is Check for
  updates, not Add source (MEDIUM).
- **After Hide, focus lands on "Show this game again"** (held back by the
  guard), not on the previous link as the changelog, MANAGER.md and the
  code's own comment say (MEDIUM).
- **Settings says "the next time RetroFE starts"** while its Save reloads
  RetroFE at once, which re-creates the window (MEDIUM, plausible).
- **The one hint row probably doesn't fit** on a results row of a screen with
  a bar at 1280x720 and in Aeon Nox 4:3; what's cut is "Page down: Save" and
  "Back: return", the useful part (MEDIUM, plausible, needs measuring).

Then a tail of LOW items: hints that promise something that does nothing on
note-only screens, Menus' artwork screen not pinned like Games' and Systems',
a doubled "Select again to confirm.", docs that still say "Reset" or "two
hint lines".

Findings: **0 HIGH, 5 MEDIUM, 11 LOW** (12 CONFIRMED, 4 PLAUSIBLE).

## 1. Press counts after round 12

Assumptions, stated once:

- Aeon Nox 16:9 at 1920 x 1080 unless said. Home starts on **Browse** when
  there's content (`Manager/Home.h:36-45`, `RetroFE.cpp:928-931`), so from
  Home: Manage is 1 press (Left), Settings 2 (Left, Left), Catalog 1 (Right,
  if the wheel wraps; 3 otherwise). The review counted from Catalog, where a
  reload used to leave Home.
- Each screen opens with focus on its **first focusable row** in document
  order (`FormWidget.cpp:781-788`), unless an action asked for a field
  (`Result::focus`) or you came back to it with Back (focus restored,
  `ManagerUI.cpp:517`). A bar button is never the first choice while a row
  can take focus.
- **Page Down** jumps from anywhere to the bar's first button that can run
  now and doesn't confirm (`Form.cpp:619-657`, `:659-664`); Left/Right move
  along the bar; Page Up returns.
- A library like the review's root `b` (~130 systems, thousands of games):
  the Games list shows 300 of them, so finding a game means **typing a
  search** (3–5 characters, ~25 presses on the keyboard, which starts on the
  key last typed) rather than scrolling. "Find a game" below is Home > Manage
  (Left, Select), Games (Select), Search (Select, type, Done), Down to
  Filters, Down to the first result, Select = **7 presses + ~25 typing**,
  assuming the search leaves the wanted game first.
- Text entry stays ~4–5 presses per character as the review estimated.
- The file browser opens **straight in a folder** when the field has a value
  or a `start=` suggestion (`BrowserProvider.cpp:113-128`), else on the
  places list. In a folder a file field focuses the first entry (`..`); a
  folder field focuses **Use this folder** first (`BrowserForms.cpp:30`), so
  each level costs Select + Down + the walk to the entry. Letter jump
  (Left/Right) is on in the browser (`BrowserForms.cpp:33`).

| # | Task | Before (review) | After | Path counted |
|---|------|-----------------|-------|--------------|
| 1 | **Add a system** (first run) | ~14 + name (~30) + two paths typed (~300–500) | **~40 + name (~30)** | Manage opens by itself (`RetroFE.cpp:955`) on **Add your first system** (first focusable, `BuiltinForms.cpp:71`): Select. `system-add` opens on Name: Select, type, Done. Down (List it in), Down (Games folder), Select → browser on the places list (no value, no start): Down ~4 to the drive, Select; per folder level Down (past Use this folder) + ~2 (letter jump/Down) + Select; two levels, then Select on **Use this folder** ≈ 15. Back on the form on the folder field: Down (Extensions, default zip), Down (Launcher: "A new emulator program..." is the first option so the Emulator field is already shown, `SystemProvider.cpp:1620`), Down (Emulator), Select → browser again, drive, two levels, the .exe among the files ≈ 15. Down (Arguments, default fine), Down, Select **Add system** (`SystemForms.cpp:239`, not pinned: it's the next row anyway). |
| 2 | **Fix a game's title** | ~22 + search (~25) + edit (~20) | **11 + search + edit** | Find a game (7). The game's screen opens on **Metadata** (first link; values aren't focusable): Select. Metadata opens on **Title**: Select, edit (cursor starts at the end; `<-`/Home on the path row to go back), Done. **Page Down** → Save (`GamesForms.cpp:101-104`), Select. |
| 3 | **Replace a game's video** | ~16 + path typed (~150–250) | **~20 + search** | Find a game (7). Down 2–3 to **Media** (Metadata, Launcher, [RetroArch], Media), Select. Media lists Shown then Missing (`GamesForms.cpp:146-147`): Down ~1 to the video row, Select. The slot opens on **Replace from a file** (no media folder) — Select → the browser opens **in the slot's own folder** (`start="game:slot:place"`, `GamesForms.cpp:164`) on `..`: Down/letter jump ~3 to the file, Select ("Chose ..."). Back on the slot with the field focused and the hint green: **Page Down** → Use this file, Select. If the video is elsewhere: Back to the places, Downloads (Down ~3, Select), find it (+~8). Videos show size and date, no preview (F4). |
| 4 | **Hide a game** | ~12 + find | **11 + search** | Find a game (7). **Page Down** lands on Start this game (first button that doesn't confirm), Right to Hide this game, Select (arms, question drawn over the bar), Select (confirms). Focus then sits on "Show this game again", guarded (finding 3). |
| 5 | **Change a launcher's arguments** | ~25 + edit at the end of ~60 chars, no cursor | **~18 + edit anywhere** | Home > Manage (2). Down 4 (Games, Menus, Systems, Videos, Emulators), Select. Emulators opens on **Add a launcher** (`EmulatorsForms.cpp:18`): Down to Search, Select, type ~3, Done; Down, Select the launcher. Its screen opens on **Used by**: Down to Edit, Select. Edit opens on Program: Down 2 (Arguments preset, Custom arguments), Select → keyboard with the path row: `<-` or Home + `->` to the spot (1 press per character moved once the key is focused, or hold letterUp/letterDown where mapped), type, Done. **Page Down** → Save, Select. |
| 6 | **Switch theme** | ~13 via Manage; ~15 via Settings ("Layout") | **12 via Manage; 10 via Settings ("Theme")** | Manage: Left, Select, Down 5 to Themes, Select; Themes opens on **New theme** (`ThemesForms.cpp:17`): Down ~2 to the theme, Select; the theme opens on **Use this theme** (first button, `ThemesForms.cpp:45`): Select. Settings: Left, Left, Select; opens on **Controls**: Down 4 (Fullscreen, Screen size, Hide mouse, **Theme**), Right, **Page Down** → Save, Select (reloads at once, `ManagerModel.cpp:1107`). |
| 7 | **Add a source and install a package** | ~22 + name (~40) + URL or path (~150–300) | **~18 + name (~40) + URL; ~26 with a folder source, no path typed** | Home > Catalog: Right (wraps) or Left 3; Select → with no packages the **Sources** screen opens (`RetroFE.cpp:3651-3657`). It opens on the RetroFE-updates toggle "I trust this source" (first focusable: Open the Catalog is hidden, the sources list is empty, `BuiltinForms.cpp:98-103`): Down 2 to Name, Select, type, Done. Down (Type; Right once for a folder), Down (Address: Select, type URL, Done — or Folder: Select → places, Down ~5 to the removable drive, Select, Select **Use this folder** ≈ 8), Down (I trust this source), Select, Down, Select **Add source**. "Added X." and, as soon as the index has loaded, focus moves to **Open the Catalog** (`ManagerModel.cpp:990`): Select → Manage closes on the Catalog list: Left/Right ~1 to the package, Select. The package screen's rows are notes, so focus is in the bar on **Install**: Select. Focus goes to Back to the Catalog, then to **Show it in Browse** when the install finishes (`ManagerModel.cpp:948`): Select → reload, Browse. |
| 8 | **Remap Select** | ~26 | **16** | Home > Settings (Left, Left, Select); opens on **Controls**: Select. Controls opens on the first Essential row (up): Down 4 (down, left, right, **select**), Select. The control opens on its first input row: Down 3 past the inputs to **Add an input (press it now)** (`ControlsForms.cpp:67`), Select, press the button. "Added ... Save to keep it." **Page Down** → Save, Select. (The hint stays plain, not green: finding 12.) |
| 9 | **Start a game from Manage** | ~17 + find (reload flashed Home) | **9 + search** | Find a game (7). **Page Down** lands on **Start this game** (first non-confirming button), Select. With changes waiting Manage closes, RetroFE reloads, then the game starts; afterwards Manage reopens on the game's screen with focus back on Start this game (`ManagerUI.cpp:1259-1266`) — the Games search text isn't restored (F8). |
| 10 | **Back up saves** | ~10 + find (focus started on Back up now) | **9 + search** (level) | Find a game (7). Down 5–6 to **Saves** (last link), Select. Saves opens on the **This game's files** link (`GamesForms.cpp:254`), so **Page Down** → Back up now (Delete these saves confirms, so the jump skips it), Select. The Backups section is in the rows under the bar; it fits at 1080p (title, 2 notes, 2 values, link, header, list). |

The recurring costs the review named: **finding a game** is now Select,
type, Down, Down (the filters are one link away); **reaching Save** is one
press everywhere a form has a bar; **paths** are a few Selects and Downs in
the browser, with the typed path gone from every task.

## 2. Cross-branch problems

Severity: **HIGH** blocks a task with a controller; **MEDIUM** misleads or
costs the user presses or confidence; **LOW** wording, docs or polish.
CONFIRMED: traced end to end in the code. PLAUSIBLE: depends on font metrics
or a path not fully traced.

| # | Sev | Status | Where | Problem | Suggested fix |
|---|-----|--------|-------|---------|---------------|
| 1 | MEDIUM | CONFIRMED | Games list, bare install, pad | The Games results have no `jump="letter"` (`Games/GamesForms.cpp:33`), so Left/Right page (`FormWidget.cpp:968-975`) and only the `letterUp`/`letterDown` controls jump by letter (`:1004-1006`). The built-in controls map **no** letterUp/letterDown (`Builtin/Builtin.cpp:43-53`: up/down/left/right/pageUp/pageDown/select/back/quit), so on a bare install a pad can't letter-jump in Games at all, while the file browser (`Browser/BrowserForms.cpp:33`) does it with Left/Right. M23 says "one letter jump everywhere"; F1 asked for it in the results. The hint shows `LetterUp/Down: letter` on every list (`FormWidget.cpp:1338`) whether or not those controls exist — and names raw controls where every other hint says "Page down" / "Back". The shipped `Package/Environment/Common/controls.conf:5-6` maps them to M/N and raw `joyButton1/2/9/10`. | Give `<results bind="game:search">` (and the other long lists: systems, emulators, menus tree, playlists) `jump="letter"` so Left/Right jump as in the browser; map `letterUp`/`letterDown` in `Builtin::controls` (e.g. the triggers or X/Y) or drop the `LetterUp/Down` hint when the controls aren't bound; spell it "Letter up/down" like the editors (`ThemeEditorScreen.cpp:1060`). |
| 2 | MEDIUM | CONFIRMED | Catalog > Sources, after typing | The Sources form's only `<actions>` is **Check for updates** (`Forms/BuiltinForms.cpp:113-115`), so that's the pinned bar; **Add source** sits in a section (`:104-112`). Finishing an edit of Name or Address sets `edited_` (`FormWidget.cpp:1185-1186`) and the hint turns green: **"Page down: Check for updates"** (`:1370-1380`) — pointing the user at the wrong button right after they typed a source. The same B2 hint fires on any form whose bar isn't its save: none other today, but the rule is "any edit, any bar". | Either put Add source in the pinned bar (`<actions><button Add source/><button Check for updates/></actions>` — the jump then lands on Add source, which can run) and `pin="no"` is not needed, or colour the hint only when the target button's action is the screen's save (a `save="yes"` attribute on the button, or when the bar's first button is labelled Save/Add/Use). |
| 3 | MEDIUM | CONFIRMED | Game > Hide this game | After Hide, the button that was focused in the bar is gone; `resolveFocus` takes `barTargetIndex()` (`FormWidget.cpp:730-739`), which returns the first "ready" button — **Show this game again** (no confirm, no problem) — and only flags it Guarded (`Form.cpp:756-770`). So focus lands on the undo, hint "Move, then Select: Show this game again". The comment above the code ("never an undo or destructive one just because it moved into its place", `:727-729`), MANAGER.md:277-280 ("never onto a button that has just appeared") and FORK-CHANGELOG.md:35 ("Hide → the previous link") all say otherwise; `hideAction` sets no `Result::focus` (`Games/GameProvider.cpp:884-903`). The guard stops a double press, so nothing is lost — but E3 isn't done for Hide. | In `resolveFocus`, when the bar target is Guarded, fall back to `returnField_` (the row the jump came from) and then to the nearest row, as the comment says; or set `result.focus = "game-metadata"` in `hideAction`. Same code path serves Install (fine: Back to the Catalog was shown before) and Undo changes (fine: Save). |
| 4 | MEDIUM | PLAUSIBLE | Settings > Save | `saveSettings` returns `reload = true` with "Saved. Display changes apply the next time RetroFE starts." (`Manager/ManagerModel.cpp:1105-1108`), and the Display section carries the note "Display changes take effect the next time RetroFE starts." (`BuiltinForms.cpp:47`). A reload sets `reboot_` and quits `run()` (`RetroFE.cpp:2623-2628`); `Main.cpp:389-392` then constructs a new `RetroFE`, so SDL and the window are re-created with the new size — the display change applies **at the reload the Save itself triggers**, not "next time". The user sees RetroFE restart and reads that it hasn't applied yet. Not traced: whether `SDL::initialize` re-reads `horizontal`/`vertical` after `config.clearProperties()` (it's the normal start path, so it should). | Say what happens: "Saved. RetroFE reloads now." and drop the note; or, if the window is kept across a reload, don't `reload` from Settings when only display keys changed. One wording for both places either way. |
| 5 | MEDIUM | PLAUSIBLE | Every results row on a screen with a bar, 1280x720 and Aeon Nox 4:3 | The hint is one row, cut where it stops fitting (`FormWidget.cpp:1931-1946`, M23). On a results row of a form with a bar it reads `Select: open   Left/Right: page   LetterUp/Down: letter   Page down: Save   Back: return` (`:1335-1338, 1370-1382`) ≈ 88 characters; with "Use this file" 97. The built-in page is 820 wide at 22 px (hint 18.7 px, ~80 characters of room); Aeon Nox 4:3 is 1020 wide at 38 px (hint 32 px, ~65 characters); 16:9 1170 wide (~75). So on a control's row in Settings > Controls, a folder file in a media slot, a backup in Saves, a system's games row: **"Page down: Save" and "Back: return" are the pieces cut**, and `LetterUp/Down: letter` (unmapped on a bare install, finding 1) is what keeps the room. MANAGER.md:801-802 still says "Hints use up to two lines so the letter and bar jumps remain readable at 1280x720", the opposite of M23. | Put the pieces in order of worth (Select, Page down, Back, then Left/Right, then letter) so the cut takes the least useful; drop the letter piece when the controls aren't mapped; fix MANAGER.md:801. Measure on the three pages (see the Windows list). |
| 6 | LOW | CONFIRMED | Package screen, a saves backup, any note-only screen with a bar | When no row can take focus, focus sits in the bar and the hints add **"Page up: back"** (`FormWidget.cpp:1370-1374`). Page Up there asks `jumpFocus` for `returnRow`/the last focusable row, finds none and stays in the bar (`Form.cpp:621-628`): the hint promises something that does nothing. Screens: `package` (`BuiltinForms.cpp:140-158`), `game-saves-backup` (`GamesForms.cpp:278-295`), `source` with no packages, `emulator-file`-like screens whenever they gain a bar. | Add "Page up: back" only when a row can take focus; make Page Up/Down from the bar scroll the notes a page when none can (today only Up/Down scroll, a row at a time: `FormWidget.cpp:823-826`). |
| 7 | LOW | CONFIRMED | Note-only screens without a bar ("This game's files", "The file", a system's Check, a theme's problems) | `FormWidget::jump` (`:851-864`) calls `jumpFocus`, which with no focusable rows returns `row = -1` (`Form.cpp:641-644`), so **Page Up/Page Down do nothing**; Left/Right page (`FormWidget.cpp:976-980`). CONTROLS.md:93-94 ("without a bar, moves a page up/down") and MANAGER.md:255 say they page. | In `jump()`, when no row is focusable, `scrollRows` by `pageRows_` as `moveFocus` does; or fix the two docs to "Left/Right page". |
| 8 | LOW | CONFIRMED | File browser, a file row | The hint for a results row with an action says **"Select: open"** (`FormWidget.cpp:1336`); a file's action is `browse.pick:` (`Browser/BrowserProvider.cpp:501`), which chooses it and returns to the field. The note above says "Choose one to use it", the hint says open. | Let a `Forms::Row` carry a `selectHint` ("choose"), or special-case rows whose action starts with `browse.pick`. |
| 9 | LOW | CONFIRMED | Menus > a menu > Info and artwork > Artwork > a slot | `menu-medium` keeps **Use this file / Add it to the folder** in a "Replace" section and **Clear / Clear the folder / Put back the original** as loose rows (`Menus/MenusForms.cpp:160-165`), while the same screen in Games (`GamesForms.cpp:168-176`) and Systems (`SystemForms.cpp:108-114`) pins them. So in Menus there's no Page Down to "Use this file" after a pick, no green hint, and the layout differs from the other two. `menu-medium-file`, `menu-entry` (Remove from this menu, `:86`), `menu-source` (`:112`) likewise have loose buttons where Games and Systems use `<actions>`. The theme screen's Use/Edit/Duplicate (`ThemesForms.cpp:45-47`) and the add forms' final button (`SystemForms.cpp:239`, `EmulatorsForms.cpp:114`, `ControlsForms.cpp:86`) are loose by design and cost nothing (first or next row). | Move Menus' medium, medium-file, entry and source buttons into `<actions>` as Games' are. |
| 10 | LOW | CONFIRMED | Menus > a menu > Actions > Delete | The button is still **"Delete"** under a "Delete" header (B5 asked for "Delete this menu"), and its `confirm` text is "Delete it? Select again to confirm." (`MenusForms.cpp:186-188`) — the widget appends " Select again to confirm." itself (`FormWidget.cpp:521`, and in the bar `:1573`), so the row reads "**! Delete it? Select again to confirm. Select again to confirm.**" | Label "Delete this menu", `confirm="Delete this menu? Its games stay in their systems."`. |
| 11 | LOW | CONFIRMED | On-screen keyboard hints | `KeyboardWidget.cpp:198-205`: a one-line keyboard says "Select: type   Back: cancel   Shift: symbols   LetterUp/Down: cursor" — no **"Done: keep"** (the multi-line one has it, `:200`), raw control names again, and on a bare install the letter controls don't exist; the path row's `<-`/`->`/Home/End do the job there, unmentioned. | "Select: type   Done: keep   Back: cancel   Shift: symbols   Letter up/down: cursor" (last piece only when mapped). |
| 12 | LOW | CONFIRMED | Settings > Controls > a control > Add an input | A capture sets the model directly (`ManagerUI.cpp:755-768`: `model_.set(bind, token)`, then the action) and never calls `widget_->markEdited()`, so after "Added Keyboard Space. Save to keep it." the hint stays a plain "Page down: Save" — the green B2 cue fires for a typed or picked change but not for the Controls screen's main way of changing something. | `widget_->markEdited()` beside `widget_->invalidate()` in the Captured branch (only when the action result was ok and not info). |
| 13 | LOW | PLAUSIBLE | Title row with changes waiting, long titles | The title detail (`titleDetail="info:pendingChanges"`) may take up to **60 %** of the title row (`FormWidget.cpp:1747-1756`, `splitRow(..., 0.6f)`), and the title is ellipsized to the rest. "2 changes apply when you close Manage" ≈ 520 px in Aeon Nox 16:9 (1124 px of row), leaving ~600 px for a 49 px title: "Teenage Mutant Ninja Turtles: Turtles in Time" is cut; at 4:3 (974 px) and on the built-in page (794 px) anything past ~30 characters is. The help panel repeats the row's label, not the screen's title, so the full title isn't shown anywhere. The ImGui renderer prints it on its own line (`ManagerUI.cpp:1318-1321`). | Shorter text when the title needs the room ("2 changes waiting"), or draw the indicator on its own line under the title when `splitRow` would cut the title. |
| 14 | LOW | CONFIRMED | Docs vs code: vocabulary | `docs/design/manage-forms.md:247` ("Save/Reset screens"), `:253` ("Undo/Reset"), `:391` ("with Save and Reset") and `docs/MANAGER.md:954` ("Undo changes, Reset") keep the pre-E2 word; every form now says **Undo changes**. `docs/CONTROLS.md:67-70` says the Manage screens use only up/down/left/right/select/back and that only the theme editor adds pageUp/pageDown/letterUp/letterDown — contradicted by `ManagerUI.cpp:877-885` (all ten reach a form) and by CONTROLS.md's own rows 93-94. `Manager/ManagerUI.h:117-120` has the same stale sentence ("Layout-drawn screens use the first six"). | Replace "Reset" with "Undo changes" in the four places; rewrite CONTROLS.md:67-70 and the ManagerUI.h comment. |
| 15 | LOW | CONFIRMED | Docs vs code: hints and paging | `docs/MANAGER.md:801-802` (two hint lines at 1280x720) contradicts M23 and `FormWidget.cpp:1646` (`hintsHeight = row`); `MANAGER.md:255` and `CONTROLS.md:93-94` (Page Up/Down page without a bar) are wrong on note-only screens (finding 7). `MANAGER.md:789` documents `titleStatus` (default `info:titleStatus`, which no provider serves) beside `titleDetail` (`:791`); when both are set the detail wins (`FormWidget.cpp:1743`). | Fix the three lines; drop `titleStatus` from the reference or say it's unused. |
| 16 | LOW | PLAUSIBLE | Built-in page, Manage hub | Without a help panel a link's `help` is appended to the hint row (`FormWidget.cpp:1383-1385`). The hub's Games help is 130 characters; after "Select: open   Back: close   " about 50 of them fit at 1280x720 and the painter ends them in "..." — the welcome/help text is never readable in full on the built-in page (it is in Aeon Nox's panel). Design-accepted in M23; noted because the first-run path is exactly where the built-in page is used. | Either accept, or on pages without a panel show the focused link's help as a wrapped note under it (as fields' help already is, `:537-539`). |

Not problems, checked: the browser's screens leave with Back at every level
(places → cancel; folder → places or the parent, `BrowserProvider.cpp:550-567`,
`applyPick` pops them all, `ManagerUI.cpp:1073-1094`); a pick marks the
screen edited and leaves focus on the field with the green jump hint; the
Guarded focus on file fields and new buttons needs a move first and the hint
says so; Up from the first row and Down from the last go into the bar and
round again, never off the screen; the keyboard swallows Page Up/Down and
turns letter up/down into cursor moves (`ManagerUI.cpp:946-963`); the
unsaved-changes warning still comes before "Apply changes now" can drop
anything (it's only on the hub, depth 1, after any deeper screen has already
warned); the Catalog page, Home and `_menu` wheels have `repeatItems="no"`;
Aeon Nox has 16:9 and 4:3 pages for the forms, the browser, the keyboard and
the package; the browser's rows carry no severity, so its page lacking
`warningColor` is harmless; the hub shows the welcome note in the panel only
while a row without help is focused, which is the first-run link, as
intended.

## 3. Parcel status (UX-REVIEW section 4)

| Item | Status | From the code |
|------|--------|---------------|
| A1 | Done | `Browser/*`, `file-browser` form, pushed as screens (`ManagerUI.cpp:1046-1071`); places, drives in the background, counts, Use this folder, Back goes up; Browse with Windows... after a mouse click (`BrowserProvider.cpp:430-432`, M23). |
| A2 | Partly | Media fields open in the slot's folder (`start=`), pictures ≤ 8 MB as thumbnails (`BrowserProvider.cpp:497-499`), and the panel shows the focused picture large; **no "video, 1:23"**: videos show size and date only (MANAGER.md:224). |
| B1 | Done | `Forms::pinnedActions`, `FormWidget::drawBar`, Page Down/Up jump (`Form.cpp:619-657`), shoulders in `Builtin::controls`. |
| B2 | Done | Green "Page down: Save" once edited (`FormWidget.cpp:1376-1379`); misfires on Sources (finding 2), misses captures (12). |
| B3 | Done | Reasons only while focused, in the hints or the panel (`FormWidget.cpp:1347-1355`, `:1405-1412`). |
| B4 | Done | `<formHelp>` (`FormHelp.*`, `PageBuilder.cpp:1244-1322`), Aeon Nox 16:9 and 4:3 two panes, `panel="yes"` notes, `preview=`. |
| B5 | Not done | Menus' Actions still "Delete" under "Delete", with a doubled confirm (finding 10). |
| C1 | Done | Aeon Nox form 38 px, notes 32 px (`noteScale` 0.85), `noteAlpha` 0.8, rows 56; 4:3 the same. |
| C2 | Done | `Forms::Severity` on statuses, values, notes, row details, link statuses; `warningColor`/`successColor` on all three pages. |
| C3 | Partly | Intro notes moved to the panel on Systems, Emulators, Menus, Themes, Metadata, Description, Media, Controls, RetroArch (`panel="yes"`); on the **built-in page** (no panel) they are still full rows. |
| C4 | Done | `horizontal = auto`, `vertical = auto` in `Builtin::settings` (M23: largest 16:9 within 90 % of the display). |
| D1 | Done | "Add your first system" link first on the hub (`BuiltinForms.cpp:71`); `collections_` re-read on every screen change (`ManagerModel.cpp:218`), so a system added this session clears the welcome. |
| D2 | Done as decided | `Home::landing(hasContent)`: Browse whenever there's content, Manage on a bare install (`RetroFE.cpp:928-931`); the review's "else back on Manage" was not taken (M22). |
| D3 | Done | Order kept, decision recorded in M22. |
| D4 | Done | `repeatItems="no"` on Home, Catalog and `_menu` wheels. |
| E1 | Done | `info:pendingChanges` in every title (`titleDetail`), `PendingChanges.h`, "Apply changes now" with reopen after the reload (`ManagerModel.cpp:1033-1046`, `ManagerUI.cpp:447-462`); Catalog installs count. |
| E2 | Partly | Save / Undo changes / Put back the original / Saved in / Theme are consistent across the forms; left: Settings' two "next time RetroFE starts" (finding 4), Menus' "Delete" (10), and "Reset" in the docs (14). |
| E3 | Partly | Install → Back to the Catalog then Show it in Browse (`ManagerModel.cpp:944-948`); Start → back on Start this game (`ManagerUI.cpp:1266`); **Hide → lands on Show this game again, guarded** (finding 3). "Installed." dedupe not checked (ImGui and page share `package:installedStatus`). |
| F1 | Partly | Results straight after Search, one Filters screen with the summary on the link (`GamesForms.cpp:28-46`); letter jump only via `letterUp`/`letterDown`, not Left/Right, and unmapped on a bare install (finding 1). |
| F2 | Done | `game:launcherProblem` one line + "Add a launcher called ..." rows opening `emulator-new` with the name (`GamesForms.cpp:125-126`, `GameProvider.cpp:917-937`). |
| F3 | Done | `game:hiddenNote`; Metadata, Media, Description stay for `isKnown`; Show this game again in the bar (`GamesForms.cpp:54,67-78`). |
| F4 | Partly | Shown / Missing groups with severity (`GamesForms.cpp:146-147`), thumbnails in the browser and the slot's picture in the panel; **no video first frame or duration** (`slot:image` is empty for videos, `GameProvider.cpp:1523`). |
| F5 | Done | Favourite toggle with page choice; "New playlist" rows when there are none (`GamesForms.cpp:199-212`). |
| F6 | Done | Files collapsed into the "This game's files" link; Backups section follows the bar (`GamesForms.cpp:254-264`). |
| F7 | Done | Genre/Players/Rating as choices from the system's values, "Other..." after None (`GamesForms.cpp:92-97`, `GameProvider.cpp:1731-1745`). |
| F8 | Not done | `returnFromLaunch` rebuilds Manage > Games(collection) > game (`ManagerUI.cpp:1253-1270`); the search text and any other screens are not restored. |
| G1 | Done | One Launcher link with its status, Games as a linked results row, Delete under Actions (`SystemForms.cpp:21-40`). |
| G2 | Done | Problems (counted) and Notes sections (`SystemForms.cpp:171-197`, `SystemProvider.cpp:907-911`). |
| G3 | Done | Add a launcher first; "Used but not defined" inline up to `maxInlineUnresolved`, else a link with the count (`EmulatorsForms.cpp:18-28`, `EmulatorProvider.cpp:608-614`). |
| G4 | Done | `menu_supported.txt` said on the details screen only (`MenuProvider.cpp:836`). |
| G5 | Done | Settings says Theme (`BuiltinForms.cpp:50`); `theme:preview` picture from preview.png / splash / background (`ThemeProvider.cpp:165`, `ThemesForms.cpp:34`). |
| G6 | Done | Hint strips in both editors (`ThemeEditorScreen.cpp:1219`, `LayoutEditorScreen.cpp:1271`). |
| H1 | Done | After Add source, focus goes to **Open the Catalog** once it can open (`ManagerModel.cpp:990`; a button rather than a link); drafts cleared. |
| H2 | Done | Sources as rows opening `source` (type, address, status, problems, packages, Check / Remove in the bar). |
| H3 | Done | "Show it in Browse" on the package screen, reload + browse (`BuiltinForms.cpp:152`, `ManagerModel.cpp:950-954`). |
| I1 | Done | Controls link first in Settings; Save pinned (`BuiltinForms.cpp:40,58-60`). |
| I2 | Done | Screen size choice, Width/Height only for Custom (`BuiltinForms.cpp:43-45`). |
| I3 | Done | "(unmapped)" in lists (`ControlsProvider.cpp:98-113`). |
| J1 | Done | Cursor keys, Home, End on one-line keyboards; `TextEdit` cursor (`KeyboardKeys.h`, `Form.h:478-491`). |
| J2 | Done | Path row `. / \ : - _`; last key remembered per kind, typing keys only (`KeyboardKeys.h`, M23). |

Tally: 33 Done (one "as decided"), 7 Partly (A2, C3, E2, E3, F1, F4), 2 Not done (B5, F8).

## 4. Needs a real Windows check

What this pass couldn't settle by reading, in the order worth doing:

1. **Hint row width** (finding 5): on the built-in page at 1280x720, Aeon Nox
   16:9 at 1920x1080 and 4:3 at 1024x768, focus a control in Settings >
   Controls, a folder file in a game's media slot and a backup in Saves, and
   read what's left of the hint. Measure, then order the pieces.
2. **Title detail vs long titles** (finding 13): open a game with a long title
   after saving metadata, at 16:9, 4:3 and on the built-in page.
3. **Settings Save** (finding 4): change Screen size, Save; does the window
   come back at the new size after the reload, and what does the status say
   when Settings reopens?
4. **Focus after Hide** (finding 3): Hide a game, read the hint ("Move, then
   Select: Show this game again"), press Select once without moving.
5. **Letter jump with a pad on a bare install** (finding 1): on `firstrun`
   with no controls.conf, in Games with >26 titles, press every pad button on
   a result row; nothing should jump; then confirm Left/Right jump in the
   browser.
6. **The browser's places on a real cabinet**: a sleeping USB drive and an
   empty card reader (M23 says labels arrive later and the empty slot drops
   out); a network share; counts on a 10,000+ ROM folder; thumbnails in a
   folder of 500 PNGs (two per frame: how long until the visible ones show).
7. **The two-pane look** (M22, "the look is yours to judge"): the panel's
   picture heights (300 / 240), the default icons, long help text at 4:3, and
   the welcome note on first run.
8. **Keyboard over the browser**: Search in a folder with >200 entries opens
   the on-screen keyboard over the list at 1080p; check the list behind stays
   readable enough, and that letter up/down (where mapped) move the cursor.
9. **"Apply changes now"**: with a Catalog install running the button should
   be dimmed with its reason in the panel/hints; after it finishes, Apply,
   and confirm Manage reopens on the hub with "Applied N changes." and the
   indicator gone.
10. **Shoulder buttons on an unmapped joystick** (a Zero Delay encoder):
    `joyLeftShoulder`/`joyRightShoulder` only bind on pads SDL has a mapping
    for, so on arcade encoders Page Down has no button at all — the bar is
    then reached only by walking past the last row (Down), which still works
    but loses the headline saving; decide whether `joyUnmappedButton4/5`
    should map to pageUp/pageDown in `Builtin::controls`.
11. **The ImGui renderer** (`formsRenderer = imgui`): the pending line at the
    top, the browser as an ImGui form, and the file field's picker there were
    not read in this pass.

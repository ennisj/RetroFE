# UX review: Home, Catalog, Settings and Manage (round after 705514dd)

A design review of the management experience as it stands, to steer the next
rounds. Nothing in the source was changed for it.

**How it was done.** Built `feature/management-ui` at 705514dd (tests: 6718/6718
pass). Every screen was walked with the controller path only (mapped Up, Down,
Left, Right, Select, Back posted to RetroFE's window; text typed through the
on-screen keyboard or, for long paths, posted characters, which stands in for
the keyboard a cabinet owner may not have). No mouse, no native pickers.

| Root | What | Window |
|------|------|--------|
| `E:\Storage\Agents\ux\a` | Fresh: `retrofe\` (exe + DLLs) and `layouts\Aeon Nox` only. A fixture folder of 5 "ROMs" and a fake emulator (`fixture\emu\fake-emu.bat`), a local Catalog index. | Bare defaults: 1280 x 720 |
| `E:\Storage\Agents\ux\b` | Copy of `qa4\a` (OnesaUCE-shaped data, ~130 collections, QA fixtures incl. a text-file `retroarch.exe`) | 1920 x 1080, then 1024 x 768 with `randomLayout = layout 4x3` |

Screenshots: `E:\Storage\Agents\ux\screenshots\` (`a*` = fresh root, `b*` =
OnesaUCE copy at 1080p, `c*` = 4:3). The names below are files in that folder.

---

## Status after round 12 (2026-10-28)

Parcels A–J were built in round 12 ([FORK-CHANGELOG.md](FORK-CHANGELOG.md),
choices in [DECISIONS.md](DECISIONS.md) M22–M23). The fifth QA pass,
[QA-ROUND12.md](QA-ROUND12.md), recounted section 2's tasks from the forms
as built (by reading the code, not on screen): presses fall on nine of the
ten tasks and no path has to be typed any more; adding the first system goes
from ~350–550 presses to about 70, starting a game from Manage from ~17 to 9,
fixing a title from ~22 to 11, remapping Select from ~26 to 16.

Of the 42 items in section 4: 33 done; partly done: A2 (no video duration),
C3 (intro notes stay rows on the built-in page), E2 (Settings wording, Menus'
Delete, docs), E3 (focus after Hide), F1 (letter jump not reachable on a bare
install's pad), F4 (no video preview); not done: B5 ("Delete this menu") and
F8 (Start this game doesn't restore the search). QA-ROUND12.md section 3 has
the item-by-item list, and its findings are what round 13 picks up.

## 1. Summary

The management screens are **functionally deep and honest**: almost every
screen says what it will change, where it writes, what it keeps, and why
something can't be done; destructive actions confirm inline; unsaved changes
warn once; backups exist everywhere. The forms are consistent in structure and
look coherent in Aeon Nox at 1080p and 4:3.

The experience is held back by four things, in order of impact:

1. **Paths can't be entered with a controller.** Every "point RetroFE at…"
   field (games folder, emulator, media file, story file, source folder) offers
   either the Windows file dialog (Select) or typing the whole path on the
   on-screen keyboard (Right). On a cabinet neither works: the dialog needs a
   mouse and takes focus from RetroFE; a 40-character path is 150-250 presses.
   Adding the first system — the one task a new owner must do — is therefore
   effectively impossible controller-only.
2. **Long forms with the action at the bottom.** Save sits below 9-13 rows
   (Metadata, Settings, Emulator Edit, RetroArch screens); at 720p and 4:3 it is
   below the fold. Every edit costs a long walk down and back.
3. **Text density and size.** Explanatory notes are long (3 lines on Media,
   2 on Description, Pages lists), small (~19 px at 1080p, ~12 px in the bare
   720p window) and grey; at cabinet distance they are unreadable, yet some
   carry the only warning on the screen.
4. **Feedback about *when* things take effect is scattered.** Each screen
   says "RetroFE shows it after it reloads, when you close Manage" (or "the
   next time RetroFE starts", or nothing), but there is no single place that
   shows pending changes or applies them, and the first-run welcome text keeps
   saying "Nothing is set up yet" after a system was added (a14).

Everything else is polish: focus landing on the undo/destructive button after
an action, duplicated rows/labels, inconsistent verbs, lists that hide below the
fold, the Catalog wheel repeating two items.

---

## 2. Press counts for common tasks

Controller presses from Home (focus where RetroFE leaves it), counting each
direction/Select/Back. "Text" is on-screen-keyboard entry, estimated at
~4-5 presses per character (move + select, with Shift for capitals and
symbols).

| # | Task | Navigation presses | Text entry | Notes |
|---|------|-------------------|------------|-------|
| 1 | **Add a system** (first run) | ~14 (Manage auto-opens; Down 2, Select, Select, then field to field, Add) | Name (~30) + games folder path (~150-250) + emulator path (~150-250) | Only feasible with a keyboard or mouse (native picker). a03-a11 |
| 2 | **Fix a game's title** | ~22 (Manage 3, Games 1, search field, ~5 Down to the result, Select, Select Metadata, Select Title, Done, **9 Down to Save**, Select) | search term (~25) + title edit (~20) | The search is typed; no letter-jump on results. b05-b11 |
| 3 | **Replace a game's video** | ~16 (find game, 3 Down Media, 6 Down Video, Right, …, Down, Select "Use this file") | file path (~150-250) | No browse of the art already on disk. b16-b20 |
| 4 | **Hide a game** | ~12 + find game (8 Down to Hide, Select, Select) | search term | Focus then lands on "Show this game again". b31-b33 |
| 5 | **Change a launcher's arguments** | ~25 (Manage 3, 4 Down Emulators, pick launcher, 2 Down Edit, 2 Down Custom arguments, Done, 5 Down Save) | edit at end of a ~60-char string; no cursor keys on single-line fields | b59-b67 |
| 6 | **Switch theme** | ~13 via Manage > Themes > theme > Use; ~15 via Settings > Layout + 7 Down to Save | — | Two places, two names ("Layout", "Theme"). b72-b74, b81 |
| 7 | **Add a source and install a package** | ~22 (Catalog opens Sources; 4 Down Name … Trust, Add; Back; Catalog; Left/Right; Select; Install; Back x2) | name (~40) + URL/path (~150-300) | After "Added", no link to the Catalog. a21-a31 |
| 8 | **Remap Select** | ~26 (Right to Settings, Select, **9 Down to Controls**, 4 Down Select, 3 Down Add, press, 3 Down Save) | — | Capture flow itself is clear. a34-a39 |
| 9 | **Start a game from Manage** | ~17 + find game (7 Down to Start, Select) | search term | Reload first if changes are pending: Home flashes (b41), then returns on the game. |
| 10 | **Back up saves** | ~10 + find game (6 Down Saves, Select, Select — focus starts on Back up now) | search term | Backups list is below the fold at 1080p (b24). |

The recurring costs: **finding a game** (typed search, then walking past 4
filter rows to the results), **reaching Save** (9-13 rows), and **paths**.

---

## 3. Findings by area

### 3.1 First run (empty root, 1280 x 720)

Works:
- RetroFE starts bare, opens Manage with a welcome note (a01); Systems > Add a
  system is a short, well-labelled form with live validation ("5 matching
  files", "Give the system a name", "Choose the emulator program") (a03, a09).
- After Add system, the status line is excellent: "Added Arcade to Main. It
  starts its games with fake-emu.bat. RetroFE shows it after it reloads, when
  you close Manage." (a11). Closing Manage reloads; Browse shows Arcade; the game
  launched through the fake emulator (`launched.txt`), a18-a19. **From nothing to
  a playable system works.**

Problems:
- **Paths** (see 1.1). The fields default to "Not chosen yet"; the hint bar says
  "Select: browse  Right: type". Select opens a Windows dialog.
- The welcome note says "Open Systems" but **focus starts on Games**, which is
  empty and useless on a bare install (a01).
- The welcome note **doesn't update** after a system is added — still "Nothing
  is set up yet" (a14) until RetroFE reloads.
- After the reload, Home lands on **Catalog**, not Browse (where the new system
  is) or Manage (where the owner was) (a15, a31).
- Browse's Main menu with one entry draws "ARCADE" five times across the wheel
  (a17): Aeon Nox's `_menu` page loops a 1-item list.
- The bare window is 1280 x 720, so forms are drawn at ~2/3 size: notes are
  ~12 px (a03). A new owner's first impression is small grey text.
- Nothing suggests installing an emulator, or explains what an emulator is;
  "Launcher: A new emulator program…" vs "Emulator" vs "Arguments" is three
  concepts on the first form.

### 3.2 Home

Works: Aeon Nox Home is the most polished screen — big icon, title, tagline,
description panel, clear hints (b02, a20).

Problems:
- Order is Manage, Browse, Catalog, Settings (wheel); the spec order was
  Catalog / Settings / Manage / Browse. Selection after a reload resets to
  Catalog (a15, a31, b41), so returning users always start on Catalog.
- After "Start this game" with pending changes Home flashes during the reload
  (b41) — looks like the launch failed for a moment.

### 3.3 Catalog (Sources, packages)

Works: sources form explains itself (a21); trust toggle is required; "Added
Local-packs." (a25); package screen shows version, size, kind, source,
description, and the Catalog info panel below (a29, a30); install is quick and
the status is clear.

Problems:
- **Address/Folder needs typing** (1.1). No way to pick an index file on a USB
  stick with a controller.
- After **Add source**, focus stays on Add source with the stale hint "Give the
  source a name." under it (a25); there is no "Open the Catalog" link. The
  owner must Back to Home and pick Catalog again.
- Sources are one read-only text line ("1: Local-packs: E:\…\index.json"); you
  can't open a source to see its packages, last check, errors, or refresh it.
  Removal is a separate choice + button section at the bottom.
- The Catalog wheel with two items repeats "Sources / QA Pack" across the
  screen (a28).
- After **Install**, focus lands on **Remove** (a30). One more Select (from
  someone mashing Select while waiting) arms removal.
- Status repeats: "Installed." and "Installed QA Pack 1.0.0." (a30).
- No "Play it" / "Show in Browse" after installing a collection.

### 3.4 Settings and Controls

Works: Settings is short and grouped (Display, Menus, Video) (a32);
"Display changes take effect the next time RetroFE starts" is stated. Controls
is a strong screen: every control with its inputs in plain words, "Add an input
(press it now)" with a 5-second capture, "Added Keyboard S. Save to keep it."
(a37), "Select (changed)" on the list, and the unsaved-changes warning (a39).

Problems:
- **Save is the last row**, below the fold at 720p and 4:3 (a33, b87); 10 Downs
  from the top.
- **Controls is the last link**, after 9 rows of display settings (a32). The
  most important setting on a cabinet is the hardest to reach.
- "Layout" here, "Themes" in Manage, "theme" in the Themes screens.
- Width/Height are free text on the OSK; no resolution choice, no "use the
  screen's size".
- Controls labels are long (`Joystick button 0 (joysticks with no controller
  mapping)`), so the right-aligned input lists on the Controls list run the
  full width of the panel (a34).

### 3.5 Manage hub

Works: one flat list, the six sections then Catalog > Installed / Sources
(b03). Clear and fast.

Problems:
- The hub shows **no status**: no counts, no "3 systems need attention", no
  "changes waiting to apply". Every link's status column is empty.
- No visual identity: Manage is a plain text dialog in a theme whose Home is
  all icons and art (b02 vs b03).
- "Remove a package" is a hub-level form section (choice + button) while every
  other remove lives on the item's own screen.
- No "Apply changes now" (reload) — the only way is to close Manage.

### 3.6 Games: search, details and drill-downs

Works: search across ~6,500 games is instant, with thumbnails and
"System - file" on the right (b04, b38); "Showing 100 of 6,478. Refine the
search" (c02); the details screen has art, facts and a link per area with a
useful status ("Won't start", "5 of 19", "7 files, 3 backups",
"Changed here") (b36, b07, b31). Metadata says exactly where it writes
(`manage/metadata/QA Genesis.xml`) and "Not saved yet." under the changed
field (b10, b11). Saves (b24-b29), Hide (b31-b33), and Start this game (b41-b42,
"Back from alpha.", play count updated) all work and explain themselves.
RetroArch screens are clear: "Default (Off)" values, "Kept in …\Sonic.rmp (not
made yet)" (b13-b15). Media replace works and says "The video is now
sonic-video.mp4." with the slot now "Own file" (b19, b20).

Problems:
- **Finding a game** costs a typed search; results start below four filter
  rows and two notes, so 5+ Downs to the first result. No jump by letter, no
  "recently played/changed".
- **Save at the bottom** of Metadata (9 Downs), RetroArch, Core options.
- **Focus after actions**: after Hide, focus is on "Show this game again" (b32);
  after Start, focus resets to Metadata (b42).
- A **hidden game's screen drops every link** (b32): you can't fix its title or
  art without unhiding it first.
- After Start this game, the navigation stack is rebuilt: Back goes to Games
  filtered "In: QA Arcade" with the original search gone (b43).
- **Launcher "Won't start"** (b37) is a dead end: two notes say the same thing
  ("There's no launcher called …" / "Not installed: …; pick an installed
  launcher") and offer no "Add a launcher called lr-stella2014" link, though
  Emulators has exactly that action.
- **Media list** (b16): 19 rows all reading "Missing" in the same muted colour,
  no thumbnails, a 3-line intro note, slot names like "Video (player)",
  "Medium (back)". The slot screen (b18) shows the path RetroFE would use
  (`…\video\Sonic.*`) but **no preview** of the video, and replacing needs a
  typed path.
- **Playlists and collections** (b22): "Favourite: No" plus a separate "Add to
  favourites" button (a toggle would do); "Add to this playlist" is offered with
  "There are no playlists to choose from." and no way to create one here
  (that's under Systems > Playlists).
- **Description** (b23): a huge focused empty box; "Replace from a file" again
  needs a path.
- **Saves**: at 1080p the Backups header sits on the last line with nothing
  under it (b24) — it looks like there are no backups; they appear only when
  you scroll (c09, c10).
- Metadata fields that have fixed sets (Genre, Players, Rating) are free text,
  against the "choices over free text" rule.

### 3.7 Menus

Works: indented tree from Browse down with kind and counts (b45); details with
the logo, "Listed in", "Drawn with Aeon Nox's menu page" (b46); Entries with
order numbers and per-entry screens (b47); Delete explains what happens to the
entries and where files go (b49).

Problems:
- Tree rows are long ("menu + system, 11 entries, 0 games; Browse").
- Actions screen has a "Delete" section header and a "Delete" button (b49).
- Notes about `menu_supported.txt (RetroFE Plus)` appear on every menu screen
  (b46, b47) — internal detail for most owners.
- First row is "Change what Browse opens", which jumps to Settings (b45) — a
  cross-section jump without saying so.

### 3.8 Systems and Videos

Works: Systems list with game count and launcher (b50); details with logo and
links with statuses (b51); Check this system lists findings in sentences (b54,
a12); Videos list says how each plays (b55); video details add Plays/Playback
(b56).

Problems:
- Details show **"Launcher" twice** — a value row ("lr-stella2014 (set in
  settings.conf), which isn't installed") and a link row ("lr-stella2014 >")
  (b51). The value row carries the warning; the link doesn't.
- **"Games 622 games"** is a link without the ">" marker (b51, a11, b56), so it
  reads as a value.
- Check counts **informational notes as problems**: "5 things to look at" of
  which 3 say "this fork honours it" (b54).
- Systems list shows "0 games" for ~all OnesaUCE systems without any warning
  colour; broken launchers aren't flagged in the list.
- "Delete this system" / "Delete this video collection" sits in the link list
  with no visual separation (a11, b56).

### 3.9 Emulators (Edit, Rename)

Works: list with scope, program and usage (b59); details state where the
program was found and the working folder (b60); Edit shows who it reaches, the
variables, the exact command ("Runs …") and the file (b62); unsaved warning
(b63); Rename's "What changes" preview is a model screen (b70).

Problems:
- "Add a launcher" sits between the defined list and "Used but not defined"
  (b59), unlike Systems/Videos/Themes where Add is the first row.
- **110 undefined launchers** are listed inline on OnesaUCE data (b59) — the
  real launchers are 4 rows; the rest is noise.
- Arguments editing appends only (no cursor movement on single-line fields), so
  changing the middle of a 60-character argument string means retyping it.
- Edit's header says changes apply "the next time RetroFE starts"; other
  screens say "after it reloads, when you close Manage".
- Edit/Rename/Duplicate/Delete: "Edit" is a link with a status ("2 systems, 1
  game") that repeats "Used by"'s status (b60).

### 3.10 Themes and the editors

Works: list with "In use" and "Editor" tags (b72); hand-made theme details
explain the layout editor and backups (b74); Use this theme says when it
applies; both editors open, show the page, and close with Back (b75, b76, b78).

Problems:
- The editors are a **different visual language** (ImGui grey/blue, 20-ish px
  text, mouse-first hints: "Drag to move", "Ctrl+Z") (b75, b78). Expected per
  M11, but the jump from Aeon Nox forms is abrupt and the controller hints are
  one sentence in a side panel.
- "Use this theme" disappears once used; nothing on the theme screen shows a
  preview/screenshot of the theme.
- Theme vs Layout naming (see 3.4).

### 3.11 On-screen keyboard

Works: big keys, clear focus, Shift for capitals/symbols, a field label and
preview box, hints (a04).

Problems:
- No path helpers (drive letters, `\`, "..", folder names), no word list.
- No cursor keys on single-line fields (multi-line has them); Del deletes from
  the end only.
- Starts on "q" every time; no memory of the last key.

### 3.12 Confirmations, unsaved changes, feedback

Works: inline two-press confirmation with a tinted row and a red question,
"Anything else: cancel" in the hint bar (b26, b31); unsaved-change warnings in
red on Controls and Emulator Edit (a39, b63); success lines in green at the
bottom (a11, b19, b25, b32).

Problems:
- Verbs differ for the same idea: **Reset** (Metadata, Emulator Edit), **Undo
  changes** (Controls, menu Info), **Put back the original** (Media, Emulator),
  **Put back the default** (Controls), **Reset to original** (RetroArch),
  **Put back the shipped title/year/…** (nine buttons on Metadata, one per
  changed field); "discard them" vs "drop them".
- "Where it writes" wording differs: "Kept in" (RetroArch), "File" (Emulator
  Edit), "Saving writes …" (Description), "Changes are kept in …" (Metadata),
  "Backups are kept in …" (Saves).
- Disabled-button reasons ("No changes to save.", "Give the source a name.")
  are permanent grey lines under buttons, adding a row each, and stay after a
  successful action (a25).
- Warnings that matter (e.g. "Won't start", "which isn't installed") use the
  same muted status colour as neutral facts.

### 3.13 4:3 spot check (1024 x 768, `layout 4x3`)

Forms use the 4:3 page correctly (c01-c10): wider panel, same structure, text
scaled down (~13 px notes). Everything fits; lists page with Left/Right. Same
legibility caveat, stronger.

---

## 4. Recommendations

Effort: S = under a day, M = 1-3 days, L = a round. Priority: H = blocks
controller-only use or the first-run path; M = frequent friction; L = polish.

### Parcel A — Controller file and folder browser (forms engine)
Files: `Forms/Form.h`, `Graphics/Component/FormWidget.*`, `Manager/ManagerUI.*`,
`Manager/FileDialog.*` (keep as secondary), reuse logic from
`Manager/Themes/app/FileChooser.cpp`; new form/page `forms/file-browser` with
Aeon Nox styling; docs `MANAGER.md`.

- **A1 (H, L).** Select on a `folder`/`file` field opens an **in-RetroFE
  browser screen** (a form pushed on the stack): drives and well-known folders
  first (RetroFE's folder, the current value's folder, removable drives,
  recent), then folders/files as result rows with counts ("212 files matching
  zip"), filtered by the field's `pattern`; Select enters a folder, a "Use this
  folder" row chooses it, Back goes up/cancels. The Windows dialog stays
  available as a row "Browse with Windows…" only when a mouse is present.
- **A2 (M, S).** Media/story fields: the browser starts in the slot's own media
  folder and shows image thumbnails / "video, 1:23" for the focused file.

### Parcel B — Form layout: sticky actions and less walking
Files: `Forms/Form.*`, `FormWidget.*`, `WidgetPainter.*`, Aeon Nox
`forms/layout*.xml`, every `*Forms.cpp` that has Save.

- **B1 (H, M).** **Sticky action bar**: an `<actions>` block renders pinned at
  the bottom of the panel (always visible), and a mapped button (e.g. the
  `menu`/Start control, shown in the hints) jumps focus to it from anywhere.
  Removes the 9-13 Downs to Save.
- **B2 (M, S).** After editing a field on the OSK (Done), offer focus on Save
  when the screen becomes dirty — or simply keep focus but show
  "Start: Save" in the hint bar.
- **B3 (M, S).** Disabled-button reasons appear only when the button is focused
  (or as a tooltip line in the hint bar), not as a permanent row.
- **B4 (M, M).** Two-pane option for Aeon Nox: the long notes/help of the
  focused row move to a side panel (bind the focused field's `help`), so lists
  stay compact. Layout-driven: `<form helpPanel="…">` or a `<formHelp>`
  component. This is also the aesthetic win (see Decisions).
- **B5 (L, S).** Section header + same-named button (menu Actions "Delete"):
  rename the button "Delete this menu".

### Parcel C — Legibility and visual hierarchy (Aeon Nox + painter)
Files: `layouts/Aeon Nox/forms/layout.xml`, `layout 4x3.xml`,
`keyboard/layout*.xml`, `WidgetPainter.*`, `FormWidget.*` (status colours).

- **C1 (H, S).** Raise Aeon Nox form sizes: notes ≥ 26 px at 1080p (now ~19),
  `noteAlpha` ≥ 0.75; row text ~34 px. Check 4:3 page too.
- **C2 (H, S).** Status **severity colours**: link/value statuses can be
  `warning`/`error`/`ok` (e.g. "Won't start", "isn't installed", "Missing" when
  nothing shows) drawn with `errorColor`/a new `warningColor`; add the
  attribute to the forms reference.
- **C3 (M, S).** Trim intro notes to one line; move detail into help (B4) —
  Media (b16), Description, Menus `menu_supported.txt`, Themes.
- **C4 (L, S).** Bare 1280 x 720 default: make the built-in form page scale
  text up when the window is small, or default the bare window to the
  desktop size (windowed fullscreen) so first-run text isn't 12 px.

### Parcel D — First run and Home
Files: `Forms/BuiltinForms.cpp` (manage form), `Manager/ManagerModel.cpp`
(`info:hasCollections`), `RetroFE.cpp`/`Manager/Home.h` (Home order and
selection after reload), Aeon Nox `collections/_menu`.

- **D1 (H, S).** First run: focus the welcome note's target — put **Systems
  first** (or a "Add your first system >" link at the top of the hub when
  `!info:hasCollections`), and make `info:hasCollections` count systems created
  in this session so the welcome note disappears (a14).
- **D2 (M, S).** After the reload that follows Manage, put Home's selection on
  **Browse** when the change added content, else back on Manage; never reset to
  Catalog (a15, a31).
- **D3 (M, S).** Home order Catalog, Settings, Manage, Browse as specified, or
  record the decision to keep Browse centred.
- **D4 (L, S).** Aeon Nox `_menu`: don't repeat items when the list is shorter
  than the wheel (a17, Catalog a28) — check RetroFE's menu loop/`minimum` or
  give the page a non-looping list.

### Parcel E — Pending changes and feedback vocabulary
Files: `Manager/ManagerUI.*`, `ManagerModel.*`, `Forms/BuiltinForms.cpp`,
all `*Provider.cpp` status strings, `docs/MANAGER.md`.

- **E1 (H, M).** A **"pending changes" indicator** in every Manage screen's
  title area ("2 changes apply when you close Manage") and an **"Apply changes
  now"** row on the hub that triggers the reload and comes back to the hub.
- **E2 (M, S).** One vocabulary, applied in every form: **Save**; **Undo
  changes** (drop unsaved edits); **Put back the original** (restore the
  backup); **Put back the default** only for built-in defaults. "Where it
  writes" always a value row labelled **Saved in** (path), never a note.
  "…when you close Manage" everywhere (fix Emulator Edit's "next time RetroFE
  starts" unless it's really a restart).
- **E3 (M, S).** Don't land focus on an undo/destructive button after an action:
  after Install focus "Back to the Catalog"/"Play" (a30); after Hide focus the
  first link, not "Show this game again" (b32); after Start return focus to
  "Start this game" (b42). Dedupe "Installed." / "Installed QA Pack 1.0.0.".

### Parcel F — Games section
Files: `Manager/Games/GamesForms.cpp`, `GameProvider.*`, `SavesProvider.*`,
`RetroArchForms.cpp`.

- **F1 (M, S).** Games search: put results directly under Search and move the
  three filters into one "Filters: All games >" link (or below the results);
  letter-jump (`letterUp/Down` controls) in results.
- **F2 (M, S).** "Won't start" Launcher screen: one warning line and a link
  "Add a launcher called lr-stella2014 >" (opens `emulator-new` with the name,
  as Emulators' unresolved rows already do) (b37).
- **F3 (M, M).** Hidden games keep their links (Metadata, Media…) with a banner
  "Hidden from RetroFE" and the Show again button (b32).
- **F4 (M, S).** Media list: status words with severity (C2), group "Shown" vs
  "Missing", thumbnail column; slot screen shows a preview (image, or the
  video's first frame/duration) (b16-b20).
- **F5 (M, S).** Playlists: Favourite as a toggle; "New playlist…" row on the
  game's screen when there are none (b22).
- **F6 (L, S).** Saves: put the Backups list before the long file list, or
  collapse "This game's files" into one row "7 files >" (b24).
- **F7 (L, S).** Metadata: Genre/Players/Rating as choices from the values in
  the system's metadata (with "Other…").
- **F8 (L, M).** After Start this game, restore the exact navigation stack
  (search text included) rather than rebuilding it (b43).

### Parcel G — Systems, Menus, Emulators, Themes clean-up
Files: `Manager/Systems/SystemForms.cpp`, `SystemProvider.*`,
`Manager/Menus/MenusForms.cpp`, `Manager/Emulators/EmulatorsForms.cpp`,
`EmulatorProvider.*`, `Manager/Themes/ThemesForms.cpp`.

- **G1 (M, S).** System details: one Launcher row (the link, carrying the
  warning status); ">" marker on the Games link; Delete in its own "Actions"
  section (b51, b56).
- **G2 (M, S).** Check: separate **Problems** (counted) from **Notes**
  (OnesaUCE keys honoured, not counted) (b54).
- **G3 (M, S).** Emulators: "Add a launcher" as the first row; collapse
  "Used but not defined" into a link with its count when > 10 (b59).
- **G4 (L, S).** Menus: drop the `menu_supported.txt` note from Entries (keep it
  on details only); shorten tree details.
- **G5 (L, S).** Themes: rename Settings "Layout" to "Theme"; show a preview
  image (the theme's `preview.png`/splash, if any) on the theme screen.
- **G6 (L, M).** Editors: a controller hint strip at the bottom of both editors
  ("Select: pick/move/resize · hold Select + direction: add · Back: pick")
  styled like the forms' hints.

### Parcel H — Catalog
Files: `Forms/BuiltinForms.cpp` (sources/package forms), `ManagerModel.*`,
`Catalog/Service.*`, Aeon Nox `forms/package`, `collections/Catalog`.

- **H1 (M, S).** After Add source: "Open the Catalog >" link + clear the stale
  hint (a25).
- **H2 (M, M).** Sources as result rows: each opens a source screen (packages
  offered, last check, problems, Refresh, Remove) — replaces the bottom
  choice+button Remove section.
- **H3 (L, S).** After installing a collection: "Show it in Browse" (after
  reload).

### Parcel I — Settings and Controls
Files: `Forms/BuiltinForms.cpp` (settings form), `Manager/Controls/ControlsForms.cpp`.

- **I1 (M, S).** Controls first in Settings (a link at the top), Save pinned
  (B1).
- **I2 (L, S).** Resolution as a choice ("Screen size", 1920 x 1080, 1280 x 720,
  Custom…) instead of two OSK fields.
- **I3 (L, S).** Shorter input names in lists ("Joystick button 0
  (unmapped pads)").

### Parcel J — On-screen keyboard
Files: `Graphics/Component/KeyboardWidget.*`, `Forms/KeyboardKeys.h`, Aeon Nox
`forms/keyboard/layout*.xml`.

- **J1 (M, S).** Cursor keys (← →) and Home/End on single-line fields, so an
  argument can be edited in the middle.
- **J2 (L, S).** Remember the last key; a ".com / \\ / :" symbol row for paths
  and URLs.

---

## 5. Top 10

1. **A1** In-RetroFE controller file/folder browser for every path field (H, L).
2. **B1** Sticky action bar with a mapped "Save" jump (H, M).
3. **C1 + C2** Bigger, brighter text in Aeon Nox forms and severity colours for
   statuses (H, S).
4. **D1** First run: focus/link to Systems, welcome note updates after adding a
   system (H, S).
5. **E1** Pending-changes indicator and "Apply changes now" on the hub (H, M).
6. **E3** Never land focus on undo/destructive buttons after an action (M, S).
7. **F2** "Won't start" → "Add a launcher called …" link (M, S).
8. **F1** Games search: results first, filters collapsed, letter-jump (M, S).
9. **E2** One vocabulary for Save / Undo changes / Put back the original /
   Saved in (M, S).
10. **D2** Home returns to Browse/Manage after a reload, not Catalog (M, S).

---

## 6. Needs your decision

1. **Native pickers vs an in-RetroFE browser (A1).** Options: (a) keep the
   Windows dialog and typing; (b) in-RetroFE controller browser as the default,
   Windows dialog as a secondary row when a mouse is present; (c) browser only.
   **Recommended: (b).** Cabinets can't use (a); (c) loses a convenience on
   desktops.
2. **Save model on long forms (B1).** Options: (a) keep Save at the bottom
   (M5); (b) Save per screen but pinned, plus a mapped jump button; (c) save on
   leaving the screen (with the existing warn-once turned into "Save changes?"
   Yes/No); (d) live writes like One Saucier. **Recommended: (b)**; it keeps M5
   and its backups/reset model and removes the walking.
3. **Applying changes (E1).** Options: (a) only when closing Manage (today);
   (b) also an "Apply changes now" row on the hub; (c) reload after every
   save. **Recommended: (b).**
4. **Aesthetics of Manage (B4/C3).** Options: (a) single list panel (today);
   (b) two-pane Aeon Nox page: list on the left, art/help/preview of the
   focused row on the right, layout-driven; (c) tiles like One Saucier.
   **Recommended: (b)** — keeps M2's links, gives each screen the art and room
   Home already has, and moves help text out of the rows.
5. **Home order and landing (D2/D3).** Options: (a) keep Manage-Browse-Catalog-
   Settings wheel with Catalog as the post-reload default; (b) spec order
   Catalog-Settings-Manage-Browse; (c) keep the order but land on Browse when
   content exists, Manage on first run. **Recommended: (c).**
6. **Theme vs Layout wording (G5).** Options: "Theme" everywhere in the UI
   (keeping `layout` as the setting key) or "Layout" everywhere.
   **Recommended: "Theme".**

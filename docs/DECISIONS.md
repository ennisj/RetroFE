# Decisions and open items

Decisions made while building the fork, mostly without waiting for review.
Each one lists the options considered, what was chosen and why, so any of
them can be revisited. **Open** items still need your input; everything else
is decided but can be changed. Newest rounds are added at the end of each
section.

Status: **Decided** (chosen, can be revisited), **Open** (needs your input),
**Deferred** (agreed to leave for later).

## Needs your input

| Item | Question |
|------|----------|
| [M22](#m22-design-review-choices-round-12--decided-the-look-is-yours-to-judge) | Round 12 has landed: does the two-pane Manage look in Aeon Nox (list left, help/art right) work for you, at 16:9 and 4:3, and do the other design-review choices feel right in use? VERIFY.md "Manage look (round 12)" lists what to look at. |

Behaviour that still needs a check on a real setup is tracked in
[VERIFY.md](VERIFY.md).

---

## Layout and look

### L1. Where Aeon Nox comes from — Decided
- Options: the copy in OnesaUCE/One Saucier installs; RetroFE's original
  repository (phulshof/RetroFE); the FunKey-Project fork.
- Chosen: **phulshof/RetroFE**, folder `Package/Environment/Common/layouts/Aeon Nox`,
  commit `9f9230d09cbd4dae074b9465ca090567fd0623e7` (2023-04-15).
- Why: you asked for the official version. It's the upstream RetroFE copy,
  licensed with RetroFE (GPL-3.0), with the author's credits intact.

### L2. Aeon Nox is the default layout, shipped with the build — Decided
- Options: (a) keep the built-in layout as the default and offer Aeon Nox
  through the Catalog; (b) ship Aeon Nox in `layouts/` with the build and make
  it the default (`layout = Aeon Nox` in the built-in settings).
- Chosen: **(b)**. Without a `layouts/Aeon Nox` folder RetroFE still falls
  back to the built-in layout, so a truly bare exe keeps working.
- Why: you asked for Aeon Nox as the default. Shipping it beside the exe is
  part of the build, not a downloaded CoinOPS build, so the "runs bare" goal
  still holds.
- Consequence: the test install `E:\Storage\OneSauce` now gets
  `layouts\Aeon Nox` copied alongside `retrofe\` when builds are deployed. It
  stays otherwise bare (no settings, collections or catalog).

### L3. The Arcades layout stays in the repository — Decided
- It's upstream CoinOPS's default and harmless to keep; removing it would make
  merging upstream harder. It's no longer the default. Its Scout fonts and
  manufacturer logos have unclear redistribution terms, so if the fork ever
  publishes its own packages it may be worth dropping it from them (see L4).

### L4. Fonts and artwork with unclear licences — Partly resolved, rest Deferred
- **Your answer (2026-10-24): not for now.** The panel images and intro stay
  as they are; revisit before publishing release packages.
- **Fixed in round 1:** Aeon Nox's `MC360.ttf` turned out to be Ubuntu
  Condensed shipped without its licence (removed; Roboto Condensed used
  instead). Its `BebasNeue.ttf` 1.002 had a no-redistribution EULA (replaced
  with the OFL release), and its `OpenSans.ttf` had no licence data (replaced
  with Open Sans Condensed built from the OFL font; Open Sans has no Reserved
  Font Name). Every font now has its licence beside it, copied verbatim from
  google/fonts. The Home icons and logos were drawn for the fork by a script
  (`Scripts/aeon-nox/`).
- **Open:** (a) Aeon Nox's panel images (wall, floor, info bars) are credited
  to Kodi's Aeon Nox skin, which is CC BY-SA 3.0 (to Jan 2014) and
  CC BY-NC-SA 4.0 since — if upstream copied them from a newer skin, they're
  NonCommercial. (b) `video/intro.mp4` (a Street Fighter II-style "RETRO FE"
  logo, Earth footage and music) and the theme's sounds have no recorded
  origin; upstream RetroFE ships them. Both are fine for personal use;
  before publishing release packages, consider redrawing the panels and
  replacing the intro with a static splash. NOTICE.txt in the layout records
  all of this.
- Arcades (upstream's default) still has Scout fonts and manufacturer logos
  of unclear terms; recommendation unchanged: leave it out of release packages.

### L5. Menu pages: a theme-wide `_menu` page — Decided
- Stock RetroFE picks a page per collection by name, so a theme could only
  style the top menu as "Main"; other menus got the games page.
- Chosen: a small engine rule. A collection whose items are all collections
  and has no page of its own uses `layouts/<L>/collections/_menu/layout/`.
  Collection-specific pages and `layoutFromAnotherCollection` still win.
  Aeon Nox's systems strip lives there.

### L6. Form pages follow the layout variant — Decided
- Management screens now use the same page variant as collections
  (`randomLayout`, e.g. `layout 4x3`), so a 4:3 setup gets 4:3 forms instead
  of squashed 16:9 ones. Aeon Nox ships 4:3 versions of all its pages.

### L7. The packaged `settings.conf` uses Aeon Nox — Decided (was Open)
- `Package/Environment/Common/settings.conf` (the upstream-style package)
  kept `layout=Arcades`, because its bundled collections were made for it.
- **Your answer (2026-10-24): switch.** It now says `layout=Aeon Nox`, like
  installs RetroFE creates itself. Arcades stays in the repository (L3).

---

## Manage screens

### M1. "Collections" in the request is named "Menus" in the UI — Decided
- Your list said *Collections*; your description called the same thing
  *Menus* (the top menu, sub-collections and menus of menus).
- Options: "Collections", "Menus", "Menus & Collections".
- Chosen: **Menus**. In RetroFE every list is a "collection", including
  systems, so "Collections" would blur Menus and Systems.

### M2. Detail screens drill down with links instead of tiles — Decided
- One Saucier shows a details screen as scrollable tiles. Here a details
  screen is a form; each tile becomes a **link** row (with a short status on
  the right, e.g. "Media — 4 of 7") that opens its own screen. Back returns.
- Why: your suggestion; it works with any controller and any layout, and each
  sub-screen can be styled separately.

### M3. One navigation stack for all screens — Decided
- Screens open on top of each other (Manage → Games → a game → Media) and
  Back returns to the previous one with its selection kept. F10 still opens
  Settings from anywhere.

### M4. Separate screens per entity, not One Saucier's single search — Decided
- One Saucier's Manage is one search with a strip of kinds (Games, Systems,
  Collections, …) and checkable tables with batch actions.
- Options: copy that; or one hub with a screen per entity.
- Chosen: **a hub with a screen per entity**, as you listed them. Tables with
  checkboxes and batch actions suit a mouse, not a controller on a cabinet; a
  list of results per entity reads well in a layout. Batch actions are
  deferred.

### M5. Edits are written when you press Save, not live — Decided
- One Saucier writes every edit immediately and groups them into named,
  switchable "customizations".
- Options: live writes; Save per screen; customization overlays.
- Chosen: **Save per screen** (the forms already work this way for Settings),
  with a backup of each original before its first change and a Reset. The
  customization overlay system is One Saucier's own and is left out.

### M6. Metadata edits go to a per-user override layer — Decided
- RetroFE's `meta.db` is only a cache rebuilt from `meta/hyperlist/*.xml`,
  which content updates overwrite, and its import never deletes rows.
- Options: edit the shipped XML in place (as One Saucier does); write only
  `meta.db`; a user override file RetroFE imports after the shipped ones.
- Chosen: **an override layer** imported last, plus an immediate `meta.db`
  update. Edits survive content updates and are easy to reset (delete the
  override). The library agent picks the exact file format and records it.

### M7. Videos are systems with a video preset — Decided
- Stock RetroFE has no video collections, and One Saucier's Videos are
  online-catalog rows only.
- Chosen: a video collection is a system whose extensions are video types,
  whose launcher is a player, marked `type = videos` in its `info.conf`. The
  Videos screen lists those and reuses the Systems screens. In-page playback
  (jukebox mode) can come later.

### M8. Emulators means launchers, read-only first — Decided
- RetroFE has no emulator entity; a launcher is an executable plus arguments.
  One Saucier doesn't let users edit executable or arguments either.
- Chosen: the Emulators screen lists launchers (global, per collection and
  local), what uses them, and unresolved names. Adding a launcher (executable
  from a file picker, arguments from presets) comes after the read-only views.
- **Round 7: launchers are now edited in place** [keeping them read-only,
  which meant duplicating a launcher and repointing every system to change
  one argument]. Only the edited lines change; the screen says which systems
  and games the change reaches, and the original can be put back. Global
  launchers can be renamed with a preview: the file in every
  `launchers.<os>` folder and every `launcher =` reference (alternatives
  included) and per-game launcher file are rewritten, all or nothing.
  Collection and local launchers aren't renamed (a collection's launcher is
  named by its folder).

### M9. Writes go where stock RetroFE reads, even on OnesaUCE data — Decided
- OnesaUCE data uses keys stock RetroFE ignores (`media.basePath`,
  `menu_supported.txt`, comma launcher lists). Screens read them to show the
  right media and launchers, but only write stock files. On a OnesaUCE install,
  menu edits would be undone by its own frontend, which regenerates `menu.txt`;
  that install isn't this fork's target.

### M10. Editing hand-made themes — Decided (was Open)
- One Saucier's editor generates layouts from a widget-based project; it can't
  open hand-made layouts (it ships pre-converted examples made by a 2,183-line
  Python importer). Aeon Nox, the default, is hand-made.
- Options: (1) port the editor as-is: new themes, duplicates of editor-made
  themes; (2) also port the importer (2–3 rounds, lossy by design); (3) add a
  simpler "layout-native" mode that moves and resizes the elements of any
  existing layout file, keeping everything else.
- Chosen: **(1) and (3)**, done in rounds 2–4. Editor-made themes open One
  Saucier's widget editor; hand-made themes (Aeon Nox, anything else in
  `layouts/`) open a **layout editor** that moves, resizes and edits the
  attributes of existing elements in place, changing only the bytes of the
  values you edit (comments, formatting and line endings stay), with the
  original kept in `manage/backups/layouts/`. Adding new elements and
  editing animations there are later steps. (2), the importer, isn't
  planned. Status: **Decided**, unless you'd rather have (2).
- Layout editor details: moves write the exact delta (positions that aren't
  whole get up to 2 decimals), only the axes you drag are written (so
  `x="center"` survives a vertical drag), restoring the original is refused
  when the file changed elsewhere and can be undone, and pages written by
  the widget editor aren't opened there.

### M11. The theme editor is drawn with ImGui — Decided
- It's a canvas editor (drag handles, overlays, colour pickers, dropdowns),
  which doesn't fit row-based forms. The Themes list stays a form; Edit opens
  the editor as an ImGui screen inside the manager, previewing with RetroFE's
  own page builder. Driving it with a controller can come later.

---

### M12. Round 2 screen decisions — Decided
Each section's agent chose these; the alternatives are in brackets.
- **Games: story text is edited on one line**, with ` | ` between paragraphs
  [read-only plus replace-from-file; a multi-line field, which forms don't
  have yet]. A story that already contains `|` can only be replaced from a
  file, so nothing is rewritten by accident. A multi-line text field would
  remove this limit (later).
- **Games: favourites are per page.** RetroFE keeps favourites in the list of
  the page you were on, so a game shown in a merged collection has a
  favourite toggle for each page that shows it [only its own system's list,
  which merged pages never read].
- **Games: file lookups run in the background** (media slots, search
  thumbnails), so slow USB drives don't stall the screen.
- **Systems: delete only empty systems made here.** A system can be deleted
  when its folder holds only settings, art and playlists, its games folder
  isn't inside it, and no `menu.xml` lists it; the whole folder moves to
  `manage/backups/deleted/` [never delete; delete anything empty].
- **Systems: playlists RetroFE won't show** (because of `cyclePlaylist`) are
  pointed out, with a one-step "Show it in RetroFE".
- **Videos:** collections marked `type = videos` appear only under Videos;
  "This is a video collection" on a system's Info screen moves it. New video
  collections share one `VideoPlayer` launcher (VLC or mpv).
- **Menus: deleting a menu moves its entries up** into the menus that list it,
  in its place [refuse unless empty; delete the entries too]. Menus listed by
  a `menu.xml` or `collectionLaunchers` can't be changed here (writing a
  `menu.txt` would hide the XML).
- **Menus: every entry and source has its own screen** [buttons on the list
  with a "selected entry" choice], which works better with a controller.
- **Menus: a new menu gets `settings.conf`, `info.conf` and `menu.txt`**;
  without `settings.conf` RetroFE logs an error at start.
- **Emulators: existing launchers stay read-only**; "Duplicate and edit" opens
  the add form filled in [edit launchers made in Manage until used]. New
  launchers write paths inside RetroFE's folder as `%RETROFE_PATH%\…`, and
  names must be plain ASCII (RetroFE reads launcher file names in the ANSI
  code page) and can't shadow another launcher (RetroFE looks up local, then
  collection, then global launchers).

### M13. Theme editor screen choices — Decided
- **The editor is a screen in the normal navigation stack** (behind a small
  `ImGuiScreen` interface), so Back, F10 and unsaved-changes warnings work as
  elsewhere; leaving with unsaved changes always asks Save / Don't save /
  Cancel [the forms' "leave again to discard"].
- **Use this theme requires a `splash.xml`** for every theme: RetroFE can't
  start on a theme without one. The editor writes a simple splash page on
  every save unless the theme has its own.
- **Use is refused for an editor theme with no wheel**, and Delete for the
  theme in use.
- **Automated editor checks use a one-shot `-editorScript <file>` option**
  [an environment variable, which could linger and re-run saves]; a banner
  shows while a script runs. ImGui can't be driven by posted key messages.
- **Pages the engine would crash on are reported, never built** — see R3 for
  the engine fix.

### M14. Round 3 choices — Decided
- **Multi-line fields are `multiline="yes"` on `<text>`** [a new element],
  so they keep every text attribute; Enter adds a line, Ctrl+Enter keeps,
  Escape cancels, and the on-screen keyboard's Done keeps.
- **Start this game reloads first when changes are waiting** [refuse; launch
  on the old settings], is refused while a Catalog install runs, launches
  from the game's own system page, and reopens Manage on the game afterwards
  [return to the page].
- **Hidden games are found with a search toggle** [a list per system].
- **A bare install gets settings.conf when Manage closes with changes**
  [in each section; changing the start-up check], as the Catalog already did.
- **Back after creating something goes to the list** (the new screen replaces
  the add form) [back to the filled-in form].
- **Program lookup on Windows:** RetroFE's folder first, then the system path
  (as Windows' SearchPath does), shared by launching and Manage's checks.
- **Theme templates show the page this engine actually uses**, not One
  Saucier's inheritance down the menu tree [inheritance, which RetroFE never
  shows]; a whole menu branch can be put on a template in one step.
- **The theme editor's file chooser stays inside RetroFE's folder**, with the
  system dialog as a second option; fonts go to `fonts/`, the rest to
  `assets/`.
- **A `<menu>` without `<itemDefaults>` gets empty defaults with a warning**
  [skip the menu; null checks everywhere].

### M16. Round 5 choices — Decided
- **Videos play in RetroFE only with `play = here`** in info.conf [treating no
  line as "here", which would change existing collections]. Manage writes it
  for new video collections and offers "Plays: In RetroFE / With an external
  player" on the Playback screen.
- **The player is a page of its own** (`player.xml`, then `_videos`, then
  built-in) drawn in place of the list [pushing it onto RetroFE's page stack].
  Left/Right are previous/next, Up/Down seek by 5%.
- **A video collection previews its own files** when it has no video artwork
  (done in the engine, so hand-made collections get it too).
- **`collections/_videos/layout/`** styles every video collection without
  pages of its own, checked before `_menu`.
- **Layout editor palette writes RetroFE's own XML** [the widget library's
  fragments], placed after the selection and styled like its neighbours;
  files go next to the page that uses them, so paths work whatever theme is
  active.
- **Log levels:** menus and merged collections without settings log INFO; a
  system missing its settings or launcher still warns.

### M17. Round 6 choices — Decided
Theme editor with a controller:
- **Panels and dialogs use ImGui's own gamepad navigation**, fed RetroFE's
  mapped controls [One Saucier's per-row decisions, which would mean
  rewriting both editors' panels; ImGui keyboard navigation, which would make
  the real arrow keys move focus and nudge the canvas at once]. The SDL
  backend's own gamepad reading is off while an editor is open, so a mapped
  pad can't act twice.
- **Canvas modes: Select cycles Pick → Move → Resize**; Back returns to Pick;
  a run of nudges is one undo step. One Saucier's Edit mode was left out (the
  properties panel has the keyboard).
- **Bigger steps come from holding a direction** (grid, ×4, ×10) [a held
  modifier, which most cabinets have no spare control for]; Shift with a
  mapped arrow still gives ×10 on a keyboard.
- **The editors draw their own on-screen keyboard** with the same keys as the
  layout one [the layout's keyboard page, which would show the edited
  theme's style under the editor window]. Colours are typed as hex.
- **Mapped letter keys stay controls** (with the shipped controls.conf B is
  Back, M/N pick previous/next) — documented rather than filtered, since on a
  cabinet those are the keys the encoder sends.

RetroArch settings:
- **One provider and one set of screens for games and systems**, logic in
  `Library/RetroArch` [a provider per section].
- **Save/Reset like the other Manage screens** [One Saucier's
  write-on-change].
- **A game's file keeps only values that differ from what it overrides**;
  files left equal to what they shadow are deleted (backed up). A core-wide
  `.opt`/`.rmp` is deleted only when empty, since RetroArch writes full files.
- **Unknown cores fall back to the core's `.info` name, marked unconfirmed**
  until a folder RetroArch made confirms it [One Saucier's rule of offering
  nothing outside its hand-checked table]. Table rows that couldn't be
  confirmed (gw, fuse, blastem, bsnes, duckstation, kronos) were moved to the
  fallback, since a wrong row is shown as certain.
- **No RetroFE reload after a RetroArch write**: RetroArch reads these files
  each time it starts a game.
- **The RetroArch link is hidden for launchers outside RetroArch** and shown
  with a reason ("RetroArch not found", "Core not known") otherwise.
- Not checked against a real RetroArch install yet: the "Default (…)" values
  and "first line wins" come from reading RetroArch's source. Worth one check
  on a cabinet.

QA follow-ups:
- **Play counts stay upstream's rule** (counted only when
  `lastPlayedSkipCollection` is set) [counting always, which would start
  writing last-played files on existing installs]. Manage says "Not counted
  (…)" with the reason instead of "Never". A launch that fails to start no
  longer counts.
- **Launch working folder:** a launcher without `currentDirectory` runs in its
  program's folder, or for a bare name in the folder it was found in on the
  path — except Windows' own `cmd.exe`/`powershell.exe` (and system programs
  on Linux/macOS), which run in RetroFE's root so relative arguments keep
  working.
- **A negative `selectedOffset` counts back** (-1 is the previous item), a
  deliberate change from upstream, where -1 showed the next one. No shipped
  layout uses it.

### M18. Round 7 choices — Decided
Saves:
- **Only RetroArch games have a Saves screen** [a per-launcher saves-folder
  setting matched by file name]. MAME and most standalone emulators keep a
  folder per game or per system, so matching by name would be wrong for the
  common cases; others say "Not known for this emulator".
- **Backups are snapshots under `<root>/manage/saves/<collection>/<game>/<time>/`**
  [reusing `manage/backups`, which keeps one original per file]. Kept until
  deleted; restoring backs up the current files first.
- **Files match by the game's exact name plus a known save or state
  ending**, ignoring case on Windows and other case-insensitive drives;
  games sharing a name (and so the same save file) are pointed out.
- **Save folders follow RetroArch's source as read** (the reviewer read it
  differently): "in content folder" swaps the folder for the game's folder
  and sorting still adds sub-folders inside it; an empty or invalid folder
  setting keeps RetroArch's default saves folder. Worth one check on a real
  install, with the RetroArch settings in M17.
- **Unknown `%VARIABLES%` in a launcher are refused** unless the program is
  the command prompt or a batch file, which expands its own (`%APPDATA%`).

Theme editors:
- **A selection is a list whose last pick is the primary** (the panel and
  the controller start from it) [an unordered set]. Groups move, align and
  space out but don't resize [proportional resize, which writes sizes nobody
  asked for]. A marquee takes only what's wholly inside, so it doesn't grab
  the full-page background.
- **Controller: hold Select and press a direction to add**, one per press
  [a dedicated key, which cabinets rarely have spare]; Select now acts when
  it's let go. Letter up/down cycle the resize handle.
- **Copying to the 4:3 page:** positions and sizes move by the same amount
  scaled to the other page (from where the run started, so small nudges
  don't drift); other values are copied only where both pages agreed; added
  or deleted elements and the page size are never copied [copying values as
  written, wrong for different page sizes; a live preview of both pages,
  too costly]. Wheel items pair by their offset from the selected item.

OnesaUCE themes:
- **`layout.lua` is read, not run** [embedding Lua, which would also need
  the other fork's page API, unknowable without its source]. Only a
  `build()` that loads one XML page is understood; XML beside a lua file
  wins. The lua hooks are not supported.
- **A malformed `transform` is ignored with a warning**, never the page;
  corners that cross over are refused.
- **`%RETROFE_PATH%` is expanded only where a path is resolved**, so
  Manage still edits settings exactly as written.
- **Self-update keeps installing its bundled layouts to `<root>/layouts`**
  even with `baseLayoutPath` set: its recovery runs before settings are read
  and only trusts folders inside the program's own. OneSauce doesn't use the
  bundled layouts.
- Left alone until their meaning is clear: case variants (`Height`,
  `maxwidth`), `reloadablePanningImage`, `<view ref>`, `onJumpEnter`,
  `pauseTime` and others listed in ONESAUCE-COMPAT.md.

### M19. Round 8 choices — Decided
- **An input RetroFE doesn't know is skipped with a warning** [stopping, as
  upstream did]. A control set but left blank (`quit =`) stays unbound, as
  upstream; the built-in default applies only when a control isn't set or
  none of its inputs works.
- **Built-in Select/Back use controller names first** (`joyA`/`joyB`), with
  buttons 0/1 only for joysticks SDL has no controller mapping for (a
  defaults-only `joyUnmapped…` form) [raw buttons for everything, which puts
  Select and Back on one button on some pads; controller names only, which
  leaves arcade USB encoders without buttons on a bare install]. A
  `joyButtonN` written in controls.conf still counts on every joystick.
- **Capture ends after five seconds, or by holding any input for two**
  [a reserved cancel key, which then couldn't be captured]. Every input can
  be captured, Escape included; controller names are kept over raw button
  numbers, without a pad number.
- **Saves go to the file the value comes from** (the last `controlsN.conf`
  that sets it, else controls.conf) [always controls.conf, which an
  override would hide], and apply at once after Save [after closing
  Manage], since the lock-out guard keeps that safe.
- **Open Settings stays keyboard-only**, F10 when controls.manager lists no
  key.

### M20. Round 9 choices — Decided
- **A media folder shows a random file** [always the first; a sequence per
  visit; the next when one ends]: the OneSauce file names imply no order
  (`ARCADES 1..9` beside `Psikyo 2; video`), RetroFE already has random art,
  and removePlaylist reaches every file. The evidence is in
  ONESAUCE-COMPAT.md. Manage's media screens don't show these folders yet.
- **Window close is ignored in kiosk mode and with a blank `quit =`**, which
  is how a cabinet says nobody may leave; the quit key itself still works in
  kiosk mode, as upstream. A close clicked while a game ran is dropped when
  RetroFE returns (it was meant for the game).
- **Twin page, destructive edits only on a sure match:** delete, removing
  animations and copying animations reach the 4:3 page only when the element
  there is surely the same one (not a wheel slot paired by place or one
  showing another file); otherwise the note says it stayed [propagating on
  every match tier, which can delete a different element]. Align and
  space-out are redone there with that page's own boxes [copying the moves
  in proportion, which misaligned them].
- **Guides snap within 6 pixels** to other elements' edges and centres and
  the page centre; the grid applies on the other axis; scripts get guides
  only when asked.

### M21. Round 10 choices — Decided
- **Replace on a folder-backed slot puts the file beside the folder**
  [also emptying the folder]: RetroFE's rule already makes the file win,
  nothing is removed unasked, and "Clear the folder" is one step away.
  Clear refuses a folder-backed slot; clearing the folder is its own button
  that asks first.
- **Menu lists keep one random pick per folder** while the list is shown
  [a fresh pick each time an item is drawn, which flickers while
  scrolling].
- **"Differs from the original" is decided by size and modification time**,
  reading both files only when the times differ [comparing contents every
  time, which re-read large videos every frame].
- **A game literally named `default`** shares the collection-wide fallback
  folder: per-file edits are allowed, clearing it or keeping only one file
  is refused.

### M22. Design review choices (round 12) — Decided, the look is yours to judge
From [UX-REVIEW.md](UX-REVIEW.md) section 6; the recommended option each time.
- **Paths: an in-RetroFE file and folder browser** driven by the controller
  for every path field, with the Windows dialog as a secondary "Browse with
  Windows…" row when a mouse is present [the Windows dialog plus typing,
  unusable on a cabinet; the browser only, losing a desktop convenience].
- **Save stays per screen (M5) but is pinned** at the bottom of the panel,
  with a mapped button that jumps to it [saving when leaving a screen; live
  writes like One Saucier].
- **"Apply changes now" on the Manage hub**, plus a "changes waiting"
  indicator on every Manage screen, besides applying when Manage closes
  [only on close; a reload after every save].
- **Manage gets a two-pane look in Aeon Nox:** the list on the left; help,
  art and a preview of the focused row on the right, layout-driven [one
  list panel, as before; tiles like One Saucier]. **This is a look you
  should judge** once it's built (see "Needs your input").
- **Home keeps its order** but lands on Browse after a reload when there's
  content, and on Manage on first run [your original Catalog / Settings /
  Manage / Browse order; Catalog as the default].
- **"Theme" everywhere in the UI** (the setting key stays `layout`).

### M23. Round 12 choices — Decided
Made while building the design review (M22); the option not taken in brackets.
- **"Browse with Windows…" needs a real mouse click in Manage** and use in
  the last 15 s; motion within a second of RetroFE moving the pointer itself
  and touch don't count [any mouse motion, which RetroFE's own pointer warps
  and trackballs triggered on cabinets; a setting]. A trackball or light
  gun's button still counts: VERIFY.md B2.
- **Slow drives:** drive letters list at once; labels and free space arrive
  per drive from a background fetch, kept 30 s, one at a time per drive;
  an empty card-reader slot drops out [asking every drive on the UI thread,
  which stalled on sleeping USB disks and lost shares].
- **Folder counts** ("212 files matching zip") for the first 50 folders of
  a listing, each capped at 10,000 entries, off the UI thread [every
  folder, a few per frame]. **No thumbnail for pictures over 8 MB**
  [decoding them off the UI thread: the widget owns the textures, so it
  waits for a later round].
- **One letter jump everywhere:** ASCII letters, case aside, with leading
  spaces skipped; anything else (digits, accents, `..`) is one `#` group;
  letter up goes to the previous letter's first row. The browser's own
  rules (digits as groups, up to the start of the current group first)
  were dropped when the branches met.
- **Note-only screens scroll** with Up/Down and page with Left/Right, for
  every form [turning the save files list into results rows only].
- **Genre, Players and Rating:** "Other…" comes right after "None" [a text
  field instead of the list past 30 values].
- **The Favourite toggle shows what was just written** until the library
  is re-read [waiting for the rebuild, which looked like a missed press].
- **The keyboard remembers only keys that type** (not Del or the cursor
  keys), so the next field doesn't start on Del.
- **Changes waiting** count each thing once, an action and its reversal
  included (adding then deleting a system is one change); Catalog installs
  count and are applied when Manage closes, like library edits.
- **One hint row** at the bottom of a form; what doesn't fit is cut [two
  rows, which cost a list row on every screen]. A link's help goes there
  when the page has no help panel (the built-in layout).
- **The page down/up controls jump to the pinned actions and back**, and a
  bare install maps them to the shoulders too [a new control].
- **`auto` window size** on a bare install: the largest 16:9 window within
  90% of the display's usable area, never below 640 x 360 [1280 x 720;
  windowed full screen].
- **The generator owns every Aeon Nox page:** the keyboard's height moved
  into `Scripts/aeon-nox/gen_layouts.py` (it had been edited in the pages),
  and the panel behind it follows the generator's margin rule.
- **Saving on Windows replaces files with a POSIX-semantics rename**
  (Windows 10 1607+, NTFS), which works while the library reads the file,
  and falls back to MoveFileEx where unsupported; both retry ten times,
  30 ms apart, on access or sharing errors [reads sharing delete access
  only, which wasn't enough; retries only]. Non-NTFS drives: VERIFY.md S1.

### M24. Round 13 choices — Decided
- **Letter up/down are built in** on the pad's left/right triggers and M/N
  (upstream's keys); the shoulders stay Page Up/Down [no built-in input,
  which made letter jump unreachable on a bare install; the shoulders].
  Typing M or N in a field on a real keyboard still types.
- **`primary="yes"` marks a form's main action** (Save, Add source, Add,
  Create...); only a pinned primary button that can run lights the green
  "Page down: ..." hint after an edit [the bar's first button, which on
  Sources was Check for updates].
- **Focus never lands on a guarded bar button by itself** (one that
  confirms, or the undo that just appeared): it goes back to the list, or
  to another bar button that isn't guarded; only when nothing else can
  take focus does it go to the guarded one, held until the user moves.
- **A full hint row drops navigation hints first** (letter jump, then
  paging; Back before the primary hint); a help text or can't-run reason
  is drawn last and cut to fit [dropping whole parts by priority].
- **Back from a game** (UX F8): the screens and each section's searches
  and filters are kept with the launch and put back, at once when no
  reload happened (the library from before still validates them), else
  once it's read again with "Getting your screens back..." meanwhile. A
  press cancels only if that input was released after the game ended, so
  a key or button still held from the game doesn't [cancelling on any
  input; a fixed grace time like Browse's 0.3 s]. Typed-but-unsaved text
  isn't kept.
- **Settings wording:** fullscreen and screen size apply the next time
  RetroFE starts (a reload keeps the window); Hide mouse applies on Save.

### M25. Round 14 choices — Decided (2026-10-29)
- **"Launchers" again** in Manage, not "Emulators" (your request); form ids
  and bindings keep their old names.
- **Launcher before system on a bare install:** "Add your first
  launcher/emulator" sits above "Add your first system" until a launcher
  exists. Once one does (created, or installed with an emulator, however
  many screens deep), the way back to the hub focuses "Add your first
  system".
- **The mouse is free by default:** `captureMouse` (default no) is the only
  thing that makes RetroFE grab the pointer [grabbing it always, as
  upstream does]. Settings has a toggle; it applies on Save.
- **One folder spec:** [FOLDERS.md](FOLDERS.md) is where everything goes;
  emulator programs live in `emulators/<name>/`, which the Catalog installs
  into.

### M26. Round 15 choices — Decided (2026-10-29)
- **Add a system: launcher first, as a choice** (Left/Right) with a
  separate "Download an emulator" link [a row per launcher, which on an
  install with many launchers or cores pushed the other fields off screen].
- **Suggestions from the launcher:** a core's info file (`systemname`,
  `supported_extensions`) or the built-in emulator's system and extensions
  (DuckStation: PlayStation; PCSX2: PlayStation 2; PPSSPP: PSP; Flycast:
  Dreamcast; MAME: Arcade) fill Name and Extensions until you type in them.
- **Default games folder per FOLDERS.md**, created on Add; `list.path` is
  written only for another folder.
- **Add games copies, never moves** (your answer: games are your own
  files; Catalog game packs come later). Existing names are skipped, not
  overwritten; companions of `.cue`/`.gdi`/`.m3u` come along, but only from
  the picked folder or below. Subfolders are kept only when the system's
  games are read from subfolders; otherwise games are copied flat so
  Browse finds them. Leaving the screen stops the copy cleanly.

### M15. Linux build packages were installed in WSL — Decided
- To compile and test on Linux locally, the packages the Linux CI workflow
  uses were installed in your WSL Ubuntu (`apt-get install`, via passwordless
  sudo): SDL2 and its image/mixer/ttf libraries, GStreamer and plugins,
  minizip, webp, libusb, libevdev. Nothing else was changed there. Remove them
  with `apt-get remove` if you don't want them.

## Catalog

### C1. This fork from GitHub — Decided (publishing Deferred)
- Neither the fork nor upstream publishes GitHub releases; CI only uploads
  build artifacts, which need a GitHub login to download.
- Chosen: a `github` source type reading a repository's releases, a one-step
  "Add RetroFE from GitHub" on the Sources screen, an `app` package kind that
  updates RetroFE itself (only from GitHub sources), and a release workflow
  that runs when a version tag is pushed.
- **Deferred (your answer, 2026-10-24):** a release will come eventually,
  but nothing is published at this early stage. Creating it (pushing a tag)
  stays with you.

### C2. Emulator downloads — Decided (2026-10-29)
- **A built-in list of six emulators** (RetroArch, MAME, DuckStation,
  PCSX2, PPSSPP, Flycast) behind one `emulators` source the user turns on
  with "I trust these downloads" [a GitHub source per emulator added by
  hand: a bare repository doesn't say which file is the program, its
  arguments or how to make it portable]. Design:
  [design/emulator-downloads.md](design/emulator-downloads.md).
- **Programs are installed only from that source**, only into
  `emulators/<name>/`, decided from `sources.conf`, never from what a
  package or download says. Every other source still can't install
  programs. `emulator-` and `core-` ids are reserved for it.
- **7-Zip is read** with the LZMA SDK's decoder (public domain), because
  RetroArch, PCSX2 and MAME only publish 7-Zip (MAME as a self-extracting
  `.exe`, opened as an archive, never run) [ZIP only]. Only emulator
  packages, or files marked `"extract": true`, are unpacked; other `.7z`
  files are installed whole.
- **Checksums:** GitHub downloads must have GitHub's SHA-256; RetroArch's
  download from libretro has none, which its screen says; RetroArch cores
  are checked against libretro's CRC list (of the DLL inside each zip).
- **Cores come from libretro's nightly builds**, one zip each, offered only
  once RetroArch is installed; each makes a "RetroArch <core>" launcher.
  Nightly updates show only on the Cores screen, not as Catalog updates,
  and RetroArch can't be removed while cores are installed.
- **Each install makes its launcher** (`%RETROFE_PATH%` paths), unless one
  of that name exists. Removal keeps a launcher that was changed or that a
  system uses, and says which systems.
- **Updates replace every file the emulator shipped**, backing up the ones
  that changed (emulators update themselves; mixed versions break). Files
  the emulator wrote (settings, saves, BIOS) are never touched.

## Working method

### W1. Parallel agents work in git worktrees — Decided
- Each agent works on its own branch in a separate worktree, builds and tests
  there, and commits. Branches are merged into `feature/management-ui` after a
  Fable adversarial review and fixes. Nothing is merged to `master`.
- GUI testing happens in private test roots under `E:\Storage\Agents\<name>\`
  so agents don't disturb `E:\Storage\OneSauce` or each other.

### W2. The fork's changelog is separate from upstream's — Decided
- Upstream's `CHANGELOG.md` is left alone so upstream merges stay clean. The
  fork's rounds are summarized in [FORK-CHANGELOG.md](FORK-CHANGELOG.md).

### W4. Windows first — Decided (your direction, 2026-10-24)
- The design is still being iterated on, so development focuses on Windows;
  other platforms get tested much later.
- What changes: agents build, test and check on Windows only (no WSL runs).
  CI runs Windows on every push; the Linux and macOS jobs run only for
  `master`, pull requests to it, and by hand. Code stays portable as
  before (the existing platform splits are kept), but a Linux or macOS
  failure doesn't hold up a round.

### W5. Agents without the Windows desktop — Decided (2026-10-28)
- From round 12 the orchestrating session runs on the Unraid server, not
  on your Windows machine. Implementation, fixes and tests are done by the
  isolated Codex worker (`/mnt/dev/agent-harness`), reviews by Fable
  agents, and every patch is read before it's committed.
- **Windows CI is the build and test of record.** Branches are pushed as
  `ci/<name>` to run it; the job repeats build errors, failing checks and
  the check count as annotations, which can be read without a GitHub login.
  The worker's image has RetroFE's Linux packages (rebuilt 2026-10-28), so
  it configures, builds and runs the tests with CMake on Linux before
  Windows CI runs.
- **Test builds reach the PC through a share.** Each passing Windows run on
  `feature/management-ui` publishes the `management-ui-latest` pre-release
  (the exe and Aeon Nox, without DLLs; docs/CI.md). An agent copies it to
  `\\JOHN-DESKTOP\retrofe\management-ui\builds` with
  `Scripts/test-build/push_to_share.py`, using a share-only local account
  [Guest, which Windows disables and would open the share to the network].
  You install it with `Install-TestBuild.ps1`, which backs up each file it
  replaces and deletes nothing (Scripts/test-build/README.md).
- What needs your desktop (GUI checks, screenshots at 16:9 and 4:3,
  installing a test build) is listed in VERIFY.md instead.

### W3. CI: two macOS builds, Xcode project left unused — Decided
- `build.yml` builds macOS with Homebrew's libraries (quick, like a
  developer's Mac); `build-macos.yml` builds the universal app bundle with
  the frameworks in `ThirdPartyMac`. Both through CMake [keeping the Xcode
  project, which had none of the fork's code].
- `RetroFE/xcode` is left in place but unused and stale (`docs/CI.md` says
  so); it can be deleted later.
- The GStreamer installer used on Windows CI and releases is pinned by
  version and SHA-256; bumping the version means bumping the hash.
- A newer push cancels a branch's running build, except on `master`.
- Not run yet: `release-windows.yml` (needs a tag, see C1) and
  `build-windows.yml` (GitHub won't start a workflow the fork has never run
  by hand); both use the same install script as the Windows job that passed.

---

## Library (Manage data layer)

The full reasoning, with the options considered for each, is in
[design/manage-library.md](design/manage-library.md#decisions).

### LB1. One conf editor; SettingsFile is built on it — Decided
- `Library::ConfFile` (one file) and `ConfLayers` (later files win) replace
  SettingsFile's own code. Edits keep every untouched byte, including each
  line's own ending.

### LB2. Originals are kept in `<root>/manage/backups/` — Decided
- Before a file's first change, it's copied there at the same relative path;
  a file that didn't exist gets an `.absent` marker. RetroFE never reads
  `manage/`.
- A file that differs from the kept original is never deleted or overwritten:
  it's kept beside it first as `<name> (2).<ext>` and so on.

### LB3. Metadata edits go in `<root>/manage/metadata/<type>.xml` — Decided
- The file is hyperlist format and holds only the changed fields.
  `MetadataDatabase` applies it after importing `meta/`, so edits survive
  content updates and re-imports. Edits are also written into meta.db at once.
- Not inside `meta/`, because a change there makes RetroFE re-import on every
  start, due to its exe-time check.
- Clearing an edit restores the value RetroFE's import would give, read from
  hyperlist, mamelist and emuarc. A row that only the user's edits created is
  deleted. If a shipped file can't be read, meta.db is left alone and a
  re-import is requested.

### LB4. `media.basePath` (OneSauce) is honoured — Decided
- The order is `media.<type>`, then `media.basePath`, then `baseMediaPath`.
  The rule is applied in `Configuration` and in the library, so OneSauce game
  art shows.

### LB5–LB10 — Decided
- **LB5.** Menu-folder menus get `menu/<child>.txt`, and `menu_supported.txt`
  is kept in step.
- **LB6.** An empty `.sub` means every game: adding to it is a no-op, removing
  from it is refused, and removing the last entry deletes the file.
- **LB7.** A launcher override naming the default is removed.
- **LB8.** Media lookups check files rather than list folders.
- **LB9.** New systems are listed in `firstCollection` (read from RetroFE's live
  configuration) only when it's a menu or can become one. Otherwise they go in
  "Main", so a games collection never gets a menu.txt.
- **LB10.** The index is an immutable snapshot, rebuilt in the background. A
  change never cancels a running build: the build publishes, then one more follows.

### LB11. `launcher = a, b, c` uses `a` (fork compat) — Decided
- OneSauce/RetroFE Plus data names a default and its alternatives; 25
  collections on K: do this.
- RetroFE (`CollectionInfoBuilder`, `Launcher`) and the library use the first
  name. The library shows the rest as read-only alternatives.
- Setting the launcher in Manage moves the pick to the front and keeps the rest.
- Options considered: leave the lists unresolved, or rewrite each list to a
  single name. Both were rejected: the first stops OneSauce games launching,
  and the second loses the alternatives.

### LB12. `list.includeFile` / `list.excludeFile` are honoured (fork compat) — Decided
- When set, they replace `include.txt` and `exclude.txt` as the collection's
  lists, with the same format and rules. Paths are resolved like other keys:
  `%BASE_ITEM_PATH%`, `%ITEM_COLLECTION_NAME%`, and relative to the root.
- Applied in RetroFE's `ImportDirectory` and in the library scanner.
- Option considered: keep ignoring them. Rejected, because then stock RetroFE
  would list games OneSauce hides.

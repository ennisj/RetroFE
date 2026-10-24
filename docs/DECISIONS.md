# Decisions and open items

Decisions made while building the fork, mostly without waiting for review.
Each one lists the options considered, what was chosen and why, so any of
them can be revisited. **Open** items still need your input; everything else
is decided but can be changed. Newest rounds are added at the end of each
section.

Status: **Decided** (chosen, can be revisited), **Open** (needs your input),
**Deferred** (agreed to leave for later).

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

### L4. Fonts and artwork with unclear licences — Partly resolved, partly Open
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

### L7. The packaged `settings.conf` still says Arcades — Open
- `Package/Environment/Common/settings.conf` (the upstream-style package)
  keeps `layout=Arcades`, because its bundled collections were made for it.
  New installs created by RetroFE itself get Aeon Nox. Say if the package
  should switch too.

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

### M15. Linux build packages were installed in WSL — Decided
- To compile and test on Linux locally, the packages the Linux CI workflow
  uses were installed in your WSL Ubuntu (`apt-get install`, via passwordless
  sudo): SDL2 and its image/mixer/ttf libraries, GStreamer and plugins,
  minizip, webp, libusb, libevdev. Nothing else was changed there. Remove them
  with `apt-get remove` if you don't want them.

## Catalog

### C1. This fork from GitHub — Decided (publishing is Open)
- Neither the fork nor upstream publishes GitHub releases; CI only uploads
  build artifacts, which need a GitHub login to download.
- Chosen: a `github` source type reading a repository's releases, a one-step
  "Add RetroFE from GitHub" on the Sources screen, an `app` package kind that
  updates RetroFE itself (only from GitHub sources), and a release workflow
  that runs when a version tag is pushed.
- **Open:** creating the first release (pushing a tag) publishes a build
  publicly, so it's left to you.

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

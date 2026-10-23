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

### M10. The theme editor only edits themes made with it — Open
- One Saucier's editor generates layouts from a widget-based project; it can't
  open hand-made layouts (it ships pre-converted examples made by a 2,183-line
  Python importer). Aeon Nox, the default, is hand-made.
- Options: (1) port the editor as-is: new themes, duplicates of editor-made
  themes; (2) also port the importer (2–3 rounds, lossy by design); (3) add a
  simpler "layout-native" mode that moves and resizes the elements of any
  existing layout file, keeping everything else.
- Chosen for now: **(1)**, since you asked for One Saucier's editor; (3) looks
  like the best value for editing Aeon Nox and other hand-made themes, and is
  my recommendation for after the port. Your call.

### M11. The theme editor is drawn with ImGui — Decided
- It's a canvas editor (drag handles, overlays, colour pickers, dropdowns),
  which doesn't fit row-based forms. The Themes list stays a form; Edit opens
  the editor as an ImGui screen inside the manager, previewing with RetroFE's
  own page builder. Driving it with a controller can come later.

---

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

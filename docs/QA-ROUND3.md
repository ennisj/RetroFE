# QA, round 3: the whole fork on OnesaUCE-shaped data

Date: 2026-10-23. Build: `feature/management-ui` at 42b8d02b plus the fixes
below (RelWithDebInfo, Windows 11). Screenshots are in
`E:\Storage\Agents\qa\screenshots\`, named in the text.

## Test installs

**Realistic install**, `E:\Storage\Agents\qa\` (4.1 GB, about 40,000 files),
copied from `K:\retrofe-data` (read only) in the OnesaUCE split shape:

| Part | What |
|------|------|
| `appdata\retrofe` (root) | all 146 collections' confs, menus, `.sub` files and launcher overrides; `meta\hyperlist` (88 XMLs) and `meta.db`; `launchers.windows`, `launchers.linux`, scripts; controls; OnesaUCE's `settings.conf` untouched |
| `appdata\retrofe\settings1.conf` | relative `baseMediaPath`/`baseItemPath`, windowed 1280x720, `layout = Aeon Nox`, `homeMenu = yes`, `attractModeTime = 0`, `log = ERROR,NOTICE,INFO,WARNING` |
| `appdata\retrofe\layouts` | the fork's Aeon Nox (from `E:\Storage\OneSauce\layouts`) and OnesaUCE's Renee |
| `appdata\retrofe\retrofe` | this build plus the DLLs from `E:\Storage\OneSauce\retrofe` |
| `base_assets\collections` | menu artwork for every menu collection except SNK Neo Geo AES and THEMES |
| `content\retrofe\collections` | MAME (5,519), Sega Genesis, Atari 2600, NES and Nintendo 64: every ROM as an empty file with its real name, plus logo, story, artwork_front(_s), marquee, screentitle, system_artwork and 25 videos each |

The index sees 146 collections and 6,467 games (include/exclude lists honoured).

**Bare install**, `E:\Storage\Agents\qa\empty\`: only `retrofe\` (exe and
DLLs), no layouts, to check the first run; plus `fixture-roms\Arcade` (five
text "ROMs", one with a `.ZIP` extension) and notepad as the emulator.

**Catalog source**, `E:\Storage\Agents\qa\catalog-src\index.json`: a local
index with one package (`qa-pack`, a collection zip with a SHA-256).

All input was posted to the QA process's own window (matched by exe path);
no native file picker or message box opened during the run.

## What works

- **Boot and Home** (`qa-02-home.png`): Aeon Nox Home with Catalog, Settings,
  Manage, Browse. Start to Home 15-20 s on this data (most of it the stock collection load).
- **Browse** (`qa-03-browse-main.png`, `qa-04-arcades.png`,
  `qa-05-arcadeall.png`): Main and `1 ARCADES` use the `_menu` page with
  OnesaUCE's menu logos; `02 ARCADE ALL` (merged, 1,861 games) opens on the
  games page with metadata, story, art and logo. Back returns to Home.
- **Settings** (`qa-06-settings-f10.png`, `qa-07â€¦`, `qa-08â€¦`): F10 opens it
  over a games page; each value says which file sets it ("Set in
  settings1.conf").
- **Manage â†’ Games** (`qa-14-games.png` to `qa-46-pgdn.png`): search
  ("street", instant), system/menu filter, "no metadata" filter, paging;
  details; **Metadata** edit (Year 1993) saved to
  `manage/metadata/MAME.xml` and shown under "Changed here"
  (`qa-22-meta-saved.png`, `qa-23-meta-changedhere.png`); **Media**: logo
  replaced from a file, then put back, file byte-identical to K: afterwards,
  original kept in `manage/backups/_external/...` (`qa-27` to
  `qa-33-putback-done.png`); **Playlists**: favourite on, written to
  `collections/MAME/playlists/favorites.txt` (`qa-36-fav-added.png`);
  **Description** shows the story (`qa-38-description.png`).
- **Systems** (`qa-47` to `qa-74`): list with counts and launchers; details;
  **Settings** save changed only `list.includeMissingItems` and kept the
  file's comments (diffed); **Launcher**, **Info**, **Media**; **Playlists**:
  create, add a game, delete (`qa-59` to `qa-66`); **Check this system**;
  **Add** a scratch system to `1 ARCADES` (written to `menu.txt` and
  `menu_supported.txt`) and **Delete** it (both lines taken out again,
  folder in `manage/backups/deleted/`) (`qa-70`, `qa-74-deleted.png`).
- **Videos** (`qa-76-videos.png`, `qa-77-addvideo.png`): empty list and the
  add form with the video preset.
- **Menus** (`qa-78` to `qa-92`): tree from Main; merged collection details
  and Sources; New menu in `1 ARCADES`; Entries add, move up, remove
  (`qa-87`, `qa-89-entry-moved.png`, `qa-90`); Delete the scratch menu, its
  entry moved up into `1 ARCADES` and nothing else touched.
- **Emulators** (`qa-95` to `qa-102`): list and "Used but not defined";
  THEMES details; Duplicate and edit, Create, Delete of the copy.
- **Themes** (`qa-103` to `qa-105`, `qa-126` to `qa-157`): list with "In use"
  and "Editor"; Renee's details; **Use this theme** wrote `layout = Renee`
  to `settings1.conf` (the file that sets it), and closing Manage reloaded
  into Renee (`qa-107-renee-home.png`); **New theme**, the editor opened
  (`qa-154-editor.png`, preview built from the real Main menu), **Delete**.
- **Theme editor script** (`-editorScript E:\Storage\Agents\qa\editor-script.txt`):
  25 of 26 commands ok (add wheel and title, nudge/move/resize, name, layer,
  colour, undo/redo, preview MAME, rebuild, save, close); save wrote 2 files
  with 0 warnings. `alpha 0.8` failed (see problem 13).
- **`--theme-check`**: New Theme 1 and Aeon Nox (all nine pages) OK;
  Renee exit 1 (see problem 14).
- **Catalog** (`qa-110` to `qa-128`): Sources screen; adding an index source
  on disk (trust switch required) wrote `catalog/sources.conf`; the Catalog
  list showed QA Pack; Install unpacked it, wrote `catalog/installed/qa-pack.json`,
  added it to Main's `menu.txt` and `menu_supported.txt`, and RetroFE reloaded
  on Home; Systems listed it with 4 games; Remove deleted it and took it off
  Main again.
- **Reload after Manage**: every change above showed after closing Manage
  (about 10 s reload on this data).
- **First run** (`firstrun-02-home.png` to `firstrun-18-arcade.png`): Manage
  opens with the welcome note; adding a system with the on-screen keyboard
  and typed paths works, the folder count warns about `mslug.ZIP`; after
  closing Manage, Home and Browse â†’ Main â†’ Arcade work with the built-in
  pages.
- **4:3** (`randomLayout = layout 4x3`): Home, `_menu`, games page, Settings,
  Manage and the editor all use the 4x3 pages (`qa-143` to `qa-152`).

### Performance

| What | Time |
|------|------|
| Index, first build after start | 2.24 s (146 collections, 6,467 games) |
| Index, rebuild after an edit | 0.52 s |
| Game search ("street", 5,519 MAME games) | no visible delay while typing |
| Opening Games, Systems, Menus, Emulators, Themes | about 0.95 s each from key to screenshot, of which 0.45 s is the posted key and a fixed 0.3 s wait: the screens themselves open at once |
| Opening a game's details / Media | about 1.7 s from key to screenshot, including a fixed 0.8 s wait (media lookups on this data) |
| Reload after closing Manage | about 10 s, the same as a start |

## Problems found

Severity: **High** breaks a task, **Medium** misleads or loses work,
**Low** wording, noise or polish.

| # | Sev | Where | Problem | Status |
|---|-----|-------|---------|--------|
| 1 | High | First run | After adding the first system in Manage â†’ Systems on a bare install, no `settings.conf` is written (only the Catalog and the old Setup path wrote one), so `RetroFE.cpp` keeps opening Manage at **every** start (`firstrun-20-second-start.png`). | Fixed |
| 2 | Medium | Settings | Back/Escape after changing a setting dropped the change silently (`qa-149-hidemouse-on.png` â†’ `qa-150-esc-with-pending.png`); every other edit screen warns. | Fixed |
| 3 | Medium | Systems add, Menus new | After **Add system**, Back from the new system went to the add form, still filled in, inviting a duplicate add (seen on the first run and on the QA install). Menus **Create** opened the new menu the same way. | Fixed |
| 4 | Medium | Emulators | **Duplicate and edit** dropped `reboot = yes` (THEMES copy came out without it), so a copied theme switcher wouldn't restart RetroFE. | Fixed |
| 5 | Medium | Launching (stock) | A bare `executable = cmd.exe` is resolved against the RetroFE root, so OnesaUCE's `launchers.windows/THEMES.conf` (theme switcher) can't start on Windows. Manage's Emulators check already looks on the path and says "found on the system path", so the two disagreed. | Fixed (not launched live; see below) |
| 6 | Medium | Menus (stock) | `MenuParser` sorts `menu/` entries with `<=`, which isn't a strict weak ordering: undefined behaviour in `std::sort` (it can crash with equal names). | Fixed |
| 7 | Low | Systems â†’ Check | "19 games have launchers of its own that isn't installed" (`qa-49-check-a2600.png`). | Fixed |
| 8 | Low | Systems â†’ Launcher | "19 games have launchers of its own; change those in Games." (`qa-55-sys-launcher.png`). | Fixed |
| 9 | Low | Menus â†’ Actions | "its 1 entry move up intoâ€¦" (`qa-91-qamenu-actions.png`). | Fixed |
| 10 | Low | Systems â†’ Info | The Title field's hint "Arcade" reads like a value on systems that have no title (`qa-56-sys-info.png`). | Fixed: "e.g. Arcade" |
| 11 | Low | Menus â†’ Sources, MANAGER.md | "Choosing which games of a source it shows will come with the Games screens" is out of date: a game's Playlists and collections screen does it. | Fixed (form and doc) |
| 12 | Medium | Emulators list | With OnesaUCE data on Windows almost every launcher is "Used but not defined" (OnesaUCE ships `launchers.linux` only), so the list is long and **Add a launcher** sat below it, several pages down (`qa-95-emulators.png`). | Fixed: the link is above the section |
| 13 | Low | Theme editor script, THEME_EDITOR.md | `alpha N` takes a whole percent (0â€“100); `alpha 0.8` fails with no reason given. The doc just says `alpha N`. | Report only (Themes/ThemeEditor area) |
| 14 | Medium | Renee (OnesaUCE theme) | `--theme-check Renee` exits 1: its pages use One Saucier/RetroFE Plus features stock ignores (`mode="layout_preferred"`, `textFormat`/`Height`/`maxwidth` on `reloadableImage`). Home and menus render, but not as on the cabinet (`qa-107`, `qa-108`, `qa-109`). | Compat, report only |
| 15 | Low | Games â†’ Launcher | OnesaUCE systems show "Not installed: lr-mame2003_plus, lr-fbneo, â€¦" for every game (`qa-24-launcher.png`): correct for this data on Windows, but nothing helps fix it (see recommendations). | Report only (Games area) |
| 16 | Low | Systems â†’ Playlists | OnesaUCE keeps some systems' playlists in the content folder (`content/.../Atari 2600/playlists`, 18 files); stock RetroFE and Manage read only `collections/<c>/playlists`, so they don't show (`qa-58-sys-playlists.png`). Consistent with RetroFE, but surprising. | Report only |
| 17 | Low | Log | Every start logs `[ERROR] Could not import any collection settings for <menu>` for ~40 OnesaUCE menus that have only `menu.txt`/`info.conf`; `[ERROR] [HiScores]` twice on installs without `hi2txt`; `[ERROR] [SDL] Unable to set DPI awareness hint`; and 12 `layout - N.xml does not exist` warnings per page load (monitor probing). None are real errors. | Report only (stock code) |
| 18 | Low | Home (Renee) | Renee has no `collections/Home` or `collections/_menu` page, so Home is drawn with its main page and has no art for its items (`qa-107-renee-home.png`). Expected for a theme made before Home; worth a note for theme authors. | Report only |

Problems 13â€“18 need decisions or touch areas other agents own; nothing in
Manager/Games, FormWidget/KeyboardWidget, Manager/Themes, ThemeEditor or
PageBuilder was edited.

## Fixes made

| # | Change | Files | Test |
|---|--------|-------|------|
| 1 | When Manage closes with a reload pending, `ensureSettingsFile()` writes the built-in defaults to `settings.conf` if there's none (as the Catalog path already did). Checked live: fresh bare install â†’ add a system â†’ close Manage â†’ `settings.conf` created â†’ the next start shows Home (`firstrun-26-home.png`, `firstrun-27-second-start-fixed.png`). | `Manager/ManagerUI.cpp` | GUI (ManagerUI is app-only) |
| 2 | `ManagerModel::leaving` warns on `settings` with pending changes; `discard` drops them. Checked live (`qa-160-settings-leave-warn.png`, then leaving again wrote nothing). | `Manager/ManagerModel.cpp` | FormsTests: "leaving Settings with unsaved changes warns once, then drops them" |
| 3 | New `Result::replace`: with `open`, the opened screen takes the current one's place (`Navigator::replace`). Systems/Videos add and Menus create use it. Checked live (`qa-164-added-replace.png` â†’ `qa-165-back-goes-to-list.png`, `firstrun-25-back-list.png`). | `Forms/Form.h`, `Manager/Navigator.*`, `Manager/ManagerUI.cpp`, `Systems/SystemProvider.cpp`, `Menus/MenuProvider.cpp`, `docs/design/manage-forms.md` | FormsTests: "a screen can take the place of the one it came from"; SystemsTests and MenusTests check `replace` |
| 4 | `NewLauncher::reboot` (writes `reboot = yes`); the add form has a "Restarts RetroFE" switch, filled in by Duplicate. | `Library/Edits.*`, `Emulators/EmulatorProvider.cpp`, `Emulators/EmulatorsForms.cpp`, `docs/design/manage-entities.md` | EmulatorsTests: "a copy of a launcher that restarts RetroFE restarts it too" |
| 5 | On Windows, a relative executable with no folder that isn't in the root is looked up with `SearchPathA` (as a command prompt does). | `Execute/Launcher.cpp`, `docs/LAUNCHERS.md` | Not launched: running THEMES would start `cmd.exe` and PowerShell on the user's desktop |
| 6 | Strict `<` comparator. | `Collection/MenuParser.cpp` | Build |
| 7â€“11 | Wording. | `Systems/SystemProvider.cpp`, `Systems/SystemForms.cpp`, `Menus/MenuProvider.cpp`, `Menus/MenusForms.cpp`, `docs/MANAGER.md` | Existing tests |
| 12 | "Add a launcher" moved above "Used but not defined". | `Emulators/EmulatorsForms.cpp` | Existing tests |

Tests: 3,894/3,894 checks pass (`retrofe_tests.exe`).

## Recommendations

1. **Launchers for OnesaUCE data on Windows** (15, 12): Emulators could
   list another platform's launcher files (read-only) and offer "Make a
   Windows copy" from a Linux launcher, prefilled. That turns the long
   "Used but not defined" list into a to-do list.
2. **Log levels** (17): log "no settings for <collection>" at INFO when the
   collection is a menu (`menu.txt`/`menu/`), the HiScores messages at
   NOTICE when `hi2txt` isn't there, and the per-monitor layout probes at
   INFO. The log would then show only real problems.
3. **Editor script** (13): accept `alpha 0.8` as well as percent, or say
   "alpha N (percent, 0-100)" in THEME_EDITOR.md and give a reason on failure.
4. **OnesaUCE themes** (14): `--theme-check` already pinpoints the
   unsupported features; a short "known differences" section in LAYOUTS.md
   (layout_preferred, textFormat on images) would help users of those themes.
5. **Playlists in the content folder** (16): decide whether Manage should
   at least mention playlist files it finds next to the ROMs.
6. **A live launch test**: launching wasn't exercised (it would open
   windows on the user's desktop). A dry-run option that logs the command
   line without running it would let QA cover launchers and test-launch.
7. **Driver note for future QA**: Q quits only outside the manager, and the
   built-in page's Select is also `1`; post Escape until Home before Q.

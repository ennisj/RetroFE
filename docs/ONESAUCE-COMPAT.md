# OneSauce compatibility
[Back](README.md)

OneSauce installs ship themes and settings written for a newer, forked
RetroFE. This page lists everything in that data that this RetroFE didn't
read, what it seems to mean, and what was done about it. The rule: support
a name only where the data itself makes its meaning clear (how it is used,
comments and READMEs shipped with it); otherwise leave it, and say what
would settle it. The other fork's source was not consulted.

Compat code is marked "Fork compat" in the source. Layout parsing is in
`Graphics/LayoutCompat` (tested in `Tests/CompatTests.cpp`).

## What was surveyed

- `K:\retrofe-data` and `I:\` (the same OneSauce data; every count below
  is identical on both): 1215 XML files under `base_assets/layouts` (17
  themes, 16 "- Imported" copies made by One Saucier, `default`, `Test
  theme`), 313 `layout.lua` files (and 10 in the imported copies) and one Lua module, `settings*.conf`,
  147 collections' `settings*.conf` and `info.conf`, `controls.conf`, 103
  `launchers.linux` and 1 `launchers.windows` file.
- Each XML element and attribute was checked against what
  `Graphics/PageBuilder.cpp` reads (the same vocabulary as the theme
  editor's checker, `ThemeEditor/LayoutCheck.cpp`), and each settings key
  against `Database/GlobalOpts.h` and the keys the source reads.
- The XML was read as RetroFE's parser reads it (a missing space between
  attributes, `x="1"y="2"`, is accepted).
- **Counts** are occurrences, as live / imported / dead:
  - live: in a page RetroFE loads (`layout.xml`, `splash.xml`, `layout -
    N.xml`, or the XML a `layout.lua` builds) of an original theme;
  - imported: in One Saucier's `<theme> - Imported` copies (those also carry
    One Saucier's own additions, marked below);
  - dead: in backups and templates nothing loads (`layoutold.xml`,
    `layout1.xml`, `Templates/`).

## Layouts

| Name | Where used | Count (live / imported / dead) | Meaning, and the evidence | Decision |
|---|---|---|---|---|
| `layout.lua` instead of `layout.xml` | Aeon Nox (root and all 131 collections, no XML pages at all), LUNA OG (root + 164 collections, 163 also have a layout.xml), the Jukebox collection of 15 other themes and Gemini's root (both with a layout.xml beside) | 313 files | `build(pageBuilder)` calls `pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. "/collections/main.xml")`, or `luna.build(pageBuilder, "../../arcade_games.xml")` through `lua/luna.lua`. Aeon Nox's README lists which XML serves which collections, matching the lua files. The paths only resolve with layoutPath = the lua file's folder. `preUpdate`/`postUpdate` hooks show/hide components at run time (e.g. LUNA OG picks a horizontal or vertical cabinet by the video's shape). | **Implemented**: a page with no XML is built from the one XML file `build()` names (static reading, no Lua; the path after `layoutPath` may start with `/` or not; strings, `--` and `--[[ ]]`/`[==[ ]==]` comments and long strings are skipped when looking for `end` and keywords). The Themes list and the library count such pages. XML beside a lua file wins, so LUNA OG keeps its standalone per-collection XML, which works without the hooks; its newer shared XML files would show both cabinets at once without them. Hooks: **not implemented** (needs a Lua runtime and the other fork's page API). Aeon Nox went from "RetroFE exits: Could not create page" to drawing its pages (main menu, system menus, game lists checked). |
| `transform="(x, y), (w, h); (x1, y1), (x2, y2), (x3, y3), (x4, y4)[; n]"` on reloadableVideo, image, reloadableImage | Amiga Memories, Atari Girl, Cafe80s, Cyberpunkd, Fan Art Magazine, Live Wallpapers, NEVATO, Old Memories, Pacman, Princess Power | reloadableVideo 105/105/54, image 8/7/0, reloadableImage 3/4/0 | A rectangle (x, y, w, h) and four corners: the TV or monitor drawn at an angle in the background (TL, TR, BL, BR, checked against the art). The rectangle is the component's own box in layout units: Amiga Memories' video sits at 0,0-400x300 with `transform="(0, 0), (400, 300); ..."`, and Gemini's `(1340, 440, 400, 200)` is exactly its logo's box (x=center+580, width 400, maxHeight 200). Without it the video is drawn in the top-left corner, under the background. | **Implemented** as a perspective (projective) map from the rectangle onto the quad, applied to whatever the component draws, drawn as a 16x16 mesh. Also the `(x, y, w, h), corners` spelling. The trailing `; 50` / `, 1` is ignored: it can't move anything under a projective map (likely mesh quality), and values differ for identical quads. Not with containers, angle, reflections, or on rotated/mirrored screens. The corners must make a convex quad in that order (else the transform is ignored with a warning, never the page); while a component's box moves outside the rectangle, points past the quad's horizon aren't drawn and the rest are clamped. |
| `transform="quad (x, y, u, v), ..."` | Gemini (on `<view>`) | 3/0/0 | Corners with texture coordinates (upper half of the video on the top DS screen, lower half on the bottom). | **Not implemented**: only on `<view>`, which isn't built either. |
| `mode="layout_preferred"` on reloadableImage | Aeon Nox, Cafe80s, CoinOPS, Cyberpunkd, Renee | 61/183/16 | Used for `cabinet`, `marquee`, `fanart` (and once logo, playlist). In Renee's and CoinOPS's arcade-system menus the cabinet exists only in the layout (`layouts/Renee/collections/Cave/system_artwork/cabinet.png`) and the marquee only in the collection's media (`base_assets/collections/Cave/system_artwork`): the same page needs both found, so "layout's copy first, else the usual place". | **Implemented**: each lookup step tries the layout folder first, then the usual folder. The cabinets now show. Option not taken: run the whole layout chain before the whole normal chain; the data doesn't tell these apart (no system has both copies). |
| Media folders: `<name>/` in place of `<name>.<ext>` | Collections' media: 52 of the 58 folders in `base_assets/collections` (K: and I: alike) have `system_artwork/video/` and no `system_artwork/video.*` (none has both). Layouts: Cafe80s `1 ARCADES/medium_artwork/video/default/` (4 videos) and `Main/medium_artwork/background/<entry>/` (7 entries, 1-3 jpgs each), `1 COLLECTIONS/medium_artwork/background/<entry>/`; Fan Art Magazine `_common/medium_artwork/cabinet/default/` (cabinet pngs); Live Wallpapers `_common/medium_artwork/video/default/`; Simple Blue `MAME/medium_artwork/video/<game>/<game>_1.mp4, _2.mp4` (220 games) | 52 collections; 4 themes | A folder of interchangeable files where one file would be: the names are free (`ARCADES 1..9.mp4`, `Racing-1..6.mp4`, `Gaelco1..3.mp4`, but also `Arcade Classics.mp4; Arcade3.mp4; video.mp4`, `Psikyo 2.mp4; video.mp4` with no 1, `Gun Games.mp4; Light Gun.mp4; One saUCE - Light Guns...mp4`), so no order or count is meant. Every folder sits exactly where RetroFE looks for a file of that name. Only video and image folders exist (no other system_artwork subfolders but Cyberpunkd's and Renee's `transition/`, which are named per system, not a media folder). Live Wallpapers' folder has a README "Put live wallpaper videos here." (several to choose from). Cafe80s's menus say "B: play different video (if available)", and B is `removePlaylist = O,joyB` in OneSauce's controls.conf, which on a menu entry does nothing but reload the item's media; its game lists say "Y/B: add to/remove from favorite list" instead. | **Implemented** in the reloadable media lookup (reloadableImage/Video/Audio, `LayoutCompat::findMedia`): when `<name>.<ext>` is missing, a file of `<name>/` with an accepted extension. **Random** when the folder comes up (several files, nothing saying which comes first); kept while the next item uses the same folder (a game list's system video doesn't restart on each scroll, as with one file); **removePlaylist** (B) moves to the next file in natural name order, wrapping. A file beside a folder wins (upstream unchanged, `.webp` included). Not at the step that looks in a game's own ROM folder (`list.romHierarchy`): a PC game's `video\` or `music\` there is the game's own, not art. Only components showing a folder's file react to removePlaylist. Each media folder's parent is listed once per run, so a miss costs no file system call. Options not taken: always the first (the rest would only show with B); sequential on each visit (needs state per folder, and nothing in the data asks for a sequence); the next video when one ends (RetroFE loops videos; Cafe80s's entry video has `numLoops="1"` and simply stops). **Menu (scrolling list) art** uses folders too (no shipped theme has one there, but nothing in the data says menus differ): where the image or video builder finds no `<name>.<ext>` at a step, a file of `<name>/` (not at the item's own ROM folder step); the random pick is kept per list and folder (`LayoutCompat::menuFolderFile`), so an item keeps its picture while the list scrolls and builds it again, and items sharing a `default/` show the same file, as with one file; only hits are kept, a miss costs the cached subfolder lookup. Upstream is unchanged where no folder exists. **Manage's media screens** (Games, Systems, Videos, Menus) show a folder-backed slot as "a folder of N files (one shown at random)", list its files, add a file to it, take one out, make it one file (*Keep only this file*) or clear it, all kept in `manage/backups` and undone by *Put back the original*; a file beside a folder is shown as winning, and Replace/Clear never remove a folder. Folders count as art a slot has in Manage's counts (`3 of 6`). RetroFE forgets its file and folder listings when it reloads, so Manage's changes to a folder show after closing Manage. Before: Renee's ARCADE cabinet and Cafe80s's main video area were black. |
| `<scrollingText src="file.txt" ...>` | Cafe80s | 138/138/18 | Same attributes as reloadableScrollingText (direction, alignment, scrollingSpeed, startPosition, startTime, endTime) plus a src naming a fixed text file: the controller instructions in the top bar (`collections/_common/text/menu_instruct.txt`). src is relative to the theme folder, as image src. | **Implemented** as reloadableScrollingText type "file". |
| `pauseTime` on scrollingText | Cafe80s | 152 in the pages checked | Probably a pause between lines of a vertical scroll; always 1. | **Unclear**, ignored. Would settle: a theme where it varies, or a recording. |
| `textFormat` on reloadableImage | Cafe80s, Cyberpunkd, Renee | 127/245/49 | Always with `textFallback="true"` on a playlist image: the fallback text in upper case, as textFormat on reloadableText. | **Implemented** for the fallback text. (The fallback shows the item title, as upstream; whether the other fork shows the playlist name there is **unclear**.) |
| `textFormat`, `textFallBack` (capital B) on menu `<item>`/`<itemDefaults>` | Cyberpunkd, LUNA OG | 108/108/1 each | Upper-case text fallback per menu item. RetroFE reads `textFallback` on `<menu>` only, case-sensitively. | **Unclear** (depends on the case question below). |
| Case variants: `Height` (reloadableImage, image), `Width`, `maxwidth`, `loadfontSize` (layout), `textFallBack`, `<Reloadableimage>` | nearly every theme; `Reloadableimage` in LUNA OG | Height 285+1/1/53, maxwidth 48/48/1, Width 20/2/0, loadfontSize 12/0/0, textFallBack 147/2/0, Reloadableimage 18/1/0 | Typos of known names. `Height="540"` sits where sibling components say `maxHeight="540"` (letter strips, `width="27"`), so either reading is plausible. | **Unclear**, ignored (RetroFE, like upstream, is case-sensitive). Reading them would resize many components. Would settle: screenshots from the other fork. |
| `<reloadablePanningImage>` | CoinOPS, NEVATO (Pacman: dead) | 6/6/1 | Artwork panned/zoomed (imageWidth, imageMinHeight, speed, zoomSpeed, zoomScaleTo, threshold). The front-art panel is empty without it. | **Unclear**: the parameters' meaning and units can't be read from 13 uses. |
| `<view ref="..." ...>` | Gemini | 4/0/0 | Draws another component (by id) again, with its own transform; Gemini's Lua hooks choose which view shows. | **Not implemented** (needs ids as names, the quad form, and the hooks). |
| `onJumpEnter`, `onJumpExit` | Cyberpunkd | 240/0/6, 134/0/6 | On the letter strip, beside onMenuScroll; other themes use upstream's `onMenuJumpEnter` for this. | **Unclear**: an alias, or a typo the author never saw? |
| `onPlaylistJumpEnter/Exit`, `onMenuJump`, `onMenuJumpEntrance`, `onMenuScrollEnter` | Cafe80s, Cyberpunkd | 1-2 each | Look like typos of upstream events. | Ignored. |
| `onEmpty` (with `<set alpha="0"/>`), `<stop/>` in an event | Aeon Nox | 2/2/0, 6/6/0 | Hide when there's no art; stop the current animation on scroll. | **Unclear**, ignored. |
| `algorithm` on `<menu>` | Aeon Nox, Cyberpunkd, LUNA OG | 258/7/1 | Easing of the scroll (always easeincircular). | Ignored: harmless (upstream scrolls linearly). |
| `mode="system"` on `<menu>` | Aeon Nox (main.xml) | 1/361/0 | Aeon Nox's README: "main.xml uses system mode in menu, this one uses layout mode". Item art from each collection's system_artwork. | Ignored: a menu with no mode already falls back to each collection's system_artwork, so the logos show; only the order differs. |
| `mode="systemlayout"` on `<menu>` | Cyberpunkd | 1/1/0 | | Ignored. |
| `hwdec` on video, reloadableVideo | most themes | 34/43/0 | Hardware decoding hint. | Ignored: harmless (`HardwareVideoAccel` setting). |
| `keepOldWhileLoading`, `scaleMode="stretch"`, `static`, `delay`, `reload`, `reloadTransform`, `miScrollTime` | Aeon Nox, LUNA OG | 1-6 each | Flicker avoidance, scaling (width and height are both given, so it stretches anyway), start delay, a typo of minScrollTime. | Ignored: harmless or unclear. |
| `endPosition` on reloadableScrollingText | Beavis and Butt-Head, Simple Blue | 3/0/0 | | **Unclear**, ignored. |
| `uppercase="1"` on reloadableScrollingText | Cafe80s | 118/0/18 | Always beside `textFormat="uppercase"`. | Ignored: redundant. |
| `angle` on `<animate>` | Cyberpunkd | 2/0/0 | Typo for `type="angle"`. | Ignored. |
| `numLoops`, `minShowTime` on reloadableVideo; `value` on statusText | the "- Imported" copies | 1/593/0, 1/593/0, 0/16/0 | Written by One Saucier's importer, not the other fork. | Ignored. |
| Top-level `<onExit>`, `<onIdle>`, `<onHighlightExit>` | Cyberpunkd | 0/0/106, 53, 53 | Only in dead files. | Ignored. |

## Settings

| Name | Where used | Meaning, and the evidence | Decision |
|---|---|---|---|
| `%RETROFE_PATH%` in settings values | settings.conf: `baseMediaPath = %RETROFE_PATH%/../../base_assets/collections`, `baseItemPath`, `baseLayoutPath`, user commands | The RetroFE root, as in launchers (which already expand it). | **Implemented** for paths: Configuration::getPropertyAbsolutePath and the library's Properties::getAbsolutePath. Plain reads stay as written (the Manager edits launchers as written). Before: no media, ROMs or layouts with OneSauce's own settings.conf. |
| `baseLayoutPath` | settings.conf | Commented "Override layouts folder location". I: has no `appdata/retrofe/layouts` at all (K: has a junction made by hand). | **Implemented** everywhere RetroFE reads or writes themes: page building, menus, reloadable media and text, video playback, forms, the Settings layout list, `--theme-check`, Manage's Themes list and create/duplicate/delete, both theme editors, the library's layout listing (`ScanOptions::layoutsPath`, else the setting) and Catalog installs (target `layouts`). Ignored when the folder doesn't exist. A Catalog package is refused if the layouts folder holds the RetroFE folder itself. **Not followed by self-update**, which still puts the layouts shipped with RetroFE in `<root>/layouts`: it runs its recovery at start-up before settings are read and checks every journaled path against the program and `<root>/layouts` folders; following the setting would mean trusting a layouts folder written in the journal. OneSauce doesn't use the shipped layouts. |
| `baseMediaPath`/`baseItemPath` overridden in `settings1.conf` | the K: dev setup | Configuration cached both on its first lookup, which can come before settings1..15.conf are read, so the override was silently ignored (the library already read the final value). | **Fixed**: looked up each time a value uses `%BASE_MEDIA_PATH%`/`%BASE_ITEM_PATH%`. |
| `firstCollectionJail` | settings.conf (`no`) | "If yes, jail the navigation inside firstCollection". | Ignored: off in the data; not display. |
| `enableResolutionChange`, launcher `resolution = 720x480` | settings_HA8819.conf, 14 launchers.linux | Display mode switching around a launch. | Not implemented (not display of themes; Linux). |
| `defaultVolume = 1.0` | settings.conf | Video volume. | Ignored: matches the default. |
| `videoAudioDelay = -0.3` | settings.conf | "Positive delays audio". | **Clear, not implemented**: audio only; would map to GStreamer's av-offset once its sign is checked. |
| `autoScanCollections`, `autoRefreshMetaDB` | settings.conf | Startup scanning. | Ignored: no display effect. |
| `user_command_new_item_selected`, `user_command_pre_launch`, `user_command_post_launch` | settings.conf, settings_windows.conf | Run scripts (LCD marquee, a loading screen). | Not implemented: runs external commands; out of scope. |
| `settings_windows.conf`, `settings_HA8819.conf` | root | Platform/board overlays. Who loads them (and when) isn't in the data. | **Unclear**, not loaded. |
| `searchMediaWithoutParenthesis = true` | 2 collections (Sega Genesis, Sega MSU-MD) | Find art by the name without "(USA)" etc. Whether as fallback or instead, and whether [brackets] too, isn't clear. | **Unclear**. |
| `custom = yes` (info.conf) | 3 collections | | **Unclear**, harmless. |
| `media.basePath`, `list.includeFile`/`excludeFile`, `launcher = a, b, c`, `list.romHierarchy` | collections | | Already supported (DECISIONS LB4, LB11, LB12). |
| SDL game controller names in controls.conf (`joyA`, `joyDPUp`, `joyLeftX-`, `joyRightTrigger`) | controls.conf | SDL GameController button and axis names. Clear. | **Implemented**: every token in OneSauce's controls.conf (buttons, d-pad, sticks with `+`/`-`, triggers, `joy0DPDown`) through SDL's game controller support, alongside the joystick tokens; pads plugged in later work. An unknown token is skipped with a warning and never stops RetroFE (a control left with nothing that works gets the built-in default). OneSauce's file runs without a controls1.conf; Settings > Controls edits it. See CONTROLS.md. |

## Also changed

- An image a page names is looked for in the page's folder, then the
  layout's folder (upstream). The failed open of the first is no longer
  logged as an error when the second exists: collection pages that use the
  layout's images (all of Aeon Nox's) logged one per image, and the theme
  check reported each as a problem.
- `--theme-check` reads the pages a layout.lua builds, from the lua file's
  folder, and accepts a root layout.lua in place of layout.xml. It no
  longer reports transform, layout_preferred, textFormat on media or
  scrollingText.
- `--theme-check` counts the names this page lists as **Ignored** (harmless,
  redundant, typos, and the unclear ones that change little: `hwdec`,
  `algorithm`, `keepOldWhileLoading`, `scaleMode`, `static`, `delay`,
  `reload`, `reloadTransform`, `miScrollTime`, `endPosition`, `uppercase`,
  `pauseTime`, `numLoops`/`minShowTime` on reloadableVideo, `value` on
  statusText, `angle` on animate, the case variants `Height`, `Width`,
  `maxwidth`, `loadfontSize`, `textFallBack`, `<Reloadableimage>`,
  `textFormat` on menu items, the event typos, `onJumpEnter/Exit`,
  `<onEmpty>`, `<stop>` in an event, `mode="system"`/`"systemlayout"` on a
  menu, the top-level events of dead files) on one "ignored (OneSauce)"
  line with a count per name, not as problems
  (`ThemeEditor::checkLayoutXml(xml, &ignored)`). What draws differently
  stays a problem: `<reloadablePanningImage>`, `<view>`, the quad
  transform, any other unknown name, and missing files. On the QA copy of
  K: (`r8-themecheck-b-*` before, `E:\Storage\Agents\compat2\screenshots\themecheck-*.txt`
  after): Cafe80s 512 problems to 4 (the splash image and three text files
  its 1 HANDHELDS page names), Aeon Nox 32 to 1 (its splash background),
  Renee 16 to 1, Amiga Memories 8 to 6 (missing files). The theme editor
  still lists every one of them, as it checks pages being written for this
  RetroFE.
- A missing layout sound is warned about once per file (the splash and the
  main page both load the layout's sounds, so each was warned twice).

## Unclear items, second look (round 9)

Re-surveyed after the media folders: none became clear. `onJumpEnter/Exit`:
Cyberpunkd's 212 pages use both `onJumpEnter` (352) and upstream's
`onMenuJumpEnter` (110), so it isn't a plain alias the theme relies on.
`searchMediaWithoutParenthesis`: the two collections that set it have no
ROMs or media on K:/I: to compare names against. `pauseTime` is still
always 1, `endPosition` still 3 uses, and the case variants, panning
image, `settings_windows.conf` and `custom = yes` have nothing new.

## Screenshots

`E:\Storage\Agents\compat\screenshots\` on the development machine (a slice
copied from K:, run with OneSauce's own settings.conf):
`before_raw_renee_main.png` (built-in layout: no layouts folder) and
`after_raw_renee_main.png`; `before_wa_renee_arcadesystems.png` (no
cabinet) and `after_raw_renee_arcadesystems.png`; `after_raw_aeonnox_*.png`
(before, RetroFE exited: `before_wa_aeonnox_log.txt`);
`before_abs_amigamemories_consoles.png` (monitor empty) and
`after_raw_amigamemories_consoles_transform.png` (and `_4x3`);
`before_wa_cafe80s_main_noscrollingtext.png` and
`after_raw_cafe80s_main_scrollingtext.png`.

Media folders, in `E:\Storage\Agents\compat2\screenshots\` (a copy of QA
round 8's slice of K:, OneSauce's own settings): `before-renee-arcade.png`
(Renee, Main, ARCADE: the cabinet's screen is black) and
`after-renee-arcade.png` (one of `1 ARCADES/system_artwork/video/` plays),
`after-renee-arcade-B1.png` (after removePlaylist, the next video),
`after-renee-arcade-again.png` (back on ARCADE after COLLECTIONS: a random
one again); `after-cafe80s-main2.png` (Cafe80s's main video area).

Media folders in Manage and menus, in `E:\Storage\Agents\mediafolders\screenshots\`
(a copy of the same slice, Renee, 1280x720): `menus-artwork-list.png` (1
ARCADES' artwork: Video is "a folder of 9 files (one shown at random)"),
`menus-video-folder.png` (the slot, its note and files), `menus-folder-file.png`
(one file: Keep only this file, Take it out of the folder),
`menus-file-taken-out.png` (8 left, the file in manage/backups),
`menus-video-folder-buttons.png` and `menus-put-back.png` (put back: 9 again);
`menu-list-folder-art.png` (the 1 ARCADES menu with `2 ARCADE GENRES.png`
moved into a `2 ARCADE GENRES/` folder of two logos in the test copy: the
item shows one of them, the same on both of the wheel's copies of it).

[Back](README.md)

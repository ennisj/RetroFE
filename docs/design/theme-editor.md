# Design: the theme editor

Status: the core is ported (rounds 1–3 of the porting plan: core port,
widget dialect audit, storage and saving), and the first UI round is done:
the Themes screens and the editor screen (see "UI round 1" below). The core
lives in `RetroFE/Source/ThemeEditor/` (namespace `ThemeEditor`) and is
tested by `RetroFE/Tests/ThemeEditorTests.cpp`; the screens are in
`RetroFE/Source/Manager/Themes/`, tested by `RetroFE/Tests/ThemesTests.cpp`.
User-facing: [../THEME_EDITOR.md](../THEME_EDITOR.md).

The editor is ported from One Saucier's theme editor. One Saucier stays a
separate project: code was copied over and adapted, nothing links to it or
depends on it.

## What the editor is

A widget-based layout generator, not an editor for arbitrary layout XML.

- A theme is a **project**: pages (the Default page, collection templates, an
  optional splash page) holding placed **widget instances**.
- A **widget** is a fragment of layout XML with `{{TOKEN}}` sites plus the
  variables its properties panel edits. There are 35 stock widgets (game
  title, logo, video, wheels, static image, ...).
- The layout XML is always **generated** from the project and never edited
  in place. Each page is written with a marker on line 1, which is what
  identifies an editor-made theme.

## What was ported

| One Saucier (`app/editor/`) | Here (`Source/ThemeEditor/`) | Notes |
|---|---|---|
| `theme_project.{h,cpp}` | `ThemeProject.{h,cpp}` | Model and `project.xml`. Parsed with **rapidxml** instead of copying MiniXml (see below). |
| `widget_defs.{h,cpp}` | `WidgetDefs.{h,cpp}` | Same `.widget` format. The stock files are embedded by CMake. |
| `theme_generate.{h,cpp}` | `ThemeGenerate.{h,cpp}` | New marker, `{{THEMEDIR}}` and `{{PAGEROOT}}` sites, translation of One Saucier dialect. |
| `editor_canvas.h` | `EditorCanvas.h` | Unchanged apart from names. Click-through now skips canvas-hidden widgets, like a plain pick. |
| `editor_nav.h` | `EditorNav.h` | Unchanged apart from names. `dir_for_listing` dropped (it served One Saucier's own file browser). |
| `letter_tiles.{h,cpp}` | `LetterTiles.{h,cpp}` | ImGui's `imstb_truetype.h` and zlib instead of a stb copy and miniz. |
| `theme_export.{h,cpp}` | `ThemeExport.{h,cpp}` | The pure save plan: pages, font, licence, letter tiles, stale-page rules. |
| `project_store.{h,cpp}`, `fs_util.h` | `ThemeStore.{h,cpp}` | Rewritten for the project-in-theme storage, with `std::filesystem`. |
| – | `LayoutCheck.{h,cpp}` | New: checks layout XML against what this fork's PageBuilder reads. |
| `assets/widgets/*.widget` | `widgets/*.widget` | The 35 stock widgets, audited (see below). |

Left out, as planned: `asset_restore.h`, `project_rescue.h`,
`wallpaper_pool.h`, `artSource`, THEMES-collection art, sending to a
cabinet, the export version file and the "export.incomplete" marker, and
everything in One Saucier's `app/retrofe/theme/*`.

### Decisions made while porting

- **rapidxml, not MiniXml.** MiniXml is forgiving by design: it auto-closes
  at the end of input, so a truncated `project.xml` loaded with pages missing
  (One Saucier's `fs_util.h` documents the data loss that caused). rapidxml
  refuses it with a reason, and it's the parser PageBuilder uses, so the same
  parser checks the generated pages. No GPL file needed copying.
- **Widgets compiled in.** `CMake/EmbedWidgets.cmake` turns
  `Source/ThemeEditor/widgets/*.widget` into a C++ source at build time (no
  bash generator; MSBuild, Ninja and Make all run it). Carriage returns are
  dropped, so a CRLF checkout embeds the same bytes. Extra widgets can be laid
  over the stock ones from a folder (`readWidgetFolder` +
  `overlayWidgetFiles`); where that folder lives is a UI-round decision
  (global, per theme under `editor/widgets/`, or both).
  One Saucier's stamp/re-stage logic was dropped: it fixed a cabinet updater
  problem this fork doesn't have.
- **A static library.** `retrofe_theme_editor` is built once and linked into
  both `retrofe` and `retrofe_tests`, so the generated sources have one owner.
- **New marker.** Pages start with
  `<!-- RetroFE theme editor; project=<slug>; v=1 -->`. One Saucier's
  `<!-- one_saucier theme editor; ... -->` is still recognised as editor-made.
  A UTF-8 BOM in front of either is tolerated.
- **Letter tiles: zlib, not `IMG_SavePNG`.** The rasteriser is ImGui's
  `imstb_truetype.h` (a private static copy, like ImGui's own). PNGs are
  written with zlib, which both targets already link, rather than
  SDL2_image's `IMG_SavePNG`: that would pull SDL into the core and the test
  binary just to write a PNG. The encoder is 40 lines and the test inflates
  the result to check it.
- **Digit and default tiles.** This engine looks a `firstLetter` tile up by
  the title's first character exactly as it is (One Saucier folds digits into
  `#`; here the file-name match is case-sensitive on every platform), so the
  set is `#`, `A`–`Z`, `0`–`9` (each digit tile highlights the `#` row) and
  `default.png` with nothing highlighted, which the engine falls back to for
  any other character: 38 files. **Limitation:** a title starting with a
  lowercase letter shows `default.png`. Lowercase copies (`a.png`) can't be
  written: on Windows they'd be the same file as `A.png`.
- **Unknown attributes kept.** Root and widget attributes this version
  doesn't know (One Saucier's `artSource`, intro and THEMES art, future
  fields) are kept as they were and written back, so a One Saucier project
  round-trips. Its deferred-asset manifest (`<asset>` entries) is dropped.
- **Second-screen pages** (`layout1.xml` in One Saucier) stay in the model
  and round-trip, but aren't saved; saving warns. This engine places a
  component on a monitor with `monitor=` instead, which a later round can
  generate.
- **Splash page kept.** It's cheap: the splash page saves as `splash.xml`,
  which this engine reads.
- **Names** follow the fork (PascalCase files, camelCase functions); the
  structure follows One Saucier so the two can be compared.

## The widget dialect

The stock widgets target RetroFE Plus. Each fragment was checked against
this fork's `Graphics/PageBuilder.cpp` and the components it builds. What
the engine does with things it doesn't support matters: **PageBuilder never
fails on them.** Unknown elements and attributes are silently ignored, and a
missing required attribute logs an error and drops the component.

| Feature | This engine | What the editor does |
|---|---|---|
| `transform=` (warped quads) | Not read | Removed from fragments; "Transform Video" isn't offered (`warpable` is off everywhere). A One Saucier project's warped widget is drawn as its corners' bounding box, with a warning. `{{?WARP}}` is always false, so One Saucier widget files still expand. |
| `mode="layout_preferred"` | Not a mode | Removed from defaults and option lists; the default is now **no mode** (the game's own media). A project that still has it gets no mode, with a warning. |
| `<scrollingText src=>` | Not built | `<reloadableScrollingText type="file" location=...>`. `location` is relative to the RetroFE folder, so it's written `layouts/{{THEMEDIR}}/<file>`. |
| `<reloadablePanningImage>` | Not built | Pan & Zoom removed from Game Art. |
| `hwdec` | Not read | Hardware Decode removed (Game Video, Live Background, Static Video). |
| `jukebox` on reloadableVideo | Read | Kept. |
| `numLoops`, `minShowTime` on reloadableVideo | Not read (they loop per the global setting) | Loop Video and Min Show Time removed from Game Video, System Video, Live Background. `<video numLoops>` is read, so Static Video keeps Loop. |
| `textFormat` on reloadableImage | Not read | Text Format removed from Game Art's text fallback. |
| `value` on `<statusText>` | Not read | Removed, with the Sample Text var. |
| `pauseTime` on scrolling text | Not read | Pause removed from Text File Scroller. |
| Text fallback in menus | Drawn only with a `fontSize` (`Text` scales by it; unset is -1) | Logo wheels write a `fontSize` from their slot pitch unless Text Size is set. |
| Menu `mode` | Only `layout`, `common`, `commonlayout` | Wheel and strip modes limited to those; default none (collection items already fall back to system art). The ghost wheel writes no mode unless it's one of those. |
| reloadableText types | Its own list | Game Metadata offers the types ReloadableText has (`numberButtons`, `numberJoyWays`, `playCount`, `lastPlayed`, `timeSpent`, `isFavorite`, ...). One Saucier's `fullTitle`, `publisher`, `subGenre`, `series`, `region`, `date`, `story` only resolve from an info file, so they're gone. Scrolling Text likewise (`story` stays: scrolling text reads it from the media folder). |
| `src=` on collection pages | Resolved against the page's own folder first | Theme files are written `src="{{PAGEROOT}}{{SRC}}"` (`../../../` on `collections/<c>/layout/layout.xml`), so they resolve first time instead of logging an error and falling back. Sounds too. |
| `id=` | Read as a number by Text; nothing uses it | The editor's string ids are kept (they read as 0). They're the joint between the project and the XML. |
| `layer=` | Page has `NUM_LAYERS = 20`: layers 0–19 are drawn; `Page::addComponent` drops (and leaks) a component on any other | `MAX_LAYER = 19` everywhere: widget definitions refuse 20, projects load 20+ as 19, the generator clamps every layer it writes, including offsets (the ghost wheel's selected slot at +6, the strip's popped and selected slots at +1). `LayoutCheck` flags a layer outside 0–19 on any element, menu item or layer animation. One Saucier allowed 0–20. |
| Second screen `layout1.xml` | Not read (monitors use `monitor=`) | Not saved yet (see above). |

### Per widget

| Widget | Result |
|---|---|
| game_genre, game_index, game_letters, game_manufacturer, game_players, game_story, game_title, game_year, game_favorite, playlist_name, static_text, system_device, system_logo, system_marquee, system_name, system_time | **Unchanged** (16): everything they write is read. |
| game_art | Transform Video, Pan & Zoom, Text Format and `layout_preferred` removed; default mode none. |
| game_bezel, game_cabinet, game_front, game_logo, game_marquee, game_screentitle | Presets of game_art: no longer default to `layout_preferred` (they inherit none); no Transform Video. |
| game_video | Loop Video, Min Show Time, Hardware Decode, Transform Video and `layout_preferred` removed; Jukebox kept. |
| system_video | Loop Video and Transform Video removed. |
| live_background | Min Show Time and Hardware Decode removed; it plays the theme's own `collections/_common` video (One Saucier fed it from its wallpaper pool). |
| static_image | Transform Video removed; `src` relative to the page. |
| static_video | Hardware Decode and Transform Video removed; `src` relative to the page. |
| text_scroller | Becomes `reloadableScrollingText type="file"` in the theme folder; Pause removed; default file `text/scroller.txt`. |
| splash_status | `value` and Sample Text removed. |
| game_meta, scroll_text | Text Type lists match this engine. |
| wheel_vertical, wheel_curved, art_strip | Menu modes limited to what menus read; default none. |

### How "it builds" is checked

PageBuilder needs SDL, a renderer, fonts and GStreamer, which the test binary
doesn't have, and it doesn't fail on what it ignores anyway. So the check
is split:

1. **`LayoutCheck`** (`checkLayoutXml`) parses with rapidxml and checks each
   element and attribute against the vocabulary PageBuilder reads, the
   required attributes it logs errors for, mode values, `<itemDefaults>` in
   menus (PageBuilder dereferences it), animations (`duration`, tween types,
   `to`, algorithm names), layers in 0–19, alpha in 0–1, and numbers where
   the engine reads numbers (`duration`, `layer`, `loadFontSize`). Saving
   runs it over every page and reports issues as warnings, which catches
   dialect problems in user-made widgets. A page that doesn't parse at all
   (rapidxml fails, or no `<layout>` root) is an **error**, from the
   generator itself: `raw` vars and verbatim ladders go in unescaped, so a
   bad value there can break the XML, and that must never be saved.
2. **A drift test** reads `Graphics/PageBuilder.cpp`,
   `Graphics/Animate/Tween.cpp` and `Graphics/Page.h` from the source tree
   and checks every name the check accepts is still mentioned there, and
   that `NUM_LAYERS` is still 20. It's a "still mentioned" check, not
   acceptance: it catches PageBuilder dropping a name, not whether it reads
   that name on that element (the element lists in `LayoutCheck.cpp` were
   written from reading `PageBuilder.cpp`). When PageBuilder learns an
   attribute, `LayoutCheck.cpp` needs the addition. A test binary run
   without the source tree (the path is compiled in) prints one "skipped"
   line instead.
3. **The key test**: every stock widget, with every bool flipped and every
   listed option, wheels with every shape knob (pulse, ghost, idle dim, fade,
   tilt, curve, text wheel), measured strips and verbatim ladders, and a page
   holding all 35, checks clean.
4. **The real engine**, once by hand (2026-10-23): with
   `RETROFE_THEME_SAMPLE_DIR=<root>/layouts`, `retrofe_tests` also saves an
   "Every Widget" theme there (all 35 widgets, wheel knobs on, a Main
   template and a splash page). Running RetroFE on that root built
   `splash.xml`, the Default `layout.xml` and the Main template with no
   Layout errors or warnings, and loaded the generated digit tile for
   "1942". A headless run isn't possible yet: SDL's dummy video driver fails
   because `SDL.cpp` always asks for an OpenGL window, so the run used a
   small window with `SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN=1` (no focus taken)
   and `videoEnable = no`. `retrofe --theme-check <layout>` (UI round 1)
   now does this in a hidden window and reports what PageBuilder logs.

## Storage

The project lives inside its theme (option (a) of the porting plan):

```
layouts/<Theme>/layout.xml                          Default page
layouts/<Theme>/collections/<c>/layout/layout.xml   collection templates (and each member)
layouts/<Theme>/splash.xml                          splash page, if any
layouts/<Theme>/editor/project.xml                  the project
layouts/<Theme>/assets/                             files imported for the theme
layouts/<Theme>/fonts/Roboto-Regular.ttf            bundled font, when the theme uses it
layouts/<Theme>/fonts/LICENSE-Apache-2.0.txt        its licence
layouts/<Theme>/collections/_common/medium_artwork/firstLetter/   letter tiles
```

An editable theme is just a layout folder: it can be copied, zipped for the
Catalog or deleted like any other, and RetroFE runs it without the editor.

**Deviation: `assets/` at the theme root, not `editor/assets/`.** Imported
files are what the pages show, so they belong to the theme: pages refer to
them as `assets/<name>` with no copy step, deleting `editor/` leaves a
working theme, and One Saucier projects (which store `assets/<name>` values)
load unchanged. `editor/` holds only what the editor needs.

### The guard

What a folder is (`probeTheme`):

| Folder | Verdict |
|---|---|
| Doesn't exist, empty, or holding only `editor/` and/or `assets/` folders (and no project) | fresh |
| `layout.xml` starts with the marker (either editor's) | editor-made |
| No `layout.xml` but a file `editor/project.xml` | editor-made (a save that stopped early) |
| Anything else | **foreign**: never written to, duplicated or deleted |

The fork's default Aeon Nox is foreign.

Inside an editor-made theme, a page somebody wrote by hand (a page position
without the marker, e.g. `collections/NES/layout/layout.xml`) is guarded
too: a save that would write there (NES gets a template, or joins one) is
refused with a message saying which file, before anything is written. The
user moves the file away or takes that collection out of the template.
(Considered: backing it up to `.bak` and carrying on. Refusing is clearer
and loses nothing.)

A theme folder can't be left looking foreign by the editor's own failures:
a first save that fails removes the files it wrote and the folders it made,
`importAsset` refuses a theme that has never been saved, and a folder
holding only `editor/` or `assets/` still counts as fresh. (One Saucier's
"export.incomplete" marker covered the same case.)

Folder names the editor writes, for themes and collection templates, must be
safe as written (`folderNameSafe`): no forbidden or control characters, no
leading or trailing space or dot (Windows would silently write somewhere
else), no device names (`CON`, `COM1.txt`, ...). A template for a collection
whose name isn't safe is a plan error.

### Operations (`ThemeStore.h`)

- `saveTheme`: refuses a foreign folder; builds the plan (nothing is written
  if it has an error); writes `project.xml` first, then every page, the font
  and the tiles; then removes pages a previous save wrote that this one
  doesn't (only marker-carrying page positions, and their emptied folders;
  media beside them stays; a stale path that is the same file as a page just
  written, as `collections/nes` and `collections/NES` are on a
  case-insensitive drive, is kept). Each file goes to `<file>.tmp` and is renamed
  over the old one; unchanged files aren't rewritten. The report lists what
  was written and removed, plan warnings, and files widgets point at that
  aren't in the theme.
- `createTheme`: trims the name, picks a free folder (`<name>`, `<name> 2`,
  ...; Windows-forbidden characters and device names made safe), saves an
  empty Default page at 1920x1080 or 1080x1920.
- `duplicateTheme`: editor-made only; copies the folder (links skipped) as
  "<name> Copy" and saves it there, which re-marks its pages and points them
  at the new folder.
- `deleteTheme`: editor-made only; links inside are removed, not followed.
- `importAsset`: copies a file into `assets/`, reusing an identical file and
  adding `-2`, `-3` for a different one with the same name. The theme must
  have been saved once.
- `listThemes`: every folder under `layouts/`, with the verdict and, for
  editor-made themes, the project's name, orientation and counts (a broken
  project lists with its error).

Pages refer to their folder through `{{THEMEDIR}}`, which saving sets to the
folder actually written (not the project name), so a theme saved as
"My Theme 2" points at itself.

Not done: `fsync` (std::filesystem has none; tmp + rename protects against a
failed write, not power loss), and a save-in-progress marker (`project.xml` is
written first, so an interrupted save is repaired by the next one).

## UI round 1: the Themes screens and the editor screen

### Where things are

| File | What |
|------|------|
| `Manager/Themes/ThemeProvider.{h,cpp}` | The `theme` provider: the list (`listThemes` plus facts read from each folder: size from the `<layout>` root, other main pages, splash, collection pages, whether the `layout` setting names it), the details, and the actions `theme.use`, `theme.edit`, `theme.create`, `theme.duplicate`, `theme.delete`. Built into the tests. |
| `Manager/Themes/ThemesForms.cpp` | Forms `themes`, `theme`, `theme-new`, and `theme-editor` (a placeholder: the editor draws that screen itself). |
| `ThemeEditor/EditorSession.{h,cpp}` | The editor's controller, SDL- and ImGui-free: open (editor-made only), pages, selection, the pointer protocol (press / move / release, click-through, handles), property setters with clamping, coalesced undo steps, undo/redo (whole-project snapshots, 100 deep), dirty tracking by revision (undoing back to the saved state is clean), the preview XML and save. |
| `Manager/ImGuiScreen.h` | The interface ManagerUI uses for a screen that draws itself (update / drawTargets / draw / escape / leaving / discard / retire / shutdown). |
| `Manager/Themes/app/ThemeEditorScreen.{h,cpp}` | The ImGui screen: three regions, the preview page and its texture, the Add widget, Saved and Unsaved changes dialogs, and the `RETROFE_EDITOR_SCRIPT` runner. |
| `Manager/Themes/app/ThemeCheck.cpp`, `ThemesApp.{h,cpp}` | `--theme-check`, the bundled font, the provider factory. |

Shared hooks, kept small: `ManagerUI` (an `ImGuiScreen` slot: shown instead
of a form for `theme-editor`, updated in `update()`, drawn in `render()`
whatever `formsRenderer` says, asked on leaving, retired and freed from
`update()`), `ManagerUI::setCollectionSource` set by `RetroFE.cpp` (the
preview loads collections the way RetroFE does), `Logger::setTap` (collects
what a page build logs), `SDL::setHiddenWindows` (for `--theme-check`; it
also leaves the mouse alone, as does `SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN`),
the `--theme-check` branch in `Main.cpp`, and the Themes slots in
`Providers.cpp`.

### How the preview works

- The page is built in `update()`, never in the render pass: after an edit
  the session's revision moves, and once it has been still for 0.25 s (and
  no drag is in progress) the page is regenerated with
  `GenOptions::canvasPreview` and the Show toggles. If the XML is the same as
  last time (a widget renamed), nothing is rebuilt.
- `PageBuilder(<folder>, "layout", config, fontCache)` with
  `setLayoutXml(xml, <page folder>)`. While the editor is open the `layout`
  setting points at the theme; `retire()` puts it back the moment the screen
  is left (before the next form is built), `shutdown()` frees the page and
  texture on the next `update()`.
- The collection: RetroFE's own `getCollection(name)` once per collection
  (the one being browsed, else `firstCollection` when on Home), and a copy of
  its first 60 items for each page built, because a page deletes its
  collections with it. A template page previews with its own collection.
- The page draws into a texture the size of the window (RetroFE scales pages
  to the window), inside `drawTargets()` under the mutex; the canvas shows it
  with the theme's own aspect (`canvasFit`), so a portrait theme on a
  landscape screen comes out the right shape.
- A page with no components isn't built: `Page::draw` logs an error every
  frame for one.
- What PageBuilder logs while building (warnings and errors) is shown under
  the canvas, as are the generator's warnings.

### Decisions made in this round

- **New theme's fields are the provider's**, `theme:newName` and
  `theme:newOrientation`, not `draft:` keys: providers can't read the model's
  drafts, and the provider needs them for `theme.create`.
- **Use this theme writes `layout` through `SettingsFile`** (to the file that
  sets it) and asks for a reload when the manager closes, like the other
  Manage edits. For an editor-made theme it's refused while the main page has
  no `<menu>`: RetroFE would start on a page with nothing to browse with.
- **Delete refuses the theme in use.**
- **Every save writes a `splash.xml`.** RetroFE shows a message box and stops
  when a layout has none (found in the GUI check: a new theme put in use
  wouldn't start). A project without a splash page gets one showing the
  theme's name; a hand-made `splash.xml` in the folder is left alone.
- **Logo wheels get a `fontSize`.** This engine's `Text` scales by
  `fontSize` and leaves it at -1 when unset, so a wheel's text fallback (an
  item without art) drew nothing, and the page never went idle, so it didn't
  take input either. The generator now writes a size from the slot pitch
  unless the wheel sets one (found in the GUI check too).
- **Templates are editable but not managed**: pages a theme already has can
  be selected and edited; adding, removing and reassigning templates is the
  next round.
- **Close asks** (Save / Don't save / Cancel); Escape and F10 use the usual
  leave-twice warning.
- **ImGui can't be driven by posted input**: posted keys reach it, but the
  SDL backend replaces the mouse position with the real cursor's whenever
  SDL thinks its window has focus, so posted clicks land wherever the user's
  pointer is. Hence `RETROFE_EDITOR_SCRIPT` (commands in theme units, run one
  per frame from `update()`, results in the log); the GUI check drives the
  form screens with posted keys and the editor with a script.

### Checked

`retrofe_tests` covers the provider on temporary roots (facts, flags, every
action and refusal, settings written, reload requested) and the session
(pointer drags, snapping, canvas clamps, minimum sizes, click-through, undo
and redo, coalescing, clamps, var types, the preview's filters, saving, the
hand-made splash guard, templates). `--theme-check` on Aeon Nox (hand-made)
reports nothing; on an editor theme without a splash page it reports that.
The GUI check (2026-10-23, private test root) went Themes list, Aeon Nox's
details, New theme, Edit, scripted editing, the Add widget, Saved and
Unsaved changes dialogs, the leaving warning, Duplicate, Delete with
confirm, Use this theme, and RetroFE running the new theme with its wheel.

### Next round

- Templates: add (`createNamed`), reassign, delete (`deleteTemplateTo`),
  preview cover with `effectiveCover`.
- Controller editing (`EditorNav.h` is ready) and an on-screen keyboard for
  the text fields.
- Guides and alignment snapping between widgets; multi-select.
- Ask for a file when placing Static Image / Static Video (their defaults
  don't exist in a new theme).
- The wheel's selected item highlight for text wheels; theme fonts and
  sounds in the theme panel.
- Hand-made themes (see below).

## Open question: hand-made themes

The editor only edits editor-made themes. CoinOPS users mostly have
hand-made layouts, including this fork's default (Aeon Nox), and those are
foreign: the editor won't touch them. Options:

1. **Editor-made only** (what's built): New Theme and Duplicate, nothing
   else. Simple and safe, but most installed themes can't be edited.
2. **Port One Saucier's importer** (`tools/import_theme.py`, 2,183 lines of
   Python) to C++: map a layout.xml onto the widget vocabulary, reporting
   what it can't map. Heuristic and lossy by design; about 2–3 rounds.
   Imported themes become editor-made copies, so the original is never
   touched. The verbatim-ladder fields and kept attributes exist for it.
3. **A layout-native mode**: a different, simpler editor that moves and
   resizes existing nodes (`x/y/width/height/layer/alpha`) directly in the
   layout DOM and keeps everything else as it was. No widget model, no
   loss, works on any theme; but no "add a widget" beyond raw elements, and
   the save path must preserve formatting and comments (rapidxml doesn't
   round-trip them), and the marker guard wouldn't apply (it would edit
   hand-made files, so it needs its own backup/undo story).
4. **Both, later**: option 1 now, option 3 for quick tweaks of any theme,
   and option 2 only if users want to turn a hand-made theme into an
   editable one.

Option 3 probably gives CoinOPS users the most per round; option 2 is the
most faithful to One Saucier. This needs the user's call.

## Other open items

- Second-screen pages via `monitor=` components.
- The Xcode project doesn't list the new files (it hasn't been updated for
  the fork's other additions either).
- `static_video`'s default `videos/background.mp4` and `static_image`'s
  `images/background.png` don't exist in a new theme; saving warns. The UI
  should ask for a file when placing them.

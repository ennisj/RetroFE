# Design: the theme editor

Status: the core is ported (rounds 1–3 of the porting plan: core port,
widget dialect audit, storage and saving), the first UI round is done (the
Themes screens and the editor screen, "UI round 1" below) and so is the
next (templates, files, theme properties, "Editor round 3" below), and
hand-made themes can be edited in place ("Layout-native mode" below,
decision M10 option 3). The core
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
   menus (without one PageBuilder uses empty defaults, so the items have no
   size; it used to crash), animations (`duration`, tween types,
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
| Anything else | **foreign**: the widget editor never writes to, duplicates or deletes it |

The fork's default Aeon Nox is foreign. Its pages can be edited in place by
the layout-native mode (below), which has its own backups and never touches
an editor-made page.

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
| `Manager/ImGuiScreen.h` | The interface ManagerUI uses for a screen that draws itself (update / drawTargets / draw / escape / askToLeave / leaving / discard / retire / shutdown). |
| `Manager/Themes/app/ThemeEditorScreen.{h,cpp}` | The ImGui screen: three regions, the preview page and its texture, the Add widget, Saved and Unsaved changes dialogs, and the `-editorScript` runner (parsing and the session commands are in `ThemeEditor/EditorScript.{h,cpp}`, tested). |
| `Manager/Themes/app/ThemeCheck.cpp`, `ThemesApp.{h,cpp}` | `--theme-check`, the bundled font, the provider factory. |

Shared hooks, kept small: `ManagerUI` (an `ImGuiScreen` slot: shown instead
of a form for `theme-editor`, updated in `update()`, drawn in `render()`
whatever `formsRenderer` says, asked on leaving, retired and freed from
`update()`), `ManagerUI::setCollectionSource` set by `RetroFE.cpp` (the
preview loads collections the way RetroFE does), `Logger::TapScope` (collects
the warnings and errors a page build logs on this thread, whatever the `log`
setting; lines it lets through only for the tap stay out of the log file),
`SDL::setHiddenWindows` (for `--theme-check`: hidden, never fullscreen, no
controllers; it also leaves the mouse alone at start and exit, as does
`SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN`),
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
- A page with no components isn't built: there's nothing to draw. (`Page::draw`
  used to log an error every frame for one, and drew nothing at all on a page
  whose only components were menus, such as a theme that is just a wheel;
  editor round 3 fixed that in `Page::draw`.)
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
- **Leaving asks** (Save / Don't save / Cancel), whether by Close, Escape or
  the Settings key (`ImGuiScreen::askToLeave`), instead of the forms'
  "leave again to discard": that warning stays armed until something resets
  it, and nothing does while an ImGui screen is shown, so a second Escape
  much later would have discarded the work. After answering, the editor goes
  back one screen (the Settings key then has to be pressed again).
- **Use this theme refuses a theme without `splash.xml`**, hand-made ones
  too: RetroFE stops at start-up on such a layout, and then can't be switched
  back from the UI.
- **Engine issues don't depend on the log level**: a `Logger::TapScope` sees
  warnings and errors on its own thread whatever `log` says, and lines from
  collection, database and high-score loading are left out
  (`Themes::pageBuildComponent`), as is anything from other threads (videos
  playing on the page behind).
- **ImGui's keyboard navigation is off while the editor is shown**: the arrow
  keys nudge the selection instead of also moving ImGui's focus. Tab still
  moves between fields. A pause of 0.6 s ends a run of nudges as one undo
  step. So the dialogs still answer from the keyboard: Unsaved changes takes
  Enter (Save), D (Don't save) and Escape (Cancel), Saved takes Enter, and
  Enter in Add widget's search adds the first match. (Posted key messages
  only reach ImGui when SDL thinks its window has keyboard focus, so the GUI
  check couldn't press D; Escape is handled by ManagerUI and was checked.)
- **ImGui can't be driven by posted input**: posted keys reach it, but the
  SDL backend replaces the mouse position with the real cursor's whenever
  SDL thinks its window has focus, so posted clicks land wherever the user's
  pointer is. Hence editor scripts: `retrofe -editorScript <file>` (any
  path), commands in theme units, run one per frame from `update()` the first
  time a theme is opened in that session only, with a banner while running
  and results in the log. (It started as an environment variable, which
  stayed live in release builds and re-ran save/use every time a theme was
  opened.) The GUI check drives the form screens with posted keys and the
  editor with a script.

### Known costs and limits

- **The page behind still reads `layout`.** While the editor overrides the
  `layout` setting, the page RetroFE was showing (and anything else that
  reads `OPTION_LAYOUT`, such as its reloadable art when its item changes)
  sees the edited theme's name. Nothing behind the editor changes item while
  it's open, so it doesn't show, but the proper fix is to pass the layout
  name to PageBuilder and the components explicitly instead of through the
  configuration.
- **Every committed change rebuilds the whole preview page** (after the
  0.25 s pause): a drag, a nudge run, a property edit. Building loads the
  page's textures and starts its videos, so a heavy page takes a noticeable
  moment. Updating the changed component in place would be faster, but a
  widget's box maps onto different attributes per widget (see
  `EditorCanvas.h`), so it stays a rebuild for now.
- **The first preview loads its collection in full** (`getCollection`,
  metadata injected), which stalls the first build on a big collection;
  only 60 items are used. Capping the list before `injectMetadata` would
  need a hook in `CollectionInfoBuilder`.

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

### Next round (as left by round 1)

- ~~Templates: add, reassign, delete, coverage~~ (round 3).
- Controller editing (`EditorNav.h` is ready) and an on-screen keyboard for
  the text fields.
- Guides and alignment snapping between widgets; multi-select.
- ~~Ask for a file when placing Static Image / Static Video~~ (round 3).
- ~~The selected item highlight for text wheels; theme fonts and sounds~~
  (round 3).
- ~~Hand-made themes~~ (layout-native mode, below).

## Editor round 3: templates, files, theme properties

### What was built

| Where | What |
|---|---|
| `EditorSession` | Templates: `newTemplate` (blank, from the Default page or a copy of a template, optionally with more collections), `assignTemplate` (one or many, one undo step), `removesTemplate` (what a move would delete), `deleteTemplate` (with a replacement), `renameTemplate`, `templateProblem`, `handMadePage`. Theme properties: `setThemeFont`, `setSound`, the splash page (`makeSplashPage`, `setSplashText`, `setSplashBackground`). Files: `missingFiles` (the selected widget's file settings whose file isn't in the theme), `themeHasFile`. Every one is an undo step; page changes keep the splash page last and keep editing the page that was being edited (found again by a collection it covered). |
| `ThemeProject` | `pageShownFor` (which page this engine shows for a collection) and `MENUS_TEMPLATE` (`_menu`). |
| `ThemeStore` | `importFile` (`importAsset` into any folder of the theme; a file already inside the theme is used where it is), `themeFiles` (a folder's files by extension), `handMadeCollectionPage`. |
| `ThemeExport` | `defaultSplashPage`, shared by the save plan's fallback and the editor's "make a splash page". |
| `ThemeGenerate` | Wheels: the selected slot gets `fontSize` × 1.3; a text wheel's other slots rest at alpha 0.60. |
| `EditorScript` | `tplnew`, `tplassign`, `tpldelete`, `tplrename`, `font`, `sound`, `splashtext`, `splashbg`. |
| `ThemeEditorScreen` | The theme panel's *Font and sounds*, *Splash*, *Pages* (new / copy / rename / delete, the template's collections) and *Collections* (the menu tree from `Services::library`, each collection's page); the template and delete dialogs; the ImGui file chooser; asking for a file after placing a widget; the screen-only script commands (`import*`, `browse`, `browsedir`, `choose`, `tpldialog`, `tpldeletedialog`, `closedialogs`, `open`). |
| `Graphics/MenuXml.h`, `PageBuilder.cpp`, `Page.cpp` | The engine fixes below. |
| `ManagerUI.cpp` | One line: the editor's context gets `library_`. |

### Engine fixes (shared code)

- **A `<menu>` without `<itemDefaults>` crashed RetroFE** (access violation):
  `PageBuilder::buildMenu` passed the missing node to `addFont`, which
  dereferences it, and `buildVerticalMenu` reads attributes from it.
  `MenuXml::itemDefaults` now gives such a menu an empty `<itemDefaults/>`
  (allocated in the document, so it lives as long as the parse) and
  PageBuilder logs a warning, as if the layout had written one. Options
  considered: skip the menu with a warning (a hand-made page then has no
  wheel at all, so RetroFE has nothing to browse with), null checks at every
  use (several sites, easy to miss one). Empty defaults keep the menu and
  touch one line of `buildMenu`.
- **A vertical menu whose items have no height looped forever**, adding slots
  until memory ran out (each slot is `-1` high, so the running height never
  reaches the menu's). `MenuXml::verticalMenuFull` also ends the menu when a
  slot adds no height, but only once every slot from there on takes
  `<itemDefaults>` (past the last `<item index=>`), so an overridden slot of
  height 0 or with negative spacing is left alone; 1024 slots is the
  backstop. PageBuilder warns when the menu stops short of its height.
- **A custom menu with no `<item>`s** reaches `ScrollingList::setPoints`
  with an empty list. Checked in RetroFE (a `ZeroItems` layout as the Home
  page): it builds, scrolls in all four directions and selects without
  crashing, and draws nothing. No guard was needed; PageBuilder and
  `LayoutCheck` now say the menu shows nothing.
- **A page whose only components are menus drew nothing** and logged "Layer
  index out of bounds" every frame: `Page::LayerComponents_` is sized when
  the first non-menu component is added, and `Page::draw` drew menus inside
  that per-layer loop. `Page::draw` now sizes it first. Found in the GUI
  check (a template page holding a wheel and an image whose file was
  missing); by the code, a page that is just a wheel does the same in
  RetroFE itself.
- Tested: `MenuXml` is header-only and SDL-free, so `retrofe_tests` checks
  both helpers on parsed XML; the drift test reads `MenuXml.h` with
  `PageBuilder.cpp`. `retrofe --theme-check` built a hand-made layout with
  both kinds of broken menu (`NoDefaults` in the test root) without crashing
  or hanging, and reported the warnings. `Page::draw` needs SDL, so it was
  checked in the GUI only.
- `layoutBuildBlocker` (the editor and `--theme-check` refused to build such
  pages) is relaxed to "it doesn't parse"; `LayoutCheck` still reports a menu
  without `<itemDefaults>`, now as "its items have no size".

### Decisions

- **Coverage is shown as this engine resolves it, not inherited down the
  menu tree.** One Saucier's engine keeps the current page when a
  collection has no override, so its editor shows a parent's template as
  inherited by everything below. This fork's RetroFE builds a new page on
  every collection change (`RetroFE.cpp`, the NEXT_PAGE state) from
  `LayoutPages::collectionFolder`: the collection's own folder, else
  `collections/_menu/layout` for a menu of collections, else the theme's
  `layout.xml`. Showing inheritance would promise pages RetroFE doesn't show.
  Instead: the Collections list shows each collection's real page
  (`pageShownFor`), the template dialog's *Also the N collections under it*
  puts a whole branch on a template explicitly (One Saucier's cascade), and
  `_menu` is offered as a template for every menu. Considered: emulating
  inheritance by writing a page for every descendant at save time (many
  files, and pages appearing in folders the user never chose).
- **Templates keep One Saucier's model** (a primary collection plus members;
  a template can't exist without a collection). Moving a template's only
  collection off it deletes it, as in One Saucier; the dialog says so and
  Undo brings it back. Considered: refusing that move (then the only way to
  reuse a collection is delete-then-create).
- **A collection with a hand-made page can't get a template**: the dialog
  and "Add a collection" refuse it with the reason, instead of letting save
  fail later (save still refuses too). Which collections have one is read
  when the menu tree is (re)read and after each save, not per frame.
- **Collection names that differ only in case** (`nes` while `NES` is on a
  template) are refused: on Windows they're one folder. Template names are
  unique ignoring case, renames included.
- **Fonts are checked before they're copied**: `.ttf`/`.otf` with a
  TrueType or OpenType header. A file over 16 MB is announced in the status
  line one frame before its (blocking) copy.
- **Files: an ImGui chooser inside the RetroFE folder first, the system
  dialog as "Somewhere else..."**. It works the same on every platform,
  never leaves the editor, and the files a theme uses usually come from the
  RetroFE folder (artwork, other layouts). The system dialog blocks, so it
  runs from `update()` (`Job::NativePick`), never while drawing. Directory
  listings are read once per folder, not per frame.
- **Fonts go into `fonts/`, everything else into `assets/`**; a file already
  inside the theme folder isn't copied again.
- **Asking for a file after placing** uses the same chooser, with *Not now*:
  the widget stays with its placeholder and saving warns, so placing never
  fails. It asks for the first missing file only.
- **Splash: text and background, made of widgets**: a splash page gets a
  Static Text (the theme's name) and, for a background, a full-page Static
  Image or Static Video (a video plays once, so the splash ends). No new
  project fields, so the page stays editable as a page.
- **Text wheel highlight**: the selected name 1.3× larger (the size tweens
  between slots), the others at 60% opacity, *Selected Text Color* if set.
  No default colour: the theme's own colour stays the user's choice.
- **The left column is wider** (340 instead of 270) for the new sections.

### Checked

`retrofe_tests`: templates (new, blank, copy, assign, members, primary moving,
deleting by moving, undo, rename, delete with replacement, the splash page
staying last, hand-made refusals, scripts), `pageShownFor` (own, member,
`_menu` for menus only, Default), saving writes member pages, font import
into `fonts/` and listing, in-theme files not copied, sounds in the page,
the splash page (text, video then image background, removal, undo), missing
files after placing, `MenuXml`. The GUI check (2026-10-23, private test root
`E:\Storage\Agents\editor3`, scripted): text wheel, the file chooser opened
by placing a Static Image (theme folder, then `media/backgrounds`), import,
font and sound import, splash text and background, two templates (one a copy
of Default joined by NES and SNES, one blank), the Collections list with
each page, the template and delete dialogs, save, the splash page preview,
Use this theme; then RetroFE running the theme: the Home and NES text wheels
with the selected name highlighted, Main on the Default page, Consoles and
NES on the Console Look template. `--theme-check` on the result: clean.

### Left for later

- Controller editing and an on-screen keyboard; guides, alignment snapping,
  multi-select (as before).
- Previewing a page as one of its member collections from the Pages list
  (the Collections list does it).
- A template's own font or sounds (they're the theme's).
- Second-screen pages; ~~hand-made themes~~ (layout-native mode, below).

## Hand-made themes: the options (decided: M10, option 3)

Before the layout-native mode the editor only edited editor-made themes. CoinOPS users mostly have
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
most faithful to One Saucier. Option 3 was approved (M10) and is built:

## Layout-native mode

A second editor for hand-made themes: the elements of one page file, edited
in place. User-facing: [THEME_EDITOR.md](../THEME_EDITOR.md#hand-made-themes-the-layout-editor).

### What was built

| Where | What |
|---|---|
| `ThemeEditor/LayoutText.{h,cpp}` | The page as text. A scanner (not a DOM) that finds every element and attribute and remembers its byte offsets: start tag, name end, attributes (whitespace before, name, value span, quote), end. Element paths (`image[3]`, `menu[0]/item[2]`, the root is `""`). Edits return new text with only those bytes changed: `withAttribute` (replace a value in its own quotes, or add one after the last attribute with the same spacing and quote), `withoutAttribute`, `withoutElement` and `withDuplicate` (whole lines when the element is alone on its lines). Entities decoded for display, encoded on write. Pure. |
| `Graphics/LayoutNodePath.h` | The same path from a rapidxml node, header-only and SDL-free. |
| `Graphics/PageBuilder.{h,cpp}` | The hook: `setTrace(std::vector<TraceEntry>*)`. When set, every component built records `{component, slot, path}` in build order; a menu's slots follow it with `slot` = the slot's `ViewInfo` (custom menus: each `<item>`; vertical menus: the node each slot was built from). One inline null check per component when unused. |
| `ThemeEditor/LayoutSession.{h,cpp}` | The controller, SDL- and ImGui-free: the theme's pages (`themePages`), opening one (refusing paths outside the theme, editor-marked pages and pages rapidxml can't read), the element list, geometry by path (fed by the screen), picking and click-through by layer then build order, the pointer protocol (`EditorCanvas.h`'s moves and resizes), turning a box back into attribute values, attribute edits, duplicate/delete, undo/redo (text snapshots, 100 deep, coalesced typing), dirty = text differs from what's on disk, save and put back the original. |
| `ThemeEditor/EditorScript.{h,cpp}` | `runLayoutCommand`: the session's script commands. |
| `Manager/Themes/app/LayoutEditorScreen.{h,cpp}` | The ImGui screen (form `theme-layout-editor`): pages and elements, the canvas (preview + boxes + handles), the attribute panel, Save / Put back the original / Unsaved changes / Delete dialogs, the screen's script commands. |
| `Manager/Themes/ThemeProvider`, `ThemesForms.cpp` | `theme:canOpen`; Edit opens the layout editor for a hand-made theme; the new note. |
| `ThemeEditorScreen.{h,cpp}` | `Themes::makeEditorScreen` (which editor for which form) and `Themes::takeEditorScript` (one script per session, whichever editor opens first). |
| `ManagerUI.cpp` | Two lines: the layout editor's form is an ImGui screen too, built by `makeEditorScreen`. |

### How the boxes are found

The preview is built as in the widget editor (`setLayoutXml` with the text
being edited, the page's own folder for relative paths, `layout` pointed at
the theme until the screen is left), plus `setTrace`. Right after
`buildPage`, before anything animates, each traced component's
`baseViewInfo` (or the slot's `ViewInfo`) is copied: resolved x and y
("center" is already 960 there), offsets, origins (fractions) and
`ScaledWidth/Height`. The box is `x + xOffset - xOrigin * w` across, the same
down. Sizes the engine doesn't know until it draws are guessed and marked
(text without a width: its font size and value length; art not loaded yet:
the layout's width/height/max size), and art that loads later (reloadable
images and videos) gets its real size from the live component's
`ImageWidth/Height` while the page runs. A custom menu without a size of
its own gets the box around its items, to select it by. A vertical menu's
slots are drawn but not dragged (they're generated from its settings).

### Decisions made in this round

- **Edit the text, never re-serialise.** rapidxml drops comments and
  formatting; a DOM writer that kept them would have to be written and kept
  exactly compatible. Editing byte ranges in the original keeps everything
  else identical by construction, and the tests compare bytes. The scanner
  follows rapidxml's `parse<0>` where they differ from strict XML: an end
  tag closes the innermost element whatever its name (shipped layouts have
  `<onMenuScroll>...</onMenuJumpScroll>`), and attributes needn't be
  separated by spaces. Considered: pugixml (another dependency, and
  formatting still isn't preserved exactly), rapidxml's printer (loses
  comments and entities).
- **Paths, not offsets, join components to text.** `name[index among
  same-named siblings]` from the root is stable across attribute edits and
  computed the same way from rapidxml and from the scanner (a test checks
  every element of every shipped page). Byte offsets would shift with every
  edit; a document-order index would shift for unrelated element kinds.
  After a duplicate or delete the paths after it shift, so the boxes are
  dropped until the next build (`structure()` moves).
- **Dragging writes numbers, only where the box changed.** A move writes
  the axes it moved (so a straight-down drag keeps `x="center"`; an axis the
  pointer moved less than 3 pixels along isn't snapped either); a resize
  writes the sizes that changed and the x / y that keep the undragged edges
  in place. Elements positioned by `xOffset` with no `x` of their own (menu
  items under `<itemDefaults>`) move by `xOffset`. Typing in the panel keeps
  any value as typed ("center", "50%", "stretch"). Considered: writing
  `center`/percentages back when the result is centred (guessing what the
  author meant), and always writing x, y, width and height (loses `stretch`
  on an untouched axis).
- **Backups: Library's, not the theme's.** Originals go to
  `<root>/manage/backups/layouts/<theme>/<page>` through `Library::Backups`,
  the same store and rules as every Manage screen: the original is taken
  before the first change only, and putting it back keeps what was there as
  a numbered copy. The theme folder stays clean (it can be zipped for the
  Catalog without an `editor/original/` in it). Considered: a copy inside
  the theme (travels with it, but ships with it too).
- **Writes stay in the theme.** The page path must be relative with no
  `..`, and the file must resolve (`weakly_canonical`, so links count)
  inside `layouts/<theme>`; save checks again. A page that changed on disk
  since it was read, or went away, isn't written.
- **Editor-made pages aren't opened here.** The widget editor regenerates
  them from its project; edits here would be lost on its next save.
- **Undo is text snapshots.** Pages are tens of KB; a snapshot per step (100
  deep) is simpler and exact. Putting back the original clears the history
  (it's a file operation, confirmed first).
- **Delete asks** (the canvas's Delete key too), though Undo brings it back:
  it removes the element's whole lines, animations and all.
- **Duplicate and delete were in scope** (whole-line copies and removals keep
  the rest of the file); adding new elements is the next step.
- **Animations are listed, not edited**, with a note when they change the
  position or size (Aeon Nox's top bar animates `y` from 0 to 50, so its
  box as the layout says it sits above the page).
- **One script per session**, shared by both editors (`takeEditorScript`).

### Checked

`retrofe_tests` (`Tests/LayoutEditTests.cpp`): the scanner on a page with a
BOM, a declaration, CRLF, a comment holding a tag, single and double quotes,
entities, self-closing tags, attributes one per line and a custom menu;
broken pages refused with their line; paths equal to rapidxml +
`LayoutNodePath` on that page and on every page shipped in
`Package/Environment/Common/layouts`; edits change only the edited bytes
(values, new attributes in self-closing and open tags and on their own line,
removal, entities and the other quote); whole-line delete and duplicate;
opening and refusing (outside the theme, absolute paths, `..` folders,
unparseable and editor-made pages); drags that keep `center`, numbers after a
sideways drag, a resize with a centred origin, an item moving by its
`xOffset`, nudges, layer clamping, the layout check unchanged; typing as one
undo step, undo back to the exact original, redo, duplicate/delete and
their undo; saving keeps the original first and only once, refuses a file
changed or removed elsewhere, and putting back the original restores the
bytes; script commands. The Themes provider tests cover `canOpen` and Edit
opening the layout editor.

GUI check (2026-10-23, private root `E:\Storage\Agents\native`, scripted):
Aeon Nox's details with the note and Edit, the layout editor on `layout.xml`
(boxes over the built page), moving and resizing the information wall,
moving the wheel's selected item, attribute edits with undo/redo, duplicate
and delete, save, the Put back the original dialog, `_menu`'s page and a
`forms` page. The saved `layout.xml` differed from the original in exactly
the edited values (a git word diff), `retrofe --theme-check "Aeon Nox"`
passed on it, closing the manager reloaded RetroFE, and Put back the original
restored the file byte for byte.

### Left for later

- Adding elements (an "add image / text / video" with the raw attributes,
  or the widget library's fragments inserted as text).
- Editing animations (a list of events, sets and tweens).
- Showing animated positions (the box where an animation ends).
- Editing several pages at once (`layout.xml` and `layout 4x3.xml` usually
  move together).
- Positions that keep a centred element on a half unit (an odd width with
  `xOrigin="center"` is rounded to whole units).

## Other open items

- Second-screen pages via `monitor=` components.
- The Xcode project doesn't list the new files (it hasn't been updated for
  the fork's other additions either).
- `static_video`'s default `videos/background.mp4` and `static_image`'s
  `images/background.png` don't exist in a new theme; the editor asks for a
  file when they're placed (round 3), and saving warns if it's still missing.

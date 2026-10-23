# The theme editor

The theme editor builds RetroFE themes (layouts) out of ready-made
**widgets**: the game wheel, the game's title, year and maker, box art,
videos, logos, fixed images and text. You place them on a canvas, size them,
set their options and save; the editor writes the layout files for you. It
was ported from One Saucier's editor.

It's drawn with the mouse in mind and runs inside Manage.

## What it edits

Only themes made with it. A theme the editor made has its project inside it
(`layouts/<Theme>/editor/project.xml`) and a marker on the first line of each
page it wrote. The editor never writes to, duplicates or deletes anything
else, so hand-made themes, Aeon Nox included, are safe: the Themes screen
says why it can't open them.

An editor-made theme is an ordinary layout folder. RetroFE runs it without
the editor, and it can be copied, zipped for the Catalog or deleted like any
other:

```
layouts/<Theme>/layout.xml                          the main page
layouts/<Theme>/splash.xml                          the splash page
layouts/<Theme>/collections/<c>/layout/layout.xml   pages for particular collections
layouts/<Theme>/editor/project.xml                  the editor's project
layouts/<Theme>/assets/                             files you chose for the theme
layouts/<Theme>/fonts/Roboto-Regular.ttf            the bundled font and its licence
```

## Starting

1. **Manage > Themes > New theme**: name it and choose landscape
   (1920 x 1080) or portrait (1080 x 1920). Create opens its details.
2. **Edit** opens the editor. Back (Escape) or Close returns to the details.
3. When it looks right, **Save**, then **Use this theme**. RetroFE switches
   to it when you close the manager.

A theme needs a wheel before it can be used: without one there's nothing to
browse with. Save always writes a splash page too (the theme's name), since
RetroFE won't start on a theme without one; Use this theme refuses any theme,
hand-made ones included, that has no `splash.xml`.

## The screen

- **Left: the theme.** Its name, orientation (switching reflows every
  widget), its pages and the widgets on the page you're editing. Click a
  widget in the list to select it.
- **Middle: the canvas.** The page as RetroFE itself builds it, with the
  collection chosen in *Preview with* (the one you were browsing, or Browse's
  first collection), and an outline of every widget on top.
  - Click a widget to select it; click it again to pick the one underneath.
  - Drag to move; drag one of the eight handles to resize. Moves snap to a
    5-unit grid (*Snap to grid* turns that off). The page is rebuilt when you
    let go.
  - Arrow keys nudge the selection by 1 (Shift: 10); Delete removes it.
  - *Show* turns art and video, text, and animations off in the preview only.
- **Right: the selected widget.** Name, position and size, layer (0 is
  drawn first, 19 on top), opacity, text colour where it has one, and the
  widget's own settings: switches, choices, numbers, colours, text and files.
  *Choose...* copies a file into the theme's `assets/` folder. *Hide in the
  editor* only hides it here, to reach what's underneath.
- **Top: Add widget, Undo, Redo, Save, Use this theme, Close.** Ctrl+Z,
  Ctrl+Y and Ctrl+S work too. Under the canvas the editor says what the
  generator warned about and anything RetroFE logged building the page.

Save shows what it did when there's something to know: warnings, and **pages
it removed** (a page an earlier save wrote that the theme no longer has).
Leaving with unsaved changes (Close, Escape or the Settings key) asks first:
Save (Enter) / Don't save (D) / Cancel (Escape). In Add widget, type to
search and press Enter to add the first match.

## Limits in this version

- Pages for particular collections (templates) can be edited if the theme has
  them, but not added, removed or reassigned yet.
- No controller driving: the editor needs a mouse and keyboard.
- Hand-made themes can't be opened (see above).
- Second-screen pages (One Saucier's `layout1.xml`) are kept in the project
  but not saved.
- Wheels show the item's name when it has no art; the selected name isn't
  highlighted yet.

## Checking a theme

```
retrofe --theme-check "<layout>" [report.txt]
```

checks a theme against this RetroFE and exits: 0 when nothing's wrong, 1 when
it found problems, 2 when it couldn't check. For an editor-made theme it
regenerates every page from the project and compares it with what's saved;
for a hand-made one it reads the page files as they are. Every page is checked
against what this RetroFE's page builder reads, then built by it in a hidden
window, and anything it logs is reported. It also checks the theme has
`layout.xml` and `splash.xml` (and, for an editor-made theme, a wheel). The
report goes to the console and, if given, the report file.

## For testing: scripted editing

```
retrofe -editorScript <file>
```

makes the editor run commands from a file, one per frame, the **first** time
a theme is opened in that session only (not again after a reload or when
another theme is opened). A banner says a script is running while it does,
and the results go to `log.txt` (`[ThemeEditor] script:`). It exists so the
editor can be checked without a mouse. Coordinates and distances are theme
units; blank lines and lines starting with `#` are skipped.

| Command | Does |
|---------|------|
| `wait N` | waits N frames |
| `page N`, `select N`, `selectid ID` | chooses a page or widget |
| `add DEFID` | places a widget (e.g. `wheel_vertical`, `game_title`) |
| `click X Y` | clicks the canvas (click-through included) |
| `move DX DY`, `resize nw\|n\|ne\|w\|e\|sw\|s\|se DX DY`, `nudge DX DY` | drags the selection |
| `name TEXT`, `layer N`, `alpha N`, `colour RRGGBB`, `var TOKEN VALUE` | sets properties |
| `delete`, `undo`, `redo` | |
| `show media\|text\|anims on\|off`, `preview COLLECTION`, `rebuild` | preview options |
| `save`, `use`, `report`, `addDialog` | |
| `close` (asks if unsaved), `discard` (leaves without saving) | |
| `log TEXT` | writes to the log |

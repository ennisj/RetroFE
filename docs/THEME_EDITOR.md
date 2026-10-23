# The theme editor

The theme editor builds RetroFE themes (layouts) out of ready-made
**widgets**: the game wheel, the game's title, year and maker, box art,
videos, logos, fixed images and text. You place them on a canvas, size them,
set their options and save; the editor writes the layout files for you. It
was ported from One Saucier's editor.

It's drawn with the mouse in mind and runs inside Manage.

## What it edits

Two kinds of theme, in two ways:

- **Themes made with it**, as widgets (most of this page). Such a theme has
  its project inside it (`layouts/<Theme>/editor/project.xml`) and a marker on
  the first line of each page it wrote.
- **Hand-made themes**, Aeon Nox included, in the **layout editor**: it
  changes the elements a page already has, in the page file itself (see
  [Hand-made themes: the layout editor](#hand-made-themes-the-layout-editor)).
  It never duplicates or deletes a hand-made theme.

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
  widget), *Font and sounds*, *Splash*, its pages, *Collections* and the
  widgets on the page you're editing. Click a widget in the list to select
  it.
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
  *Choose...* opens the file chooser (below). *Hide in the editor* only hides
  it here, to reach what's underneath.
- **Top: Add widget, Undo, Redo, Save, Use this theme, Close.** Ctrl+Z,
  Ctrl+Y and Ctrl+S work too. Under the canvas the editor says what the
  generator warned about and anything RetroFE logged building the page.

Save shows what it did when there's something to know: warnings, and **pages
it removed** (a page an earlier save wrote that the theme no longer has).
Leaving with unsaved changes (Close, Escape or the Settings key) asks first:
Save (Enter) / Don't save (D) / Cancel (Escape). In Add widget, type to
search and press Enter to add the first match.

## Files

Placing a widget that shows a file of the theme's own (Static Image, Static
Video, Text File Scroller) asks for that file straight away; *Not now* keeps
the placeholder name, and saving warns that it isn't in the theme. The
chooser lists the matching files inside the RetroFE folder (*This theme* and
*RetroFE folder* jump there); *Somewhere else...* opens the system's file
dialog instead. The file you choose is copied into the theme: fonts into
`fonts/`, everything else into `assets/` (a file with the same name but
different content gets `-2`, `-3`). A file that's already in the theme folder
is used where it is.

## Font, sounds and the splash page

- **Font and sounds**: the theme's font (the bundled Roboto, or any font in
  the theme's `fonts/`; *Import...* copies one in) and the sounds RetroFE
  plays with it: Start, Quit, Highlight (scrolling) and Select. *x* takes one
  off; the file stays in the folder.
- **Splash**: what RetroFE shows while it starts. Without a splash page the
  theme's name is shown. Typing a text or choosing a background (an image, or
  a video that plays once) makes a splash page; *Edit the splash page* opens
  it like any other page.

## Collection templates

A template is a page of its own for one or more collections; every other
collection uses the Default page. In this RetroFE a collection never takes
its menu's page: entering a collection always shows its own template, else
(for a menu of collections) the `_menu` template if the theme has one, else
the Default page.

- **New template...** (under *Pages*): choose the collection, a name and what
  it starts as (the Default page, an empty page, or a copy of a template).
  **Copy...** does the same starting from the template you're editing.
- The template being edited shows its name (edit it there), the collections
  using it (*x* sends one back to the Default page) and *Add a
  collection...*. **Delete...** asks which page its collections use instead.
- **Collections** lists the menu tree with the page each collection gets.
  Clicking one previews it on that page; double-clicking it, or *Change its
  template...*, chooses its page: the Default page, a template, or a new one.
  For a menu, *Also the N collections under it* puts the whole branch on the
  same page.
- `_menu` (in the collection list of *New template...*) makes the page every
  menu of collections without one of its own uses.
- Saving writes `collections/<c>/layout/layout.xml` for each collection a
  template covers and removes the ones an earlier save wrote that are no
  longer covered. A page somebody wrote by hand there is never overwritten:
  such a collection can't be given a template (the editor says why).
- Moving a template's only collection off it deletes the template (the
  dialog says so first; Undo brings it back).

## Wheels

A wheel with a *Text Size* is a text wheel: the selected name is drawn 1.3
times larger, in *Selected Text Color* if set, and the other names rest at
60% opacity. Logo wheels show the item's name when it has no art; there the
selected name is larger too.

## Hand-made themes: the layout editor

**Manage > Themes > (a hand-made theme) > Edit** opens its pages in the
layout editor. It moves, resizes and changes the attributes of the elements
a page already has, and can copy (Duplicate) or delete one; it can't add new
elements yet. Everything you don't change stays exactly as it was: comments,
spacing, the order of attributes, quotes, line endings, animations.

- **Left: Pages and Elements.** Every page of the theme: `layout.xml` and its
  variants (`layout 4x3.xml`, `layout - 1.xml`), `splash.xml`, the
  collections' pages (`collections/<c>/layout/*.xml`, `_menu` included) and
  the Manage screens' pages (`forms/**`). Under it, the page's elements in
  file order: the page itself (`<layout>`: its size, font and colour), then
  each element, with a menu's `<itemDefaults>` and `<item>`s under it.
  Elements greyed out draw nothing on the page (sounds, for instance).
- **Middle: the canvas.** The page as RetroFE builds it, from the text being
  edited, with the collection in *Preview with* (a collection's own page
  previews with that collection), and every element's box over it (*All
  boxes* off shows only the selected one and the one under the mouse).
  Click to select, click again to pick the one underneath; drag to move;
  drag a handle to resize. Moves snap to a 5-unit grid. Arrow keys nudge
  (Shift: 10), Delete deletes (it asks), Ctrl+Z / Ctrl+Y / Ctrl+S.
- **Right: the selected element.** *Position, size and look*: `x`, `y`,
  `width`, `height`, `xOrigin`, `yOrigin`, `xOffset`, `yOffset`, `alpha`,
  `layer` (0 is drawn first, 19 on top), `font`, `fontSize`, `fontColor` and
  `src`, as the text they are, with what RetroFE makes of the position and
  size beside them (`x = center` shows `= 960`). A menu item shows what it
  takes from its `<itemDefaults>`. *All attributes* lists every attribute,
  each editable as text and removable (*x*), and adds new ones. Its
  animations (`<onEnter>` and the rest) are listed, not changed.

What a drag writes: **numbers**. Dragging an element sideways turns
`x="center"` into the number it stood for plus the move; dragging it
straight up or down leaves `x` alone. Resizing writes `width` and/or
`height` (only the ones that changed) and moves `x` / `y` so the edges you
didn't drag stay put, whatever the origin. A menu item whose position comes
from its `<itemDefaults>` (it sets `xOffset` but not `x`, like Aeon Nox's
wheel) moves by its `xOffset` / `yOffset` instead, so it keeps following the
defaults. An element whose animations change its position or size says so:
on screen it moves from where it's placed here. A vertical menu's slots are
made from its settings, so they're shown but not dragged; boxes of text and
of art that hasn't loaded are a guess until it draws (the panel says so).

**Save** writes the page, changing only the values you edited. Before the
first save changes a page, its original is kept in
`manage/backups/layouts/<Theme>/<page>` (where every Manage screen keeps
originals), and **Put back the original...** restores it (what's there then is
kept beside it as `<page> (2).xml`; Undo brings back what you were editing).
A page changed by another program since it was opened isn't overwritten or
put back. If the theme is the one in use, RetroFE
reloads when you close the manager. Undo and Redo cover every change until
you leave.

A page the widget editor wrote (it has the marker) isn't opened here: edit
the theme in the widget editor instead.

## Limits in this version

- No controller driving: the editor needs a mouse and keyboard.
- The layout editor changes existing elements only: no new elements yet,
  and animations are shown but not edited.
- Second-screen pages (One Saucier's `layout1.xml`) are kept in the project
  but not saved.

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
report goes to the console and, if given, the report file. A hand-made menu
without `<itemDefaults>` (which used to crash RetroFE; it now gets empty
defaults) is reported, as is a vertical menu whose items have no height.

## For testing: scripted editing

```
retrofe -editorScript <file>
```

makes the editor run commands from a file, one per frame, the **first** time
a theme is opened in that session only (not again after a reload or when
another theme is opened), in whichever editor opens it. A banner says a script is running while it does,
and the results go to `log.txt` (`[ThemeEditor] script:`). It exists so the
editor can be checked without a mouse. Coordinates and distances are theme
units; blank lines and lines starting with `#` are skipped.

| Command | Does |
|---------|------|
| `wait N` | waits N frames |
| `page N`, `select N`, `selectid ID` | chooses a page or widget |
| `add DEFID` | places a widget (e.g. `wheel_vertical`, `game_title`); one that needs a file opens the chooser, as the dialog does |
| `click X Y` | clicks the canvas (click-through included) |
| `move DX DY`, `resize nw\|n\|ne\|w\|e\|sw\|s\|se DX DY`, `nudge DX DY` | drags the selection |
| `name TEXT`, `layer N`, `alpha N`, `colour RRGGBB`, `var TOKEN VALUE` | sets properties |
| `delete`, `undo`, `redo` | |
| `show media\|text\|anims on\|off`, `preview COLLECTION`, `rebuild` | preview options |
| `save`, `use`, `report`, `addDialog` | |
| `close` (asks if unsaved), `discard` (leaves without saving) | |
| `log TEXT` | writes to the log |
| `tplnew COLLECTION blank\|default\|PRIMARY NAME...` | a new template, empty, a copy of the Default page or of a template (named by its first collection) |
| `tplassign COLLECTION PRIMARY\|default`, `tpldelete PRIMARY [REPLACEMENT\|default]`, `tplrename PRIMARY NAME...` | templates |
| `choose COLLECTION` | picks it in the Collections list (previews it on its page) |
| `tpldialog [COLLECTION]`, `tpldeletedialog PRIMARY`, `closedialogs` | opens or closes the template dialogs |
| `import TOKEN PATH`, `importfont PATH`, `importsound load\|unload\|highlight\|select PATH`, `importsplash PATH` | copies a file in and uses it, as choosing it does |
| `font PATH\|default`, `sound KIND PATH\|none`, `splashtext TEXT`, `splashbg PATH\|none` | theme properties, with paths already in the theme |
| `browse [TOKEN]`, `browsedir FOLDER` | opens the file chooser for the selected widget, and shows a folder (relative to RetroFE's) in it |
| `open fonts\|splash\|collections`, `open -fonts` ... | unfolds (or folds) a section of the theme panel |

Collection names in commands can't contain spaces.

In the layout editor the commands are:

| Command | Does |
|---------|------|
| `wait N`, `log TEXT`, `status` | as above; `status` notes the status line |
| `page PATH` | opens another page of the theme (`collections/_menu/layout/layout.xml`); refused with unsaved changes |
| `select N`, `selectpath PATH` | chooses an element by its place in the list, or by its path (`image[3]`, `menu[0]/item[4]`, `-` for the page) |
| `click X Y`, `move DX DY`, `resize nw\|n\|ne\|w\|e\|sw\|s\|se DX DY`, `nudge DX DY` | as above, on the element's box |
| `attr NAME VALUE...`, `rmattr NAME`, `get NAME`, `layer N` | sets, removes or notes an attribute |
| `duplicate`, `delete`, `undo`, `redo`, `snap on\|off` | |
| `save`, `restore` (puts back the original), `restoredialog`, `deletedialog`, `closedialogs` | |
| `boxes on\|off`, `preview COLLECTION`, `rebuild`, `close`, `discard` | |

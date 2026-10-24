# The theme editor

The theme editor builds RetroFE themes (layouts) out of ready-made
**widgets**: the game wheel, the game's title, year and maker, box art,
videos, logos, fixed images and text. You place them on a canvas, size them,
set their options and save; the editor writes the layout files for you. It
was ported from One Saucier's editor.

It runs inside Manage, and works with a mouse and keyboard or with only a
controller or arcade controls (see [With a controller](#with-a-controller)).

## What it edits

Two kinds of theme, in two ways:

- **Themes made with it**, as widgets (most of this page). Such a theme has
  its project inside it (`layouts/<Theme>/editor/project.xml`) and a marker on
  the first line of each page it wrote.
- **Hand-made themes**, Aeon Nox included, in the **layout editor**: it
  changes a page's elements, adds new ones and edits their animations, in
  the page file itself (see
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
  - Arrow keys nudge the selection by 1 (Shift: 10); Delete removes it;
    Ctrl+D duplicates it (the copy goes 20 units down and right).
  - *Show* turns art and video, text, and animations off in the preview only.
  - Several at once: see [Several at once](#several-at-once).
- **Right: the selected widget.** Name, position and size, layer (0 is
  drawn first, 19 on top), opacity, text colour where it has one, and the
  widget's own settings: switches, choices, numbers, colours, text and files.
  *Choose...* opens the file chooser (below). *Hide in the editor* only hides
  it here, to reach what's underneath.
- **Top: Add widget, Undo, Redo, Save, Use this theme, Close.** Ctrl+Z,
  Ctrl+Y and Ctrl+S work too. Under the canvas the editor says what the
  generator warned about and anything RetroFE logged building the page.

The widget list follows the selection: a widget picked on the canvas (or
with the controller) is scrolled into view there.

Save shows what it did when there's something to know: warnings, and **pages
it removed** (a page an earlier save wrote that the theme no longer has).
Leaving with unsaved changes (Close, Escape, Back or the Settings key) asks
first: Save (Enter) / Don't save (D) / Cancel (Escape). In Add widget, type to
search and press Enter to add the first match.

## Several at once

Both editors can select several widgets (in the layout editor, elements)
and change them together:

- **Selecting**: Ctrl-click or Shift-click one on the canvas to add it to
  the selection or take it out; in the list, Ctrl-click adds or removes one
  and Shift-click takes everything between the last one picked and the one
  clicked. Dragging from an empty part of the canvas draws a box: what's
  wholly inside it is selected. With Ctrl or Shift held the box can start
  anywhere (a hand-made page's background usually covers the whole page)
  and adds to the selection. Ctrl+A selects everything on the canvas.
  Escape (or Back) keeps only the last one picked.
- **Moving**: dragging any of them moves them all, snapped as one box;
  the arrow keys nudge them all. A group stays on the page as one box.
- **The properties panel** shows *N selected* with **Left, Centre, Right,
  Top, Middle, Bottom** (they line up on the box around them; sizes don't
  change), **Space across / Space down** (three or more: the outer two stay
  and the others get equal gaps), **Duplicate them**, **Delete them** and
  **Only this one**. Below that are the settings of the last one picked.
- Every one of these is a single undo step.
- Resizing is one at a time: with several selected there are no handles.
- In the layout editor, positions are written as numbers, as a drag writes
  them (aligning a `x="center"` element sideways makes its `x` a number); an
  element that can't be moved here (a vertical menu's generated slots) can be
  selected but stays put. Duplicate copies each element right after itself;
  one inside another selected element (a menu's item) goes with it.

## With a controller

Both editors work with RetroFE's own controls (`controls.conf`: a gamepad,
arcade controls, or the keyboard keys mapped there), no mouse needed. The
screen has four regions: the **toolbar** at the top, the **theme panel** (in
the layout editor: pages and elements) on the left, the **canvas** in the
middle and the **properties** on the right. The focused region has a gold
frame, the focused item a gold outline, and the line under the canvas says
what the controls do there.

- **The canvas** (where the editor starts) has three modes, shown in its top
  left corner:
  - **Pick**: the directions select the nearest widget (element, menu item)
    that way; with nothing selected, the first press picks the one nearest the
    middle. Past the last one in a direction, the focus moves to the region
    that way (left: the theme panel, right: the properties, up: the
    toolbar). Letter up / Letter down select the previous / next one in list
    order. Select (when it's let go) starts moving it (with nothing selected,
    it opens Add widget / Add element). Back leaves the editor, asking first
    about unsaved changes; with several selected it keeps only the last one
    picked instead.
  - **Several at once: hold Select** and press a direction: the nearest one
    that way joins the selection (and becomes the one the next press goes
    on from), one per press (holding the direction doesn't add a run). Letter up / down while holding Select add the previous / next
    in list order, Back drops the last one added. Let go of Select and press
    it again to move them all.
  - **Move** (cyan outline): the directions move the selection by the snap
    grid (5 units, or 1 with *Snap to grid* off). Holding a direction speeds
    up: four times the step after a moment, ten times after a while. Select
    switches to Resize (with several selected, back to Pick); Back goes back
    to Pick.
  - **Resize** (pink outline, the handle being dragged enlarged): the
    directions move that handle, with the same speed-up. It starts on the
    bottom-right corner; **Letter up / Letter down** step round the eight
    handles (bottom edge, bottom-left corner, left edge, top-left corner...),
    as the mouse can grab any of them: an edge moves one way only, the
    opposite edge stays put. Select or Back go back to Pick.
  A run of moves and resizes is one undo step. A vertical menu's generated
  slots can't be moved (the status line says so): change the menu's settings
  instead.
- **Panels and the toolbar**: Up and Down move between rows, Left and Right
  between the items of a row (a field's *Choose...* and *x*). Where nothing
  is that way, the focus moves to the region that way (Up from the top of a
  panel: the toolbar; Down from the toolbar: the canvas). Back returns to the
  canvas. Page Up / Page Down step through the regions from anywhere.
  - Numbers (position, size, layer, opacity, a widget's number settings):
    Left / Right step them; Select types one on the on-screen keyboard.
  - Choices (drop-downs): Left / Right pick the previous / next; Select opens
    the list (Up / Down, Select chooses, Back closes it).
  - Text fields (names, attribute values, animation values, search and filter
    boxes): Select types on the **on-screen keyboard**, the same keys as the
    Manage screens' keyboard. Colours are typed as six hex digits (`FFAA00`).
  - Sections and animation events fold with Left / Right or Select.
  - Buttons, checkboxes and list rows: Select.
- **Dialogs** (Add widget, Add element, the template dialogs, the file
  chooser, Saved, Unsaved changes, Delete, Put back the original): Up / Down
  / Left / Right move between their items, Select presses, Back closes the
  dialog (as Cancel). A dialog opens on the button its Enter label names
  (*Save (Enter)*, *Delete (Enter)*), so Enter does what it says whether or
  not it's mapped as Select. In the file chooser Select opens a folder and
  chooses a file.
- **The on-screen keyboard**: the directions move between keys, Select
  presses one, Done keeps the text and Back cancels. Typing on a real
  keyboard while it's up types there instead (Enter keeps it, Escape cancels).
  With `onScreenKeyboard = no` Select starts typing into the field with a
  real keyboard instead.

The mouse and keyboard keep working alongside, but a keyboard key that's
mapped in `controls.conf` acts as that control while no field is being typed
into:

- With the shipped `controls.conf`, Left and Right are mapped (directions)
  but Up and Down aren't (Keypad 8 / 2 are), so on the canvas Left / Right
  pick a widget while Up / Down nudge it by 1. Shift with a mapped arrow
  still nudges by 10, as before.
- **B** is Back (it leaves the editor from the canvas, asking about unsaved
  changes first), **M** / **N** are Letter up / down (the previous / next
  widget), and Return is Select. Typing into a field (a click puts the cursor
  there) or on the on-screen keyboard isn't affected.
- Enter mapped as Select presses the focused button in a dialog; *D* (Don't
  save) works from the keyboard as before (any key that isn't mapped hands
  the dialogs back to the keyboard's shortcuts).
- A control mapped to a mouse button doesn't act in the editor: the mouse
  clicks.

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
layout editor. It moves, resizes and changes the attributes of a page's
elements, copies (Duplicate) or deletes one, adds new ones and edits their
animations. Everything you don't change stays exactly as it was: comments,
spacing, the order of attributes, quotes, line endings.

- **Left: Pages and Elements.** Every page of the theme: `layout.xml` and its
  variants (`layout 4x3.xml`, `layout - 1.xml`), `splash.xml`, the
  collections' pages (`collections/<c>/layout/*.xml`, `_menu` included) and
  the Manage screens' pages (`forms/**`). Under it, the page's elements in
  file order: the page itself (`<layout>`: its size, font and colour), then
  each element, with a menu's `<itemDefaults>` and `<item>`s under it.
  Elements greyed out draw nothing on the page (sounds, for instance). An
  element picked on the canvas is scrolled into view in the list.
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
  takes from its `<itemDefaults>`. Game art, video and text have *Shows*
  (their `type`, or a video's `imageType`) with the usual choices; an image,
  video or sound has *Choose...* beside `src`. *All attributes* lists every
  attribute, each editable as text and removable (*x*), and adds new ones.
  *Animations* edits its events (below).
- **Top:** *Where animations end* draws a dashed box where an element's
  `onEnter` leaves it, when that isn't where it's placed (Aeon Nox's top bar
  is placed above the page and slides down to it); with *All boxes*, every
  element's. The list beside it picks another event (*after onMenuScroll*,
  *after onHighlightEnter*...) or *every event*: then the selected element
  gets a box, labelled, for each of its events that moves it. Each box
  starts from where the element is placed and takes the last value each
  change of the event gives, so an event that plays after another (a
  `onMenuExit` after a `onMenuEnter`) is shown from the placed box, not from
  where the other left it. *Play the start* builds the preview again, so
  every `onEnter` plays.

### layout.xml and layout 4x3.xml together

Most hand-made themes have a 16:9 page and a 4:3 one with the same elements
at different positions. When the page being edited has one (`layout.xml`
and `layout 4x3.xml` in the same folder, either way round), a row under the
toolbar offers:

- **Also apply to layout 4x3.xml**: every change on this page is made to the
  same element on the other page too, as text, changing only those bytes
  there. *The same element* is looked for under the same parent (the page,
  the same menu): the one in the same place among as many of its tag, with
  the same `type`, `src`, `id` and the like; else the only one like it; else
  the one in the same place that differs only by its file or text (`src`,
  `value`). A wheel's items pair up by their place from its selected item,
  so a 9-item wheel and a 7-item one match around the middle and the outer
  two have no counterpart; a vertical menu's by their `index`. What's
  copied:
  - a position or size set to a number (`x`, `y`, `width`, `height`,
    `xOffset`, `yOffset`, `maxWidth`, `maxHeight`): the same move there,
    scaled to that page and rounded to whole units (moving 96 to the right
    on a 1920-wide page moves it 80 on a 1600-wide one). `center` and `50%`
    count as what they mean on each page;
  - anything else (colours, fonts, layers, files, animation values, a
    `center` typed in): only where the other page had the same value as this
    one. Where it already had its own, it's left, and the line beside the
    switch says so;
  - not the page's own size, and not adding, deleting or duplicating
    elements, events or steps (the line says so).
  A run of separate moves is scaled as one (ten 1-unit nudges move the
  1600-wide page 8, not 10), until the other page's value is changed there.
  Undo and Redo take both pages' step back together; when the other page
  was changed on its own since, Undo works on this page only and says so.
  Save saves both (each keeps its original in `manage/backups`, as any page
  does).
- **Show layout 4x3.xml** (or choosing it in the Pages list): the other page
  is shown with the same elements selected; neither page's unsaved changes
  are lost, and Show layout.xml goes back. Leaving, or opening another page,
  asks about both pages' changes.
- The properties panel says where the selected element is on the other page
  (or that it isn't there).

### Adding elements

**Add element...** (above the element list) shows the elements RetroFE
builds, written as its own XML:

| Entry | Element | |
|---|---|---|
| Image, Video | `image`, `video` | a file of the theme's; asks for it first |
| Text, Status text | `text`, `statusText` | fixed text; what RetroFE is doing |
| Container (a box) | `container` | a coloured box |
| Game art | `reloadableImage type=` | logo, artwork_front, screenshot, fanart, marquee... |
| Game video | `reloadableVideo imageType=` | the game's video, with art of that kind when it has none |
| Game text, Scrolling game text | `reloadableText`, `reloadableScrollingText type=` | title, year, manufacturer...; story, description, tagline |
| Hiscores | `reloadableHiscores` | |
| Menu (a list), Menu (a wheel) | `menu` with `<itemDefaults>` (and three `<item>`s for the wheel) | |

The new element goes after the selected element (after its menu, for a menu
item), or at the end of the page when nothing is selected, on lines of its
own indented as its neighbours, with the file's line endings. It's placed in
the middle of the page at a usual size, on a layer above every other, and
selected. For an image or video the file chooser opens first: a file from
elsewhere is copied into the page's own `images/` (`video/` or `videos/` for
a video) when its folder has one, else the theme's, else `assets/` (a
different file with the same name gets `-2`); one already in the theme is
used where it is. `src` is written relative to the page's folder, as RetroFE
reads it: a collection's page gets `images/x.png` beside it, or
`../../../images/x.png` for the theme's. *Not now* adds it with a
placeholder name, which RetroFE skips until *Choose...* names a file.

### Animations

An element's events (`<onEnter>`, `<onExit>`, `<onMenuEnter>`,
`<onHighlightEnter>`, ... every one RetroFE reads) are listed in file order.
Each has steps (`<set duration=>`) that play one after another, and each step
has changes (`<animate>`) that play together: a *type* (x, y, alpha, width,
xOffset, fontSize...), *from* (empty: from where it is), *to*, and an
*algorithm* (linear when not set). Everything is edited in place, as text:

- *+ change* adds `alpha to 1` to a step, *+ step* a step of 0.2 seconds,
  *Add event* an event with one step (alpha from 0 to 1); *x* removes an
  event, step or change. Clearing a field removes that attribute (an empty
  `from` would make RetroFE start at 0 rather than where the element is).
  The layout check reports empty `from`, `to` and `menuIndex`, and a `to`
  that isn't a number, a percentage or left/center/right/top/bottom/stretch.
- *Play* plays that event on the preview (as RetroFE triggers it), with the
  page as edited.
- *Copy* (with an element chosen beside it) replaces the element's events
  with copies of that element's, as they're written.

An element whose animations change its position or size says so: on screen
it moves from where it's placed here.

What a drag writes: **numbers**. Dragging an element sideways turns
`x="center"` into the number it stood for plus the move; dragging it
straight up or down leaves `x` alone. Resizing writes `width` and/or
`height` (only the ones that changed) and moves `x` / `y` so the edges you
didn't drag stay put, whatever the origin. A menu item whose position comes
from its `<itemDefaults>` (it sets `xOffset` but not `x`, like Aeon Nox's
wheel) moves by its `xOffset` / `yOffset` instead, so it keeps following the
defaults. A vertical menu's slots are
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

- With a controller, colour pickers are typed as hex rather than picked.
- Several selected move, line up and space together, but resize one at a
  time.
- The layout editor edits a page with its 4:3 twin only (not other variants
  such as `layout - 1.xml`, nor several collections' pages), adds raw
  elements rather than the widget editor's widgets, and draws where each
  event leaves an element from where it's placed (not from where an earlier
  event left it, and not frame by frame).
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
Names from OneSauce's themes that [ONESAUCE-COMPAT.md](ONESAUCE-COMPAT.md)
lists as ignored (`hwdec`, `Height`, `pauseTime`, ...) aren't problems
here: one "ignored (OneSauce)" line counts them.

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
| `name TEXT`, `layer N`, `alpha N`, `colour RRGGBB`, `var TOKEN VALUE` | sets properties; `alpha` takes 0-1 as layouts write it (`0.8`; `1` is opaque) or a percent (`2` to `100`, or `80%`; `1%` for 1%) |
| `delete`, `undo`, `redo` | (`delete` takes every selected widget) |
| `addselect N`, `toggleselect N`, `selectall`, `selection` | several at once (`selection` notes them) |
| `clickadd X Y`, `marquee X1 Y1 X2 Y2 [add]` | a Ctrl-click; a box from one corner to the other (without `add` it must start on empty canvas) |
| `align left\|center\|right\|top\|middle\|bottom`, `distribute h\|v`, `duplicate` | the selection |
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
| `boxes on\|off`, `endboxes on\|off`, `preview COLLECTION`, `rebuild`, `close`, `discard` | |
| `add ID [TYPE\|-] [SRC...]` | adds a palette entry (`image`, `text`, `reloadableText`, `menu-custom`...; `-` keeps its type) |
| `addfile image\|video PATH`, `srcfile PATH` | copies a file in and adds an image or video with it, or makes it the selected element's `src` |
| `palette`, `browse [src\|image\|video]`, `browsedir FOLDER` | opens the palette, or the editor's file chooser (for the `src`, or a new image or video) |
| `event NAME`, `addset EVENT`, `addanimate SET`, `rmanim PATH` | animations, by paths inside the selected element (`onEnter[0]/set[0]/animate[1]`) |
| `animattr PATH NAME VALUE...`, `rmanimattr PATH NAME`, `getanim PATH NAME` | an event's, step's or change's attributes (an empty value removes it) |
| `showanims`, `openevent EVENT` | scrolls the panel to *Animations*; unfolds an event (`onEnter[0]`) |
| `copyanims PATH`, `events`, `endbox [EVENT]`, `movingevents`, `replay EVENT` | copies another element's animations; notes the events, where onEnter (or EVENT) leaves it and which events move it; plays an event on the preview |
| `endevent EVENT\|all` | which event's end boxes the canvas draws (`all`: every event) |
| `addpath PATH`, `togglepath PATH`, `selectall`, `selection`, `clickadd X Y`, `marquee X1 Y1 X2 Y2 [add]`, `align ...`, `distribute h\|v` | several at once, as in the widget editor; `duplicate` and `delete` take every selected element |
| `twin on\|off`, `switchtwin`, `twinnote`, `twinget NAME` | edits copied to the page's 4:3 twin (or not); shows the twin with the same elements selected; notes what the last copy did; notes the selected element's attribute on the twin |

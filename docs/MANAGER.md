# Home Menu, Catalog, Settings and Manage

RetroFE can start with nothing but its executable. With no `settings.conf`,
`controls.conf`, collections or layout it uses built-in defaults, and you set
everything else up from inside RetroFE.

## The Home menu

With `homeMenu = yes` RetroFE starts on **Home**:

| Item     | What it does |
|----------|--------------|
| Catalog  | Packages you can install from your sources (see [The Catalog](#the-catalog)) |
| Settings | Display, menu and video settings |
| Manage   | Add collections, see what's installed, and open Catalog sources |
| Browse   | Opens `firstCollection`, your usual top menu. Back returns to Home. |

`homeMenu` is on for a bare install and in the `settings.conf` the setup
creates. Existing installs don't have it, so they start on `firstCollection`
as before. Kiosk mode always skips Home.

F10 opens Settings from anywhere, switches to it from another screen, and
closes it again; change the key with `manager` in `controls.conf`. Back or
Escape closes a screen.

Home is a collection built into RetroFE, so a layout styles it like any other:
`layouts/<layout>/collections/Home/layout/layout.xml`, with artwork named after
the items (`Catalog`, `Settings`, `Manage`, `Browse`).

## The Catalog

The Catalog installs packages (collections, artwork, layouts and so on) from
**sources** you add: an index file on the web or on disk, or a folder of
package archives such as a USB stick or a NAS share. The design, including the
index format, is in [design/catalog-sources.md](design/catalog-sources.md).

- **Catalog** on Home lists every package your sources offer, followed by a
  **Sources** item. Until there's something to list it opens the Sources screen.
- Selecting a package shows its details, with **Install**, **Update**,
  **Remove** or **Stop** as they apply. Downloads continue in the background,
  so you can go back to the list and pick another; they run one at a time.
- Once you're back on Home with nothing left to do, RetroFE reloads so new
  content shows up. Installed collections are added to the Main menu, so they
  appear under Browse; removed ones are taken off it. Saving Settings while an
  install is running also waits for it before reloading.
- **Sources** (from the Catalog or Manage) adds and removes sources. Adding
  one asks you to confirm that you trust it: RetroFE doesn't check what sources
  offer. Plain `http://` is only allowed on your own network (localhost, a
  machine name like `nas`, or a private address), for indexes and downloads alike.
- **Manage** lists what's installed and can remove any of it, including
  packages no source offers any more.
- A package can only be updated from the source it was installed from. If
  another source offers the same package, remove it first to switch.

What a package may install: content, such as collection settings, launchers,
artwork, layouts and ROMs, under its targets. Programs and scripts (`.exe`,
`.dll`, `.bat`, `.ps1`, `.sh` and the like) are refused, as is anything in the
catalog folder or RetroFE's program folder. File names Windows treats as
devices (`CON`, `NUL`, `COM1`…) or that end in a dot or space are refused too.

Everything the Catalog keeps is in one folder, `catalogPath` in
`settings.conf` (default `catalog` under the RetroFE root): `sources.conf`,
cached indexes, downloads, artwork, backups, and a record of every installed
file in `installed/<id>.json`.

- **Installing** backs up every file it's about to replace first. If any part
  of the install fails, or RetroFE stops and can't finish it on the next start,
  everything is put back as it was.
- **Removing** a package deletes the files it installed, puts back files of
  yours it had replaced, and keeps any file you've changed since.
- **Backups** kept per package: your original files, and the previous version
  from the last update.

In kiosk mode the Settings key does nothing.

The Catalog list is a collection built into RetroFE like Home, so layouts
style it as `layouts/<layout>/collections/Catalog/layout/layout.xml`. Each
item is named by package id, so artwork follows the usual naming: the Catalog
saves package artwork as `catalog/artwork/<type>/<id>.<ext>` and points the
Catalog collection's `media.<type>` there. Items also carry the package's kind
as `genre`, its source as `manufacturer` and its version as `year`.

HTTP sources need Windows for now; on Linux and macOS folder sources and
index files on disk work.

## Settings and Manage screens

Each screen is a **form**: fields bound to settings or other values. Up and
Down move between fields; Left and Right change a switch, number or choice;
Select edits text, picks a folder or file, or presses a button. While editing
text, type and press Enter to keep it or Escape to cancel; Ctrl+V pastes.

Settings writes each value to the file RetroFE actually reads it from:
`settings.conf`, `settings1.conf` to `settings15.conf`, or
`settings_saved.conf`. It only changes that line, keeping comments and
layout. Values set in an override file say so ("Set in settings1.conf").

### Styling the screens

A screen is drawn as a page from your layout, so it can use backgrounds,
images, video, text and animations like any other page. RetroFE looks for:

1. `layouts/<layout>/forms/<screen>/layout.xml` for one screen (`settings`,
   `manage`, `sources` or `package`)
2. `layouts/<layout>/forms/layout.xml` for all of them
3. otherwise its built-in form page

The page places the form with a `<form>` element:

```xml
<layout width="1920" height="1080" font="fonts/standard.ttf" loadFontSize="40">
  <image src="images/settings_background.png" x="0" y="0" width="1920" height="1080"/>
  <form x="center" y="center" xOrigin="center" yOrigin="center" width="1200" height="900"
        fontSize="32" fontColor="c8c8c8" focusColor="ffffff"
        rowHeight="50" labelWidth="400"
        focusBackgroundColor="3d6fb6" focusBackgroundAlpha="0.55"
        backgroundColor="14181e" backgroundAlpha="0.9" layer="2"/>
</layout>
```

| Attribute | Meaning |
|-----------|---------|
| `x`, `y`, `width`, `height`, `xOrigin`, `yOrigin`, `layer`, `alpha`, `monitor` | As for other components |
| `font`, `fontSize`, `fontColor`, `loadFontSize` | Text, as for `<text>` |
| `focusColor` | Text color of the focused row (default `ffffff`) |
| `rowHeight` | Height of each row (default 1.6 x `fontSize`) |
| `labelWidth` | Width of the label column (default 40% of the width) |
| `focusBackgroundColor`, `focusBackgroundAlpha` | Highlight behind the focused row |
| `focusImage` | Image drawn behind the focused row instead, relative to the page's folder |
| `backgroundColor`, `backgroundAlpha` | Panel behind the form |
| `noteAlpha` | Opacity of help text and notes (default 0.55) |
| `disabledAlpha` | Opacity of fields that can't be changed right now (default 0.35) |
| `onText`, `offText` | How switches read (default `On` / `Off`) |
| `title` | `no` hides the screen's title |
| `hints` | `no` hides the controls hint at the bottom |

Set `formsRenderer = imgui` in `settings.conf` to draw the screens with Dear
ImGui instead, for example while developing.

### The on-screen keyboard

Editing a text, number or path field shows an on-screen keyboard, so you can
type with a gamepad or arcade controls: the directions move between keys,
Select presses one, Back cancels. Shift switches to capitals and symbols, and
number fields get a number pad. Typing on a real keyboard puts the on-screen
one away for that edit; finish with Enter or cancel with Escape. Set
`onScreenKeyboard = no` to never show it.

A layout styles it with `layouts/<layout>/forms/keyboard/layout.xml`, a page
with a `<keyboard>` element; it's drawn over the screen being edited:

```xml
<layout width="1920" height="1080" font="fonts/standard.ttf" loadFontSize="40">
  <keyboard x="center" y="1050" xOrigin="center" yOrigin="bottom" width="1400" height="600"
            fontSize="36" fontColor="c8c8c8" focusColor="ffffff"
            keyColor="ffffff" keyAlpha="0.07" focusKeyColor="3d6fb6" focusKeyAlpha="0.75"
            backgroundColor="0d1014" backgroundAlpha="1" layer="1"/>
</layout>
```

| Attribute | Meaning |
|-----------|---------|
| `x`, `y`, `width`, `height`, `xOrigin`, `yOrigin`, `layer`, `alpha`, `monitor` | As for other components |
| `font`, `fontSize`, `fontColor`, `loadFontSize` | Text, as for `<text>` |
| `focusColor` | Text color of the focused key (default `ffffff`) |
| `keyColor`, `keyAlpha` | Key background (default white at 0.07) |
| `focusKeyColor`, `focusKeyAlpha` | Focused key background |
| `keyImage`, `focusKeyImage` | Images drawn for keys instead, relative to the page's folder |
| `keySpacing` | Gap between keys (default a quarter of `fontSize`) |
| `backgroundColor`, `backgroundAlpha` | Panel behind the keyboard |
| `noteAlpha` | Opacity of the field name and hints (default 0.55) |
| `hints` | `no` hides the controls hint at the bottom |

### Changing what a screen contains

The fields themselves are also XML. RetroFE's built-in screens are in
`RetroFE/Source/Forms/BuiltinForms.cpp`; a layout can replace one with
`layouts/<layout>/forms/<screen>.xml`:

```xml
<form id="settings" title="Settings">
  <section label="Display">
    <toggle label="Fullscreen" bind="settings:fullscreen"/>
    <text label="Width" bind="settings:horizontal" hint="pixels, or stretch" enabledWhen="!settings:fullscreen"/>
  </section>
  <actions>
    <button label="Save" action="settings.save"/>
  </actions>
</form>
```

| Element | Field |
|---------|-------|
| `section` | A titled group of fields |
| `actions` | Buttons |
| `text`, `folder`, `file` | Text, a folder path, or a file path (`filter`, `pattern` like `*.exe`) |
| `toggle` | On or off |
| `number` | A whole number (`min`, `max`) |
| `choice` | One of a list (`options` is `layouts`, `collections`, `sources`, `sourceTypes` or `installed`) |
| `list` | A read-only list (`empty` is shown when there's nothing in it) |
| `note` | Text: `value`, or with `bind` a value from RetroFE; hidden when empty |
| `button` | Runs an `action` |

Every field can have `label`, `hint` (shown while empty), `help` (shown
under it), `status` (a value shown under it), `enabledWhen` (a value that must
be on, or `!value` for off; the field is greyed out otherwise), `visibleWhen`
(the same, but the field is hidden) and `default`. On `<form>`, `bind` gives
a value to use as the title when it has one, e.g. `bind="package:name"`.

`bind` says what a field shows and changes:

- `settings:<key>`: a setting from `settings.conf`. Changes are kept until
  `settings.save`. `default` is the value RetroFE uses when no file sets it.
- `draft:<key>`: input for an action, such as a new collection's name.
  `default` is its starting value.
- `info:<key>`: read-only: `info:collections`, `info:sources`,
  `info:installed` and `info:catalogWarnings` (lists), `info:romCount`,
  `info:catalogStatus`, `info:removalStatus`, and `info:hasSources`,
  `info:hasInstalled` and `info:sourceIsFolder` for `visibleWhen`.
- `package:<key>`: the package chosen in the Catalog: `name`, `summary`,
  `description`, `installedStatus`, `requires`, `checksum` (a warning when the
  source gives none), `progress`, and `canInstall`, `canUpdate`, `canRemove`
  and `isBusy` for `visibleWhen`.

Actions:

| Action | Does |
|--------|------|
| `settings.save` | Writes the changed settings |
| `collection.create` | Creates a collection from `draft:name`, `draft:romPath`, `draft:extensions`, `draft:executable` and `draft:arguments` |
| `package.install`, `package.remove`, `package.cancel` | Install or update, remove, or stop the chosen package |
| `package.removeInstalled` | Removes the installed package `draft:removePackage` |
| `source.add` | Adds `draft:sourceName` of `draft:sourceType` at `draft:sourceUrl` or `draft:sourcePath`, once `draft:sourceTrusted` is on. A layout's form can't give these a `default`. |
| `source.remove` | Removes `draft:removeSource` |
| `catalog.refresh` | Reads the sources again |
| `open.sources` | Opens the Sources screen |

## The default layout: Aeon Nox

RetroFE's default layout is **Aeon Nox**, David Horowitz's theme after Kodi's
Aeon Nox skin: the built-in settings, and the `settings.conf` RetroFE creates,
say `layout = Aeon Nox`. When that folder isn't installed, RetroFE uses its
built-in pages. `layouts/Aeon Nox/NOTICE.txt` says where it comes from and what
this fork changed. It styles everything above, so it's a working example:

| File in `layouts/Aeon Nox/` | Draws |
|-----------------------------|-------|
| `layout.xml` | A collection's games: the information wall and box art. Every collection without a page of its own uses it. |
| `collections/Main/layout/layout.xml` | Browse: one logo per collection, or its name when there's no logo |
| `collections/Home/layout/layout.xml` | Home, in the same style as Browse |
| `collections/Home/medium_artwork/` | Home's artwork and text: `logo/`, `icon/`, `tagline/` and `description/`, each with a file per item (`Catalog.png`, `Catalog.txt`...) |
| `collections/Catalog/layout/layout.xml` | The Catalog: package logos, with kind, source and version in the info panel |
| `collections/Catalog/medium_artwork/` | An icon and text for the Sources item (`Catalog Sources.png`, `.txt`), which has no package artwork |
| `forms/layout.xml` | Settings, Manage and Sources |
| `forms/package/layout.xml` | A package: the form takes the top of the screen, so the Catalog's info panel stays visible below it |
| `forms/keyboard/layout.xml` | The on-screen keyboard |
| `layout 4x3.xml` next to each `layout.xml` | 4:3 versions, used with `randomLayout = layout 4x3` |

Things worth knowing when you make your own:

- Every collection, Home and the Catalog included, is a page of its own with its
  list at `menuIndex` 0, and a page starts with `onEnter`. Tweens that only use
  `onMenuEnter` for a deeper menu never run.
- Home's items and the Sources item are built in, so their artwork comes from the
  layout: `mode="layout"` on a `<menu>`, `reloadableImage` or
  `reloadableScrollingText` reads
  `layouts/<layout>/collections/<collection>/medium_artwork/<type>/<item>`.
  `reloadableScrollingText` with `direction="vertical"` and `startPosition="0"`
  shows a text file as wrapped text that only scrolls when it doesn't fit.
- `src` paths are relative to the page's own folder. RetroFE falls back to the
  layout's folder but logs an error first, so pages in `collections/<name>/layout/`
  use `../../../images/...`. Fonts are always relative to the layout's folder.
- Form and keyboard pages have no selected item, so reloadable components show
  nothing there (not even `type="time"`). To show a package's artwork, leave part
  of the Catalog page visible behind the form, as `forms/package/layout.xml` does.
- A label that should only appear with its value can be the value's prefix
  (`singlePrefix` and `pluralPrefix`) padded with spaces and cut off by
  `maxWidth`; the Catalog page does this for Kind, Source and Version.

# Home Menu, Settings and Manage

RetroFE can start with nothing but its executable. With no `settings.conf`,
`controls.conf`, collections or layout it uses built-in defaults, and you set
everything else up from inside RetroFE.

## The Home menu

With `homeMenu = yes` RetroFE starts on **Home**:

| Item     | What it does |
|----------|--------------|
| Catalog  | Content you can download (not built yet) |
| Settings | Display, menu and video settings |
| Manage   | Add collections |
| Browse   | Opens `firstCollection`, your usual top menu. Back returns to Home. |

`homeMenu` is on for a bare install and in the `settings.conf` the setup
creates. Existing installs don't have it, so they start on `firstCollection`
as before. Kiosk mode always skips Home.

F10 opens Settings from anywhere; change the key with `manager` in
`controls.conf`. Back or Escape closes a screen.

Home is a collection built into RetroFE, so a layout styles it like any other:
`layouts/<layout>/collections/Home/layout/layout.xml`, with artwork named after
the items (`Catalog`, `Settings`, `Manage`, `Browse`).

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
   `manage` or `catalog`)
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
| `choice` | One of a list (`options="layouts"` or `options="collections"`) |
| `list` | A read-only list (`empty` is shown when there's nothing in it) |
| `note` | Text (`value`) |
| `button` | Runs an `action` |

Every field can have `label`, `hint` (shown while empty), `help` (shown
under it), `status` (a value shown under it), `enabledWhen` (a value that must
be on, or `!value` for off) and `default`.

`bind` says what a field shows and changes:

- `settings:<key>`: a setting from `settings.conf`. Changes are kept until
  `settings.save`. `default` is the value RetroFE uses when no file sets it.
- `draft:<key>`: input for an action, such as a new collection's name.
  `default` is its starting value.
- `info:<key>`: read-only: `info:collections` (list), `info:romCount`.

Actions are `settings.save` and `collection.create` (which uses
`draft:name`, `draft:romPath`, `draft:extensions`, `draft:executable` and
`draft:arguments`).

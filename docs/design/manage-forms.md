# Manage screens: adding a section

How to build one of the Manage sections (Games, Menus, Systems, Videos,
Emulators, Themes) on top of the forms layer. User-facing behaviour, every
form element and the built-in bindings are in [../MANAGER.md](../MANAGER.md).

## How a screen works

- A screen is a **form**: XML with fields bound to model keys
  (`game:title`) and buttons that run actions (`game.save`). See
  `RetroFE/Source/Forms/Form.h`.
- `ManagerUI` keeps a **stack** of screens (`Manager/Navigator.h`). Each frame
  is a form id, its **parameters** (`Forms::Params`, a string map such as
  `{ "collection": "Arcade", "id": "pacman" }`) and where the user was on it.
  A `link`, a `results` row or an action's result pushes a screen; Back pops
  it and puts the focus back where it was. Back on the first screen closes
  the manager. Past 32 screens the oldest but the first is dropped.
- `ManagerModel` answers every binding. Built-in prefixes (`settings`,
  `draft`, `info`, `param`, `package`, and the actions `collection.*`,
  `source.*`, `catalog.*`, `open.*`, `back`) are its own; any other prefix
  goes to the **provider** registered for it.
- Forms are drawn by `FormWidget` (layout pages, driven by RetroFE's controls)
  or `FormView` (Dear ImGui, `formsRenderer = imgui`, for development).
  `FormWidget` rebuilds a screen from the model after input, when the
  model's `generation()` changes, and every 250 ms otherwise (so status lines
  such as Catalog progress keep moving). `get()`, `rows()`, `list()`,
  `options()` and `actionProblem()` run on every rebuild, so keep them cheap.
  The ImGui view calls them every frame.

## Where things go

Each section has its own folder, `RetroFE/Source/Manager/<Section>/`
(`Games`, `Menus`, `Systems`, `Videos`, `Emulators`, `Themes`, and `Controls` for Settings > Controls). CMake picks
up the files in these folders, so you don't edit `CMakeLists.txt` to add
files. Re-run the configure step after adding a file (the Visual Studio
generator only notices new files on the build after the one that finds them).

| File | What |
|------|------|
| `Manager/<Section>/<Section>Forms.cpp` | The section's built-in forms, returned by `Forms::<section>Forms()` (declared in `Forms/BuiltinForms.h`). It exists already with a "Coming soon" placeholder for the section's form id: replace it and add more forms to the table. `retrofe_tests` parses every built-in form. |
| `Manager/<Section>/*.cpp`, `*.h` | Your provider(s), implementing `Manager::Provider` (`Manager/Provider.h`), and their helpers. Built into RetroFE **and** `retrofe_tests`, so they must not need SDL or graphics code. |
| `Manager/<Section>/app/*.cpp`, `*.h` | Code only RetroFE builds: anything that needs SDL, pages or platform APIs. Platform-specific files go here too (or wrap their body in `#ifdef _WIN32`). |
| `Manager/<Section>/tests.cmake` | Optional: what your files need in the tests beyond the Forms/Manager/Catalog code already there, e.g. `list(APPEND RETROFE_TEST_EXTRA_SOURCES "${RETROFE_DIR}/Source/Database/DB.cpp")`. Use `${RETROFE_DIR}/Source/...` paths so duplicates between sections are merged. |
| `Manager/Providers.cpp` | Registration: one include line and one `model.addProvider(...)` line per provider, in your section's slots. Keep the blank lines between slots so sections added in parallel merge cleanly. |
| `Tests/<Section>Tests.cpp` | Unit tests, as a `static Test::Suite` (see `Tests/TestSupport.h`); every `Tests/*.cpp` is built in. |

The Systems section replaced the old "Add a collection" fields with its own
`system-add` screen (`system.create`, which adds a parent menu, installed
launchers and the video preset); `collection.create` still works for layouts
that use it. Systems and Videos share one provider class and one set of form
templates (`Manager/Systems/SystemForms.cpp`), registered twice. The Manage
hub links to each section's first form
(`BuiltinForms.cpp`, form `manage`); you shouldn't need to change it.

## Naming

- **Form ids**: lowercase, words joined with `-`. A section's first screen is
  its plural (`games`, `menus`, `systems`, `videos`, `emulators`, `themes`,
  linked from Manage); one record is the singular (`game`, `system`); deeper
  screens are `<singular>-<what>` (`game-media`, `system-emulator`). Ids are
  also folder and file names for layouts
  (`layouts/<layout>/forms/game-media/layout.xml`, `forms/game-media.xml`).
- **Binding prefixes**: the singular entity (`game:`, `menu:`, `system:`,
  `video:`, `emulator:`, `theme:`), the same in actions (`game.save`) and
  option sources (`options="game:genres"`). A section may register more than
  one provider, one per prefix.
- **Keys**: camelCase (`mediaSummary`); flags for `visibleWhen` and
  `enabledWhen` read `has…`, `can…`, `is…` and return `yes`/`no`.
- **Parameters**: `id` for the record a screen is about, plus whatever else
  is needed to find it, camelCase. Games are only unique within a
  collection, so game screens get **`collection` + `id`**; systems and menus
  get `id` (the collection or menu name). Use the same names across sections
  so one section's screens can open another's.

## Parameters: who passes what

| Opened by | Gets |
|-----------|------|
| `<link open="...">`, `open.<form>` | The current screen's parameters, unchanged: drilling into the same record. |
| A `results` row | Only the row's: `Row::params` plus `Row::id` as the field's `param` (default `id`) unless `params` already sets it. Nothing from the current screen, so an unrelated screen's parameters (say `tab=logo`) never leak into another record's. |
| An action's `Result::open` | `Result::params`, plus the current screen's when `Result::keepParams` is set (`params` win). |

So a games list returns rows like
`{ id: "pacman", label: "Pac-Man", detail: "1980", image: ".../pacman.png", params: { collection: "Arcade" } }`.
`Forms::openParams` and `Forms::rowParams` implement these rules.

## A provider

```cpp
class GameProvider : public Manager::Provider
{
public:
    explicit GameProvider(const Manager::Services& services) : services_(services) {}

    std::string prefix() const override { return "game"; }

    // The manager opened: data on disk may have changed while it was closed
    void refresh() override { searched_ = "\x01"; }

    // A screen became current (opened, or returned to with Back). Every
    // provider hears about every screen, so check the form.
    void enter(const Manager::Screen& screen) override
    {
        std::string key = screen.param("collection") + "/" + screen.param("id");
        if (screen.form == "game" && key != edits_.id()) {
            edits_.load(key, read(screen));   // keeps unsaved edits when it's the same game
        }
    }

    // Unsaved changes: warn once; leaving again calls discard()
    std::string leaving(const Manager::Screen& screen) override
    {
        return screen.form == "game" && edits_.isDirty()
            ? "You have unsaved changes. Save them, or leave again to discard them." : "";
    }
    void discard(const Manager::Screen&) override { edits_.reset(); }

    std::string get(const std::string& key, const Manager::Screen& screen) override
    {
        if (key == "isDirty") return edits_.isDirty() ? "yes" : "no";
        return edits_.get(key);   // "title", "year"...
    }
    void set(const std::string& key, const std::string& value, const Manager::Screen&) override
    {
        if (key == "query") query_ = value; else edits_.set(key, value);
    }

    // <results bind="game:search">: at most limit rows, and how many match in all
    Forms::Rows rows(const std::string& key, size_t limit, const Manager::Screen& screen) override
    {
        if (query_ != searched_) { searched_ = query_; matches_ = search(query_); }   // once per change
        Forms::Rows rows;
        rows.total = matches_.size();
        rows.items.assign(matches_.begin(), matches_.begin() + std::min(limit, matches_.size()));
        return rows;
    }

    std::string actionProblem(const std::string& action, const Manager::Screen& screen) override
    {
        if (action == "save" || action == "reset") return edits_.isDirty() ? "" : "No changes to save.";
        if (action == "delete") return "";
        return Provider::actionProblem(action, screen);   // "Unknown action game.<action>"
    }

    Forms::Model::Result runAction(const std::string& action, const Manager::Screen& screen) override
    {
        Forms::Model::Result result;
        result.message = actionProblem(action, screen);
        if (!result.message.empty()) return result;
        result.ok = true;
        if (action == "save") {
            write(screen, edits_.changes());
            edits_.commit();
            services_.requestReload();   // RetroFE shows the new title once the manager closes
            result.message = "Saved.";
        }
        else if (action == "reset") {
            edits_.reset();
        }
        else if (action == "delete") {
            remove(screen);
            result.message = "Deleted.";
            result.back = true;          // or: result.open = "game-media"; result.keepParams = true;
        }
        return result;
    }

private:
    Manager::Services services_;
    Manager::EditBuffer edits_;
    std::string query_, searched_ = "\x01";
    std::vector<Forms::Row> matches_;
};
```

and in `Manager/Providers.cpp`:

```cpp
// games
#include "Games/GameProvider.h"
...
    // games
    model.addProvider(std::make_unique<GameProvider>(services));
```

Things to know:

- Keys and action names arrive **without the prefix**: `game:title` is
  `get("title")`, `game.save` is `runAction("save")`.
- Every call gets the current `Manager::Screen`: its form id and parameters.
  Use `screen.param("id")` rather than remembering a selection yourself.
- Cache anything that reads files or a database, keyed by what it depends on
  (the search text, the id). For slow work (scanning folders, hashing), do a
  little in `update(dt)`, which runs every frame while the manager is open,
  and bump `generation()` when there's something new to show.
- `rows()` should honour `limit` (default 200, set per field) and set `total`;
  the model cuts longer lists anyway. `detail` is shown on the right.
- A search box is `<text bind="game:query" live="yes"/>`: `set("query", ...)`
  arrives on every keystroke, on a real or the on-screen keyboard. Cancelling
  the edit sets the old text back. Other text fields only `set()` when the
  edit is kept.
- **Multi-line text** is `<text bind="game:story:text" multiline="yes" rows="8"/>`
  (an attribute rather than a new element, so it's a text field in every
  other way: `live`, `hint`, `status`, `help`, `enabledWhen`). `get()`
  returns the lines joined with `"\n"` and `set()` gets them back the same
  way, never `"\r\n"`: the provider decides the file's line endings when it
  writes (the Games story keeps each line's original ending; see
  `GameProvider::textToStory`). The page draws `rows` wrapped lines, ending in
  "..." when there's more; while editing, it scrolls to the cursor. The
  layout and cursor logic is `Forms::layoutLines`, `Forms::TextEdit` and
  friends in `Forms/Form.h`, and is unit-tested (`Tests/TextEditTests.cpp`).
  ImGui draws it with `InputTextMultiline`.
- **Pictures**: a row's `image` (thumbnail) and an `image` field's value are
  absolute paths; return "" for missing media. Files are loaded at the size
  they're drawn at, a couple of new thumbnails per frame, the least recently
  drawn are dropped past a memory budget, and files that fail are tried again
  every few seconds, so artwork that appears while the screen is open shows
  up. A file replaced under the same name (new size or write time) is loaded
  again after an action runs, a screen opens or `generation()` changes (files
  aren't checked on a timer, so a slow network drive never stalls drawing),
  so "replace this logo" can keep the path; the old picture stays until the
  new one loads. Prefer small artwork for thumbnails (a box or logo) when you have a
  choice: big files still cost a decode each.
- **Leaving**: `leaving(screen)` is asked when the user leaves a screen with
  Back or Escape, and for every open screen when the manager closes or F10
  switches to Settings. Return a warning to stop them once; leaving the same
  way again calls `discard(screen)` and goes. The ways of leaving (Back,
  closing Manage, closing RetroFE's window) each warn once
  (`Manager/LeaveWarning.h`), so a window close after a Back warning warns
  again; for a window close "leave again to " in the warning becomes "close
  the window again to quit and ", so word warnings "... Save them, or leave
  again to discard them.".
- **Saving**: the convention for Save/Reset screens is in
  `Manager/EditBuffer.h`: `load()` in `enter()`, `get`/`set` through it,
  `isDirty` for `visibleWhen`/`enabledWhen`, `save` writes `changes()` and
  `commit()`s, `reset`/`discard()` call `reset()`, and `leaving()` warns
  while `isDirty()`.
- **Messages**: `Result::message` shows green when `ok`, red when not. Set
  `Result::info` when nothing happened (Undo/Reset, a cancel, "X is already
  in Y", an edit that wrote no files): it's shown plain (`infoColor`), not
  as a success or an error, whatever `ok` says (`Forms::statusKind`).
- **Confirming**: put `confirm="Delete this game?"` on the button; the action
  only runs on the second Select, so `runAction` doesn't need to ask.
- **Buttons that hide themselves** (`visibleWhen` that turns off once
  pressed): focus stays at the same place or moves up, and never lands on a
  file or folder field by itself when anything else can take it. If it has
  to, Select there does nothing until the user moves, so a second press
  doesn't open the file browser by surprise.
- **Folder and file fields** open the file browser (`Manager/Browser/`, form
  `file-browser`, prefix `browse`) on Select, never the system dialog. Its
  pick comes back through your `set()` on your own screen, as if typed, so
  nothing else is needed. Help it along with attributes on the field:
  `pattern` (the files listed), `start="yourprefix:someFolder"` (a value
  naming the folder it opens in while the field is empty; a slot's place
  like `.../logo/Sonic.*` works) and, on a `<folder>`,
  `matching="yourprefix:extensions"` (counted for the first 50 folders in list order). The
  browser keeps a value relative to RetroFE's folder when it was relative
  (`Browser::storedValue`); otherwise it gives an absolute path, as before.
  Filesystem listing and capped counts run on workers, publishing results
  through mutex-protected storage consumed by the UI thread. Workers own
  that storage and never access the provider or forms; shutdown does not wait
  for a drive. Drive details arrive independently. Search reuses lowercase
  listing keys and caches an unchanged query. Pictures above 8 MB have no
  browser thumbnail. The Windows dialog requires a real mouse press in Manage
  and use within 15 seconds; touch and motion after pointer warps are ignored.
- **A row that runs an action**: set `Row::action` (e.g. `"browse.pick:..."`)
  and choosing the row runs it instead of opening the field's form; put
  whatever the action needs in its name. **Picking for another screen's
  field**: `Result::pick` / `Result::pickFor` make the manager return past
  every screen of the current form and `set()` that binding there.
- **Letter jump**: `<results jump="letter">` makes Left/Right jump between
  first letters instead of paging (`Forms::letterJump`); the letter up and
  letter down controls do it in every results list.
- **Navigating after an action**: `Result::open` pushes a screen and
  `Result::back` returns (asking `leaving()` like Back does), after success
  only. The message is shown on the screen you end up on. A form id that
  doesn't exist shows an error on the current screen instead. Set
  `Result::replace` with `open` when the screen the action ran on is done
  with, e.g. an add form opening what it added: the new screen takes its
  place, so Back goes to the screen before the form, not to the filled-in form.
- **Press it now**: set `Result::capture` (a binding, e.g.
  `control:captured`) and `Result::captureAction` (e.g.
  `control.addCaptured`) on a successful result. `ManagerUI` then takes
  every input instead of moving around the screen (navigate() waits, the
  page and ImGui see nothing; RetroFE's own input still follows keys going
  up and down), shows a countdown as the status, and when an input is
  pressed and let go `set()`s its controls.conf token (`Control/InputToken.h`)
  on the binding and runs the action, whose result is handled as usual.
  Nothing pressed for 5 seconds, or an input held for 2, ends it with a
  message and nothing set. A game controller's press is taken by its
  controller name (`joyA`) rather than the joystick button SDL also reports.
  The rules are `Manager/Controls/InputCapture.h` (SDL-free, tested in
  `ControlsTests.cpp`); `Controls/app/CaptureEvents.cpp` turns SDL events into
  presses. Key presses posted to RetroFE's window (WM_KEYDOWN/WM_KEYUP, as
  the GUI checks do) are captured like real ones.
- **Reloading**: when RetroFE needs to reload to show a change (a new title,
  a moved game), call `services.requestReload()`: RetroFE reloads after the
  user closes the manager, so they can keep working. `Result::reload`
  instead closes the manager and reloads straight away; Settings' Save and
  Add a system use that, because the user expects to see the result at once.
- **Starting a game**: `services.requestLaunch(system, game)` from an action
  (Games' `game.launch`); check `services.launchProblem()` in
  `actionProblem` first: it refuses while a reload is due and a Catalog
  install is running (RetroFE can't reload then, and a game starting minutes
  later would be a surprise). After the action succeeds the manager closes,
  asking `leaving()` like Close does: if a screen objects, nothing starts and
  its warning shows; running the same action again leaves the changes and
  starts the game. RetroFE reloads first when changes are pending (the
  request is kept across that reload and dropped by any other shutdown),
  launches the game as its system's page would, and opens Manage > Games >
  that game again afterwards, with "Back from ..." or why it couldn't start
  (`Launcher::failure()`). The rules are `Manager/LaunchQueue.h`, tested in
  `GamesTests.cpp`. Both are null in tests unless a test sets them.
- `Manager::Services` (`Provider.h`) is what providers can use: the
  configuration, the Catalog, `requestReload`, `requestLaunch`,
  `reloadControls` (RetroFE re-reads controls*.conf and remaps, straight
  away), and the shared library:
  `library` (one `Library::Indexer` for every section, rebuilt each time the
  manager opens) and `editor` (the `Library::Editor` all writes go through).
  After a write, call `library->changed("what")`; when the manager closes with
  changes pending, RetroFE reloads. Don't create your own Indexer. If you need
  more, add a field there and set it where `ManagerUI::initialize` registers
  providers.

## Forms for a section

```xml
<form id="games" title="Games">
  <text label="Search" bind="game:query" live="yes" hint="Name, year or maker"/>
  <results bind="game:search" open="game" empty="No games match."/>
</form>

<form id="game" title="Game" bind="game:title">
  <image bind="game:media:logo" rows="4"/>
  <value label="Year" bind="game:year"/>
  <value label="Maker" bind="game:manufacturer"/>
  <link label="Media" open="game-media" status="game:mediaSummary"/>
  <link label="Edit details" open="game-edit"/>
  <button label="Delete game" action="game.delete" confirm="Delete this game?"/>
</form>

<form id="game-edit" title="Edit details" bind="game:title">
  <text label="Title" bind="game:title"/>
  <text label="Year" bind="game:year"/>
  <actions>
    <button label="Save" action="game.save"/>
    <button label="Reset" action="game.reset" visibleWhen="game:isDirty"/>
  </actions>
</form>
```

Details screens are mostly `value`s (hidden when empty) and `link`s, one per
"tile", each opening a deeper screen with the same parameters. Editing
screens use the usual fields bound to the provider, with Save and Reset.

## Testing

- `Tests/<Section>Tests.cpp`: build a `ManagerModel`, `addProvider` your
  provider, `setScreen("game", { { "collection", "Arcade" }, { "id", "pacman" } })`,
  then check `get`, `rows`, `actionProblem`, `runAction` (including
  `Result::open`/`back`) and `leaving`. `Tests/FormsTests.cpp` has an example
  with a fake provider.
- In RetroFE: results, links, values and images all draw in the built-in
  form page, driven by the arrow keys, Enter and Escape. The ImGui renderer
  is for development; test logic through the model rather than through it.

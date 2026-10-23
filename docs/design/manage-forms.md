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
  `{ "id": "pacman" }`) and where the user was on it. A `link`, a `results`
  row or an action's result pushes a screen; Back pops it and puts the focus
  back where it was. Back on the first screen closes the manager.
- `ManagerModel` answers every binding. Built-in prefixes (`settings`,
  `draft`, `info`, `param`, `package`, and the actions `collection.*`,
  `source.*`, `catalog.*`, `open.*`, `back`) are its own; any other prefix
  goes to the **provider** registered for it.
- Forms are drawn by `FormWidget` (layout pages, driven by RetroFE's controls)
  or `FormView` (Dear ImGui, `formsRenderer = imgui`). Both rebuild the screen
  every frame from the model, so values are always current, and both call the
  model every frame: keep `get()` and `rows()` cheap.

## Where things go

Each section has its own folder, `RetroFE/Source/Manager/<Section>/`
(`Games`, `Menus`, `Systems`, `Videos`, `Emulators`, `Themes`). CMake picks
up every `.cpp` and `.h` in these folders, so you don't edit `CMakeLists.txt`
to add files. Re-run the configure step after adding a file (the Visual
Studio generator only notices new files on the build after the one that
finds them).

| File | What |
|------|------|
| `Manager/<Section>/<Section>Forms.cpp` | The section's built-in forms, returned by `Forms::<section>Forms()` (declared in `Forms/BuiltinForms.h`). It exists already with a "Coming soon" placeholder for the section's form id: replace it and add more forms to the table. Also built into `retrofe_tests`, which parses every built-in form. |
| `Manager/<Section>/<Name>Provider.h/.cpp` | Your provider(s), implementing `Manager::Provider` (`Manager/Provider.h`). |
| `Manager/Providers.cpp` | Registration: one include line and one `model.addProvider(...)` line per provider, in your section's slots. Keep the blank lines between slots so sections added in parallel merge cleanly. |
| `Tests/<Section>Tests.cpp` | Unit tests, as a `static Test::Suite` (see `Tests/TestSupport.h`); every `Tests/*.cpp` is built in. To test a provider, add its `.cpp` files to `retrofe_tests` in `CMakeLists.txt` (keep them free of SDL and graphics code). |

The Systems placeholder keeps the old "Add a collection" fields (now "Add a
system", action `collection.create`); the Systems section can replace them
with its own. The Manage hub links to each section's first form
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
- **Parameters**: `id` for the record a screen is about (the default name a
  `results` row is passed as); other names camelCase. A pushed screen gets its
  parent's parameters plus the new ones, so a `link` from `game` to
  `game-media` keeps `id`.

## A provider

```cpp
class GameProvider : public Manager::Provider
{
public:
    explicit GameProvider(const Manager::Services& services) : config_(services.config) {}

    std::string prefix() const override { return "game"; }

    // The manager opened: data on disk may have changed while it was closed
    void refresh() override { matches_.clear(); searched_ = "\x01"; }

    // A screen became current (opened, or returned to with Back)
    void enter(const Manager::Screen& screen) override
    {
        if (screen.form == "game" && screen.param("id") != editing_) {
            load(screen.param("id"));   // into an edit buffer; keeps edits when it's the same game
        }
    }

    std::string get(const std::string& key, const Manager::Screen& screen) override;   // "title", "canSave"...
    void set(const std::string& key, const std::string& value, const Manager::Screen& screen) override;

    // <results bind="game:search">: at most limit rows, and how many match in all
    Forms::Rows rows(const std::string& key, size_t limit, const Manager::Screen& screen) override
    {
        if (query_ != searched_) { searched_ = query_; matches_ = search(query_); }   // once per change, not per frame
        Forms::Rows rows;
        rows.total = matches_.size();
        rows.items.assign(matches_.begin(), matches_.begin() + std::min(limit, matches_.size()));
        return rows;
    }

    std::string actionProblem(const std::string& action, const Manager::Screen& screen) override
    {
        if (action == "save") return dirty_ ? "" : "No changes to save.";
        return Provider::actionProblem(action, screen);   // "Unknown action game.<action>"
    }

    Forms::Model::Result runAction(const std::string& action, const Manager::Screen& screen) override
    {
        Forms::Model::Result result;
        result.message = actionProblem(action, screen);
        if (!result.message.empty()) return result;
        // ...write it...
        result.ok = true;
        result.message = "Saved.";
        result.back = true;            // or: result.open = "game-media"; result.params["tab"] = "logo";
        return result;
    }
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
- `get()`, `rows()`, `list()`, `options()` and `actionProblem()` run every
  frame while a screen shows them. Cache anything that reads files or a
  database, keyed by what it depends on (the search text, the id).
- `rows()` should honour `limit` (default 200, set per field) and set `total`;
  the model cuts longer lists anyway. A row's `image` is an optional absolute
  path to a thumbnail; `detail` is shown on the right.
- A search box is `<text bind="game:query" live="yes"/>`: `set("query", ...)`
  arrives on every keystroke, on a real or the on-screen keyboard. Cancelling
  the edit sets the old text back.
- An `image` field's value is an absolute path; it's hidden when empty or when
  the file can't be loaded, so return "" for missing media.
- `Result::open` pushes a screen (with `Result::params` added to the current
  ones) and `Result::back` returns, after success only. The message is shown
  on the screen you end up on. `Result::reload` closes the manager and reloads
  RetroFE; use it only when something RetroFE has already loaded changed
  (collections, menus, layouts).
- `Manager::Services` (`Provider.h`) is what providers can use: the
  configuration and the Catalog for now. If you need more (say RetroFE's
  metadata database), add a field there and set it where `ManagerUI`
  registers providers.

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
</form>
```

Details screens are mostly `value`s (hidden when empty) and `link`s, one per
"tile", each opening a deeper screen with the same `id`. Editing screens use
the usual fields bound to the provider and a Save button whose action goes
back when it succeeds.

## Testing

- `Tests/<Section>Tests.cpp`: build a `ManagerModel`, `addProvider` your
  provider, `setScreen("game", { { "id", "pacman" } })`, then check `get`,
  `rows`, `actionProblem` and `runAction` (including `Result::open`/`back`).
  `Tests/FormsTests.cpp` has an example with a fake provider.
- In RetroFE: results, links, values and images all draw in the built-in
  form page, driven by the arrow keys, Enter and Escape. The ImGui renderer
  is for development; test logic through the model rather than through it.

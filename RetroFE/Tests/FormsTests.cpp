/* This file is part of RetroFE.
 *
 * RetroFE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RetroFE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RetroFE.  If not, see <http://www.gnu.org/licenses/>.
 */

// Tests for the forms layer: SettingsFile, form parsing and ManagerModel.

#include "TestSupport.h"
#include "../Source/Builtin/Builtin.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Forms/Form.h"
#include "../Source/Forms/SettingsFile.h"
#include "../Source/Manager/EditBuffer.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Navigator.h"
#include "../Source/Manager/Provider.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    // Serves game:<key> and game.<action> like an entity module would
    class GameProvider : public Manager::Provider
    {
    public:
        std::string entered;   // the last screen entered
        Manager::EditBuffer edits;
        unsigned found = 0;    // what update() has found so far

        std::string prefix() const override { return "game"; }

        void enter(const Manager::Screen& screen) override
        {
            entered = screen.form + ":" + screen.param("id");
            if (screen.form == "game" && screen.param("id") != edits.id()) {
                edits.load(screen.param("id"), { { "notes", "" } });
            }
        }

        std::string leaving(const Manager::Screen& screen) override
        {
            return screen.form == "game" && edits.isDirty() ? "Unsaved changes." : "";
        }

        void discard(const Manager::Screen&) override { edits.reset(); }

        void update(float) override { found++; }
        unsigned generation() const override { return found; }

        std::string get(const std::string& key, const Manager::Screen& screen) override
        {
            return key == "title" ? "Game " + screen.param("id") : edits.get(key);
        }

        void set(const std::string& key, const std::string& value, const Manager::Screen&) override
        {
            edits.set(key, value);
        }

        Forms::Rows rows(const std::string& key, size_t limit, const Manager::Screen&) override
        {
            // "search" respects the limit; "careless" doesn't, nor give a total
            Forms::Rows rows;
            size_t count = key == "search" ? std::min<size_t>(limit, 5312) : 10;
            for (size_t i = 0; i < count; i++) {
                rows.items.push_back({ "g" + std::to_string(i), "Game " + std::to_string(i), "", "" });
            }
            rows.total = key == "search" ? 5312 : 0;
            return rows;
        }

        std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Manager::Screen&) override
        {
            if (key == "genres") return { { "shmup", "Shoot 'em up" } };
            return {};
        }

        std::string actionProblem(const std::string& action, const Manager::Screen& screen) override
        {
            if (action == "save") return screen.param("id").empty() ? "No game chosen." : "";
            return Provider::actionProblem(action, screen);
        }

        Forms::Model::Result runAction(const std::string& action, const Manager::Screen& screen) override
        {
            Forms::Model::Result result;
            result.message = actionProblem(action, screen);
            result.ok = result.message.empty();
            if (result.ok) {
                edits.commit();
                result.message = "Saved.";
                result.open = "game-media";
                result.params["tab"] = "logo";
                result.keepParams = true;
            }
            return result;
        }
    };

    class SettingsProvider : public GameProvider
    {
    public:
        std::string prefix() const override { return "settings"; }
    };
}

void runFormsTests()
{
    std::string error;

    // --- SettingsFile ---------------------------------------------------------

    run("edits keep comments, spacing and CRLF line endings", [&] {
        std::string root = makeRoot("comments");
        writeFile(root + "/settings.conf",
                  "# Display\r\n"
                  "horizontal = stretch # number of horizontal pixels, or use stretch\r\n"
                  "attractModeTime\t= 180\r\n");
        SettingsFile file(root);
        check(file.set("horizontal", "1600", "", error), "set horizontal");
        check(file.set("attractModeTime", "30", "", error), "set attractModeTime");
        checkEqual(readFile(root + "/settings.conf"),
                   "# Display\r\n"
                   "horizontal = 1600 # number of horizontal pixels, or use stretch\r\n"
                   "attractModeTime\t= 30\r\n",
                   "file content");
    });

    run("LF files stay LF and new keys are appended", [&] {
        std::string root = makeRoot("append");
        writeFile(root + "/settings.conf", "layout = Arcades\n");
        SettingsFile file(root);
        check(file.set("videoEnable", "no", "", error), "set videoEnable");
        checkEqual(readFile(root + "/settings.conf"), "layout = Arcades\nvideoEnable = no\n", "file content");
    });

    run("values are written to the override file they come from", [&] {
        std::string root = makeRoot("override");
        std::string settings = "horizontal = stretch\nvertical = stretch\n";
        writeFile(root + "/settings.conf", settings);
        writeFile(root + "/settings1.conf", "horizontal = 1280\n");
        SettingsFile file(root);
        checkEqual(file.get("horizontal"), "1280", "effective value");
        checkEqual(file.source("horizontal"), "settings1.conf", "source");
        checkEqual(file.source("vertical"), "settings.conf", "source of vertical");
        check(file.set("horizontal", "1600", "", error), "set horizontal");
        checkEqual(readFile(root + "/settings1.conf"), "horizontal = 1600\n", "settings1.conf");
        checkEqual(readFile(root + "/settings.conf"), settings, "settings.conf untouched");
    });

    run("settings_saved.conf wins over numbered files", [&] {
        std::string root = makeRoot("saved");
        writeFile(root + "/settings.conf", "layout = A\n");
        writeFile(root + "/settings3.conf", "layout = B\n");
        writeFile(root + "/settings_saved.conf", "layout = C\n");
        SettingsFile file(root);
        checkEqual(file.get("layout"), "C", "effective value");
        checkEqual(file.source("layout"), "settings_saved.conf", "source");
    });

    run("the last of duplicate lines is the one edited", [&] {
        std::string root = makeRoot("duplicates");
        writeFile(root + "/settings.conf", "rememberMenu = true\nother = 1\nrememberMenu = yes\n");
        SettingsFile file(root);
        checkEqual(file.get("rememberMenu"), "yes", "effective value");
        check(file.set("rememberMenu", "no", "", error), "set");
        checkEqual(readFile(root + "/settings.conf"), "rememberMenu = true\nother = 1\nrememberMenu = no\n", "file content");
    });

    run("a bare install's first save starts from the built-in defaults", [&] {
        std::string root = makeRoot("bare");
        SettingsFile file(root);
        check(!fs::exists(root + "/settings.conf"), "no settings.conf yet");
        check(file.set("horizontal", "1600", Builtin::settings, error), "set horizontal");
        std::string content = readFile(root + "/settings.conf");
        check(content.find("horizontal = 1600") != std::string::npos, "edited in place");
        check(content.find("horizontal = 1280") == std::string::npos, "built-in value replaced, not duplicated");
        check(content.find("homeMenu = yes") != std::string::npos, "built-in defaults kept");
    });

    run("a missing override file isn't created", [&] {
        std::string root = makeRoot("no-overrides");
        writeFile(root + "/settings.conf", "layout = A\n");
        SettingsFile file(root);
        check(file.set("layout", "B", "", error), "set");
        check(!fs::exists(root + "/settings1.conf"), "no settings1.conf");
        check(!fs::exists(root + "/settings.conf.tmp"), "no temp file left");
    });

    // --- Form parsing ---------------------------------------------------------

    run("built-in forms parse", [&] {
        std::vector<std::string> ids = Forms::builtinFormIds();
        for (const char* id : { "settings", "manage", "sources", "package",
                                "games", "menus", "systems", "videos", "emulators", "themes" }) {
            check(std::find(ids.begin(), ids.end(), id) != ids.end(), std::string(id) + " is built in");
        }
        for (const std::string& id : ids) {
            auto form = Forms::parse(Forms::builtinForm(id), error);
            check(form != nullptr, id + ": " + error);
            if (form) {
                checkEqual(form->id, id, "id");
                check(!form->fields.empty(), id + " has fields");
            }
        }
    });

    run("results, link, value and image elements", [&] {
        auto form = Forms::parse(R"(
            <form id="t">
              <text label="Search" bind="game:query" live="yes"/>
              <results bind="game:search" open="game" empty="No games match" limit="50"/>
              <results bind="game:byYear" open="year" param="year"/>
              <section label="Details">
                <link label="Media" open="game-media" status="game:mediaSummary"/>
                <value label="Year" bind="game:year"/>
                <image bind="game:media:logo" rows="6"/>
              </section>
            </form>)", error);
        check(form != nullptr, "parsed: " + error);
        if (!form) return;
        const Forms::Field& search = form->fields[0];
        const Forms::Field& results = form->fields[1];
        check(search.live && !results.live, "live text");
        check(results.type == Forms::FieldType::Results, "results type");
        checkEqual(results.open, "game", "results open");
        checkEqual(results.param, "id", "param defaults to id");
        checkEqual(results.empty, "No games match", "empty text");
        check(results.limit == 50 && form->fields[2].limit == 200, "limit, and its default");
        checkEqual(form->fields[2].param, "year", "param");
        const std::vector<Forms::Field>& details = form->fields[3].children;
        check(details.size() == 3 && details[0].type == Forms::FieldType::Link
              && details[1].type == Forms::FieldType::Value && details[2].type == Forms::FieldType::Image, "link, value and image types");
        if (details.size() != 3) return;
        check(details[0].open == "game-media" && details[0].status == "game:mediaSummary", "link open and status");
        check(details[2].rows == 6 && details[1].rows == 4, "image rows, and the default");

        // Focus is remembered by position, sections' children included
        checkEqual(std::to_string(Forms::fieldIndex(*form, &details[1])), "5", "position of a field in a section");
        check(Forms::fieldAt(*form, 5) == &details[1] && Forms::fieldAt(*form, 3) == &form->fields[3], "field at a position");
        check(Forms::fieldAt(*form, 7) == nullptr && Forms::fieldIndex(*form, nullptr) == -1, "out of range");
    });

    run("a results list says when it isn't showing everything", [&] {
        Forms::Rows rows;
        rows.items.resize(200);
        rows.total = 200;
        checkEqual(Forms::rowsNote(rows), "", "all shown");
        rows.total = 5312;
        checkEqual(Forms::rowsNote(rows), "Showing 200 of 5,312. Refine the search to see the rest.", "some shown");
        rows.total = 1234567;
        check(Forms::rowsNote(rows).find("of 1,234,567.") != std::string::npos, "millions");
    });

    run("what an opened screen's parameters are", [&] {
        Forms::Field results;
        results.type = Forms::FieldType::Results;
        results.param = "id";
        Forms::Row row{ "pacman", "Pac-Man", "", "", { { "collection", "Arcade" } } };
        Forms::Params opened = Forms::rowParams(results, row);
        check(opened.size() == 2 && opened.at("id") == "pacman" && opened.at("collection") == "Arcade", "a row passes its id and params");
        row.params["id"] = "other";
        checkEqual(Forms::rowParams(results, row).at("id"), "other", "the row's own id param wins");
        Forms::Row sameId{ "pacman", "", "", "", { { "collection", "Namco" } } };
        check(Forms::rowKey(Forms::Row{ "pacman", "", "", "", { { "collection", "Arcade" } } }) != Forms::rowKey(sameId), "rows are told apart by their params too");

        // A row opens only what it says; a link keeps the screen's params
        Forms::Params current = { { "id", "dkong" }, { "tab", "logo" } };
        Forms::Model::Result fromRow;
        fromRow.params = { { "id", "pacman" } };
        check(Forms::openParams(current, fromRow) == fromRow.params, "nothing leaks from the current screen");
        Forms::Model::Result fromLink;
        fromLink.keepParams = true;
        check(Forms::openParams(current, fromLink) == current, "a link keeps them");
        fromLink.params = { { "tab", "video" } };
        checkEqual(Forms::openParams(current, fromLink).at("tab"), "video", "and what it adds wins");
    });

    run("a button with confirm runs on the second Select", [&] {
        Forms::Field remove;
        remove.type = Forms::FieldType::Button;
        remove.confirm = "Delete this game?";
        Forms::Field save;
        save.type = Forms::FieldType::Button;
        Forms::Confirm confirm;
        check(confirm.select(save), "no confirm: runs at once");
        check(!confirm.select(remove) && confirm.armed() == &remove, "first Select asks");
        check(confirm.select(remove) && !confirm.armed(), "second Select runs");
        check(!confirm.select(remove), "asks again next time");
        confirm.cancel();
        check(!confirm.select(remove), "moving away disarmed it");
        check(confirm.select(save) && !confirm.armed(), "another button runs, and disarms");
    });

    run("an edit buffer knows when there's something to save", [&] {
        Manager::EditBuffer edits;
        edits.load("pacman", { { "title", "Pac-Man" }, { "year", "1980" } });
        check(!edits.isDirty() && edits.id() == "pacman", "clean when loaded");
        edits.set("title", "Puck Man");
        edits.set("notes", "");
        check(edits.isDirty() && edits.changes().size() == 1, "one change; an empty new value isn't one");
        edits.set("title", "Pac-Man");
        check(!edits.isDirty(), "setting it back isn't a change");
        edits.set("year", "1981");
        edits.reset();
        checkEqual(edits.get("year"), "1980", "reset");
        edits.set("year", "1981");
        edits.commit();
        check(!edits.isDirty() && edits.get("year") == "1981", "committed");
    });

    run("form elements, attributes and entities", [&] {
        auto form = Forms::parse(R"(
            <form id="t" title="Test">
              <section label="S">
                <text label="Args" bind="draft:a" default="&quot;%ITEM_FILEPATH%&quot;"/>
                <bogus/>
                <number label="N" bind="settings:n" min="1" max="9"/>
              </section>
              <note value="Hello"/>
            </form>)", error);
        check(form != nullptr, "parsed: " + error);
        if (!form) return;
        checkEqual(form->title, "Test", "title");
        check(form->fields.size() == 2, "section and note");
        const Forms::Field& section = form->fields[0];
        check(section.type == Forms::FieldType::Section, "section type");
        check(section.children.size() == 2, "unknown element skipped");
        checkEqual(section.children[0].defaultValue, "\"%ITEM_FILEPATH%\"", "entities decoded");
        check(section.children[1].min == 1 && section.children[1].max == 9, "min and max");
        checkEqual(form->fields[1].label, "Hello", "note value");
    });

    run("visibleWhen, bound notes and bound titles", [&] {
        auto form = Forms::parse(R"(<form id="t" title="Fallback" bind="draft:title">
              <note bind="draft:text"/>
              <button label="Go" action="x" visibleWhen="!draft:hide"/>
            </form>)", error);
        check(form != nullptr, "parsed: " + error);
        if (!form) return;
        std::string root = makeRoot("visible");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        checkEqual(Forms::title(*form, model), "Fallback", "title without a value");
        model.set("draft:title", "Bound");
        checkEqual(Forms::title(*form, model), "Bound", "bound title");
        model.set("draft:text", "hello");
        checkEqual(Forms::noteText(form->fields[0], model), "hello", "bound note");
        check(Forms::isVisible(form->fields[1], model), "visible");
        model.set("draft:hide", "yes");
        check(!Forms::isVisible(form->fields[1], model), "hidden");
    });

    run("invalid form XML is reported", [&] {
        check(Forms::parse("<form><text", error) == nullptr, "broken XML");
        check(Forms::parse("<layout/>", error) == nullptr, "not a form");
    });

    // --- ManagerModel ---------------------------------------------------------

    run("model resolves screen keys and keeps edits pending until saved", [&] {
        std::string root = makeRoot("model");
        writeFile(root + "/settings.conf", "horizontal = 1280\nhorizontal0 = 1920\nvideoEnable = true\n");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);

        checkEqual(model.get("settings:horizontal"), "1920", "horizontal0 wins for the main screen");
        checkEqual(model.actionProblem("settings.save"), "No changes to save.", "nothing to save");

        model.set("settings:horizontal", "1600");
        model.set("settings:videoEnable", "no");
        checkEqual(model.get("settings:horizontal"), "1600", "pending value shown");
        checkEqual(model.actionProblem("settings.save"), "", "can save");
        checkEqual(readFile(root + "/settings.conf"), "horizontal = 1280\nhorizontal0 = 1920\nvideoEnable = true\n", "nothing written yet");

        Forms::Model::Result result = model.runAction("settings.save");
        check(result.ok, "saved: " + result.message);
        checkEqual(readFile(root + "/settings.conf"), "horizontal = 1280\nhorizontal0 = 1600\nvideoEnable = false\n",
                   "written to horizontal0, true/false style kept");
    });

    run("setting a value back to what's saved isn't a change", [&] {
        std::string root = makeRoot("model-unchanged");
        writeFile(root + "/settings.conf", "videoEnable = yes\n");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        model.set("settings:videoEnable", "no");
        model.set("settings:videoEnable", "yes");
        checkEqual(model.actionProblem("settings.save"), "No changes to save.", "no pending changes");
    });

    run("form defaults: fallbacks for settings, initial values for drafts", [&] {
        std::string root = makeRoot("model-defaults");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        model.setDefault("settings:attractModeTime", "0");
        model.setDefault("draft:extensions", "zip");
        checkEqual(model.get("settings:attractModeTime"), "0", "settings fallback");
        checkEqual(model.get("draft:extensions"), "zip", "draft initial value");
        model.set("draft:extensions", "7z");
        model.setDefault("draft:extensions", "zip");
        checkEqual(model.get("draft:extensions"), "7z", "default doesn't overwrite input");
    });

    run("model reports where overridden settings come from", [&] {
        std::string root = makeRoot("model-source");
        writeFile(root + "/settings.conf", "layout = A\nhideMouse = no\n");
        writeFile(root + "/settings2.conf", "layout = B\n");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        checkEqual(model.get("info:source:settings:layout"), "settings2.conf", "override file named");
        checkEqual(model.get("info:source:settings:hideMouse"), "", "settings.conf isn't mentioned");
    });

    // --- Navigation -----------------------------------------------------------

    run("screens stack up, and Back returns to where you were", [&] {
        Navigator nav;
        check(nav.empty() && !nav.current(), "closed at first");
        nav.open("manage");
        Forms::Focus onGames;
        onGames.field = 2;
        onGames.scroll = 40.0f;
        nav.push("games", {}, onGames);
        Forms::Focus onRow;
        onRow.field = 1;
        onRow.item = "pacman";
        nav.push("game", { { "id", "pacman" } }, onRow);
        check(nav.depth() == 3 && nav.current()->form == "game" && nav.current()->params.at("id") == "pacman", "three deep");

        check(nav.back(), "back to games");
        const Navigator::Frame* games = nav.current();
        check(games->form == "games" && games->focus.field == 1 && games->focus.item == "pacman", "focus on the row that was chosen");
        check(nav.back() && nav.current()->focus.field == 2 && nav.current()->focus.scroll == 40.0f, "back to manage, scrolled as it was");
        check(!nav.back() && nav.empty(), "back from the first screen closes");

        // Opening starts again
        nav.open("manage");
        nav.push("games", {}, {});
        nav.open("sources");
        check(nav.depth() == 1 && nav.current()->form == "sources", "open starts a new stack");

        for (size_t i = 0; i < Navigator::maxDepth + 5; i++) {
            nav.push("game", { { "id", std::to_string(i) } }, {});
        }
        check(nav.depth() == Navigator::maxDepth, "depth is limited");
        check(nav.frames().front().form == "sources", "the first screen is kept");
        checkEqual(nav.current()->params.at("id"), std::to_string(Navigator::maxDepth + 4), "and the newest");
        while (nav.depth() > 1) {
            nav.back();
        }
        checkEqual(nav.current()->form, "sources", "so Back still ends where it started");
    });

    run("a screen can take the place of the one it came from", [&] {
        Navigator nav;
        nav.open("manage");
        Forms::Focus onList;
        onList.field = 3;
        nav.push("systems", {}, Forms::Focus{});
        nav.push("system-add", {}, onList);
        nav.replace("system", { { "id", "Arcade" } });
        check(nav.depth() == 3 && nav.current()->form == "system" && nav.current()->params.at("id") == "Arcade",
              "the new system's screen replaces the add form");
        check(nav.back() && nav.current()->form == "systems" && nav.current()->focus.field == 3,
              "Back goes to the list, where the user was");
        Navigator empty;
        empty.replace("manage", {});
        check(empty.depth() == 1 && empty.current()->form == "manage", "with nothing open it just opens");
    });

    run("the Settings key opens, switches to and closes Settings", [&] {
        Navigator nav;
        check(nav.toggle("settings") && nav.depth() == 1 && nav.current()->form == "settings", "opens");
        check(!nav.toggle("settings") && nav.empty(), "closes");
        nav.open("manage");
        nav.push("games", {}, {});
        check(nav.toggle("settings") && nav.depth() == 1 && nav.current()->form == "settings", "switches from another screen");
    });

    run("providers serve their prefix with the screen's parameters", [&] {
        std::string root = makeRoot("providers");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        auto owned = std::make_unique<GameProvider>();
        GameProvider* games = owned.get();
        check(model.addProvider(std::move(owned)), "added");
        check(!model.addProvider(std::make_unique<GameProvider>()), "a prefix is served once");
        check(!model.addProvider(std::make_unique<SettingsProvider>()), "built-in prefixes can't be taken");

        model.setScreen("game", { { "id", "pacman" } });
        checkEqual(games->entered, "game:pacman", "told about the screen");
        checkEqual(model.get("param:id"), "pacman", "param binding");
        checkEqual(model.get("param:missing"), "", "missing param");
        checkEqual(model.get("game:title"), "Game pacman", "get, with the params");
        model.set("game:notes", "Classic");
        checkEqual(games->edits.get("notes"), "Classic", "set, without the prefix");
        check(model.options("game:genres").size() == 1, "options");
        check(model.options("layouts").front().second == "Built-in", "built-in options still work");

        Forms::Rows rows = model.rows("game:search", 200);
        check(rows.items.size() == 200 && rows.total == 5312, "rows, limited by the provider");
        rows = model.rows("game:careless", 3);
        check(rows.items.size() == 3 && rows.total == 10, "rows over the limit are cut, and counted");
        check(model.rows("info:collections", 10).items.empty(), "no rows for other bindings");

        checkEqual(model.actionProblem("game.save"), "", "action allowed");
        checkEqual(model.actionProblem("game.fly"), "Unknown action game.fly", "unknown provider action");
        checkEqual(model.actionProblem("nobody.save"), "Unknown action nobody.save", "unknown prefix");

        // Unsaved changes: leaving warns; leaving anyway discards them
        Manager::Screen game{ "game", { { "id", "pacman" } } };
        checkEqual(model.leaving(game), "Unsaved changes.", "leaving with unsaved changes");
        checkEqual(model.leaving(Manager::Screen{ "games", {} }), "", "other screens don't mind");
        model.discard(game);
        checkEqual(model.leaving(game), "", "discarded");
        checkEqual(model.get("game:notes"), "", "back to the saved value");

        // An action moves on after success: the manager opens game-media with id and tab
        model.set("game:notes", "Classic");
        Forms::Model::Result saved = model.runAction("game.save");
        check(saved.ok && saved.open == "game-media" && saved.params.at("tab") == "logo", "action opens a screen");
        Forms::Params next = Forms::openParams(model.screen().params, saved);
        check(next.at("id") == "pacman" && next.at("tab") == "logo", "keepParams keeps the current params");
        checkEqual(model.leaving(game), "", "saved, so nothing to warn about");

        unsigned before = model.generation();
        model.update(0.016f);
        check(model.generation() != before, "update() runs, and changes show through generation()");

        model.setScreen("games", {});
        check(!model.runAction("game.save").ok, "the screen's params decide");

        model.setScreen("game", { { "id", "pacman" } });
        Forms::Model::Result opened = model.runAction("open.game-media");
        check(opened.ok && opened.open == "game-media" && opened.keepParams, "open.<form> works like a link");
        Forms::Model::Result back = model.runAction("back");
        check(back.ok && back.back && back.open.empty(), "back");
    });

    run("leaving Settings with unsaved changes warns once, then drops them", [&] {
        std::string root = makeRoot("settings-leaving");
        writeFile(root + "/settings.conf", "hideMouse = no\n");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        model.refresh();
        Manager::Screen settings{ "settings", {} };
        model.setScreen("settings", {});
        checkEqual(model.leaving(settings), "", "nothing changed: no warning");
        model.set("settings:hideMouse", "yes");
        check(!model.leaving(settings).empty(), "a changed setting warns");
        checkEqual(model.leaving(Manager::Screen{ "manage", {} }), "", "other screens don't mind");
        model.discard(settings);
        checkEqual(model.leaving(settings), "", "dropped");
        checkEqual(model.get("settings:hideMouse"), "no", "back to the saved value");
        checkEqual(readFile(root + "/settings.conf"), "hideMouse = no\n", "nothing was written");
    });
}

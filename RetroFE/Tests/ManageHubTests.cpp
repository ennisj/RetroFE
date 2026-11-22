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

// The Manage hub, Home and Settings: changes waiting for a reload, where
// RetroFE goes after one, focus after an action, screen sizes and short
// input names.

#include "TestSupport.h"
#include "../Source/Manager/HubProgress.h"
#include "../Source/Manager/Navigator.h"
#include "../Source/Control/InputToken.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Database/GlobalOpts.h"
#include "../Source/Forms/Form.h"
#include "../Source/Graphics/Component/ListIndex.h"
#include "../Source/Manager/AfterReload.h"
#include "../Source/Manager/Controls/ControlsProvider.h"
#include "../Source/Manager/Home.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/PendingChanges.h"
#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace Test;

static Test::Suite manageHub("manage hub", [] {
    std::string error;

    // --- Changes waiting for a reload --------------------------------------------

    run("hub: the first launcher comes before the first system", [] {
        std::string error;
        auto form = Forms::parse(Forms::builtinForm("manage"), error);
        check(form != nullptr, "hub parses: " + error);
        if (!form) return;
        const Forms::Field* launcher = Forms::findField(*form, "emulator-new");
        const Forms::Field* system = Forms::findField(*form, "system-add");
        check(launcher && system && launcher < system, "launcher link comes first");
        check(launcher && launcher->label == "Add your first launcher/emulator"
              && launcher->visibleWhen == "!info:hasLaunchers", "only until a launcher exists");
        check(system && system->visibleWhen == "!system:hasSystems", "only until a system exists");
        const Forms::Field* games = Forms::findField(*form, "system:firstGames");
        check(games && system && games < system && games->visibleWhen == "system:hasFirstEmptySystem"
              && games->open == "system-add-games", "games hint comes before the first system link and opens its screen");
        auto launchers = Forms::parse(Forms::builtinForm("emulators"), error);
        check(launchers && launchers->title == "Launchers", "screen name, with its existing id");
    });

    run("settings: captureMouse is a registered boolean, off by default", [] {
        const global_options::options_entry* capture = nullptr;
        for (const auto* option = global_options::s_option_entries; option->description; ++option) {
            if (option->name && std::string(option->name) == OPTION_CAPTUREMOUSE) capture = option;
        }
        check(capture && capture->type == global_options::option_type::BOOLEAN, "registered as BOOLEAN");
        check(capture && std::string(capture->defvalue) == "false", "default false");
    });

    run("hub: pending changes count each different change once", [] {
        Manager::PendingChanges none;
        check(none.count() == 0 && none.text().empty(), "nothing waiting, nothing said");

        Manager::PendingChanges saved;
        saved.library = { "game metadata", "game metadata", "added Arcade" };
        saved.libraryFlag = true;
        check(saved.count() == 2, "the same screen saved twice is one change");
        checkEqual(saved.text(), "2 changes apply when you close Manage", "plural");

        Manager::PendingChanges unnamed;
        unnamed.libraryFlag = true;
        checkEqual(unnamed.text(), "1 change applies when you close Manage", "a change without a description still counts");

        Manager::PendingChanges mixed;
        mixed.library = { "launcher mame" };
        mixed.libraryFlag = true;
        mixed.reloadAsked = true;   // "Use this theme"
        check(mixed.count() == 2, "library and a reload asked for");
        checkEqual(Manager::PendingChanges::appliedText(1), "Applied 1 change.", "applied, one");
        checkEqual(Manager::PendingChanges::appliedText(3), "Applied 3 changes.", "applied, several");
    });

    run("hub: closing requests a reload for each pending reason", [] {
        Manager::PendingChanges pending;
        check(!pending.needsReloadOnClose(), "nothing saved");
        pending.libraryFlag = true;
        check(pending.needsReloadOnClose(), "saved library edits");
        pending.libraryFlag = false;
        pending.reloadAsked = true;
        check(pending.needsReloadOnClose(), "an explicit or already queued reload");
        checkEqual(pending.text(), "1 change applies when you close Manage", "queued reload remains visible after library changes are cleared");

        pending.library = { "systems: Arcade", "systems: Arcade" };
        pending.reloadAsked = false;
        check(pending.count() == 1, "adding then deleting a system counts once");
    });

    run("hub: the model shows pending changes and applies them by reloading back to the hub", [] {
        std::string root = makeRoot("hub-pending");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        model.setScreen("manage", {});

        checkEqual(model.get("info:pendingChanges"), "", "no source: nothing waiting");
        checkEqual(model.get("info:hasPendingChanges"), "no", "for visibleWhen");
        checkEqual(model.actionProblem("changes.apply"), "Nothing is waiting to be applied.", "nothing to apply");

        Manager::PendingChanges pending;
        model.setPendingChanges([&] { return pending; });
        pending.library = { "added Arcade" };
        pending.libraryFlag = true;
        checkEqual(model.get("info:pendingChanges"), "1 change applies when you close Manage", "shown on every screen");
        checkEqual(model.get("info:hasPendingChanges"), "yes", "the hub's Apply row shows");
        checkEqual(model.actionProblem("changes.apply"), "", "can apply");

        Forms::Model::Result applied = model.runAction("changes.apply");
        check(applied.ok && applied.reload, "reloads now");
        checkEqual(applied.open, "manage", "then opens the hub again");
        checkEqual(applied.message, "Applied 1 change.", "and says so there");
    });

    run("hub: after a reload RetroFE reopens Manage, once", [] {
        Manager::AfterReload after;
        Manager::AfterReload::Screen screen;
        check(!after.takeReopen(screen), "nothing asked");

        after.reopen({ "manage", {}, "Applied 2 changes." });
        after.reloading();
        check(!after.takeReopen(screen), "not before the reload");
        after.shutdown();   // the old RetroFE goes away
        after.started();    // the new one comes up
        check(after.takeReopen(screen) && screen.form == "manage" && screen.message == "Applied 2 changes.", "Manage again, with the message");
        check(!after.takeReopen(screen), "only once");

        Manager::AfterReload quitting;
        quitting.reopen({ "manage", {}, "Applied 1 change." });
        quitting.shutdown();   // RetroFE quits without reloading
        check(!quitting.hasReopen(), "dropped when RetroFE goes away without reloading");
    });

    // --- Home and the first run -----------------------------------------------------

    run("home: Settings, Manage and Browse; lands on Browse when there's content, on Manage on a bare install", [] {
        check(sizeof(Home::items) / sizeof(Home::items[0]) == 3, "three items");
        check(std::string(Home::items[0]) == "Settings" && std::string(Home::items[1]) == "Manage"
              && std::string(Home::items[2]) == "Browse", "in that order");
        checkEqual(Home::items[Home::landing(true)], "Browse", "content");
        checkEqual(Home::items[Home::landing(false)], "Manage", "first run");
    });

    run("menus: repeatItems=\"no\" draws a short list once, around the selected position", [] {
        auto shown = [](size_t selected, size_t slots, size_t size) {
            std::string drawn;
            for (size_t slot = 0; slot < slots; slot++) drawn += showsMenuSlot(slot, selected, slots, size) ? 'x' : '.';
            return drawn;
        };
        checkEqual(shown(0, 5, 0), ".....", "no items: no positions drawn");
        checkEqual(shown(4, 9, 1), "....x....", "one system: just the selected position (Browse's Main menu)");
        checkEqual(shown(4, 9, 2), "....xx...", "two: the selected one and the next");
        checkEqual(shown(4, 9, 3), "...xxx...", "three: one either side (Home)");
        checkEqual(shown(4, 9, 4), "...xxxx..", "four: one before, two after");
        checkEqual(shown(4, 9, 5), "..xxxxx..", "five: two either side");
        checkEqual(shown(4, 9, 9), "xxxxxxxxx", "as many as positions: all of them");
        checkEqual(shown(4, 9, 30), "xxxxxxxxx", "more: all of them");
        checkEqual(shown(0, 5, 2), "xx...", "selected at the start");
        checkEqual(shown(0, 5, 4), "xxxx.", "even window shifts at the start");
        checkEqual(shown(4, 5, 4), ".xxxx", "even window shifts at the end");
        checkEqual(shown(0, 5, 3), "xxx..", "odd window shifts at the start");
        checkEqual(shown(4, 5, 3), "..xxx", "odd window shifts at the end");
        for (size_t selected : { size_t(0), size_t(4) }) {
            std::set<size_t> items;
            for (size_t slot = 0; slot < 5; slot++) {
                if (showsMenuSlot(slot, selected, 5, 4)) items.insert(loopIncrement(0, slot, 4));
            }
            check(items.size() == 4, "ScrollingList's consecutive slot indices show every item once");
        }
    });

    run("hub: a system added on another screen counts straight away", [] {
        std::string root = makeRoot("hub-first-run");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        model.refresh();
        model.setScreen("manage", {});
        checkEqual(model.get("info:hasCollections"), "no", "bare: the welcome and \"Add your first system\" show");
        writeFile(root + "/collections/Arcade/settings.conf", "list.extensions = zip\n");   // Systems > Add a system
        model.setScreen("system", { { "id", "Arcade" } });
        model.setScreen("manage", {});
        checkEqual(model.get("info:hasCollections"), "yes", "back on the hub, the welcome is gone");
    });

    // --- Focus after an action --------------------------------------------------------

    run("focus: never lands on a new or destructive button after an action", [] {
        using Forms::FocusTarget;
        Forms::Field link;
        link.type = Forms::FieldType::Link;
        Forms::Field undo;
        undo.type = Forms::FieldType::Button;
        Forms::Field remove = undo;
        remove.confirm = "Remove this package?";
        Forms::Field file;
        file.type = Forms::FieldType::File;

        check(Forms::refocusTarget(&link, true, false) == FocusTarget::Free, "a link, even a new one");
        check(Forms::refocusTarget(&undo, true, true) == FocusTarget::Free, "a button that was there before");
        check(Forms::refocusTarget(&undo, true, false) == FocusTarget::Guarded, "a button that just appeared (Show this game again)");
        check(Forms::refocusTarget(&remove, true, true) == FocusTarget::Guarded, "a button that asks for confirmation");
        check(Forms::refocusTarget(&file, true, true) == FocusTarget::Guarded, "a file field");
        check(Forms::refocusTarget(&link, false, true) == FocusTarget::None, "not focusable");

        // Install was pressed: [Back (was there), Stop (new), Remove (new, confirms)]
        std::vector<FocusTarget> rows = { Forms::refocusTarget(&undo, true, true), Forms::refocusTarget(&undo, true, false),
                                          Forms::refocusTarget(&remove, true, false) };
        check(Forms::refocusIndex(rows, 1) == 0, "Back, not Stop or Remove");
        // Hide was pressed and only "Show this game again" is left
        check(Forms::refocusIndex({ Forms::refocusTarget(&undo, true, false) }, 0) == 0, "the new button when it's all there is (Select waits for a move)");
    });

    run("focus: an action names its field by action, bind or open", [&] {
        auto form = Forms::parse(R"(<form id="t" title="T">
              <link label="Controls" open="controls"/>
              <section label="S">
                <choice label="Theme" bind="settings:layout" options="layouts"/>
                <button label="Start" action="game.launch"/>
              </section>
            </form>)", error);
        check(form != nullptr, "parsed: " + error);
        if (!form) return;
        const Forms::Field* start = Forms::findField(*form, "game.launch");
        check(start && start->label == "Start", "by action, inside a section");
        check(Forms::findField(*form, "settings:layout") && Forms::findField(*form, "controls"), "by bind and by open");
        check(!Forms::findField(*form, "") && !Forms::findField(*form, "nothing"), "nothing for an unknown key");
    });

    run("hub: launcher progress survives the screens a launcher is added on", [] {
        Manager::HubProgress progress;
        Navigator nav;
        nav.open("manage");
        check(!progress.focusSystem(true, false), "bare hub starts at launcher");
        nav.push("emulator-new", {}, {});
        nav.push("emulator", {{"id", "DuckStation"}, {"scope", "global"}}, {});
        nav.push("emulator-edit", {{"id", "DuckStation"}, {"scope", "global"}}, {});
        check(!progress.focusSystem(false, true), "a new launcher does not steal focus on its own screens");
        nav.back();
        nav.back();
        check(!progress.focusSystem(false, true), "still below hub");
        nav.back();
        check(nav.current()->form == "manage" && progress.focusSystem(true, true), "Back to hub focuses Add your first system");
        check(!progress.focusSystem(true, true), "only once");
        progress.reset();
        check(!progress.focusSystem(true, true), "existing launcher on a new visit does not move focus");
    });

    // --- Settings ----------------------------------------------------------------------

    run("settings: screen size is one choice over horizontal and vertical", [] {
        std::string root = makeRoot("hub-screen-size");
        writeFile(root + "/settings.conf", "horizontal = 1920\nvertical = 1080\n");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);

        checkEqual(model.get("settings:screenSize"), "1920x1080", "a size in the list");
        checkEqual(model.get("info:customScreenSize"), "no", "Width and Height hidden");
        model.set("settings:screenSize", "1280x720");
        checkEqual(model.get("settings:horizontal"), "1280", "sets the width");
        checkEqual(model.get("settings:vertical"), "720", "and the height");
        model.set("settings:screenSize", "stretch");
        check(model.get("settings:horizontal") == "stretch" && model.get("settings:vertical") == "stretch", "the screen's size");
        checkEqual(model.get("settings:screenSize"), "stretch", "shown as such");

        model.set("settings:screenSize", "custom");
        checkEqual(model.get("info:customScreenSize"), "yes", "Custom shows Width and Height");
        checkEqual(model.get("settings:horizontal"), "stretch", "with what's set, unchanged");
        model.set("settings:horizontal", "1000");
        model.set("settings:vertical", "700");
        checkEqual(model.get("settings:screenSize"), "custom", "stays Custom");
        Forms::Model::Result saved = model.runAction("settings.save");
        check(saved.ok, "saved: " + saved.message);
        checkEqual(readFile(root + "/settings.conf"), "horizontal = 1000\nvertical = 700\n", "written to the two settings");

        model.refresh();
        checkEqual(model.get("settings:screenSize"), "custom", "a size not in the list is Custom");
        auto options = model.options("screenSizes");
        check(!options.empty() && options.front().first == "stretch" && options.back().first == "custom", "the screen's size first, Custom last");
        check(std::any_of(options.begin(), options.end(), [](const auto& o) { return o.first == "1920x1080" && o.second == "1920 x 1080"; }),
              "sizes read as width x height");
    });

    run("settings: a settings.conf that still sets catalogPath starts as usual", [] {
        std::string root = makeRoot("hub-old-catalog-path");
        writeFile(root + "/settings.conf", "catalogPath = catalog\nlayout = Aeon Nox\n");
        Configuration::absolutePath = root;
        Configuration config;
        check(config.import("", root + "/settings.conf"), "read without complaint");
        std::string layout;
        check(config.getProperty("layout", layout) && layout == "Aeon Nox", "the settings around it apply");
        ManagerModel model(config);
        model.refresh();
        checkEqual(model.get("settings:layout"), "Aeon Nox", "Settings shows them");
        model.set("settings:layout", "Other");
        check(model.runAction("settings.save").ok, "and saves");
        check(readFile(root + "/settings.conf").find("catalogPath = catalog") != std::string::npos, "leaving the unknown line alone");
    });

    // --- Controls ------------------------------------------------------------------------

    run("controls: inputs in the list use short names", [] {
        checkEqual(Manager::ControlsProvider::describeShort(InputToken::parse("Return")), "Key Return", "a key");
        checkEqual(Manager::ControlsProvider::describeShort(InputToken::parse("joyA")), "Pad A", "a pad button");
        checkEqual(Manager::ControlsProvider::describeShort(InputToken::parse("joyDPUp")), "D-pad up", "a pad's D-pad");
        checkEqual(Manager::ControlsProvider::describeShort(InputToken::parse("joyLeftY-")), "Left stick up", "a pad's stick");
        checkEqual(Manager::ControlsProvider::describeShort(InputToken::parse("joyUnmappedButton0")), "Joy button 0 (unmapped)",
                   "an encoder button");
        checkEqual(Manager::ControlsProvider::describeValueShort("Return,joyA"), "Key Return, Pad A", "a value");
        checkEqual(Manager::ControlsProvider::describeValueShort(""), "No input", "none");
        checkEqual(InputToken::describe(InputToken::parse("joyUnmappedButton0")),
                   "Joystick button 0 (joysticks with no controller mapping)", "the input's own screen keeps the long words");
    });
});

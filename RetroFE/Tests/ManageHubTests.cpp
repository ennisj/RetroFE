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

// The Manage hub, Home, Catalog sources and Settings: changes waiting for a
// reload, where RetroFE goes after one, focus after an action, the source
// screen, screen sizes and short input names.

#include "TestSupport.h"
#include "../Source/Catalog/Installer.h"
#include "../Source/Catalog/Service.h"
#include "../Source/Control/InputToken.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Forms/Form.h"
#include "../Source/Manager/AfterReload.h"
#include "../Source/Manager/Controls/ControlsProvider.h"
#include "../Source/Manager/Home.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/PendingChanges.h"
#include "minizip/zip.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    bool makeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& entries)
    {
        fs::create_directories(fs::path(path).parent_path());
        zipFile zip = zipOpen64(path.c_str(), APPEND_STATUS_CREATE);
        if (!zip) return false;
        for (const auto& [name, content] : entries) {
            zip_fileinfo info{};
            if (zipOpenNewFileInZip64(zip, name.c_str(), &info, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0) != ZIP_OK) {
                zipClose(zip, nullptr);
                return false;
            }
            zipWriteInFileInZip(zip, content.data(), static_cast<unsigned>(content.size()));
            zipCloseFileInZip(zip);
        }
        return zipClose(zip, nullptr) == ZIP_OK;
    }

    Catalog::Targets targetsUnder(const std::string& root)
    {
        Catalog::Targets targets;
        targets.root = root;
        targets.collections = root + "/collections";
        targets.items = root + "/roms";
        targets.media = root + "/media";
        targets.layouts = root + "/layouts";
        targets.meta = root + "/meta";
        return targets;
    }

    // A root with a local index offering a collection package and a plain one
    std::string makeCatalogRoot(const std::string& name)
    {
        std::string root = makeRoot(name);
        makeZip(root + "/share/demo.zip", { { "settings.conf", "list.extensions = txt" } });
        makeZip(root + "/share/art.zip", { { "art.txt", "art" } });
        writeFile(root + "/share/index.json", R"({"packages": [
            {"id": "demo", "name": "Demo", "version": "1", "kind": "collection",
             "files": [{"url": "demo.zip", "target": "collections", "path": "Demo"}]},
            {"id": "art", "name": "Art", "version": "2", "files": [{"url": "art.zip", "target": "root"}]}
        ]})");
        return root;
    }

    Catalog::SourceConfig localSource(const std::string& root, const std::string& name = "local")
    {
        Catalog::SourceConfig source;
        source.name = name;
        source.type = "index";
        source.url = root + "/share/index.json";
        return source;
    }

    const Forms::Row* rowWithId(const Forms::Rows& rows, const std::string& id)
    {
        for (const Forms::Row& row : rows.items) {
            if (row.id == id) return &row;
        }
        return nullptr;
    }
}

static Test::Suite manageHub("manage hub", [] {
    std::string error;

    // --- Changes waiting for a reload --------------------------------------------

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
        mixed.catalog = true;       // an install finished
        check(mixed.count() == 3, "library, a reload asked for and the Catalog");
        checkEqual(Manager::PendingChanges::appliedText(1), "Applied 1 change.", "applied, one");
        checkEqual(Manager::PendingChanges::appliedText(3), "Applied 3 changes.", "applied, several");
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

    run("hub: after a reload RetroFE reopens Manage or shows a collection, once", [] {
        Manager::AfterReload after;
        Manager::AfterReload::Screen screen;
        std::string collection;
        check(!after.takeReopen(screen) && !after.takeBrowse(false, false, collection), "nothing asked");

        after.reopen({ "manage", {}, "Applied 2 changes." });
        after.browse("Demo");
        check(!after.takeBrowse(false, true, collection), "a collection waits for the reload that's due");
        after.reloading();
        check(!after.takeReopen(screen), "not before the reload");
        after.shutdown();   // the old RetroFE goes away
        after.started();    // the new one comes up
        check(after.takeReopen(screen) && screen.form == "manage" && screen.message == "Applied 2 changes.", "Manage again, with the message");
        check(!after.takeReopen(screen), "only once");
        check(!after.takeBrowse(true, false, collection), "not while the manager is open");
        check(after.takeBrowse(false, false, collection) && collection == "Demo", "then the collection");
        check(!after.takeBrowse(false, false, collection), "once");

        Manager::AfterReload quitting;
        quitting.browse("Demo");
        quitting.shutdown();   // RetroFE quits without reloading
        check(!quitting.hasBrowse(), "dropped when RetroFE goes away without reloading");

        Manager::AfterReload now;
        now.browse("Catalog");
        check(now.takeBrowse(false, false, collection) && collection == "Catalog", "without a reload, as soon as Manage has closed");
    });

    // --- Home and the first run -----------------------------------------------------

    run("home: lands on Browse when there's content, on Manage on a bare install, never the Catalog", [] {
        checkEqual(Home::items[Home::landing(true)], "Browse", "content");
        checkEqual(Home::items[Home::landing(false)], "Manage", "first run");
        checkEqual(Home::items[0], "Catalog", "the order stays as it was");
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

    // --- Catalog: sources and packages -----------------------------------------------

    run("catalog: sources are rows, each opening its own screen", [] {
        std::string root = makeCatalogRoot("hub-sources");
        writeFile(root + "/settings.conf", "homeMenu = yes\n");
        Configuration::absolutePath = root;
        Configuration config;
        Catalog::Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        ManagerModel model(config);
        model.setCatalog(&service);
        model.setScreen("sources", {});
        checkEqual(model.get("info:canOpenCatalog"), "no", "nothing to open before a source");
        checkEqual(model.actionProblem("catalog.open"), "The Catalog has no packages to show yet.", "so it can't be opened");

        model.set("draft:sourceName", "local");
        model.set("draft:sourceUrl", root + "/share/index.json");
        model.set("draft:sourceTrusted", "yes");
        Forms::Model::Result added = model.runAction("source.add");
        check(added.ok, "added: " + added.message);
        checkEqual(added.focus, "catalog.open", "focus goes to Open the Catalog once it shows");
        checkEqual(model.actionProblem("source.add"), "", "the cleared form doesn't nag for the next name");
        Forms::Model::Result empty = model.runAction("source.add");
        check(!empty.ok && empty.message == "Give the source a name.", "but pressing Add source again says why");
        model.set("draft:sourceName", "x");
        model.set("draft:sourceName", "");
        checkEqual(model.actionProblem("source.add"), "Give the source a name.", "typing a new one shows its problems again");
        service.waitUntilIdle();

        checkEqual(model.get("info:canOpenCatalog"), "yes", "packages to show, on Home");
        Forms::Model::Result open = model.runAction("catalog.open");
        check(open.ok && open.browse == "Catalog", "Open the Catalog shows the Catalog list");

        Forms::Rows sources = model.rows("info:sources", 50);
        const Forms::Row* local = rowWithId(sources, "local");
        check(local && local->label == "local" && local->detail == "2 packages", "a row per source with what it offers");

        model.setScreen("source", { { "id", "local" } });
        checkEqual(model.get("source:name"), "local", "its title");
        checkEqual(model.get("source:type"), "Index file", "type");
        checkEqual(model.get("source:location"), root + "/share/index.json", "where it reads from");
        check(model.get("source:status").rfind("Read at ", 0) == 0 && model.get("source:status").find("2 packages.") != std::string::npos,
              "when it was read and what it offers: " + model.get("source:status"));
        check(model.list("source:problems").empty(), "no problems");
        Forms::Rows packages = model.rows("source:packages", 50);
        const Forms::Row* demo = rowWithId(packages, "demo");
        check(packages.items.size() == 2 && demo && demo->label == "Demo" && demo->params.at("listed") == "yes",
              "its packages, each opening the package screen");

        Forms::Model::Result removed = model.runAction("source.removeThis");
        check(removed.ok && removed.back, "Remove this source goes back to Sources: " + removed.message);
        service.waitUntilIdle();
        check(service.sources().empty(), "removed");
        checkEqual(model.actionProblem("source.removeThis"), "This source has been removed.", "and can't be removed twice");
    });

    run("catalog: a source that can't be read says so on its screen", [&] {
        std::string root = makeCatalogRoot("hub-source-broken");
        Configuration::absolutePath = root;
        Configuration config;
        Catalog::Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        Catalog::SourceConfig usb = localSource(root, "usb");
        usb.url = root + "/usb/index.json";
        writeFile(root + "/usb/index.json", R"({"packages": []})");
        bool added = service.addSource(usb, error);
        check(added, "added: " + error);
        service.waitUntilIdle();
        fs::remove_all(root + "/usb");   // the stick was taken out
        service.refresh();
        service.waitUntilIdle();
        ManagerModel model(config);
        model.setCatalog(&service);
        Forms::Rows sources = model.rows("info:sources", 50);
        const Forms::Row* row = rowWithId(sources, "usb");
        check(row && row->detail == "Can't be read", "the row says it");
        model.setScreen("source", { { "id", "usb" } });
        check(model.get("source:status").rfind("Couldn't be read at ", 0) == 0, "the screen says why: " + model.get("source:status"));
        check(model.rows("source:packages", 50).items.empty(), "nothing offered");
    });

    run("catalog: installing focuses Show it in Browse, which reloads to the collection; no repeated status", [&] {
        std::string root = makeCatalogRoot("hub-install");
        Configuration::absolutePath = root;
        Configuration config;
        Catalog::Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        bool added = service.addSource(localSource(root), error);
        check(added, "source: " + error);
        service.waitUntilIdle();
        ManagerModel model(config);
        model.setCatalog(&service);

        model.setScreen("package", { { "id", "demo" } });
        checkEqual(model.get("package:canShow"), "no", "nothing to show before it's installed");
        Forms::Model::Result install = model.runAction("package.install");
        check(install.ok, "install started");
        checkEqual(install.focus, "package.showInBrowse", "focus waits for Show it in Browse, never Remove");
        service.waitUntilIdle();
        checkEqual(model.get("package:progress"), "Installed Demo 1.", "the outcome");
        checkEqual(model.get("package:installedStatus"), "", "not repeated as \"Installed.\"");
        checkEqual(model.get("package:isInstalled"), "yes", "installed");
        checkEqual(model.get("package:canShow"), "yes", "a collection: it can be shown");
        Forms::Model::Result show = model.runAction("package.showInBrowse");
        check(show.ok && show.reload && show.browse == "Demo", "reloads, then shows Demo");

        model.setScreen("package", { { "id", "art" } });
        check(model.runAction("package.install").ok, "art install started");
        service.waitUntilIdle();
        checkEqual(model.get("package:canShow"), "no", "not a collection: nothing to show");
        checkEqual(model.actionProblem("package.showInBrowse"), "It has no collection to show.", "so the action says why");
    });

    run("catalog: installed packages open their own screen, also when no source offers them now", [&] {
        std::string root = makeCatalogRoot("hub-installed");
        Configuration::absolutePath = root;
        Configuration config;
        Catalog::Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        bool added = service.addSource(localSource(root), error);
        check(added, "source: " + error);
        service.waitUntilIdle();
        service.install("art");
        service.waitUntilIdle();
        check(service.removeSource("local", error), "source removed: " + error);
        service.waitUntilIdle();

        ManagerModel model(config);
        model.setCatalog(&service);
        Forms::Rows installed = model.rows("info:installed", 50);
        const Forms::Row* art = rowWithId(installed, "art");
        check(art && art->label == "Art" && art->detail == "2" && art->params.at("listed") == "yes", "a row per installed package");

        model.setScreen("package", art ? Forms::rowParams(Forms::Field{}, *art) : Forms::Params{});
        checkEqual(model.get("package:name"), "Art", "its screen");
        check(model.get("package:summary").find("no source offers it now") != std::string::npos, "says no source offers it: " + model.get("package:summary"));
        checkEqual(model.get("package:canInstall"), "no", "can't be installed again");
        checkEqual(model.get("package:canRemove"), "yes", "but can be removed from here");
        checkEqual(model.get("package:checksum"), "", "no checksum note for what's already installed");
        check(model.runAction("package.remove").ok, "removing");
        service.waitUntilIdle();
        check(service.installed().empty(), "removed");
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

    // --- Controls ------------------------------------------------------------------------

    run("controls: inputs in the list use short names", [] {
        checkEqual(Manager::ControlsProvider::describeShort(InputToken::parse("Return")), "Key Return", "a key");
        checkEqual(Manager::ControlsProvider::describeShort(InputToken::parse("joyA")), "Pad A", "a pad button");
        checkEqual(Manager::ControlsProvider::describeShort(InputToken::parse("joyUnmappedButton0")), "Joy button 0 (unmapped)",
                   "an encoder button");
        checkEqual(Manager::ControlsProvider::describeValueShort("Return,joyA"), "Key Return, Pad A", "a value");
        checkEqual(Manager::ControlsProvider::describeValueShort(""), "No input", "none");
        checkEqual(InputToken::describe(InputToken::parse("joyUnmappedButton0")),
                   "Joystick button 0 (joysticks with no controller mapping)", "the input's own screen keeps the long words");
    });
});

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

// The Themes section (Manager/Themes/ThemeProvider) and the theme editor's
// controller (ThemeEditor/EditorSession): what the screens decide, on
// temporary layout folders.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Themes/ThemeProvider.h"
#include "../Source/ThemeEditor/EditorScript.h"
#include "../Source/ThemeEditor/EditorSession.h"
#include "../Source/ThemeEditor/LayoutCheck.h"
#include "../Source/Utility/Log.h"
#include <thread>
#include "../Source/ThemeEditor/ThemeStore.h"
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace Test;
using namespace ThemeEditor;

namespace
{
    std::string fontBytes()
    {
        return readFile(std::string(RETROFE_THIRD_PARTY_DIR) + "/fonts/Roboto-Regular.ttf");
    }

    // A root with a hand-made theme ("Hand", 1280x1024, an NES page and a
    // splash page) that settings.conf uses
    std::string makeThemesRoot(const std::string& name)
    {
        std::string root = makeRoot(name);
        writeFile(root + "/settings.conf", "# test\nlayout = Hand\nfullscreen = no\n");
        writeFile(root + "/layouts/Hand/layout.xml",
                  "<?xml version=\"1.0\"?>\n<!-- made by hand -->\n<layout width=\"1280\" height = '1024' font=\"x.ttf\">\n</layout>\n");
        writeFile(root + "/layouts/Hand/splash.xml", "<layout width=\"1280\" height=\"1024\"></layout>");
        writeFile(root + "/layouts/Hand/layout - 1.xml", "<layout width=\"1280\" height=\"1024\"></layout>");
        writeFile(root + "/layouts/Hand/collections/NES/layout/layout.xml", "<layout width=\"1280\" height=\"1024\"></layout>");
        writeFile(root + "/layouts/Hand/collections/_common/medium_artwork/logo/x.png", "png");
        writeFile(root + "/layouts/Empty/readme.txt", "nothing");
        return root;
    }

    struct Fixture
    {
        Configuration config;
        bool reloadRequested = false;
        std::unique_ptr<ManagerModel> model;
        Themes::ThemeProvider* provider = nullptr;

        explicit Fixture(const std::string& root)
        {
            model = std::make_unique<ManagerModel>(config);
            Manager::Services services{ config };
            services.requestReload = [this] { reloadRequested = true; };
            Themes::Paths paths;
            paths.root = root;
            paths.fontBytes = [] { return fontBytes(); };
            auto p = std::make_unique<Themes::ThemeProvider>(services, paths);
            provider = p.get();
            model->addProvider(std::move(p));
        }
    };

    const Forms::Row* findRow(const Forms::Rows& rows, const std::string& id)
    {
        for (const Forms::Row& row : rows.items) {
            if (row.id == id) return &row;
        }
        return nullptr;
    }

    // The canvas the session tests use: 1920x1080 drawn at half size
    CanvasFit halfFit() { return canvasFit(960.0f, 540.0f, 1920, 1080); }
    float stageX(float tx) { return themeToCanvasX(halfFit(), tx); }
    float stageY(float ty) { return themeToCanvasY(halfFit(), ty); }

    // A fresh editor-made theme, opened
    bool openNew(const std::string& root, const std::string& name, EditorSession& session, std::string& folder)
    {
        SaveInputs inputs;
        inputs.fontBytes = fontBytes();
        std::string error;
        const std::string layouts = root + "/layouts";
        if (!createTheme(layouts, name, false, Themes::widgetLibrary(), inputs, folder, error)) return false;
        return session.open(layouts, folder, Themes::widgetLibrary(), error);
    }
}

static Test::Suite themes("themes", [] {
    Test::run("a layout's size is read from its <layout> root", [] {
        int w = 0, h = 0;
        check(Themes::layoutSize("<?xml version=\"1.0\"?>\n<layout width=\"1920\" height=\"1080\">", w, h) && w == 1920 && h == 1080,
              "plain attributes");
        check(Themes::layoutSize("<!-- <layoutish> --><layout\n  font='a' height = '768' width='1366'>", w, h) && w == 1366 && h == 768,
              "single quotes, spaces and order");
        check(!Themes::layoutSize("<layout width=\"stretch\" height=\"stretch\">", w, h) && w == 0, "stretch is unknown");
        check(!Themes::layoutSize("<layouts width=\"10\" height=\"10\">", w, h), "only the layout element counts");
        check(!Themes::layoutSize("", w, h), "no root");
        check(Themes::mainPageProblem("<layout><menu id=\"a\"/></layout>").empty(), "a page with a menu is fine");
        check(!Themes::mainPageProblem("<layout><menus/><text/></layout>").empty(), "one without isn't");
        check(layoutBuildBlocker("<layout><menu><itemDefaults/><item/></menu></layout>").empty(), "a menu with item defaults builds");
        check(layoutBuildBlocker("<layout><menu><item/></menu></layout>").find("itemDefaults") != std::string::npos,
              "one without would crash PageBuilder, so it isn't built");
        check(!layoutBuildBlocker("<layout><text>").empty() && !layoutBuildBlocker("<page/>").empty(), "nor is broken XML");
    });

    Test::run("a theme without splash.xml can't be used (RetroFE wouldn't start)", [] {
        std::string root = makeThemesRoot("themes_nosplash");
        writeFile(root + "/layouts/NoSplash/layout.xml", "<layout width=\"1280\" height=\"720\"><menu/></layout>");
        Fixture f(root);
        f.model->setScreen("theme", { { "id", "NoSplash" } });
        std::string problem = f.model->actionProblem("theme.use");
        check(problem.find("splash.xml") != std::string::npos, "a hand-made one is refused: " + problem);
        Forms::Model::Result used = f.model->runAction("theme.use");
        check(!used.ok && Themes::configuredLayout(root) == "Hand" && !f.reloadRequested, "and nothing is written");

        // An editor-made one whose splash page went missing: saving puts it back
        f.model->setScreen("theme-new", {});
        f.model->set("theme:newName", "Wheel");
        check(f.model->runAction("theme.create").ok, "created");
        {
            EditorSession s;
            std::string error;
            SaveInputs inputs;
            inputs.fontBytes = fontBytes();
            SaveReport report;
            check(s.open(root + "/layouts", "Wheel", Themes::widgetLibrary(), error) && s.addWidget("wheel_vertical") &&
                  s.save(inputs, report, error), "a wheel: " + error);
        }
        fs::remove(root + "/layouts/Wheel/splash.xml");
        f.model->setScreen("theme", { { "id", "Wheel" } });
        check(f.model->actionProblem("theme.use").find("saving writes one") != std::string::npos,
              "the editor-made one says saving fixes it: " + f.model->actionProblem("theme.use"));
    });

    Test::run("a log tap sees this thread's warnings and errors whatever the log level", [] {
        std::vector<std::string> seen;
        {
            Logger::TapScope tap([&seen](Logger::Zone zone, const std::string& component, const std::string& message) {
                seen.push_back(std::string(zone == Logger::ZONE_ERROR ? "E" : "W") + component + ":" + message);
            });
            // The logger isn't initialised in the tests, like log = NONE: nothing would be written
            LOG_WARNING("Layout", "one");
            LOG_ERROR("Image", "two");
            LOG_INFO("Layout", "not a problem");
            std::thread other([] { LOG_ERROR("VideoPool", "another thread"); });
            other.join();
        }
        LOG_ERROR("Layout", "after the scope");
        check(seen.size() == 2 && seen[0] == "WLayout:one" && seen[1] == "EImage:two",
              "only this thread's warnings and errors, while in scope: " + std::to_string(seen.size()));
        check(Themes::pageBuildComponent("Layout") && Themes::pageBuildComponent("Video") &&
              !Themes::pageBuildComponent("CollectionInfoBuilder") && !Themes::pageBuildComponent("HiScores"),
              "lines from collection and database loading aren't the page's");
    });

    Test::run("editor scripts parse, and session commands run", [] {
        EditorCommand c = parseEditorCommand("  var VALUE  Hello   there  ");
        check(c.name == "var" && c.words.size() == 3 && c.words[0] == "VALUE" && c.tail(1) == "Hello   there" &&
              c.rest == "VALUE  Hello   there", "words, rest and tail");
        check(parseEditorCommand("# a comment").name.empty() && parseEditorCommand("   ").name.empty(), "comments and blanks");
        std::vector<EditorCommand> script = parseEditorScript("wait 3\r\n# note\r\nadd static_text\r\n\r\nmove 100 0\r\n");
        check(script.size() == 3 && script[0].name == "wait" && script[2].words.size() == 2, "a script, CRLF and all");

        std::string root = makeThemesRoot("session_script");
        EditorSession s;
        std::string folder, note;
        check(openNew(root, "Scripted", s, folder), "opened");
        check(runSessionCommand(s, parseEditorCommand("add static_text"), note) == CommandResult::Ok, "add: " + note);
        check(runSessionCommand(s, parseEditorCommand("move 100 -20"), note) == CommandResult::Ok &&
              s.selectedWidget()->x == 960 && s.selectedWidget()->y == 490, "move in theme units: " + note);
        check(runSessionCommand(s, parseEditorCommand("resize se 40 40"), note) == CommandResult::Ok &&
              s.selectedWidget()->w == 240 && s.selectedWidget()->h == 100, "resize: " + note);
        check(runSessionCommand(s, parseEditorCommand("move ten 0"), note) == CommandResult::Failed, "a bad number fails");
        check(runSessionCommand(s, parseEditorCommand("resize middle 1 1"), note) == CommandResult::Failed, "a bad handle fails");
        check(runSessionCommand(s, parseEditorCommand("var VALUE Two words"), note) == CommandResult::Ok &&
              s.selectedWidget()->varValue("VALUE", "") == "Two words", "var takes the rest of the line");
        check(runSessionCommand(s, parseEditorCommand("layer 30"), note) == CommandResult::Ok && s.selectedWidget()->layer == 19, "layer");
        check(runSessionCommand(s, parseEditorCommand("select -1"), note) == CommandResult::Ok && s.selected() == -1, "deselect");
        check(runSessionCommand(s, parseEditorCommand("click 1000 530"), note) == CommandResult::Ok && s.selected() == 0, "click: " + note);
        check(runSessionCommand(s, parseEditorCommand("save"), note) == CommandResult::NotSession, "save is the screen's");
        check(runSessionCommand(s, parseEditorCommand("undo"), note) == CommandResult::Ok, "undo");
    });

    Test::run("separate nudges are separate undo steps", [] {
        std::string root = makeThemesRoot("session_nudge");
        EditorSession s;
        std::string folder;
        check(openNew(root, "Nudged", s, folder) && s.addWidget("static_text"), "a widget");
        s.moveBy(1, 0);
        s.moveBy(1, 0);
        s.endCoalesce();   // the screen does this after a pause
        s.moveBy(1, 0);
        check(s.undo() && s.selectedWidget()->x == 862, "the last nudge undoes alone");
        check(s.undo() && s.selectedWidget()->x == 860, "the first run undoes together");
    });

    Test::run("a hand-made splash.xml inside an editor theme is kept", [] {
        std::string root = makeThemesRoot("themes_splash");
        std::string layouts = root + "/layouts";
        SaveInputs inputs;
        inputs.fontBytes = fontBytes();
        std::string folder, error;
        check(createTheme(layouts, "Splashy", false, Themes::widgetLibrary(), inputs, folder, error), "created");
        check(isEditorMarker(readFile(layouts + "/Splashy/splash.xml").substr(0, readFile(layouts + "/Splashy/splash.xml").find('\n'))),
              "the automatic splash page carries the marker");
        writeFile(layouts + "/Splashy/splash.xml", "<layout width=\"1920\" height=\"1080\"><text value=\"mine\" layer=\"1\"/></layout>");
        ThemeProject p;
        check(loadTheme(layouts, folder, p, error), "loaded");
        SaveReport report;
        check(saveTheme(layouts, folder, p, Themes::widgetLibrary(), inputs, &report, error), "saved: " + error);
        check(readFile(layouts + "/Splashy/splash.xml").find("mine") != std::string::npos, "the hand-made splash page is untouched");
    });

    Test::run("the Themes list shows every layout folder with its facts", [] {
        std::string root = makeThemesRoot("themes_list");
        Fixture f(root);
        f.model->setScreen("themes", {});
        Forms::Rows rows = f.model->rows("theme:list", 50);
        check(rows.total == 2 && rows.items.size() == 2, "two folders listed, got " + std::to_string(rows.total));
        const Forms::Row* hand = findRow(rows, "Hand");
        check(hand && hand->label == "Hand", "the hand-made theme is listed");
        check(hand && hand->detail.find("In use") != std::string::npos && hand->detail.find("1280 x 1024") != std::string::npos,
              "its row says it's in use and its size: " + (hand ? hand->detail : std::string()));

        f.model->setScreen("theme", { { "id", "Hand" } });
        checkEqual(f.model->get("theme:name"), "Hand", "name");
        checkEqual(f.model->get("theme:kind"), "Made by hand", "kind");
        checkEqual(f.model->get("theme:size"), "1280 x 1024, landscape", "size");
        checkEqual(f.model->get("theme:pages"), "Main page, 1 other main page (layout - 1), splash page, 1 collection page (NES)",
                   "pages: _common isn't a page");
        checkEqual(f.model->get("theme:isActive"), "yes", "active");
        checkEqual(f.model->get("theme:isEditor"), "no", "not editor-made");
        checkEqual(f.model->get("theme:isHandMade"), "yes", "hand-made");
        check(f.model->get("theme:editNote").find("made by hand") != std::string::npos, "the note says why it can't be edited");
        check(!f.model->actionProblem("theme.edit").empty(), "a hand-made theme can't be edited");
        check(!f.model->actionProblem("theme.duplicate").empty(), "or duplicated");
        check(!f.model->actionProblem("theme.delete").empty(), "or deleted");
        check(!f.model->actionProblem("theme.use").empty(), "the theme in use can't be used again");
        Forms::Model::Result refused = f.model->runAction("theme.delete");
        check(!refused.ok && fs::exists(root + "/layouts/Hand/layout.xml"), "deleting it is refused and nothing is touched");

        f.model->setScreen("theme", { { "id", "Empty" } });
        check(!f.model->get("theme:problem").empty(), "a folder without layout.xml says RetroFE can't use it");
        check(!f.model->actionProblem("theme.use").empty(), "and it can't be used");
    });

    Test::run("New theme creates an editor-made theme and opens it", [] {
        std::string root = makeThemesRoot("themes_new");
        Fixture f(root);
        f.model->setScreen("theme-new", {});
        checkEqual(f.model->get("theme:newName"), "New Theme 1", "a free name is suggested");
        f.model->set("theme:newName", "   ");
        check(!f.model->actionProblem("theme.create").empty(), "a blank name is refused");
        f.model->set("theme:newName", "My Theme");
        f.model->set("theme:newOrientation", "portrait");
        Forms::Model::Result created = f.model->runAction("theme.create");
        check(created.ok, "created: " + created.message);
        checkEqual(created.open, "theme", "the new theme's details open");
        checkEqual(created.params["id"], "My Theme", "with its folder");
        check(fs::exists(root + "/layouts/My Theme/editor/project.xml"), "the project is saved");

        f.model->setScreen("theme", { { "id", "My Theme" } });
        checkEqual(f.model->get("theme:kind"), "Made with the theme editor", "kind");
        checkEqual(f.model->get("theme:size"), "1080 x 1920, portrait", "portrait size");
        checkEqual(f.model->get("theme:widgets"), "0 widgets on 1 page", "widget count");
        checkEqual(f.model->get("theme:canEdit"), "yes", "editable");
        checkEqual(f.model->get("theme:editNote"), "", "no note");
        Forms::Model::Result edit = f.model->runAction("theme.edit");
        check(edit.ok && edit.open == Themes::editorForm && edit.keepParams, "Edit opens the editor screen for this theme");

        f.model->setScreen("theme-new", {});
        f.model->set("theme:newName", "My Theme");
        Forms::Model::Result second = f.model->runAction("theme.create");
        check(second.ok && second.params["id"] == "My Theme 2", "a second theme with the same name gets a free folder");
    });

    Test::run("Use, Duplicate and Delete", [] {
        std::string root = makeThemesRoot("themes_actions");
        Fixture f(root);
        f.model->setScreen("theme-new", {});
        f.model->set("theme:newName", "Mine");
        check(f.model->runAction("theme.create").ok, "created");

        check(fs::exists(root + "/layouts/Mine/splash.xml"), "a new theme gets a splash page (RetroFE won't start without one)");
        check(readFile(root + "/layouts/Mine/splash.xml").find("value=\"Mine\"") != std::string::npos, "showing its name");

        f.model->setScreen("theme", { { "id", "Mine" } });
        check(f.model->actionProblem("theme.use").find("no wheel") != std::string::npos,
              "a theme without a wheel can't be used: " + f.model->actionProblem("theme.use"));
        {
            EditorSession s;
            std::string error;
            SaveInputs inputs;
            inputs.fontBytes = fontBytes();
            SaveReport report;
            check(s.open(root + "/layouts", "Mine", Themes::widgetLibrary(), error) && s.addWidget("wheel_vertical") &&
                  s.save(inputs, report, error), "a wheel is added: " + error);
        }
        f.model->setScreen("theme", { { "id", "Mine" } });
        check(f.model->actionProblem("theme.use").empty(), "now it can be used");
        Forms::Model::Result used = f.model->runAction("theme.use");
        check(used.ok && !used.reload, "use: " + used.message);
        check(f.reloadRequested, "RetroFE reloads once the manager closes");
        checkEqual(Themes::configuredLayout(root), "Mine", "settings.conf now names it");
        check(readFile(root + "/settings.conf").find("fullscreen = no") != std::string::npos, "other settings are kept");
        checkEqual(f.model->get("theme:isActive"), "yes", "it's the active one now");
        check(!f.model->actionProblem("theme.delete").empty(), "the theme in use can't be deleted");

        Forms::Model::Result copy = f.model->runAction("theme.duplicate");
        check(copy.ok && copy.open == "theme" && copy.params["id"] == "Mine Copy", "duplicate opens the copy: " + copy.message);
        check(probeTheme(root + "/layouts", "Mine Copy") == TF_EDITOR, "the copy is editor-made");

        f.model->setScreen("theme", { { "id", "Mine Copy" } });
        check(f.model->actionProblem("theme.delete").empty(), "the copy can be deleted");
        Forms::Model::Result deleted = f.model->runAction("theme.delete");
        check(deleted.ok && deleted.back, "delete goes back to the list");
        check(!fs::exists(root + "/layouts/Mine Copy"), "the folder is gone");
        f.model->setScreen("themes", {});
        check(!findRow(f.model->rows("theme:list", 50), "Mine Copy"), "and it's no longer listed");
        check(fs::exists(root + "/layouts/Hand/layout.xml"), "the hand-made theme is untouched");
    });

    Test::run("the editor session opens only editor-made themes", [] {
        std::string root = makeThemesRoot("session_open");
        EditorSession session;
        std::string error;
        check(!session.open(root + "/layouts", "Hand", Themes::widgetLibrary(), error) && !session.isOpen(),
              "a hand-made theme is refused");
        check(error.find("wasn't made with the theme editor") != std::string::npos, "with a reason: " + error);
        check(!session.open(root + "/layouts", "Nope", Themes::widgetLibrary(), error), "a missing folder is refused");
        check(!session.open(root + "/layouts", "../Hand", Themes::widgetLibrary(), error), "a path is refused");
        std::string folder;
        check(openNew(root, "Fresh", session, folder) && session.isOpen(), "a new theme opens");
        check(!session.dirty() && !session.canUndo() && session.pageCount() == 1, "clean, nothing to undo, one page");
        checkEqual(session.pageLabel(0), "Default", "the Default page");
    });

    Test::run("the editor session: add, select, move, resize, click-through", [] {
        std::string root = makeThemesRoot("session_edit");
        EditorSession s;
        std::string folder;
        check(openNew(root, "Edit Me", s, folder), "opened");
        const CanvasFit fit = halfFit();

        check(s.addWidget("static_text"), "a widget is added");
        check(!s.addWidget("no_such_widget"), "an unknown widget isn't");
        check(s.selected() == 0 && s.selectedWidget(), "the new widget is selected");
        const WidgetInstance first = *s.selectedWidget();
        check(first.x == 860 && first.y == 510 && first.w == 200 && first.h == 60, "it's centred at its default size");
        checkEqual(first.id, "static_text_1", "it has the next id");
        check(s.dirty(), "the theme is dirty");

        // Drag the body: the wireframe follows, the widget changes on release, snapped
        s.pointerDown(fit, stageX(960), stageY(540), 6.0f);
        check(s.dragging() && s.dragHandle() == CH_BODY, "pressing on the selection starts a move");
        s.pointerMove(fit, stageX(960) + 50.0f, stageY(540) + 11.0f);
        check(s.dragMoved() && s.dragRect().x == 960 && s.dragRect().y == 530, "the wireframe moves 100,22 theme units, snapped to 100,20");
        check(s.selectedWidget()->x == 860, "the project doesn't change while dragging");
        unsigned before = s.revision();
        s.pointerUp();
        check(s.selectedWidget()->x == 960 && s.selectedWidget()->y == 530, "release commits the move");
        check(s.revision() != before, "and the preview is told to rebuild");

        check(s.undo() && s.selectedWidget()->x == 860 && s.selectedWidget()->y == 510, "undo puts it back");
        check(s.redo() && s.selectedWidget()->x == 960, "redo moves it again");

        // Escape during a drag changes nothing
        s.pointerDown(fit, stageX(1000), stageY(550), 6.0f);
        s.pointerMove(fit, stageX(1000) + 100.0f, stageY(550));
        s.cancelDrag();
        s.pointerUp();
        check(s.selectedWidget()->x == 960, "a cancelled drag changes nothing");

        // A second widget on top of the first: click-through cycles between them
        check(s.addWidget("static_text") && s.selected() == 1, "a second widget is added and selected");
        check(s.setRect(960, 530, 200, 60), "placed exactly over the first");
        s.pointerDown(fit, stageX(1000), stageY(550), 6.0f);
        s.pointerUp();
        check(s.selected() == 0, "clicking the selection selects the one under it");
        s.pointerDown(fit, stageX(1000), stageY(550), 6.0f);
        s.pointerUp();
        check(s.selected() == 1, "and clicking again wraps round");
        s.pointerDown(fit, stageX(100), stageY(100), 6.0f);
        s.pointerUp();
        check(s.selected() == -1, "clicking empty canvas clears the selection");
        s.pointerDown(fit, stageX(1000), stageY(550), 6.0f);
        check(s.selected() == 1 && s.dragging(), "pressing on an unselected widget selects the top one and starts moving it");
        s.pointerUp();

        // Resize from the south-east handle
        s.select(0);
        s.pointerDown(fit, stageX(1160), stageY(590), 6.0f);
        check(s.dragHandle() == CH_SE, "the corner is the south-east handle");
        s.pointerMove(fit, stageX(1160) + 20.0f, stageY(590) + 21.0f);
        s.pointerUp();
        check(s.selectedWidget()->w == 240 && s.selectedWidget()->h == 100 && s.selectedWidget()->x == 960,
              "resized by 40,42 snapped to 40,40, the far corner anchored");
        // ... and from the north-west one, past the opposite edge: floors at the widget's minimum
        s.pointerDown(fit, stageX(960), stageY(530), 6.0f);
        check(s.dragHandle() == CH_NW, "the north-west handle");
        s.pointerMove(fit, stageX(960) + 400.0f, stageY(530) + 400.0f);
        s.pointerUp();
        const WidgetDef* def = s.selectedDef();
        check(def && s.selectedWidget()->w == def->minW && s.selectedWidget()->h == def->minH,
              "the size floors at the definition's minimum");
        check(s.selectedWidget()->x + s.selectedWidget()->w == 1200, "the right edge stays put");

        // Snapping off moves by whole units
        s.snap = false;
        s.pointerDown(fit, stageX(1190), stageY(620), 1.0f);
        s.pointerMove(fit, stageX(1190) - 3.5f, stageY(620));
        s.pointerUp();
        check(s.selectedWidget()->x == 1200 - def->minW - 7, "without snapping the move is exact");
        s.snap = true;

        // A widget can't be dragged off the canvas
        s.select(1);
        s.pointerDown(fit, stageX(1000), stageY(550), 1.0f);
        s.pointerMove(fit, stageX(1000) + 2000.0f, stageY(550) + 2000.0f);
        s.pointerUp();
        check(s.selectedWidget()->x == 1720 && s.selectedWidget()->y == 1020, "it stops at the canvas edge");

        check(s.moveBy(-20, 0) && s.selectedWidget()->x == 1700, "the keyboard nudges by whole units");
    });

    Test::run("the editor session: properties, undo steps and saving", [] {
        std::string root = makeThemesRoot("session_props");
        EditorSession s;
        std::string folder;
        check(openNew(root, "Props", s, folder), "opened");
        s.addWidget("static_text");

        // Typing in a field is one undo step
        s.setCoalesceKey("name");
        check(s.setName("A") && s.setName("AB") && s.setName("ABC"), "the name changes as it's typed");
        s.endCoalesce();
        check(s.undo() && s.selectedWidget()->name == "Static Text 1", "one undo takes the whole edit back");
        check(s.redo() && s.selectedWidget()->name == "ABC", "redo puts it back");
        check(!s.setName("ABC"), "setting the same value isn't a change");

        check(s.setLayer(25) && s.selectedWidget()->layer == MAX_LAYER, "layers clamp to 19");
        check(s.setLayer(-3) && s.selectedWidget()->layer == 0, "and to 0");
        check(s.setAlpha(40) && s.selectedWidget()->alphaPct == 40, "alpha");
        check(s.setAlpha(150) && s.selectedWidget()->alphaPct == 100, "alpha clamps to 100");
        check(s.setFontColor("#ff8800") && s.selectedWidget()->fontColor == "FF8800", "a colour is normalised");
        check(!s.setFontColor("orange"), "a non-colour is refused");
        check(s.setFontColor("") && s.selectedWidget()->fontColor.empty(), "and can be cleared");

        check(s.setVar("CENTER", "yes") && s.selectedWidget()->varValue("CENTER", "") == "true", "a bool var");
        check(s.setVar("VALUE", "Hello <world>"), "a text var");
        check(!s.setVar("NOPE", "1"), "an unknown var is refused");
        check(s.resetVar("CENTER") && s.selectedWidget()->varValue("CENTER", "false") == "false", "reset to the default");

        check(s.addWidget("wheel_vertical"), "a wheel");
        check(!s.setVar("IMAGETYPE", "nope"), "a dropdown only takes its options");
        check(s.setVar("IMAGETYPE", "marquee"), "an option");
        check(s.setVar("MODE", "layout") && s.setVar("MODE", "-") &&s.selectedWidget()->varValue("MODE", "x").empty(), "'-' is the empty option");
        check(s.setVar("WHEELFONTSIZE", "500") && s.selectedWidget()->varValue("WHEELFONTSIZE", "") == "200", "a number clamps");
        check(!s.setVar("WHEELFONTSIZE", "big"), "a number must be a number");

        const WidgetDef* wheel = s.selectedDef();
        check(wheel && varKind(*wheel->var("WHEELFONTCOLOR")) == VarKind::Colour, "colour vars are recognised");
        check(wheel && varKind(*wheel->var("IMAGETYPE")) == VarKind::Choice, "dropdowns");
        check(wheel && varKind(*wheel->var("WHEELFONTSIZE")) == VarKind::Number, "numbers");
        check(wheel && varKind(*wheel->var("TEXTFALLBACK")) == VarKind::Bool, "bools");

        // The preview is the generated page, pointed at the theme's folder
        GenOptions show;
        GenResult preview = s.previewXml(show);
        check(preview.ok() && preview.xml.find("id=\"static_text_1\"") != std::string::npos &&
              preview.xml.find("id=\"wheel_vertical_1\"") != std::string::npos, "the preview has both widgets");
        show.includeMedia = false;
        check(s.previewXml(show).xml.find("wheel_vertical_1") == std::string::npos, "the Show toggles filter it");
        check(s.setCanvasHidden(true), "a widget can be hidden on the canvas");
        check(s.previewXml(GenOptions()).xml.find("wheel_vertical_1") == std::string::npos, "and the preview skips it");
        s.setCanvasHidden(false);

        // Saving
        SaveInputs inputs;
        inputs.fontBytes = fontBytes();
        SaveReport report;
        std::string error;
        check(s.save(inputs, report, error), "saved: " + error);
        check(!s.dirty(), "clean after saving");
        bool wroteLayout = false;
        for (const std::string& w : report.written) wroteLayout |= w == "layout.xml";
        check(wroteLayout, "the report lists layout.xml");
        std::string saved = readFile(root + "/layouts/" + folder + "/layout.xml");
        check(saved.find("wheel_vertical_1") != std::string::npos, "the page on disk has the wheel");
        check(s.undo() && s.dirty(), "undoing past the save makes it dirty again");
        check(s.redo() && !s.dirty(), "and redoing back to it clean");

        check(s.removeSelected() && s.selected() == -1 && s.page().widgets.size() == 1, "delete removes the selected widget");
        check(s.undo() && s.page().widgets.size() == 2 && s.selected() == 1, "and undo restores it, selected");

        check(s.setVertical(true) && s.project().canvasW() == 1080, "switching orientation reflows");
        check(!s.setVertical(true), "switching to the same orientation does nothing");
        check(s.undo() && !s.project().vertical, "undo switches back");

        // Holding an arrow key is one step
        int stepsBefore = 0;
        {
            EditorSession copy = s;
            while (copy.undo()) stepsBefore++;
        }
        for (int i = 0; i < 10; ++i) s.moveBy(1, 0);
        int stepsAfter = 0;
        {
            EditorSession copy = s;
            while (copy.undo()) stepsAfter++;
        }
        check(stepsAfter == stepsBefore + 1, "ten nudges in a row are one undo step");

        // The undo history is capped
        for (int i = 0; i < 150; ++i) s.setAlpha(i % 2 ? 10 : 20);
        int undos = 0;
        while (s.undo()) undos++;
        check(undos == static_cast<int>(EditorSession::UNDO_LIMIT), "at most 100 undo steps, got " + std::to_string(undos));
    });

    Test::run("the editor session: pages", [] {
        std::string root = makeThemesRoot("session_pages");
        std::string layouts = root + "/layouts";
        SaveInputs inputs;
        inputs.fontBytes = fontBytes();
        std::string folder, error;
        check(createTheme(layouts, "Paged", false, Themes::widgetLibrary(), inputs, folder, error), "created");
        ThemeProject p;
        check(loadTheme(layouts, folder, p, error), "loaded");
        createTemplate(p, "Arcade");
        attachCollection(p, "Arcade", "NES");
        SaveReport report;
        check(saveTheme(layouts, folder, p, Themes::widgetLibrary(), inputs, &report, error), "saved with a template: " + error);

        EditorSession s;
        check(s.open(layouts, folder, Themes::widgetLibrary(), error) && s.pageCount() == 2, "two pages");
        checkEqual(s.pageLabel(1), "Arcade - also NES", "the template says which collections it covers");
        s.addWidget("game_title");
        s.setPage(1);
        check(s.selected() == -1 && s.pageIndex() == 1, "switching pages clears the selection");
        checkEqual(s.pageFolder(), "collections/Arcade/layout", "a template's folder");
        check(s.addWidget("game_year") && s.page().widgets.size() == 1, "widgets go on the current page");
        GenResult preview = s.previewXml(GenOptions());
        check(preview.ok() && preview.xml.find("game_year_1") != std::string::npos, "the template generates");
        s.undo();
        check(s.page().widgets.empty(), "undo is per change, on the page it was made");
        s.setPage(0);
        checkEqual(s.pageFolder(), "", "the Default page's folder is the theme's");
    });
});

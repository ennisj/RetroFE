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
#include "../Source/Graphics/MenuXml.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Themes/ThemeProvider.h"
#include "../Source/ThemeEditor/EditorScript.h"
#include "../Source/ThemeEditor/EditorSession.h"
#include "../Source/ThemeEditor/LayoutCheck.h"
#include "../Source/Utility/Log.h"
#include <algorithm>
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
        check(layoutBuildBlocker("<layout><menu><item/></menu></layout>").empty(),
              "so does one without, now PageBuilder gives it empty defaults");
        check(!layoutBuildBlocker("<layout><text>").empty() && !layoutBuildBlocker("<page/>").empty(), "broken XML isn't built");
    });

    Test::run("PageBuilder: a menu without <itemDefaults> gets empty ones, and a vertical menu always ends", [] {
        // What PageBuilder::buildMenu runs first (Graphics/MenuXml.h); without
        // it a hand-made <menu> with no <itemDefaults> crashed RetroFE
        std::string xml = "<layout><menu type=\"vertical\" height=\"500\"><item index=\"0\" selected=\"true\"/></menu>"
                          "<menu><itemDefaults x=\"5\"/></menu></layout>";
        std::vector<char> buffer(xml.begin(), xml.end());
        buffer.push_back('\0');
        rapidxml::xml_document<> doc;
        doc.parse<0>(buffer.data());
        rapidxml::xml_node<>* first = doc.first_node("layout")->first_node("menu");
        bool added = false;
        rapidxml::xml_node<>* defaults = MenuXml::itemDefaults(first, &added);
        check(added && defaults && first->first_node("itemDefaults") == defaults && !defaults->first_attribute(),
              "the first menu gets an empty <itemDefaults>, in the menu");
        check(MenuXml::itemDefaults(first, &added) == defaults && !added, "asking again finds that one");
        check(first->first_node("item") != nullptr, "its items are kept");
        rapidxml::xml_node<>* second = first->next_sibling("menu");
        rapidxml::xml_node<>* own = MenuXml::itemDefaults(second, &added);
        check(!added && own && own->first_attribute("x"), "a menu with its own keeps them");
        check(MenuXml::itemDefaults(nullptr) == nullptr, "no menu, nothing");

        // A slot of height 50 in a 120-high menu: two fit, the third ends it
        check(!MenuXml::verticalMenuFull(0, 50, 120, true, 1) && !MenuXml::verticalMenuFull(50, 100, 120, true, 2) &&
              MenuXml::verticalMenuFull(100, 150, 120, true, 3), "slots are added until the menu is full");
        // Default items with no height (-1, as buildViewInfo leaves them) never fill it
        check(MenuXml::verticalMenuFull(0, -1, 500, true, 1) && MenuXml::verticalMenuFull(0, 0, 500, true, 1),
              "a default slot that adds no height ends the menu instead of looping forever");
        // ...but an overridden slot of height 0, or one with negative spacing, is the layout's choice
        check(!MenuXml::verticalMenuFull(100, 100, 500, false, 3) && !MenuXml::verticalMenuFull(100, 80, 500, false, 3),
              "an <item index=> slot of height 0 or negative spacing doesn't end the menu");
        check(!MenuXml::verticalMenuFull(100, 150, 500, false, 3), "and the slots after it carry on");
        check(MenuXml::verticalMenuFull(100, 100, 500, false, MenuXml::MAX_VERTICAL_SLOTS),
              "whatever the sizes, a menu stops at MAX_VERTICAL_SLOTS slots");
        check(MenuXml::verticalMenuFull(0, 40, -1, true, 1), "a menu with no height of its own gets one slot, as before");
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
        // alpha: 0-1 with a decimal point, or a percent
        auto alpha = [&](const std::string& value) {
            return runSessionCommand(s, parseEditorCommand("alpha " + value), note) == CommandResult::Ok ? std::to_string(s.selectedWidget()->alphaPct) : std::string("failed: ") + note;
        };
        checkEqual(alpha("0.8"), "80", "alpha 0.8 is 80%");
        checkEqual(alpha("40"), "40", "alpha 40 is a percent");
        checkEqual(alpha("25%"), "25", "alpha 25%");
        checkEqual(alpha("1.0"), "100", "alpha 1.0 is opaque");
        checkEqual(alpha(".5"), "50", "alpha .5");
        checkEqual(alpha(".5"), "50", "the same value again is fine");
        for (const char* bad : { "1.5", "150", "-0.2", "half", "50%%", "", "0.5 0.6" }) {
            check(runSessionCommand(s, parseEditorCommand(std::string("alpha ") + bad), note) == CommandResult::Failed
                  && note.find("0-1") != std::string::npos && s.selectedWidget()->alphaPct == 50,
                  std::string("alpha ") + bad + " fails and says why: " + note);
        }
        check(runSessionCommand(s, parseEditorCommand("select -1"), note) == CommandResult::Ok && s.selected() == -1, "deselect");
        check(runSessionCommand(s, parseEditorCommand("alpha 0.3"), note) == CommandResult::Failed && note == "nothing selected", "alpha needs a widget");
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
        check(f.model->get("theme:editNote").find("made by hand") != std::string::npos &&
              f.model->get("theme:editNote").find("layout editor") != std::string::npos, "the note says how it's edited");
        checkEqual(f.model->get("theme:canOpen"), "yes", "a hand-made theme can be opened");
        check(f.model->actionProblem("theme.edit").empty(), "and edited, in the layout editor");
        Forms::Model::Result edit = f.model->runAction("theme.edit");
        check(edit.ok && edit.open == Themes::layoutEditorForm && edit.keepParams, "Edit opens the layout-native editor");
        check(!f.model->actionProblem("theme.duplicate").empty(), "it can't be duplicated");
        check(!f.model->actionProblem("theme.delete").empty(), "or deleted");
        check(!f.model->actionProblem("theme.use").empty(), "the theme in use can't be used again");
        Forms::Model::Result refused = f.model->runAction("theme.delete");
        check(!refused.ok && fs::exists(root + "/layouts/Hand/layout.xml"), "deleting it is refused and nothing is touched");

        f.model->setScreen("theme", { { "id", "Empty" } });
        check(!f.model->get("theme:problem").empty(), "a folder without layout.xml says RetroFE can't use it");
        check(!f.model->actionProblem("theme.use").empty(), "and it can't be used");
        check(f.model->get("theme:canOpen") == "no" && !f.model->actionProblem("theme.edit").empty(), "or edited: it has no pages");
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

    Test::run("the editor session: templates are made, copied, assigned and deleted", [] {
        std::string root = makeThemesRoot("session_templates");
        EditorSession s;
        std::string folder, error;
        check(openNew(root, "Tpl", s, folder), "opened");
        s.addWidget("wheel_vertical");

        // New, based on the Default page
        check(!s.templateProblem("", "X").empty() && !s.templateProblem("NES", " ").empty() &&
              !s.templateProblem("bad:name", "X").empty() && !s.templateProblem("NES.", "X").empty(),
              "a collection, a name and a safe folder name are needed");
        check(s.newTemplate("Arcade", "Arcade Look", ""), "a new template");
        check(s.pageCount() == 2 && s.page().collection == "Arcade" && s.page().widgets.size() == 1,
              "it's a copy of the Default page, and it's the page being edited");
        check(!s.templateProblem("NES", "arcade look").empty(), "names are unique, ignoring case");

        // A blank one, and a copy of the first
        check(s.newTemplate("SNES", "Plain", EditorSession::BLANK) && s.page().widgets.empty(), "a blank template");
        check(s.newTemplate("Genesis", "Arcade Copy", "Arcade") && s.page().widgets.size() == 1, "a copy of a template");
        check(s.templates().size() == 3, "three templates");

        // Assigning: NES joins Arcade Look; it's written as Arcade Look's page
        check(s.assignTemplate("NES", "Arcade") && s.project().page("Arcade")->covers("NES"), "NES joins a template");
        check(!s.assignTemplate("NES", "Arcade"), "joining the same one again isn't a change");
        check(!s.assignTemplate("NES", "Nope"), "a template that doesn't exist is refused");
        checkEqual(s.removesTemplate("NES"), "", "NES is a member, so moving it removes nothing");
        checkEqual(s.removesTemplate("SNES"), "Plain", "SNES is Plain's only collection");

        // Moving a template's primary off it keeps the template for its members
        s.setPage(1);
        check(s.page().collection == "Arcade", "editing Arcade Look");
        check(s.assignTemplate("Arcade", "") && !s.project().coveredBy("Arcade"), "Arcade goes back to Default");
        check(s.project().page("NES") && s.project().page("NES")->displayName() == "Arcade Look",
              "Arcade Look carries on, for NES");
        check(s.page().collection == "NES", "and it's still the page being edited");

        // Moving its only collection deletes a template (undo brings it back)
        size_t pages = s.project().pages.size();
        check(s.assignTemplate("SNES", "") && s.project().pages.size() == pages - 1 && !s.project().page("SNES"), "Plain is gone");
        check(s.undo() && s.project().page("SNES"), "undo brings it back");

        // Rename and delete with a replacement
        check(s.renameTemplate("Genesis", "Sega") && s.project().page("Genesis")->displayName() == "Sega", "renamed");
        check(!s.renameTemplate("Genesis", "  "), "an empty name is refused");
        check(s.deleteTemplate("NES", "Genesis"), "deleted, its collections moving to Sega");
        check(!s.project().page("NES") && s.project().page("Genesis")->covers("NES"), "NES now uses Sega");
        check(!s.deleteTemplate("Genesis", "Genesis") && !s.deleteTemplate("", ""), "not onto itself, never the Default page");

        // The splash page stays last whatever is added
        check(s.setSplashText("Starting...") && ThemeProject::isSplashPage(s.project().pages.back().collection), "a splash page");
        check(s.newTemplate("MAME", "Mame", "") && ThemeProject::isSplashPage(s.project().pages.back().collection),
              "a template added later goes before it");

        // Which page RetroFE shows: no inheritance down the menu tree here
        ThemeProject p = s.project();
        PageVia via;
        check(&pageShownFor(p, "NES", false, &via) == p.page("Genesis") && via == PageVia::Member, "a member uses its template");
        check(&pageShownFor(p, "MAME", false, &via) == p.page("MAME") && via == PageVia::Own, "a template's own collection");
        check(&pageShownFor(p, "Arcade", false, &via) == &p.pages[0] && via == PageVia::Default, "anything else the Default page");
        check(&pageShownFor(p, "Consoles", true, &via) == &p.pages[0], "so is a menu, while there's no _menu template");
        createTemplate(p, MENUS_TEMPLATE);
        check(&pageShownFor(p, "Consoles", true, &via) == p.page(MENUS_TEMPLATE) && via == PageVia::Menus, "then menus use that");
        check(&pageShownFor(p, "Arcade", false, &via) == &p.pages[0], "but not a system");

        // Saved: one page per covered collection, member pages included
        SaveInputs inputs;
        inputs.fontBytes = fontBytes();
        SaveReport report;
        check(s.save(inputs, report, error), "saved: " + error);
        const std::string dir = root + "/layouts/" + folder;
        check(fs::exists(dir + "/collections/Genesis/layout/layout.xml") && fs::exists(dir + "/collections/NES/layout/layout.xml") &&
              fs::exists(dir + "/collections/SNES/layout/layout.xml") && !fs::exists(dir + "/collections/Arcade/layout/layout.xml"),
              "Sega's pages for Genesis and NES, Plain's for SNES, none for Arcade");

        // A page written by hand is never taken over
        writeFile(dir + "/collections/Atari/layout/layout.xml", "<layout width=\"1920\" height=\"1080\"></layout>");
        check(s.handMadePage("Atari") && !s.handMadePage("NES"), "a hand-made page is recognised");
        check(s.templateProblem("Atari", "Atari Look").find("by hand") != std::string::npos, "no template for it");
        check(!s.assignTemplate("Atari", "MAME"), "and it can't join one");
        check(s.assignTemplate("Atari", "") == false, "it's on the Default page already");

        // Scripts drive the same
        check(runSessionCommand(s, parseEditorCommand("tplnew Lynx blank Handheld"), error) == CommandResult::Ok &&
              s.page().collection == "Lynx", "tplnew: " + error);
        check(runSessionCommand(s, parseEditorCommand("tplassign GBA Lynx"), error) == CommandResult::Ok &&
              s.project().page("Lynx")->covers("GBA"), "tplassign");
        check(runSessionCommand(s, parseEditorCommand("tplrename Lynx Pocket Size"), error) == CommandResult::Ok &&
              s.project().page("Lynx")->displayName() == "Pocket Size", "tplrename keeps the spaces");
        check(runSessionCommand(s, parseEditorCommand("tpldelete Lynx default"), error) == CommandResult::Ok &&
              !s.project().page("Lynx") && !s.project().page("GBA"), "tpldelete");
    });

    Test::run("the editor session: cascades, case, renames, stale pages and the page being edited", [] {
        std::string root = makeThemesRoot("session_templates2");
        EditorSession s;
        std::string folder, error;
        check(openNew(root, "Tpl2", s, folder), "opened");
        const std::string dir = root + "/layouts/" + folder;
        writeFile(dir + "/collections/Atari/layout/layout.xml", "<layout width=\"1920\" height=\"1080\"></layout>");

        // A cascade skips the collection whose page was written by hand, and says so
        check(s.newTemplate("Consoles", "Consoles Look", "", { "NES", "Atari", "SNES" }), "a new template for a menu and what's under it");
        const ThemePage* consoles = s.project().page("Consoles");
        check(consoles && consoles->covers("NES") && consoles->covers("SNES") && !consoles->covers("Atari"),
              "newTemplate(alsoFor) takes NES and SNES, not the hand-made Atari");
        check(s.newTemplate("Arcade", "Arcade Look", EditorSession::BLANK), "another template");
        int skipped = 0;
        check(s.assignTemplate(std::vector<std::string>{ "MAME", "Atari", "FBNeo" }, "Arcade", &skipped) && skipped == 1,
              "assignTemplate(many) skips Atari: " + std::to_string(skipped));
        check(s.project().page("Arcade")->covers("MAME") && s.project().page("Arcade")->covers("FBNeo") &&
              !s.project().page("Arcade")->covers("Atari"), "and takes the rest");

        // Names that differ only in case are one folder on Windows
        check(s.templateProblem("nes", "Lower").find("only in case") != std::string::npos, "a template for nes while NES has one");
        check(!s.assignTemplate("mame", "Consoles"), "nor can mame join one while MAME is on another");
        check(!s.renameTemplate("Arcade", "consoles look"), "renaming to another template's name is refused, ignoring case");
        check(s.renameTemplate("Arcade", "Arcade LOOK") && s.project().page("Arcade")->displayName() == "Arcade LOOK",
              "changing the case of its own name is fine");

        // Erasing a template before the one being edited keeps editing that one
        check(s.newTemplate("GBA", "Handheld", EditorSession::BLANK), "a third template, now being edited");
        check(s.newTemplate("Lynx", "Lynx Only", EditorSession::BLANK), "and a fourth");
        s.setPage(s.pageCount() - 2);   // Handheld (the splash page isn't there yet)
        check(s.page().collection == "GBA", "editing Handheld");
        check(s.assignTemplate(std::vector<std::string>{ "MAME", "FBNeo", "Arcade" }, ""), "Arcade Look loses all its collections");
        check(!s.project().page("Arcade") && s.page().collection == "GBA", "it's gone, and Handheld is still the page being edited");

        // Saving after a template went: its pages go too
        SaveInputs inputs;
        inputs.fontBytes = fontBytes();
        SaveReport report;
        check(s.save(inputs, report, error), "saved: " + error);
        check(fs::exists(dir + "/collections/Lynx/layout/layout.xml") && fs::exists(dir + "/collections/NES/layout/layout.xml"),
              "the templates' pages are written");
        check(s.deleteTemplate("Lynx", "") && s.assignTemplate("NES", ""), "Lynx Only deleted, NES back on Default");
        check(s.save(inputs, report, error), "saved again: " + error);
        bool removedLynx = false, removedNes = false;
        for (const std::string& r : report.removed) {
            removedLynx |= r == "collections/Lynx/layout/layout.xml";
            removedNes |= r == "collections/NES/layout/layout.xml";
        }
        check(removedLynx && removedNes && !fs::exists(dir + "/collections/Lynx") && !fs::exists(dir + "/collections/NES/layout/layout.xml"),
              "the pages they no longer have are removed");
        check(fs::exists(dir + "/collections/Atari/layout/layout.xml") && fs::exists(dir + "/collections/SNES/layout/layout.xml"),
              "the hand-made page and the ones still covered stay");

        // Only real fonts go into fonts/
        writeFile(root + "_src/fake.ttf", "not a font at all");
        writeFile(root + "_src/font.txt", fontBytes());
        writeFile(root + "_src/Real.ttf", fontBytes());
        writeFile(root + "_src/Open.otf", std::string("OTTO") + std::string(60, '\0'));
        check(!fontFileProblem(root + "_src/fake.ttf").empty() && !fontFileProblem(root + "_src/font.txt").empty(),
              "a .ttf that isn't one, and a font that isn't called .ttf/.otf, are refused");
        check(fontFileProblem(root + "_src/Real.ttf").empty() && fontFileProblem(root + "_src/Open.otf").empty(),
              "TrueType and OpenType headers pass");
        std::error_code ec;
        fs::remove_all(root + "_src", ec);
    });

    Test::run("the editor session: theme font, sounds, splash and files to ask for", [] {
        std::string root = makeThemesRoot("session_theme_props");
        EditorSession s;
        std::string folder, error;
        check(openNew(root, "Props", s, folder), "opened");
        const std::string layouts = root + "/layouts";
        const std::string dir = layouts + "/" + folder;

        // Fonts: imported into fonts/, listed from there
        writeFile(root + "_src/Fancy.ttf", fontBytes());
        std::string rel;
        check(importFile(layouts, folder, root + "_src/Fancy.ttf", "fonts", rel, error) && rel == "fonts/Fancy.ttf", "a font goes into fonts/: " + rel);
        std::vector<std::string> fonts = themeFiles(layouts, folder, "fonts", { ".ttf", ".otf" });
        check(std::find(fonts.begin(), fonts.end(), "fonts/Fancy.ttf") != fonts.end() &&
              std::find(fonts.begin(), fonts.end(), "fonts/Roboto-Regular.ttf") != fonts.end(), "and listed with the bundled one");
        check(importFile(layouts, folder, dir + "/fonts/Fancy.ttf", "assets", rel, error) && rel == "fonts/Fancy.ttf",
              "a file already in the theme isn't copied");
        check(s.setThemeFont("fonts/Fancy.ttf") && s.project().font == "fonts/Fancy.ttf", "the theme font");
        check(s.setThemeFont(BUNDLED_FONT) && s.project().font.empty(), "choosing the bundled one clears it");

        // Sounds
        check(s.setSound("select", "assets/click.wav") && s.sound("select") == "assets/click.wav", "a sound");
        check(!s.setSound("boom", "assets/x.wav"), "only load, unload, highlight and select");
        check(s.previewXml(GenOptions()).xml.find("<sound type=\"select\" src=\"assets/click.wav\"/>") != std::string::npos,
              "the page plays it");
        check(s.setSound("select", "") && s.sound("select").empty(), "and it can be taken off");

        // Splash: none until it's given text or a background
        check(!s.hasSplashPage() && s.splashText() == "Props", "without a page the splash shows the name");
        check(s.setSplashText("Loading Props") && s.hasSplashPage() && s.splashText() == "Loading Props", "text makes a page");
        check(s.setSplashBackground("assets/intro.mp4"), "a video background");
        const ThemePage* splash = s.project().page(ThemeProject::splashPage());
        check(splash && splash->widgets.size() == 2 && splash->widgets[0].def == "static_video" &&
              splash->widgets[0].w == 1920 && splash->widgets[0].varValue("STRETCH", "") == "true", "it fills the page, under the text");
        check(s.setSplashBackground("assets/intro.png") && s.project().page(ThemeProject::splashPage())->widgets[0].def == "static_image" &&
              s.project().page(ThemeProject::splashPage())->widgets.size() == 2, "an image replaces it");
        check(s.splashBackground() == "assets/intro.png", "the background is reported");
        check(s.setSplashBackground("") && s.splashBackground().empty() && s.project().page(ThemeProject::splashPage())->widgets.size() == 1,
              "and removed");
        check(s.undo() && s.splashBackground() == "assets/intro.png", "undo puts it back");

        // A Static Image's default file isn't in a new theme: ask for one
        check(s.addWidget("static_image"), "a static image");
        std::vector<std::string> missing = s.missingFiles();
        check(missing.size() == 1 && missing[0] == "SRC", "its file is missing");
        writeFile(root + "_src/bg.png", "PNG");
        check(importAsset(layouts, folder, root + "_src/bg.png", rel, error) && s.setVar("SRC", rel), "a file is chosen");
        check(s.missingFiles().empty(), "nothing is missing then");
        check(s.addWidget("game_title") && s.missingFiles().empty(), "widgets without files never ask");

        // Scripts
        check(runSessionCommand(s, parseEditorCommand("font fonts/Fancy.ttf"), error) == CommandResult::Ok && s.project().font == "fonts/Fancy.ttf", "font");
        check(runSessionCommand(s, parseEditorCommand("sound load assets/a b.wav"), error) == CommandResult::Ok && s.sound("load") == "assets/a b.wav", "sound");
        check(runSessionCommand(s, parseEditorCommand("splashbg none"), error) == CommandResult::Ok && s.splashBackground().empty(), "splashbg none");
        std::error_code ec;
        fs::remove_all(root + "_src", ec);
    });
});

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

// The theme editors' controller decisions (ThemeEditor/EditorNav.h) and the
// on-screen keyboard's keys (Forms/KeyboardKeys.h), shared by the editors'
// ImGui keyboard and the layout-drawn one.

#include "TestSupport.h"
#include "../Source/Forms/KeyboardKeys.h"
#include "../Source/ThemeEditor/EditorNav.h"
#include "../Source/ThemeEditor/EditorRows.h"

using namespace ThemeEditor;
using Test::check;

namespace
{
    DragRect rect(int x, int y, int w, int h)
    {
        DragRect r;
        r.x = x; r.y = y; r.w = w; r.h = h;
        return r;
    }
}

static Test::Suite editorNav("editor controller", [] {
    Test::run("widget editor: tile order and cycling", [] {
        for (bool vertical : { false, true }) {
            int region = EF_THEME;
            std::vector<int> walk;
            for (int i = 0; i < 6; ++i) { walk.push_back(region); region = widgetRegionCycle(region, 1, vertical); }
            check(walk == (vertical ? std::vector<int>{ EF_THEME, EF_TOOLBAR, EF_CANVAS, EF_ACTIONS, EF_PROPS, EF_THEME_PROPS }
                                   : std::vector<int>{ EF_THEME, EF_TOOLBAR, EF_CANVAS, EF_ACTIONS, EF_THEME_PROPS, EF_PROPS }), "tile reading order");
            check(region == EF_THEME && widgetRegionCycle(EF_THEME, -1, vertical) == walk.back(), "wrap both ways");
        }
        check(widgetRegionStep(EF_THEME, 0, -1, false) == -1 && widgetRegionStep(EF_CANVAS, 0, 1, false) == EF_ACTIONS, "vertical edges follow tile stack");
        check(widgetRegionStep(EF_THEME, 1, 0, false) == EF_THEME_PROPS && widgetRegionStep(EF_THEME_PROPS, 1, 0, false) == -1, "landscape right column edge");
        check(widgetRegionStep(EF_PROPS, 1, 0, true) == EF_THEME_PROPS && widgetRegionStep(EF_THEME_PROPS, 0, -1, true) == EF_ACTIONS, "portrait bottom band edges");
        check(listStep(-1, 0, 1) == -1 && listStep(-1, 3, -1) == 2 && listStep(-1, 3, 1) == 0, "empty and unselected widget cycle");
        check(listStep(0, 3, -1) == 2 && listStep(2, 3, 1) == 0 && listStep(0, 1, 1) == 0, "widget cycle wraps");
    });
    Test::run("widget editor: collection dropdown rows", [] {
        ThemeProject project;
        ThemePage page; page.collection = "Arcade"; page.members = { "Member" }; project.pages.push_back(page);
        ThemePage menus; menus.collection = "_menu"; project.pages.push_back(menus);
        std::vector<CollectionRow> tree;
        auto push = [&](const std::string& name, int depth, bool menu, bool children) {
            CollectionRow row; row.name = name; row.depth = depth; row.menu = menu; row.children = children; tree.push_back(row);
        };
        push("Arcade", 0, true, true); push("Member", 1, false, false); push("Child", 1, false, false);
        push("Menu", 0, true, false); push("", -1, false, false); push("Loose", 0, false, false);
        auto rows = collectionRows(tree, {}, project, project.pages[1]);
        check(rows.size() == 7 && rows[0].label() == "<root> (Top Level)" && rows[0].depth == 0, "Top Level first");
        check(rows[1].name == "Arcade" && rows[2].name == "Member" && rows[2].depth == 1 && rows[5].label() == "Not in a menu" && rows[6].name == "Loose", "depth-first, then Not in a menu");
        check(rows[1].covered && rows[2].covered && !rows[3].covered && !rows[4].covered && !rows[0].covered, "own and member bright, child does not inherit");
        auto folded = collectionRows(tree, { "Arcade" }, project, project.pages[0]);
        check(folded.size() == 5 && folded[2].name == "Menu" && folded[0].covered && folded.back().covered, "collapsed branch hides descendants, Default coverage");
        check(collectionRows(tree, {}, project, project.pages[0]).size() == 7, "expand restores all rows");
        check(templateChangeLine("Arcade", "Default", 0) == "Changes \"Arcade\" to Default.", "single live line");
        check(templateChangeLine("Arcade", "Look", 2) == "Changes \"Arcade\" and its 2 collections to Look.", "branch live line");
    });
    Test::run("editor controller: regions", [] {
        check(regionStep(EF_CANVAS, -1, 0) == EF_THEME && regionStep(EF_CANVAS, 1, 0) == EF_PROPS &&
              regionStep(EF_CANVAS, 0, -1) == EF_TOOLBAR && regionStep(EF_CANVAS, 0, 1) == -1,
              "from the canvas: theme left, properties right, toolbar up, nothing down");
        check(regionStep(EF_THEME, 1, 0) == EF_CANVAS && regionStep(EF_THEME, -1, 0) == -1 && regionStep(EF_THEME, 0, -1) == EF_TOOLBAR &&
              regionStep(EF_THEME, 0, 1) == -1, "from the theme panel");
        check(regionStep(EF_PROPS, -1, 0) == EF_CANVAS && regionStep(EF_PROPS, 1, 0) == -1 && regionStep(EF_PROPS, 0, -1) == EF_TOOLBAR,
              "from the properties");
        check(regionStep(EF_TOOLBAR, 0, 1) == EF_CANVAS && regionStep(EF_TOOLBAR, 0, -1) == -1 && regionStep(EF_TOOLBAR, 1, 0) == -1 &&
              regionStep(EF_TOOLBAR, -1, 0) == -1, "from the toolbar: down to the canvas, its ends stay");
        check(regionStep(EF_ACTIONS, 1, 0) == -1, "regions this editor doesn't have stay");
        // Page Down walks toolbar, theme, canvas, properties and round; Page Up back
        int r = EF_TOOLBAR;
        std::vector<int> walk;
        for (int i = 0; i < 4; ++i) walk.push_back(r = regionCycle(r, 1));
        check(walk == std::vector<int>({ EF_THEME, EF_CANVAS, EF_PROPS, EF_TOOLBAR }), "page down cycles in reading order");
        check(regionCycle(EF_TOOLBAR, -1) == EF_PROPS && regionCycle(EF_CANVAS, -1) == EF_THEME, "page up goes back, wrapping");
    });

    Test::run("editor controller: panel and dialog routes", [] {
        // Panels: directions go to ImGui, and leave the panel when nothing moves
        check(panelRoute(EIK_OTHER, EP_UP, EL_PANEL) == ER_FORWARD_EDGE && panelRoute(EIK_OTHER, EP_DOWN, EL_PANEL) == ER_FORWARD_EDGE,
              "up / down in a panel can leave it");
        check(panelRoute(EIK_OTHER, EP_LEFT, EL_PANEL) == ER_FORWARD_EDGE && panelRoute(EIK_TEXT, EP_RIGHT, EL_PANEL) == ER_FORWARD_EDGE,
              "left / right on a button or a text field: the neighbour on the row, else the region that way");
        check(panelRoute(EIK_NUMBER, EP_LEFT, EL_PANEL) == ER_ADJUST && panelRoute(EIK_CHOICE, EP_RIGHT, EL_PANEL) == ER_ADJUST,
              "left / right step numbers and choices");
        check(panelRoute(EIK_NODE, EP_LEFT, EL_PANEL) == ER_FORWARD && panelRoute(EIK_NODE, EP_DOWN, EL_PANEL) == ER_FORWARD_EDGE,
              "left / right fold a section header, never leaving the panel");
        check(panelRoute(EIK_TEXT, EP_SELECT, EL_PANEL) == ER_KEYBOARD && panelRoute(EIK_NUMBER, EP_SELECT, EL_PANEL) == ER_KEYBOARD,
              "select types into text and numbers on the keyboard");
        check(panelRoute(EIK_OTHER, EP_SELECT, EL_PANEL) == ER_FORWARD && panelRoute(EIK_CHOICE, EP_SELECT, EL_PANEL) == ER_FORWARD &&
              panelRoute(EIK_NODE, EP_SELECT, EL_PANEL) == ER_FORWARD, "select presses buttons, opens choices, folds headers");
        check(panelRoute(EIK_OTHER, EP_BACK, EL_PANEL) == ER_LEAVE_REGION && panelRoute(EIK_TEXT, EP_BACK, EL_PANEL) == ER_LEAVE_REGION,
              "back leaves a panel for the canvas");
        // Dialogs: no edges; back closes the dialog
        check(panelRoute(EIK_OTHER, EP_UP, EL_DIALOG) == ER_FORWARD && panelRoute(EIK_OTHER, EP_LEFT, EL_DIALOG) == ER_FORWARD,
              "a dialog keeps the focus");
        check(panelRoute(EIK_CHOICE, EP_LEFT, EL_DIALOG) == ER_ADJUST && panelRoute(EIK_TEXT, EP_SELECT, EL_DIALOG) == ER_KEYBOARD,
              "choices and text fields in a dialog work as in a panel");
        check(panelRoute(EIK_OTHER, EP_BACK, EL_DIALOG) == ER_CLOSE_DIALOG, "back closes a dialog");
        // An open drop-down list: everything to ImGui, back closes the list (not the dialog under it)
        check(panelRoute(EIK_OTHER, EP_DOWN, EL_LIST) == ER_FORWARD && panelRoute(EIK_OTHER, EP_SELECT, EL_LIST) == ER_FORWARD &&
              panelRoute(EIK_CHOICE, EP_LEFT, EL_LIST) == ER_FORWARD, "a drop-down list moves and chooses");
        check(panelRoute(EIK_OTHER, EP_BACK, EL_LIST) == ER_CLOSE_LIST, "back closes the list");
        check(panelRoute(EIK_TEXT, EP_SELECT, EL_LIST) == ER_KEYBOARD, "a filter field in a list types on the keyboard");
        check(panelRoute(EIK_OTHER, EP_PAGE_UP, EL_PANEL) == ER_IGNORE && panelRoute(EIK_OTHER, EP_LETTER_DOWN, EL_DIALOG) == ER_IGNORE,
              "page and letter keys aren't the panels'");
    });

    Test::run("editor controller: canvas modes", [] {
        EdCanvasDecision d = canvasPress(ECM_PICK, EP_RIGHT, true, true);
        check(d.action == ECA_PICK && d.mode == ECM_PICK, "pick: a direction picks");
        d = canvasPress(ECM_PICK, EP_SELECT, true, true);
        check(d.action == ECA_MODE && d.mode == ECM_MOVE, "pick: select moves the selection");
        d = canvasPress(ECM_MOVE, EP_SELECT, true, true);
        check(d.action == ECA_MODE && d.mode == ECM_RESIZE, "move: select resizes instead");
        d = canvasPress(ECM_RESIZE, EP_SELECT, true, true);
        check(d.action == ECA_MODE && d.mode == ECM_PICK, "resize: select is done");
        check(canvasPress(ECM_MOVE, EP_LEFT, true, true).action == ECA_MOVE && canvasPress(ECM_RESIZE, EP_UP, true, true).action == ECA_RESIZE,
              "directions nudge in move and resize");
        d = canvasPress(ECM_MOVE, EP_BACK, true, true);
        check(d.action == ECA_MODE && d.mode == ECM_PICK, "back leaves move for pick");
        d = canvasPress(ECM_RESIZE, EP_BACK, true, true);
        check(d.action == ECA_MODE && d.mode == ECM_PICK, "back leaves resize for pick");
        check(canvasPress(ECM_PICK, EP_BACK, true, true).action == ECA_LEAVE_EDITOR, "back in pick leaves the editor");
        check(canvasPress(ECM_PICK, EP_SELECT, false, false).action == ECA_ADD, "nothing selected: select adds one");
        check(canvasPress(ECM_PICK, EP_SELECT, true, false).action == ECA_CANT_MOVE, "a box made by a menu can't be moved");
        d = canvasPress(ECM_MOVE, EP_RIGHT, false, false);
        check(d.mode == ECM_PICK && d.action == ECA_PICK, "the selection went away mid-move: back to picking");
        check(canvasPress(ECM_PICK, EP_LETTER_UP, true, true).action == ECA_PREV && canvasPress(ECM_PICK, EP_LETTER_DOWN, false, false).action == ECA_NEXT,
              "letter up / down: previous / next");
        check(canvasPress(ECM_MOVE, EP_LETTER_DOWN, true, true).action == ECA_NONE, "letter keys only pick");
        check(canvasPress(ECM_PICK, EP_PAGE_DOWN, true, true).action == ECA_NONE, "page keys are the screen's");
    });

    Test::run("editor controller: picking", [] {
        // Three boxes: left, right (a little lower), below
        std::vector<DragRect> rs = { rect(100, 100, 200, 200), rect(700, 120, 200, 200), rect(120, 700, 200, 200) };
        check(pickStep({}, -1, 1, 0, 1920, 1080) == -1, "nothing on the page: leave the canvas");
        check(pickStep(rs, -1, 0, 1, 1920, 1080) == 1, "nothing selected: the one nearest the middle");
        check(pickStep(rs, 0, 1, 0, 1920, 1080) == 1 && pickStep(rs, 0, 0, 1, 1920, 1080) == 2, "right and down");
        check(pickStep(rs, 1, 1, 0, 1920, 1080) == -1 && pickStep(rs, 0, -1, 0, 1920, 1080) == -1 && pickStep(rs, 0, 0, -1, 1920, 1080) == -1,
              "nothing that way: leave the canvas");
        check(pickStep(rs, 2, 0, -1, 1920, 1080) == 0, "straight ahead beats a nearer diagonal");
        check(pickStep(rs, 7, 1, 0, 1920, 1080) == 1, "a selection that isn't a box picks afresh");
    });

    Test::run("editor controller: steps", [] {
        check(nudgeStep(5, 0) == 5 && nudgeStep(5, 3) == 5 && nudgeStep(5, 4) == 20 && nudgeStep(5, 11) == 20 && nudgeStep(5, 12) == 50,
              "held nudges speed up");
        check(nudgeStep(1, 0) == 1 && nudgeStep(0, 0) == 1 && nudgeStep(1, 20) == 10, "without snapping, from 1");
        check(listStep(-1, 4, 1) == 0 && listStep(-1, 4, -1) == 3 && listStep(3, 4, 1) == 0 && listStep(0, 4, -1) == 3 &&
              listStep(1, 4, 1) == 2 && listStep(0, 0, 1) == -1, "previous / next element, wrapping");
        check(choiceStep(0, 3, -1) == 0 && choiceStep(2, 3, 1) == 2 && choiceStep(1, 3, 1) == 2 && choiceStep(-1, 3, 1) == 0 &&
              choiceStep(-1, 3, -1) == 2 && choiceStep(0, 0, 1) == -1, "choices step and stop at the ends");
        check(numberStep(10, 5, 1, 0, 0) == 15 && numberStep(10, 5, -1, 0, 0) == 5, "numbers step, unbounded");
        check(numberStep(98, 5, 1, 0, 100) == 100 && numberStep(2, 5, -1, 0, 100) == 0 && numberStep(0.5, 0.1, 1, 0, 1) > 0.59,
              "and clamp when bounded");
        check(roundTripNumber(0.1) == "0.1" && roundTripNumber(0.1234567) == "0.1234567" && roundTripNumber(1234567) == "1234567" &&
              roundTripNumber(-2.5) == "-2.5" && roundTripNumber(0) == "0", "numbers the keyboard starts from read back exactly");
        check(std::strtod(roundTripNumber(1.0 / 3.0).c_str(), nullptr) == 1.0 / 3.0, "even a third");
        int dx = 9, dy = 9;
        pressDirection(EP_LEFT, dx, dy);
        check(dx == -1 && dy == 0, "left");
        pressDirection(EP_DOWN, dx, dy);
        check(dx == 0 && dy == 1, "down");
        pressDirection(EP_SELECT, dx, dy);
        check(dx == 0 && dy == 0, "select isn't a direction");
    });

    Test::run("on-screen keyboard keys", [] {
        using Keys = Forms::KeyboardKeys;
        Keys::forgetLastKeys();
        Keys keys;
        Forms::TextEdit text;
        text.set("ab");
        keys.reset(false, false);
        check(keys.row() == 1 && keys.column() == 0 && keys.rows()[1][0].label == "q", "starts on q");
        check(keys.navigate(Keys::Nav::Select, text) == Keys::Result::None && text.text == "abq", "select types the key");
        keys.navigate(Keys::Nav::Left, text);
        check(keys.column() == 9, "left wraps to the end of the row");
        keys.navigate(Keys::Nav::Up, text);
        check(keys.row() == 0 && keys.rows()[0][keys.column()].label == "0", "up keeps the column");
        keys.navigate(Keys::Nav::Up, text);
        check(keys.row() == 5 && keys.rows()[5][keys.column()].action == Keys::Action::Done, "up from the top wraps to Done");
        check(keys.navigate(Keys::Nav::Select, text) == Keys::Result::Done, "Done finishes");
        // Shift: capitals and symbols, from the bottom row's Shift
        keys.navigate(Keys::Nav::Left, text);
        keys.navigate(Keys::Nav::Left, text);
        keys.navigate(Keys::Nav::Left, text);
        check(keys.rows()[5][keys.column()].action == Keys::Action::Shift, "Shift");
        keys.navigate(Keys::Nav::Select, text);
        check(keys.shift() && keys.rows()[1][0].label == "Q", "shift shows capitals");
        keys.navigate(Keys::Nav::Right, text);
        keys.navigate(Keys::Nav::Right, text);
        check(keys.rows()[5][keys.column()].action == Keys::Action::Delete, "Del");
        keys.navigate(Keys::Nav::Select, text);
        check(text.text == "ab", "Del takes the character before the cursor");
        check(keys.navigate(Keys::Nav::Back, text) == Keys::Result::Cancel, "back cancels");

        // A one-line text: a row of path symbols and keys that move the cursor
        const auto& pathRow = keys.rows()[4];
        std::string symbols;
        for (const auto& key : pathRow) {
            if (key.action == Keys::Action::Insert) symbols += key.label;
        }
        check(symbols == "./\\:-_", "path symbols, whatever Shift says: " + symbols);
        text.set("C:\\games");
        auto pressAction = [&](Keys::Action action) {
            for (const auto& key : pathRow) {
                if (key.action == action) return keys.press(key, text);
            }
            return Keys::Result::Cancel;
        };
        pressAction(Keys::Action::Home);
        check(text.cursor == 0, "Home goes to the start");
        pressAction(Keys::Action::CursorRight);
        pressAction(Keys::Action::CursorRight);
        pressAction(Keys::Action::CursorRight);
        Keys::Key d;
        d.label = "D";
        keys.press(d, text);
        check(text.text == "C:\\Dgames", "typed in the middle: " + text.text);
        pressAction(Keys::Action::CursorLeft);
        keys.press(d, text);
        check(text.text == "C:\\DDgames", "and after moving left: " + text.text);
        pressAction(Keys::Action::End);
        check(text.cursor == text.text.size(), "End goes to the end");

        // The next edit starts on the last key typed with (not on Done or Shift)
        Keys::forgetLastKeys();
        keys.reset(false, false);
        keys.navigate(Keys::Nav::Right, text);
        keys.navigate(Keys::Nav::Right, text);
        keys.navigate(Keys::Nav::Select, text);   // "e"
        Keys again;
        again.reset(false, false);
        check(again.row() == 1 && again.column() == 2, "the next keyboard starts on e");
        again.navigate(Keys::Nav::Down, text);
        again.navigate(Keys::Nav::Down, text);
        again.navigate(Keys::Nav::Down, text);
        again.navigate(Keys::Nav::Down, text);
        check(again.rows()[again.row()][again.column()].action == Keys::Action::Space, "on Space");
        again.navigate(Keys::Nav::Right, text);
        again.navigate(Keys::Nav::Right, text);
        check(again.navigate(Keys::Nav::Select, text) == Keys::Result::Done, "Done");
        Keys third;
        third.reset(false, false);
        check(third.row() == 1 && third.column() == 2, "Done isn't remembered");
        for (Keys::Action action : { Keys::Action::Insert, Keys::Action::Space }) {
            check(Keys::remembers(action), "character keys are remembered");
        }
        for (Keys::Action action : { Keys::Action::Delete, Keys::Action::CursorLeft, Keys::Action::CursorRight,
                                    Keys::Action::Home, Keys::Action::End, Keys::Action::LineUp,
                                    Keys::Action::LineDown, Keys::Action::NewLine, Keys::Action::Done, Keys::Action::Shift }) {
            check(!Keys::remembers(action), "editing and movement keys aren't remembered");
        }
        again.navigate(Keys::Nav::Left, text);   // Del, then Done
        again.navigate(Keys::Nav::Select, text);
        again.navigate(Keys::Nav::Right, text);
        again.navigate(Keys::Nav::Select, text);
        third.reset(false, false);
        check(third.row() == 1 && third.column() == 2, "deleting before Done still remembers e");
        Keys numbers;
        numbers.reset(true, false);
        check(numbers.row() == 0 && numbers.column() == 0, "a number pad remembers its own keys");
        Keys::forgetLastKeys();
        text.set("ab");

        // Numbers, with and without a decimal point
        keys.reset(true, false);
        check(keys.row() == 0 && keys.rows().size() == 5 && keys.rows()[3].size() == 3, "a number pad");
        keys.reset(true, false, true);
        check(keys.rows()[3].size() == 4 && keys.rows()[3][2].label == ".", "a decimal pad has a point");
        keys.reset(false, true, true);
        check(!keys.numeric() && keys.multiline(), "decimal is only for numbers");
        // Multi-line: line keys report to the owner
        bool lineUp = false;
        for (const auto& key : keys.rows()[4]) lineUp = lineUp || key.action == Keys::Action::LineUp;
        check(keys.rows().size() == 6 && lineUp, "a multi-line text has cursor and line keys");
        Keys::Key up;
        up.action = Keys::Action::LineUp;
        check(keys.press(up, text) == Keys::Result::LineUp, "Line up is the owner's to do");
        Keys::Key newline;
        newline.action = Keys::Action::NewLine;
        keys.press(newline, text);
        check(text.text == "ab\n", "New line types one");
    });
});

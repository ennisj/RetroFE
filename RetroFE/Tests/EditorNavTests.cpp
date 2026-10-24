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
        check(keys.row() == 4 && keys.rows()[4][keys.column()].action == Keys::Action::Done, "up from the top wraps to Done");
        check(keys.navigate(Keys::Nav::Select, text) == Keys::Result::Done, "Done finishes");
        // Shift: capitals and symbols, from the bottom row's Shift
        keys.navigate(Keys::Nav::Left, text);
        keys.navigate(Keys::Nav::Left, text);
        keys.navigate(Keys::Nav::Left, text);
        check(keys.rows()[4][keys.column()].action == Keys::Action::Shift, "Shift");
        keys.navigate(Keys::Nav::Select, text);
        check(keys.shift() && keys.rows()[1][0].label == "Q", "shift shows capitals");
        keys.navigate(Keys::Nav::Right, text);
        keys.navigate(Keys::Nav::Right, text);
        check(keys.rows()[4][keys.column()].action == Keys::Action::Delete, "Del");
        keys.navigate(Keys::Nav::Select, text);
        check(text.text == "ab", "Del takes the character before the cursor");
        check(keys.navigate(Keys::Nav::Back, text) == Keys::Result::Cancel, "back cancels");

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

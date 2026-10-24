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

// Several widgets or elements at once in the theme editors: the selection
// maths, marquee, group moves, align and distribute (ThemeEditor/
// EditorSelection.h), resizing from any handle with the controller and the
// held-Select controls (EditorNav.h), and both sessions' multi-selection
// (EditorSession, LayoutSession): one undo step per operation, the layout
// editor's edits byte for byte.

#include "TestSupport.h"
#include "../Source/Manager/Themes/ThemeProvider.h"
#include "../Source/ThemeEditor/EditorNav.h"
#include "../Source/ThemeEditor/EditorScript.h"
#include "../Source/ThemeEditor/EditorSelection.h"
#include "../Source/ThemeEditor/EditorSession.h"
#include "../Source/ThemeEditor/LayoutSession.h"
#include "../Source/ThemeEditor/ThemeStore.h"
#include <string>
#include <vector>

using namespace Test;
using namespace ThemeEditor;

namespace
{
    bool same(const DragRect& a, const DragRect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

    std::string str(const DragRect& r)
    {
        return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h);
    }

    CanvasFit unitFit() { return canvasFit(1920.0f, 1080.0f, 1920, 1080); }

    // A fresh editor-made theme, opened, with three Static Texts placed
    bool openThree(const std::string& root, EditorSession& s)
    {
        SaveInputs inputs;
        inputs.fontBytes = readFile(std::string(RETROFE_THIRD_PARTY_DIR) + "/fonts/Roboto-Regular.ttf");
        std::string error, folder;
        const std::string layouts = root + "/layouts";
        if (!createTheme(layouts, "Multi", false, Themes::widgetLibrary(), inputs, folder, error)) return false;
        if (!s.open(layouts, folder, Themes::widgetLibrary(), error)) return false;
        const int rects[3][4] = { { 100, 100, 200, 100 }, { 500, 300, 200, 100 }, { 900, 600, 200, 100 } };
        for (const auto& r : rects) {
            if (!s.addWidget("static_text") || !s.setRect(r[0], r[1], r[2], r[3])) return false;
        }
        return s.page().widgets.size() == 3;
    }

    DragRect widgetRect(const EditorSession& s, int i)
    {
        const WidgetInstance& w = s.page().widgets[i];
        return makeRect(w.x, w.y, w.w, w.h);
    }

    // A hand-made page: two images (one centred) and a text, then a vertical menu
    const std::string PAGE =
        "<layout width=\"1920\" height=\"1080\">\r\n"
        "    <!-- three boxes -->\r\n"
        "    <image src=\"a.png\" x=\"100\" y=\"100\" width=\"200\" height=\"100\" layer=\"1\"/>\r\n"
        "    <image src=\"b.png\" x=\"center\" y=\"300\" width=\"200\" height=\"100\" xOrigin=\"center\" layer=\"1\">\r\n"
        "        <onEnter><set duration=\"1\"><animate type=\"y\" to=\"350\"/></set></onEnter>\r\n"
        "        <onMenuScroll><set duration=\"1\"><animate type=\"x\" from=\"900\" to=\"1000\"/></set></onMenuScroll>\r\n"
        "        <onHighlightEnter><set duration=\"1\"><animate type=\"alpha\" to=\"1\"/></set></onHighlightEnter>\r\n"
        "    </image>\r\n"
        "    <text value=\"c\" x=\"900\" y=\"600\" width=\"200\" height=\"100\" fontSize=\"30\" layer=\"2\"/>\r\n"
        "    <menu type=\"vertical\" x=\"1500\" y=\"0\" width=\"300\" height=\"1080\">\r\n"
        "        <itemDefaults height=\"100\"/>\r\n"
        "    </menu>\r\n"
        "</layout>\r\n";

    std::string makePageRoot(const std::string& name)
    {
        std::string root = makeRoot(name);
        writeFile(root + "/layouts/Hand/layout.xml", PAGE);
        return root;
    }

    void feed(LayoutSession& s)
    {
        ElementGeom a; a.x = 100; a.y = 100; a.w = 200; a.h = 100; a.layer = 1; a.order = 0;
        s.setGeometry("image[0]", a);
        ElementGeom b; b.x = 960; b.y = 300; b.xOrigin = 0.5f; b.w = 200; b.h = 100; b.layer = 1; b.order = 1;
        s.setGeometry("image[1]", b);
        ElementGeom t; t.x = 900; t.y = 600; t.w = 200; t.h = 100; t.layer = 2; t.order = 2;
        s.setGeometry("text[0]", t);
        ElementGeom m; m.x = 1500; m.y = 0; m.w = 300; m.h = 100; m.layer = 0; m.order = 3; m.readOnly = true;
        s.setGeometry("menu[0]/itemDefaults[0]", m);
    }

    void drag(LayoutSession& s, float x1, float y1, float x2, float y2, bool additive = false)
    {
        s.pointerDown(unitFit(), x1, y1, 4.0f, additive);
        s.pointerMove(unitFit(), x2, y2);
        s.pointerUp();
    }

    void drag(EditorSession& s, float x1, float y1, float x2, float y2, bool additive = false)
    {
        s.pointerDown(unitFit(), x1, y1, 4.0f, additive);
        s.pointerMove(unitFit(), x2, y2);
        s.pointerUp();
    }

    std::string attr(const LayoutSession& s, const std::string& path, const std::string& name)
    {
        const int i = s.doc().find(path);
        return i < 0 ? "(no element)" : s.element(i).value(name, "(unset)");
    }
}

static Test::Suite editorSelection("editor selection", [] {
    Test::run("selection: a list in picking order, the primary last", [] {
        std::vector<int> sel;
        sel = selectionWith(sel, 3);
        sel = selectionWith(sel, 1);
        sel = selectionWith(sel, 3);
        check(sel == std::vector<int>({ 1, 3 }), "a repeat moves to the end as the primary");
        check(selectionToggled(sel, 1) == std::vector<int>({ 3 }) && selectionToggled(sel, 7) == std::vector<int>({ 1, 3, 7 }),
              "toggling: out if in, in (as the primary) if out");
        check(selectionWithout(sel, 3) == std::vector<int>({ 1 }), "without");
        const std::vector<int> order = { 0, 2, 4, 6, 8 };
        check(selectionRange(order, 2, 6) == std::vector<int>({ 2, 4, 6 }), "a range down the list, clicked last");
        check(selectionRange(order, 8, 4) == std::vector<int>({ 8, 6, 4 }), "and up it");
        check(selectionRange(order, 5, 4) == std::vector<int>({ 4 }), "an anchor that isn't listed gives just the clicked one");
        check(selectionRange(order, 2, 5).empty(), "a click on something unlisted gives nothing");
        check(selectionAfterErase({ 1, 5, 3 }, 3) == std::vector<int>({ 1, 4 }), "after an erase the later indices shift down");
    });

    Test::run("selection: union, marquee, group moves", [] {
        const std::vector<DragRect> rs = { makeRect(100, 100, 200, 100), makeRect(500, 300, 200, 100), makeRect(-50, 900, 100, 400) };
        check(same(unionRect(rs), makeRect(-50, 100, 750, 1200)), "the box around them: " + str(unionRect(rs)));
        check(same(unionRect({}), DragRect()), "nothing: an empty box");
        check(same(marqueeRect(800.4f, 50.0f, 90.0f, 450.6f), makeRect(90, 50, 710, 401)), "a marquee dragged up-left is normalised");
        check(marqueePick(rs, makeRect(90, 90, 700, 400)) == std::vector<int>({ 0, 1 }), "the marquee takes what's wholly inside");
        check(marqueePick(rs, makeRect(150, 150, 300, 100)).empty(), "a box only partly inside isn't taken");
        check(marqueePick(rs, makeRect(100, 100, 200, 100)) == std::vector<int>({ 0 }), "edges count as inside");

        int dx = 0, dy = 0;
        const std::vector<DragRect> two = { makeRect(100, 100, 200, 100), makeRect(500, 300, 200, 100) };
        groupMoveDelta(two, 12.0f, -2.0f, 5, 1920, 1080, dx, dy);
        check(dx == 10 && dy == 0, "the group's corner snaps to the nearest grid line: " + std::to_string(dx) + "," + std::to_string(dy));
        groupMoveDelta(two, -500.0f, 0.0f, 1, 1920, 1080, dx, dy);
        check(dx == -100 && dy == 0, "the group stops at the canvas's edge, as one box");
        groupMoveDelta(rs, -10.0f, 0.0f, 1, 1920, 1080, dx, dy);
        check(dx == 0, "a group already over the edge doesn't go further out");
        groupMoveDelta(rs, 30.0f, 0.0f, 1, 1920, 1080, dx, dy);
        check(dx == 30, "but can come back in");
    });

    Test::run("selection: align and distribute", [] {
        const std::vector<DragRect> rs = { makeRect(100, 100, 200, 100), makeRect(500, 300, 100, 50), makeRect(900, 600, 300, 101) };
        auto xs = [](const std::vector<DragRect>& v) { std::vector<int> o; for (const DragRect& r : v) o.push_back(r.x); return o; };
        auto ys = [](const std::vector<DragRect>& v) { std::vector<int> o; for (const DragRect& r : v) o.push_back(r.y); return o; };
        check(xs(alignRects(rs, AL_LEFT)) == std::vector<int>({ 100, 100, 100 }), "left edges on the left");
        check(xs(alignRects(rs, AL_RIGHT)) == std::vector<int>({ 1000, 1100, 900 }), "right edges on the right (1200)");
        check(xs(alignRects(rs, AL_HCENTER)) == std::vector<int>({ 550, 600, 500 }), "centres on the centre (650)");
        check(ys(alignRects(rs, AL_TOP)) == std::vector<int>({ 100, 100, 100 }), "tops");
        check(ys(alignRects(rs, AL_BOTTOM)) == std::vector<int>({ 601, 651, 600 }), "bottoms on 701");
        check(ys(alignRects(rs, AL_VCENTER)) == std::vector<int>({ 350, 375, 350 }), "middles (a half unit rounds down)");
        const std::vector<DragRect> aligned = alignRects(rs, AL_LEFT);
        check(aligned[1].w == 100 && aligned[1].h == 50 && aligned[1].y == 300, "sizes and the other axis never change");
        check(same(alignRects({ rs[0] }, AL_RIGHT)[0], rs[0]), "one box: nothing to line up with");
        check(alignFromName("middle") == AL_VCENTER && alignFromName("centre") == AL_HCENTER && alignFromName("x") == -1, "names");

        // Distribute: the first and last stay, equal gaps between
        const std::vector<DragRect> d = { makeRect(0, 0, 100, 10), makeRect(1000, 0, 100, 10), makeRect(130, 0, 300, 10), makeRect(700, 0, 50, 10) };
        const std::vector<DragRect> out = distributeRects(d, true);
        check(out[0].x == 0 && out[1].x == 1000, "the outer two stay");
        // sizes 550, span 1100: gap (1100 - 550) / 3 = 183.33: 0, 283.33, 766.67, 1000
        check(out[2].x == 283 && out[3].x == 767, "the middle ones evenly spaced, in the order of their centres: " +
              std::to_string(out[2].x) + ", " + std::to_string(out[3].x));
        check(out[2].y == 0 && out[2].w == 300, "only that axis moves");
        const std::vector<DragRect> v = distributeRects({ makeRect(0, 0, 10, 100), makeRect(0, 500, 10, 100), makeRect(0, 900, 10, 100) }, false);
        check(v[1].y == 450, "vertically: " + std::to_string(v[1].y));
        check(same(distributeRects({ rs[0], rs[1] }, true)[1], rs[1]), "two boxes: nothing to space");
        const std::vector<DragRect> tight = distributeRects({ makeRect(0, 0, 300, 10), makeRect(100, 0, 300, 10), makeRect(200, 0, 300, 10) }, true);
        check(tight[1].x == 100, "boxes wider than the space overlap evenly (a negative gap)");
    });

    Test::run("controller: resizing from any handle", [] {
        const DragRect r = makeRect(100, 100, 200, 100);
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = -2; dy <= 2; ++dy) {
                check(same(canvasNudgeResizeFrom(r, CH_SE, dx, dy, 5, 8, 8, 1920, 1080), canvasNudgeResize(r, dx, dy, 5, 8, 8, 1920, 1080)),
                      "from the bottom-right it's the old resize (" + std::to_string(dx) + "," + std::to_string(dy) + ")");
            }
        }
        check(same(canvasNudgeResizeFrom(r, CH_NW, -1, -1, 10, 8, 8, 1920, 1080), makeRect(90, 90, 210, 110)),
              "the top-left corner grows up and left; the bottom-right stays");
        check(same(canvasNudgeResizeFrom(r, CH_NW, 1, 1, 10, 8, 8, 1920, 1080), makeRect(110, 110, 190, 90)), "and shrinks");
        check(same(canvasNudgeResizeFrom(r, CH_N, 5, -1, 10, 8, 8, 1920, 1080), makeRect(100, 90, 200, 110)), "the top edge ignores left/right");
        check(same(canvasNudgeResizeFrom(r, CH_W, -1, 5, 10, 8, 8, 1920, 1080), makeRect(90, 100, 210, 100)), "the left edge ignores up/down");
        check(same(canvasNudgeResizeFrom(r, CH_NE, 1, -1, 10, 8, 8, 1920, 1080), makeRect(100, 90, 210, 110)), "top-right");
        check(same(canvasNudgeResizeFrom(r, CH_SW, -1, 1, 10, 8, 8, 1920, 1080), makeRect(90, 100, 210, 110)), "bottom-left");
        check(same(canvasNudgeResizeFrom(r, CH_W, 1, 0, 500, 8, 8, 1920, 1080), makeRect(292, 100, 8, 100)),
              "the minimum size stops a shrinking edge, the far edge stays");
        check(same(canvasNudgeResizeFrom(r, CH_NW, -1, -1, 500, 8, 8, 1920, 1080), makeRect(0, 0, 300, 200)), "a moving edge stops at the canvas");
        const DragRect over = makeRect(-40, 10, 200, 100);
        check(canvasNudgeResizeFrom(over, CH_W, -1, 0, 10, 8, 8, 1920, 1080).x == -40, "a box already over the edge isn't pulled further out");
        check(canvasNudgeResizeFrom(over, CH_W, 1, 0, 10, 8, 8, 1920, 1080).x == -30, "nor kept there");

        int h = CH_SE;
        std::vector<int> walk;
        for (int i = 0; i < 8; ++i) walk.push_back(h = resizeHandleStep(h, 1));
        check(walk == std::vector<int>({ CH_S, CH_SW, CH_W, CH_NW, CH_N, CH_NE, CH_E, CH_SE }), "Letter down walks clockwise and wraps");
        check(resizeHandleStep(CH_SE, -1) == CH_E && resizeHandleStep(CH_E, -1) == CH_NE, "Letter up the other way");
        check(std::string(resizeHandleLabel(CH_NW)) == "top-left corner", "labels");
    });

    Test::run("controller: several selected, and holding Select", [] {
        check(canvasPress(ECM_MOVE, EP_SELECT, true, true, true).mode == ECM_PICK, "a group skips Resize: Select in Move goes back to Pick");
        check(canvasPress(ECM_MOVE, EP_SELECT, true, true, false).mode == ECM_RESIZE, "one still resizes");
        check(canvasPress(ECM_PICK, EP_BACK, true, true, true).action == ECA_COLLAPSE, "Back in Pick with a group keeps the primary");
        check(canvasPress(ECM_PICK, EP_BACK, true, true, false).action == ECA_LEAVE_EDITOR, "with one it leaves, as before");
        check(canvasPress(ECM_RESIZE, EP_LETTER_DOWN, true, true).action == ECA_HANDLE_NEXT &&
              canvasPress(ECM_RESIZE, EP_LETTER_UP, true, true).action == ECA_HANDLE_PREV, "Letter up/down in Resize step the handle");
        check(canvasPress(ECM_MOVE, EP_LETTER_DOWN, true, true).action == ECA_NONE, "and do nothing in Move");
        check(heldSelectPress(EP_LEFT) == EHA_ADD_DIR && heldSelectPress(EP_LETTER_UP) == EHA_ADD_PREV &&
              heldSelectPress(EP_LETTER_DOWN) == EHA_ADD_NEXT && heldSelectPress(EP_BACK) == EHA_DROP &&
              heldSelectPress(EP_PAGE_UP) == EHA_NONE, "what a press does while Select is held");

        HeldSelect held;
        check(!held.release(false), "nothing armed: nothing happens");
        held.press();
        check(!held.release(true), "still held: not yet");
        check(held.release(false), "let go with nothing else pressed: Select acts");
        check(!held.release(false), "once");
        held.press();
        check(held.combine(), "a direction while held is the combination");
        check(!held.release(false), "then letting go doesn't also act");
        check(!held.combine(), "and nothing is armed any more");
        held.press();
        held.cancel();
        check(!held.release(false), "cancelled (a dialog opened)");
        // One add per press: a held direction's repeats are swallowed, but still count as used
        held.press();
        check(held.combine(false) && !held.combine(true) && !held.combine(true), "the press acts, its repeats don't");
        check(!held.release(false), "and letting go doesn't act either");
        held.press();
        check(!held.combine(true), "a repeat alone doesn't act");
        check(!held.release(false), "but Select doesn't act after it either");
        // A second press while armed keeps what was done
        held.press();
        held.combine(false);
        held.press();
        check(!held.release(false), "used survives a second press");
    });

    Test::run("widget editor: several widgets at once", [] {
        const std::string root = makeRoot("selection_widgets");
        EditorSession s;
        check(openThree(root, s), "a theme with three texts");
        s.select(0);
        s.toggleSelected(1);
        check(s.selection() == std::vector<int>({ 0, 1 }) && s.selected() == 1, "Ctrl-click adds as the primary");
        s.addSelected(2);
        check(s.selected() == 2 && s.dropPrimary() && s.selection() == std::vector<int>({ 0, 1 }) && s.selected() == 1,
              "dropping the primary hands it to the one picked before");
        s.selectAll();
        check(s.selectionCount() == 3 && s.selected() == 1, "select all keeps the primary");

        // A nudge moves them all, one undo step for a run
        check(s.moveBy(10, 0) && s.moveBy(10, 0), "nudged twice");
        check(widgetRect(s, 0).x == 120 && widgetRect(s, 1).x == 520 && widgetRect(s, 2).x == 920, "all three moved 20");
        s.endCoalesce();
        check(s.undo() && widgetRect(s, 0).x == 100 && widgetRect(s, 2).x == 900, "one undo takes the run back");
        check(s.selectionCount() == 3, "and the selection with it");
        check(s.moveBy(-1000, 0) && widgetRect(s, 0).x == 0 && widgetRect(s, 1).x == 400, "the group stops at the edge as one box");
        s.endCoalesce();
        s.undo();

        // Align and distribute: one step each
        check(s.alignSelected(AL_TOP) && widgetRect(s, 1).y == 100 && widgetRect(s, 2).y == 100, "tops lined up");
        check(s.undo() && widgetRect(s, 2).y == 600, "undone in one");
        s.setSelection({ 0, 1, 2 });
        s.select(1);
        s.setRect(250, 300, 200, 100);
        s.setSelection({ 0, 1, 2 });
        check(s.distributeSelected(true) && widgetRect(s, 1).x == 500, "distributed: the middle one centred between (gap 200)");

        // Duplicate: copies with new ids, 20 down and right, selected
        s.setSelection({ 0, 2 });
        check(s.duplicateSelected(), "duplicated");
        const auto& w = s.page().widgets;
        check(w.size() == 5 && s.selection() == std::vector<int>({ 3, 4 }), "two copies, selected");
        check(w[3].x == 120 && w[3].y == 120 && w[4].x == 920 && w[4].y == 620, "offset by 20");
        check(w[3].id != w[0].id && w[4].id != w[2].id && w[3].id != w[4].id, "with ids of their own");
        check(s.removeSelected() && s.page().widgets.size() == 3 && s.selectionCount() == 0, "delete takes every selected widget");
        check(s.undo() && s.page().widgets.size() == 5 && s.selection() == std::vector<int>({ 3, 4 }), "undo brings them back, selected");
        check(s.undo() && s.page().widgets.size() == 3, "and the duplicate was one step");
        // A copy at the bottom-right edge goes up and left instead
        s.select(2);
        s.setRect(1720, 980, 200, 100);
        check(s.duplicateSelected() && s.page().widgets.back().x == 1700 && s.page().widgets.back().y == 960, "a copy that wouldn't fit goes the other way");
        s.undo();
        s.undo();
    });

    Test::run("widget editor: the mouse with several", [] {
        const std::string root = makeRoot("selection_widgets_mouse");
        EditorSession s;
        check(openThree(root, s), "a theme with three texts");
        // A marquee from empty canvas takes what's wholly inside it
        drag(s, 50, 50, 750, 450);
        check(s.selection() == std::vector<int>({ 0, 1 }), "the marquee selected the first two");
        // Ctrl-click adds, Ctrl-click again takes away
        s.pointerDown(unitFit(), 1000, 650, 4.0f, true);
        s.pointerUp();
        check(s.selectionCount() == 3 && s.selected() == 2, "Ctrl-click added the third");
        s.pointerDown(unitFit(), 1000, 650, 4.0f, true);
        s.pointerUp();
        check(s.selection() == std::vector<int>({ 0, 1 }), "and took it away");
        // Dragging one of them moves both, snapped as a group, one step
        drag(s, 600, 350, 652, 371);
        check(widgetRect(s, 0).x == 150 && widgetRect(s, 0).y == 120 && widgetRect(s, 1).x == 550 && widgetRect(s, 1).y == 320,
              "both moved 50, 20: " + str(widgetRect(s, 0)) + " / " + str(widgetRect(s, 1)));
        check(widgetRect(s, 2).x == 900, "the other stayed");
        check(s.undo() && widgetRect(s, 0).x == 100 && widgetRect(s, 1).x == 500, "one undo step");
        // A click on one of them (no drag) selects just it; on empty canvas, nothing
        s.setSelection({ 0, 1 });
        drag(s, 200, 150, 200, 150);
        check(s.selection() == std::vector<int>({ 0 }), "a click picks one out of the group");
        drag(s, 1500, 1000, 1500, 1000);
        check(s.selectionCount() == 0, "a click on empty canvas selects nothing");
        // A marquee with Ctrl adds to what's selected, and may start on a widget
        s.select(2);
        drag(s, 50, 50, 350, 250, true);
        check(s.selection() == std::vector<int>({ 2, 0 }), "the additive marquee added the first");
        // Several selected: no handles, the box moves instead
        s.setSelection({ 0, 1 });
        drag(s, 300, 200, 340, 200);   // the first one's bottom-right corner
        check(widgetRect(s, 0).w == 200 && widgetRect(s, 0).x == 140 && widgetRect(s, 1).x == 540, "a group doesn't resize, it moves");

        // Scripts
        std::string note;
        s.select(0);
        check(runSessionCommand(s, parseEditorCommand("addselect 2"), note) == CommandResult::Ok && s.selectionCount() == 2, "addselect: " + note);
        check(runSessionCommand(s, parseEditorCommand("align left"), note) == CommandResult::Ok && widgetRect(s, 2).x == 140, "align: " + note);
        check(runSessionCommand(s, parseEditorCommand("distribute x"), note) == CommandResult::Failed, "distribute needs h or v");
        check(runSessionCommand(s, parseEditorCommand("selectall"), note) == CommandResult::Ok && s.selectionCount() == 3, "selectall: " + note);
        check(runSessionCommand(s, parseEditorCommand("marquee 1500 50 1900 100"), note) == CommandResult::Ok && s.selectionCount() == 0,
              "a marquee over nothing selects nothing");
        check(runSessionCommand(s, parseEditorCommand("marquee 200 150 1900 1000"), note) == CommandResult::Failed, "one that starts on a widget isn't a marquee");
        check(runSessionCommand(s, parseEditorCommand("clickadd 600 350"), note) == CommandResult::Ok, "clickadd: " + note);
        check(runSessionCommand(s, parseEditorCommand("duplicate"), note) == CommandResult::Ok && s.page().widgets.size() == 4, "duplicate: " + note);
    });

    Test::run("layout editor: several elements at once, byte for byte", [] {
        const std::string root = makePageRoot("selection_layout");
        LayoutSession s;
        std::string error;
        check(s.open(root, "Hand", "layout.xml", error), "opened: " + error);
        feed(s);
        s.selectPath("image[0]");
        s.addSelected(s.doc().find("image[1]"));
        s.addSelected(s.doc().find("text[0]"));
        check(s.selectionCount() == 3 && s.element(s.selected()).path == "text[0]", "three selected, the text the primary");
        s.addSelected(0);
        check(s.selectionCount() == 3 && !s.isSelected(0), "the page itself is never one of several");

        // Straight down: x stays as written ("center" too)
        check(s.moveBy(0, 10), "nudged down");
        check(attr(s, "image[1]", "x") == "center" && attr(s, "image[1]", "y") == "310" && attr(s, "image[0]", "y") == "110" &&
              attr(s, "text[0]", "y") == "610", "every y moved, x untouched");
        s.endCoalesce();
        check(s.undo() && s.text() == PAGE, "one undo back to the exact bytes");
        feed(s);   // as the screen's rebuild would
        // Sideways: the centred one becomes a number, as a drag would make it
        check(s.moveBy(10, 0), "nudged right");
        check(attr(s, "image[1]", "x") == "970" && attr(s, "image[0]", "x") == "110" && attr(s, "text[0]", "x") == "910", "x written as numbers");
        s.endCoalesce();
        s.undo();
        feed(s);

        // Align: one text change, only the values
        check(s.alignSelected(AL_TOP), "aligned");
        check(attr(s, "image[1]", "y") == "100" && attr(s, "text[0]", "y") == "100" && attr(s, "image[0]", "y") == "100", "tops at 100");
        check(s.text().find("<!-- three boxes -->\r\n") != std::string::npos, "the rest as it was");
        check(s.undo() && s.text() == PAGE && !s.canUndo(), "one step");
        feed(s);
        check(s.distributeSelected(true), "distributed");
        // left edges 100 (w 200), 860 (centred), 900 (w 200): gap 200, so the middle one's left is 500 and its x (the centre) 600
        check(attr(s, "image[1]", "x") == "600", "the centred one moves to the middle: x=" + attr(s, "image[1]", "x"));
        s.undo();
        feed(s);

        // A read-only box can be selected but doesn't move
        s.addSelected(s.doc().find("menu[0]/itemDefaults[0]"));
        check(s.movableSelection().size() == 3, "the menu's generated slot isn't movable");
        check(s.moveBy(5, 0) && attr(s, "menu[0]/itemDefaults[0]", "x") == "(unset)", "a nudge leaves it");
        s.endCoalesce();
        s.undo();
        feed(s);

        // Duplicate several: each copy right after its original, the copies selected
        s.setSelection({ s.doc().find("image[0]"), s.doc().find("image[1]"), s.doc().find("text[0]") });
        check(s.duplicateSelected(), "duplicated");
        std::vector<std::string> paths;
        for (int i : s.selection()) paths.push_back(s.element(i).path);
        check(paths == std::vector<std::string>({ "image[1]", "image[3]", "text[1]" }), "the copies are selected: " +
              (paths.empty() ? std::string() : paths[0] + " " + (paths.size() > 1 ? paths[1] : "") + " " + (paths.size() > 2 ? paths[2] : "")));
        check(attr(s, "image[1]", "src") == "a.png" && attr(s, "image[3]", "src") == "b.png" && attr(s, "text[1]", "value") == "c",
              "copies of the right elements, in order");
        check(s.doc().find("image[3]/onMenuScroll[0]") > 0, "animations and all");
        check(s.deleteSelected() && s.text() == PAGE, "deleting the copies gives the page back exactly");
        check(s.undo() && s.undo() && s.text() == PAGE && !s.canUndo(), "two steps: duplicate, delete");

        // An item and its menu: the item goes with the menu
        s.setSelection({ s.doc().find("menu[0]"), s.doc().find("menu[0]/itemDefaults[0]") });
        check(s.duplicateSelected() && s.doc().find("menu[1]/itemDefaults[0]") > 0 && s.doc().find("menu[0]/itemDefaults[1]") < 0,
              "the menu is copied once, its defaults inside it");
        s.undo();
        feed(s);

        // Delete: a parent and its child selected, an unselected sibling between, another selected one before
        s.setSelection({ s.doc().find("image[1]"), s.doc().find("menu[0]/itemDefaults[0]"), s.doc().find("menu[0]") });
        check(s.deleteSelected(), "deleted");
        check(s.doc().find("image[1]") < 0 && s.doc().find("menu[0]") < 0 && s.doc().find("image[0]") > 0 && s.doc().find("text[0]") > 0,
              "the image and the menu (with its defaults) are gone; the text between stays");
        check(attr(s, "text[0]", "value") == "c" && s.text().find("<!-- three boxes -->") != std::string::npos, "the rest as it was");
        check(s.undo() && s.text() == PAGE, "one undo gives the bytes back");
        feed(s);

        // The selection changed: the next nudge is a step of its own, not merged into the group's
        s.setSelection({ s.doc().find("image[0]"), s.doc().find("text[0]") });
        check(s.moveBy(5, 0), "a group nudge");
        s.toggleSelected(s.doc().find("image[0]"));   // the same primary, a smaller set
        check(s.moveBy(5, 0), "a nudge of what's left");
        check(s.undo() && attr(s, "text[0]", "x") == "905" && attr(s, "image[0]", "x") == "105", "undo takes only the second back");
        check(s.undo() && s.text() == PAGE, "then the first");
    });

    Test::run("layout editor: duplicating several on one line", [] {
        const std::string root = makeRoot("selection_inline");
        const std::string page = "<layout width=\"1920\" height=\"1080\"><image src=\"a.png\"/><image src=\"b.png\"/></layout>";
        writeFile(root + "/layouts/Hand/layout.xml", page);
        LayoutSession s;
        std::string error;
        check(s.open(root, "Hand", "layout.xml", error), "open: " + error);
        s.setSelection({ s.doc().find("image[0]"), s.doc().find("image[1]") });
        check(s.duplicateSelected(), "duplicated");
        checkEqual(s.text(), "<layout width=\"1920\" height=\"1080\"><image src=\"a.png\"/> <image src=\"a.png\"/><image src=\"b.png\"/> <image src=\"b.png\"/></layout>",
                   "each copy right after its original, inline");
        std::vector<std::string> paths;
        for (int i : s.selection()) paths.push_back(s.element(i).path);
        check(paths == std::vector<std::string>({ "image[1]", "image[3]" }) && attr(s, "image[1]", "src") == "a.png" && attr(s, "image[3]", "src") == "b.png",
              "the copies are selected");
        check(s.deleteSelected() && s.doc().find("image[1]") > 0 && s.doc().find("image[2]") < 0 && attr(s, "image[1]", "src") == "b.png",
              "deleting the copies leaves the two originals");
        check(s.undo() && s.undo() && s.text() == page, "and undo gives the bytes back");
    });

    Test::run("layout editor: the mouse with several, and linked steps", [] {
        const std::string root = makePageRoot("selection_layout_mouse");
        LayoutSession s;
        std::string error;
        check(s.open(root, "Hand", "layout.xml", error), "opened: " + error);
        feed(s);
        drag(s, 50, 50, 1150, 450);
        std::vector<std::string> paths;
        for (int i : s.selection()) paths.push_back(s.element(i).path);
        check(paths == std::vector<std::string>({ "image[0]", "image[1]" }), "the marquee took the two images");
        drag(s, 1000, 650, 1000, 650, true);
        check(s.selectionCount() == 3, "Ctrl-click added the text");
        // Straight down keeps x="center"; one step
        drag(s, 960, 350, 961, 400);
        check(attr(s, "image[1]", "x") == "center" && attr(s, "image[1]", "y") == "350" && attr(s, "text[0]", "y") == "650",
              "all moved down 50, centre kept: y=" + attr(s, "image[1]", "y"));
        check(s.undo() && s.text() == PAGE, "one step");
        check(s.selectionCount() == 3, "selection restored");
        feed(s);
        drag(s, 200, 150, 200, 150);
        check(s.selectionCount() == 1 && s.element(s.selected()).path == "image[0]", "a click picks one out of the group");
        drag(s, 1300, 900, 1300, 900);
        check(s.selected() < 0, "empty canvas: nothing");

        // Linked steps (two pages edited together)
        s.selectPath("text[0]");
        s.setAttribute("fontColor", "ff0000");
        check(s.undoLink() == 0, "an ordinary step isn't linked");
        s.linkUndo(7);
        check(s.undoLink() == 7, "tagged");
        std::string text = s.text();
        const size_t at = text.find("value=\"c\"");
        check(s.applyLinked(text.replace(at, 9, "value=\"d\""), 7) && attr(s, "text[0]", "value") == "d", "applied");
        check(s.undoLink() == 7 && s.undo() && attr(s, "text[0]", "value") == "c" && !s.element(s.selected()).has("fontColor"),
              "merged into the tagged step: one undo takes both");
        check(s.redoLink() == 7 && s.redo() && s.undoLink() == 7, "redo keeps the tag");
        s.undo();
        check(s.applyLinked(PAGE, 9) == false, "the same text is no change");

        // End boxes for other events
        DragRect end;
        const int b = s.doc().find("image[1]");
        check(s.eventEndBox(b, "onMenuScroll", end) && end.x == 900 && end.y == 300, "onMenuScroll ends with x 1000 (left 900): " + str(end));
        check(s.eventEndBox(b, "onEnter", end) && end.y == 350, "onEnter brings y to 350");
        check(!s.eventEndBox(b, "onHighlightEnter", end), "an alpha-only event doesn't move it");
        check(!s.eventEndBox(b, "onExit", end), "an event it doesn't have");
        const std::vector<std::string> moving = s.movingEvents(b);
        check(moving == std::vector<std::string>({ "onEnter", "onMenuScroll" }), "the events that move it");
        std::string note;
        s.select(b);
        check(runLayoutCommand(s, parseEditorCommand("endbox onMenuScroll"), note) == CommandResult::Ok, "endbox EVENT");
        check(note == "onMenuScroll ends at 900,300 200x100", "its note: " + note);
        check(runLayoutCommand(s, parseEditorCommand("addpath text[0]"), note) == CommandResult::Ok && s.selectionCount() == 2, "addpath: " + note);
        check(runLayoutCommand(s, parseEditorCommand("align bottom"), note) == CommandResult::Ok && attr(s, "image[1]", "y") == "600", "align: " + note);
    });
});

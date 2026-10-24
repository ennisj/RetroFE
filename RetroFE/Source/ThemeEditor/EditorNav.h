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
#pragma once

// The editor's controller decisions: focus regions, the canvas's pick, move
// and resize modes, directional widget picking, nudges, the properties
// panel's rows and which overlay owns a press. Deciding them here keeps the
// UI loop thin and testable. Both theme editor screens are driven by
// RetroFE's mapped controls through these (Manager/Themes/app/EditorPad.h);
// the second half of this file holds the decisions added for that.
//
// Ported from One Saucier's theme editor (app/editor/editor_nav.h). Pure,
// header-only.

#include "EditorCanvas.h"
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace ThemeEditor
{
    // Focus regions, in reading order. EF_TOOLBAR is the canvas header row
    // between the theme tile and the canvas.
    enum EdFocus { EF_THEME = 0, EF_CANVAS, EF_ACTIONS, EF_SELECTION, EF_TOOLBAR, EF_PROPS };

    // The canvas header's stops in visual order (the controller walk)
    enum EdToolbarStop { ETB_UNDO = 0, ETB_REDO, ETB_CMUTE, ETB_WIRE, ETB_MEDIA, ETB_TEXT, ETB_LABELS,
                         ETB_PREV, ETB_NEXT, ETB_ADD, ETB_TRASH, ETB_COUNT };

    // In the vertical layout the header wraps after the Show toggles:
    // row 0 = ETB_UNDO..ETB_LABELS, row 1 = ETB_PREV..ETB_TRASH
    static inline int toolbarRowOf(int stop) { return stop >= ETB_PREV ? 1 : 0; }

    // Where up/down lands from `stop`, or -1 when the move leaves the toolbar.
    // The horizontal position carries over, clamped to the shorter row. A
    // one-row header (two_rows false) always leaves.
    static inline int toolbarRowStep(int stop, int dy, bool twoRows)
    {
        if (!twoRows || dy == 0) return -1;
        if (stop < ETB_UNDO || stop >= ETB_COUNT) return -1;
        if (dy > 0) {
            if (toolbarRowOf(stop) != 0) return -1;
            const int i = stop - ETB_UNDO;
            const int last = ETB_TRASH - ETB_PREV;
            return ETB_PREV + (i > last ? last : i);
        }
        if (toolbarRowOf(stop) != 1) return -1;
        return ETB_UNDO + (stop - ETB_PREV);
    }

    // The canvas's controller modes. Move and resize are an overlay nudge
    // session like a mouse drag: the project commits once when the mode ends.
    enum EdCanvasMode { ECM_PICK = 0, ECM_MOVE, ECM_RESIZE, ECM_EDIT };

    // From `cur`, one step in the dx/dy direction. The candidate must lie
    // beyond the current centre that way; the nearest by along + 2 * across
    // wins, so a widget straight ahead beats a nearer diagonal one.
    // -1 = no rects, 0 = cur invalid (first pick), cur = nothing that way.
    static inline int rectNeighbor(const std::vector<DragRect>& rs, int cur, int dx, int dy)
    {
        const int n = static_cast<int>(rs.size());
        if (n == 0) return -1;
        if (cur < 0 || cur >= n) return 0;
        const DragRect& c = rs[static_cast<size_t>(cur)];
        const float cx = static_cast<float>(c.x) + static_cast<float>(c.w) * 0.5f;
        const float cy = static_cast<float>(c.y) + static_cast<float>(c.h) * 0.5f;
        int best = cur;
        float bestScore = 0.0f;
        bool found = false;
        for (int i = 0; i < n; ++i) {
            if (i == cur) continue;
            const DragRect& w = rs[static_cast<size_t>(i)];
            const float ax = (static_cast<float>(w.x) + static_cast<float>(w.w) * 0.5f) - cx;
            const float ay = (static_cast<float>(w.y) + static_cast<float>(w.h) * 0.5f) - cy;
            const float along = ax * static_cast<float>(dx) + ay * static_cast<float>(dy);
            if (along <= 0.5f) continue;
            const float perp = (dx != 0) ? (ay < 0 ? -ay : ay) : (ax < 0 ? -ax : ax);
            const float score = along + 2.0f * perp;
            if (!found || score < bestScore) { found = true; best = i; bestScore = score; }
        }
        return found ? best : cur;
    }

    static inline int canvasNeighbor(const ThemePage& page, int cur, int dx, int dy)
    {
        std::vector<DragRect> rs;
        rs.reserve(page.widgets.size());
        for (const WidgetInstance& w : page.widgets) {
            DragRect r;
            r.x = w.x; r.y = w.y; r.w = w.w; r.h = w.h;
            rs.push_back(r);
        }
        return rectNeighbor(rs, cur, dx, dy);
    }

    // Controller nudges over the overlay rect. Like drags they keep the widget
    // on the canvas but respect one that already overhangs it: the start is
    // always legal, so a nudge never yanks an oversized wheel into place.
    static inline DragRect canvasNudgeMove(const DragRect& r, int dx, int dy, int step, int canvasW, int canvasH)
    {
        DragRect o = r;
        o.x += dx * step;
        o.y += dy * step;
        int loX = canvasW - o.w < 0 ? canvasW - o.w : 0;
        int hiX = canvasW - o.w > 0 ? canvasW - o.w : 0;
        int loY = canvasH - o.h < 0 ? canvasH - o.h : 0;
        int hiY = canvasH - o.h > 0 ? canvasH - o.h : 0;
        if (r.x < loX) loX = r.x;
        if (r.x > hiX) hiX = r.x;
        if (r.y < loY) loY = r.y;
        if (r.y > hiY) hiY = r.y;
        if (o.x > hiX) o.x = hiX;
        if (o.y > hiY) o.y = hiY;
        if (o.x < loX) o.x = loX;
        if (o.y < loY) o.y = loY;
        return o;
    }

    // Resize nudges act on the bottom-right edge; the far edge stops at the
    // canvas (or where it already was); the minimum size outranks the clamp
    static inline DragRect canvasNudgeResize(const DragRect& r, int dx, int dy, int step, int minW, int minH,
                                             int canvasW, int canvasH)
    {
        if (minW < 1) minW = 1;
        if (minH < 1) minH = 1;
        DragRect o = r;
        o.w += dx * step;
        o.h += dy * step;
        const int hiX2 = r.x + r.w > canvasW ? r.x + r.w : canvasW;
        const int hiY2 = r.y + r.h > canvasH ? r.y + r.h : canvasH;
        if (o.x + o.w > hiX2) o.w = hiX2 - o.x;
        if (o.y + o.h > hiY2) o.h = hiY2 - o.y;
        if (o.w < minW) o.w = minW;
        if (o.h < minH) o.h = minH;
        return o;
    }

    // Properties panel rows: name, layer, alpha, then one per variable
    enum EdPanelRow { EPR_NAME = 0, EPR_LAYER = 1, EPR_ALPHA = 2, EPR_VAR0 = 3 };
    static inline int panelRowCount(int varCount) { return 3 + (varCount < 0 ? 0 : varCount); }
    static inline int panelRowVar(int row) { return row - EPR_VAR0; }

    // The number a ladder rung's numeric row shows, and so where its stepper
    // starts: its own value, else the inherited one, else the computed
    // geometry, else alpha's 1, else 0
    static inline double ladderAttrEffective(bool hasOwn, double own, bool hasInherited, double inherited,
                                             bool isGeometry, double calculated, bool isAlpha)
    {
        if (hasOwn) return own;
        if (hasInherited) return inherited;
        if (isGeometry) return calculated;
        if (isAlpha) return 1.0;
        return 0.0;
    }

    // The position of `wanted` in `items` with `excluded` left out (the index a
    // picker built from the filtered list uses); -1 when it isn't there
    template <class StringList, class String>
    static inline int indexExcluding(const StringList& items, const String& excluded, const String& wanted)
    {
        int k = 0;
        for (const auto& it : items) {
            if (it == excluded) continue;
            if (it == wanted) return k;
            ++k;
        }
        return -1;
    }

    // Which overlay gets a controller press: the one on top. Leaf overlays
    // (keyboard, file browser, dropdown) first, then the dialogs that host
    // them, then the editor itself.
    enum EdInputOwner {
        EIO_EDITOR = 0, EIO_OSK, EIO_FILE_BROWSER, EIO_DROPDOWN, EIO_MEDIA, EIO_COLOR_PICKER,
        EIO_ADD_WIDGET, EIO_TPL_HUB, EIO_CMENU, EIO_XCONFIRM, EIO_TPL_DIALOG,
    };

    struct EdOverlays
    {
        bool osk = false, fileBrowser = false, dropdown = false;
        bool media = false, colorPicker = false, addWidget = false;
        bool tplHub = false, cmenu = false, xconfirm = false, tplDialog = false;
    };

    static inline EdInputOwner editorInputOwner(const EdOverlays& o)
    {
        if (o.osk) return EIO_OSK;
        if (o.fileBrowser) return EIO_FILE_BROWSER;
        if (o.dropdown) return EIO_DROPDOWN;
        if (o.media) return EIO_MEDIA;
        if (o.colorPicker) return EIO_COLOR_PICKER;
        if (o.addWidget) return EIO_ADD_WIDGET;
        if (o.tplHub) return EIO_TPL_HUB;
        if (o.cmenu) return EIO_CMENU;
        if (o.xconfirm) return EIO_XCONFIRM;
        if (o.tplDialog) return EIO_TPL_DIALOG;
        return EIO_EDITOR;
    }

    // Where a rung value chosen in an overlay lands: -1 = the strip's
    // itemDefaults, >= 0 = that rung, LADDER_COMMIT_DROP = nowhere (the
    // selection went away; promoting it to the defaults would restyle every rung)
    enum { LADDER_COMMIT_DROP = -2 };
    static inline int ladderCommitIndex(bool openedOnDefaults, int selectedRung)
    {
        if (openedOnDefaults) return -1;
        return selectedRung >= 0 ? selectedRung : LADDER_COMMIT_DROP;
    }

    // An in-place edit lands on the widget it began on, if it still exists;
    // -1 = drop the edit
    static inline int editTargetIndex(int beganOn, int widgetCount)
    {
        return (beganOn >= 0 && beganOn < widgetCount) ? beganOn : -1;
    }

    // A theme colour: 6 hex digits, optionally 2 more (alpha, ignored) or a
    // leading '#'. Either case.
    static inline bool hexRgb(const std::string& text, unsigned& rgb)
    {
        const size_t at = (!text.empty() && text[0] == '#') ? 1 : 0;
        const size_t n = text.size() - at;
        if (n != 6 && n != 8) return false;
        unsigned v = 0;
        for (size_t i = 0; i < n; ++i) {
            const char c = text[at + i];
            unsigned d;
            if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
            else return false;
            if (i < 6) v = (v << 4) | d;
        }
        rgb = v;
        return true;
    }

    // The whole collection tree in pre-order for the template dropdowns, via
    // the caller's child lister. A menu cycle lists each name once; maxDepth
    // caps runaway nesting.
    static inline void collectionTreeFlatten(const std::string& root,
                                             const std::function<std::vector<std::string>(const std::string&)>& kidsOf,
                                             std::vector<std::string>& names, std::vector<int>& depths, int maxDepth = 6)
    {
        std::set<std::string> seen;
        struct Frame { std::string name; int depth; };
        std::vector<Frame> stack;
        stack.push_back({ root, 0 });
        while (!stack.empty()) {
            Frame f = stack.back();
            stack.pop_back();
            if (f.name.empty() || !seen.insert(f.name).second) continue;
            names.push_back(f.name);
            depths.push_back(f.depth);
            if (f.depth >= maxDepth) continue;
            std::vector<std::string> kids = kidsOf(f.name);
            for (size_t i = kids.size(); i > 0; --i) stack.push_back({ kids[i - 1], f.depth + 1 });
        }
    }

    // ---- Controller editing in this fork's editors --------------------------
    //
    // Both screens have the same four regions: the toolbar across the top,
    // the theme (or pages and elements) panel on the left, the canvas in the
    // middle and the properties on the right (EF_TOOLBAR, EF_THEME,
    // EF_CANVAS, EF_PROPS). Inside a panel or a dialog ImGui's own gamepad
    // navigation moves between items; the presses below decide what reaches
    // it and what the editor does itself.

    // RetroFE's mapped controls as the editor sees them
    enum EdPress { EP_UP = 0, EP_DOWN, EP_LEFT, EP_RIGHT, EP_SELECT, EP_BACK, EP_PAGE_UP, EP_PAGE_DOWN,
                   EP_LETTER_UP, EP_LETTER_DOWN, EP_COUNT };

    static inline void pressDirection(int press, int& dx, int& dy)
    {
        dx = press == EP_LEFT ? -1 : press == EP_RIGHT ? 1 : 0;
        dy = press == EP_UP ? -1 : press == EP_DOWN ? 1 : 0;
    }

    // Where leaving a region in a direction lands; -1 = nowhere, stay
    static inline int regionStep(int from, int dx, int dy)
    {
        switch (from) {
        case EF_TOOLBAR: return dy > 0 ? EF_CANVAS : -1;
        case EF_THEME:
            if (dx > 0) return EF_CANVAS;
            return dy < 0 ? EF_TOOLBAR : -1;
        case EF_CANVAS:
            if (dx < 0) return EF_THEME;
            if (dx > 0) return EF_PROPS;
            return dy < 0 ? EF_TOOLBAR : -1;
        case EF_PROPS:
            if (dx < 0) return EF_CANVAS;
            return dy < 0 ? EF_TOOLBAR : -1;
        default: return -1;
        }
    }

    // Page Up / Page Down: the previous or next region, in reading order
    static inline int regionCycle(int from, int dir)
    {
        static const int order[4] = { EF_TOOLBAR, EF_THEME, EF_CANVAS, EF_PROPS };
        int at = 2;   // anything else counts as the canvas
        for (int i = 0; i < 4; ++i) {
            if (order[i] == from) at = i;
        }
        return order[(at + (dir < 0 ? 3 : 1)) % 4];
    }

    // What a focused item in a panel or dialog is, as far as the controls go
    enum EdItemKind {
        EIK_OTHER = 0,   // buttons, checkboxes, list rows...: ImGui's navigation activates them
        EIK_TEXT,        // a text field: Select types into it on the on-screen keyboard
        EIK_NUMBER,      // a number: Left / Right step it, Select types one
        EIK_CHOICE,      // a list of choices: Left / Right pick the previous / next, Select opens the list
        EIK_NODE,        // a tree node or section header: Left / Right fold it, never leave the panel
    };
    // Where the focus is: a panel (a region), one of the editor's dialogs, or
    // a drop-down list (an open combo; inside a dialog too)
    enum EdLayer { EL_PANEL = 0, EL_DIALOG, EL_LIST };

    enum EdRoute {
        ER_IGNORE = 0,
        ER_FORWARD,        // to ImGui's navigation
        ER_FORWARD_EDGE,   // to ImGui's navigation; when the focus doesn't move, the region in that direction
        ER_ADJUST,         // the item steps itself (previous / next)
        ER_KEYBOARD,       // the on-screen keyboard opens for the item
        ER_CLOSE_LIST,     // ImGui's cancel: closes the drop-down list
        ER_CLOSE_DIALOG,   // the editor closes its dialog
        ER_LEAVE_REGION,   // back to the canvas
    };

    static inline EdRoute panelRoute(int kind, int press, int layer)
    {
        if (press == EP_BACK) {
            if (layer == EL_LIST) return ER_CLOSE_LIST;
            return layer == EL_DIALOG ? ER_CLOSE_DIALOG : ER_LEAVE_REGION;
        }
        if (press == EP_SELECT) return kind == EIK_TEXT || kind == EIK_NUMBER ? ER_KEYBOARD : ER_FORWARD;
        if (press == EP_UP || press == EP_DOWN) return layer == EL_PANEL ? ER_FORWARD_EDGE : ER_FORWARD;
        if (press == EP_LEFT || press == EP_RIGHT) {
            if (layer != EL_LIST && (kind == EIK_NUMBER || kind == EIK_CHOICE)) return ER_ADJUST;
            if (kind == EIK_NODE || layer != EL_PANEL) return ER_FORWARD;
            return ER_FORWARD_EDGE;
        }
        return ER_IGNORE;
    }

    // The canvas: what a press does in each mode
    enum EdCanvasAction {
        ECA_NONE = 0,
        ECA_PICK,           // select the nearest element that way (or leave the canvas)
        ECA_MOVE,           // nudge the selection
        ECA_RESIZE,         // nudge its bottom-right corner
        ECA_MODE,           // switch to the decision's mode (the change so far is kept)
        ECA_ADD,            // nothing to select: open Add widget / Add element
        ECA_CANT_MOVE,      // the selection can't be moved here (say why)
        ECA_LEAVE_EDITOR,   // Back in Pick mode
        ECA_PREV, ECA_NEXT, // Letter up / down: the previous / next element in list order
    };
    struct EdCanvasDecision
    {
        EdCanvasAction action = ECA_NONE;
        EdCanvasMode mode = ECM_PICK;
    };

    // Select goes Pick -> Move -> Resize -> Pick; Back goes back to Pick, or
    // leaves the editor from Pick
    static inline EdCanvasDecision canvasPress(EdCanvasMode mode, int press, bool hasSelection, bool movable)
    {
        EdCanvasDecision d;
        d.mode = mode;
        if (mode != ECM_PICK && !(hasSelection && movable)) {
            d.mode = ECM_PICK;   // the selection went away mid-move
            mode = ECM_PICK;
        }
        switch (press) {
        case EP_UP: case EP_DOWN: case EP_LEFT: case EP_RIGHT:
            d.action = mode == ECM_MOVE ? ECA_MOVE : mode == ECM_RESIZE ? ECA_RESIZE : ECA_PICK;
            break;
        case EP_SELECT:
            if (mode == ECM_PICK) {
                if (!hasSelection) d.action = ECA_ADD;
                else if (!movable) d.action = ECA_CANT_MOVE;
                else { d.action = ECA_MODE; d.mode = ECM_MOVE; }
            }
            else {
                d.action = ECA_MODE;
                d.mode = mode == ECM_MOVE ? ECM_RESIZE : ECM_PICK;
            }
            break;
        case EP_BACK:
            if (mode == ECM_PICK) d.action = ECA_LEAVE_EDITOR;
            else { d.action = ECA_MODE; d.mode = ECM_PICK; }
            break;
        case EP_LETTER_UP:
            if (mode == ECM_PICK) d.action = ECA_PREV;
            break;
        case EP_LETTER_DOWN:
            if (mode == ECM_PICK) d.action = ECA_NEXT;
            break;
        default:
            break;
        }
        return d;
    }

    // Pick mode, one step: the nearest rect that way (rectNeighbor), the one
    // nearest the middle of the canvas when nothing is selected, or -1 when
    // there's nothing that way (the focus leaves the canvas)
    static inline int pickStep(const std::vector<DragRect>& rs, int cur, int dx, int dy, int canvasW, int canvasH)
    {
        const int n = static_cast<int>(rs.size());
        if (n == 0) return -1;
        if (cur < 0 || cur >= n) {
            int best = 0;
            double bestD = -1.0;
            for (int i = 0; i < n; ++i) {
                const double ex = rs[i].x + rs[i].w * 0.5 - canvasW * 0.5;
                const double ey = rs[i].y + rs[i].h * 0.5 - canvasH * 0.5;
                const double d = ex * ex + ey * ey;
                if (bestD < 0.0 || d < bestD) { bestD = d; best = i; }
            }
            return best;
        }
        const int next = rectNeighbor(rs, cur, dx, dy);
        return next == cur ? -1 : next;
    }

    // Move and Resize nudge by the snap grid (or 1 without snapping); holding
    // the direction speeds up: x4 after 4 repeats, x10 after 12
    static inline int nudgeStep(int base, int repeats)
    {
        if (base < 1) base = 1;
        if (repeats >= 12) return base * 10;
        if (repeats >= 4) return base * 4;
        return base;
    }

    // The previous / next index in a list of n, wrapping; from none, the first or last
    static inline int listStep(int cur, int n, int dir)
    {
        if (n <= 0) return -1;
        if (cur < 0 || cur >= n) return dir < 0 ? n - 1 : 0;
        return (cur + (dir < 0 ? n - 1 : 1)) % n;
    }

    // A choice row stepped by Left / Right: the option after (or before) the
    // current one, clamped at the ends (no wrap, so a held direction stops at
    // the last); an unknown current value steps to the first or last
    static inline int choiceStep(int cur, int n, int dir)
    {
        if (n <= 0) return -1;
        if (cur < 0 || cur >= n) return dir < 0 ? n - 1 : 0;
        const int next = cur + (dir < 0 ? -1 : 1);
        return next < 0 ? 0 : next >= n ? n - 1 : next;
    }

    // A number stepped by Left / Right, clamped to [lo, hi]
    static inline double numberStep(double value, double step, int dir, double lo, double hi)
    {
        double v = value + step * (dir < 0 ? -1.0 : 1.0);
        if (lo < hi) {
            if (v < lo) v = lo;
            if (v > hi) v = hi;
        }
        return v;
    }
}

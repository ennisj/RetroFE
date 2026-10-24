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

// Selecting several widgets or elements at once, for both theme editors:
// the selection as a list (the last one is the primary, the one the
// properties panel shows and the controller picks from), the marquee,
// moving a group, and aligning and distributing boxes. Pure, header-only;
// the sessions (EditorSession, LayoutSession) turn the boxes this gives
// back into edits, one undo step per operation.

#include "EditorCanvas.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ThemeEditor
{
    // ---- the selection --------------------------------------------------------
    //
    // A selection is a list of indices in the order they were picked; the last
    // one is the primary. An empty list is no selection.

    // i joins the selection as its primary (moved to the end if already in it)
    static inline std::vector<int> selectionWith(const std::vector<int>& sel, int i)
    {
        std::vector<int> out;
        for (int s : sel) {
            if (s != i) out.push_back(s);
        }
        out.push_back(i);
        return out;
    }

    static inline std::vector<int> selectionWithout(const std::vector<int>& sel, int i)
    {
        std::vector<int> out;
        for (int s : sel) {
            if (s != i) out.push_back(s);
        }
        return out;
    }

    // Ctrl-click: in it, it leaves; not in it, it joins as the primary
    static inline std::vector<int> selectionToggled(const std::vector<int>& sel, int i)
    {
        if (std::find(sel.begin(), sel.end(), i) != sel.end()) return selectionWithout(sel, i);
        return selectionWith(sel, i);
    }

    // Shift-click in a list: everything from `anchor` to `clicked` in the
    // list's order, `clicked` last (the primary). An anchor that isn't in
    // the list gives just `clicked`.
    static inline std::vector<int> selectionRange(const std::vector<int>& order, int anchor, int clicked)
    {
        int a = -1, b = -1;
        for (int k = 0; k < static_cast<int>(order.size()); ++k) {
            if (order[k] == anchor) a = k;
            if (order[k] == clicked) b = k;
        }
        if (b < 0) return {};
        if (a < 0) return { clicked };
        std::vector<int> out;
        const int step = a <= b ? 1 : -1;
        for (int k = a; k != b; k += step) out.push_back(order[k]);
        out.push_back(clicked);
        return out;
    }

    // The selection with indices above `removed` shifted down after it was
    // deleted (and `removed` gone)
    static inline std::vector<int> selectionAfterErase(const std::vector<int>& sel, int removed)
    {
        std::vector<int> out;
        for (int s : sel) {
            if (s == removed) continue;
            out.push_back(s > removed ? s - 1 : s);
        }
        return out;
    }

    // ---- boxes ------------------------------------------------------------------

    static inline DragRect makeRect(int x, int y, int w, int h)
    {
        DragRect r;
        r.x = x; r.y = y; r.w = w; r.h = h;
        return r;
    }

    // The box around every box; an empty list gives an empty box at 0, 0
    static inline DragRect unionRect(const std::vector<DragRect>& rs)
    {
        if (rs.empty()) return DragRect();
        int x1 = rs[0].x, y1 = rs[0].y, x2 = rs[0].x + rs[0].w, y2 = rs[0].y + rs[0].h;
        for (const DragRect& r : rs) {
            x1 = std::min(x1, r.x);
            y1 = std::min(y1, r.y);
            x2 = std::max(x2, r.x + r.w);
            y2 = std::max(y2, r.y + r.h);
        }
        return makeRect(x1, y1, x2 - x1, y2 - y1);
    }

    // The marquee between two corners, whichever way it was dragged (theme units)
    static inline DragRect marqueeRect(float x1, float y1, float x2, float y2)
    {
        const int ax = roundDelta(std::min(x1, x2)), ay = roundDelta(std::min(y1, y2));
        const int bx = roundDelta(std::max(x1, x2)), by = roundDelta(std::max(y1, y2));
        return makeRect(ax, ay, bx - ax, by - ay);
    }

    static inline bool rectInside(const DragRect& inner, const DragRect& outer)
    {
        return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
               inner.y + inner.h <= outer.y + outer.h;
    }

    // The boxes a marquee selects: the ones wholly inside it, so a marquee
    // drawn over part of a full-page background doesn't take the background
    static inline std::vector<int> marqueePick(const std::vector<DragRect>& rs, const DragRect& marquee)
    {
        std::vector<int> out;
        for (int i = 0; i < static_cast<int>(rs.size()); ++i) {
            if (rectInside(rs[i], marquee)) out.push_back(i);
        }
        return out;
    }

    // A group moved by a theme-space delta, as one box: the box around them
    // snaps its corner to the grid and stays on the canvas (or where it
    // already was), as canvasMove does for one. The delta every member moves by.
    static inline void groupMoveDelta(const std::vector<DragRect>& rs, float dx, float dy, int grid, int canvasW, int canvasH,
                                      int& outDx, int& outDy)
    {
        const DragRect u = unionRect(rs);
        const DragRect moved = canvasMove(u, dx, dy, grid, canvasW, canvasH);
        outDx = moved.x - u.x;
        outDy = moved.y - u.y;
    }

    // ---- align and distribute ------------------------------------------------------

    enum AlignOp { AL_LEFT = 0, AL_HCENTER, AL_RIGHT, AL_TOP, AL_VCENTER, AL_BOTTOM };

    // Names as scripts and the panel write them: left, center, right, top, middle, bottom
    static inline int alignFromName(const std::string& name)
    {
        static const char* const names[] = { "left", "center", "right", "top", "middle", "bottom" };
        for (int i = 0; i < 6; ++i) {
            if (name == names[i]) return i;
        }
        if (name == "centre") return AL_HCENTER;
        return -1;
    }

    // Lines the boxes up on the box around them: their left edges on its
    // left, their centres on its centre, ... Sizes never change. A centre
    // that falls on a half unit rounds down.
    static inline std::vector<DragRect> alignRects(const std::vector<DragRect>& rs, int op)
    {
        std::vector<DragRect> out = rs;
        if (rs.size() < 2) return out;
        const DragRect u = unionRect(rs);
        for (DragRect& r : out) {
            switch (op) {
            case AL_LEFT: r.x = u.x; break;
            case AL_RIGHT: r.x = u.x + u.w - r.w; break;
            case AL_HCENTER: r.x = u.x + (u.w - r.w) / 2; break;
            case AL_TOP: r.y = u.y; break;
            case AL_BOTTOM: r.y = u.y + u.h - r.h; break;
            case AL_VCENTER: r.y = u.y + (u.h - r.h) / 2; break;
            default: break;
            }
        }
        return out;
    }

    // Equal gaps between the boxes along one axis: they're taken in the order
    // of their centres; the first and the last stay where they are and the
    // others are spaced between them (a gap can be negative when they don't
    // fit). Fewer than three: nothing to do.
    static inline std::vector<DragRect> distributeRects(const std::vector<DragRect>& rs, bool horizontal)
    {
        std::vector<DragRect> out = rs;
        const int n = static_cast<int>(rs.size());
        if (n < 3) return out;
        std::vector<int> order(n);
        for (int i = 0; i < n; ++i) order[i] = i;
        auto centre = [&](int i) { return horizontal ? rs[i].x * 2 + rs[i].w : rs[i].y * 2 + rs[i].h; };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return centre(a) < centre(b); });
        auto pos = [&](int i) { return horizontal ? rs[i].x : rs[i].y; };
        auto size = [&](int i) { return horizontal ? rs[i].w : rs[i].h; };
        const int first = order.front(), last = order.back();
        const double start = pos(first);
        const double end = pos(last) + size(last);
        double sizes = 0.0;
        for (int i : order) sizes += size(i);
        const double gap = (end - start - sizes) / static_cast<double>(n - 1);
        double running = start;
        for (int k = 0; k < n; ++k) {
            const int i = order[k];
            if (k > 0 && k < n - 1) {
                const int v = static_cast<int>(std::floor(running + 0.5));
                if (horizontal) out[i].x = v;
                else out[i].y = v;
            }
            running += size(i) + gap;
        }
        return out;
    }
}

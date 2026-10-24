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
// moving a group, aligning and distributing boxes, alignment guides while
// dragging, and one setting's value across several. Pure, header-only;
// the sessions (EditorSession, LayoutSession) turn the boxes this gives
// back into edits, one undo step per operation.

#include "EditorCanvas.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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

    // ---- alignment guides ------------------------------------------------------------
    //
    // While a box is dragged with the mouse, its edges and centre line up with
    // the other boxes' edges and centres and with the page's centre when they
    // come within reach, and a line shows each match. Worked out in half
    // units (a centre of an odd-sized box sits on one), so "lined up" is exact.

    // A line to draw: vertical at x = at from y = from to y = to, or horizontal at y = at
    struct GuideLine
    {
        bool vertical = true;
        int at = 0;
        int from = 0, to = 0;
    };

    // Which of a box's edges take part: a move all three on each axis, a
    // resize only the edges its handle moves
    enum GuideEdges { GE_LOW = 1, GE_MID = 2, GE_HIGH = 4, GE_ALL = 7 };

    namespace guide_detail
    {
        // Doubled positions of a box's edges along one axis: low, middle, high
        inline void edges2(const DragRect& r, bool x, int out[3])
        {
            const int p = x ? r.x : r.y, s = x ? r.w : r.h;
            out[0] = p * 2;
            out[1] = p * 2 + s;
            out[2] = (p + s) * 2;
        }
        // Doubled targets along one axis: every other box's edges and middle, and the page's middle
        inline std::vector<int> targets2(const std::vector<DragRect>& others, bool x, int canvasW, int canvasH)
        {
            std::vector<int> t;
            for (const DragRect& o : others) {
                int e[3];
                edges2(o, x, e);
                t.insert(t.end(), e, e + 3);
            }
            t.push_back(x ? canvasW : canvasH);
            return t;
        }
    }

    // The shift (whole units) that lines `r` up on the nearest target along
    // one axis, within `reach` theme units; false when nothing is in reach.
    // `edges`: which of its edges take part (GuideEdges).
    static inline bool guideShift(const DragRect& r, bool x, int edges, const std::vector<DragRect>& others, int canvasW,
                                  int canvasH, int reach, int& shift)
    {
        int mine[3];
        guide_detail::edges2(r, x, mine);
        const std::vector<int> targets = guide_detail::targets2(others, x, canvasW, canvasH);
        int best = -1;
        int bestDiff = 0;
        for (int k = 0; k < 3; ++k) {
            if (!(edges & (1 << k))) continue;
            for (int t : targets) {
                const int diff = t - mine[k];
                const int dist = diff < 0 ? -diff : diff;
                if (dist > reach * 2) continue;
                if (best < 0 || dist < best) { best = dist; bestDiff = diff; }
            }
        }
        if (best < 0) return false;
        // A half-unit difference (a centre onto an edge) rounds towards the target's lower side
        shift = bestDiff >= 0 ? bestDiff / 2 : -((-bestDiff + 1) / 2);
        return true;
    }

    // The lines for every edge or centre of `r` that is lined up (to within
    // half a unit) with another box's or the page's centre; each spans the
    // boxes it joins (the page's centre: the whole page)
    static inline std::vector<GuideLine> guideLines(const DragRect& r, const std::vector<DragRect>& others, int canvasW, int canvasH,
                                                    int edgesX = GE_ALL, int edgesY = GE_ALL)
    {
        std::vector<GuideLine> out;
        auto add = [&out](bool vertical, int at, int from, int to) {
            for (GuideLine& g : out) {
                if (g.vertical == vertical && g.at == at) {
                    g.from = std::min(g.from, from);
                    g.to = std::max(g.to, to);
                    return;
                }
            }
            GuideLine g;
            g.vertical = vertical;
            g.at = at;
            g.from = from;
            g.to = to;
            out.push_back(g);
        };
        for (int axis = 0; axis < 2; ++axis) {
            const bool x = axis == 0;
            const int edges = x ? edgesX : edgesY;
            int mine[3];
            guide_detail::edges2(r, x, mine);
            const int lo = x ? r.y : r.x, hi = x ? r.y + r.h : r.x + r.w;
            for (int k = 0; k < 3; ++k) {
                if (!(edges & (1 << k))) continue;
                for (const DragRect& o : others) {
                    int theirs[3];
                    guide_detail::edges2(o, x, theirs);
                    for (int t : theirs) {
                        if (std::abs(t - mine[k]) > 1) continue;
                        const int olo = x ? o.y : o.x, ohi = x ? o.y + o.h : o.x + o.w;
                        add(x, t / 2, std::min(lo, olo), std::max(hi, ohi));
                    }
                }
                const int page = x ? canvasW : canvasH;
                if (std::abs(page - mine[k]) <= 1) add(x, page / 2, 0, x ? canvasH : canvasW);
            }
        }
        return out;
    }

    // A drag's box with the guides applied. `snapped` is where the grid put
    // it and `raw` where the pointer did (no grid); an axis whose edge comes
    // within reach of a target lines up on it (from the raw box, so the grid
    // doesn't pull it off), any other keeps the grid's value. handle: CH_BODY
    // moves (every edge and the centre take part), a resize handle moves only
    // its edges (the size changes with them). moveX / moveY: the axes the
    // pointer really moved along (an axis it didn't stays put). As the drag
    // itself, the result stays on the canvas (or where `start`, the box when
    // the drag began, already was) and a resize keeps at least minW x minH.
    // `lines` gets what to draw for the result.
    static inline DragRect applyGuides(const DragRect& snapped, const DragRect& raw, const DragRect& start, int handle,
                                       const std::vector<DragRect>& others, int canvasW, int canvasH, int reach, int minW, int minH,
                                       bool moveX, bool moveY, std::vector<GuideLine>& lines)
    {
        if (minW < 1) minW = 1;
        if (minH < 1) minH = 1;
        DragRect out = snapped;
        int edgesX = GE_ALL, edgesY = GE_ALL;
        if (handle != CH_BODY) {
            const bool left = handle == CH_NW || handle == CH_W || handle == CH_SW;
            const bool right = handle == CH_NE || handle == CH_E || handle == CH_SE;
            const bool up = handle == CH_NW || handle == CH_N || handle == CH_NE;
            const bool down = handle == CH_SW || handle == CH_S || handle == CH_SE;
            edgesX = (left ? GE_LOW : 0) | (right ? GE_HIGH : 0);
            edgesY = (up ? GE_LOW : 0) | (down ? GE_HIGH : 0);
        }
        for (int axis = 0; axis < 2; ++axis) {
            const bool x = axis == 0;
            const int edges = x ? edgesX : edgesY;
            if (!edges || !(x ? moveX : moveY)) continue;
            int shift = 0;
            if (!guideShift(raw, x, edges, others, canvasW, canvasH, reach, shift)) continue;
            int& pos = x ? out.x : out.y;
            int& size = x ? out.w : out.h;
            const int rawPos = x ? raw.x : raw.y, rawSize = x ? raw.w : raw.h;
            const int canvas = x ? canvasW : canvasH;
            const int startPos = x ? start.x : start.y, startSize = x ? start.w : start.h;
            const int minSize = x ? minW : minH;
            if (handle == CH_BODY) {
                // On the canvas, or where it already was (canvasMove's range)
                int lo = canvas - size < 0 ? canvas - size : 0;
                int hi = canvas - size > 0 ? canvas - size : 0;
                if (startPos < lo) lo = startPos;
                if (startPos > hi) hi = startPos;
                pos = std::max(lo, std::min(hi, rawPos + shift));
            }
            else if (edges & GE_LOW) {
                // The low edge moves, the high one stays where the grid left it;
                // not past the canvas's (or its own start's) edge, not below the minimum
                const int high = pos + size;
                const int next = std::max(std::min(0, startPos), rawPos + shift);
                if (high - next >= minSize) { pos = next; size = high - next; }
            }
            else {
                const int next = std::min(std::max(canvas, startPos + startSize), rawPos + rawSize + shift);
                if (next - pos >= minSize) size = next - pos;
            }
        }
        lines = guideLines(out, others, canvasW, canvasH, moveX ? edgesX : 0, moveY ? edgesY : 0);
        return out;
    }

    // ---- one setting of several at once ----------------------------------------------

    // What several selected things have for one setting: whether every one
    // has it set, whether any does, and the value when they all agree (set
    // to the same, or none set); mixed when they differ ("(several values)")
    struct SharedValue
    {
        bool mixed = false;
        bool any = false;
        bool all = false;
        std::string value;
    };

    // From each one's (set, value)
    static inline SharedValue sharedValue(const std::vector<std::pair<bool, std::string>>& each)
    {
        SharedValue v;
        if (each.empty()) return v;
        v.all = true;
        for (const auto& e : each) {
            v.any = v.any || e.first;
            v.all = v.all && e.first;
            if (e.first != each.front().first || e.second != each.front().second) v.mixed = true;
        }
        if (!v.mixed) v.value = each.front().second;
        return v;
    }
}

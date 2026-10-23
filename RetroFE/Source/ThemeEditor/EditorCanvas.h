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

// The canvas's geometry: letterbox fit, theme <-> stage transforms, widget
// hit-testing, placement and drag/resize. The preview page is drawn
// letterboxed into the stage rect, so one fit and one scale is the whole
// mapping. The overlay's geometry always comes from the project, never from
// the engine.
//
// Drags are overlay-led: while the mouse is down only the wireframe follows;
// the project commits (snapped) on release and the page is rebuilt from the
// regenerated XML. Not live ViewInfo changes: a widget's box maps to
// different fields per definition (a text box's height is its font size, an
// aspect-fit image writes maxHeight), so moving ViewInfo directly would show
// something the XML doesn't say.
//
// Ported from One Saucier's theme editor (app/editor/editor_canvas.h). Pure,
// header-only.

#include "ThemeProject.h"
#include "WidgetDefs.h"
#include <algorithm>
#include <vector>

namespace ThemeEditor
{
    // Where the theme lands inside a stage
    struct CanvasFit
    {
        float x = 0, y = 0;   // top-left of the drawn theme within the stage
        float w = 0, h = 0;   // drawn size
        float scale = 0;      // stage pixels per theme unit
        bool ok = false;      // false for degenerate inputs
    };

    // Contain-fits a themeW x themeH layout into a stageW x stageH box, centred
    static inline CanvasFit canvasFit(float stageW, float stageH, int themeW, int themeH)
    {
        CanvasFit f;
        if (!(stageW > 0.0f) || !(stageH > 0.0f) || themeW <= 0 || themeH <= 0) return f;
        const float sx = stageW / static_cast<float>(themeW);
        const float sy = stageH / static_cast<float>(themeH);
        f.scale = sx < sy ? sx : sy;
        f.w = static_cast<float>(themeW) * f.scale;
        f.h = static_cast<float>(themeH) * f.scale;
        f.x = (stageW - f.w) * 0.5f;
        f.y = (stageH - f.h) * 0.5f;
        f.ok = true;
        return f;
    }

    static inline float canvasToThemeX(const CanvasFit& f, float sx) { return f.scale > 0.0f ? (sx - f.x) / f.scale : 0.0f; }
    static inline float canvasToThemeY(const CanvasFit& f, float sy) { return f.scale > 0.0f ? (sy - f.y) / f.scale : 0.0f; }
    static inline float themeToCanvasX(const CanvasFit& f, float tx) { return f.x + tx * f.scale; }
    static inline float themeToCanvasY(const CanvasFit& f, float ty) { return f.y + ty * f.scale; }

    // The rect a pointer hit-tests against: a warped widget's corners' bounds
    static inline void canvasHitRect(const WidgetInstance& w, int& hx, int& hy, int& hw, int& hh)
    {
        if (w.warp) { w.warpBounds(hx, hy, hw, hh); return; }
        hx = w.x; hy = w.y; hw = w.w; hh = w.h;
    }

    static inline bool canvasHit(const WidgetInstance& w, float tx, float ty)
    {
        int hx, hy, hw, hh;
        canvasHitRect(w, hx, hy, hw, hh);
        return !(tx < static_cast<float>(hx) || ty < static_cast<float>(hy) ||
                 tx > static_cast<float>(hx + hw) || ty > static_cast<float>(hy + hh));
    }

    // The widget at a theme-space point: the highest layer wins, then the
    // later-placed one (document order is draw order within a layer).
    // -1 for empty space. Canvas-hidden widgets don't pick.
    static inline int canvasPick(const ThemePage& page, float tx, float ty)
    {
        int best = -1;
        int bestLayer = -1;
        for (int i = 0; i < static_cast<int>(page.widgets.size()); ++i) {
            const WidgetInstance& w = page.widgets[i];
            if (w.canvasHide) continue;
            if (!canvasHit(w, tx, ty)) continue;
            if (w.layer >= bestLayer) { bestLayer = w.layer; best = i; }
        }
        return best;
    }

    // Click-through: the next widget under the point after `cur`, in
    // canvasPick's order, wrapping, so repeated clicks visit every widget
    // in a stack. canvasPick's answer when `cur` isn't under the point.
    static inline int canvasPickNext(const ThemePage& page, float tx, float ty, int cur)
    {
        std::vector<int> hits;
        for (int i = 0; i < static_cast<int>(page.widgets.size()); ++i) {
            if (!page.widgets[i].canvasHide && canvasHit(page.widgets[i], tx, ty)) hits.push_back(i);
        }
        if (hits.empty()) return -1;
        std::sort(hits.begin(), hits.end(), [&page](int a, int b) {
            if (page.widgets[a].layer != page.widgets[b].layer) return page.widgets[a].layer > page.widgets[b].layer;
            return a > b;
        });
        for (size_t k = 0; k < hits.size(); ++k) {
            if (hits[k] == cur) return hits[(k + 1) % hits.size()];
        }
        return hits[0];
    }

    // A new widget: the definition's default size (shrunk to the canvas), centred
    static inline void canvasPlaceCenter(const WidgetDef& def, int canvasW, int canvasH, int& x, int& y, int& w, int& h)
    {
        w = def.w > canvasW ? canvasW : def.w;
        h = def.h > canvasH ? canvasH : def.h;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        x = (canvasW - w) / 2;
        y = (canvasH - h) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }

    // Resize handles in reading order; CH_BODY = inside the rect (move)
    enum CanvasHandle { CH_NONE = -1, CH_NW = 0, CH_N, CH_NE, CH_W, CH_E, CH_SW, CH_S, CH_SE, CH_BODY };

    struct DragRect { int x = 0, y = 0, w = 0, h = 0; };

    // The handle at a stage-space point, handles `grabPx` wide in stage
    // pixels. Handles win over the body.
    static inline int canvasHandleAt(const CanvasFit& f, const DragRect& r, float sx, float sy, float grabPx)
    {
        if (!f.ok) return CH_NONE;
        const float x1 = themeToCanvasX(f, static_cast<float>(r.x));
        const float y1 = themeToCanvasY(f, static_cast<float>(r.y));
        const float x2 = themeToCanvasX(f, static_cast<float>(r.x + r.w));
        const float y2 = themeToCanvasY(f, static_cast<float>(r.y + r.h));
        const float mx = (x1 + x2) * 0.5f, my = (y1 + y2) * 0.5f;
        const float hx[8] = { x1, mx, x2, x1, x2, x1, mx, x2 };
        const float hy[8] = { y1, y1, y1, my, my, y2, y2, y2 };
        for (int i = 0; i < 8; ++i) {
            if (sx >= hx[i] - grabPx && sx <= hx[i] + grabPx && sy >= hy[i] - grabPx && sy <= hy[i] + grabPx) return i;
        }
        if (sx >= x1 && sx <= x2 && sy >= y1 && sy <= y2) return CH_BODY;
        return CH_NONE;
    }

    static inline int canvasSnap(int v, int grid)
    {
        if (grid <= 1) return v;
        const int half = grid / 2;
        return v >= 0 ? ((v + half) / grid) * grid : -(((-v + half) / grid) * grid);
    }

    static inline int roundDelta(float d) { return static_cast<int>(d >= 0 ? d + 0.5f : d - 0.5f); }

    // Moves by a theme-space delta, snapping the origin and keeping the widget
    // on the canvas. A widget bigger than the canvas (imported wheels overhang
    // by design) slides within its cover range, and its start is always legal,
    // so a zero delta never moves anything.
    static inline DragRect canvasMove(const DragRect& start, float dx, float dy, int grid, int canvasW, int canvasH)
    {
        DragRect r = start;
        r.x = canvasSnap(start.x + roundDelta(dx), grid);
        r.y = canvasSnap(start.y + roundDelta(dy), grid);
        int loX = canvasW - r.w < 0 ? canvasW - r.w : 0;
        int hiX = canvasW - r.w > 0 ? canvasW - r.w : 0;
        int loY = canvasH - r.h < 0 ? canvasH - r.h : 0;
        int hiY = canvasH - r.h > 0 ? canvasH - r.h : 0;
        if (start.x < loX) loX = start.x;
        if (start.x > hiX) hiX = start.x;
        if (start.y < loY) loY = start.y;
        if (start.y > hiY) hiY = start.y;
        if (r.x > hiX) r.x = hiX;
        if (r.y > hiY) r.y = hiY;
        if (r.x < loX) r.x = loX;
        if (r.y < loY) r.y = loY;
        return r;
    }

    // Resizes from `handle` by a theme-space delta. Moving edges snap and stay
    // on the canvas (or where they already were); anchored edges never move;
    // sizes floor at minW/minH, which outranks the canvas clamp.
    static inline DragRect canvasResize(const DragRect& start, int handle, float dx, float dy, int minW, int minH,
                                        int grid, int canvasW, int canvasH)
    {
        if (minW < 1) minW = 1;
        if (minH < 1) minH = 1;
        int x1 = start.x, y1 = start.y, x2 = start.x + start.w, y2 = start.y + start.h;
        const int idx = roundDelta(dx);
        const int idy = roundDelta(dy);
        const bool left = handle == CH_NW || handle == CH_W || handle == CH_SW;
        const bool right = handle == CH_NE || handle == CH_E || handle == CH_SE;
        const bool up = handle == CH_NW || handle == CH_N || handle == CH_NE;
        const bool down = handle == CH_SW || handle == CH_S || handle == CH_SE;
        if (!left && !right && !up && !down) return start;
        const int loX1 = start.x < 0 ? start.x : 0;
        const int hiX2 = start.x + start.w > canvasW ? start.x + start.w : canvasW;
        const int loY1 = start.y < 0 ? start.y : 0;
        const int hiY2 = start.y + start.h > canvasH ? start.y + start.h : canvasH;
        if (left) {
            x1 = canvasSnap(x1 + idx, grid);
            if (x1 < loX1) x1 = loX1;
            if (x1 > x2 - minW) x1 = x2 - minW;
        }
        if (right) {
            x2 = canvasSnap(x2 + idx, grid);
            if (x2 > hiX2) x2 = hiX2;
            if (x2 < x1 + minW) x2 = x1 + minW;
        }
        if (up) {
            y1 = canvasSnap(y1 + idy, grid);
            if (y1 < loY1) y1 = loY1;
            if (y1 > y2 - minH) y1 = y2 - minH;
        }
        if (down) {
            y2 = canvasSnap(y2 + idy, grid);
            if (y2 > hiY2) y2 = hiY2;
            if (y2 < y1 + minH) y2 = y1 + minH;
        }
        DragRect r;
        r.x = x1; r.y = y1; r.w = x2 - x1; r.h = y2 - y1;
        return r;
    }

    // The snap grid, in theme units
    static const int CANVAS_SNAP_GRID = 5;
}

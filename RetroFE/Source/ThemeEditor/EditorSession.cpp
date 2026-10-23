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
#include "EditorSession.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ThemeEditor
{
    namespace
    {
        // A press that moves less than this (stage pixels) is a click
        const float CLICK_SLOP = 3.0f;

        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
    }

    VarKind varKind(const WidgetVar& var)
    {
        if (var.type == "bool") return VarKind::Bool;
        if (var.type == "file") return VarKind::File;
        if (var.type == "raw") return VarKind::Raw;
        if (!var.options.empty()) return VarKind::Choice;
        if (var.numeric) return VarKind::Number;
        // The stock widgets name their colour fields "... Color" (WHEELFONTCOLOR, "Text Color")
        std::string token = lower(var.token);
        std::string name = lower(var.name);
        if (name.find("color") != std::string::npos || name.find("colour") != std::string::npos ||
            (token.size() >= 5 && token.compare(token.size() - 5, 5, "color") == 0)) {
            return VarKind::Colour;
        }
        return VarKind::Text;
    }

    std::string normalizeColour(const std::string& text)
    {
        std::string s;
        for (char c : text) {
            if (c == '#' && s.empty()) continue;
            if (std::isspace(static_cast<unsigned char>(c))) continue;
            s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return widgetColorValid(s) ? s : "";
    }

    bool EditorSession::open(const std::string& layoutsRoot, const std::string& folder, const WidgetLibrary& lib,
                             std::string& error)
    {
        if (!themeFolderValid(folder)) {
            error = "\"" + folder + "\" isn't a theme folder name";
            return false;
        }
        ThemeFolderKind kind = probeTheme(layoutsRoot, folder);
        if (kind == TF_FOREIGN) {
            error = "\"" + folder + "\" wasn't made with the theme editor, so the editor can't open it";
            return false;
        }
        if (kind == TF_FRESH) {
            error = "\"" + folder + "\" has no theme in it yet";
            return false;
        }
        ThemeProject project;
        if (!loadTheme(layoutsRoot, folder, project, error)) {
            return false;
        }
        lib_ = &lib;
        layoutsRoot_ = layoutsRoot;
        folder_ = folder;
        project_ = std::move(project);
        page_ = 0;
        selected_ = -1;
        drag_ = Drag();
        undo_.clear();
        redo_.clear();
        coalesce_.clear();
        lastChangeKey_.clear();
        revision_ = savedRevision_ = 0;
        nextRevision_ = 1;
        return true;
    }

    void EditorSession::setPage(int index)
    {
        if (index < 0 || index >= pageCount() || index == page_) return;
        cancelDrag();
        page_ = index;
        selected_ = -1;
        lastChangeKey_.clear();
    }

    std::string EditorSession::pageLabel(int index) const
    {
        if (index < 0 || index >= pageCount()) return "";
        const ThemePage& p = project_.pages[index];
        if (p.collection.empty()) return "Default";
        if (ThemeProject::isSplashPage(p.collection)) return "Splash";
        std::string label = p.displayName();
        if (p.tplName.empty() || p.tplName == p.collection) {
            label = p.collection;
        }
        else {
            label += " (" + p.collection;
            label += ")";
        }
        if (!p.members.empty()) {
            label += " - also ";
            for (size_t i = 0; i < p.members.size(); ++i) {
                label += (i ? ", " : "") + p.members[i];
            }
        }
        return label;
    }

    std::string EditorSession::pageFolder() const
    {
        const ThemePage& p = page();
        if (p.collection.empty() || ThemeProject::isSplashPage(p.collection)) return "";
        return "collections/" + p.collection + "/layout";
    }

    void EditorSession::select(int index)
    {
        if (index < -1 || index >= static_cast<int>(page().widgets.size())) index = -1;
        if (index != selected_) lastChangeKey_.clear();
        selected_ = index;
    }

    bool EditorSession::selectId(const std::string& id)
    {
        const auto& widgets = page().widgets;
        for (size_t i = 0; i < widgets.size(); ++i) {
            if (widgets[i].id == id) {
                select(static_cast<int>(i));
                return true;
            }
        }
        return false;
    }

    const WidgetInstance* EditorSession::selectedWidget() const
    {
        if (selected_ < 0 || selected_ >= static_cast<int>(page().widgets.size())) return nullptr;
        return &page().widgets[selected_];
    }

    WidgetInstance* EditorSession::selectedMutable()
    {
        if (selected_ < 0 || selected_ >= static_cast<int>(project_.pages[page_].widgets.size())) return nullptr;
        return &project_.pages[page_].widgets[selected_];
    }

    const WidgetDef* EditorSession::selectedDef() const
    {
        const WidgetInstance* w = selectedWidget();
        return w && lib_ ? lib_->find(w->def) : nullptr;
    }

    // ---- pointer -----------------------------------------------------------

    void EditorSession::pointerDown(const CanvasFit& fit, float sx, float sy, float grabPx)
    {
        drag_ = Drag();
        if (!fit.ok || !isOpen()) return;
        const float tx = canvasToThemeX(fit, sx);
        const float ty = canvasToThemeY(fit, sy);

        int handle = CH_NONE;
        if (const WidgetInstance* sel = selectedWidget()) {
            DragRect r;
            canvasHitRect(*sel, r.x, r.y, r.w, r.h);
            if (!sel->canvasHide) handle = canvasHandleAt(fit, r, sx, sy, grabPx);
        }
        if (handle == CH_NONE) {
            // Not on the selection: pick what's on top here
            int picked = canvasPick(page(), tx, ty);
            select(picked);
            if (picked < 0) return;
            handle = CH_BODY;
        }
        else if (handle == CH_BODY) {
            drag_.cycleOnClick = true;
        }

        const WidgetInstance* sel = selectedWidget();
        drag_.active = true;
        drag_.handle = handle;
        drag_.startSx = sx;
        drag_.startSy = sy;
        drag_.startTx = tx;
        drag_.startTy = ty;
        canvasHitRect(*sel, drag_.start.x, drag_.start.y, drag_.start.w, drag_.start.h);
        drag_.rect = drag_.start;
    }

    void EditorSession::pointerMove(const CanvasFit& fit, float sx, float sy)
    {
        if (!drag_.active || !fit.ok) return;
        if (!drag_.moved && std::fabs(sx - drag_.startSx) < CLICK_SLOP && std::fabs(sy - drag_.startSy) < CLICK_SLOP) {
            return;
        }
        drag_.moved = true;
        const float dx = (sx - drag_.startSx) / fit.scale;
        const float dy = (sy - drag_.startSy) / fit.scale;
        const int grid = snap ? CANVAS_SNAP_GRID : 1;
        if (drag_.handle == CH_BODY) {
            drag_.rect = canvasMove(drag_.start, dx, dy, grid, project_.canvasW(), project_.canvasH());
        }
        else {
            const WidgetDef* def = selectedDef();
            drag_.rect = canvasResize(drag_.start, drag_.handle, dx, dy, def ? def->minW : 8, def ? def->minH : 8,
                                      grid, project_.canvasW(), project_.canvasH());
        }
    }

    void EditorSession::pointerUp()
    {
        if (!drag_.active) return;
        Drag d = drag_;
        drag_ = Drag();
        if (!d.moved) {
            // A click on the selection: the next widget under the point
            if (d.cycleOnClick) select(canvasPickNext(page(), d.startTx, d.startTy, selected_));
            return;
        }
        const DragRect& r = d.rect;
        if (r.x == d.start.x && r.y == d.start.y && r.w == d.start.w && r.h == d.start.h) return;

        WidgetInstance* w = selectedMutable();
        if (!w) return;
        beginChange("");
        if (w->warp) {
            if (d.handle == CH_BODY) {
                // Moving keeps a One Saucier warp's shape
                const int dx = r.x - d.start.x, dy = r.y - d.start.y;
                for (int i = 0; i < 4; ++i) { w->wx[i] += dx; w->wy[i] += dy; }
                w->x += dx;
                w->y += dy;
            }
            else {
                // This engine can't warp anyway: resizing makes it a plain box
                w->warp = false;
                w->x = r.x; w->y = r.y; w->w = r.w; w->h = r.h;
            }
        }
        else {
            w->x = r.x; w->y = r.y; w->w = r.w; w->h = r.h;
        }
        changed();
    }

    void EditorSession::cancelDrag()
    {
        drag_ = Drag();
    }

    // ---- edits -------------------------------------------------------------

    EditorSession::Snapshot EditorSession::snapshot() const
    {
        Snapshot s;
        s.project = project_;
        s.page = page_;
        s.selected = selected_;
        s.revision = revision_;
        return s;
    }

    void EditorSession::restore(const Snapshot& s)
    {
        project_ = s.project;
        page_ = s.page < pageCount() ? s.page : 0;
        selected_ = s.selected;
        revision_ = s.revision;
        clampSelection();
        drag_ = Drag();
        lastChangeKey_.clear();
    }

    void EditorSession::clampSelection()
    {
        if (selected_ >= static_cast<int>(page().widgets.size())) selected_ = -1;
    }

    void EditorSession::beginChange(const std::string& key)
    {
        std::string full;
        if (!key.empty()) full = key + "@" + std::to_string(page_) + ":" + std::to_string(selected_);
        bool merge = !full.empty() && full == lastChangeKey_ && !undo_.empty() && redo_.empty();
        if (!merge) {
            undo_.push_back(snapshot());
            if (undo_.size() > UNDO_LIMIT) undo_.erase(undo_.begin());
        }
        redo_.clear();
        lastChangeKey_ = full;
    }

    void EditorSession::changed()
    {
        revision_ = nextRevision_++;
    }

    bool EditorSession::moveBy(int dx, int dy)
    {
        const WidgetInstance* w = selectedWidget();
        if (!w || (dx == 0 && dy == 0)) return false;
        DragRect start;
        canvasHitRect(*w, start.x, start.y, start.w, start.h);
        DragRect r = canvasMove(start, static_cast<float>(dx), static_cast<float>(dy), 1, project_.canvasW(), project_.canvasH());
        if (r.x == start.x && r.y == start.y) return false;
        beginChange(coalesce_.empty() ? "nudge" : coalesce_);
        WidgetInstance* m = selectedMutable();
        const int mx = r.x - start.x, my = r.y - start.y;
        if (m->warp) {
            for (int i = 0; i < 4; ++i) { m->wx[i] += mx; m->wy[i] += my; }
        }
        m->x += mx;
        m->y += my;
        changed();
        return true;
    }

    bool EditorSession::setRect(int x, int y, int w, int h)
    {
        WidgetInstance* m = selectedMutable();
        if (!m) return false;
        const WidgetDef* def = selectedDef();
        const int minW = def ? std::max(1, def->minW) : 1;
        const int minH = def ? std::max(1, def->minH) : 1;
        w = std::max(w, minW);
        h = std::max(h, minH);
        if (m->x == x && m->y == y && m->w == w && m->h == h && !m->warp) return false;
        beginChange(coalesce_);
        m = selectedMutable();
        m->warp = false;
        m->x = x; m->y = y; m->w = w; m->h = h;
        changed();
        return true;
    }

    bool EditorSession::setName(const std::string& name)
    {
        WidgetInstance* m = selectedMutable();
        if (!m || m->name == name) return false;
        beginChange(coalesce_);
        selectedMutable()->name = name;
        changed();
        return true;
    }

    bool EditorSession::setLayer(int layer)
    {
        layer = clampLayer(layer);
        WidgetInstance* m = selectedMutable();
        if (!m || m->layer == layer) return false;
        beginChange(coalesce_);
        selectedMutable()->layer = layer;
        changed();
        return true;
    }

    bool EditorSession::setAlpha(int pct)
    {
        pct = std::max(0, std::min(100, pct));
        WidgetInstance* m = selectedMutable();
        if (!m || m->alphaPct == pct) return false;
        beginChange(coalesce_);
        selectedMutable()->alphaPct = pct;
        changed();
        return true;
    }

    bool EditorSession::setFontColor(const std::string& hex)
    {
        std::string value = hex.empty() ? "" : normalizeColour(hex);
        if (!hex.empty() && value.empty()) return false;
        WidgetInstance* m = selectedMutable();
        if (!m || m->fontColor == value) return false;
        beginChange(coalesce_);
        selectedMutable()->fontColor = value;
        changed();
        return true;
    }

    bool EditorSession::setVar(const std::string& token, const std::string& value)
    {
        WidgetInstance* m = selectedMutable();
        const WidgetDef* def = selectedDef();
        if (!m || !def) return false;
        const WidgetVar* var = def->var(token);
        if (!var) return false;
        std::string v = value;
        if (var->type == "bool") {
            v = (value == "true" || value == "yes" || value == "1" || value == "on") ? "true" : "false";
        }
        else if (var->numeric && !v.empty()) {
            char* end = nullptr;
            double d = std::strtod(v.c_str(), &end);
            if (end == v.c_str()) return false;
            if (var->minV < var->maxV) d = std::max(var->minV, std::min(var->maxV, d));
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", d);
            v = buffer;
        }
        else if (!var->options.empty()) {
            // A dropdown takes one of its options ('-' is the empty value, however it's spelled)
            if (v == "-") v.clear();
            bool listed = false;
            for (const std::string& option : var->options) {
                listed |= option == v || (v.empty() && option == "-");
            }
            if (!listed) return false;
        }
        if (m->varValue(token, var->def) == v) return false;
        beginChange(coalesce_.empty() ? "" : coalesce_ + "/" + token);
        selectedMutable()->setVar(token, v);
        changed();
        return true;
    }

    bool EditorSession::resetVar(const std::string& token)
    {
        WidgetInstance* m = selectedMutable();
        if (!m) return false;
        bool has = false;
        for (const auto& kv : m->vars) {
            if (kv.first == token) has = true;
        }
        if (!has) return false;
        beginChange("");
        selectedMutable()->clearVar(token);
        changed();
        return true;
    }

    bool EditorSession::setCanvasHidden(bool hidden)
    {
        WidgetInstance* m = selectedMutable();
        if (!m || m->canvasHide == hidden) return false;
        beginChange("");
        selectedMutable()->canvasHide = hidden;
        changed();
        return true;
    }

    bool EditorSession::removeSelected()
    {
        if (!selectedWidget()) return false;
        beginChange("");
        auto& widgets = project_.pages[page_].widgets;
        widgets.erase(widgets.begin() + selected_);
        selected_ = -1;
        changed();
        return true;
    }

    bool EditorSession::addWidget(const std::string& defId)
    {
        const WidgetDef* def = lib_ ? lib_->find(defId) : nullptr;
        if (!def) return false;
        cancelDrag();
        beginChange("");
        ThemePage& p = project_.pages[page_];
        WidgetInstance w;
        w.def = def->id;
        nextIdentity(p, def->id, def->name, w.id, w.name);
        canvasPlaceCenter(*def, project_.canvasW(), project_.canvasH(), w.x, w.y, w.w, w.h);
        w.layer = clampLayer(def->layer);
        p.widgets.push_back(w);
        selected_ = static_cast<int>(p.widgets.size()) - 1;
        changed();
        return true;
    }

    bool EditorSession::setThemeName(const std::string& name)
    {
        if (name.empty() || project_.name == name) return false;
        beginChange(coalesce_.empty() ? "" : coalesce_);
        project_.name = name;
        changed();
        return true;
    }

    bool EditorSession::setVertical(bool vertical)
    {
        if (project_.vertical == vertical) return false;
        cancelDrag();
        beginChange("");
        reflowOrientation(project_, vertical);
        changed();
        return true;
    }

    bool EditorSession::undo()
    {
        if (undo_.empty()) return false;
        redo_.push_back(snapshot());
        restore(undo_.back());
        undo_.pop_back();
        return true;
    }

    bool EditorSession::redo()
    {
        if (redo_.empty()) return false;
        undo_.push_back(snapshot());
        restore(redo_.back());
        redo_.pop_back();
        return true;
    }

    GenResult EditorSession::previewXml(const GenOptions& show) const
    {
        GenOptions opt = show;
        opt.canvasPreview = true;
        opt.themeDir = folder_;
        if (!lib_) {
            GenResult r;
            r.errors.push_back("no theme is open");
            return r;
        }
        return generateLayoutXml(project_, page(), *lib_, opt);
    }

    bool EditorSession::save(const SaveInputs& inputs, SaveReport& report, std::string& error)
    {
        if (!isOpen()) {
            error = "no theme is open";
            return false;
        }
        SaveInputs in = inputs;
        in.themeDir = folder_;
        report = SaveReport();
        if (!saveTheme(layoutsRoot_, folder_, project_, *lib_, in, &report, error)) {
            return false;
        }
        savedRevision_ = revision_;
        return true;
    }
}

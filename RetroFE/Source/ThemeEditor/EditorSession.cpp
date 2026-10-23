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
#include <filesystem>

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
        beginChange("nudge");   // a run of nudges is one step, until endCoalesce()
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

    // ---- theme properties --------------------------------------------------

    bool EditorSession::setThemeFont(const std::string& rel)
    {
        const std::string value = rel == BUNDLED_FONT ? std::string() : rel;
        if (!isOpen() || project_.font == value) return false;
        beginChange("");
        project_.font = value;
        changed();
        return true;
    }

    namespace
    {
        std::string* soundSlot(ThemeProject& p, const std::string& kind)
        {
            if (kind == "load") return &p.soundLoad;
            if (kind == "unload") return &p.soundUnload;
            if (kind == "highlight") return &p.soundHighlight;
            if (kind == "select") return &p.soundSelect;
            return nullptr;
        }

        std::string trimmed(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return s.substr(a, b - a);
        }

        bool isVideoFile(const std::string& rel)
        {
            const size_t dot = rel.find_last_of('.');
            if (dot == std::string::npos) return false;
            const std::string ext = lower(rel.substr(dot));
            for (const char* v : { ".mp4", ".mkv", ".avi", ".webm", ".mov", ".m4v", ".mpg", ".mpeg", ".wmv", ".flv" }) {
                if (ext == v) return true;
            }
            return false;
        }

        const char* const SPLASH_BACKGROUND = "splash_background";
    }

    bool EditorSession::setSound(const std::string& kind, const std::string& rel)
    {
        std::string* slot = soundSlot(project_, kind);
        if (!isOpen() || !slot || *slot == rel) return false;
        beginChange("");
        *soundSlot(project_, kind) = rel;
        changed();
        return true;
    }

    std::string EditorSession::sound(const std::string& kind) const
    {
        std::string* slot = soundSlot(const_cast<ThemeProject&>(project_), kind);
        return slot ? *slot : std::string();
    }

    // ---- the splash page ------------------------------------------------------

    bool EditorSession::hasSplashPage() const
    {
        return project_.page(ThemeProject::splashPage()) != nullptr;
    }

    bool EditorSession::makeSplashPage()
    {
        if (!isOpen() || hasSplashPage()) return false;
        ThemePage splash;
        if (!defaultSplashPage(project_, *lib_, splash)) return false;
        const std::vector<std::string> covered = coveredByCurrent();
        beginChange("");
        project_.pages.push_back(splash);
        pagesChanged(covered, "");
        changed();
        return true;
    }

    WidgetInstance* EditorSession::splashWidget(const std::string& def)
    {
        ThemePage* splash = project_.page(ThemeProject::splashPage());
        if (!splash) return nullptr;
        for (auto& w : splash->widgets) {
            if (def == SPLASH_BACKGROUND ? w.id == SPLASH_BACKGROUND : w.def == def) return &w;
        }
        return nullptr;
    }

    std::string EditorSession::splashText() const
    {
        const ThemePage* splash = project_.page(ThemeProject::splashPage());
        if (!splash) return project_.name;
        for (const auto& w : splash->widgets) {
            if (w.def == "static_text") {
                const WidgetDef* def = lib_ ? lib_->find(w.def) : nullptr;
                const WidgetVar* var = def ? def->var("VALUE") : nullptr;
                return w.varValue("VALUE", var ? var->def : std::string());
            }
        }
        return "";
    }

    std::string EditorSession::splashBackground() const
    {
        const ThemePage* splash = project_.page(ThemeProject::splashPage());
        if (!splash) return "";
        for (const auto& w : splash->widgets) {
            if (w.id == SPLASH_BACKGROUND) return w.varValue("SRC", "");
        }
        return "";
    }

    bool EditorSession::setSplashText(const std::string& text)
    {
        if (!isOpen() || (hasSplashPage() && splashWidget("static_text") && splashText() == text)) return false;
        const std::vector<std::string> covered = coveredByCurrent();
        beginChange(coalesce_.empty() ? "" : coalesce_);
        if (!hasSplashPage()) {
            ThemePage splash;
            if (!defaultSplashPage(project_, *lib_, splash)) return false;
            project_.pages.push_back(splash);
            pagesChanged(covered, "");
        }
        WidgetInstance* w = splashWidget("static_text");
        if (!w) {
            ThemePage fresh;
            defaultSplashPage(project_, *lib_, fresh);
            ThemePage* splash = project_.page(ThemeProject::splashPage());
            WidgetInstance name = fresh.widgets[0];
            if (splash->findWidget(name.id)) nextIdentity(*splash, name.def, "Static Text", name.id, name.name);
            splash->widgets.push_back(name);
            w = &splash->widgets.back();
        }
        w->setVar("VALUE", text);
        clampSelection();
        changed();
        return true;
    }

    bool EditorSession::setSplashBackground(const std::string& rel)
    {
        if (!isOpen() || splashBackground() == rel) return false;
        if (!rel.empty() && !lib_->find(isVideoFile(rel) ? "static_video" : "static_image")) return false;
        const std::vector<std::string> covered = coveredByCurrent();
        beginChange("");
        if (!hasSplashPage()) {
            ThemePage splash;
            if (defaultSplashPage(project_, *lib_, splash)) project_.pages.push_back(splash);
            else {
                splash = ThemePage();
                splash.collection = ThemeProject::splashPage();
                project_.pages.push_back(splash);
            }
            pagesChanged(covered, "");
        }
        ThemePage* splash = project_.page(ThemeProject::splashPage());
        for (size_t i = 0; i < splash->widgets.size(); ++i) {
            if (splash->widgets[i].id == SPLASH_BACKGROUND) {
                splash->widgets.erase(splash->widgets.begin() + static_cast<long>(i));
                break;
            }
        }
        if (!rel.empty()) {
            WidgetInstance bg;
            bg.def = isVideoFile(rel) ? "static_video" : "static_image";
            bg.id = SPLASH_BACKGROUND;
            bg.name = "Background";
            bg.x = 0;
            bg.y = 0;
            bg.w = project_.canvasW();
            bg.h = project_.canvasH();
            bg.layer = 0;
            bg.setVar("SRC", rel);
            bg.setVar("STRETCH", "true");
            splash->widgets.insert(splash->widgets.begin(), bg);
        }
        // The selection index may now point elsewhere on the splash page
        if (ThemeProject::isSplashPage(page().collection)) selected_ = -1;
        changed();
        return true;
    }

    // ---- templates ---------------------------------------------------------------

    const char* const EditorSession::BLANK = "\x01";

    std::vector<std::string> EditorSession::coveredByCurrent() const
    {
        std::vector<std::string> out;
        const ThemePage& p = page();
        out.push_back(p.collection);
        for (const auto& m : p.members) out.push_back(m);
        return out;
    }

    void EditorSession::pagesChanged(const std::vector<std::string>& covered, const std::string& goTo)
    {
        // The splash page goes last, after the templates
        auto& pages = project_.pages;
        for (size_t i = 1; i + 1 < pages.size(); ++i) {
            if (ThemeProject::isSplashPage(pages[i].collection)) {
                ThemePage splash = pages[i];
                pages.erase(pages.begin() + static_cast<long>(i));
                pages.push_back(splash);
                break;
            }
        }
        int found = -1;
        for (int i = 0; i < pageCount() && found < 0; ++i) {
            if (!goTo.empty()) {
                if (pages[i].collection == goTo) found = i;
                continue;
            }
            for (const auto& c : covered) {
                if (pages[i].covers(c)) { found = i; break; }
            }
        }
        const int before = page_;
        page_ = found < 0 ? 0 : found;
        if (page_ != before || found < 0) selected_ = -1;
        drag_ = Drag();
        clampSelection();
    }

    std::vector<std::string> EditorSession::templates() const
    {
        std::vector<std::string> out;
        for (const auto& p : project_.pages) {
            if (!p.collection.empty() && !ThemeProject::isSplashPage(p.collection)) out.push_back(p.collection);
        }
        return out;
    }

    bool EditorSession::handMadePage(const std::string& collection) const
    {
        return isOpen() && handMadeCollectionPage(layoutsRoot_, folder_, collection);
    }

    std::string EditorSession::templateProblem(const std::string& collection, const std::string& name) const
    {
        if (collection.empty()) return "Choose a collection for the template.";
        if (ThemeProject::isSplashPage(collection) || !folderNameSafe(collection)) {
            return "\"" + collection + "\" can't be a folder name, so it can't have a page of its own.";
        }
        if (handMadePage(collection)) {
            return "collections/" + collection + "/layout/layout.xml in this theme was written by hand; RetroFE uses it "
                   "for " + collection + ", and the editor won't overwrite it.";
        }
        const std::string clean = trimmed(name);
        if (clean.empty()) return "Give the template a name.";
        for (const auto& p : project_.pages) {
            if (p.collection.empty() || ThemeProject::isSplashPage(p.collection)) continue;
            if (lower(p.displayName()) == lower(clean)) return "There's already a template called " + p.displayName() + ".";
        }
        return "";
    }

    bool EditorSession::newTemplate(const std::string& collection, const std::string& name, const std::string& basedOn,
                                    const std::vector<std::string>& alsoFor)
    {
        if (!isOpen() || !templateProblem(collection, name).empty()) return false;
        if (basedOn != BLANK && !basedOn.empty()) {
            const ThemePage* seed = project_.page(basedOn);
            if (!seed || ThemeProject::isSplashPage(basedOn)) return false;
        }
        beginChange("");
        if (!createNamed(project_, collection, trimmed(name), basedOn == BLANK ? std::string("\x01") : basedOn)) return false;
        for (const std::string& c : alsoFor) {
            if (c == collection || c.empty() || ThemeProject::isSplashPage(c) || !folderNameSafe(c) || handMadePage(c)) continue;
            reassign(project_, c, collection);
        }
        pagesChanged({}, collection);
        selected_ = -1;
        changed();
        return true;
    }

    bool EditorSession::assignTemplate(const std::string& collection, const std::string& primary)
    {
        int skipped = 0;
        return assignTemplate(std::vector<std::string>{ collection }, primary, &skipped) && skipped == 0;
    }

    bool EditorSession::assignTemplate(const std::vector<std::string>& collections, const std::string& primary, int* skipped)
    {
        if (skipped) *skipped = 0;
        if (!isOpen()) return false;
        if (!primary.empty()) {
            const ThemePage* target = project_.page(primary);
            if (!target || ThemeProject::isSplashPage(primary)) return false;
        }
        std::vector<std::string> moving;
        for (const std::string& c : collections) {
            if (c.empty() || ThemeProject::isSplashPage(c)) continue;
            if (!primary.empty() && (!folderNameSafe(c) || handMadePage(c))) {
                if (skipped) ++*skipped;
                continue;
            }
            const ThemePage* current = project_.coveredBy(c);
            if ((current ? current->collection : std::string()) == primary) continue;
            if (std::find(moving.begin(), moving.end(), c) == moving.end()) moving.push_back(c);
        }
        if (moving.empty()) return false;
        const std::vector<std::string> covered = coveredByCurrent();
        beginChange("");
        for (const std::string& c : moving) {
            // An earlier move can have deleted the target (it was the target's own collection)
            if (!primary.empty() && !project_.page(primary)) break;
            reassign(project_, c, primary);
        }
        pagesChanged(covered, "");
        changed();
        return true;
    }

    std::string EditorSession::removesTemplate(const std::string& collection) const
    {
        for (const auto& p : project_.pages) {
            if (p.collection.empty() || ThemeProject::isSplashPage(p.collection)) continue;
            if (p.collection == collection && p.members.empty()) return p.displayName();
        }
        return "";
    }

    bool EditorSession::deleteTemplate(const std::string& primary, const std::string& replacement)
    {
        if (!isOpen() || primary.empty() || ThemeProject::isSplashPage(primary) || !project_.page(primary)) return false;
        if (!replacement.empty() && (replacement == primary || !project_.page(replacement) || ThemeProject::isSplashPage(replacement))) {
            return false;
        }
        const std::vector<std::string> covered = coveredByCurrent();
        beginChange("");
        deleteTemplateTo(project_, primary, replacement);
        pagesChanged(covered, "");
        changed();
        return true;
    }

    bool EditorSession::renameTemplate(const std::string& primary, const std::string& name)
    {
        const std::string clean = trimmed(name);
        ThemePage* p = project_.page(primary);
        if (!isOpen() || clean.empty() || !p || primary.empty() || ThemeProject::isSplashPage(primary)) return false;
        if (p->displayName() == clean) return false;
        beginChange(coalesce_.empty() ? "" : coalesce_);
        project_.page(primary)->tplName = clean;
        changed();
        return true;
    }

    // ---- files -----------------------------------------------------------------

    bool EditorSession::themeHasFile(const std::string& rel) const
    {
        if (!isOpen() || rel.empty()) return false;
        std::error_code ec;
        return std::filesystem::is_regular_file(std::filesystem::path(layoutsRoot_) / folder_ / rel, ec);
    }

    std::vector<std::string> EditorSession::missingFiles() const
    {
        std::vector<std::string> out;
        const WidgetInstance* w = selectedWidget();
        const WidgetDef* def = selectedDef();
        if (!w || !def) return out;
        for (const auto& v : def->vars) {
            if (v.type != "file") continue;
            if (!themeHasFile(w->varValue(v.token, v.def))) out.push_back(v.token);
        }
        return out;
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

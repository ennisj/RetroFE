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
#include "LayoutSession.h"
#include "EditorSelection.h"
#include "LayoutCheck.h"
#include "LayoutPalette.h"
#include "ThemeExport.h"
#include "ThemeGenerate.h"
#include "ThemeProject.h"
#include "ThemeStore.h"
#include "../Library/Backups.h"
#include "../Library/Files.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace ThemeEditor
{
    namespace
    {
        const float CLICK_SLOP = 3.0f;

        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool numberValue(const std::string& text, int& out)
        {
            if (text.empty()) return false;
            char* end = nullptr;
            double v = std::strtod(text.c_str(), &end);
            if (!end || *end != '\0') return false;
            out = static_cast<int>(std::lround(v));
            return true;
        }

        // A position as written: "960", "-20", "1012.5"
        std::string numberText(float v)
        {
            const float whole = std::round(v);
            if (std::fabs(v - whole) < 0.005f) return std::to_string(static_cast<long>(whole));
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.2f", v);
            std::string s = buffer;
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
            return s;
        }

        std::string firstLine(const std::string& text)
        {
            size_t nl = text.find('\n');
            std::string line = text.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }

        // A file name RetroFE reads as a main page: layout.xml, "layout 4x3.xml",
        // "layout - 1.xml", and the fixed-resolution "16x9layout.xml"
        bool mainPageName(const std::string& name)
        {
            const std::string l = lower(name);
            if (l.size() < 4 || l.compare(l.size() - 4, 4, ".xml") != 0) return false;
            return l.find("layout") != std::string::npos || l == "splash.xml";
        }

        bool isAnimationEvent(const std::string& name)
        {
            return name.size() > 2 && name[0] == 'o' && name[1] == 'n' && std::isupper(static_cast<unsigned char>(name[2]));
        }

        bool inAnimation(const LayoutDoc& doc, int i)
        {
            for (int e = i; e >= 0; e = doc.elements[e].parent) {
                if (isAnimationEvent(doc.elements[e].name)) return true;
            }
            return false;
        }

        // The paths of everything but animations, in document order
        std::vector<std::string> pagePaths(const LayoutDoc& doc)
        {
            std::vector<std::string> out;
            for (size_t i = 0; i < doc.elements.size(); ++i) {
                if (!inAnimation(doc, static_cast<int>(i))) out.push_back(doc.elements[i].path);
            }
            return out;
        }

        // An animate's value as PageBuilder resolves it (getHorizontalAlignment
        // for x, xOffset, width; getVerticalAlignment for y, yOffset, height)
        float tweenValue(const std::string& value, bool horizontal, int w, int h)
        {
            const float size = static_cast<float>(horizontal ? w : h);
            if (value.empty()) return 0.0f;
            if (value == (horizontal ? "left" : "top")) return 0.0f;
            if (value == "center") return size / 2.0f;
            if (value == (horizontal ? "right" : "bottom") || value == "stretch") return size;
            if (value.back() == '%') return std::round(size * static_cast<float>(std::atof(value.substr(0, value.size() - 1).c_str())) / 100.0f);
            return static_cast<float>(std::atof(value.c_str()));
        }
    }

    // ---- boxes from the engine -----------------------------------------------

    ElementGeom geometryOf(const LayoutDoc& doc, const TracedView& v, int order)
    {
        ElementGeom g;
        g.x = v.x;
        g.y = v.y;
        g.xOffset = v.xOffset;
        g.yOffset = v.yOffset;
        g.xOrigin = v.xOrigin;
        g.yOrigin = v.yOrigin;
        g.layer = v.layer;
        g.order = order;
        float w = v.scaledW;
        float h = v.scaledH;
        if (!(std::isfinite(w) && std::isfinite(h) && w > 0.0f && h > 0.0f)) {
            // Not known until it draws (text without a width, art not loaded yet): what the layout says, else a guess
            g.approximate = true;
            float fh = v.height > 0 ? v.height : (v.maxHeight > 0 ? v.maxHeight : (v.fontSize > 0 ? v.fontSize : 0.0f));
            float fw = v.width > 0 ? v.width : (v.maxWidth > 0 ? v.maxWidth : 0.0f);
            if (fw <= 0) {
                int el = doc.find(v.path);
                size_t chars = el >= 0 ? doc.elements[el].value("value").size() : 0;
                fw = chars > 0 && fh > 0 ? static_cast<float>(chars) * fh * 0.5f : (fh > 0 ? fh * 6.0f : 64.0f);
            }
            if (fh <= 0) fh = std::min(fw, 64.0f);
            w = fw;
            h = fh;
        }
        g.w = w;
        g.h = h;
        return g;
    }

    namespace
    {
        DragRect rectOf(const ElementGeom& g)
        {
            DragRect r;
            r.x = g.left();
            r.y = g.top();
            r.w = std::max(1, roundDelta(g.w));
            r.h = std::max(1, roundDelta(g.h));
            return r;
        }
    }

    PageBoxes pageBoxes(const LayoutDoc& doc, const std::vector<TracedView>& views)
    {
        PageBoxes out;
        // Menus whose slots are generated from their settings (vertical menus)
        std::set<const void*> generated;
        for (const TracedView& v : views) {
            if (v.slot) continue;
            int el = doc.find(v.path);
            if (el >= 0 && doc.elements[el].name == "menu" && doc.elements[el].value("type") != "custom") generated.insert(v.component);
        }
        for (size_t i = 0; i < views.size(); ++i) {
            const TracedView& v = views[i];
            std::string path = v.path;
            int el = doc.find(path);
            if (el < 0 && v.slot) {
                // Built from a node that isn't in the text (the empty <itemDefaults> a menu without one gets): its menu's
                size_t slash = path.rfind('/');
                path = slash == std::string::npos ? std::string() : path.substr(0, slash);
                el = path.empty() ? -1 : doc.find(path);
            }
            if (el <= 0) continue;
            ElementGeom g = geometryOf(doc, v, static_cast<int>(i));
            g.readOnly = v.slot && generated.count(v.component) > 0;
            if (out.geometry.count(path)) {
                out.extra.emplace_back(path, rectOf(g));
                continue;
            }
            out.geometry[path] = g;
        }
        // A menu without a size of its own: the box around its slots, to select it by (not to drag)
        for (const TracedView& v : views) {
            if (v.slot) continue;
            auto it = out.geometry.find(v.path);
            if (it == out.geometry.end() || !it->second.approximate || doc.find(v.path) < 0 ||
                doc.elements[doc.find(v.path)].name != "menu") {
                continue;
            }
            bool any = false;
            DragRect around;
            for (size_t i = 0; i < views.size(); ++i) {
                if (!views[i].slot || views[i].component != v.component) continue;
                DragRect r = rectOf(geometryOf(doc, views[i], static_cast<int>(i)));
                if (!any) { around = r; any = true; continue; }
                const int x2 = std::max(around.x + around.w, r.x + r.w);
                const int y2 = std::max(around.y + around.h, r.y + r.h);
                around.x = std::min(around.x, r.x);
                around.y = std::min(around.y, r.y);
                around.w = x2 - around.x;
                around.h = y2 - around.y;
            }
            if (!any) continue;
            it->second.around = true;
            it->second.aroundBox = around;
            it->second.readOnly = true;
            it->second.order = -1;   // under its slots
        }
        return out;
    }

    // ---- pages ---------------------------------------------------------------

    std::vector<std::string> LayoutSession::themePages(const std::string& themeDir)
    {
        std::vector<std::string> main, collections, forms;
        std::error_code ec;
        const fs::path dir = fs::path(themeDir);
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::string name = it->path().filename().string();
            if (it->is_regular_file(ec) && mainPageName(name)) main.push_back(name);
        }
        // layout.xml first, then its variants, splash.xml last
        std::sort(main.begin(), main.end(), [](const std::string& a, const std::string& b) {
            auto rank = [](const std::string& n) { return n == "layout.xml" ? 0 : (lower(n) == "splash.xml" ? 2 : 1); };
            if (rank(a) != rank(b)) return rank(a) < rank(b);
            return lower(a) < lower(b);
        });
        for (fs::directory_iterator it(dir / "collections", ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            std::error_code pageEc;
            for (fs::directory_iterator page(it->path() / "layout", pageEc), pageEnd; !pageEc && page != pageEnd; page.increment(pageEc)) {
                if (page->is_regular_file(pageEc) && lower(page->path().extension().string()) == ".xml") {
                    collections.push_back("collections/" + it->path().filename().string() + "/layout/" + page->path().filename().string());
                }
            }
        }
        for (fs::recursive_directory_iterator it(dir / "forms", ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".xml") {
                forms.push_back(it->path().lexically_relative(dir).generic_string());
            }
        }
        auto byName = [](const std::string& a, const std::string& b) { return lower(a) < lower(b); };
        std::sort(collections.begin(), collections.end(), byName);
        std::sort(forms.begin(), forms.end(), byName);
        std::vector<std::string> all = main;
        all.insert(all.end(), collections.begin(), collections.end());
        all.insert(all.end(), forms.begin(), forms.end());
        return all;
    }

    // ---- opening -------------------------------------------------------------

    std::string LayoutSession::themeDir() const
    {
        return (fs::path(root_) / "layouts" / fs::path(folder_)).string();
    }

    std::string LayoutSession::file() const
    {
        return (fs::path(themeDir()) / fs::path(page_)).make_preferred().string();
    }

    std::string LayoutSession::pageDir() const
    {
        return fs::path(file()).parent_path().string();
    }

    bool LayoutSession::insideTheme(std::string& error) const
    {
        std::error_code ec;
        const fs::path theme = fs::weakly_canonical(fs::path(themeDir()), ec);
        const fs::path layouts = fs::weakly_canonical(fs::path(root_) / "layouts", ec);
        const fs::path target = fs::weakly_canonical(fs::path(file()), ec);
        const fs::path inTheme = target.lexically_relative(theme);
        const fs::path inLayouts = theme.lexically_relative(layouts);
        if (ec || inTheme.empty() || *inTheme.begin() == ".." || inLayouts.empty() || *inLayouts.begin() == "..") {
            error = "layouts/" + folder_ + "/" + page_ + " isn't inside the theme's folder, so it isn't written.";
            return false;
        }
        return true;
    }

    bool LayoutSession::open(const std::string& root, const std::string& folder, const std::string& page, std::string& error)
    {
        *this = LayoutSession();
        if (!themeFolderValid(folder)) {
            error = "\"" + folder + "\" isn't a theme folder name.";
            return false;
        }
        const fs::path rel = fs::path(page);
        bool relative = !page.empty() && rel.is_relative() && !rel.has_root_name() && !rel.has_root_directory();
        for (const fs::path& part : rel) {
            if (part == ".." || part == ".") relative = false;
        }
        if (!relative || lower(rel.extension().string()) != ".xml") {
            error = "\"" + page + "\" isn't a page of the theme.";
            return false;
        }
        root_ = root;
        folder_ = folder;
        page_ = rel.generic_string();
        if (!insideTheme(error)) return false;
        std::string text;
        if (!Library::readFile(file(), text)) {
            error = "Can't read layouts/" + folder_ + "/" + page_ + ".";
            return false;
        }
        std::string line = firstLine(text);
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);
        if (isEditorMarker(line)) {
            error = "layouts/" + folder_ + "/" + page_ + " was written by the theme editor from its widgets; edit it there.";
            return false;
        }
        std::string parseError = layoutParseError(text);
        if (!parseError.empty()) {
            error = "RetroFE can't read layouts/" + folder_ + "/" + page_ + ": " + parseError;
            return false;
        }
        if (!load(text, error)) {
            error = "layouts/" + folder_ + "/" + page_ + " can't be edited here: " + error;
            return false;
        }
        saved_ = text;
        open_ = true;
        return true;
    }

    bool LayoutSession::load(const std::string& text, std::string& error)
    {
        LayoutDoc next;
        if (!parseLayoutText(text, next, error)) return false;
        // Animation edits move only the paths inside events: the boxes stay
        const bool samePaths = pagePaths(next) == pagePaths(doc_);
        doc_ = std::move(next);
        if (!samePaths) {
            // Elements came or went: the boxes are found again by the next build,
            // and what was hidden by path may be another element now
            geom_.clear();
            hidden_.clear();
            structure_++;
        }
        return true;
    }

    int LayoutSession::canvasW() const
    {
        int w = 0;
        if (!doc_.elements.empty() && numberValue(doc_.elements[0].value("width"), w) && w > 0) return w;
        return engineW_ > 0 ? engineW_ : 1920;
    }

    int LayoutSession::canvasH() const
    {
        int h = 0;
        if (!doc_.elements.empty() && numberValue(doc_.elements[0].value("height"), h) && h > 0) return h;
        return engineH_ > 0 ? engineH_ : 1080;
    }

    // ---- elements ------------------------------------------------------------

    std::vector<int> LayoutSession::listed() const
    {
        std::vector<int> out;
        if (doc_.elements.empty()) return out;
        out.push_back(0);
        for (int child : doc_.elements[0].children) {
            out.push_back(child);
            if (doc_.elements[child].name != "menu") continue;
            for (int item : doc_.elements[child].children) {
                const std::string& n = doc_.elements[item].name;
                if (n == "item" || n == "itemDefaults") out.push_back(item);
            }
        }
        return out;
    }

    std::string LayoutSession::label(int i) const
    {
        if (i < 0 || i >= elementCount()) return "";
        const LayoutElement& e = doc_.elements[i];
        if (i == 0) return "The page (<layout>)";
        if (e.name == "itemDefaults") return "item defaults";
        if (e.name == "item") {
            size_t open = e.path.rfind('[');
            std::string n = open == std::string::npos ? "" : e.path.substr(open + 1, e.path.size() - open - 2);
            std::string text = "item " + n;
            if (e.has("index")) text += " (index " + e.value("index") + ")";
            if (e.has("selected")) text += ", selected";
            return text;
        }
        std::string detail;
        for (const char* key : { "id", "src", "type", "value", "imageType", "location" }) {
            if (e.has(key)) { detail = e.value(key); break; }
        }
        if (detail.size() > 40) detail = detail.substr(0, 37) + "...";
        return detail.empty() ? e.name : e.name + " (" + detail + ")";
    }

    std::vector<std::string> LayoutSession::animations(int i) const
    {
        std::vector<std::string> out;
        if (i < 0 || i >= elementCount()) return out;
        for (int child : doc_.elements[i].children) {
            const LayoutElement& c = doc_.elements[child];
            if (!isAnimationEvent(c.name)) continue;
            int sets = 0;
            for (int s : c.children) sets += doc_.elements[s].name == "set";
            std::string text = c.name + " (" + std::to_string(sets) + (sets == 1 ? " set" : " sets");
            if (c.has("menuIndex")) text += ", menuIndex " + c.value("menuIndex");
            out.push_back(text + ")");
        }
        return out;
    }

    std::vector<std::string> LayoutSession::animatedProperties(int i) const
    {
        std::vector<std::string> out;
        if (i < 0 || i >= elementCount()) return out;
        std::set<std::string> seen;
        for (int event : doc_.elements[i].children) {
            if (!isAnimationEvent(doc_.elements[event].name)) continue;
            for (int set : doc_.elements[event].children) {
                for (int animate : doc_.elements[set].children) {
                    const std::string type = doc_.elements[animate].value("type");
                    if (!type.empty() && seen.insert(type).second) out.push_back(type);
                }
            }
        }
        return out;
    }

    const ElementGeom* LayoutSession::geometry(int i) const
    {
        if (i <= 0 || i >= elementCount()) return nullptr;
        auto it = geom_.find(doc_.elements[i].path);
        return it == geom_.end() ? nullptr : &it->second;
    }

    DragRect LayoutSession::box(int i) const
    {
        DragRect r;
        const ElementGeom* g = geometry(i);
        if (!g) return r;
        if (g->around) return g->aroundBox;
        r.x = g->left();
        r.y = g->top();
        r.w = std::max(1, roundDelta(g->w));
        r.h = std::max(1, roundDelta(g->h));
        return r;
    }

    bool LayoutSession::editableBox(int i) const
    {
        const ElementGeom* g = geometry(i);
        return g && !g->readOnly;
    }

    bool LayoutSession::hidden(int i) const
    {
        if (i < 0 || i >= elementCount()) return false;
        auto it = hidden_.find(doc_.elements[i].path);
        return it != hidden_.end() && it->second;
    }

    void LayoutSession::setHidden(int i, bool hide)
    {
        if (i <= 0 || i >= elementCount()) return;
        hidden_[doc_.elements[i].path] = hide;
    }

    void LayoutSession::select(int i)
    {
        assignSelection(i >= 0 && i < elementCount() ? std::vector<int>{ i } : std::vector<int>());
    }

    void LayoutSession::assignSelection(const std::vector<int>& elements)
    {
        std::vector<int> next;
        for (int i : elements) {
            if (i < 0 || i >= elementCount()) continue;
            next = selectionWith(next, i);
        }
        // The page itself is only ever selected alone
        if (next.size() > 1) next = selectionWithout(next, 0);
        // Another set selected: the next edit is a step of its own (a nudge
        // mustn't merge into the last group's), as EditorSession does
        std::vector<int> a = next, b = selection_;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) lastKey_.clear();
        selection_ = next;
        selected_ = selection_.empty() ? -1 : selection_.back();
    }

    bool LayoutSession::isSelected(int i) const
    {
        return std::find(selection_.begin(), selection_.end(), i) != selection_.end();
    }

    void LayoutSession::setSelection(const std::vector<int>& elements)
    {
        assignSelection(elements);
    }

    void LayoutSession::toggleSelected(int i)
    {
        if (i < 0 || i >= elementCount()) return;
        assignSelection(selectionToggled(selection_, i));
    }

    void LayoutSession::addSelected(int i)
    {
        if (i < 0 || i >= elementCount()) return;
        assignSelection(selectionWith(selection_, i));
    }

    bool LayoutSession::dropPrimary()
    {
        if (selection_.empty()) return false;
        assignSelection(selectionWithout(selection_, selected_));
        return true;
    }

    void LayoutSession::selectAll()
    {
        std::vector<int> all;
        for (int i : listed()) {
            if (i > 0 && hasBox(i) && !hidden(i)) all.push_back(i);
        }
        if (selected_ > 0 && isSelected(selected_) && hasBox(selected_) && !hidden(selected_)) all = selectionWith(all, selected_);
        assignSelection(all);
    }

    std::vector<int> LayoutSession::movableSelection() const
    {
        std::vector<int> out;
        for (int i : selection_) {
            if (editableBox(i)) out.push_back(i);
        }
        return out;
    }

    std::vector<int> LayoutSession::topSelected() const
    {
        std::vector<int> out;
        for (int i : selection_) {
            if (i <= 0) continue;
            bool inside = false;
            for (int e = doc_.elements[i].parent; e > 0 && !inside; e = doc_.elements[e].parent) inside = isSelected(e);
            if (!inside) out.push_back(i);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<std::string> LayoutSession::selectionPaths() const
    {
        std::vector<std::string> out;
        for (int i : selection_) out.push_back(doc_.elements[i].path);
        return out;
    }

    void LayoutSession::selectPaths(const std::vector<std::string>& paths)
    {
        std::vector<int> found;
        for (const std::string& p : paths) {
            const int i = doc_.find(p);
            if (i >= 0) found.push_back(i);
        }
        assignSelection(found);
    }

    DragRect LayoutSession::marqueeBox() const
    {
        return marqueeRect(drag_.startTx, drag_.startTy, drag_.endTx, drag_.endTy);
    }

    bool LayoutSession::selectPath(const std::string& path)
    {
        int i = doc_.find(path);
        if (i < 0) return false;
        select(i);
        return true;
    }

    const LayoutElement* LayoutSession::selectedElement() const
    {
        return selected_ >= 0 && selected_ < elementCount() ? &doc_.elements[selected_] : nullptr;
    }

    std::string LayoutSession::selectedPath() const
    {
        const LayoutElement* e = selectedElement();
        return e ? e->path : std::string("\x01");   // nothing selected: matches no path
    }

    // ---- picking -------------------------------------------------------------

    int LayoutSession::pick(float tx, float ty) const
    {
        int best = -1;
        const ElementGeom* bestGeom = nullptr;
        for (int i : listed()) {
            const ElementGeom* g = geometry(i);
            if (!g || hidden(i)) continue;
            DragRect r = box(i);
            if (tx < r.x || ty < r.y || tx > r.x + r.w || ty > r.y + r.h) continue;
            if (!bestGeom || g->layer > bestGeom->layer || (g->layer == bestGeom->layer && g->order >= bestGeom->order)) {
                best = i;
                bestGeom = g;
            }
        }
        return best;
    }

    int LayoutSession::pickNext(float tx, float ty, int current) const
    {
        std::vector<int> hits;
        for (int i : listed()) {
            if (!geometry(i) || hidden(i)) continue;
            DragRect r = box(i);
            if (tx < r.x || ty < r.y || tx > r.x + r.w || ty > r.y + r.h) continue;
            hits.push_back(i);
        }
        if (hits.empty()) return -1;
        std::sort(hits.begin(), hits.end(), [this](int a, int b) {
            const ElementGeom* ga = geometry(a);
            const ElementGeom* gb = geometry(b);
            if (ga->layer != gb->layer) return ga->layer > gb->layer;
            return ga->order > gb->order;
        });
        for (size_t k = 0; k < hits.size(); ++k) {
            if (hits[k] == current) return hits[(k + 1) % hits.size()];
        }
        return hits[0];
    }

    // ---- pointer -------------------------------------------------------------

    void LayoutSession::pointerDown(const CanvasFit& fit, float sx, float sy, float grabPx, bool additive)
    {
        drag_ = Drag();
        if (!fit.ok || !open_) return;
        const float tx = canvasToThemeX(fit, sx);
        const float ty = canvasToThemeY(fit, sy);
        auto begin = [&]() {
            drag_.active = true;
            drag_.startSx = sx;
            drag_.startSy = sy;
            drag_.startTx = drag_.endTx = tx;
            drag_.startTy = drag_.endTy = ty;
        };
        auto marquee = [&]() {
            begin();
            drag_.marquee = true;
            drag_.additive = additive;
        };
        // Ctrl / Shift: a click toggles the element under it, a drag (from anywhere:
        // hand-made pages usually have a full-page background) is a marquee that adds
        if (additive) {
            marquee();
            drag_.clicked = pick(tx, ty);
            return;
        }
        // Several selected: a press on any of them moves the ones that can move
        if (selection_.size() > 1) {
            int on = -1;
            const ElementGeom* onGeom = nullptr;
            for (int i : selection_) {
                const ElementGeom* g = geometry(i);
                if (!g || hidden(i)) continue;
                const DragRect r = box(i);
                if (tx < r.x || ty < r.y || tx > r.x + r.w || ty > r.y + r.h) continue;
                if (!onGeom || g->layer > onGeom->layer || (g->layer == onGeom->layer && g->order >= onGeom->order)) { on = i; onGeom = g; }
            }
            if (on > 0) {
                begin();
                drag_.group = true;
                drag_.handle = CH_BODY;
                drag_.clicked = on;
                std::vector<DragRect> boxes;
                for (int i : movableSelection()) boxes.push_back(box(i));
                drag_.start = unionRect(boxes);
                drag_.rect = drag_.start;
                return;
            }
        }
        int handle = CH_NONE;
        if (selection_.size() == 1 && selected_ > 0 && hasBox(selected_) && !hidden(selected_)) {
            handle = canvasHandleAt(fit, box(selected_), sx, sy, grabPx);
            // A box that can't be edited still takes clicks (click-through), never handles
            if (!editableBox(selected_) && handle != CH_NONE) handle = CH_BODY;
        }
        if (handle == CH_NONE) {
            int picked = pick(tx, ty);
            select(picked);
            if (picked < 0) {
                marquee();
                return;
            }
            handle = CH_BODY;
        }
        else if (handle == CH_BODY) {
            drag_.cycleOnClick = true;
        }
        begin();
        drag_.handle = handle;
        drag_.start = box(selected_);
        drag_.rect = drag_.start;
    }

    void LayoutSession::pointerMove(const CanvasFit& fit, float sx, float sy)
    {
        if (!drag_.active || !fit.ok) return;
        if (!drag_.moved && std::fabs(sx - drag_.startSx) < CLICK_SLOP && std::fabs(sy - drag_.startSy) < CLICK_SLOP) return;
        if (drag_.marquee) {
            drag_.moved = true;
            drag_.endTx = canvasToThemeX(fit, sx);
            drag_.endTy = canvasToThemeY(fit, sy);
            return;
        }
        // Something that can't be edited is only clicked
        if (drag_.group ? movableSelection().empty() : !editableBox(selected_)) return;
        drag_.moved = true;
        const float dx = (sx - drag_.startSx) / fit.scale;
        const float dy = (sy - drag_.startSy) / fit.scale;
        const int grid = snap ? CANVAS_SNAP_GRID : 1;
        if (drag_.handle == CH_BODY) {
            drag_.rect = canvasMove(drag_.start, dx, dy, grid, canvasW(), canvasH());
            // An axis the pointer hasn't really moved along stays as it is,
            // unsnapped: dragging straight down keeps x="center"
            if (std::fabs(sx - drag_.startSx) < CLICK_SLOP) drag_.rect.x = drag_.start.x;
            if (std::fabs(sy - drag_.startSy) < CLICK_SLOP) drag_.rect.y = drag_.start.y;
        }
        else {
            drag_.rect = canvasResize(drag_.start, drag_.handle, dx, dy, 1, 1, grid, canvasW(), canvasH());
        }
    }

    void LayoutSession::pointerUp()
    {
        if (!drag_.active) return;
        Drag d = drag_;
        drag_ = Drag();
        if (d.marquee) {
            if (!d.moved) {
                if (d.additive && d.clicked > 0) toggleSelected(d.clicked);
                return;
            }
            const DragRect area = marqueeRect(d.startTx, d.startTy, d.endTx, d.endTy);
            std::vector<DragRect> rects;
            std::vector<int> index;
            for (int i : listed()) {
                if (i <= 0 || !hasBox(i) || hidden(i)) continue;
                rects.push_back(box(i));
                index.push_back(i);
            }
            std::vector<int> picked = d.additive ? selection_ : std::vector<int>();
            for (int k : marqueePick(rects, area)) {
                if (!d.additive || !isSelected(index[k])) picked.push_back(index[k]);
            }
            assignSelection(picked);
            return;
        }
        if (d.group) {
            if (!d.moved) { select(d.clicked); return; }
            endCoalesce();
            shiftSelection(d.rect.x - d.start.x, d.rect.y - d.start.y);
            return;
        }
        if (!d.moved) {
            if (d.cycleOnClick) select(pickNext(d.startTx, d.startTy, selected_));
            return;
        }
        endCoalesce();
        commitBox(d.start, d.rect, d.handle != CH_BODY);
    }

    void LayoutSession::cancelDrag()
    {
        drag_ = Drag();
    }

    // ---- edits ---------------------------------------------------------------

    bool LayoutSession::change(const std::string& text, const std::string& key, const std::string& selectAfter)
    {
        if (selectAfter.empty()) return change(text, key, selectionPaths());
        return change(text, key, std::vector<std::string>{ selectAfter });
    }

    bool LayoutSession::change(const std::string& text, const std::string& key, const std::vector<std::string>& selectAfter)
    {
        if (!open_ || text == doc_.text) return false;
        const std::string before = doc_.text;
        const std::string beforeSelected = selectedPath();
        const std::vector<std::string> beforeSelection = selectionPaths();
        std::string error;
        if (!load(text, error)) return false;   // an edit never makes text that doesn't scan
        std::string full = key.empty() ? "" : key + "@" + beforeSelected;
        bool merge = !full.empty() && full == lastKey_ && !undo_.empty() && redo_.empty();
        if (!merge) {
            pushUndo(Snapshot{ before, beforeSelection, 0, ++nextStep_ });
        }
        redo_.clear();
        lastKey_ = full;
        selectPaths(selectAfter);
        revision_++;
        return true;
    }

    void LayoutSession::pushUndo(const Snapshot& s)
    {
        undo_.push_back(s);
        while (undo_.size() > UNDO_LIMIT) {
            if (undo_.front().link != 0) evicted_.push_back(undo_.front().link);
            undo_.erase(undo_.begin());
        }
    }

    bool LayoutSession::hasUndoLink(unsigned link) const
    {
        for (const Snapshot& s : undo_) {
            if (link != 0 && s.link == link) return true;
        }
        return false;
    }

    bool LayoutSession::hasRedoLink(unsigned link) const
    {
        for (const Snapshot& s : redo_) {
            if (link != 0 && s.link == link) return true;
        }
        return false;
    }

    std::vector<unsigned> LayoutSession::takeEvictedLinks()
    {
        std::vector<unsigned> out;
        out.swap(evicted_);
        return out;
    }

    void LayoutSession::forgetThrough(unsigned link)
    {
        if (link == 0) return;
        for (size_t k = 0; k < undo_.size(); ++k) {
            if (undo_[k].link == link) {
                undo_.erase(undo_.begin(), undo_.begin() + static_cast<long>(k) + 1);
                return;
            }
        }
    }

    bool LayoutSession::applyLinked(const std::string& text, unsigned link)
    {
        if (!open_ || text == doc_.text) return false;
        const std::string before = doc_.text;
        const std::vector<std::string> beforeSelection = selectionPaths();
        std::string error;
        if (!load(text, error)) return false;
        const bool merge = link != 0 && !undo_.empty() && undo_.back().link == link && redo_.empty();
        if (!merge) {
            pushUndo(Snapshot{ before, beforeSelection, link, ++nextStep_ });
        }
        redo_.clear();
        lastKey_.clear();
        selectPaths(beforeSelection);
        revision_++;
        return true;
    }

    std::string LayoutSession::attributeNameProblem(const std::string& name)
    {
        if (name.empty()) return "An attribute needs a name.";
        auto start = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':'; };
        if (!start(name[0])) return "An attribute's name starts with a letter.";
        for (char c : name) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '-' || c == '.')) {
                return "An attribute's name has only letters, digits, '_', '-', '.' and ':'.";
            }
        }
        return "";
    }

    bool LayoutSession::setAttribute(const std::string& name, const std::string& value)
    {
        if (selected_ < 0 || !attributeNameProblem(name).empty()) return false;
        return change(withAttribute(doc_, selected_, name, value), coalesce_);
    }

    bool LayoutSession::removeAttribute(const std::string& name)
    {
        if (selected_ < 0) return false;
        return change(withoutAttribute(doc_, selected_, name), "");
    }

    bool LayoutSession::setLayer(int layer)
    {
        return setAttribute("layer", std::to_string(clampLayer(layer)));
    }

    bool LayoutSession::commitBox(const DragRect& from, const DragRect& to, bool resize)
    {
        BoxEdit edit;
        edit.element = selected_;
        edit.from = from;
        edit.to = to;
        edit.resize = resize;
        // A run of controller nudges (move or resize) with a key is one undo
        // step; a mouse drag ended the run first (pointerUp), so it's its own
        return commitBoxes({ edit }, coalesce_);
    }

    bool LayoutSession::commitBoxes(const std::vector<BoxEdit>& edits, const std::string& key)
    {
        // One text change, one undo step. Attribute edits don't move elements,
        // so every index stays valid in the working copy.
        LayoutDoc work = doc_;
        std::vector<std::pair<std::string, ElementGeom>> after;
        for (const BoxEdit& edit : edits) {
            std::vector<std::pair<std::string, std::string>> writes;
            ElementGeom g;
            if (!boxWrites(edit, writes, g)) continue;
            for (const auto& w : writes) {
                std::string next = withAttribute(work, edit.element, w.first, w.second);
                std::string error;
                if (!parseLayoutText(next, work, error)) return false;
            }
            after.emplace_back(doc_.elements[edit.element].path, g);
        }
        if (after.empty()) return false;
        if (!change(work.text, key)) return false;
        // The boxes stay where they were dropped until the page is rebuilt
        for (const auto& a : after) geom_[a.first] = a.second;
        return true;
    }

    bool LayoutSession::boxWrites(const BoxEdit& edit, std::vector<std::pair<std::string, std::string>>& writes, ElementGeom& after) const
    {
        const DragRect& from = edit.from;
        const DragRect& to = edit.to;
        const bool resize = edit.resize;
        const ElementGeom* found = geometry(edit.element);
        if (!found || found->readOnly) return false;
        if (from.x == to.x && from.y == to.y && from.w == to.w && from.h == to.h) return false;
        ElementGeom g = *found;
        const LayoutElement& el = doc_.elements[edit.element];

        // Which attribute positions it on each axis: its own x, else its own xOffset, else a new x
        const bool byXOffset = !el.has("x") && el.has("xOffset");
        const bool byYOffset = !el.has("y") && el.has("yOffset");
        // The exact edges before the change (a centred box of odd width sits on a half unit)
        const float leftBefore = g.exactLeft();
        const float topBefore = g.exactTop();
        auto place = [&](bool horizontal) {
            const bool byOffset = horizontal ? byXOffset : byYOffset;
            float& current = horizontal ? (byOffset ? g.xOffset : g.x) : (byOffset ? g.yOffset : g.y);
            const float moved = static_cast<float>(horizontal ? to.x - from.x : to.y - from.y);
            float wanted = current + moved;
            if (resize) {
                // The moved edge, exactly: left = x + xOffset - xOrigin * w, solved for the attribute that moves
                const float base = horizontal ? (byOffset ? g.x : g.xOffset) : (byOffset ? g.y : g.yOffset);
                const float origin = horizontal ? g.xOrigin : g.yOrigin;
                const float size = horizontal ? g.w : g.h;
                // A dragged edge goes where it was dropped (on the grid); one that wasn't stays exactly put
                const float edge = moved != 0.0f ? static_cast<float>(horizontal ? to.x : to.y) : (horizontal ? leftBefore : topBefore);
                wanted = edge - base + origin * size;
            }
            wanted = std::round(wanted * 100.0f) / 100.0f;
            if (std::fabs(wanted - current) < 0.005f) return;
            const std::string name = horizontal ? (byOffset ? "xOffset" : "x") : (byOffset ? "yOffset" : "y");
            writes.emplace_back(name, numberText(wanted));
            current = wanted;
        };
        if (resize) {
            const float oldW = g.w, oldH = g.h;
            if (to.w != from.w) { writes.emplace_back("width", std::to_string(to.w)); }
            if (to.h != from.h) { writes.emplace_back("height", std::to_string(to.h)); }
            // An untouched size keeps its exact value for the edge maths
            g.w = to.w != from.w ? static_cast<float>(to.w) : oldW;
            g.h = to.h != from.h ? static_cast<float>(to.h) : oldH;
            place(true);
            place(false);
        }
        else {
            if (to.x != from.x) place(true);
            if (to.y != from.y) place(false);
        }
        if (writes.empty()) return false;
        g.approximate = false;
        after = g;
        return true;
    }

    bool LayoutSession::moveBy(int dx, int dy)
    {
        const std::vector<int> movable = movableSelection();
        if (movable.empty() || (dx == 0 && dy == 0)) return false;
        std::vector<DragRect> boxes;
        for (int i : movable) boxes.push_back(box(i));
        // The selection as one box stays on the canvas (or where it already was)
        int mx = 0, my = 0;
        groupMoveDelta(boxes, static_cast<float>(dx), static_cast<float>(dy), 1, canvasW(), canvasH(), mx, my);
        if (mx == 0 && my == 0) return false;
        std::string saved = coalesce_;
        coalesce_ = "nudge";   // a run of nudges is one step, until endCoalesce()
        bool ok = shiftSelection(mx, my);
        coalesce_ = saved;
        return ok;
    }

    bool LayoutSession::shiftSelection(int dx, int dy)
    {
        if (dx == 0 && dy == 0) return false;
        std::vector<BoxEdit> edits;
        for (int i : movableSelection()) {
            BoxEdit e;
            e.element = i;
            e.from = box(i);
            e.to = e.from;
            e.to.x += dx;
            e.to.y += dy;
            edits.push_back(e);
        }
        return !edits.empty() && commitBoxes(edits, coalesce_);
    }

    bool LayoutSession::alignSelected(int op)
    {
        const std::vector<int> movable = movableSelection();
        if (movable.size() < 2) return false;
        std::vector<DragRect> before;
        for (int i : movable) before.push_back(box(i));
        const std::vector<DragRect> after = alignRects(before, op);
        std::vector<BoxEdit> edits;
        for (size_t k = 0; k < movable.size(); ++k) {
            BoxEdit e;
            e.element = movable[k];
            e.from = before[k];
            e.to = after[k];
            edits.push_back(e);
        }
        endCoalesce();
        return commitBoxes(edits, "");
    }

    bool LayoutSession::distributeSelected(bool horizontal)
    {
        const std::vector<int> movable = movableSelection();
        if (movable.size() < 3) return false;
        std::vector<DragRect> before;
        for (int i : movable) before.push_back(box(i));
        const std::vector<DragRect> after = distributeRects(before, horizontal);
        std::vector<BoxEdit> edits;
        for (size_t k = 0; k < movable.size(); ++k) {
            BoxEdit e;
            e.element = movable[k];
            e.from = before[k];
            e.to = after[k];
            edits.push_back(e);
        }
        endCoalesce();
        return commitBoxes(edits, "");
    }

    bool LayoutSession::setBox(const DragRect& r, bool resize)
    {
        if (!editableBox(selected_)) return false;
        DragRect to = r;
        if (to.w < 1) to.w = 1;
        if (to.h < 1) to.h = 1;
        return commitBox(box(selected_), to, resize);
    }

    bool LayoutSession::duplicateSelected()
    {
        const std::vector<int> top = topSelected();
        if (top.empty()) return false;
        if (top.size() == 1) {
            const std::string copy = duplicatePath(doc_.elements[top[0]].path);
            endCoalesce();
            return change(withDuplicate(doc_, top[0]), "", copy);
        }
        // In document order: each copy goes right after its original, before
        // the next original, so an original's offset moves by what was inserted
        // so far and a copy's never moves again
        LayoutDoc work = doc_;
        size_t shift = 0;
        std::vector<size_t> copies;
        std::string error;
        for (int e : top) {
            const size_t at = doc_.elements[e].begin + shift;
            int now = -1;
            for (size_t k = 0; k < work.elements.size(); ++k) {
                if (work.elements[k].begin == at) { now = static_cast<int>(k); break; }
            }
            if (now <= 0) return false;
            const std::string copyPath = duplicatePath(work.elements[now].path);
            LayoutDoc next;
            if (!parseLayoutText(withDuplicate(work, now), next, error)) return false;
            const int copy = next.find(copyPath);
            if (copy <= 0) return false;
            copies.push_back(next.elements[copy].begin);
            shift += next.text.size() - work.text.size();
            work = std::move(next);
        }
        std::vector<std::string> paths;
        for (size_t begin : copies) {
            for (const LayoutElement& el : work.elements) {
                if (el.begin == begin) { paths.push_back(el.path); break; }
            }
        }
        endCoalesce();
        return change(work.text, "", paths);
    }

    bool LayoutSession::deleteSelected()
    {
        const std::vector<int> top = topSelected();
        if (top.empty()) return false;
        endCoalesce();
        if (top.size() == 1) {
            const int el = top[0];
            int parent = doc_.elements[el].parent;
            const std::string parentPath = parent >= 0 ? doc_.elements[parent].path : "";
            if (!change(withoutElement(doc_, el), "", "\x01")) return false;
            // The parent is selected when it was a menu (an item went), else nothing
            if (parent > 0) selectPath(parentPath);
            else select(-1);
            return true;
        }
        // Last to first, so the earlier ones' indices stay
        LayoutDoc work = doc_;
        std::string error;
        for (auto it = top.rbegin(); it != top.rend(); ++it) {
            if (!parseLayoutText(withoutElement(work, *it), work, error)) return false;
        }
        return change(work.text, "", "\x01");
    }

    // ---- adding elements -------------------------------------------------------

    int LayoutSession::newElementLayer() const
    {
        int highest = -1;
        for (const LayoutElement& e : doc_.elements) {
            int layer = 0;
            if (numberValue(e.value("layer"), layer)) highest = std::max(highest, layer);
        }
        return clampLayer(highest + 1);
    }

    std::string LayoutSession::importFolder(const std::string& kind) const
    {
        std::vector<std::string> names;
        if (kind == "image") names = { "images" };
        else if (kind == "video") names = { "video", "videos" };
        // The page's own folder first (a collection's page keeps its art beside it), then the theme's
        std::vector<std::string> candidates;
        const std::string own = pageFolder();
        if (!own.empty()) {
            for (const std::string& n : names) candidates.push_back(own + "/" + n);
        }
        for (const std::string& n : names) candidates.push_back(n);
        std::error_code ec;
        for (const std::string& c : candidates) {
            if (fs::is_directory(fs::path(themeDir()) / fs::path(c), ec)) return c;
        }
        return ASSETS_FOLDER;
    }

    std::string LayoutSession::pageFolder() const
    {
        return fs::path(page_).parent_path().generic_string();
    }

    bool LayoutSession::addElement(const std::string& paletteId, const std::string& type, const std::string& src)
    {
        const PaletteEntry* entry = paletteEntry(paletteId);
        if (!open_ || !entry || doc_.elements.empty()) return false;
        // After the page element the selection is part of (a menu, for its item), else at the end
        int after = -1;
        if (selected_ > 0) {
            int top = selected_;
            while (doc_.elements[top].parent > 0) top = doc_.elements[top].parent;
            if (doc_.elements[top].parent == 0) after = top;
        }
        const int x = (canvasW() - entry->w) / 2;
        const int y = (canvasH() - entry->h) / 2;
        size_t at = std::string::npos;
        const std::string text = withChild(doc_, 0, paletteFragment(*entry, type, src, x, y, newElementLayer()), after, &at);
        LayoutDoc next;
        std::string error;
        if (!parseLayoutText(text, next, error)) return false;
        std::string path;
        for (const LayoutElement& e : next.elements) {
            if (e.begin == at) { path = e.path; break; }
        }
        if (path.empty()) return false;
        endCoalesce();
        return change(text, "", path);
    }

    // ---- animations ------------------------------------------------------------

    std::vector<int> LayoutSession::events(int element) const
    {
        std::vector<int> out;
        if (element < 0 || element >= elementCount()) return out;
        for (int child : doc_.elements[element].children) {
            if (isAnimationEvent(doc_.elements[child].name)) out.push_back(child);
        }
        return out;
    }

    std::vector<int> LayoutSession::childrenNamed(int element, const std::string& name) const
    {
        std::vector<int> out;
        if (element < 0 || element >= elementCount()) return out;
        for (int child : doc_.elements[element].children) {
            if (doc_.elements[child].name == name) out.push_back(child);
        }
        return out;
    }

    std::vector<std::string> LayoutSession::eventNames()
    {
        return layoutVocabulary().events;
    }

    bool LayoutSession::insideSelected(int element) const
    {
        if (selected_ < 0 || element < 0 || element >= elementCount()) return false;
        for (int e = element; e >= 0; e = doc_.elements[e].parent) {
            if (e == selected_) return true;
        }
        return false;
    }

    int LayoutSession::findInSelected(const std::string& relativePath) const
    {
        const LayoutElement* e = selectedElement();
        if (!e) return -1;
        if (relativePath.empty() || relativePath == "-") return selected_;
        return doc_.find(e->path.empty() ? relativePath : e->path + "/" + relativePath);
    }

    bool LayoutSession::addEvent(const std::string& event)
    {
        if (selected_ <= 0 || inAnimation(doc_, selected_) || !isAnimationEvent(event) ||
            !attributeNameProblem(event).empty()) {
            return false;
        }
        endCoalesce();
        return change(withChild(doc_, selected_, eventFragment(event)), "");
    }

    bool LayoutSession::addSet(int event)
    {
        if (!insideSelected(event) || event == selected_ || !isAnimationEvent(doc_.elements[event].name)) return false;
        endCoalesce();
        return change(withChild(doc_, event, setFragment()), "");
    }

    bool LayoutSession::addAnimate(int set)
    {
        if (!insideSelected(set) || doc_.elements[set].name != "set" || !inAnimation(doc_, set)) return false;
        endCoalesce();
        return change(withChild(doc_, set, animateFragment()), "");
    }

    bool LayoutSession::removeAnimation(int element)
    {
        if (!insideSelected(element) || element == selected_ || !inAnimation(doc_, element)) return false;
        endCoalesce();
        return change(withoutElement(doc_, element), "");
    }

    bool LayoutSession::setAttributeOf(int element, const std::string& name, const std::string& value)
    {
        if (!insideSelected(element) || !attributeNameProblem(name).empty()) return false;
        // Inside an animation an empty value is never what's meant (PageBuilder reads
        // from="" and to="" as 0, menuIndex="" as menu 0): the attribute goes instead
        if (value.empty() && inAnimation(doc_, element)) return change(withoutAttribute(doc_, element, name), coalesce_);
        return change(withAttribute(doc_, element, name, value), coalesce_);
    }

    bool LayoutSession::removeAttributeOf(int element, const std::string& name)
    {
        if (!insideSelected(element)) return false;
        endCoalesce();
        return change(withoutAttribute(doc_, element, name), "");
    }

    bool LayoutSession::copyAnimationsFrom(int source)
    {
        if (selected_ <= 0 || source <= 0 || source >= elementCount() || source == selected_ ||
            inAnimation(doc_, selected_) || inAnimation(doc_, source)) {
            return false;
        }
        std::vector<std::string> copies;
        for (int ev : events(source)) copies.push_back(fragmentOf(doc_, ev));
        if (copies.empty()) return false;
        const int target = selected_;
        LayoutDoc work = doc_;
        std::string error;
        // Its own go first, last to first (the target comes before them, so its index stays)
        std::vector<int> own = events(target);
        for (auto it = own.rbegin(); it != own.rend(); ++it) {
            if (!parseLayoutText(withoutElement(work, *it), work, error)) return false;
        }
        for (const std::string& copy : copies) {
            if (!parseLayoutText(withChild(work, target, copy), work, error)) return false;
        }
        endCoalesce();
        return change(work.text, "");
    }

    bool LayoutSession::enterEndBox(int element, DragRect& out) const
    {
        return eventEndBox(element, "onEnter", out);
    }

    std::vector<std::string> LayoutSession::movingEvents(int element) const
    {
        std::vector<std::string> out;
        for (int ev : events(element)) {
            const std::string& name = doc_.elements[ev].name;
            if (std::find(out.begin(), out.end(), name) != out.end()) continue;
            DragRect r;
            if (eventEndBox(element, name, r)) out.push_back(name);
        }
        return out;
    }

    bool LayoutSession::eventEndBox(int element, const std::string& event, DragRect& out) const
    {
        const ElementGeom* found = geometry(element);
        if (!found || found->around || !isAnimationEvent(event)) return false;
        // The event as the engine plays it on the page: one without a menuIndex, else the first
        int enter = -1;
        for (int ev : childrenNamed(element, event)) {
            if (!doc_.elements[ev].has("menuIndex")) { enter = ev; break; }
            if (enter < 0) enter = ev;
        }
        if (enter < 0) return false;
        ElementGeom g = *found;
        bool changed = false;
        const int w = canvasW(), h = canvasH();
        for (int set : childrenNamed(enter, "set")) {
            for (int animate : childrenNamed(set, "animate")) {
                const LayoutElement& a = doc_.elements[animate];
                if (!a.has("to")) continue;
                const std::string type = lower(a.value("type"));
                const std::string to = a.value("to");
                if (type == "x") g.x = tweenValue(to, true, w, h);
                else if (type == "y") g.y = tweenValue(to, false, w, h);
                else if (type == "xoffset") g.xOffset = tweenValue(to, true, w, h);
                else if (type == "yoffset") g.yOffset = tweenValue(to, false, w, h);
                else if (type == "width") g.w = tweenValue(to, true, w, h);
                else if (type == "height") g.h = tweenValue(to, false, w, h);
                else continue;
                changed = true;
            }
        }
        if (!changed) return false;
        DragRect r;
        r.x = g.left();
        r.y = g.top();
        r.w = std::max(1, roundDelta(g.w));
        r.h = std::max(1, roundDelta(g.h));
        const DragRect placed = box(element);
        if (r.x == placed.x && r.y == placed.y && r.w == placed.w && r.h == placed.h) return false;
        out = r;
        return true;
    }

    bool LayoutSession::undo()
    {
        if (undo_.empty()) return false;
        Snapshot s = undo_.back();
        undo_.pop_back();
        redo_.push_back(Snapshot{ doc_.text, selectionPaths(), s.link, s.id });
        std::string error;
        load(s.text, error);
        selectPaths(s.selection);
        drag_ = Drag();
        lastKey_.clear();
        revision_++;
        return true;
    }

    bool LayoutSession::redo()
    {
        if (redo_.empty()) return false;
        Snapshot s = redo_.back();
        redo_.pop_back();
        undo_.push_back(Snapshot{ doc_.text, selectionPaths(), s.link, s.id });
        std::string error;
        load(s.text, error);
        selectPaths(s.selection);
        drag_ = Drag();
        lastKey_.clear();
        revision_++;
        return true;
    }

    // ---- saving --------------------------------------------------------------

    bool LayoutSession::save(std::string& error)
    {
        if (!open_) { error = "Nothing is open."; return false; }
        if (!insideTheme(error)) return false;
        std::string onDisk;
        if (!Library::readFile(file(), onDisk)) {
            error = "layouts/" + folder_ + "/" + page_ + " isn't there any more, so it isn't written.";
            return false;
        }
        if (onDisk != saved_) {
            error = "layouts/" + folder_ + "/" + page_ + " was changed by something else since it was opened. "
                    "Close the editor and open it again (your changes here would overwrite it).";
            return false;
        }
        if (!dirty()) return true;
        // Never write a page RetroFE couldn't read (an edit can't make one, but this is the last gate)
        const std::string unreadable = layoutParseError(doc_.text);
        if (!unreadable.empty()) {
            error = "The page as edited isn't one RetroFE can read (" + unreadable + "), so it isn't written.";
            return false;
        }
        Library::Backups backups(root_);
        if (!backups.beforeChange(file(), error)) return false;
        if (!Library::writeFileAtomic(file(), doc_.text, error)) return false;
        saved_ = doc_.text;
        return true;
    }

    bool LayoutSession::hasOriginal() const
    {
        return open_ && Library::Backups(root_).hasOriginal(file());
    }

    std::string LayoutSession::originalPath() const
    {
        return Library::Backups(root_).backupPath(file());
    }

    bool LayoutSession::restoreOriginal(std::string& error)
    {
        if (!open_) { error = "Nothing is open."; return false; }
        if (!insideTheme(error)) return false;
        Library::Backups backups(root_);
        if (!backups.hasOriginal(file())) {
            error = "No original of this page was kept: it hasn't been changed here.";
            return false;
        }
        // As save(): what another program wrote since the page was read isn't replaced unseen
        std::string onDisk;
        if (Library::readFile(file(), onDisk) && onDisk != saved_) {
            error = "layouts/" + folder_ + "/" + page_ + " was changed by something else since it was opened. "
                    "Close the editor and open it again before putting the original back.";
            return false;
        }
        const Snapshot before{ doc_.text, selectionPaths(), 0, ++nextStep_ };
        if (!backups.restore(file(), error)) return false;
        std::string text;
        if (!Library::readFile(file(), text) || !load(text, error)) {
            if (error.empty()) error = "Can't read the page back.";
            return false;
        }
        // Undoable: Undo brings back what was being edited (unsaved, until saved again)
        if (before.text != text) pushUndo(before);
        saved_ = text;
        redo_.clear();
        lastKey_.clear();
        select(-1);
        geom_.clear();
        hidden_.clear();
        structure_++;
        revision_++;
        return true;
    }
}

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
#include "LayoutTwin.h"
#include "LayoutSession.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ThemeEditor
{
    namespace
    {
        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool isEvent(const std::string& name)
        {
            return name.size() > 2 && name[0] == 'o' && name[1] == 'n' && std::isupper(static_cast<unsigned char>(name[2]));
        }

        // The element's nearest ancestor-or-self that isn't inside an animation event
        int pageElementOf(const LayoutDoc& doc, int i)
        {
            int owner = i;
            for (int e = i; e >= 0; e = doc.elements[e].parent) {
                if (isEvent(doc.elements[e].name)) owner = doc.elements[e].parent;
            }
            return owner;
        }

        // What tells an element apart from its siblings of the same tag
        std::string identity(const LayoutElement& e)
        {
            static const char* const keys[] = { "type", "imageType", "src", "id", "location", "index", "selected", "value" };
            std::string out = e.name;
            for (const char* k : keys) {
                if (const LayoutAttr* a = e.attr(k)) out += std::string("\x1f") + k + "=" + a->value;
            }
            return out;
        }

        // A path without its indices: "menu/item"
        std::string shape(const std::string& path)
        {
            std::string out;
            bool skip = false;
            for (char c : path) {
                if (c == '[') skip = true;
                else if (c == ']') skip = false;
                else if (!skip) out += c;
            }
            return out;
        }

        bool inAnimation(const LayoutDoc& doc, int i)
        {
            return pageElementOf(doc, i) != i;
        }

        // The page elements of a document with this shape and identity, in document order
        std::vector<int> alike(const LayoutDoc& doc, const std::string& shapeOf, const std::string& id)
        {
            std::vector<int> out;
            for (int k = 1; k < static_cast<int>(doc.elements.size()); ++k) {
                if (inAnimation(doc, k)) continue;
                if (shape(doc.elements[k].path) == shapeOf && identity(doc.elements[k]) == id) out.push_back(k);
            }
            return out;
        }

        bool plainNumber(const std::string& text, double& out)
        {
            if (text.empty()) return false;
            char* end = nullptr;
            out = std::strtod(text.c_str(), &end);
            return end && *end == '\0' && std::isfinite(out);
        }

        // A position or size as PageBuilder resolves it (getHorizontalAlignment
        // / getVerticalAlignment); false for anything else
        bool resolve(const std::string& value, bool horizontal, int w, int h, double& out)
        {
            const double size = horizontal ? w : h;
            if (plainNumber(value, out)) return true;
            if (value == (horizontal ? "left" : "top")) { out = 0; return true; }
            if (value == "center") { out = size / 2.0; return true; }
            if (value == (horizontal ? "right" : "bottom") || value == "stretch") { out = size; return true; }
            if (value.size() > 1 && value.back() == '%') {
                double pct = 0;
                if (!plainNumber(value.substr(0, value.size() - 1), pct)) return false;
                out = std::round(size * pct / 100.0);
                return true;
            }
            return false;
        }

        std::string numberText(double v)
        {
            v = std::round(v * 100.0) / 100.0;
            const double whole = std::round(v);
            if (std::fabs(v - whole) < 0.005) return std::to_string(static_cast<long long>(whole));
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.2f", v);
            std::string s = buffer;
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
            return s;
        }

        // 1: horizontal, 2: vertical, 0: not a position or size
        int geometryAxis(const std::string& name)
        {
            if (name == "x" || name == "xOffset" || name == "width" || name == "maxWidth") return 1;
            if (name == "y" || name == "yOffset" || name == "height" || name == "maxHeight") return 2;
            return 0;
        }

        std::string where(const LayoutDoc& doc, int i)
        {
            const LayoutElement& e = doc.elements[i];
            return e.path.empty() ? std::string("the page") : e.name + " (" + e.path + ")";
        }
    }

    std::string twinPage(const std::string& page, const std::vector<std::string>& pages)
    {
        const size_t slash = page.rfind('/');
        const std::string folder = slash == std::string::npos ? std::string() : page.substr(0, slash + 1);
        const std::string name = lower(slash == std::string::npos ? page : page.substr(slash + 1));
        std::string other;
        if (name == "layout.xml") other = "layout 4x3.xml";
        else if (name == "layout 4x3.xml") other = "layout.xml";
        else return "";
        for (const std::string& p : pages) {
            const size_t s = p.rfind('/');
            const std::string pf = s == std::string::npos ? std::string() : p.substr(0, s + 1);
            const std::string pn = lower(s == std::string::npos ? p : p.substr(s + 1));
            if (pf == folder && pn == other) return p;
        }
        return "";
    }

    int matchElement(const LayoutDoc& from, int element, const LayoutDoc& to)
    {
        if (element < 0 || element >= static_cast<int>(from.elements.size()) || to.elements.empty()) return -1;
        if (element == 0) return 0;
        const int owner = pageElementOf(from, element);
        if (owner != element) {
            // Inside an event: the same steps under the same element there
            const int there = matchElement(from, owner, to);
            if (there < 0) return -1;
            const std::string& ownerPath = from.elements[owner].path;
            const std::string rel = from.elements[element].path.substr(ownerPath.empty() ? 0 : ownerPath.size() + 1);
            const std::string& base = to.elements[there].path;
            const int j = to.find(base.empty() ? rel : base + "/" + rel);
            return j >= 0 && to.elements[j].name == from.elements[element].name ? j : -1;
        }
        const LayoutElement& e = from.elements[element];
        const std::string id = identity(e);
        const int same = to.find(e.path);
        if (same > 0 && identity(to.elements[same]) == id) return same;
        // Elsewhere on the page: the only one like it, or the same one of several
        const std::vector<int> here = alike(from, shape(e.path), id);
        const std::vector<int> there = alike(to, shape(e.path), id);
        if (there.size() == 1 && here.size() == 1) return there[0];
        if (!there.empty() && there.size() == here.size()) {
            for (size_t k = 0; k < here.size(); ++k) {
                if (here[k] == element) return there[k];
            }
        }
        return -1;
    }

    bool attributeChanges(const LayoutDoc& before, const LayoutDoc& after, std::vector<AttrChange>& out)
    {
        out.clear();
        if (before.elements.size() != after.elements.size()) return false;
        for (size_t i = 0; i < before.elements.size(); ++i) {
            if (before.elements[i].path != after.elements[i].path || before.elements[i].name != after.elements[i].name) {
                out.clear();
                return false;
            }
        }
        for (size_t i = 0; i < before.elements.size(); ++i) {
            const LayoutElement& a = before.elements[i];
            const LayoutElement& b = after.elements[i];
            // Changed or removed
            for (const LayoutAttr& attr : a.attrs) {
                if (a.attr(attr.name) != &attr) continue;   // only the first of a repeated name counts (rapidxml's)
                const LayoutAttr* now = b.attr(attr.name);
                if (now && now->value == attr.value) continue;
                AttrChange c;
                c.path = a.path;
                c.name = attr.name;
                c.had = true;
                c.before = attr.value;
                c.has = now != nullptr;
                c.after = now ? now->value : std::string();
                out.push_back(c);
            }
            // Added
            for (const LayoutAttr& attr : b.attrs) {
                if (b.attr(attr.name) != &attr || a.has(attr.name)) continue;
                AttrChange c;
                c.path = a.path;
                c.name = attr.name;
                c.has = true;
                c.after = attr.value;
                out.push_back(c);
            }
        }
        return true;
    }

    MirrorResult mirrorChanges(const LayoutDoc& before, const std::vector<AttrChange>& changes, const LayoutDoc& twin,
                               int thisW, int thisH, int twinW, int twinH)
    {
        MirrorResult r;
        r.text = twin.text;
        LayoutDoc work = twin;
        for (const AttrChange& c : changes) {
            const int i = before.find(c.path);
            // Matched on the other page as it was (a copied src mustn't make the element unrecognisable);
            // attribute edits don't move elements, so the index holds in the working copy
            const int j = i < 0 ? -1 : matchElement(before, i, twin);
            if (j < 0) {
                r.skipped.push_back(c.name + " of " + (i < 0 ? c.path : where(before, i)) + ": not on the other page");
                continue;
            }
            const LayoutElement& t = work.elements[j];
            const bool twinHas = t.has(c.name);
            const std::string twinValue = t.value(c.name);
            const int axis = geometryAxis(c.name);
            const bool pageSize = j == 0 && (c.name == "width" || c.name == "height");
            const bool scalable = axis != 0 && j > 0 && !inAnimation(work, j);
            std::string next;
            bool remove = false;
            bool apply = false;
            double number = 0;
            if (pageSize) {
                r.skipped.push_back(c.name + " of the page: each page keeps its own size");
                continue;
            }
            if (scalable && c.has && plainNumber(c.after, number)) {
                // A number for a position or size: the same move there, scaled to that page
                const bool horizontal = axis == 1;
                const int aw = thisW > 0 ? thisW : 1, ah = thisH > 0 ? thisH : 1;
                const int bw = twinW > 0 ? twinW : aw, bh = twinH > 0 ? twinH : ah;
                const double scale = horizontal ? static_cast<double>(bw) / aw : static_cast<double>(bh) / ah;
                double was = 0, there = 0;
                bool known = false;
                if (c.had && twinHas) {
                    known = resolve(c.before, horizontal, aw, ah, was) && resolve(twinValue, horizontal, bw, bh, there);
                }
                else if (!c.had && !twinHas) {
                    known = true;   // set on neither: the new value, in proportion to that page
                }
                if (known) {
                    // Values that were the same stay the same when the pages are the same size that way
                    // A proportion isn't exact anyway: whole units, as hand-made pages write them
                    if (c.had && twinValue == c.before && scale == 1.0) next = c.after;
                    else next = numberText(std::round(there + (number - was) * scale));
                    apply = true;
                }
            }
            if (!apply) {
                // Anything else: only where the other page had what this one had
                const bool same = twinHas == c.had && (!c.had || twinValue == c.before);
                if (!same) {
                    r.skipped.push_back(c.name + " of " + where(work, j) + ": " +
                                        (twinHas ? "\"" + twinValue + "\"" : std::string("not set")) + " there, " +
                                        (c.had ? "\"" + c.before + "\"" : std::string("not set")) + " here");
                    continue;
                }
                apply = true;
                remove = !c.has;
                next = c.after;
            }
            if (!remove && twinHas && twinValue == next) continue;
            if (remove && !twinHas) continue;
            const std::string text = remove ? withoutAttribute(work, j, c.name) : withAttribute(work, j, c.name, next);
            std::string error;
            LayoutDoc parsed;
            if (!parseLayoutText(text, parsed, error)) {
                r.skipped.push_back(c.name + " of " + where(work, j) + ": " + error);
                continue;
            }
            work = std::move(parsed);
            r.applied++;
        }
        r.text = work.text;
        return r;
    }

    int designSize(const LayoutDoc& doc, bool width, int fallback)
    {
        if (doc.elements.empty()) return fallback;
        double v = 0;
        if (plainNumber(doc.elements[0].value(width ? "width" : "height"), v) && v > 0) return static_cast<int>(std::lround(v));
        return fallback;
    }

    void TwinSync::reset(const LayoutSession& here)
    {
        synced = here.text();
        step = here.undoId();
        stepBase = synced;
    }

    TwinCopy syncTwinPages(LayoutSession& here, LayoutSession& there, TwinSync& s, MirrorResult& result)
    {
        result = MirrorResult();
        if (!here.isOpen() || !there.isOpen() || here.text() == s.synced) return TwinCopy::Nothing;
        const unsigned top = here.undoId();
        // Every step that's copied carries a link, given the first time it's seen
        unsigned link = here.undoLink();
        if (link == 0) {
            link = ++s.links;
            here.linkUndo(link);
        }
        const bool sameStep = top != 0 && top == s.step;
        if (!sameStep) s.stepBase = s.synced;
        // The other page as it was before this step's copy (its own linked step), else as it is
        const std::string twinBase = there.undoLink() == link ? there.undoText() : there.text();
        LayoutDoc before, twin;
        std::string error;
        if (!parseLayoutText(s.stepBase, before, error) || !parseLayoutText(twinBase, twin, error)) {
            s.reset(here);
            return TwinCopy::Nothing;
        }
        std::vector<AttrChange> changes;
        if (!attributeChanges(before, here.doc(), changes)) {
            s.reset(here);
            return TwinCopy::Structure;
        }
        result = mirrorChanges(before, changes, twin, designSize(before, true, here.canvasW()), designSize(before, false, here.canvasH()),
                               there.canvasW(), there.canvasH());
        if (result.text != there.text()) there.applyLinked(result.text, link);
        // Still this step: the next copy is worked out from the same start
        const std::string base = s.stepBase;
        s.synced = here.text();
        s.step = top;
        s.stepBase = base;
        return TwinCopy::Copied;
    }
}

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

        // What tells an element apart from its siblings of the same tag. The
        // weak identity leaves out what a page's art and text differ by
        // (src, value): two pages can show different files in the same place.
        std::string identity(const LayoutElement& e, bool weak = false)
        {
            static const char* const keys[] = { "type", "imageType", "src", "id", "location", "index", "selected", "value" };
            std::string out = e.name;
            for (const char* k : keys) {
                if (weak && (std::string(k) == "src" || std::string(k) == "value")) continue;
                if (const LayoutAttr* a = e.attr(k)) out += std::string("\x1f") + k + "=" + a->value;
            }
            return out;
        }

        bool inAnimation(const LayoutDoc& doc, int i)
        {
            return pageElementOf(doc, i) != i;
        }

        // A parent's children with this tag, in document order
        std::vector<int> childrenNamed(const LayoutDoc& doc, int parent, const std::string& name)
        {
            std::vector<int> out;
            for (int c : doc.elements[parent].children) {
                if (doc.elements[c].name == name) out.push_back(c);
            }
            return out;
        }

        int indexIn(const std::vector<int>& list, int value)
        {
            for (size_t k = 0; k < list.size(); ++k) {
                if (list[k] == value) return static_cast<int>(k);
            }
            return -1;
        }

        // The selected="true" one of a menu's items, else -1
        int selectedItem(const LayoutDoc& doc, const std::vector<int>& items)
        {
            for (size_t k = 0; k < items.size(); ++k) {
                if (doc.elements[items[k]].value("selected") == "true") return static_cast<int>(k);
            }
            return -1;
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
        // Its parent first (the page, a menu): what's under it is looked for under its match
        const int parentThere = matchElement(from, e.parent, to);
        if (parentThere < 0) return -1;
        const std::vector<int> here = childrenNamed(from, e.parent, e.name);
        const std::vector<int> there = childrenNamed(to, parentThere, e.name);
        const int k = indexIn(here, element);
        if (k < 0 || there.empty()) return -1;

        // A menu's items: by their index= when they have one (a vertical menu's
        // slots), else by their place from the selected item, so a 9-item wheel
        // and a 7-item one pair up around the middle; one with no counterpart
        // there has no match
        if (e.name == "item" && from.elements[e.parent].name == "menu") {
            if (e.has("index")) {
                for (int t : there) {
                    if (to.elements[t].value("index") == e.value("index")) return t;
                }
                return -1;
            }
            const int selHere = selectedItem(from, here);
            const int selThere = selectedItem(to, there);
            if (selHere >= 0 && selThere >= 0) {
                const int at = selThere + (k - selHere);
                return at >= 0 && at < static_cast<int>(there.size()) ? there[at] : -1;
            }
            return here.size() == there.size() ? there[k] : -1;
        }

        // The same place among as many alike siblings, and what it is agrees
        const std::string id = identity(e);
        const bool sameCount = here.size() == there.size();
        if (sameCount && identity(to.elements[there[k]]) == id) return there[k];
        // Elsewhere under the parent: the only one like it, or the same one of as many
        std::vector<int> hereAlike, thereAlike;
        for (int h : here) {
            if (identity(from.elements[h]) == id) hereAlike.push_back(h);
        }
        for (int t : there) {
            if (identity(to.elements[t]) == id) thereAlike.push_back(t);
        }
        if (!thereAlike.empty() && thereAlike.size() == hereAlike.size()) return thereAlike[indexIn(hereAlike, element)];
        // The same place, showing another file or text (the pages' art differs, or an edit of src wasn't copied)
        if (sameCount && identity(to.elements[there[k]], true) == identity(e, true)) return there[k];
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
                               int thisW, int thisH, int twinW, int twinH, TwinOrigins* origins)
    {
        static const std::string UNSET = "\x01";
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
                // Where it started on both pages: an origin still true on the other page, else as they are
                const std::string key = c.path + "\x1f" + c.name;
                const std::string twinNow = twinHas ? twinValue : UNSET;
                TwinOrigin* origin = nullptr;
                if (origins) {
                    auto it = origins->find(key);
                    if (it != origins->end() && it->second.expect == twinNow) origin = &it->second;
                }
                const std::string hereStart = origin ? origin->here : (c.had ? c.before : UNSET);
                const std::string twinStart = origin ? origin->there : twinNow;
                double was = 0, there = 0;
                bool known = false;
                if (hereStart != UNSET && twinStart != UNSET) {
                    known = resolve(hereStart, horizontal, aw, ah, was) && resolve(twinStart, horizontal, bw, bh, there);
                }
                else if (hereStart == UNSET && twinStart == UNSET) {
                    known = true;   // set on neither: the new value, in proportion to that page
                }
                if (known) {
                    // Values that were the same stay the same when the pages are the same size that way
                    // A proportion isn't exact anyway: whole units, as hand-made pages write them
                    if (hereStart != UNSET && twinStart == hereStart && scale == 1.0) next = c.after;
                    else next = numberText(std::round(there + (number - was) * scale));
                    apply = true;
                    if (origins) {
                        if (!origin) {
                            TwinOrigin& fresh = (*origins)[key];
                            fresh = TwinOrigin{ hereStart, twinStart, twinNow, next };
                            origin = &fresh;
                        }
                        origin->written = next;
                    }
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
        origins.clear();
    }

    void pairEvictions(LayoutSession& a, LayoutSession& b)
    {
        for (int round = 0; round < 2; ++round) {   // dropping on one can't drop more there, but be sure
            for (unsigned link : a.takeEvictedLinks()) b.forgetThrough(link);
            for (unsigned link : b.takeEvictedLinks()) a.forgetThrough(link);
        }
    }

    TwinCopy syncTwinPages(LayoutSession& here, LayoutSession& there, TwinSync& s, MirrorResult& result)
    {
        result = MirrorResult();
        if (!here.isOpen() || !there.isOpen()) return TwinCopy::Nothing;
        pairEvictions(here, there);
        if (here.text() == s.synced) return TwinCopy::Nothing;
        const unsigned top = here.undoId();
        // Every step that's copied carries a link, given the first time it's seen
        unsigned link = here.undoLink();
        if (link == 0) {
            link = ++s.links;
            here.linkUndo(link);
        }
        const bool sameStep = top != 0 && top == s.step;
        if (!sameStep) {
            s.stepBase = s.synced;
            // A new step starts from what the last one wrote there
            for (auto& o : s.origins) o.second.expect = o.second.written;
        }
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
                               there.canvasW(), there.canvasH(), &s.origins);
        if (result.text != there.text()) there.applyLinked(result.text, link);
        pairEvictions(here, there);
        // Still this step: the next copy is worked out from the same start
        const std::string base = s.stepBase;
        s.synced = here.text();
        s.step = top;
        s.stepBase = base;
        return TwinCopy::Copied;
    }
}

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
#include <cfloat>
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

        // An attribute as PageBuilder finds it (findAttribute): the element's
        // own, else, for a menu's item, its menu's <itemDefaults>
        const LayoutAttr* effectiveAttr(const LayoutDoc& doc, int i, const char* name)
        {
            const LayoutElement& e = doc.elements[i];
            if (const LayoutAttr* a = e.attr(name)) return a;
            if (e.name == "item" && e.parent > 0 && doc.elements[e.parent].name == "menu") {
                for (int c : doc.elements[e.parent].children) {
                    if (doc.elements[c].name != "itemDefaults") continue;
                    if (const LayoutAttr* a = doc.elements[c].attr(name)) return a;
                }
            }
            return nullptr;
        }

        // PageBuilder::getHorizontalAlignment / getVerticalAlignment
        float engineValue(const LayoutAttr* a, bool horizontal, int w, int h, float ifNull)
        {
            if (!a || a->value.empty()) return ifNull;
            const std::string& v = a->value;
            const float size = static_cast<float>(horizontal ? w : h);
            if (v == (horizontal ? "left" : "top")) return 0.0f;
            if (v == "center") return size / 2.0f;
            if (v == (horizontal ? "right" : "bottom") || v == "stretch") return size;
            if (v.back() == '%') return std::round(size * static_cast<float>(std::atof(v.substr(0, v.size() - 1).c_str())) / 100.0f);
            return static_cast<float>(std::atof(v.c_str()));
        }

        // The size settings ViewInfo scales by, as buildViewInfo reads them
        struct SizeSettings
        {
            float width = -1, height = -1;
            float minW = 0, minH = 0;
            float maxW = FLT_MAX, maxH = FLT_MAX;
            float font = -1;
            bool operator==(const SizeSettings& o) const
            {
                return width == o.width && height == o.height && minW == o.minW && minH == o.minH && maxW == o.maxW &&
                       maxH == o.maxH && font == o.font;
            }
        };

        SizeSettings sizeSettings(const LayoutDoc& doc, int i, int w, int h)
        {
            SizeSettings s;
            const LayoutAttr* width = effectiveAttr(doc, i, "width");
            const LayoutAttr* height = effectiveAttr(doc, i, "height");
            if (width || height) {
                s.height = engineValue(height, false, w, h, -1);
                s.width = engineValue(width, true, w, h, -1);
            }
            s.minH = engineValue(effectiveAttr(doc, i, "minHeight"), false, w, h, 0);
            s.minW = engineValue(effectiveAttr(doc, i, "minWidth"), true, w, h, 0);
            s.maxH = engineValue(effectiveAttr(doc, i, "maxHeight"), false, w, h, FLT_MAX);
            s.maxW = engineValue(effectiveAttr(doc, i, "maxWidth"), false, w, h, FLT_MAX);   // PageBuilder reads it vertically
            s.font = engineValue(effectiveAttr(doc, i, "fontSize"), false, w, h, -1);
            return s;
        }

        // ViewInfo::ScaledWidth and ScaledHeight from the absolute size, as
        // written there (each works its own dimension, the other as it was)
        float scaledWidth(float width, float height, const SizeSettings& s)
        {
            if (height < s.minH || width < s.minW) {
                const float scaleH = s.minH / height, scaleW = s.minW / width;
                if (height >= s.minH && width < s.minW) width = s.minW;
                else if (height < s.minH && width >= s.minW) width = scaleH * width;
                else width = scaleH > scaleW ? s.minW : width * scaleH;
            }
            if (width > s.maxW || height > s.maxH) {
                const float scaleH = s.maxH / height, scaleW = s.maxW / width;
                if (height <= s.maxH && width > s.maxW) width = s.maxW;
                else if (height > s.maxH && width <= s.maxW) width = scaleH * width;
                else width = scaleH > scaleW ? s.maxW : width * scaleH;
            }
            return width;
        }

        float scaledHeight(float width, float height, const SizeSettings& s)
        {
            if (height < s.minH || width < s.minW) {
                const float scaleH = s.minH / height, scaleW = s.minW / width;
                if (width >= s.minW && height < s.minH) height = s.minH;
                else if (width < s.minW && height >= s.minH) height = scaleW * height;
                else height = scaleH > scaleW ? s.minH : height * scaleW;
            }
            if (width > s.maxW || height > s.maxH) {
                const float scaleH = s.maxH / height, scaleW = s.maxW / width;
                if (width <= s.maxW && height > s.maxH) height = s.maxH;
                else if (width > s.maxW && height <= s.maxH) height = scaleW * height;
                else height = scaleH < scaleW ? s.maxH : height * scaleW;
            }
            return height;
        }

        bool closeTo(float a, float b) { return std::fabs(a - b) < 0.5f; }
    }

    bool twinGeometry(const LayoutDoc& here, int i, const ElementGeom& g, int hereW, int hereH,
                      const LayoutDoc& there, int j, int thereW, int thereH, ElementGeom& out, bool sameArt)
    {
        if (i <= 0 || j <= 0 || i >= static_cast<int>(here.elements.size()) || j >= static_cast<int>(there.elements.size()) || g.around) {
            return false;
        }
        if (hereW <= 0 || hereH <= 0 || thereW <= 0 || thereH <= 0) return false;
        out = g;
        // Where it is: that page's own attributes, as PageBuilder reads them
        out.x = engineValue(effectiveAttr(there, j, "x"), true, thereW, thereH, 0);
        out.y = engineValue(effectiveAttr(there, j, "y"), false, thereW, thereH, 0);
        out.xOffset = engineValue(effectiveAttr(there, j, "xOffset"), true, thereW, thereH, 0);
        out.yOffset = engineValue(effectiveAttr(there, j, "yOffset"), false, thereW, thereH, 0);
        out.xOrigin = engineValue(effectiveAttr(there, j, "xOrigin"), true, thereW, thereH, 0) / static_cast<float>(thereW);
        out.yOrigin = engineValue(effectiveAttr(there, j, "yOrigin"), false, thereW, thereH, 0) / static_cast<float>(thereH);

        // How big: its size here, with what's set differently there
        const SizeSettings a = sizeSettings(here, i, hereW, hereH);
        const SizeSettings b = sizeSettings(there, j, thereW, thereH);
        if (g.approximate) {
            // A guess here (text without a width, art not loaded): the same guess there
            TracedView v;
            v.path = there.elements[j].path;
            v.width = b.width;
            v.height = b.height;
            v.maxWidth = b.maxW < FLT_MAX ? b.maxW : -1.0f;
            v.maxHeight = b.maxH < FLT_MAX ? b.maxH : -1.0f;
            v.fontSize = b.font;
            const ElementGeom guess = geometryOf(there, v, g.order);
            out.w = guess.w;
            out.h = guess.h;
            out.approximate = true;
            return true;
        }
        // Showing another file there (a weak match): its size from the art is a guess
        if (!sameArt && (b.width < 0 || b.height < 0)) out.approximate = true;
        if (a == b) return true;
        // Its own (unscaled) size, from here: known when nothing set or clamped it
        const bool setHere = a.width >= 0 || a.height >= 0;
        const bool clampedHere = (a.maxW < FLT_MAX && closeTo(g.w, a.maxW)) || (a.maxH < FLT_MAX && closeTo(g.h, a.maxH)) ||
                                 (a.minW > 0 && closeTo(g.w, a.minW)) || (a.minH > 0 && closeTo(g.h, a.minH));
        // Its shape is known unless both width and height were set here
        const bool shapeKnown = !(a.width >= 0 && a.height >= 0) && g.h > 0;
        const float aspect = shapeKnown ? g.w / g.h : 1.0f;
        float natW = 0, natH = 0;
        bool natKnown = false;
        if (!setHere && !clampedHere) {
            natW = g.w;
            natH = g.h;
            // Text: its size follows the font size
            if (a.font > 0 && b.font > 0 && a.font != b.font) {
                natW *= b.font / a.font;
                natH *= b.font / a.font;
            }
            natKnown = true;
        }
        float absW = 0, absH = 0;
        bool known = true;
        if (b.width < 0 && b.height < 0) {
            known = natKnown;
            absW = natW;
            absH = natH;
        }
        else if (b.height < 0) {
            known = shapeKnown;
            absW = b.width;
            absH = b.width / aspect;
        }
        else if (b.width < 0) {
            known = shapeKnown;
            absW = b.height * aspect;
            absH = b.height;
        }
        else {
            absW = b.width;
            absH = b.height;
        }
        if (!known || absW <= 0 || absH <= 0) {
            out.w = g.w * static_cast<float>(thereW) / static_cast<float>(hereW);
            out.h = g.h * static_cast<float>(thereH) / static_cast<float>(hereH);
            out.approximate = true;
            return true;
        }
        out.w = scaledWidth(absW, absH, b);
        out.h = scaledHeight(absW, absH, b);
        return true;
    }

    TwinOpResult runOnBoth(LayoutSession& here, LayoutSession& there, TwinSync& s, const TwinOp& op)
    {
        TwinOpResult r;
        if (!op.run) return r;
        const std::function<int(int)> self = [](int i) { return i; };
        if (!here.isOpen() || !there.isOpen()) {
            r.here = op.run(here, self);
            return r;
        }
        // What was done before is copied first, as its own step
        MirrorResult pending;
        syncTwinPages(here, there, s, pending);

        // What it acts on, and the same elements there
        std::vector<int> acting = op.movable ? here.movableSelection() : here.selection();
        if (op.primaryOnly) acting = here.selected() >= 0 ? std::vector<int>{ here.selected() } : std::vector<int>();
        std::vector<int> mapped;
        std::map<int, int> map;
        std::vector<std::pair<std::string, ElementGeom>> boxes;
        auto note = [&here](std::vector<std::string>& list, int i) {
            const std::string label = here.label(i);
            if (std::find(list.begin(), list.end(), label) == list.end()) list.push_back(label);
        };
        // A match there, or -1 (noted as missing, or as not surely the same for a destructive one)
        auto find = [&](int i, MatchKind& kind) {
            const int j = matchElement(here.doc(), i, there.doc(), &kind);
            if (j < 0) { note(r.missing, i); return -1; }
            if (op.destructive && kind != MatchKind::Exact) { note(r.unsure, i); return -1; }
            return j;
        };
        for (int i : acting) {
            MatchKind kind = MatchKind::None;
            const int j = find(i, kind);
            if (j < 0) continue;
            if (op.boxes) {
                const ElementGeom* g = here.geometry(i);
                ElementGeom tg;
                if (!g || !twinGeometry(here.doc(), i, *g, here.canvasW(), here.canvasH(), there.doc(), j, there.canvasW(),
                                        there.canvasH(), tg, kind != MatchKind::Weak)) {
                    note(r.missing, i);
                    continue;
                }
                boxes.emplace_back(there.element(j).path, tg);
            }
            mapped.push_back(j);
            map[i] = j;
        }
        for (int i : op.touches) {
            if (i < 0 || i >= here.elementCount()) continue;
            MatchKind kind = MatchKind::None;
            const int j = find(i, kind);
            if (j >= 0) map[i] = j;
        }

        r.here = op.run(here, self);
        if (!r.missing.empty() || !r.unsure.empty() || (!r.here && !op.evenIfUnchanged)) return r;

        // The same there, on its own elements
        std::vector<std::string> keep;
        for (int k : there.selection()) keep.push_back(there.element(k).path);
        for (const auto& b : boxes) there.setGeometry(b.first, b.second);
        there.endCoalesce();
        there.setSelection(mapped);
        const std::function<int(int)> toThere = [&map](int i) {
            auto it = map.find(i);
            return it == map.end() ? -1 : it->second;
        };
        const unsigned structure = there.structure();
        r.there = op.run(there, toThere);
        r.failed = !r.there;
        // The boxes were only for this; the page isn't shown
        if (op.boxes) there.clearGeometry();
        there.endCoalesce();
        // Its own selection back, by path; after elements came or went the paths
        // may name other elements, so then none (showing that page selects the
        // matches of this page's selection anyway)
        std::vector<int> back;
        if (there.structure() == structure) {
            for (const std::string& p : keep) {
                const int k = there.doc().find(p);
                if (k >= 0) back.push_back(k);
            }
        }
        there.setSelection(back);
        if (r.here && r.there) {
            unsigned link = here.undoLink();
            if (link == 0) {
                link = ++s.links;
                here.linkUndo(link);
            }
            there.linkUndo(link);
            r.linked = true;
        }
        pairEvictions(here, there);
        s.reset(here);
        return r;
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

    namespace
    {
        // What tells an event, a step or a change apart from its siblings: a
        // step by its duration, a change by what it animates (its from / to
        // are positions, which the pages tune each on their own)
        std::string animationIdentity(const LayoutElement& e)
        {
            if (e.name == "set") return e.name + "\x1f" + e.value("duration");
            if (e.name == "animate") return e.name + "\x1f" + e.value("type");
            return e.name;
        }

        // The weaker of two
        MatchKind weaker(MatchKind a, MatchKind b) { return static_cast<int>(a) > static_cast<int>(b) ? a : b; }

        int matchIn(const LayoutDoc& from, int element, const LayoutDoc& to, MatchKind& kind)
        {
            kind = MatchKind::None;
            if (element < 0 || element >= static_cast<int>(from.elements.size()) || to.elements.empty()) return -1;
            if (element == 0) { kind = MatchKind::Exact; return 0; }
            const LayoutElement& e = from.elements[element];
            const int owner = pageElementOf(from, element);
            if (owner != element) {
                // Inside an event: under its parent's match, the one in the same place
                // among as many of its tag, and what it is agrees (an event's name, a
                // step's duration, a change's type), so a different step list there
                // isn't taken for this one
                MatchKind parentKind = MatchKind::None;
                const int parentThere = matchIn(from, e.parent, to, parentKind);
                if (parentThere < 0) return -1;
                const std::vector<int> here = childrenNamed(from, e.parent, e.name);
                const std::vector<int> there = childrenNamed(to, parentThere, e.name);
                const int k = indexIn(here, element);
                if (k < 0 || here.size() != there.size()) return -1;
                if (animationIdentity(to.elements[there[k]]) != animationIdentity(e)) return -1;
                kind = parentKind;
                return there[k];
            }
            // Its parent first (the page, a menu): what's under it is looked for under its match
            MatchKind parentKind = MatchKind::None;
            const int parentThere = matchIn(from, e.parent, to, parentKind);
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
                        if (to.elements[t].value("index") == e.value("index")) { kind = parentKind; return t; }
                    }
                    return -1;
                }
                int at = -1;
                const int selHere = selectedItem(from, here);
                const int selThere = selectedItem(to, there);
                if (selHere >= 0 && selThere >= 0) at = selThere + (k - selHere);
                else if (here.size() == there.size()) at = k;
                if (at < 0 || at >= static_cast<int>(there.size())) return -1;
                kind = weaker(parentKind, MatchKind::Slot);
                return there[at];
            }

            // The same place among as many alike siblings, and what it is agrees
            const std::string id = identity(e);
            const bool sameCount = here.size() == there.size();
            kind = parentKind;
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
            if (sameCount && identity(to.elements[there[k]], true) == identity(e, true)) {
                kind = weaker(parentKind, MatchKind::Weak);
                return there[k];
            }
            kind = MatchKind::None;
            return -1;
        }
    }

    int matchElement(const LayoutDoc& from, int element, const LayoutDoc& to, MatchKind* kind)
    {
        MatchKind k = MatchKind::None;
        const int j = matchIn(from, element, to, k);
        if (kind) *kind = j < 0 ? MatchKind::None : k;
        return j;
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

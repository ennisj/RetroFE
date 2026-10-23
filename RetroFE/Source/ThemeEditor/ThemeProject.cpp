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

#include "ThemeProject.h"
#include "WidgetDefs.h"
#include <rapidxml.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace ThemeEditor
{
    // --- WidgetInstance -------------------------------------------------------

    void WidgetInstance::warpFromRect()
    {
        wx[0] = x;     wy[0] = y;
        wx[1] = x + w; wy[1] = y;
        wx[2] = x;     wy[2] = y + h;
        wx[3] = x + w; wy[3] = y + h;
    }

    void WidgetInstance::warpBounds(int& bx, int& by, int& bw, int& bh) const
    {
        int x0 = wx[0], x1 = wx[0], y0 = wy[0], y1 = wy[0];
        for (int i = 1; i < 4; ++i) {
            if (wx[i] < x0) x0 = wx[i];
            if (wx[i] > x1) x1 = wx[i];
            if (wy[i] < y0) y0 = wy[i];
            if (wy[i] > y1) y1 = wy[i];
        }
        bx = x0; by = y0; bw = x1 - x0; bh = y1 - y0;
    }

    std::string WidgetInstance::varValue(const std::string& token, const std::string& fallback) const
    {
        for (const auto& v : vars) {
            if (v.first == token) return v.second;
        }
        return fallback;
    }

    void WidgetInstance::setVar(const std::string& token, const std::string& value)
    {
        for (auto& v : vars) {
            if (v.first == token) { v.second = value; return; }
        }
        vars.emplace_back(token, value);
    }

    void WidgetInstance::clearVar(const std::string& token)
    {
        for (size_t i = 0; i < vars.size(); ++i) {
            if (vars[i].first == token) { vars.erase(vars.begin() + static_cast<long>(i)); return; }
        }
    }

    // --- pages ------------------------------------------------------------------

    bool ThemePage::covers(const std::string& c) const
    {
        if (collection == c) return true;
        for (const auto& m : members) {
            if (m == c) return true;
        }
        return false;
    }

    std::string ThemePage::displayName() const
    {
        if (!tplName.empty()) return tplName;
        return collection.empty() ? std::string("Default") : collection;
    }

    const WidgetInstance* ThemePage::findWidget(const std::string& id) const
    {
        for (const auto& w : widgets) {
            if (w.id == id) return &w;
        }
        return nullptr;
    }

    ThemePage* ThemeProject::page(const std::string& collection)
    {
        for (auto& p : pages) {
            if (p.collection == collection) return &p;
        }
        return nullptr;
    }

    const ThemePage* ThemeProject::page(const std::string& collection) const
    {
        for (const auto& p : pages) {
            if (p.collection == collection) return &p;
        }
        return nullptr;
    }

    ThemePage* ThemeProject::coveredBy(const std::string& collection)
    {
        if (collection.empty()) return &pages[0];
        for (auto& p : pages) {
            if (p.covers(collection)) return &p;
        }
        return nullptr;
    }

    const ThemePage* ThemeProject::coveredBy(const std::string& collection) const
    {
        if (collection.empty()) return &pages[0];
        for (const auto& p : pages) {
            if (p.covers(collection)) return &p;
        }
        return nullptr;
    }

    // --- templates --------------------------------------------------------------

    bool attachCollection(ThemeProject& p, const std::string& tplPrimary, const std::string& collection)
    {
        if (collection.empty() || ThemeProject::isSplashPage(collection)) return false;
        ThemePage* t = p.page(tplPrimary);
        if (!t || t->collection.empty()) return false;   // never the Default page
        if (p.page(collection)) return false;            // it has its own page
        detachCollection(p, collection);
        t = p.page(tplPrimary);
        if (t && !t->covers(collection)) t->members.push_back(collection);
        return true;
    }

    bool detachCollection(ThemeProject& p, const std::string& collection)
    {
        for (auto& pg : p.pages) {
            for (size_t i = 0; i < pg.members.size(); ++i) {
                if (pg.members[i] == collection) {
                    pg.members.erase(pg.members.begin() + static_cast<long>(i));
                    return true;
                }
            }
        }
        return false;
    }

    // Unhooks a collection from whatever template it uses now: drops a
    // membership, or when the collection is a template's primary, moves the
    // primary to another member (lowest name) or removes a template left with
    // no collections. `erased` reports a removed template.
    static void unhook(ThemeProject& p, const std::string& collection, std::string* erased)
    {
        if (erased) erased->clear();
        if (detachCollection(p, collection)) return;
        for (size_t k = 1; k < p.pages.size(); ++k) {
            if (p.pages[k].collection != collection) continue;
            std::vector<std::string>& mm = p.pages[k].members;
            if (!mm.empty()) {
                size_t lo = 0;
                for (size_t j = 1; j < mm.size(); ++j) {
                    if (mm[j] < mm[lo]) lo = j;
                }
                p.pages[k].collection = mm[lo];
                mm.erase(mm.begin() + static_cast<long>(lo));
            }
            else {
                if (erased) *erased = p.pages[k].collection;
                p.pages.erase(p.pages.begin() + static_cast<long>(k));
            }
            return;
        }
    }

    std::string reassign(ThemeProject& p, const std::string& collection, const std::string& targetPrimary)
    {
        if (collection.empty() || ThemeProject::isSplashPage(collection)) return "";
        const ThemePage* cur = p.coveredBy(collection);
        const std::string curPrimary = (cur && !cur->collection.empty()) ? cur->collection : std::string();
        if (curPrimary == targetPrimary) return "";
        std::string erased;
        unhook(p, collection, &erased);
        if (!targetPrimary.empty()) {
            ThemePage* t = p.page(targetPrimary);
            if (t && !t->collection.empty() && !t->covers(collection)) t->members.push_back(collection);
        }
        return erased;
    }

    const ThemePage* effectiveCover(const ThemeProject& p, const std::string& collection,
                                    const std::function<std::string(const std::string&)>& parentOf,
                                    bool* inherited)
    {
        if (inherited) *inherited = false;
        if (collection.empty() || ThemeProject::isSplashPage(collection)) return nullptr;
        std::string cur = collection;
        // Depth-capped: menu data can contain cycles
        for (int guard = 0; guard < 16 && !cur.empty(); ++guard) {
            const ThemePage* cb = p.coveredBy(cur);
            if (cb && !cb->collection.empty()) {
                if (inherited) *inherited = cur != collection;
                return cb;
            }
            if (!parentOf) break;
            cur = parentOf(cur);
        }
        return nullptr;
    }

    ThemePage* createNamed(ThemeProject& p, const std::string& collection, const std::string& name,
                           const std::string& seedPrimary, std::string* erased)
    {
        if (collection.empty() || ThemeProject::isSplashPage(collection)) return nullptr;
        const ThemePage* seed = nullptr;
        if (seedPrimary == "\x01") seed = nullptr;
        else if (seedPrimary.empty()) seed = &p.pages[0];
        else seed = p.page(seedPrimary);
        // Copy the seed before unhooking: unhooking can erase a page, which
        // would leave `seed` pointing at a moved or destroyed element
        ThemePage page;
        if (seed) page.widgets = seed->widgets;
        unhook(p, collection, erased);
        page.collection = collection;
        page.tplName = name.empty() ? collection : name;
        p.pages.push_back(page);
        return &p.pages.back();
    }

    bool deleteTemplateTo(ThemeProject& p, const std::string& primary, const std::string& replacementPrimary)
    {
        if (primary.empty() || primary == replacementPrimary) return false;
        const ThemePage* t = p.page(primary);
        if (!t || t->collection.empty()) return false;
        std::vector<std::string> colls;
        colls.push_back(t->collection);
        for (const auto& m : t->members) colls.push_back(m);
        for (size_t k = 1; k < p.pages.size(); ++k) {
            if (p.pages[k].collection == primary) {
                p.pages.erase(p.pages.begin() + static_cast<long>(k));
                break;
            }
        }
        if (!replacementPrimary.empty()) {
            ThemePage* r = p.page(replacementPrimary);
            if (r && !r->collection.empty()) {
                for (const auto& c : colls) {
                    if (!r->covers(c)) r->members.push_back(c);
                }
            }
        }
        return true;
    }

    ThemePage* createTemplate(ThemeProject& p, const std::string& collection)
    {
        return createTemplate(p, collection, &p.pages[0]);
    }

    ThemePage* createTemplate(ThemeProject& p, const std::string& collection, const ThemePage* seed)
    {
        if (collection.empty()) return &p.pages[0];
        if (ThemePage* have = p.page(collection)) return have;
        ThemePage page;
        if (seed) page = *seed;
        page.collection = collection;
        page.tplName = collection;
        page.members.clear();
        p.pages.push_back(page);
        return &p.pages.back();
    }

    bool deleteTemplate(ThemeProject& p, const std::string& collection)
    {
        if (collection.empty()) return false;
        for (size_t i = 1; i < p.pages.size(); ++i) {
            if (p.pages[i].collection == collection) {
                p.pages.erase(p.pages.begin() + static_cast<long>(i));
                return true;
            }
        }
        return false;
    }

    // --- small helpers ------------------------------------------------------------

    std::string xmlEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
            }
        }
        return out;
    }

    std::string editorSlug(const std::string& name)
    {
        std::string out;
        bool pendingDash = false;
        for (char c : name) {
            char l = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) {
                if (pendingDash && !out.empty()) out += '-';
                pendingDash = false;
                out += l;
            }
            else {
                pendingDash = true;
            }
        }
        if (out.empty()) out = "theme";
        return out;
    }

    // CON, PRN, AUX, NUL, COM1-9, LPT1-9 are devices on Windows, with or without an extension
    static bool isDeviceName(const std::string& name)
    {
        std::string base;
        for (char c : name) base += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const size_t dot = base.find('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        while (!base.empty() && base.back() == ' ') base.pop_back();
        return base == "con" || base == "prn" || base == "aux" || base == "nul" ||
               (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) &&
                base[3] >= '1' && base[3] <= '9');
    }

    std::string themeFolderName(const std::string& themeName)
    {
        std::string out;
        bool pendingSpace = false;
        for (char c : themeName) {
            const bool bad = static_cast<unsigned char>(c) < 0x20 || c == '\\' || c == '/' || c == ':' ||
                             c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
            if (bad || c == ' ' || c == '\t') { pendingSpace = true; continue; }
            if (pendingSpace && !out.empty()) out += ' ';
            pendingSpace = false;
            out += c;
        }
        while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
        while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());   // no "..", no hidden folders
        if (out.empty()) out = "Theme";
        // Windows reads everything before the first dot, so "COM1.txt" becomes "COM1 Theme.txt"
        if (isDeviceName(out)) out.insert(std::min(out.find('.'), out.size()), " Theme");
        return out;
    }

    bool folderNameSafe(const std::string& name)
    {
        if (name.empty() || name == "." || name == "..") return false;
        for (char c : name) {
            if (static_cast<unsigned char>(c) < 0x20 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|') {
                return false;
            }
        }
        if (name.front() == ' ' || name.front() == '.' || name.back() == ' ' || name.back() == '.') return false;
        return !isDeviceName(name);
    }

    void nextIdentity(const ThemePage& page, const std::string& defId, const std::string& defName,
                      std::string& outId, std::string& outName)
    {
        long maxN = 0;
        const std::string prefix = defId + "_";
        for (const auto& w : page.widgets) {
            if (w.id.size() <= prefix.size() || w.id.compare(0, prefix.size(), prefix) != 0) continue;
            const std::string tail = w.id.substr(prefix.size());
            bool digits = !tail.empty() && tail.size() < 9;
            for (char c : tail) {
                if (c < '0' || c > '9') { digits = false; break; }
            }
            if (!digits) continue;
            long n = std::strtol(tail.c_str(), nullptr, 10);
            if (n > maxN) maxN = n;
        }
        const long n = maxN + 1;
        outId = prefix + std::to_string(n);
        outName = defName + " " + std::to_string(n);
    }

    void reflowOrientation(ThemeProject& p, bool vertical)
    {
        if (p.vertical == vertical) return;
        const int oldW = p.canvasW(), oldH = p.canvasH();
        p.vertical = vertical;
        const int newW = p.canvasW(), newH = p.canvasH();
        auto scaleX = [&](int v) { return static_cast<int>((static_cast<long long>(v) * newW + oldW / 2) / oldW); };
        auto scaleY = [&](int v) { return static_cast<int>((static_cast<long long>(v) * newH + oldH / 2) / oldH); };
        for (auto& page : p.pages) {
            for (auto& w : page.widgets) {
                w.x = scaleX(w.x);
                w.w = scaleX(w.w);
                w.y = scaleY(w.y);
                w.h = scaleY(w.h);
                if (w.w < 1) w.w = 1;
                if (w.h < 1) w.h = 1;
            }
        }
    }

    // --- serialization ------------------------------------------------------------

    static void attr(std::string& out, const char* name, const std::string& value)
    {
        out += ' ';
        out += name;
        out += "=\"";
        out += xmlEscape(value);
        out += '"';
    }

    static void attr(std::string& out, const char* name, int value)
    {
        out += ' ';
        out += name;
        out += "=\"";
        out += std::to_string(value);
        out += '"';
    }

    static void emitPage(std::string& out, const ThemePage& page, bool asMon)
    {
        out += "  <page";
        attr(out, "collection", page.collection);
        if (asMon) {
            out += " mon=\"1\"";
            if (page.monW || page.monH) {
                attr(out, "monW", page.monW);
                attr(out, "monH", page.monH);
            }
        }
        else {
            if (!page.tplName.empty() && page.tplName != page.collection) attr(out, "tpl", page.tplName);
            if (!page.members.empty()) {
                std::string joined;
                for (size_t i = 0; i < page.members.size(); ++i) {
                    if (i) joined += '|';
                    joined += page.members[i];
                }
                attr(out, "also", joined);
            }
        }
        out += ">\n";
        for (const auto& w : page.widgets) {
            out += "    <widget";
            attr(out, "def", w.def);
            attr(out, "id", w.id);
            attr(out, "name", w.name);
            attr(out, "x", w.x);
            attr(out, "y", w.y);
            attr(out, "w", w.w);
            attr(out, "h", w.h);
            attr(out, "layer", w.layer);
            if (!w.autoScroll) out += " autoscroll=\"0\"";
            if (w.canvasHide) out += " hide=\"1\"";
            if (w.alphaPct != 100) attr(out, "alpha", w.alphaPct);
            if (w.menuSlots > 0) attr(out, "slots", w.menuSlots);
            if (w.menuFadePct > 0) attr(out, "fade", w.menuFadePct);
            if (w.menuCurvePx > 0) attr(out, "curve", w.menuCurvePx);
            if (w.menuAngleDeg > 0) attr(out, "tilt", w.menuAngleDeg);
            if (w.menuPerspDeg > 0) attr(out, "persp", w.menuPerspDeg);
            if (w.menuItemH > 0) attr(out, "itemh", w.menuItemH);
            if (w.menuPulse) out += " pulse=\"1\"";
            if (w.menuIdleDimPct >= 0) attr(out, "idledim", w.menuIdleDimPct);
            if (w.menuGhost) out += " ghost=\"1\"";
            if (w.menuGhostDx >= 0) attr(out, "gdx", w.menuGhostDx);
            if (w.menuGhostSpanPct >= 0) attr(out, "gspan", w.menuGhostSpanPct);
            if (w.menuGhostItemPct >= 0) attr(out, "gitem", w.menuGhostItemPct);
            if (w.menuGhostAlphaPct >= 0) attr(out, "galpha", w.menuGhostAlphaPct);
            if (!w.menuGhostMode.empty()) attr(out, "gmode", w.menuGhostMode);
            if (w.menuScrollMs > 0) attr(out, "scrollms", w.menuScrollMs);
            if (w.menuAccelMs > 0) attr(out, "accelms", w.menuAccelMs);
            if (w.stripPitchPx > 0) attr(out, "spitch", w.stripPitchPx);
            if (w.stripItemMaxW > 0) attr(out, "sitemw", w.stripItemMaxW);
            if (w.stripDimPct >= 0) attr(out, "sdim", w.stripDimPct);
            if (w.stripSelW > 0) attr(out, "sselw", w.stripSelW);
            if (w.stripSelH > 0) attr(out, "sselh", w.stripSelH);
            if (!w.stripItemDefsRaw.empty()) attr(out, "sitemraw", w.stripItemDefsRaw);
            if (!w.stripItemsRaw.empty()) attr(out, "sladraw", w.stripItemsRaw);
            if (!w.stripItemKidsRaw.empty()) attr(out, "sladkids", w.stripItemKidsRaw);
            if (!w.font.empty()) attr(out, "font", w.font);
            if (!w.fontColor.empty()) attr(out, "fontcolor", w.fontColor);
            if (w.warp) {
                out += " warp=\"1\"";
                static const char* corners[4] = { "wtl", "wtr", "wbl", "wbr" };
                for (int c = 0; c < 4; ++c) {
                    attr(out, corners[c], std::to_string(w.wx[c]) + "," + std::to_string(w.wy[c]));
                }
                if (w.www > 0 && w.wwh > 0) {
                    attr(out, "wwin", std::to_string(w.wwx) + "," + std::to_string(w.wwy) + "," +
                                      std::to_string(w.www) + "," + std::to_string(w.wwh));
                }
            }
            for (const auto& e : w.extra) attr(out, e.first.c_str(), e.second);
            if (w.vars.empty()) {
                out += "/>\n";
            }
            else {
                out += ">\n";
                for (const auto& v : w.vars) {
                    out += "      <var";
                    attr(out, "token", v.first);
                    attr(out, "value", v.second);
                    out += "/>\n";
                }
                out += "    </widget>\n";
            }
        }
        if (!asMon) {
            for (const auto& m : page.mons) emitPage(out, m, true);
        }
        out += "  </page>\n";
    }

    std::string projectSerialize(const ThemeProject& p)
    {
        std::string out = "<themeProject";
        attr(out, "version", ThemeProject::VERSION);
        attr(out, "name", p.name);
        attr(out, "orientation", std::string(p.vertical ? "vertical" : "horizontal"));
        if (p.authorW || p.authorH) {
            attr(out, "authorW", p.authorW);
            attr(out, "authorH", p.authorH);
        }
        if (!p.font.empty()) attr(out, "font", p.font);
        if (!p.soundLoad.empty()) attr(out, "soundLoad", p.soundLoad);
        if (!p.soundUnload.empty()) attr(out, "soundUnload", p.soundUnload);
        if (!p.soundHighlight.empty()) attr(out, "soundHighlight", p.soundHighlight);
        if (!p.soundSelect.empty()) attr(out, "soundSelect", p.soundSelect);
        for (const auto& e : p.extra) attr(out, e.first.c_str(), e.second);
        out += ">\n";
        for (const auto& page : p.pages) emitPage(out, page, false);
        out += "</themeProject>\n";
        return out;
    }

    // --- parsing -------------------------------------------------------------------

    namespace
    {
        using Node = rapidxml::xml_node<>;

        std::string get(const Node& n, const char* key, const std::string& def = "")
        {
            const rapidxml::xml_attribute<>* a = n.first_attribute(key);
            return a ? std::string(a->value(), a->value_size()) : def;
        }

        // Tolerant for our own numbers: a hand-edited coordinate becomes 0
        // rather than refusing the project (geometry can be fixed in the editor)
        int getInt(const Node& n, const char* key, int def = 0)
        {
            const rapidxml::xml_attribute<>* a = n.first_attribute(key);
            if (!a || a->value_size() == 0) return def;
            return static_cast<int>(std::strtol(a->value(), nullptr, 10));
        }

        bool inList(const char* name, const char* const* list)
        {
            for (; *list; ++list) {
                if (std::strcmp(name, *list) == 0) return true;
            }
            return false;
        }

        const char* const ROOT_KNOWN[] = {
            "version", "name", "orientation", "authorW", "authorH", "font",
            "soundLoad", "soundUnload", "soundHighlight", "soundSelect", nullptr
        };

        const char* const WIDGET_KNOWN[] = {
            "def", "id", "name", "x", "y", "w", "h", "layer", "autoscroll", "hide", "alpha",
            "slots", "fade", "curve", "tilt", "persp", "itemh", "pulse", "idledim", "ghost",
            "gdx", "gspan", "gitem", "galpha", "gmode", "scrollms", "accelms",
            "spitch", "sitemw", "sdim", "sselw", "sselh", "sitemraw", "sladraw", "sladkids",
            "font", "fontcolor", "warp", "wtl", "wtr", "wbl", "wbr", "wwin", nullptr
        };

        bool parseWidget(const Node& n, WidgetInstance& w, const std::string& pageName, std::string& error)
        {
            w.def = get(n, "def");
            w.id = get(n, "id");
            w.name = get(n, "name");
            if (w.def.empty() || w.id.empty()) {
                error = "widget missing def/id on page '" + pageName + "'";
                return false;
            }
            if (!widgetIdentValid(w.id)) {
                error = "widget id '" + w.id + "' invalid";
                return false;
            }
            if (w.name.empty()) w.name = w.id;
            w.x = getInt(n, "x");
            w.y = getInt(n, "y");
            w.w = getInt(n, "w");
            w.h = getInt(n, "h");
            w.layer = clampLayer(getInt(n, "layer"));
            w.autoScroll = get(n, "autoscroll") != "0";
            w.canvasHide = getInt(n, "hide") != 0;
            w.alphaPct = getInt(n, "alpha", 100);
            if (w.alphaPct < 0) w.alphaPct = 0;
            if (w.alphaPct > 100) w.alphaPct = 100;
            w.font = get(n, "font");
            w.fontColor = get(n, "fontcolor");
            w.menuSlots = getInt(n, "slots");
            w.menuFadePct = getInt(n, "fade");
            w.menuCurvePx = getInt(n, "curve");
            w.menuAngleDeg = getInt(n, "tilt");
            w.menuPerspDeg = getInt(n, "persp");
            w.menuItemH = getInt(n, "itemh");
            w.menuPulse = getInt(n, "pulse") != 0;
            w.menuIdleDimPct = getInt(n, "idledim", -1);
            w.menuGhost = getInt(n, "ghost") != 0;
            w.menuGhostDx = getInt(n, "gdx", -1);
            w.menuGhostSpanPct = getInt(n, "gspan", -1);
            w.menuGhostItemPct = getInt(n, "gitem", -1);
            w.menuGhostAlphaPct = getInt(n, "galpha", -1);
            w.menuGhostMode = get(n, "gmode");
            w.menuScrollMs = getInt(n, "scrollms");
            w.menuAccelMs = getInt(n, "accelms");
            w.stripPitchPx = getInt(n, "spitch");
            w.stripItemMaxW = getInt(n, "sitemw");
            w.stripDimPct = getInt(n, "sdim", -1);
            w.stripSelW = getInt(n, "sselw");
            w.stripSelH = getInt(n, "sselh");
            w.stripItemDefsRaw = get(n, "sitemraw");
            w.stripItemsRaw = get(n, "sladraw");
            w.stripItemKidsRaw = get(n, "sladkids");
            w.warp = get(n, "warp") == "1";
            if (w.warp) {
                static const char* corners[4] = { "wtl", "wtr", "wbl", "wbr" };
                for (int c = 0; c < 4; ++c) {
                    const std::string v = get(n, corners[c]);
                    const size_t comma = v.find(',');
                    if (comma == std::string::npos) { w.warp = false; break; }
                    w.wx[c] = std::atoi(v.substr(0, comma).c_str());
                    w.wy[c] = std::atoi(v.substr(comma + 1).c_str());
                }
                const std::string win = get(n, "wwin");
                int v[4] = { 0, 0, 0, 0 };
                size_t pos = 0;
                bool ok = !win.empty();
                for (int c = 0; c < 4 && ok; ++c) {
                    const size_t comma = win.find(',', pos);
                    const std::string part = win.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                    if (part.empty()) { ok = false; break; }
                    v[c] = std::atoi(part.c_str());
                    if (comma == std::string::npos) { ok = (c == 3); break; }
                    pos = comma + 1;
                }
                if (ok) { w.wwx = v[0]; w.wwy = v[1]; w.www = v[2]; w.wwh = v[3]; }
            }
            for (const rapidxml::xml_attribute<>* a = n.first_attribute(); a; a = a->next_attribute()) {
                if (!inList(a->name(), WIDGET_KNOWN)) w.extra.emplace_back(a->name(), a->value());
            }
            for (const Node* v = n.first_node("var"); v; v = v->next_sibling("var")) {
                const std::string token = get(*v, "token");
                if (token.empty()) {
                    error = "widget '" + w.id + "' has a <var> without a token";
                    return false;
                }
                w.setVar(token, get(*v, "value"));
            }
            return true;
        }

        bool parsePage(const Node& n, ThemePage& page, bool asMon, std::string& error)
        {
            page.collection = get(n, "collection");
            if (asMon) {
                page.monW = getInt(n, "monW");
                page.monH = getInt(n, "monH");
            }
            else {
                page.tplName = get(n, "tpl");
                if (page.tplName.empty() && !page.collection.empty() && !ThemeProject::isSplashPage(page.collection)) {
                    page.tplName = page.collection;
                }
                std::string cur;
                for (char c : get(n, "also")) {
                    if (c == '|') {
                        if (!cur.empty()) page.members.push_back(cur);
                        cur.clear();
                    }
                    else {
                        cur += c;
                    }
                }
                if (!cur.empty()) page.members.push_back(cur);
            }
            const std::string pageName = page.collection.empty() ? "Default" : page.collection;
            for (const Node* c = n.first_node(); c; c = c->next_sibling()) {
                if (c->type() != rapidxml::node_element) continue;
                const std::string tag(c->name(), c->name_size());
                if (tag == "page" && !asMon && get(*c, "mon") == "1") {
                    ThemePage m;
                    if (!parsePage(*c, m, true, error)) return false;
                    m.collection = page.collection;   // rides the parent's key
                    page.mons.push_back(m);
                    continue;
                }
                if (tag != "widget") continue;
                WidgetInstance w;
                if (!parseWidget(*c, w, pageName, error)) return false;
                if (page.findWidget(w.id)) {
                    error = "duplicate widget id '" + w.id + "' on page '" + pageName + "'";
                    return false;
                }
                page.widgets.push_back(w);
            }
            return true;
        }
    }

    bool projectParse(const std::string& xml, ThemeProject& out, std::string& error)
    {
        out = ThemeProject();
        out.pages.clear();
        error.clear();

        std::vector<char> buffer(xml.begin(), xml.end());
        buffer.push_back('\0');
        rapidxml::xml_document<> doc;
        try {
            doc.parse<0>(buffer.data());
        }
        catch (const rapidxml::parse_error& e) {
            error = std::string("project.xml is not valid XML: ") + e.what();
            return false;
        }
        const Node* root = doc.first_node("themeProject");
        if (!root) {
            error = "no <themeProject> root";
            return false;
        }
        const int version = getInt(*root, "version");
        if (version > ThemeProject::VERSION) {
            error = "project version " + std::to_string(version) + " is newer than this editor";
            return false;
        }
        out.name = get(*root, "name", "New Theme");
        const std::string orientation = get(*root, "orientation", "horizontal");
        if (orientation != "horizontal" && orientation != "vertical") {
            error = "orientation '" + orientation + "' unknown";
            return false;
        }
        out.vertical = orientation == "vertical";
        out.authorW = getInt(*root, "authorW");
        out.authorH = getInt(*root, "authorH");
        out.font = get(*root, "font");
        out.soundLoad = get(*root, "soundLoad");
        out.soundUnload = get(*root, "soundUnload");
        out.soundHighlight = get(*root, "soundHighlight");
        out.soundSelect = get(*root, "soundSelect");
        for (const rapidxml::xml_attribute<>* a = root->first_attribute(); a; a = a->next_attribute()) {
            if (!inList(a->name(), ROOT_KNOWN)) out.extra.emplace_back(a->name(), a->value());
        }
        // One Saucier's deferred-asset manifest (<asset> children) is dropped:
        // here a project's assets always live in its theme folder
        for (const Node* n = root->first_node("page"); n; n = n->next_sibling("page")) {
            if (get(*n, "mon") == "1") continue;   // never a top-level page
            ThemePage page;
            if (!parsePage(*n, page, false, error)) return false;
            if (out.page(page.collection)) {
                error = "duplicate page for collection '" + page.collection + "'";
                return false;
            }
            out.pages.push_back(page);
        }

        // The Default page is an invariant: hoist a misplaced one to the front,
        // or insert an empty one
        for (size_t i = 0; i < out.pages.size(); ++i) {
            if (out.pages[i].collection.empty()) {
                if (i != 0) {
                    ThemePage def = out.pages[i];
                    out.pages.erase(out.pages.begin() + static_cast<long>(i));
                    out.pages.insert(out.pages.begin(), def);
                }
                return true;
            }
        }
        out.pages.insert(out.pages.begin(), ThemePage());
        return true;
    }
}

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

#include "ThemeGenerate.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ThemeEditor
{
    static const char* const MARKER_PREFIX = "<!-- RetroFE theme editor;";
    static const char* const ONE_SAUCIER_MARKER_PREFIX = "<!-- one_saucier theme editor;";

    std::string editorMarker(const std::string& projectSlug)
    {
        return std::string(MARKER_PREFIX) + " project=" + projectSlug + "; v=1 -->";
    }

    bool isEditorMarker(const std::string& firstLine)
    {
        std::string line = firstLine;
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        return line.rfind(MARKER_PREFIX, 0) == 0 || line.rfind(ONE_SAUCIER_MARKER_PREFIX, 0) == 0;
    }

    // --- expansion ------------------------------------------------------------------

    namespace
    {
        struct TokenValues
        {
            // token -> (value, escape?): text vars are escaped, everything else is raw
            std::vector<std::pair<std::string, std::pair<std::string, bool>>> values;
            void add(const std::string& t, const std::string& v, bool escape) { values.push_back({ t, { v, escape } }); }
            const std::pair<std::string, bool>* find(const std::string& t) const
            {
                for (const auto& kv : values) {
                    if (kv.first == t) return &kv.second;
                }
                return nullptr;
            }
        };

        std::string formatAlpha(int pct)
        {
            char buf[8];
            if (pct >= 100) std::snprintf(buf, sizeof(buf), "1");
            else if (pct <= 0) std::snprintf(buf, sizeof(buf), "0");
            else std::snprintf(buf, sizeof(buf), "0.%02d", pct);
            return buf;
        }

        std::string formatSeconds(int ms)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f", ms / 1000.0);
            return buf;
        }

        TokenValues buildValues(const WidgetDef& def, const WidgetInstance& inst, const GenOptions& opt)
        {
            TokenValues tv;
            tv.add("ID", inst.id, false);
            tv.add("X", std::to_string(inst.x), false);
            tv.add("Y", std::to_string(inst.y), false);
            tv.add("W", std::to_string(inst.w), false);
            tv.add("H", std::to_string(inst.h), false);
            tv.add("LAYER", std::to_string(inst.layer), false);
            tv.add("CX", std::to_string(inst.x + inst.w / 2), false);
            tv.add("CY", std::to_string(inst.y + inst.h / 2), false);
            tv.add("X2", std::to_string(inst.x + inst.w), false);
            tv.add("Y2", std::to_string(inst.y + inst.h), false);
            tv.add("FS", std::to_string((inst.h * 72) / 100), false);
            tv.add("ALPHA", formatAlpha(inst.alphaPct), false);
            tv.add("FONT", inst.font, true);
            tv.add("FONTCOLOR", inst.fontColor, true);
            tv.add("SCROLLTIME", formatSeconds(inst.menuScrollMs > 0 ? inst.menuScrollMs : 120), false);
            tv.add("SCROLLACCEL", formatSeconds(inst.menuAccelMs), false);
            tv.add("THEMEDIR", opt.themeDir, true);
            tv.add("PAGEROOT", opt.pageRoot, true);
            for (const auto& v : def.vars) {
                // "raw" substitutes verbatim (whole attribute strings); other
                // non-bool values are escaped
                tv.add(v.token, inst.varValue(v.token, v.def), v.type != "bool" && v.type != "raw");
            }
            return tv;
        }

        // A bool token's value; nullptr when the token isn't a bool
        const bool* boolValue(const WidgetDef& def, const WidgetInstance& inst, const std::string& token, bool& storage)
        {
            if (token == "WARP") { storage = false; return &storage; }   // no transform= in this engine
            if (token == "HASFONT") { storage = !inst.font.empty(); return &storage; }
            if (token == "HASFONTCOLOR") { storage = !inst.fontColor.empty(); return &storage; }
            if (token == "HASACCEL") { storage = inst.menuAccelMs > 0; return &storage; }
            const WidgetVar* v = def.var(token);
            if (!v || v->type != "bool") return nullptr;
            storage = inst.varValue(token, v->def) == "true";
            return &storage;
        }

        std::string spliceAll(std::string text, const std::string& site, const std::string& body)
        {
            size_t pos = 0;
            while ((pos = text.find(site, pos)) != std::string::npos) {
                text.replace(pos, site.size(), body);
                pos += body.size();
            }
            return text;
        }
    }

    ExpandResult expandText(const std::string& text, const WidgetDef& def, const WidgetInstance& inst,
                            const GenOptions& opt)
    {
        ExpandResult r;
        const TokenValues tv = buildValues(def, inst, opt);
        std::string& out = r.text;
        out.reserve(text.size());

        size_t i = 0;
        std::vector<std::string> openGates;   // true gates whose bodies we're inside
        while (i < text.size()) {
            const size_t open = text.find("{{", i);
            if (open == std::string::npos) { out += text.substr(i); break; }
            out += text.substr(i, open - i);
            const size_t close = text.find("}}", open + 2);
            if (close == std::string::npos) {
                r.unknown.push_back(text.substr(open));
                out += text.substr(open);
                break;
            }
            const std::string inner = text.substr(open + 2, close - open - 2);
            i = close + 2;

            // {{/TOKEN}}: the close of a true gate (a false gate consumes its own close)
            if (!inner.empty() && inner[0] == '/') {
                const std::string token = inner.substr(1);
                if (!openGates.empty() && openGates.back() == token) { openGates.pop_back(); continue; }
                r.unknown.push_back("{{" + inner + "}}");
                out += "{{" + inner + "}}";
                continue;
            }

            // {{?TOKEN}} / {{!TOKEN}}
            if (!inner.empty() && (inner[0] == '?' || inner[0] == '!')) {
                const std::string token = inner.substr(1);
                bool storage = false;
                const bool* b = boolValue(def, inst, token, storage);
                const std::string closeMark = "{{/" + token + "}}";
                const size_t end = text.find(closeMark, i);
                if (!b || end == std::string::npos) {
                    r.unknown.push_back("{{" + inner + "}}");
                    out += "{{" + inner + "}}";
                    continue;
                }
                const bool keep = (inner[0] == '?') ? *b : !*b;
                if (!keep) i = end + closeMark.size();
                else openGates.push_back(token);
                continue;
            }

            // {{TOKEN:whenTrue:whenFalse}}
            const size_t c1 = inner.find(':');
            if (c1 != std::string::npos) {
                const std::string token = inner.substr(0, c1);
                const size_t c2 = inner.find(':', c1 + 1);
                bool storage = false;
                const bool* b = boolValue(def, inst, token, storage);
                if (!b || c2 == std::string::npos) {
                    r.unknown.push_back("{{" + inner + "}}");
                    out += "{{" + inner + "}}";
                    continue;
                }
                out += *b ? inner.substr(c1 + 1, c2 - c1 - 1) : inner.substr(c2 + 1);
                continue;
            }

            // {{TOKEN}}
            const std::pair<std::string, bool>* v = tv.find(inner);
            if (!v) {
                r.unknown.push_back("{{" + inner + "}}");
                out += "{{" + inner + "}}";
                continue;
            }
            out += v->second ? xmlEscape(v->first) : v->first;
        }
        return r;
    }

    ExpandResult expandFragment(const WidgetDef& def, const WidgetInstance& inst, const GenOptions& opt)
    {
        // Splice the animations first so their own sites expand in the same pass
        const std::string body = opt.includeAnims ? def.animDefaults : std::string();
        return expandText(spliceAll(def.fragment, "{{ANIM_DEFAULTS}}", body), def, inst, opt);
    }

    // --- menu slots ---------------------------------------------------------------------

    std::string menuItemsXml(int x, int y, int w, int h, int layer, bool vertical, int arcPct, int canvasW,
                             int slots, int fadePct, int curvePx, int tiltDeg, int perspDeg, int itemHPx,
                             MenuSelGeom* selOut, bool ghostStyle, int idleDimPct, const std::string& fontSizePx,
                             const std::string& fontColor, const std::string& selFontColor,
                             int ghostItemPct, int ghostAlphaPct)
    {
        // A wheel with a text size is a text wheel: its selected slot doesn't swell
        const bool textWheel = !ghostStyle && !fontSizePx.empty();
        std::string styleDefaults;
        if (!ghostStyle) {
            // Escaped: these come from free text vars, spliced in before expandText runs
            if (!fontSizePx.empty()) styleDefaults += " fontSize=\"" + xmlEscape(fontSizePx) + "\"";
            if (!fontColor.empty()) styleDefaults += " fontColor=\"" + xmlEscape(fontColor) + "\"";
        }
        // Idle dim: one second into menu idle each unselected item dims; the
        // next scroll's own animation brings it back
        std::string idleDim;
        if (idleDimPct >= 0 && !ghostStyle) {
            char ab[16];
            std::snprintf(ab, sizeof(ab), "%.2f", (idleDimPct > 100 ? 100 : idleDimPct) / 100.0);
            idleDim = std::string(">\n") +
                      "  <onMenuIdle menuIndex=\"i\">\n" +
                      "    <set duration=\"1\"><animate type=\"nop\"/></set>\n" +
                      "    <set duration=\"0.25\"><animate type=\"alpha\" to=\"" + ab + "\"/></set>\n" +
                      "  </onMenuIdle>\n" +
                      "</item>\n";
        }
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (slots < MENU_SLOTS_MIN) slots = MENU_SLOTS_MIN;
        if (slots > MENU_SLOTS_MAX) slots = MENU_SLOTS_MAX;
        if (!(slots & 1)) ++slots;   // odd: always one centre slot
        if (fadePct < 0) fadePct = 0;
        if (fadePct > 100) fadePct = 100;
        const int cx = x + w / 2;
        const int cy = y + h / 2;
        const int mid = slots / 2;
        std::string out;

        // Edge fade applies to the outermost pair only, the way themes author it.
        // A ghost rests dimmer unless its alpha is 100 (then it follows the main ramps).
        auto slotAlpha = [&](int k) -> std::string {
            if (ghostStyle && ghostAlphaPct != 100 && (fadePct <= 0 || (k != 0 && k != slots - 1))) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%.2f", (ghostAlphaPct >= 0 ? ghostAlphaPct : 75) / 100.0);
                return std::string(" alpha=\"") + buf + "\"";
            }
            if (fadePct <= 0) return std::string();
            if (k != 0 && k != slots - 1) return std::string();
            double a = 1.0 - (static_cast<double>(fadePct) / 100.0);
            if (a < 0.0) a = 0.0;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.2f", a);
            return std::string(" alpha=\"") + buf + "\"";
        };

        if (vertical) {
            // Lay the slot line out first: the item height comes from its pitch.
            // Perspective foreshortens the ladder (slots at even angles on a
            // circle); it falls back to the tilt.
            const int perspEff = perspDeg > 0 ? perspDeg : tiltDeg;
            const double persRad = static_cast<double>(perspEff) * 3.14159265358979 / 180.0;
            const bool onArc = perspEff > 0 && std::sin(persRad) > 0.0001;
            const double halfSpan = static_cast<double>(h) * static_cast<double>(slots - 1) / static_cast<double>(2 * slots);
            auto slotT = [slots](int k) {
                return slots > 1 ? static_cast<double>(2 * k - (slots - 1)) / static_cast<double>(slots - 1) : 0.0;
            };
            auto slotY = [&](int k) -> int {
                if (onArc) return cy + static_cast<int>(std::lround(halfSpan * std::sin(persRad * slotT(k)) / std::sin(persRad)));
                return y + (h * (2 * k + 1)) / (2 * slots);
            };
            // The widest gap is the pitch the art is cut to (at the centre on an arc)
            int pitch = h / (slots > 0 ? slots : 1);
            for (int k = 0; k + 1 < slots; ++k) {
                const int g = slotY(k + 1) - slotY(k);
                if (g > pitch) pitch = g;
            }
            if (pitch < 1) pitch = 1;
            // Resting art fills its slot; the selected logo swells to 5/3
            const int restW = (w * 65) / 100;
            const int itemH0 = itemHPx > 0 ? itemHPx : pitch;
            const int gip = ghostItemPct >= 0 ? ghostItemPct : 146;
            const int itemH = ghostStyle ? (itemH0 * gip) / 100 : itemH0;
            const int selW = ghostStyle ? restW : w;
            const int selH = ghostStyle ? itemH : (itemH0 * 10) / 6;
            // The bulge faces the canvas centre
            const int arcDir = (x + w / 2) <= canvasW / 2 ? 1 : -1;
            const int arcMag = arcPct < 0 ? -arcPct : arcPct;
            const double arcAmp = curvePx > 0 ? static_cast<double>(curvePx * arcDir)
                                              : static_cast<double>(w) * static_cast<double>(arcMag * arcDir) / 100.0;
            if (selOut) {
                selOut->x = cx + static_cast<int>(std::lround(arcAmp));
                selOut->y = slotY(mid);
                selOut->w = selW;
                selOut->h = selH;
                selOut->valid = true;
            }
            // Exact width + maxHeight (aspect kept), never width + height
            out += "<itemDefaults x=\"" + std::to_string(cx) + "\" xOrigin=\"center\" yOrigin=\"center\" width=\"" +
                   std::to_string(restW) + "\" maxHeight=\"" + std::to_string(itemH) + "\"" + styleDefaults +
                   " layer=\"" + std::to_string(layer) + "\"/>\n";
            for (int k = 0; k < slots; ++k) {
                const double t = slotT(k);
                const double phi = persRad * t;
                out += "<item y=\"" + std::to_string(slotY(k)) + "\"";
                if (arcAmp != 0.0) {
                    // On the arc the sideways offset follows the same circle,
                    // normalised so the end slots land at the curve depth;
                    // off it, a cosine bulge (centre out, ends home)
                    const double frac = onArc ? (1.0 - std::cos(phi)) / (1.0 - std::cos(persRad)) : std::cos(t * 1.5707963);
                    const int off = static_cast<int>(std::lround(arcAmp * (onArc ? (1.0 - frac) : frac)));
                    if (off != 0) out += " x=\"" + std::to_string(cx + off) + "\"";
                }
                // Tilt: 0 at the centre ramping to tiltDeg at the ends
                if (tiltDeg != 0 && slots > 1) {
                    const int deg = static_cast<int>(std::lround(-static_cast<double>(tiltDeg) * t));
                    if (deg != 0) out += " angle=\"" + std::to_string(deg) + "\"";
                }
                if (k == mid) {
                    if (textWheel) {
                        out += " selected=\"true\"";
                        if (!selFontColor.empty()) out += " fontColor=\"" + xmlEscape(selFontColor) + "\"";
                    }
                    else {
                        out += " width=\"" + std::to_string(selW) + "\" maxHeight=\"" + std::to_string(selH) + "\" selected=\"true\"";
                    }
                    if (ghostStyle) out += " alpha=\"1\" layer=\"" + std::to_string(layer + 6) + "\"";
                }
                else {
                    out += slotAlpha(k);
                }
                // No idle dim on hidden end slots: it would fade them in
                const bool edgeHidden = fadePct > 0 && (k == 0 || k == slots - 1);
                if (k != mid && !edgeHidden && !idleDim.empty()) out += idleDim;
                else out += "/>\n";
            }
            return out;
        }

        // Horizontal: slots spread across the box, exact height + maxWidth
        const int itemW = w / (slots + 1);
        const int selW = (itemW * 14) / 10;
        const int artH = (h * 8) / 10;
        out += "<itemDefaults y=\"" + std::to_string(cy) + "\" xOrigin=\"center\" yOrigin=\"center\" height=\"" +
               std::to_string(artH) + "\" maxWidth=\"" + std::to_string(itemW) + "\" layer=\"" + std::to_string(layer) + "\"/>\n";
        for (int k = 0; k < slots; ++k) {
            const int xk = x + (w * (2 * k + 1)) / (2 * slots);
            out += "<item x=\"" + std::to_string(xk) + "\"";
            if (k == mid) out += " maxWidth=\"" + std::to_string(selW) + "\" selected=\"true\"";
            else out += slotAlpha(k);
            const bool edgeHidden = fadePct > 0 && (k == 0 || k == slots - 1);
            if (k != mid && !edgeHidden && !idleDim.empty()) out += idleDim;
            else out += "/>\n";
        }
        return out;
    }

    std::string menuPulseXml(const MenuSelGeom& g, int layer)
    {
        // Rests invisible (every menu and playlist event snaps alpha to 0);
        // on menu idle it appears over the selected logo and grows 1.2x while
        // fading out. One layer below the wheel so the grown edges peek out.
        if (!g.valid) return std::string();
        const int lay = layer > 0 ? layer - 1 : 0;
        const std::string W = std::to_string(g.w);
        const std::string H = std::to_string(g.h);
        const std::string W2 = std::to_string((g.w * 12) / 10);
        const std::string H2 = std::to_string((g.h * 12) / 10);
        std::string out;
        out += "<reloadableImage type=\"logo\" x=\"" + std::to_string(g.x) + "\" y=\"" + std::to_string(g.y) +
               "\" xOrigin=\"center\" yOrigin=\"center\" width=\"" + W + "\" maxHeight=\"" + H +
               "\" layer=\"" + std::to_string(lay) + "\">\n";
        static const char* const zeroed[] = {
            "onPlaylistEnter", "onPlaylistExit", "onMenuJumpEnter", "onMenuJumpExit", "onMenuExit", "onMenuEnter", "onMenuScroll",
        };
        for (const char* ev : zeroed) {
            out += std::string("  <") + ev + ">\n" +
                   "    <set duration=\"0.001\"><animate type=\"alpha\" to=\"0\"/></set>\n" +
                   "  </" + ev + ">\n";
        }
        out += "  <onMenuIdle>\n"
               "    <set duration=\"0.4\"><animate type=\"alpha\" to=\"0\"/></set>\n"
               "    <set duration=\"0.001\">\n"
               "      <animate type=\"width\" to=\"" + W + "\" algorithm=\"easeinquadratic\"/>\n"
               "      <animate type=\"maxHeight\" to=\"" + H + "\" algorithm=\"easeinquadratic\"/>\n"
               "      <animate type=\"alpha\" to=\"0.9\"/>\n"
               "    </set>\n"
               "    <set duration=\"1.3\">\n"
               "      <animate type=\"width\" to=\"" + W2 + "\" algorithm=\"easeinquadratic\"/>\n"
               "      <animate type=\"maxHeight\" to=\"" + H2 + "\" algorithm=\"easeinquadratic\"/>\n"
               "      <animate type=\"alpha\" to=\"0.0\"/>\n"
               "    </set>\n"
               "    <set duration=\"0.4\"><animate type=\"alpha\" to=\"0\"/></set>\n"
               "  </onMenuIdle>\n"
               "</reloadableImage>\n";
        return out;
    }

    std::string menuStripItemsXml(int x, int y, int w, int h, int layer, int slots, int pitchPx, int itemMaxW,
                                  int dimPct, int selW, int selH)
    {
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (slots < 7) slots = 7;
        if (!(slots & 1)) ++slots;
        if (pitchPx > 0) {
            // A measured linear strip: left to right at the pitch, aspect-fit
            // items, unselected rows dimmed, alpha-0 ends just past the span
            const int cx2 = x + w / 2;
            const int cy2 = y + h / 2;
            int n = (slots - 3) / 2;
            if (n < 1) n = 1;
            const int imw = itemMaxW > 0 ? itemMaxW : pitchPx - 40;
            std::string out;
            out += "<itemDefaults x=\"" + std::to_string(cx2) + "\" y=\"" + std::to_string(cy2) +
                   "\" xOrigin=\"center\" yOrigin=\"center\" maxWidth=\"" + std::to_string(imw) + "\" height=\"" +
                   std::to_string(h) + "\" layer=\"" + std::to_string(layer) + "\"";
            if (dimPct >= 0 && dimPct < 100) out += " alpha=\"" + formatAlpha(dimPct) + "\"";
            out += "/>\n";
            auto item = [&](int off, const std::string& extra) {
                out += "<item";
                if (off != 0) out += " xOffset=\"" + std::to_string(off) + "\"";
                out += extra;
                out += "/>\n";
            };
            item(-(n + 1) * pitchPx, " alpha=\"0\"");
            for (int i = n; i >= 1; --i) item(-i * pitchPx, "");
            std::string sel = " alpha=\"1\" selected=\"true\"";
            if (selW > 0 || selH > 0) {
                if (selW > 0) sel += " width=\"" + std::to_string(selW) + "\"";
                if (selH > 0) sel += " maxHeight=\"" + std::to_string(selH) + "\"";
                sel += " layer=\"" + std::to_string(layer < 20 ? layer + 1 : 20) + "\"";
            }
            item(0, sel);
            for (int i = 1; i <= n; ++i) item(i * pitchPx, "");
            item((n + 1) * pitchPx, " alpha=\"0\"");
            return out;
        }
        const int hpairs = (slots - 5) / 2;
        const int itemW = w / (slots - 2);
        const int cx = x + w / 2;
        std::string out;
        out += "<itemDefaults x=\"" + std::to_string(cx) + "\" y=\"" + std::to_string(y) +
               "\" yOrigin=\"top\" xOrigin=\"center\" width=\"" + std::to_string(itemW) + "\" minHeight=\"" +
               std::to_string(h) + "\" layer=\"" + std::to_string(layer) + "\"/>\n";
        auto item = [&](int off, const char* extra) {
            out += "<item";
            if (off != 0) out += " xOffset=\"" + std::to_string(off) + "\"";
            out += extra;
            out += "/>\n";
        };
        // The ribbon's slot order: entering edge, the popped slot, the right
        // side, the hidden selected slot, the left side
        item(0, " alpha=\"0\"");
        out += "<item layer=\"" + std::to_string(layer + 1) + "\"/>\n";
        for (int i = 1; i <= hpairs; ++i) item(itemW * i, "");
        item(itemW * (hpairs + 1), " alpha=\"0\"");
        item(0, " alpha=\"0\" selected=\"true\"");
        for (int i = 1; i <= hpairs; ++i) item(-itemW * i, "");
        item(-itemW * (hpairs + 1), " alpha=\"0\"");
        return out;
    }

    // --- layout generation ----------------------------------------------------------------

    namespace
    {
        bool categoryIncluded(const std::string& category, const GenOptions& opt)
        {
            if (category == "text") return opt.includeText;
            return opt.includeMedia;
        }

        // Appends `block` with every non-empty line indented two spaces
        void appendIndented(std::string& out, const std::string& block)
        {
            size_t pos = 0;
            while (pos <= block.size()) {
                const size_t nl = block.find('\n', pos);
                const std::string line = block.substr(pos, (nl == std::string::npos ? block.size() : nl) - pos);
                pos = (nl == std::string::npos) ? block.size() + 1 : nl + 1;
                if (!line.empty()) { out += "  "; out += line; }
                out += '\n';
            }
            if (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.erase(out.size() - 1);
        }

        std::vector<std::string> splitBar(const std::string& joined)
        {
            std::vector<std::string> parts;
            std::string cur;
            for (char c : joined) {
                if (c == '|') { parts.push_back(cur); cur.clear(); }
                else cur += c;
            }
            if (!joined.empty()) parts.push_back(cur);
            return parts;
        }

        // Menu modes this engine reads (PageBuilder::buildMenu); anything else is no mode
        std::string menuMode(const std::string& mode)
        {
            if (mode == "layout" || mode == "common" || mode == "commonlayout") return mode;
            return "";
        }
    }

    GenResult generateLayoutXml(const ThemeProject& p, const ThemePage& page, const WidgetLibrary& lib,
                                const GenOptions& options)
    {
        GenOptions opt = options;
        if (opt.themeDir.empty()) opt.themeDir = themeFolderName(p.name);
        // Collection pages live in collections/<c>/layout/, three folders below the theme
        const bool collectionPage = !page.collection.empty() && !ThemeProject::isSplashPage(page.collection);
        opt.pageRoot = collectionPage ? "../../../" : "";
        const int spaceW = opt.spaceW ? opt.spaceW : p.canvasW();
        const int spaceH = opt.spaceH ? opt.spaceH : p.canvasH();

        GenResult r;

        // Structural checks (whatever the Show toggles say)
        for (const auto& w : page.widgets) {
            const WidgetDef* def = lib.find(w.def);
            if (!def) { r.errors.push_back("widget '" + w.id + "' references unknown definition '" + w.def + "'"); continue; }
            if (!widgetIdentValid(w.id)) { r.errors.push_back("widget id '" + w.id + "' invalid (want [a-z0-9_]+)"); continue; }
            for (const auto& o : page.widgets) {
                if (&o != &w && o.id == w.id) { r.errors.push_back("duplicate widget id '" + w.id + "'"); break; }
            }
            int bx = w.x, by = w.y, bw = w.w, bh = w.h;
            if (w.warp) w.warpBounds(bx, by, bw, bh);
            if (bw < 1 || bh < 1) r.errors.push_back("widget '" + w.id + "' has a degenerate size");
            if (w.layer < 0 || w.layer > 20) r.warnings.push_back("widget '" + w.id + "' layer clamped to 0..20");
            if (bx + bw <= 0 || by + bh <= 0 || bx >= spaceW || by >= spaceH) {
                r.warnings.push_back("widget '" + w.id + "' lies fully outside the canvas");
            }
        }
        // (a,b) and (b,a) report the same duplicate
        for (size_t i = 0; i < r.errors.size(); ++i) {
            for (size_t j = r.errors.size(); j-- > i + 1;) {
                if (r.errors[j] == r.errors[i]) r.errors.erase(r.errors.begin() + static_cast<long>(j));
            }
        }
        if (!r.ok()) return r;

        std::string& out = r.xml;
        out += editorMarker(editorSlug(p.name)) + "\n";
        out += "<layout width=\"" + std::to_string(spaceW) + "\" height=\"" + std::to_string(spaceH) +
               "\" font=\"" + (p.font.empty() ? std::string("fonts/Roboto-Regular.ttf") : xmlEscape(p.font)) +
               // 48 suits the 1080-unit canvas; an author space scales it
               "\" loadFontSize=\"" + std::to_string(48 * std::min(spaceW, spaceH) / 1080) +
               "\" fontColor=\"E6E7EB\">\n";
        const std::pair<const char*, const std::string*> sounds[4] = {
            { "load", &p.soundLoad }, { "unload", &p.soundUnload },
            { "highlight", &p.soundHighlight }, { "select", &p.soundSelect },
        };
        for (const auto& s : sounds) {
            if (!s.second->empty()) {
                out += std::string("  <sound type=\"") + s.first + "\" src=\"" + opt.pageRoot + xmlEscape(*s.second) + "\"/>\n";
            }
        }

        for (const auto& w : page.widgets) {
            const WidgetDef* def = lib.find(w.def);
            if (!categoryIncluded(def->category, opt)) continue;
            if (opt.canvasPreview && w.canvasHide) continue;

            WidgetInstance inst = w;
            if (inst.layer < 0) inst.layer = 0;
            if (inst.layer > 20) inst.layer = 20;
            // A splash video must finish, or the splash never ends
            if (ThemeProject::isSplashPage(page.collection) && inst.def == "static_video") inst.setVar("LOOP", "false");

            // Dialect: no transform= here, so a warped widget is drawn as the
            // bounding box of its corners
            if (inst.warp) {
                inst.warpBounds(inst.x, inst.y, inst.w, inst.h);
                inst.warp = false;
                r.warnings.push_back("widget '" + w.id + "' is warped (Transform Video), which this engine can't draw; "
                                     "it is drawn as the bounding box of its corners");
            }
            // Dialect: mode="layout_preferred" isn't a mode here; no mode (the
            // game's own media) is the nearest
            for (const auto& v : def->vars) {
                if (v.type != "bool" && inst.varValue(v.token, v.def) == "layout_preferred") {
                    inst.setVar(v.token, "");
                    r.warnings.push_back("widget '" + w.id + "': mode layout_preferred isn't supported by this engine; "
                                         "using the game's own media");
                }
            }

            std::string fragment = def->fragment;
            MenuSelGeom selGeom;
            if (def->menu && !inst.stripItemsRaw.empty()) {
                // A verbatim ladder: itemDefaults and each item as authored
                std::string vb;
                if (!inst.stripItemDefsRaw.empty()) vb += "<itemDefaults " + inst.stripItemDefsRaw + "/>\n";
                std::string idleBlock;
                if (opt.includeAnims && inst.menuIdleDimPct >= 0) {
                    char ab[16];
                    std::snprintf(ab, sizeof(ab), "%.2f", (inst.menuIdleDimPct > 100 ? 100 : inst.menuIdleDimPct) / 100.0);
                    idleBlock = std::string("  <onMenuIdle menuIndex=\"i\">\n") +
                                "    <set duration=\"1\"><animate type=\"nop\"/></set>\n" +
                                "    <set duration=\"0.25\"><animate type=\"alpha\" to=\"" + ab + "\"/></set>\n" +
                                "  </onMenuIdle>\n";
                }
                const std::vector<std::string> parts = splitBar(inst.stripItemsRaw);
                const std::vector<std::string> kids = splitBar(inst.stripItemKidsRaw);
                for (size_t vi = 0; vi < parts.size(); ++vi) {
                    const std::string& part = parts[vi];
                    if (part.empty()) continue;
                    const std::string kid = vi < kids.size() ? kids[vi] : std::string();
                    const bool restsHidden = part.find("alpha=\"0\"") != std::string::npos ||
                                             part.find("alpha=\"0.0\"") != std::string::npos ||
                                             part.find("alpha=\"0.00\"") != std::string::npos;
                    const bool isSelected = part.find("selected=\"true\"") != std::string::npos;
                    std::string inner = opt.includeAnims ? kid : std::string();
                    if (!idleBlock.empty() && kid.empty() && !restsHidden && !isSelected) inner = idleBlock;
                    if (inner.empty()) vb += "<item " + part + "/>\n";
                    else vb += "<item " + part + ">\n" + inner + "\n</item>\n";
                }
                fragment = spliceAll(fragment, "{{MENU_ITEMS}}", vb);
            }
            else if (def->menu && def->strip) {
                fragment = spliceAll(fragment, "{{MENU_ITEMS}}",
                                     menuStripItemsXml(inst.x, inst.y, inst.w, inst.h, inst.layer,
                                                       inst.menuSlots > 0 ? inst.menuSlots : 9, inst.stripPitchPx,
                                                       inst.stripItemMaxW, inst.stripDimPct, inst.stripSelW, inst.stripSelH));
            }
            else if (def->menu) {
                fragment = spliceAll(fragment, "{{MENU_ITEMS}}",
                                     menuItemsXml(inst.x, inst.y, inst.w, inst.h, inst.layer, !def->horizontal, def->arc, spaceW,
                                                  inst.menuSlots > 0 ? inst.menuSlots : MENU_ITEM_SLOTS, inst.menuFadePct,
                                                  inst.menuCurvePx, inst.menuAngleDeg, inst.menuPerspDeg, inst.menuItemH, &selGeom,
                                                  false, opt.includeAnims ? inst.menuIdleDimPct : -1,
                                                  inst.varValue("WHEELFONTSIZE", ""), inst.varValue("WHEELFONTCOLOR", ""),
                                                  inst.varValue("WHEELSELCOLOR", "")));
            }
            fragment = spliceAll(fragment, "{{ANIM_DEFAULTS}}", opt.includeAnims ? def->animDefaults : std::string());
            ExpandResult ex = expandText(fragment, *def, inst, opt);
            if (!ex.ok()) {
                std::string sites;
                for (const auto& u : ex.unknown) {
                    if (!sites.empty()) sites += ", ";
                    sites += u;
                }
                r.errors.push_back("widget '" + w.id + "' has unresolved template sites: " + sites);
                continue;
            }
            appendIndented(out, ex.text);

            // The selected-logo pulse, a sibling after the wheel
            if (def->menu && !def->strip && inst.menuPulse && opt.includeAnims) appendIndented(out, menuPulseXml(selGeom, inst.layer));

            // The ghost wheel: a second menu on the same list (same-depth menus
            // scroll together), shifted away from the centre, a little taller,
            // dimmer, uniform slots, and a faster ease so it leads each step.
            // Unpinned, it takes the next menu depth: hidden at the top level.
            if (def->menu && !def->strip && inst.menuGhost && !def->horizontal && !opt.canvasPreview) {
                const int glayer = inst.layer - 1 < 1 ? 1 : inst.layer - 1;
                const int arcDir = (inst.x + inst.w / 2) <= spaceW / 2 ? 1 : -1;
                const int gdx = inst.menuGhostDx >= 0 ? inst.menuGhostDx : 20;
                const int gspan = inst.menuGhostSpanPct >= 0 ? inst.menuGhostSpanPct : 3;
                const std::string gmode = menuMode(inst.menuGhostMode);
                const int gh = (inst.h * (100 + gspan)) / 100;
                const int gx = inst.x - gdx * arcDir;
                const int gy = inst.y - (gh - inst.h) / 2;
                const int gms = ((inst.menuScrollMs > 0 ? inst.menuScrollMs : 120) * 8) / 10;
                std::string g = "<menu id=\"" + inst.id + "_ghost\" type=\"custom\" imageType=\"" +
                                xmlEscape(inst.varValue("IMAGETYPE", "logo")) + "\"";
                if (!gmode.empty()) g += " mode=\"" + gmode + "\"";
                g += " orientation=\"vertical\" scrollTime=\"" + formatSeconds(gms) + "\" minScrollTime=\"0.06\"";
                if (inst.menuAccelMs > 0) g += " scrollAcceleration=\"" + formatSeconds(inst.menuAccelMs) + "\"";
                g += " layer=\"" + std::to_string(glayer) + "\">\n";
                g += menuItemsXml(gx, gy, inst.w, gh, glayer, true, def->arc, spaceW,
                                  inst.menuSlots > 0 ? inst.menuSlots : MENU_ITEM_SLOTS, inst.menuFadePct, inst.menuCurvePx,
                                  inst.menuAngleDeg, inst.menuPerspDeg, inst.menuItemH, nullptr, true, -1,
                                  std::string(), std::string(), std::string(), inst.menuGhostItemPct, inst.menuGhostAlphaPct);
                g += "</menu>\n";
                appendIndented(out, g);
            }
        }

        out += "</layout>\n";
        if (!r.ok()) r.xml.clear();   // never hand back half a theme
        return r;
    }
}

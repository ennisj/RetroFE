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
#include "EditorScript.h"
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ThemeEditor
{
    namespace
    {
        std::string trim(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return s.substr(a, b - a);
        }

        bool number(const std::vector<std::string>& words, size_t i, float& out)
        {
            if (i >= words.size()) return false;
            const char* text = words[i].c_str();
            char* end = nullptr;
            out = std::strtof(text, &end);
            return end != text && *end == '\0';
        }

        bool integer(const std::vector<std::string>& words, size_t i, int& out)
        {
            float f = 0;
            if (!number(words, i, f)) return false;
            out = static_cast<int>(f);
            return static_cast<float>(out) == f;
        }

        // Opacity as the Opacity slider has it (0-100): "0.8" or "1" (0-1, as
        // layouts write alpha), "80" (2-100 is a percent) or "80%". Plain
        // decimals only: no sign, exponent or hex.
        bool alphaPercent(const std::vector<std::string>& words, int& pct)
        {
            if (words.size() != 1) return false;
            std::string text = words[0];
            bool percent = !text.empty() && text.back() == '%';
            if (percent) text.pop_back();
            size_t digits = 0, dots = 0;
            for (char ch : text) {
                if (std::isdigit(static_cast<unsigned char>(ch))) ++digits;
                else if (ch == '.') ++dots;
                else return false;
            }
            if (digits == 0 || dots > 1) return false;
            float value = std::strtof(text.c_str(), nullptr);
            bool fraction = !percent && (dots == 1 || value == 1.0f);
            if (fraction) {
                if (value > 1) return false;
                value *= 100;
            }
            if (value > 100) return false;
            pct = static_cast<int>(value + 0.5f);
            return true;
        }

        int handleFromName(const std::string& name)
        {
            static const char* names[] = { "nw", "n", "ne", "w", "e", "sw", "s", "se" };
            for (int i = 0; i < 8; ++i) {
                if (name == names[i]) return i;
            }
            return CH_NONE;
        }

        std::string rectNote(const WidgetInstance* w)
        {
            if (!w) return "nothing selected";
            return w->id + " at " + std::to_string(w->x) + "," + std::to_string(w->y) + " " +
                   std::to_string(w->w) + "x" + std::to_string(w->h);
        }
    }

    std::string EditorCommand::tail(size_t n) const
    {
        std::istringstream in(rest);
        std::string word;
        for (size_t i = 0; i < n && (in >> word); ++i) {}
        std::string text;
        std::getline(in, text);
        return trim(text);
    }

    EditorCommand parseEditorCommand(const std::string& line)
    {
        EditorCommand c;
        std::string text = trim(line);
        if (text.empty() || text[0] == '#') return c;
        std::istringstream in(text);
        in >> c.name;
        std::getline(in, c.rest);
        c.rest = trim(c.rest);
        std::istringstream words(c.rest);
        std::string word;
        while (words >> word) c.words.push_back(word);
        return c;
    }

    std::vector<EditorCommand> parseEditorScript(const std::string& text)
    {
        std::vector<EditorCommand> out;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            EditorCommand c = parseEditorCommand(line);
            if (!c.name.empty()) out.push_back(c);
        }
        return out;
    }

    CommandResult runSessionCommand(EditorSession& s, const EditorCommand& c, std::string& note)
    {
        note.clear();
        auto result = [](bool ok) { return ok ? CommandResult::Ok : CommandResult::Failed; };
        const std::string& n = c.name;
        const int cw = s.project().canvasW();
        const int ch = s.project().canvasH();
        // Theme units: a canvas drawn at 1:1
        const CanvasFit unit = canvasFit(static_cast<float>(cw), static_cast<float>(ch), cw, ch);

        if (n == "page") { int i = -1; return result(integer(c.words, 0, i) && (s.setPage(i), s.pageIndex() == i)); }
        if (n == "select") { int i = -2; return result(integer(c.words, 0, i) && (s.select(i), s.selected() == i)); }
        if (n == "selectid") return result(s.selectId(c.rest));
        if (n == "add") {
            bool ok = !c.words.empty() && s.addWidget(c.words[0]);
            note = rectNote(s.selectedWidget());
            return result(ok);
        }
        if (n == "click") {
            float x = 0, y = 0;
            if (!number(c.words, 0, x) || !number(c.words, 1, y)) return CommandResult::Failed;
            s.pointerDown(unit, x, y, 0.5f);
            s.pointerUp();
            note = "selected " + std::to_string(s.selected());
            return CommandResult::Ok;
        }
        if (n == "move" || n == "resize") {
            const WidgetInstance* w = s.selectedWidget();
            if (!w) return CommandResult::Failed;
            DragRect r;
            canvasHitRect(*w, r.x, r.y, r.w, r.h);
            float sx = r.x + r.w * 0.5f, sy = r.y + r.h * 0.5f;
            size_t first = 0;
            if (n == "resize") {
                int handle = c.words.empty() ? CH_NONE : handleFromName(c.words[0]);
                if (handle == CH_NONE) return CommandResult::Failed;
                const float x1 = static_cast<float>(r.x), y1 = static_cast<float>(r.y);
                const float x2 = static_cast<float>(r.x + r.w), y2 = static_cast<float>(r.y + r.h);
                const float hx[8] = { x1, (x1 + x2) * 0.5f, x2, x1, x2, x1, (x1 + x2) * 0.5f, x2 };
                const float hy[8] = { y1, y1, y1, (y1 + y2) * 0.5f, (y1 + y2) * 0.5f, y2, y2, y2 };
                sx = hx[handle];
                sy = hy[handle];
                first = 1;
            }
            float dx = 0, dy = 0;
            if (!number(c.words, first, dx) || !number(c.words, first + 1, dy)) return CommandResult::Failed;
            s.pointerDown(unit, sx, sy, 0.5f);
            s.pointerMove(unit, sx + dx, sy + dy);
            s.pointerUp();
            note = rectNote(s.selectedWidget());
            return CommandResult::Ok;
        }
        if (n == "nudge") {
            int dx = 0, dy = 0;
            if (!integer(c.words, 0, dx) || !integer(c.words, 1, dy)) return CommandResult::Failed;
            s.endCoalesce();   // each scripted nudge is its own step
            bool ok = s.moveBy(dx, dy);
            note = rectNote(s.selectedWidget());
            return result(ok);
        }
        if (n == "name") return result(s.setName(c.rest));
        if (n == "layer") { int i = 0; return result(integer(c.words, 0, i) && s.setLayer(i)); }
        if (n == "alpha") {
            int pct = 0;
            if (!alphaPercent(c.words, pct)) {
                note = "alpha takes 0-1 (e.g. 0.8; 1 is opaque) or a percent (2-100, or 80%), not \"" + c.rest + "\"";
                return CommandResult::Failed;
            }
            const WidgetInstance* w = s.selectedWidget();
            if (!w) { note = "nothing selected"; return CommandResult::Failed; }
            if (w->alphaPct == pct) { note = "already " + std::to_string(pct) + "%"; return CommandResult::Ok; }
            bool ok = s.setAlpha(pct);
            note = std::to_string(pct) + "%";
            return result(ok);
        }
        if (n == "colour" || n == "color") return result(s.setFontColor(c.rest));
        if (n == "var") return result(!c.words.empty() && s.setVar(c.words[0], c.tail(1)));
        if (n == "delete") return result(s.removeSelected());
        if (n == "undo") return result(s.undo());
        if (n == "redo") return result(s.redo());

        // Templates. "default" names the Default page, "blank" an empty seed
        auto primary = [](const std::string& word) { return word == "default" || word == "-" ? std::string() : word; };
        if (n == "tplnew") {
            if (c.words.size() < 3) return CommandResult::Failed;
            const std::string basedOn = c.words[1] == "blank" ? std::string(EditorSession::BLANK) : primary(c.words[1]);
            note = s.templateProblem(c.words[0], c.tail(2));
            bool ok = s.newTemplate(c.words[0], c.tail(2), basedOn);
            if (ok) note = "editing page " + std::to_string(s.pageIndex()) + ": " + s.pageLabel(s.pageIndex());
            return result(ok);
        }
        if (n == "tplassign") {
            if (c.words.size() < 2) return CommandResult::Failed;
            note = s.removesTemplate(c.words[0]).empty() ? "" : "removes the template " + s.removesTemplate(c.words[0]);
            return result(s.assignTemplate(c.words[0], primary(c.words[1])));
        }
        if (n == "tpldelete") {
            if (c.words.empty()) return CommandResult::Failed;
            return result(s.deleteTemplate(c.words[0], c.words.size() > 1 ? primary(c.words[1]) : std::string()));
        }
        if (n == "tplrename") return result(c.words.size() >= 2 && s.renameTemplate(c.words[0], c.tail(1)));

        // Theme properties (paths already in the theme; the screen's import* commands copy files in)
        if (n == "font") return result(s.setThemeFont(c.rest == "default" ? std::string() : c.rest));
        if (n == "sound") {
            if (c.words.empty()) return CommandResult::Failed;
            const std::string path = c.tail(1);
            return result(s.setSound(c.words[0], path == "none" ? std::string() : path));
        }
        if (n == "splashtext") return result(s.setSplashText(c.rest));
        if (n == "splashbg") return result(s.setSplashBackground(c.rest == "none" ? std::string() : c.rest));
        return CommandResult::NotSession;
    }

    CommandResult runLayoutCommand(LayoutSession& s, const EditorCommand& c, std::string& note)
    {
        note.clear();
        auto result = [](bool ok) { return ok ? CommandResult::Ok : CommandResult::Failed; };
        auto boxNote = [&s]() {
            const LayoutElement* e = s.selectedElement();
            if (!e) return std::string("nothing selected");
            std::string text = s.label(s.selected()) + " [" + e->path + "]";
            if (s.hasBox(s.selected())) {
                DragRect r = s.box(s.selected());
                text += " at " + std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h);
            }
            return text;
        };
        const std::string& n = c.name;
        const int cw = s.canvasW();
        const int ch = s.canvasH();
        const CanvasFit unit = canvasFit(static_cast<float>(cw), static_cast<float>(ch), cw, ch);

        if (n == "select") {
            int i = -1;
            std::vector<int> listed = s.listed();
            if (!integer(c.words, 0, i) || i < 0 || i >= static_cast<int>(listed.size())) return CommandResult::Failed;
            s.select(listed[i]);
            note = boxNote();
            return CommandResult::Ok;
        }
        if (n == "selectpath") {
            bool ok = s.selectPath(c.rest == "-" ? std::string() : c.rest);
            note = boxNote();
            return result(ok);
        }
        if (n == "click") {
            float x = 0, y = 0;
            if (!number(c.words, 0, x) || !number(c.words, 1, y)) return CommandResult::Failed;
            s.pointerDown(unit, x, y, 0.5f);
            s.pointerUp();
            note = boxNote();
            return CommandResult::Ok;
        }
        if (n == "move" || n == "resize") {
            if (!s.editableBox(s.selected())) return CommandResult::Failed;
            DragRect r = s.box(s.selected());
            float sx = r.x + r.w * 0.5f, sy = r.y + r.h * 0.5f;
            size_t first = 0;
            if (n == "resize") {
                int handle = c.words.empty() ? CH_NONE : handleFromName(c.words[0]);
                if (handle == CH_NONE) return CommandResult::Failed;
                const float x1 = static_cast<float>(r.x), y1 = static_cast<float>(r.y);
                const float x2 = static_cast<float>(r.x + r.w), y2 = static_cast<float>(r.y + r.h);
                const float hx[8] = { x1, (x1 + x2) * 0.5f, x2, x1, x2, x1, (x1 + x2) * 0.5f, x2 };
                const float hy[8] = { y1, y1, y1, (y1 + y2) * 0.5f, (y1 + y2) * 0.5f, y2, y2, y2 };
                sx = hx[handle];
                sy = hy[handle];
                first = 1;
            }
            float dx = 0, dy = 0;
            if (!number(c.words, first, dx) || !number(c.words, first + 1, dy)) return CommandResult::Failed;
            const unsigned before = s.revision();
            s.pointerDown(unit, sx, sy, 0.5f);
            s.pointerMove(unit, sx + dx, sy + dy);
            s.pointerUp();
            note = boxNote();
            return result(s.revision() != before);
        }
        if (n == "nudge") {
            int dx = 0, dy = 0;
            if (!integer(c.words, 0, dx) || !integer(c.words, 1, dy)) return CommandResult::Failed;
            s.endCoalesce();
            bool ok = s.moveBy(dx, dy);
            note = boxNote();
            return result(ok);
        }
        if (n == "attr") {
            if (c.words.empty()) return CommandResult::Failed;
            s.endCoalesce();
            return result(s.setAttribute(c.words[0], c.tail(1)));
        }
        if (n == "rmattr") return result(!c.words.empty() && s.removeAttribute(c.words[0]));
        if (n == "get") {
            const LayoutElement* e = s.selectedElement();
            if (!e || c.words.empty()) return CommandResult::Failed;
            note = c.words[0] + (e->has(c.words[0]) ? "=\"" + e->value(c.words[0]) + "\"" : " not set");
            return CommandResult::Ok;
        }
        if (n == "layer") { int i = 0; s.endCoalesce(); return result(integer(c.words, 0, i) && s.setLayer(i)); }
        if (n == "duplicate") { bool ok = s.duplicateSelected(); note = boxNote(); return result(ok); }
        if (n == "delete") return result(s.deleteSelected());
        if (n == "undo") return result(s.undo());
        if (n == "redo") return result(s.redo());
        if (n == "snap") { s.snap = c.rest != "off"; return CommandResult::Ok; }

        // Adding elements and editing animations
        if (n == "add") {
            if (c.words.empty()) return CommandResult::Failed;
            const std::string type = c.words.size() > 1 && c.words[1] != "-" ? c.words[1] : std::string();
            bool ok = s.addElement(c.words[0], type, c.tail(2));
            note = boxNote();
            return result(ok);
        }
        if (n == "event") return result(!c.words.empty() && s.addEvent(c.words[0]));
        if (n == "addset") return result(!c.words.empty() && s.addSet(s.findInSelected(c.words[0])));
        if (n == "addanimate") return result(!c.words.empty() && s.addAnimate(s.findInSelected(c.words[0])));
        if (n == "rmanim") return result(!c.words.empty() && s.removeAnimation(s.findInSelected(c.words[0])));
        if (n == "animattr") {
            if (c.words.size() < 2) return CommandResult::Failed;
            s.endCoalesce();
            return result(s.setAttributeOf(s.findInSelected(c.words[0]), c.words[1], c.tail(2)));
        }
        if (n == "rmanimattr") return result(c.words.size() >= 2 && s.removeAttributeOf(s.findInSelected(c.words[0]), c.words[1]));
        if (n == "getanim") {
            if (c.words.size() < 2) return CommandResult::Failed;
            int el = s.findInSelected(c.words[0]);
            if (el < 0) return CommandResult::Failed;
            const LayoutElement& e = s.element(el);
            note = c.words[1] + (e.has(c.words[1]) ? "=\"" + e.value(c.words[1]) + "\"" : " not set");
            return CommandResult::Ok;
        }
        if (n == "copyanims") {
            bool ok = s.copyAnimationsFrom(s.doc().find(c.rest));
            note = boxNote();
            return result(ok);
        }
        if (n == "events") {
            for (const std::string& a : s.animations(s.selected())) note += (note.empty() ? "" : "; ") + a;
            return result(s.selected() >= 0);
        }
        if (n == "endbox") {
            DragRect r;
            if (!s.enterEndBox(s.selected(), r)) { note = "no end box"; return CommandResult::Failed; }
            note = "ends at " + std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h);
            return CommandResult::Ok;
        }
        return CommandResult::NotSession;
    }
}

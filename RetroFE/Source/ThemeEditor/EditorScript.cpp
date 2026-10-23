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
        if (n == "alpha") { int i = 0; return result(integer(c.words, 0, i) && s.setAlpha(i)); }
        if (n == "colour" || n == "color") return result(s.setFontColor(c.rest));
        if (n == "var") return result(!c.words.empty() && s.setVar(c.words[0], c.tail(1)));
        if (n == "delete") return result(s.removeSelected());
        if (n == "undo") return result(s.undo());
        if (n == "redo") return result(s.redo());
        return CommandResult::NotSession;
    }
}

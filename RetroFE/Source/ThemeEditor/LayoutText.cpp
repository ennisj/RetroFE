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
#include "LayoutText.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

namespace ThemeEditor
{
    namespace
    {
        bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
        // What rapidxml takes as a name character (element and attribute names)
        bool isNameChar(char c) { return !isSpace(c) && c != '/' && c != '>' && c != '?' && c != '=' && c != '<' && c != '\0'; }

        void appendUtf8(std::string& out, unsigned long code)
        {
            if (code < 0x80) out += static_cast<char>(code);
            else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
            else if (code < 0x10000) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
            else {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
        }

        std::string at(const std::string& text, size_t offset)
        {
            return "line " + std::to_string(lineOf(text, offset));
        }

        // A UTF-8 byte order mark's length at the start of the text (0 or 3)
        size_t bomLength(const std::string& text)
        {
            return text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
                           static_cast<unsigned char>(text[2]) == 0xBF
                       ? 3
                       : 0;
        }

        // The start of offset's line, and whether only whitespace comes before
        // it there. A byte order mark isn't part of the first line.
        size_t lineStart(const std::string& text, size_t offset, bool& onlySpace)
        {
            size_t s = offset;
            onlySpace = true;
            const size_t first = std::min(bomLength(text), offset);
            while (s > first && text[s - 1] != '\n') {
                if (!isSpace(text[s - 1])) onlySpace = false;
                --s;
            }
            return s;
        }

        // Just past the end of offset's line (its line ending included), and
        // whether only whitespace comes after offset there
        size_t lineEnd(const std::string& text, size_t offset, bool& onlySpace)
        {
            size_t e = offset;
            onlySpace = true;
            while (e < text.size() && text[e] != '\n') {
                if (!isSpace(text[e])) onlySpace = false;
                ++e;
            }
            if (e < text.size()) ++e;   // the '\n' (a '\r' before it was skipped as space)
            return e;
        }

        // The file's own line ending: CRLF when its first line break is one
        std::string newlineOf(const std::string& text)
        {
            size_t nl = text.find('\n');
            if (nl != std::string::npos && nl > 0 && text[nl - 1] == '\r') return "\r\n";
            return "\n";
        }

        // The line ending of offset's line (a file can mix them); the file's
        // when that line has none
        std::string newlineAt(const std::string& text, size_t offset)
        {
            size_t nl = text.find('\n', offset);
            if (nl == std::string::npos) return newlineOf(text);
            return nl > 0 && text[nl - 1] == '\r' ? "\r\n" : "\n";
        }
    }

    const LayoutAttr* LayoutElement::attr(const std::string& n) const
    {
        for (const LayoutAttr& a : attrs) {
            if (a.name == n) return &a;
        }
        return nullptr;
    }

    std::string LayoutElement::value(const std::string& n, const std::string& fallback) const
    {
        const LayoutAttr* a = attr(n);
        return a ? a->value : fallback;
    }

    int LayoutDoc::find(const std::string& path) const
    {
        for (size_t i = 0; i < elements.size(); ++i) {
            if (elements[i].path == path) return static_cast<int>(i);
        }
        return -1;
    }

    int lineOf(const std::string& text, size_t offset)
    {
        int line = 1;
        for (size_t i = 0; i < offset && i < text.size(); ++i) {
            if (text[i] == '\n') ++line;
        }
        return line;
    }

    std::string xmlDecode(const std::string& raw)
    {
        if (raw.find('&') == std::string::npos) return raw;
        std::string out;
        out.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '&') { out += raw[i]; continue; }
            size_t semi = raw.find(';', i);
            if (semi == std::string::npos || semi - i > 12) { out += raw[i]; continue; }
            std::string name = raw.substr(i + 1, semi - i - 1);
            if (name == "lt") out += '<';
            else if (name == "gt") out += '>';
            else if (name == "amp") out += '&';
            else if (name == "quot") out += '"';
            else if (name == "apos") out += '\'';
            else if (name.size() > 1 && name[0] == '#') {
                const bool hex = name[1] == 'x' || name[1] == 'X';
                const char* digits = name.c_str() + (hex ? 2 : 1);
                char* end = nullptr;
                unsigned long code = std::strtoul(digits, &end, hex ? 16 : 10);
                if (!end || *end != '\0' || end == digits) { out += raw[i]; continue; }
                appendUtf8(out, code);
            }
            else { out += raw[i]; continue; }
            i = semi;
        }
        return out;
    }

    std::string xmlEncodeAttr(const std::string& value, char quote)
    {
        std::string out;
        out.reserve(value.size());
        for (char c : value) {
            if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == quote) out += quote == '"' ? "&quot;" : "&apos;";
            else out += c;
        }
        return out;
    }

    bool parseLayoutText(const std::string& text, LayoutDoc& doc, std::string& error)
    {
        doc = LayoutDoc();
        doc.text = text;
        std::vector<LayoutElement>& els = doc.elements;
        std::vector<int> stack;
        // Per parent, how many children of each name so far (for the paths)
        std::vector<std::map<std::string, int>> counts(1);   // [0]: top level
        std::vector<int> countSlot;                         // element -> its index in counts
        int topLevel = 0;

        size_t i = 0;
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF) {
            i = 3;
        }
        while (i < text.size()) {
            size_t lt = text.find('<', i);
            if (lt == std::string::npos) break;
            i = lt;
            if (text.compare(i, 4, "<!--") == 0) {
                size_t e = text.find("-->", i + 4);
                if (e == std::string::npos) { error = "A comment isn't closed (" + at(text, i) + ")."; return false; }
                i = e + 3;
                continue;
            }
            if (text.compare(i, 9, "<![CDATA[") == 0) {
                size_t e = text.find("]]>", i + 9);
                if (e == std::string::npos) { error = "A CDATA section isn't closed (" + at(text, i) + ")."; return false; }
                i = e + 3;
                continue;
            }
            if (text.compare(i, 2, "<?") == 0) {
                size_t e = text.find("?>", i + 2);
                if (e == std::string::npos) { error = "A <? ... ?> isn't closed (" + at(text, i) + ")."; return false; }
                i = e + 2;
                continue;
            }
            if (text.compare(i, 2, "<!") == 0) {
                // <!DOCTYPE ...>, with an internal subset in [ ] perhaps
                int depth = 0;
                size_t e = i + 2;
                for (; e < text.size(); ++e) {
                    if (text[e] == '[') ++depth;
                    else if (text[e] == ']') --depth;
                    else if (text[e] == '>' && depth <= 0) break;
                }
                if (e >= text.size()) { error = "A <! ... > isn't closed (" + at(text, i) + ")."; return false; }
                i = e + 1;
                continue;
            }
            if (text.compare(i, 2, "</") == 0) {
                size_t n = i + 2;
                while (n < text.size() && isNameChar(text[n])) ++n;
                std::string name = text.substr(i + 2, n - i - 2);
                while (n < text.size() && isSpace(text[n])) ++n;
                if (n >= text.size() || text[n] != '>') { error = "An end tag </" + name + " isn't closed (" + at(text, i) + ")."; return false; }
                if (stack.empty()) { error = "</" + name + "> closes nothing (" + at(text, i) + ")."; return false; }
                // As rapidxml (parse<0>, what PageBuilder uses): an end tag closes
                // the innermost open element whatever it's called. Shipped
                // layouts rely on it (<onMenuScroll>...</onMenuJumpScroll>).
                els[stack.back()].end = n + 1;
                stack.pop_back();
                i = n + 1;
                continue;
            }

            // A start tag
            LayoutElement el;
            el.begin = i;
            size_t n = i + 1;
            while (n < text.size() && isNameChar(text[n])) ++n;
            el.name = text.substr(i + 1, n - i - 1);
            el.nameEnd = n;
            if (el.name.empty()) { error = "A '<' that doesn't start a tag (" + at(text, i) + ")."; return false; }
            bool closed = false;
            while (n < text.size()) {
                size_t ws = n;
                while (n < text.size() && isSpace(text[n])) ++n;
                if (n >= text.size()) break;
                if (text[n] == '>') { el.startEnd = n + 1; closed = true; break; }
                if (text[n] == '/') {
                    if (n + 1 < text.size() && text[n + 1] == '>') {
                        el.selfClosing = true;
                        el.startEnd = n + 2;
                        closed = true;
                    }
                    break;
                }
                LayoutAttr a;
                a.begin = ws;
                a.nameBegin = n;
                while (n < text.size() && isNameChar(text[n])) ++n;
                a.name = text.substr(a.nameBegin, n - a.nameBegin);
                while (n < text.size() && isSpace(text[n])) ++n;
                if (a.name.empty() || n >= text.size() || text[n] != '=') {
                    error = "<" + el.name + "> has an attribute without a value (" + at(text, a.nameBegin) + ").";
                    return false;
                }
                ++n;
                while (n < text.size() && isSpace(text[n])) ++n;
                if (n >= text.size() || (text[n] != '"' && text[n] != '\'')) {
                    error = "<" + el.name + "> " + a.name + "= has no quoted value (" + at(text, a.nameBegin) + ").";
                    return false;
                }
                a.quote = text[n];
                a.valueBegin = n + 1;
                size_t q = text.find(a.quote, a.valueBegin);
                if (q == std::string::npos) {
                    error = "<" + el.name + "> " + a.name + "= isn't closed (" + at(text, a.nameBegin) + ").";
                    return false;
                }
                a.valueEnd = q;
                a.value = xmlDecode(text.substr(a.valueBegin, q - a.valueBegin));
                el.attrs.push_back(a);
                n = q + 1;
            }
            if (!closed) { error = "<" + el.name + "> isn't closed (" + at(text, i) + ")."; return false; }

            // Where it sits, and its path
            const int index = static_cast<int>(els.size());
            el.parent = stack.empty() ? -1 : stack.back();
            el.depth = static_cast<int>(stack.size());
            int slot = el.parent < 0 ? 0 : countSlot[el.parent];
            int nth = counts[slot][el.name]++;
            if (el.parent < 0) {
                el.path = topLevel++ == 0 ? "" : "#" + el.name + "[" + std::to_string(nth) + "]";
            }
            else {
                const std::string& parentPath = els[el.parent].path;
                el.path = (parentPath.empty() ? "" : parentPath + "/") + el.name + "[" + std::to_string(nth) + "]";
                els[el.parent].children.push_back(index);
            }
            counts.emplace_back();
            countSlot.push_back(static_cast<int>(counts.size()) - 1);
            if (el.selfClosing) el.end = el.startEnd;
            i = el.startEnd;
            els.push_back(el);
            if (!els.back().selfClosing) stack.push_back(index);
        }
        if (!stack.empty()) {
            error = "<" + els[stack.back()].name + "> (" + at(text, els[stack.back()].begin) + ") is never closed.";
            return false;
        }
        if (els.empty() || els[0].name != "layout") {
            error = els.empty() ? "There's no <layout> element." : "The first element is <" + els[0].name + ">, not <layout>.";
            return false;
        }
        return true;
    }

    std::string withAttribute(const LayoutDoc& doc, int element, const std::string& name, const std::string& value)
    {
        if (element < 0 || element >= static_cast<int>(doc.elements.size()) || name.empty()) return doc.text;
        const LayoutElement& el = doc.elements[element];
        std::string text = doc.text;
        if (const LayoutAttr* a = el.attr(name)) {
            if (a->value == value) return text;
            return text.replace(a->valueBegin, a->valueEnd - a->valueBegin, xmlEncodeAttr(value, a->quote));
        }
        // After the last attribute, spaced and quoted as it is
        size_t pos = el.nameEnd;
        std::string space = " ";
        char quote = '"';
        if (!el.attrs.empty()) {
            const LayoutAttr& last = el.attrs.back();
            pos = last.valueEnd + 1;
            std::string before = text.substr(last.begin, last.nameBegin - last.begin);
            if (before.find('\n') != std::string::npos) space = before;
            quote = last.quote;
        }
        std::string added = space + name + "=" + quote + xmlEncodeAttr(value, quote) + quote;
        return text.insert(pos, added);
    }

    std::string withoutAttribute(const LayoutDoc& doc, int element, const std::string& name)
    {
        if (element < 0 || element >= static_cast<int>(doc.elements.size())) return doc.text;
        const LayoutAttr* a = doc.elements[element].attr(name);
        if (!a) return doc.text;
        std::string text = doc.text;
        return text.erase(a->begin, a->valueEnd + 1 - a->begin);
    }

    std::string withoutElement(const LayoutDoc& doc, int element)
    {
        // The root stays: without it there's no page
        if (element <= 0 || element >= static_cast<int>(doc.elements.size())) return doc.text;
        const LayoutElement& el = doc.elements[element];
        bool spaceBefore = false, spaceAfter = false;
        size_t from = lineStart(doc.text, el.begin, spaceBefore);
        size_t to = lineEnd(doc.text, el.end, spaceAfter);
        std::string text = doc.text;
        if (spaceBefore && spaceAfter) return text.erase(from, to - from);
        return text.erase(el.begin, el.end - el.begin);
    }

    std::string withDuplicate(const LayoutDoc& doc, int element)
    {
        if (element <= 0 || element >= static_cast<int>(doc.elements.size())) return doc.text;
        const LayoutElement& el = doc.elements[element];
        bool spaceBefore = false, spaceAfter = false;
        size_t from = lineStart(doc.text, el.begin, spaceBefore);
        size_t to = lineEnd(doc.text, el.end, spaceAfter);
        std::string text = doc.text;
        if (spaceBefore && spaceAfter) {
            std::string copy = text.substr(from, to - from);
            // The last line of the file may have no line ending
            if (copy.empty() || copy.back() != '\n') {
                return text.insert(to, newlineOf(text) + copy);
            }
            return text.insert(to, copy);
        }
        return text.insert(el.end, " " + text.substr(el.begin, el.end - el.begin));
    }

    std::string duplicatePath(const std::string& path)
    {
        size_t open = path.rfind('[');
        size_t close = path.rfind(']');
        if (open == std::string::npos || close == std::string::npos || close < open) return path;
        int n = std::atoi(path.substr(open + 1, close - open - 1).c_str());
        return path.substr(0, open + 1) + std::to_string(n + 1) + path.substr(close);
    }

    // ---- inserting -------------------------------------------------------------

    namespace
    {
        bool startsLine(const std::string& text, size_t offset)
        {
            bool onlySpace = false;
            lineStart(text, offset, onlySpace);
            return onlySpace;
        }

        // Only whitespace and whole comments from offset to the end of its line
        bool restQuiet(const std::string& text, size_t offset)
        {
            size_t i = offset;
            while (i < text.size() && text[i] != '\n') {
                if (isSpace(text[i])) { ++i; continue; }
                if (text.compare(i, 4, "<!--") != 0) return false;
                size_t e = text.find("-->", i + 4);
                if (e == std::string::npos) return false;
                for (size_t k = i; k < e; ++k) {
                    if (text[k] == '\n') return false;   // a comment that runs on: not this line's
                }
                i = e + 3;
            }
            return true;
        }

        bool isEvent(const std::string& name)
        {
            return name.size() > 2 && name[0] == 'o' && name[1] == 'n' && name[2] >= 'A' && name[2] <= 'Z';
        }

        // The root, a component, a menu item: not an animation event or inside one
        bool pageElement(const LayoutDoc& doc, int i)
        {
            for (int e = i; e >= 0; e = doc.elements[e].parent) {
                if (isEvent(doc.elements[e].name)) return false;
            }
            return true;
        }

        std::vector<std::string> splitLines(const std::string& s)
        {
            std::vector<std::string> lines;
            size_t from = 0;
            while (true) {
                size_t nl = s.find('\n', from);
                std::string line = s.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
                if (nl == std::string::npos) break;
                from = nl + 1;
            }
            return lines;
        }

        // A fragment's lines in the file's style: the first as it is, the
        // rest indented by `indent` plus a unit per 4 spaces they had
        std::string placeFragment(const std::string& fragment, const std::string& indent, const std::string& unit, const std::string& nl)
        {
            std::vector<std::string> lines = splitLines(fragment);
            std::string out = lines[0];
            for (size_t i = 1; i < lines.size(); ++i) {
                const std::string& line = lines[i];
                size_t n = 0, spaces = 0;
                while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) {
                    spaces += line[n] == '\t' ? 4 : 1;
                    ++n;
                }
                out += nl;
                if (n == line.size()) continue;   // a blank line stays blank
                out += indent;
                for (size_t l = 0; l < spaces / 4; ++l) out += unit;
                out += std::string(spaces % 4, ' ');
                out += line.substr(n);
            }
            return out;
        }

        // A fragment on one line: its lines without their indentation, joined
        std::string joinFragment(const std::string& fragment)
        {
            std::string out;
            for (const std::string& line : splitLines(fragment)) {
                size_t n = 0;
                while (n < line.size() && isSpace(line[n])) ++n;
                out += line.substr(n);
            }
            return out;
        }

        // Space made only of blanks and tabs, on one line
        bool inlineSpace(const std::string& s)
        {
            for (char c : s) {
                if (c != ' ' && c != '\t') return false;
            }
            return true;
        }
    }

    std::string lineEnding(const std::string& text)
    {
        return newlineOf(text);
    }

    std::string indentationOf(const LayoutDoc& doc, int element)
    {
        if (element < 0 || element >= static_cast<int>(doc.elements.size())) return "";
        bool onlySpace = false;
        const size_t begin = doc.elements[element].begin;
        size_t s = lineStart(doc.text, begin, onlySpace);
        return onlySpace ? doc.text.substr(s, begin - s) : std::string();
    }

    std::string indentUnit(const LayoutDoc& doc)
    {
        std::map<std::string, int> counts;
        for (size_t i = 1; i < doc.elements.size(); ++i) {
            const LayoutElement& e = doc.elements[i];
            if (e.parent < 0) continue;
            const LayoutElement& p = doc.elements[e.parent];
            if (!startsLine(doc.text, e.begin) || !startsLine(doc.text, p.begin)) continue;
            if (lineOf(doc.text, e.begin) == lineOf(doc.text, p.begin)) continue;
            const std::string ci = indentationOf(doc, static_cast<int>(i));
            const std::string pi = indentationOf(doc, e.parent);
            if (ci.size() > pi.size() && ci.compare(0, pi.size(), pi) == 0) counts[ci.substr(pi.size())]++;
        }
        std::string best = "    ";
        int most = 0;
        for (const auto& entry : counts) {
            if (entry.second > most) { most = entry.second; best = entry.first; }
        }
        return best;
    }

    std::string fragmentOf(const LayoutDoc& doc, int element)
    {
        if (element < 0 || element >= static_cast<int>(doc.elements.size())) return "";
        const LayoutElement& e = doc.elements[element];
        const std::string own = indentationOf(doc, element);
        const std::string unit = indentUnit(doc);
        std::vector<std::string> lines = splitLines(doc.text.substr(e.begin, e.end - e.begin));
        std::string out = lines[0];
        for (size_t i = 1; i < lines.size(); ++i) {
            std::string line = lines[i];
            if (!own.empty() && line.compare(0, own.size(), own) == 0) line = line.substr(own.size());
            std::string lead;
            while (!unit.empty() && line.compare(0, unit.size(), unit) == 0) {
                lead += "    ";
                line = line.substr(unit.size());
            }
            out += "\n" + lead + line;
        }
        return out;
    }

    std::string withChild(const LayoutDoc& doc, int parent, const std::string& fragment, int after, size_t* at)
    {
        const int count = static_cast<int>(doc.elements.size());
        if (parent < 0 || parent >= count || fragment.empty()) return doc.text;
        if (after >= 0 && (after >= count || doc.elements[after].parent != parent)) return doc.text;
        const std::string& text = doc.text;
        const std::string unit = indentUnit(doc);
        const LayoutElement& p = doc.elements[parent];
        const int ref = after >= 0 ? after : (p.children.empty() ? -1 : p.children.back());

        size_t pos = 0, eraseTo = 0, offset = 0;
        std::string insert;
        if (ref >= 0) {
            const LayoutElement& r = doc.elements[ref];
            // The line ending of the sibling's own line
            const std::string nl = newlineAt(text, r.end);
            const std::string indent = indentationOf(doc, ref);
            const bool starts = startsLine(text, r.begin);
            // The sibling after it, when it's on the same line
            int next = -1;
            for (size_t k = 0; k + 1 < p.children.size(); ++k) {
                if (p.children[k] == ref) next = p.children[k + 1];
            }
            if (next >= 0 && text.find('\n', r.end) < doc.elements[next].begin) next = -1;
            if (next >= 0) {
                // Between two on one line: inline, spaced as they are
                const std::string between = text.substr(r.end, doc.elements[next].begin - r.end);
                const std::string sep = inlineSpace(between) ? between : " ";
                pos = r.end;
                insert = sep + joinFragment(fragment);
                offset = sep.size();
            }
            else if (starts && restQuiet(text, r.end)) {
                // A line of its own, after the sibling's line
                bool quiet = false;
                pos = lineEnd(text, r.end, quiet);
                std::string lead;
                if (pos == text.size() && (pos == 0 || text[pos - 1] != '\n')) lead = nl;
                insert = lead + indent + placeFragment(fragment, indent, unit, nl) + nl;
                offset = lead.size() + indent.size();
            }
            else if (starts) {
                // Something follows on its line (the parent's end tag): a new line right after it
                pos = r.end;
                insert = nl + indent + placeFragment(fragment, indent, unit, nl);
                offset = nl.size() + indent.size();
            }
            else {
                // On the sibling's line, spaced as the siblings are
                int prev = -1;
                for (size_t k = 0; k < p.children.size(); ++k) {
                    if (p.children[k] == ref) prev = k > 0 ? p.children[k - 1] : -1;
                }
                const size_t prevEnd = prev >= 0 ? doc.elements[prev].end : p.startEnd;
                const std::string between = text.substr(prevEnd, r.begin - prevEnd);
                std::string sep;
                if (prev >= 0) sep = inlineSpace(between) ? between : " ";
                else sep = between.empty() ? "" : " ";
                pos = r.end;
                insert = sep + joinFragment(fragment);
                offset = sep.size();
            }
            eraseTo = pos;
        }
        else {
            // The line ending of the parent's start tag's line
            const std::string nl = newlineAt(text, p.startEnd);
            const std::string indent = indentationOf(doc, parent);
            const std::string inner = indent + unit;
            const bool lines = startsLine(text, p.begin) && pageElement(doc, parent);
            if (p.selfClosing) {
                // <x a="1" /> becomes <x a="1">...</x>
                size_t from = p.startEnd - 2;
                while (from > p.nameEnd && isSpace(text[from - 1])) --from;
                const std::string close = "</" + p.name + ">";
                if (lines) {
                    insert = ">" + nl + inner + placeFragment(fragment, inner, unit, nl) + nl + indent + close;
                    offset = 1 + nl.size() + inner.size();
                }
                else {
                    insert = ">" + joinFragment(fragment) + close;
                    offset = 1;
                }
                pos = from;
                eraseTo = p.startEnd;
            }
            else {
                const size_t endTag = text.rfind("</", p.end - 1);
                if (endTag == std::string::npos || endTag < p.startEnd) return text;
                bool endStarts = false;
                const size_t endLine = lineStart(text, endTag, endStarts);
                if (lines && endStarts) {
                    pos = endLine;
                    insert = inner + placeFragment(fragment, inner, unit, nl) + nl;
                    offset = inner.size();
                }
                else if (lines) {
                    pos = endTag;
                    insert = nl + inner + placeFragment(fragment, inner, unit, nl) + nl + indent;
                    offset = nl.size() + inner.size();
                }
                else {
                    pos = endTag;
                    insert = joinFragment(fragment);
                    offset = 0;
                }
                eraseTo = pos;
            }
        }
        if (at) *at = pos + offset;
        std::string out = text;
        return out.replace(pos, eraseTo - pos, insert);
    }
}

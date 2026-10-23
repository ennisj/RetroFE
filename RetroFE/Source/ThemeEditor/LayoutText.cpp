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

        // The start of offset's line, and whether only whitespace comes before it there
        size_t lineStart(const std::string& text, size_t offset, bool& onlySpace)
        {
            size_t s = offset;
            onlySpace = true;
            while (s > 0 && text[s - 1] != '\n') {
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
}

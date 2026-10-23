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
#pragma once

// A layout file as text, for editing a hand-made layout in place. rapidxml
// (what PageBuilder parses with) drops comments and whitespace and can't
// write a document back as it was, so the layout-native editor never
// re-serialises: it scans the original text for elements and attributes,
// remembering where each one is, and an edit replaces, inserts or removes
// exactly those bytes. Everything else (comments, indentation, attribute
// order, quote style, CRLF, entities in other values) stays byte for byte.
//
// Elements are named by a path: the steps from the <layout> root, each the
// element's name and its index among its parent's children of that name,
// e.g. "image[3]" or "menu[0]/item[2]"; the root itself is "". PageBuilder
// records the same path for each component it builds
// (Graphics/LayoutNodePath.h), which is how a box on the preview finds its
// element here.
//
// Pure: no SDL, no filesystem.

#include <cstddef>
#include <string>
#include <vector>

namespace ThemeEditor
{
    struct LayoutAttr
    {
        std::string name;
        std::string value;        // decoded (entities replaced)
        size_t begin = 0;         // where the whitespace before the name starts
        size_t nameBegin = 0;
        size_t valueBegin = 0;    // just after the opening quote
        size_t valueEnd = 0;      // the closing quote
        char quote = '"';
    };

    struct LayoutElement
    {
        std::string name;
        std::string path;
        int parent = -1;
        int depth = 0;            // 0 = the root
        std::vector<int> children;
        size_t begin = 0;         // '<'
        size_t nameEnd = 0;       // just after the name
        size_t startEnd = 0;      // just after the start tag's '>'
        size_t end = 0;           // just after the element (its end tag, or the start tag when self-closing)
        bool selfClosing = false;
        std::vector<LayoutAttr> attrs;

        // The first attribute with this name (as rapidxml's first_attribute), else null
        const LayoutAttr* attr(const std::string& n) const;
        std::string value(const std::string& n, const std::string& fallback = "") const;
        bool has(const std::string& n) const { return attr(n) != nullptr; }
    };

    struct LayoutDoc
    {
        std::string text;
        std::vector<LayoutElement> elements;   // document order; [0] is the <layout> root

        // The element at a path, else -1
        int find(const std::string& path) const;
    };

    // Scans a layout file. False, with a reason that names the line, for text
    // that isn't well-formed (an unclosed tag, a mismatched end tag, an
    // attribute without a quoted value) or whose root isn't <layout>.
    bool parseLayoutText(const std::string& text, LayoutDoc& doc, std::string& error);

    // Entities in an attribute value: &lt; &gt; &amp; &quot; &apos; &#N; &#xN;
    // (anything else is left as it is, as rapidxml does)
    std::string xmlDecode(const std::string& raw);
    // A value ready to go between `quote`s: & < and the quote escaped
    std::string xmlEncodeAttr(const std::string& value, char quote);

    // Edits: each returns the whole new text. Only the bytes named change.
    //
    // Sets an attribute's value: an existing one keeps its place and quotes;
    // a new one goes after the last attribute, with the same spacing before
    // it (a newline and indentation when the tag's attributes are one per
    // line) and the same quotes.
    std::string withAttribute(const LayoutDoc& doc, int element, const std::string& name, const std::string& value);
    // Removes an attribute and the whitespace before it
    std::string withoutAttribute(const LayoutDoc& doc, int element, const std::string& name);
    // Removes an element. One alone on its lines goes with its lines (its
    // indentation and line ending); otherwise only its own text.
    std::string withoutElement(const LayoutDoc& doc, int element);
    // Copies an element right after itself, on lines of its own when it's
    // alone on its lines. The copy's path is the original's with the index
    // one higher (duplicatePath).
    std::string withDuplicate(const LayoutDoc& doc, int element);
    std::string duplicatePath(const std::string& path);

    // The line (1-based) a byte offset is on
    int lineOf(const std::string& text, size_t offset);
}

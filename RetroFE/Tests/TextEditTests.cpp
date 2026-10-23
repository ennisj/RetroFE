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

// Tests for multi-line text fields (Forms/Form.h): the <text multiline="yes">
// attribute, the cursor, and how a text is laid out in lines and moved through.

#include "TestSupport.h"
#include "../Source/Forms/Form.h"
#include <string>
#include <vector>

using namespace Test;

namespace
{
    // Every character is 1 wide, so width counts characters
    float measure(const std::string& text)
    {
        float width = 0;
        for (unsigned char c : text) {
            width += (c & 0xC0) == 0x80 ? 0.0f : 1.0f;
        }
        return width;
    }

    std::vector<std::string> shown(const std::string& text, const std::vector<Forms::TextLine>& lines)
    {
        std::vector<std::string> result;
        for (const Forms::TextLine& line : lines) {
            result.push_back(text.substr(line.start, line.end - line.start));
        }
        return result;
    }

    std::string join(const std::vector<std::string>& parts)
    {
        std::string text;
        for (const std::string& part : parts) {
            text += "[" + part + "]";
        }
        return text;
    }
}

static void runTextEditTests();
static Test::Suite textEditSuite("text editing", runTextEditTests);

static void runTextEditTests()
{
    run("text editing: multiline is a text field's attribute", [] {
        std::string error;
        auto form = Forms::parse(R"(<form id="f"><text label="Story" bind="x:story" multiline="yes" rows="6"/>
                                     <text label="Name" bind="x:name"/><note value="n" multiline="yes"/></form>)", error);
        check(form && form->fields.size() == 3, "parsed: " + error);
        if (form && form->fields.size() == 3) {
            check(form->fields[0].multiline && form->fields[0].rows == 6, "multi-line, 6 rows");
            check(!form->fields[1].multiline, "one line by default");
            check(!form->fields[2].multiline, "only text fields have lines");
        }
    });

    run("text editing: the cursor inserts and deletes whole characters", [] {
        Forms::TextEdit edit;
        edit.set("abc");
        check(edit.cursor == 3, "starts at the end");
        edit.left();
        edit.insert("X");
        checkEqual(edit.text, "abXc", "typed at the cursor");
        edit.backspace();
        edit.backspace();
        checkEqual(edit.text, "ac", "backspace removes before the cursor");
        edit.erase();
        checkEqual(edit.text, "a", "delete removes at the cursor");
        edit.set("h\xC3\xA9");   // "hé": the é is two bytes
        edit.left();
        check(edit.cursor == 1, "left steps over a whole character");
        edit.right();
        check(edit.cursor == 3, "and right");
        edit.backspace();
        checkEqual(edit.text, "h", "backspace takes the whole character");
        edit.set("");
        edit.backspace();
        edit.erase();
        edit.left();
        check(edit.text.empty() && edit.cursor == 0, "nothing to do at the edges");
    });

    run("text editing: lines wrap at spaces and break at newlines", [] {
        std::string text = "the quick brown fox";
        checkEqual(join(shown(text, Forms::layoutLines(text, 10, measure))), "[the quick ][brown fox]", "spaces stay at the line's end");
        checkEqual(join(shown(text, Forms::layoutLines(text, 100, measure))), "[the quick brown fox]", "fits on one line");

        text = "one\n\ntwo\n";
        checkEqual(join(shown(text, Forms::layoutLines(text, 10, measure))), "[one][][two][]", "every newline starts a line; a final one leaves an empty line");
        checkEqual(join(shown("", Forms::layoutLines("", 10, measure))), "[]", "an empty text is one empty line");

        text = "abcdefghijkl xy";
        checkEqual(join(shown(text, Forms::layoutLines(text, 5, measure))), "[abcde][fghij][kl xy]", "a word too long is cut where it reaches the edge");
        text = "  indented words";
        checkEqual(join(shown(text, Forms::layoutLines(text, 10, measure))), "[  indented ][words]", "leading spaces are kept");
        text = "ab    ";
        checkEqual(join(shown(text, Forms::layoutLines(text, 3, measure))), "[ab    ]", "trailing spaces don't make a line of their own");

        // Every byte is on exactly one line, in order, except the newlines
        text = "Pac-Man | Namco\n  1980 arcade classic with a very long line that wraps several times\n";
        std::vector<Forms::TextLine> lines = Forms::layoutLines(text, 12, measure);
        std::string rebuilt;
        for (size_t i = 0; i < lines.size(); i++) {
            rebuilt += text.substr(lines[i].start, lines[i].end - lines[i].start);
            if (i + 1 < lines.size() && lines[i + 1].start != lines[i].end) {
                rebuilt += "\n";
            }
            check(measure(text.substr(lines[i].start, lines[i].end - lines[i].start)) <= 12
                  || text[lines[i].end - 1] == ' ', "line " + std::to_string(i) + " fits, but for its trailing spaces");
        }
        checkEqual(rebuilt, text, "the lines give back the text");
    });

    run("text editing: the cursor moves through lines", [] {
        std::string text = "the quick brown fox";
        std::vector<Forms::TextLine> lines = Forms::layoutLines(text, 10, measure);   // [the quick ][brown fox]
        check(Forms::lineAt(lines, 0) == 0 && Forms::lineAt(lines, 9) == 0, "on the first line");
        check(Forms::lineAt(lines, 10) == 1, "where a line wraps, the cursor is at the start of the next");
        check(Forms::moveCursorVertically(text, lines, 4, 1, measure) == 14, "down keeps the column: 'q' -> after 'brow'");
        check(Forms::moveCursorVertically(text, lines, 14, -1, measure) == 4, "and up");
        check(Forms::moveCursorVertically(text, lines, 18, -1, measure) == 8, "up stays before the wrap");
        check(Forms::moveCursorVertically(text, lines, 4, -1, measure) == 0, "up from the first line: the start");
        check(Forms::moveCursorVertically(text, lines, 12, 1, measure) == text.size(), "down from the last line: the end");
        check(Forms::lineHome(lines, 14) == 10 && Forms::lineEnd(text, lines, 14) == text.size(), "home and end of the last line");
        check(Forms::lineEnd(text, lines, 2) == 9, "end of a wrapped line is before its space");

        text = "long line here\nab\n";
        lines = Forms::layoutLines(text, 100, measure);   // [long line here][ab][]
        check(Forms::moveCursorVertically(text, lines, 10, 1, measure) == 17, "down to a shorter line: its end");
        check(Forms::moveCursorVertically(text, lines, 17, 1, measure) == 18, "down to the empty last line");
        check(Forms::lineAt(lines, 14) == 0 && Forms::lineAt(lines, 15) == 1, "the newline belongs to the line before it");
    });
}

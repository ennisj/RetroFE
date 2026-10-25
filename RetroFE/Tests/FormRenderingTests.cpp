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

// The renderer-independent parts of drawing forms: cutting text short,
// wrapping it, sharing a row, where focus goes, and noticing replaced pictures

#include "TestSupport.h"
#include "../Source/Forms/Form.h"
#include "../Source/Utility/FileStamp.h"
#include <chrono>
#include <filesystem>

using Test::check;
using Test::checkEqual;
using Test::run;

namespace
{
    // Every character 10 units wide, a UTF-8 character counting once
    float measure(const std::string& text)
    {
        float width = 0.0f;
        for (char c : text) {
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
                width += 10.0f;
            }
        }
        return width;
    }

    std::string joined(const std::vector<std::string>& lines)
    {
        std::string all;
        for (const std::string& line : lines) {
            all += (all.empty() ? "" : "|") + line;
        }
        return all;
    }
}

static Test::Suite renderingSuite("rendering", [] {
    using Forms::FocusTarget;

    run("ellipsize keeps text that fits", [] {
        checkEqual(Forms::ellipsize("Own file", 80.0f, measure), "Own file", "exactly as wide");
        checkEqual(Forms::ellipsize("", 0.0f, measure), "", "empty");
    });

    run("ellipsize cuts at a whole character and adds ...", [] {
        checkEqual(Forms::ellipsize("mystery.zip", 100.0f, measure), "mystery...", "7 letters and the dots");
        checkEqual(Forms::ellipsize("caf\xc3\xa9 au lait", 70.0f, measure), "caf\xc3\xa9...", "an accented letter isn't split");
        checkEqual(Forms::ellipsize("Two words", 70.0f, measure), "Two...", "no space before the dots");
        checkEqual(Forms::ellipsize("Missing", 30.0f, measure), "...", "only the dots fit");
        checkEqual(Forms::ellipsize("Missing", 20.0f, measure), "", "not even the dots fit");
    });

    run("ellipsize and wrapText cope with text that isn't UTF-8", [] {
        // e.g. Latin-1 metadata: bytes that look like stray UTF-8 continuation bytes
        auto bytes = [](const std::string& s) { return 10.0f * static_cast<float>(s.size()); };
        checkEqual(Forms::ellipsize("caf\xe9 cr\xe8me", 60.0f, bytes), "caf...", "Latin-1 accents count as characters");
        checkEqual(Forms::ellipsize("\x80\x81\x82\x83", 30.0f, bytes), "...", "only continuation bytes: just the dots");
        checkEqual(Forms::ellipsize("\x80\x81\x82\x83", 40.0f, bytes), "\x80\x81\x82\x83", "fits as it is");
        check(!Forms::wrapText("\x80\x81\x82\x83 \x84\x85", 30.0f, bytes).empty(), "wrapping finishes");
    });

    run("parseColor takes six hex digits, with or without #", [] {
        unsigned char rgb[3] = { 1, 2, 3 };
        check(Forms::parseColor("75c8f7", rgb) && rgb[0] == 0x75 && rgb[1] == 0xc8 && rgb[2] == 0xf7, "rrggbb");
        check(Forms::parseColor("#FF0000", rgb) && rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 0, "#RRGGBB");
        rgb[0] = 9;
        check(!Forms::parseColor("red", rgb) && rgb[0] == 9, "a name is refused, the color untouched");
        check(!Forms::parseColor("fff", rgb), "three digits");
        check(!Forms::parseColor("#ff00zz", rgb), "not hex");
        check(!Forms::parseColor("", rgb), "empty");
    });

    run("parseNumber takes numbers only", [] {
        float value = 7.0f;
        check(Forms::parseNumber("0.85", value) && value == 0.85f, "a fraction");
        check(Forms::parseNumber(" 40 ", value) && value == 40.0f, "spaces around");
        check(Forms::parseNumber("-2", value) && value == -2.0f, "negative");
        value = 7.0f;
        check(!Forms::parseNumber("half", value) && value == 7.0f, "a word is refused, the value untouched");
        check(!Forms::parseNumber("12px", value), "a unit");
        check(!Forms::parseNumber("", value), "empty");
    });

    run("wrapText breaks at spaces, and inside words too long for a line", [] {
        checkEqual(joined(Forms::wrapText("one two three", 70.0f, measure)), "one two|three", "at spaces");
        checkEqual(joined(Forms::wrapText("C:\\Games\\Arcade\\roms", 60.0f, measure)), "C:\\Gam|es\\Arc|ade\\ro|ms", "a long path");
        checkEqual(joined(Forms::wrapText("Set in C:\\a\\very\\long", 60.0f, measure)), "Set in|C:\\a\\v|ery\\lo|ng", "a long word after a short one");
        checkEqual(joined(Forms::wrapText("first\n\nthird\n", 100.0f, measure)), "first||third", "lines and an empty one; not the final newline");
        check(Forms::wrapText("", 100.0f, measure).empty(), "nothing for empty text");
        checkEqual(joined(Forms::wrapText("\xc3\xa9\xc3\xa9\xc3\xa9", 20.0f, measure)), "\xc3\xa9\xc3\xa9|\xc3\xa9", "whole UTF-8 characters");
    });

    run("splitRow gives label and detail what they need when they fit", [] {
        Forms::RowSplit split = Forms::splitRow(200.0f, 100.0f, 1000.0f, 20.0f);
        check(split.detail == 100.0f, "detail as wide as it is");
        check(split.label == 880.0f, "label gets the rest");
        split = Forms::splitRow(200.0f, 0.0f, 1000.0f, 20.0f);
        check(split.detail == 0.0f && split.label == 1000.0f, "no detail: the label gets the whole row, no gap");
    });

    run("splitRow shares a crowded row", [] {
        // A long label and a long detail: the detail gets its share, the label the rest
        Forms::RowSplit split = Forms::splitRow(900.0f, 800.0f, 1020.0f, 20.0f, 0.45f);
        check(split.detail == 450.0f, "detail capped at 45% of the room");
        check(split.label == 550.0f, "label gets the rest");
        // A short label lets the detail have more than its share
        split = Forms::splitRow(300.0f, 800.0f, 1020.0f, 20.0f, 0.45f);
        check(split.detail == 700.0f, "detail takes what the label leaves");
        check(split.label == 300.0f, "the label still fits");
        // A long label with a short detail: the detail is never cut
        split = Forms::splitRow(2000.0f, 80.0f, 1020.0f, 20.0f, 0.45f);
        check(split.detail == 80.0f, "a short detail is whole");
        check(split.label == 920.0f, "the label is cut instead");
    });

    run("refocusIndex stays put, then goes up, then down", [] {
        std::vector<FocusTarget> rows = { FocusTarget::Free, FocusTarget::None, FocusTarget::Free, FocusTarget::Free };
        check(Forms::refocusIndex(rows, 2) == 2, "the row now at the same place");
        check(Forms::refocusIndex(rows, 1) == 0, "the nearest above");
        rows = { FocusTarget::None, FocusTarget::None, FocusTarget::Free };
        check(Forms::refocusIndex(rows, 0) == 2, "below when nothing's above");
        check(Forms::refocusIndex(rows, 9) == 2, "past the end counts from the last row");
        check(Forms::refocusIndex({}, 0) == -1, "no rows");
        check(Forms::refocusIndex({ FocusTarget::None }, 0) == -1, "nothing to focus");
    });

    run("refocusIndex avoids a file field when anything else will do", [] {
        // A button above a file field hid itself: focus mustn't land on the file field
        std::vector<FocusTarget> rows = { FocusTarget::Free, FocusTarget::None, FocusTarget::Guarded, FocusTarget::Free };
        check(Forms::refocusIndex(rows, 2) == 0, "up past the file field");
        rows = { FocusTarget::None, FocusTarget::Guarded, FocusTarget::Free };
        check(Forms::refocusIndex(rows, 1) == 2, "down past it when nothing's above");
        rows = { FocusTarget::None, FocusTarget::Guarded };
        check(Forms::refocusIndex(rows, 0) == 1, "the file field when it's all there is");
    });

    run("letterJump goes to the next or previous letter's first row", [] {
        std::vector<std::string> labels = { "1942", "Alpha", "asteroids", "Beta", "Bomb", "bravo", "Zed" };
        check(Forms::letterJump(labels, 0, 1) == 1, "from the numbers to A");
        check(Forms::letterJump(labels, 1, 1) == 3, "A (any case) to B");
        check(Forms::letterJump(labels, 4, 1) == 6, "from the middle of B to Z");
        check(Forms::letterJump(labels, 6, 1) == 6, "nothing after Z: stays");
        check(Forms::letterJump(labels, 5, -1) == 1, "from the middle of B to the first A");
        check(Forms::letterJump(labels, 3, -1) == 1, "from the first B to the first A");
        check(Forms::letterJump(labels, 2, -1) == 0, "A back to the numbers");
        check(Forms::letterJump(labels, 0, -1) == 0, "nothing before: stays");
        check(Forms::letterJump({}, 0, 1) == 0, "an empty list");
    });

    run("a one-line edit moves its cursor: Home, End, left and right", [] {
        Forms::TextEdit edit;
        edit.set("C:\\games");
        edit.home();
        check(edit.cursor == 0, "Home");
        edit.right();
        edit.right();
        edit.right();
        edit.insert("roms\\");
        check(edit.text == "C:\\roms\\games", "typed in the middle: " + edit.text);
        edit.end();
        check(edit.cursor == edit.text.size(), "End");
    });

    run("FileStamp tells when a file was replaced under the same name", [] {
        std::string root = Test::makeRoot("filestamp");
        std::string path = root + "/logo.png";
        check(!FileStamp::of(path).exists, "a missing file");
        Test::writeFile(path, "first picture");
        FileStamp first = FileStamp::of(path);
        check(first.exists, "the file exists");
        check(FileStamp::of(path) == first, "unchanged");
        Test::writeFile(path, "a different, longer picture");
        check(FileStamp::of(path) != first, "new size");
        FileStamp second = FileStamp::of(path);
        // Same size, later write time
        std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(5));
        check(FileStamp::of(path) != second, "same size, newer");
    });
});

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

// The on-screen keyboard's keys and how the controls move over them, without
// drawing: the layout-drawn <keyboard> (Graphics/Component/KeyboardWidget)
// and the theme editor's ImGui keyboard (Manager/Themes/app/EditorPad) both
// draw this, so they type the same way. Pure, header-only.

#include "Form.h"
#include <algorithm>
#include <string>
#include <vector>

namespace Forms
{
    class KeyboardKeys
    {
    public:
        // LineUp / LineDown: the owner moves the cursor a line as the field draws it
        enum class Result { None, Done, Cancel, LineUp, LineDown };
        enum class Nav { Up, Down, Left, Right, Select, Back };
        enum class Action { Insert, Shift, Space, Delete, Done, NewLine, CursorLeft, CursorRight, LineUp, LineDown };
        struct Key
        {
            std::string label;
            Action action = Action::Insert;
            int units = 1;     // width in key widths
        };
        using Rows = std::vector<std::vector<Key>>;

        // numeric shows a number pad (decimal adds a "." key); multiline adds
        // a New line key and a row of cursor keys
        void reset(bool numeric, bool multiline, bool decimal = false)
        {
            numeric_ = numeric;
            multiline_ = multiline && !numeric;
            decimal_ = decimal && numeric;
            shift_ = false;
            // Start on the first row of letters, or on 1 for numbers
            row_ = numeric ? 0 : 1;
            column_ = 0;
        }

        bool numeric() const { return numeric_; }
        bool multiline() const { return multiline_; }
        bool shift() const { return shift_; }
        int row() const { return row_; }
        int column() const { return column_; }

        const Rows& rows() const
        {
            static const std::vector<Key> bottom = {
                { "Shift", Action::Shift, 2 }, { "Space", Action::Space, 4 }, { "Del", Action::Delete, 2 }, { "Done", Action::Done, 2 }
            };
            // Covers what names and paths need: \ / : . - _ ' and more under Shift.
            // ASCII only: the fonts' atlases only have ASCII.
            static const Rows lower = {
                keyRow("1234567890"), keyRow("qwertyuiop"), keyRow("asdfghjkl-"), keyRow("zxcvbnm./\\"), bottom
            };
            static const Rows upper = {
                keyRow("!@$%&()_+="), keyRow("QWERTYUIOP"), keyRow("ASDFGHJKL'"), keyRow("ZXCVBNM,:\""), bottom
            };
            static const Rows numbers = {
                keyRow("123"), keyRow("456"), keyRow("789"),
                { { "-", Action::Insert, 1 }, { "0", Action::Insert, 1 }, { "Del", Action::Delete, 1 } },
                { { "Done", Action::Done, 3 } }
            };
            static const Rows decimals = {
                keyRow("123"), keyRow("456"), keyRow("789"),
                { { "-", Action::Insert, 1 }, { "0", Action::Insert, 1 }, { ".", Action::Insert, 1 }, { "Del", Action::Delete, 1 } },
                { { "Done", Action::Done, 4 } }
            };
            // Multi-line text: cursor keys above, and a New line key; Done still finishes
            static const std::vector<Key> cursorKeys = {
                { "<-", Action::CursorLeft, 2 }, { "->", Action::CursorRight, 2 },
                { "Line up", Action::LineUp, 3 }, { "Line down", Action::LineDown, 3 }
            };
            static const std::vector<Key> multilineBottom = {
                { "Shift", Action::Shift, 2 }, { "Space", Action::Space, 2 }, { "New line", Action::NewLine, 2 },
                { "Del", Action::Delete, 2 }, { "Done", Action::Done, 2 }
            };
            static const Rows lowerLines = {
                keyRow("1234567890"), keyRow("qwertyuiop"), keyRow("asdfghjkl-"), keyRow("zxcvbnm./\\"), cursorKeys, multilineBottom
            };
            static const Rows upperLines = {
                keyRow("!@$%&()_+="), keyRow("QWERTYUIOP"), keyRow("ASDFGHJKL'"), keyRow("ZXCVBNM,:\""), cursorKeys, multilineBottom
            };
            if (numeric_) return decimal_ ? decimals : numbers;
            if (multiline_) return shift_ ? upperLines : lowerLines;
            return shift_ ? upper : lower;
        }

        void clampFocus()
        {
            const Rows& all = rows();
            row_ = std::clamp(row_, 0, static_cast<int>(all.size()) - 1);
            column_ = std::clamp(column_, 0, static_cast<int>(all[row_].size()) - 1);
        }

        // Arrows move between keys (wrapping), Select presses the focused one, Back cancels
        Result navigate(Nav nav, TextEdit& text)
        {
            const Rows& all = rows();
            clampFocus();
            const int rowCount = static_cast<int>(all.size());
            const int columnCount = static_cast<int>(all[row_].size());

            switch (nav) {
            case Nav::Left:
                column_ = (column_ - 1 + columnCount) % columnCount;
                break;
            case Nav::Right:
                column_ = (column_ + 1) % columnCount;
                break;
            case Nav::Up:
            case Nav::Down: {
                // Move to the key above or below the middle of the current one
                float middle = 0.0f;
                for (int i = 0; i < column_; i++) middle += static_cast<float>(all[row_][i].units);
                middle += static_cast<float>(all[row_][column_].units) / 2.0f;
                const float position = middle / static_cast<float>(unitsOf(all[row_]));

                row_ = (row_ + (nav == Nav::Down ? 1 : -1) + rowCount) % rowCount;
                const float target = position * static_cast<float>(unitsOf(all[row_]));
                float start = 0.0f;
                column_ = static_cast<int>(all[row_].size()) - 1;
                for (size_t i = 0; i < all[row_].size(); i++) {
                    if (target < start + static_cast<float>(all[row_][i].units)) {
                        column_ = static_cast<int>(i);
                        break;
                    }
                    start += static_cast<float>(all[row_][i].units);
                }
                break;
            }
            case Nav::Select:
                return press(all[row_][column_], text);
            case Nav::Back:
                return Result::Cancel;
            }
            return Result::None;
        }

        // Presses a key (Select on it, or a click)
        Result press(const Key& key, TextEdit& text)
        {
            switch (key.action) {
            case Action::Insert: text.insert(key.label); break;
            case Action::Space: text.insert(" "); break;
            case Action::NewLine: text.insert("\n"); break;
            case Action::Delete: text.backspace(); break;   // the whole UTF-8 character before the cursor
            case Action::CursorLeft: text.left(); break;
            case Action::CursorRight: text.right(); break;
            case Action::Shift: shift_ = !shift_; break;
            case Action::Done: return Result::Done;
            case Action::LineUp: return Result::LineUp;
            case Action::LineDown: return Result::LineDown;
            }
            return Result::None;
        }

        static int unitsOf(const std::vector<Key>& row)
        {
            int units = 0;
            for (const Key& key : row) units += key.units;
            return units;
        }

    private:
        static std::vector<Key> keyRow(const std::string& characters)
        {
            std::vector<Key> row;
            for (char c : characters) {
                Key key;
                key.label = std::string(1, c);
                row.push_back(key);
            }
            return row;
        }

        bool numeric_ = false;
        bool multiline_ = false;
        bool decimal_ = false;
        bool shift_ = false;
        int row_ = 1;
        int column_ = 0;
    };
}

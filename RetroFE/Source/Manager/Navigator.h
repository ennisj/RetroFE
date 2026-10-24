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

#include "../Forms/Form.h"
#include <string>
#include <vector>

// The manager's screens, as a stack: Manage -> Games -> one game -> its media.
// Each frame is a form id with its parameters and where the user was on it.
// Back returns to the previous frame; leaving the bottom one closes the manager.
class Navigator
{
public:
    struct Frame
    {
        std::string form;
        Forms::Params params;
        Forms::Focus focus;    // saved when another screen is opened over it
    };

    // Most screens deep; opening another drops the oldest but the first
    static constexpr size_t maxDepth = 32;

    // Every screen, first to current
    const std::vector<Frame>& frames() const { return frames_; }

    bool empty() const { return frames_.empty(); }
    size_t depth() const { return frames_.size(); }

    // The screen shown, or nullptr when the manager is closed
    const Frame* current() const { return frames_.empty() ? nullptr : &frames_.back(); }

    // Starts again with one screen
    void open(const std::string& form, const Forms::Params& params = {});

    // Opens a screen over the current one, remembering where the user was on it
    void push(const std::string& form, const Forms::Params& params, const Forms::Focus& leaving);

    // Shows another screen in place of the current one (e.g. a new record's
    // details in place of the form that added it), so Back skips the old one
    void replace(const std::string& form, const Forms::Params& params);

    // Returns to the previous screen. False when there's none, i.e. the manager closes.
    bool back();

    void clear() { frames_.clear(); }

    // The Settings key: opens form, switches to it from another screen, or closes it.
    // Returns false when that leaves nothing open.
    bool toggle(const std::string& form);

private:
    std::vector<Frame> frames_;
};

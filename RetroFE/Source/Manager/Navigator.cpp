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
#include "Navigator.h"

void Navigator::open(const std::string& form, const Forms::Params& params)
{
    frames_.clear();
    frames_.push_back(Frame{ form, params, {} });
}

void Navigator::push(const std::string& form, const Forms::Params& params, const Forms::Focus& leaving)
{
    if (frames_.empty()) {
        open(form, params);
        return;
    }
    frames_.back().focus = leaving;
    // Too deep: forget the oldest screen but the first, so Back still ends where it started
    if (frames_.size() >= maxDepth && frames_.size() > 1) {
        frames_.erase(frames_.begin() + 1);
    }
    frames_.push_back(Frame{ form, params, {} });
}

void Navigator::replace(const std::string& form, const Forms::Params& params)
{
    if (frames_.empty()) {
        open(form, params);
        return;
    }
    // The screen below keeps where the user was on it
    frames_.back() = Frame{ form, params, {} };
}

bool Navigator::back()
{
    if (!frames_.empty()) {
        frames_.pop_back();
    }
    return !frames_.empty();
}

bool Navigator::toggle(const std::string& form)
{
    if (!frames_.empty() && frames_.back().form == form) {
        frames_.clear();
        return false;
    }
    open(form);
    return true;
}

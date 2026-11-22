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
#include <optional>
#include <string>

namespace Manager
{
    // Where RetroFE goes after an action closed Manage (ManagerUI keeps one,
    // static, since RetroFE reloads by starting again; RetroFE polls it).
    // Kept free of SDL so the rules are tested:
    //
    //   reopen()    Result::reload with open: once RetroFE is back up,
    //               Manage opens on that form with the message ("Apply
    //               changes now" returns to the hub)
    //   reloading() the reload is starting: it's kept for the RetroFE
    //               that comes back up
    //   shutdown()  RetroFE is going away: dropped unless reloading()
    class AfterReload
    {
    public:
        struct Screen
        {
            std::string form;
            Forms::Params params;
            std::string message;
        };

        void reopen(const Screen& screen) { reopen_ = screen; }

        // The RetroFE that came back up, once it's ready: the screen to open, once
        bool takeReopen(Screen& screen)
        {
            if (!reopen_ || reloading_) {
                return false;   // not before the reload it waits for
            }
            screen = *reopen_;
            reopen_.reset();
            return true;
        }

        void reloading() { reloading_ = reopen_.has_value(); }

        // A new RetroFE: what reloading() kept is now its own
        void started() { reloading_ = false; }

        void shutdown()
        {
            if (!reloading_) {
                reopen_.reset();
            }
        }

        void clear()
        {
            reopen_.reset();
            reloading_ = false;
        }

        bool hasReopen() const { return reopen_.has_value(); }

    private:
        std::optional<Screen> reopen_;
        bool reloading_ = false;
    };
}

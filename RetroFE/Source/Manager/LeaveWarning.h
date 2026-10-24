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

#include <optional>
#include <string>

namespace Manager
{
    // The ways of leaving Manage's screens
    enum class Leave
    {
        Back,    // Back or Escape: the current screen (Manage itself from its first screen)
        Close,   // closing Manage: the Settings key, the Close button, starting a game
        Quit     // closing RetroFE's window: Manage, then RetroFE
    };

    // "Warn once, then leave" for unsaved changes (Provider::leaving), per
    // way of leaving: a screen with unsaved changes warns, and doing the same
    // again leaves, dropping them. Another way of leaving after the warning
    // warns again, in its own words: a Back warning says "leave again", so a
    // window close straight after it mustn't quit and drop the changes
    // unasked. ManagerUI keeps one; kept free of SDL so the rules are tested.
    class LeaveWarning
    {
    public:
        enum class Verdict
        {
            Leave,           // nothing objects
            LeaveDropping,   // warned about this way of leaving last time: leave, dropping the changes
            Warn             // show message(): leaving this way again leaves
        };

        // The user asked to leave `how`; warning is what the screens say ("" when they don't mind)
        Verdict ask(Leave how, const std::string& warning)
        {
            if (warning.empty()) {
                warned_.reset();
                return Verdict::Leave;
            }
            if (warned_ == how) {
                warned_.reset();
                return Verdict::LeaveDropping;
            }
            warned_ = how;
            message_ = wording(warning, how);
            return Verdict::Warn;
        }

        // What to show after Verdict::Warn
        const std::string& message() const { return message_; }

        // Something else happened (another screen, a move on this one): the next attempt warns again
        void reset() { warned_.reset(); }

        // Treat the next attempt to leave `how` as the second one (a game
        // asked for again after its launch was stopped by a warning)
        void arm(Leave how) { warned_ = how; }

        std::optional<Leave> warned() const { return warned_; }

        // A screen's warning in the words of this way of leaving. Screens
        // word theirs for Back ("... Save them, or leave again to discard
        // them."); a window close says that closing it again quits.
        static std::string wording(const std::string& warning, Leave how)
        {
            if (how != Leave::Quit) {
                return warning;
            }
            static const std::string leaveAgain = "leave again to ";
            std::string::size_type at = warning.find(leaveAgain);
            if (at == std::string::npos) {
                return warning + " Close the window again to quit anyway.";
            }
            return warning.substr(0, at) + "close the window again to quit and " + warning.substr(at + leaveAgain.size());
        }

    private:
        std::optional<Leave> warned_;
        std::string message_;
    };
}

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

#include "TestSupport.h"
#include "../Source/Manager/LaunchReturn.h"
#include "../Source/Forms/Form.h"

static Test::Suite launchWaitSuite("launch return wait", [] {
    Test::run("published index restores during a refresh", [] {
        Test::check(Manager::LaunchReturnWait::ready(true, true), "no reload restores at once");
        Test::check(!Manager::LaunchReturnWait::ready(false, true), "reload waits");
        Test::check(Manager::LaunchReturnWait::ready(false, false), "failed build finishes waiting");
    });
    Test::run("held inputs must be released before cancelling", [] {
        Manager::LaunchReturnWait wait;
        for (int kind = 0; kind < 4; kind++) {
            Test::check(!wait.input(kind, 7, true), "held from game or Manage open");
            Test::check(!wait.input(kind, 7, false), "release does not cancel");
            Test::check(!wait.input(kind, 7, true, true), "repeat does not cancel");
            Test::check(wait.input(kind, 7, true), "release then press cancels");
            Test::check(!wait.input(kind, 7, true), "held does not cancel again");
        }
    });
    Test::run("hint fitting preserves primary and reserves a reason", [] {
        auto kept = Forms::fitHints({80.0f, 80.0f}, {4, 3}, 100.0f, 10.0f);
        Test::check(kept == std::vector<int>{0}, "primary survives Back");
        kept = Forms::fitHints({80.0f, 80.0f}, {4, 3}, 100.0f, 10.0f, 50.0f);
        Test::check(kept.empty(), "reason can displace all navigation");
        kept = Forms::fitHints({20.0f, 20.0f}, {2, 3}, 100.0f, 10.0f, 40.0f);
        Test::check(kept == std::vector<int>({0, 1}), "navigation fits beside reason");
    });
    Test::run("note-only bar fallback prefers safe buttons", [] {
        using Target = Forms::FocusTarget;
        auto move = Forms::refocusBar({}, -1, Target::Guarded);
        Test::check(move.row == -1 && !move.bar, "no rows available");
        Test::check(Forms::refocusBarFallback({Target::Guarded, Target::Free}, 0) == 1, "free button");
        int target = Forms::refocusBarFallback({Target::Guarded}, 0);
        Test::check(target == 0, "only guarded target remains");
        Test::check(std::vector<Target>{Target::Guarded}[target] != Target::Free, "guard required");
    });
});

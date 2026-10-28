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
#include "../Source/Builtin/Builtin.h"
#include "../Source/Forms/Form.h"
#include <map>
#include <sstream>

static Test::Suite round12Suite("round 12", [] {
    Test::run("built-in letter controls and distinct inputs", [] {
        std::map<std::string, std::string> owners;
        std::map<std::string, std::string> controls;
        std::istringstream lines(Builtin::controls);
        std::string line;
        while (std::getline(lines, line)) {
            size_t equal = line.find('=');
            if (equal == std::string::npos) continue;
            std::string name = line.substr(0, equal);
            name.erase(name.find_last_not_of(" \t") + 1);
            if (name == "deadZone") continue;
            std::istringstream inputs(line.substr(equal + 1));
            std::string input;
            while (std::getline(inputs, input, ',')) {
                input.erase(0, input.find_first_not_of(" \t"));
                input.erase(input.find_last_not_of(" \t\r") + 1);
                Test::check(owners.emplace(input, name).second, input + " has only one control");
                controls[name] += input + ",";
            }
        }
        Test::checkEqual(controls["letterUp"], "M,joyLeftTrigger,", "previous letter");
        Test::checkEqual(controls["letterDown"], "N,joyRightTrigger,", "next letter");
    });
    Test::run("primary hint requires a primary, an edit and a runnable button", [] {
        std::string error;
        auto form = Forms::parse(R"(<form id="t"><button primary="yes" label="Save" action="save"/><button label="Check" action="check"/></form>)", error);
        Test::check(form != nullptr, error);
        if (!form) return;
        auto& save = form->fields[0];
        Test::check(save.primary && !form->fields[1].primary, "primary parsed, default false");
        Test::check(Forms::primaryHint(save, true, true), "edited and ready");
        Test::check(!Forms::primaryHint(save, false, true), "no edit");
        Test::check(!Forms::primaryHint(save, true, false), "cannot run");
        Test::check(!Forms::primaryHint(form->fields[1], true, true), "other action never green");
    });
    Test::run("Sources pins its primary Add source before Check for updates", [] {
        std::string error;
        auto form = Forms::parse(Forms::builtinForm("sources"), error);
        Test::check(form != nullptr, error);
        if (!form) return;
        auto pinned = Forms::pinnedActions(*form);
        Test::check(!pinned.empty(), "Sources has a bar");
        if (pinned.empty()) return;
        const auto& buttons = pinned[0]->children;
        Test::check(buttons.size() == 2, "Add source and Check for updates");
        if (buttons.size() != 2) return;
        Test::check(buttons[0].action == "source.add" && buttons[0].primary, "jump targets Add source when ready");
        Test::check(buttons[1].action == "catalog.refresh" && !buttons[1].primary, "Check never turns green");
    });
    Test::run("a replacement bar button must be free before retaining bar focus", [] {
        Forms::Field button;
        button.type = Forms::FieldType::Button;
        Test::check(Forms::refocusTarget(&button, true, true) == Forms::FocusTarget::Free, "shown safe button");
        Test::check(Forms::refocusTarget(&button, true, false) != Forms::FocusTarget::Free, "new undo returns to rows");
        std::vector<Forms::FocusTarget> rows = {Forms::FocusTarget::Free, Forms::FocusTarget::None, Forms::FocusTarget::Guarded};
        auto move = Forms::refocusBar(rows, 1, Forms::refocusTarget(&button, true, false));
        Test::check(!move.bar && move.row == 0, "Hide returns to the previous link, not its new undo");
        move = Forms::refocusBar(rows, 1, Forms::refocusTarget(&button, true, true));
        Test::check(move.bar, "a previously shown safe button keeps bar focus");
        button.confirm = "Delete?";
        Test::check(Forms::refocusTarget(&button, true, true) != Forms::FocusTarget::Free, "confirming button returns to rows");
        move = Forms::refocusBar(rows, 1, Forms::refocusTarget(&button, true, true));
        Test::check(!move.bar && move.row == 0, "a confirming replacement returns to the previous link");
        move = Forms::refocusBar({}, -1, Forms::FocusTarget::Guarded);
        Test::check(!move.bar && move.row == -1, "no rows never authorizes guarded bar focus");
        Test::check(Forms::refocusIndex({Forms::FocusTarget::Free, Forms::FocusTarget::None, Forms::FocusTarget::Guarded}, 1) == 0, "previous link wins");
    });
    Test::run("hints lose letter then paging before primary and Back", [] {
        std::vector<float> widths = { 20, 20, 20, 30, 20 };
        std::vector<int> priorities = { 2, 1, 0, 3, 3 };
        Test::check(Forms::fitHints(widths, priorities, 118, 2) == std::vector<int>({0,1,2,3,4}), "all fit exactly");
        Test::check(Forms::fitHints(widths, priorities, 96, 2) == std::vector<int>({0,1,3,4}), "letter drops first");
        Test::check(Forms::fitHints(widths, priorities, 74, 2) == std::vector<int>({0,3,4}), "then paging");
        Test::check(Forms::fitHints(widths, priorities, 52, 2) == std::vector<int>({3,4}), "primary and Back survive");
    });
});

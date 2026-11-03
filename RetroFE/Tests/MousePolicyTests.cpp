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
#include "../Source/Utility/MousePolicy.h"
#include "../Source/Forms/Form.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Database/Configuration.h"

using namespace Test;

static Test::Suite mousePolicySuite("mouse-policy", [] {
    run("pointer grab truth table", [] {
        for (bool fullscreen : { false, true }) {
            for (bool capture : { false, true }) {
                for (bool background : { false, true }) {
                    check(MousePolicy::grabsPointer(fullscreen, capture, background)
                        == (fullscreen && capture && !background), "grab policy");
                }
            }
        }
    });
    run("pointer movement stays on the fullscreen display", [] {
        MousePolicy::Bounds main{ 0, 0, 2560, 1440 };
        for (bool fullscreen : { false, true }) {
            for (bool background : { false, true }) {
                check(MousePolicy::mayMovePointer(fullscreen, background, 100, 100, main)
                    == (fullscreen && !background), "inside");
                check(!MousePolicy::mayMovePointer(fullscreen, background, -100, 100, main), "left monitor");
                check(!MousePolicy::mayMovePointer(fullscreen, background, 3000, 100, main), "right monitor");
                check(!MousePolicy::mayMovePointer(fullscreen, background, 100, -100, main), "above monitor");
            }
        }
        check(MousePolicy::mayMovePointer(true, false, 0, 0, main), "top left included");
        check(MousePolicy::mayMovePointer(true, false, 2559, 1439, main), "bottom right included");
        check(!MousePolicy::mayMovePointer(true, false, 2560, 100, main), "right edge excluded");
        check(!MousePolicy::mayMovePointer(true, false, 100, 1440, main), "bottom edge excluded");
        check(MousePolicy::mayMovePointer(true, false, -100, 100, { -1920, 0, 1920, 1080 }), "chosen monitor on left");
        check(!MousePolicy::mayMovePointer(true, false, 100, 100, { -1920, 0, 1920, 1080 }), "primary outside chosen left monitor");
        check(!MousePolicy::mayMovePointer(true, false, 0, 0, { 0, 0, 0, 0 }), "invalid bounds");
    });
    run("display choices use SDL indices and short labels", [] {
        auto main = MousePolicy::displayOption(0, { 0, 0, 2560, 1440 });
        auto left = MousePolicy::displayOption(2, { -1920, 0, 1920, 1080 });
        checkEqual(main.first, "0", "zero based value");
        checkEqual(main.second, "1: 2560 x 1440 (main)", "one based main label");
        checkEqual(left.first, "2", "display index value");
        checkEqual(left.second, "3: 1920 x 1080", "one based secondary label");
        Configuration config;
        ManagerModel model(config);
        model.setDisplays({ main, left });
        check(model.options("displays") == std::vector<std::pair<std::string, std::string>>{ main, left }, "display options supplied by UI");
    });
    run("settings display fields", [] {
        std::string error;
        auto form = Forms::parse(Forms::builtinForm("settings"), error);
        check(form != nullptr, "settings parses: " + error);
        if (!form) return;
        bool monitor = false, capture = false;
        for (const auto& section : form->fields) {
            if (section.label != "Display") continue;
            for (const auto& field : section.children) {
                if (field.label == "Monitor") {
                    monitor = field.type == Forms::FieldType::Choice && field.bind == "settings:screenNum0"
                        && field.options == "displays" && field.defaultValue == "0";
                }
                if (field.label == "Capture mouse") {
                    capture = field.enabledWhen == "settings:fullscreen";
                }
            }
        }
        check(monitor, "Monitor choice uses main screen key and display options");
        check(capture, "Capture mouse enabled only with fullscreen");
    });
});

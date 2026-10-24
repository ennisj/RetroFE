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
#include "Edits.h"
#include "ConfFile.h"
#include "../Builtin/Builtin.h"
#include "../Control/InputToken.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <map>

// Editor::controls / setControls: controls.conf and controls1..9.conf
namespace Library
{

namespace
{
    // An input of value that RetroFE can use for the control
    bool hasWorkingInput(const ControlList::Control& control, const std::string& value)
    {
        for (const std::string& token : InputToken::split(value)) {
            InputToken::Input input = InputToken::parse(token);
            if (input.kind == InputToken::Kind::Invalid) {
                continue;
            }
            if (!control.keyboardOnly || input.kind == InputToken::Kind::Keyboard) {
                return true;
            }
        }
        return false;
    }

    bool isNumber(const std::string& value, int min, int max)
    {
        if (value.empty() || value.size() > 4) {
            return false;
        }
        for (char c : value) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return false;
            }
        }
        int number = std::stoi(value);
        return number >= min && number <= max;
    }
}

std::vector<std::string> Editor::controlsFileNames()
{
    // RetroFE::run's order: later files win, key by key
    std::vector<std::string> names = { "controls.conf" };
    for (int i = 1; i < 10; i++) {
        names.push_back("controls" + std::to_string(i) + ".conf");
    }
    return names;
}

std::string Editor::checkControls(const std::map<std::string, std::string>& current)
{
    for (const ControlList::Control& control : ControlList::all()) {
        if (!control.required) {
            continue;
        }
        auto found = current.find(control.name);
        // Not set by any file: RetroFE uses its default, which works
        std::string value = found == current.end() ? ControlList::builtinDefault(control.name) : found->second;
        if (!hasWorkingInput(control, value)) {
            return control.keyboardOnly
                ? control.label + " needs a keyboard key, or you couldn't open Settings again."
                : control.label + " needs at least one input, or you couldn't use RetroFE.";
        }
    }
    return "";
}

EditResult Editor::setControls(const std::map<std::string, std::string>& values)
{
    if (values.empty()) {
        return EditResult::fail("Nothing to save.");
    }
    ConfLayers layers(root_, controlsFileNames());

    // Allowed keys and values first, and the state they'd leave
    std::map<std::string, std::string> after;
    for (const ControlList::Control& control : ControlList::all()) {
        if (layers.has(control.name)) {
            after[control.name] = layers.get(control.name);
        }
    }
    for (const auto& [key, value] : values) {
        if (key == "deadZone") {
            if (!isNumber(value, 0, 100)) {
                return EditResult::fail("The dead zone is a number from 0 to 100.");
            }
            continue;
        }
        const ControlList::Control* control = ControlList::find(key);
        if (!control) {
            return EditResult::fail("\"" + key + "\" isn't a control the Controls screen changes.");
        }
        std::string problem = ConfFile::checkValue(value);
        for (const std::string& token : InputToken::split(value)) {
            InputToken::Input input = InputToken::parse(token);
            if (problem.empty() && input.kind == InputToken::Kind::Invalid) {
                problem = input.problem;
            }
        }
        if (!problem.empty()) {
            return EditResult::fail(control->label + ": " + problem);
        }
        after[key] = value;
    }
    std::string locked = checkControls(after);
    if (!locked.empty()) {
        return EditResult::fail(locked);
    }

    // Each where its value comes from now, so an override in controlsN.conf
    // keeps winning; otherwise controls.conf, made from the built-in controls
    // RetroFE was using if it doesn't exist yet
    const std::string initial = std::string("# RetroFE's controls (see docs/CONTROLS.md)\n") + Builtin::controls;
    EditResult result;
    for (const auto& [key, value] : values) {
        std::string error;
        if (!layers.set(key, value, error, 0, initial, &backups_)) {
            result.message = "Couldn't save " + key + ": " + error;
            return result;
        }
        std::string file = Utils::combinePath(root_, layers.source(key));
        if (std::find(result.files.begin(), result.files.end(), file) == result.files.end()) {
            result.files.push_back(file);
        }
    }
    result.ok = true;
    result.message = "Saved.";
    return result;
}

}

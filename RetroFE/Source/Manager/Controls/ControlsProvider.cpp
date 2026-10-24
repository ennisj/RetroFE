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
#include "ControlsProvider.h"
#include "../../Control/InputToken.h"
#include "../../Database/Configuration.h"
#include "../../Library/ConfFile.h"
#include "../../Library/Edits.h"
#include <algorithm>
#include <iterator>

namespace Manager
{

namespace
{
    const char* const listForm = "controls";
    const char* const controlForm = "control";
    const char* const inputForm = "control-input";

    // UserInput gives these an input whatever the files say (see UserInput::initialize)
    bool alwaysMapped(const std::string& name)
    {
        static const char* const names[] = { "up", "down", "left", "right", "select", "back", "quit" };
        return std::find_if(std::begin(names), std::end(names), [&](const char* n) { return name == n; }) != std::end(names);
    }

    // A direction's partner, used for it when it isn't set
    std::string partner(const std::string& name)
    {
        if (name == "up") return "left";
        if (name == "left") return "up";
        if (name == "down") return "right";
        if (name == "right") return "down";
        return "";
    }

    std::string joinWords(const std::vector<std::string>& words)
    {
        std::string text;
        for (size_t i = 0; i < words.size(); i++) {
            text += (i == 0 ? "" : i + 1 == words.size() ? " and " : ", ") + words[i];
        }
        return text;
    }

    std::vector<std::string> nonEmpty(const std::vector<std::string>& tokens)
    {
        std::vector<std::string> kept;
        for (const std::string& token : tokens) {
            if (!token.empty()) {
                kept.push_back(token);
            }
        }
        return kept;
    }

    // Same inputs in the same order, however they're spaced
    bool sameValue(const std::string& a, const std::string& b)
    {
        return nonEmpty(InputToken::split(a)) == nonEmpty(InputToken::split(b));
    }
}

std::string ControlsProvider::describeValue(const std::string& value)
{
    std::vector<std::string> words;
    for (const std::string& token : nonEmpty(InputToken::split(value))) {
        InputToken::Input input = InputToken::parse(token);
        words.push_back(input.kind == InputToken::Kind::Invalid ? "\"" + token + "\" (not recognised)" : InputToken::describe(input));
    }
    if (words.empty()) {
        return "No input";
    }
    std::string text;
    for (size_t i = 0; i < words.size(); i++) {
        text += (i == 0 ? "" : ", ") + words[i];
    }
    return text;
}

void ControlsProvider::refresh()
{
    // The files may have changed while the manager was closed
    if (!edits_.isDirty()) {
        loaded_ = false;
    }
}

void ControlsProvider::load()
{
    Library::ConfLayers layers(Configuration::absolutePath, Library::Editor::controlsFileNames());
    bool hasBase = layers.file(0).exists();
    std::map<std::string, std::string> values;
    sources_.clear();

    // What RetroFE reads before its fallbacks: the files, or the built-in
    // controls when there's no controls.conf
    auto fromFiles = [&](const std::string& name, std::string& value, Source& source) {
        if (layers.has(name)) {
            value = layers.get(name);
            source = { layers.source(name), "Set in " + layers.source(name) + "." };
            return true;
        }
        std::string builtin = name == "manager" ? "" : ControlList::builtinDefault(name);
        if (!hasBase && !builtin.empty()) {
            value = builtin;
            source = { "", "From RetroFE's built-in controls: there's no controls.conf yet. Saving creates one." };
            return true;
        }
        return false;
    };

    for (const ControlList::Control& control : ControlList::all()) {
        std::string value;
        Source source;
        if (!fromFiles(control.name, value, source)) {
            std::string other = partner(control.name);
            Source ignored;
            if (!other.empty() && fromFiles(other, value, ignored)) {
                source = { "", "Not set: RetroFE uses " + ControlList::find(other)->label + "'s inputs for it." };
            }
            else if (alwaysMapped(control.name) || control.name == "manager") {
                value = ControlList::builtinDefault(control.name);
                source = { "", "Not set: RetroFE uses its default." };
            }
            else {
                source = { "", "Not set in any controls file." };
            }
        }
        values[control.name] = value;
        sources_[control.name] = source;
    }
    values["deadZone"] = layers.has("deadZone") ? layers.get("deadZone") : hasBase ? "3" : "30";

    edits_.load("controls", values);
    saved_ = values;
    loaded_ = true;
}

void ControlsProvider::enter(const Screen& screen)
{
    if (screen.form != listForm && screen.form != controlForm && screen.form != inputForm) {
        return;
    }
    if (!loaded_) {
        load();
    }

}

std::string ControlsProvider::leaving(const Screen& screen)
{
    return screen.form == listForm && edits_.isDirty()
        ? "Your control changes aren't saved. Save them, or leave again to drop them." : "";
}

void ControlsProvider::discard(const Screen& screen)
{
    if (screen.form == listForm) {
        edits_.reset();
    }
}

std::vector<std::string> ControlsProvider::tokens(const std::string& control) const
{
    return nonEmpty(InputToken::split(edits_.get(control)));
}

void ControlsProvider::setTokens(const std::string& control, const std::vector<std::string>& list)
{
    std::string value = InputToken::join(list);
    // Back to what's saved: its spelling exactly, so it isn't "changed" for nothing
    auto saved = saved_.find(control);
    if (saved != saved_.end() && sameValue(saved->second, value)) {
        value = saved->second;
    }
    edits_.set(control, value);
}

std::vector<std::string> ControlsProvider::usedBy(const std::string& token, const std::string& except) const
{
    // The same input by name, or the same button on a pad connected now under
    // another name (joyA is joyButton1 on some pads)
    std::vector<InputToken::Input> inputs = { InputToken::parse(token) };
    for (const std::string& twin : services_.sameInputs ? services_.sameInputs(token) : std::vector<std::string>{}) {
        inputs.push_back(InputToken::parse(twin));
    }
    std::vector<std::string> labels;
    for (const ControlList::Control& control : ControlList::all()) {
        if (control.name == except) {
            continue;
        }
        bool uses = false;
        for (const std::string& other : tokens(control.name)) {
            InputToken::Input theirs = InputToken::parse(other);
            for (size_t i = 0; i < inputs.size(); i++) {
                // A pad's other name for it (i > 0) is a mapped pad's: joyUnmapped... doesn't act there
                uses = uses || (InputToken::same(inputs[i], theirs) && (i == 0 || !theirs.unmappedOnly));
            }
        }
        if (uses) {
            labels.push_back(control.label);
        }
    }
    return labels;
}

std::string ControlsProvider::notes(const std::string& name) const
{
    const ControlList::Control* control = ControlList::find(name);
    if (!control) {
        return "";
    }
    auto source = sources_.find(name);
    std::string text = source != sources_.end() ? source->second.note : "";
    if (edits_.changes().count(name)) {
        text += " Changed: not saved yet.";
    }
    if (control->combo) {
        text += " Hold the first input and press the second; only two are used.";
    }
    std::vector<std::string> list = tokens(name);
    bool works = std::any_of(list.begin(), list.end(), [](const std::string& token) {
        return InputToken::parse(token).kind != InputToken::Kind::Invalid;
    });
    // Left empty in the file itself (quit = on a kiosk), not just emptied here and unsaved
    auto saved = saved_.find(name);
    bool emptyInFile = saved != saved_.end() && nonEmpty(InputToken::split(saved->second)).empty();
    if (list.empty() && emptyInFile && alwaysMapped(name) && source != sources_.end() && !source->second.file.empty()) {
        text += " It's empty there, so nothing is bound to it.";
    }
    if (control->keyboardOnly && std::none_of(list.begin(), list.end(), [](const std::string& token) {
            return InputToken::parse(token).kind == InputToken::Kind::Keyboard;
        })) {
        text += " It has no keyboard key, so RetroFE uses F10.";
    }
    if (!list.empty() && !works && alwaysMapped(name)) {
        text += " None of these works, so RetroFE uses its default: " + describeValue(ControlList::builtinDefault(name)) + ".";

    }
    for (const std::string& token : list) {
        InputToken::Input input = InputToken::parse(token);
        if (input.kind == InputToken::Kind::Invalid) {
            text += " RetroFE skips \"" + token + "\": " + input.problem;
            continue;
        }
        std::vector<std::string> others = usedBy(token, name);
        if (!others.empty()) {
            text += " " + InputToken::describe(input) + " is also an input of " + joinWords(others) + ".";
        }
    }
    return text;
}

std::string ControlsProvider::problems() const
{
    std::vector<std::string> bad;
    for (const ControlList::Control& control : ControlList::all()) {
        for (const std::string& token : tokens(control.name)) {
            if (InputToken::parse(token).kind == InputToken::Kind::Invalid) {
                bad.push_back(control.label + " (" + token + ")");
            }
        }
    }
    if (bad.empty()) {
        return "";
    }
    return "RetroFE skips " + std::string(bad.size() == 1 ? "an input" : std::to_string(bad.size()) + " inputs")
        + " it doesn't recognise: " + joinWords(bad) + ". Open the control to remove it.";
}

std::string ControlsProvider::get(const std::string& key, const Screen& screen)
{
    std::string id = screen.param("id");
    const ControlList::Control* control = ControlList::find(id);
    if (key == "intro") {
        return "Choose a control to see its inputs, add one by pressing it, or remove one. "
               "Changes are kept until you Save, then RetroFE uses them straight away.";
    }
    if (key == "problems") return problems();
    if (key == "isDirty") return edits_.isDirty() ? "yes" : "no";
    if (key == "deadZone") return edits_.get("deadZone");
    if (key == "title") return control ? control->label : "Control";
    if (key == "help") return control ? control->help : "This control isn't one RetroFE reads.";
    if (key == "notes") return notes(id);
    if (key == "hasInputs") return control && !tokens(id).empty() ? "yes" : "no";
    if (key == "inputTitle" || key == "inputToken" || key == "inputNotes") {
        std::string token = screen.param("input");
        InputToken::Input input = InputToken::parse(token);
        bool known = input.kind != InputToken::Kind::Invalid;
        if (key == "inputTitle") return known ? InputToken::describe(input) : "\"" + token + "\"";
        if (key == "inputToken") return token;
        if (!known) return "RetroFE doesn't recognise it, so it skips it: " + input.problem;
        std::vector<std::string> others = usedBy(token, id);
        return others.empty() ? "" : "It's also an input of " + joinWords(others) + ".";
    }
    if (key == "defaultText") {
        std::string fallback = ControlList::builtinDefault(id);
        return fallback.empty() ? "No input" : describeValue(fallback);
    }

    if (key == "captured") return captured_;
    return "";
}

void ControlsProvider::set(const std::string& key, const std::string& value, const Screen&)
{
    if (key == "deadZone") {
        edits_.set("deadZone", value);
    }

    else if (key == "captured") {
        captured_ = value;
    }
}

Forms::Rows ControlsProvider::rows(const std::string& key, size_t limit, const Screen& screen)
{
    Forms::Rows rows;
    if (key == "inputs") {
        // One row per input: Select opens it (param:input), to remove it
        std::string id = screen.param("id");
        for (const std::string& token : tokens(id)) {
            InputToken::Input input = InputToken::parse(token);
            rows.total++;
            if (rows.items.size() < limit) {
                Forms::Row row;
                row.id = token;
                row.label = input.kind == InputToken::Kind::Invalid ? "\"" + token + "\": not recognised" : InputToken::describe(input);
                row.detail = token;
                row.params = { { "id", id } };
                rows.items.push_back(row);
            }
        }
        return rows;
    }
    const std::string prefix = "group:";
    if (key.compare(0, prefix.size(), prefix) != 0) {
        return rows;
    }
    std::string group = key.substr(prefix.size());
    std::map<std::string, std::string> changed = edits_.changes();
    for (const ControlList::Control& control : ControlList::all()) {
        if (control.group != group) {
            continue;
        }
        rows.total++;
        if (rows.items.size() < limit) {
            Forms::Row row;
            row.id = control.name;
            row.label = control.label + (changed.count(control.name) ? " (changed)" : "");
            row.detail = describeValue(edits_.get(control.name));
            rows.items.push_back(row);
        }
    }
    return rows;
}

std::string ControlsProvider::actionProblem(const std::string& action, const Screen& screen)
{
    std::string id = screen.param("id");
    const ControlList::Control* control = ControlList::find(id);
    if (action == "save") {
        if (!edits_.isDirty()) {
            return "No changes to save.";
        }
        if (!services_.editor) {
            return "Controls can't be saved here.";
        }
        return Library::Editor::checkControls(edits_.changes());
    }
    if (action == "reset") {
        return edits_.isDirty() ? "" : "No changes to undo.";
    }
    if (action == "capture" || action == "addCaptured" || action == "remove" || action == "default") {
        if (!control) {
            return "This control isn't one RetroFE reads.";
        }
        if (action == "capture" && control->combo && tokens(id).size() >= 2) {
            return "A combo has two inputs: remove one first.";
        }
        if (action == "remove") {
            std::vector<std::string> list = tokens(id);
            if (std::find(list.begin(), list.end(), screen.param("input")) == list.end()) {
                return "It doesn't have this input any more.";
            }
        }
        if (action == "default" && sameValue(edits_.get(id), ControlList::builtinDefault(id))) {
            return "It's already RetroFE's default.";
        }
        return "";
    }
    return Provider::actionProblem(action, screen);
}

Forms::Model::Result ControlsProvider::addCaptured(const std::string& id)
{
    Forms::Model::Result result;
    const ControlList::Control* control = ControlList::find(id);
    std::string token = captured_;
    captured_.clear();
    InputToken::Input input = InputToken::parse(token);
    if (input.kind == InputToken::Kind::Invalid) {
        result.message = "That input can't be used: " + input.problem;
        return result;
    }
    if (control->keyboardOnly && input.kind != InputToken::Kind::Keyboard) {
        result.message = control->label + " only works with keyboard keys; that was " + InputToken::describe(input) + ".";
        return result;
    }
    std::vector<std::string> list = tokens(id);
    for (const std::string& existing : list) {
        if (InputToken::same(input, InputToken::parse(existing))) {
            result.info = true;   // nothing to add, but nothing wrong either
            result.message = InputToken::describe(input) + " is already one of its inputs.";
            return result;
        }
    }
    if (control->combo && list.size() >= 2) {
        result.message = "A combo has two inputs: remove one first.";
        return result;
    }
    list.push_back(InputToken::format(input));
    setTokens(id, list);

    result.ok = true;
    result.message = "Added " + InputToken::describe(input) + ".";
    std::vector<std::string> others = usedBy(token, id);
    if (!others.empty()) {
        result.message += " Note: it's also an input of " + joinWords(others) + ", so it does both.";
    }
    result.message += " Save to keep it.";
    return result;
}

Forms::Model::Result ControlsProvider::runAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) {
        return result;
    }
    std::string id = screen.param("id");
    const ControlList::Control* control = ControlList::find(id);

    if (action == "capture") {
        // ManagerUI waits for the press, sets control:captured, then runs control.addCaptured
        result.ok = true;
        result.capture = "control:captured";
        result.captureAction = "control.addCaptured";
        return result;
    }
    if (action == "addCaptured") {
        return addCaptured(id);
    }
    if (action == "remove") {
        std::vector<std::string> list = tokens(id);
        std::string chosen = screen.param("input");
        InputToken::Input input = InputToken::parse(chosen);
        list.erase(std::find(list.begin(), list.end(), chosen));
        setTokens(id, list);
        result.ok = true;
        result.back = true;   // to the control, which shows what's left
        result.message = "Removed " + (input.kind == InputToken::Kind::Invalid ? "\"" + chosen + "\"" : InputToken::describe(input)) + ".";
        if (list.empty() && control->required) {
            result.message += " " + control->label + " has no input now: add one before you save.";
        }
        else {
            result.message += " Save to keep the change.";
        }
        return result;
    }
    if (action == "default") {
        std::string fallback = ControlList::builtinDefault(id);
        setTokens(id, nonEmpty(InputToken::split(fallback)));
        result.ok = true;
        result.message = fallback.empty()
            ? "RetroFE has no default for " + control->label + ", so it has no input now. Save to keep the change."
            : "Back to RetroFE's default: " + describeValue(fallback) + ". Save to keep it.";
        return result;
    }
    if (action == "reset") {
        edits_.reset();
        result.ok = true;
        result.info = true;
        result.message = "Changes undone.";
        return result;
    }
    if (action == "save") {
        Library::EditResult saved = services_.editor->setControls(edits_.changes());
        result.message = saved.message;
        if (!saved.ok) {
            return result;
        }
        edits_.commit();
        load();   // where each value lives now (a new controls.conf, say)
        services_.reloadControls();
        std::vector<std::string> names;
        for (const std::string& file : saved.files) {
            names.push_back(file.substr(file.find_last_of("/\\") + 1));
        }
        result.ok = true;
        result.message = "Saved to " + joinWords(names) + ". RetroFE uses the new controls now.";
        return result;
    }
    return result;
}

}

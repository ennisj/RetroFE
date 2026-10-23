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

#include <memory>
#include <string>
#include <utility>
#include <vector>

// Screens such as Settings and Manage, described as data so that any renderer
// can draw them. A form is a tree of fields; fields read and write values
// through a Model using binding keys like "settings:fullscreen", and buttons
// run named actions. Forms are written in XML:
//
//   <form id="settings" title="Settings">
//     <section label="Display">
//       <toggle label="Fullscreen" bind="settings:fullscreen"/>
//       <text label="Width" bind="settings:horizontal" enabledWhen="!settings:fullscreen"/>
//     </section>
//     <actions>
//       <button label="Save" action="settings.save"/>
//     </actions>
//   </form>
//
// The built-in forms are compiled in; a layout can replace one with
// layouts/<layout>/forms/<id>.xml.
namespace Forms
{
    enum class FieldType
    {
        Section,   // a titled group of fields
        Actions,   // a row of buttons
        Text,      // single line text
        Folder,    // a folder path, with a picker
        File,      // a file path, with a picker
        Toggle,    // on/off
        Number,    // integer between min and max
        Choice,    // one of a list of options
        List,      // read-only list of values
        Note,      // read-only text
        Button     // runs an action
    };

    struct Field
    {
        FieldType type = FieldType::Note;
        std::string label;
        std::string bind;         // model key the value is read from and written to
        std::string action;       // Button: action to run
        std::string hint;         // placeholder shown while empty
        std::string help;         // fixed text shown under the field
        std::string status;       // model key whose value is shown under the field
        std::string enabledWhen;  // model key that must be truthy, or "!key" for falsy
        std::string defaultValue; // draft: the initial value; settings: the value RetroFE uses when nothing sets it
        std::string options;      // Choice: name of the option source
        std::string filterName;   // File: e.g. "Programs"
        std::string pattern;      // File: e.g. "*.exe;*.bat"
        std::string empty;        // List: text shown when there's nothing in it
        int min = 0;
        int max = 0;
        std::vector<Field> children;
    };

    struct Form
    {
        std::string id;
        std::string title;
        std::vector<Field> fields;
    };

    // Values and actions a form is bound to
    class Model
    {
    public:
        virtual ~Model() = default;

        virtual std::string get(const std::string& key) = 0;
        virtual void set(const std::string& key, const std::string& value) = 0;

        // A field's default attribute; what it means depends on the binding
        virtual void setDefault(const std::string& key, const std::string& value) = 0;

        // Values of a List field
        virtual std::vector<std::string> list(const std::string& key) = 0;

        // (value, label) pairs for a Choice field's option source
        virtual std::vector<std::pair<std::string, std::string>> options(const std::string& source) = 0;

        // Why an action can't run right now, or "" if it can
        virtual std::string actionProblem(const std::string& action) = 0;

        struct Result
        {
            bool ok = false;
            std::string message;
            bool reload = false;  // RetroFE has to reload to show the change
        };
        virtual Result runAction(const std::string& action) = 0;
    };

    // "yes", "true", "on" and "1" in any case
    bool isTruthy(const std::string& value);

    // Whether a field's enabledWhen condition holds
    bool isEnabled(const Field& field, Model& model);

    // Parses form XML. Returns nullptr and sets error if it isn't a valid form.
    std::unique_ptr<Form> parse(const std::string& xml, std::string& error);

    // XML of a built-in form, or nullptr (see BuiltinForms.cpp)
    const char* builtinForm(const std::string& id);

    // The layout's version of a form if it has one, otherwise the built-in one
    std::unique_ptr<Form> load(const std::string& id, const std::string& layoutName);

    // Passes every field's default attribute to the model
    void applyDefaults(const Form& form, Model& model);
}

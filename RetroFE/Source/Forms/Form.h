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

#include <cstddef>
#include <map>
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
// layouts/<layout>/forms/<id>.xml. Forms are loaded by id when first opened.
//
// Screens open other screens: a link, a row of a results list, or an action's
// Result pushes a form by id with parameters (e.g. id=<game>), and Back
// returns. The form's bindings read them as param:<name>.
namespace Forms
{
    // A screen's parameters, e.g. { "id": "pacman" }
    using Params = std::map<std::string, std::string>;

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
        Button,    // runs an action
        Results,   // selectable rows from the model; Select opens a form for the row
        Link,      // opens another form with the same parameters
        Value,     // read-only label and value, hidden when empty
        Image      // an image file whose path comes from the model
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
        std::string visibleWhen;  // the same, but the field is hidden rather than greyed out
        std::string defaultValue; // draft: the initial value; settings: the value RetroFE uses when nothing sets it
        std::string options;      // Choice: name of the option source
        std::string filterName;   // File: e.g. "Programs"
        std::string pattern;      // File: e.g. "*.exe;*.bat"
        std::string empty;        // List, Results: text shown when there's nothing in it
        std::string open;         // Link, Results: id of the form to open
        std::string param;        // Results: parameter the chosen row's id is passed as (default "id")
        bool live = false;        // Text: the model gets every change while editing, e.g. for a search box
        int min = 0;
        int max = 0;
        int limit = 200;          // Results: at most this many rows
        int rows = 4;             // Image: height in rows
        std::vector<Field> children;
    };

    struct Form
    {
        std::string id;
        std::string title;
        std::string titleBind;    // model key whose value replaces the title when set
        std::vector<Field> fields;
    };

    // One row of a Results field
    struct Row
    {
        std::string id;       // passed to the opened form as its param
        std::string label;
        std::string detail;   // shown on the right, e.g. a year or a status
        std::string image;    // optional thumbnail, an absolute path
    };

    struct Rows
    {
        std::vector<Row> items;   // at most the limit asked for
        size_t total = 0;         // how many there are in all
    };

    // Where the user was on a form, so returning to it puts them back there
    struct Focus
    {
        int field = -1;        // the focused field's position in the form (see fieldIndex)
        std::string item;      // in a Results field, the focused row's id
        float scroll = 0.0f;
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

        // Rows of a Results field, at most limit of them. Called every frame
        // while the form is shown, so keep it quick (e.g. cache by search text).
        virtual Rows rows(const std::string& key, size_t limit) { (void)key; (void)limit; return {}; }

        // (value, label) pairs for a Choice field's option source
        virtual std::vector<std::pair<std::string, std::string>> options(const std::string& source) = 0;

        // Why an action can't run right now, or "" if it can
        virtual std::string actionProblem(const std::string& action) = 0;

        struct Result
        {
            bool ok = false;
            std::string message;
            bool reload = false;  // RetroFE has to reload to show the change

            // After success, go to another screen: open pushes that form with
            // the current screen's parameters overlaid with params; back
            // returns to the previous screen. The message is shown on the
            // screen it ends up on.
            std::string open;
            Params params;
            bool back = false;
        };
        virtual Result runAction(const std::string& action) = 0;
    };

    // "yes", "true", "on" and "1" in any case
    bool isTruthy(const std::string& value);

    // Fields are numbered in document order, sections' children included,
    // so a focus can be remembered without pointers. -1 / nullptr when not found.
    int fieldIndex(const Form& form, const Field* field);
    const Field* fieldAt(const Form& form, int index);

    // What a Results field says when it isn't showing every row, e.g.
    // "Showing 200 of 5,312. Refine the search to see the rest.", or ""
    std::string rowsNote(const Rows& rows);

    // The parameters of a form opened from one with current: current's, overlaid with added
    Params mergeParams(const Params& current, const Params& added);

    // Whether a field's enabledWhen condition holds
    bool isEnabled(const Field& field, Model& model);

    // Whether a field's visibleWhen condition holds; hidden fields aren't shown at all
    bool isVisible(const Field& field, Model& model);

    // What a note shows: its value, or with bind the model's text. A note
    // with nothing to say ("") isn't shown.
    std::string noteText(const Field& field, Model& model);

    // The form's title: the model's value for its bind attribute, if any, or its title
    std::string title(const Form& form, Model& model);

    // Parses form XML. Returns nullptr and sets error if it isn't a valid form.
    std::unique_ptr<Form> parse(const std::string& xml, std::string& error);

    // XML of a built-in form, or nullptr (see BuiltinForms.cpp)
    const char* builtinForm(const std::string& id);

    // Ids of every built-in form
    std::vector<std::string> builtinFormIds();

    // The layout's version of a form if it has one, otherwise the built-in one
    std::unique_ptr<Form> load(const std::string& id, const std::string& layoutName);

    // Passes every field's default attribute to the model
    void applyDefaults(const Form& form, Model& model);
}

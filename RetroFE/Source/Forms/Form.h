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
#include <functional>
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
        std::string confirm;      // Button: asks this first, e.g. "Delete this game?"; a second Select runs it
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
        bool multiline = false;   // Text: several lines, rows high; the value keeps "\n" between them
        int min = 0;
        int max = 0;
        int limit = 200;          // Results: at most this many rows
        int rows = 4;             // Image, multi-line Text: height in rows
        std::vector<Field> children;
    };

    struct Form
    {
        std::string id;
        std::string title;
        std::string titleBind;    // model key whose value replaces the title when set
        std::vector<Field> fields;
    };

    // One row of a Results field. Choosing it opens the field's form with
    // params, plus id as the field's param (default "id") unless params sets
    // that already. A record that needs more than an id to find it passes the
    // rest in params, e.g. a game's collection.
    struct Row
    {
        std::string id;
        std::string label;
        std::string detail;   // shown on the right, e.g. a year or a status
        std::string image;    // optional thumbnail, an absolute path
        Params params;
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
        std::string item;      // in a Results field, the focused row (see rowKey)
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

        // Rows of a Results field, at most limit of them. Called whenever the
        // screen is rebuilt, so keep it quick (e.g. cache by search text).
        virtual Rows rows(const std::string& key, size_t limit) { (void)key; (void)limit; return {}; }

        // Changes when values changed by themselves (not through set or an
        // action), so a screen shows them straight away. Screens also refresh
        // a few times a second regardless.
        virtual unsigned generation() { return 0; }

        // (value, label) pairs for a Choice field's option source
        virtual std::vector<std::pair<std::string, std::string>> options(const std::string& source) = 0;

        // Why an action can't run right now, or "" if it can
        virtual std::string actionProblem(const std::string& action) = 0;

        struct Result
        {
            bool ok = false;
            std::string message;

            // Nothing happened (cancelled, undone, nothing to do): the
            // message is shown in the neutral style (infoColor) rather than
            // as a success or an error, whatever ok says (see statusKind)
            bool info = false;

            // RetroFE has to reload to show the change: the manager closes and
            // RetroFE reloads now. To reload once the user closes the manager
            // instead, providers call Services::requestReload.
            bool reload = false;

            // After success, go to another screen: open pushes that form with
            // params, plus the current screen's when keepParams (params win);
            // back returns to the previous screen. The message is shown on
            // the screen it ends up on. A link opens with keepParams, a
            // results row without it, so a row always opens its own record.
            // replace with open shows that form in place of the current
            // screen (e.g. what an add form added), so Back skips the form.
            std::string open;
            Params params;
            bool keepParams = false;
            bool back = false;
            bool replace = false;

            // After success, the next input the user presses (a key, a
            // joystick button, hat or axis, a game controller button or
            // axis) is captured instead of moving around the screen: its
            // controls.conf token (Control/InputToken.h) is set() on the
            // binding capture, then the action captureAction runs and its
            // result is handled like any other. Nothing pressed within a few
            // seconds, or an input held down for two, cancels (every input
            // can be captured, Back's included, so it can't be the way out).
            std::string capture;
            std::string captureAction;
        };
        virtual Result runAction(const std::string& action) = 0;
    };

    // How a status message is shown: a success (statusColor), a neutral
    // note when nothing happened or for a prompt (infoColor), an error (errorColor)
    enum class StatusKind { Success, Info, Error };
    inline StatusKind statusKind(const Model::Result& result)
    {
        return result.info ? StatusKind::Info : result.ok ? StatusKind::Success : StatusKind::Error;
    }

    // A button with confirm="...": the first Select arms it, a second one runs
    // it; anything else in between disarms it
    class Confirm
    {
    public:
        // Select on a button: true when its action should run now
        bool select(const Field& button)
        {
            if (button.confirm.empty() || armed_ == &button) {
                armed_ = nullptr;
                return true;
            }
            armed_ = &button;
            return false;
        }
        void cancel() { armed_ = nullptr; }
        const Field* armed() const { return armed_; }

    private:
        const Field* armed_ = nullptr;
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

    // The parameters a row of a Results field opens its form with: the row's
    // params, and its id as the field's param unless they set that already.
    // Nothing comes from the current screen, so a row always opens its own record.
    Params rowParams(const Field& results, const Row& row);

    // Identifies a row within its list, for keeping it focused: its id and params
    std::string rowKey(const Row& row);

    // The parameters of the screen result opens, from a screen with current ones
    Params openParams(const Params& current, const Model::Result& result);

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

    // --- For renderers ---------------------------------------------------------

    // Text that fits in width, as measured by measure: the text itself, or as
    // much of its start as fits followed by "...". Whole UTF-8 characters are
    // kept and spaces before the "..." dropped; "" when not even "..." fits.
    std::string ellipsize(const std::string& text, float width, const std::function<float(const std::string&)>& measure);

    // Text split into lines no wider than width: at spaces, and within a word
    // (at a whole character) only when the word alone is too wide, e.g. a long
    // path. Each line of the text starts a new line; an empty one stays empty.
    std::vector<std::string> wrapText(const std::string& text, float width, const std::function<float(const std::string&)>& measure);

    // How a row of width is shared between a label on the left and a detail on
    // the right, gap apart, given the width each needs. Both get what they need
    // when they fit; otherwise the detail gets what the label leaves, but at
    // most what it needs and at least maxDetailShare of the row, and the label
    // the rest (to be ellipsized).
    struct RowSplit
    {
        float label = 0.0f;
        float detail = 0.0f;   // 0 when there's no detail
    };
    RowSplit splitRow(float labelNeeds, float detailNeeds, float width, float gap, float maxDetailShare = 0.45f);

    // Where focus goes when the focused field has gone (e.g. a button that hid
    // itself once pressed). targets[i] says whether row i can take focus;
    // Guarded rows can (e.g. a file field, whose Select opens a picker) but
    // shouldn't get it by themselves. Prefers the row at previous, then the
    // nearest one above it, then below; a Guarded row only when there's no
    // other. -1 when nothing can take focus.
    enum class FocusTarget { None, Free, Guarded };
    int refocusIndex(const std::vector<FocusTarget>& targets, int previous);

    // A layout attribute's color, "rrggbb" or "#rrggbb" in hex: false (rgb
    // untouched) for anything else, e.g. "red" or "fff"
    bool parseColor(const std::string& text, unsigned char rgb[3]);

    // A layout attribute's number, e.g. "0.85" or "40": false (value untouched)
    // for anything else, e.g. "" or "half"
    bool parseNumber(const std::string& text, float& value);

    // --- Editing text --------------------------------------------------------------

    // Text being edited and where the cursor is: a byte offset, always at the
    // start of a UTF-8 character. A one-line field keeps it at the end.
    struct TextEdit
    {
        std::string text;
        size_t cursor = 0;

        void set(const std::string& value) { text = value; cursor = text.size(); }
        void insert(const std::string& typed);   // at the cursor, which moves past it
        void backspace();                        // the character before the cursor
        void erase();                            // the character at the cursor (Delete)
        void left();
        void right();
    };

    // The byte offset of the next / previous UTF-8 character
    size_t nextCharacter(const std::string& text, size_t position);
    size_t previousCharacter(const std::string& text, size_t position);

    // A multi-line text as drawn: lines no wider than width, each the bytes
    // [start, end) of the text. A line breaks after a run of spaces (they stay
    // at its end), within a word only when the word alone is too wide, and at
    // every '\n' (which belongs to neither line). There's always at least one
    // line: an empty text, or one that ends in '\n', ends with an empty line.
    struct TextLine
    {
        size_t start = 0;
        size_t end = 0;
    };
    std::vector<TextLine> layoutLines(const std::string& text, float width,
                                      const std::function<float(const std::string&)>& measure);

    // The line a cursor is drawn on: where a line wraps, the start of the next one
    size_t lineAt(const std::vector<TextLine>& lines, size_t cursor);

    // Where the cursor goes on Up (direction -1) or Down (+1): the line above
    // or below, as near the same x as it allows; from the first line up to the
    // start of the text, from the last down to its end
    size_t moveCursorVertically(const std::string& text, const std::vector<TextLine>& lines, size_t cursor, int direction,
                                const std::function<float(const std::string&)>& measure);

    // Home and End: the start or end of the cursor's line (before the space
    // where a line wraps, so the cursor stays on it)
    size_t lineHome(const std::vector<TextLine>& lines, size_t cursor);
    size_t lineEnd(const std::string& text, const std::vector<TextLine>& lines, size_t cursor);
}

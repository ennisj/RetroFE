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

// The form page's layout and look (docs/UX-REVIEW.md parcels B and C): the
// pinned action bar (which actions, scrolling, focus moving in and out,
// the jump), status severities, and the bare install's window size.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Forms/Form.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Provider.h"
#include "../Source/Utility/WindowSize.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Test;
using Forms::FocusMove;
using Forms::RowShape;
using Forms::Severity;

namespace
{
    std::unique_ptr<Forms::Form> parsed(const std::string& xml)
    {
        std::string error;
        auto form = Forms::parse(xml, error);
        check(form != nullptr, "parses: " + error);
        return form;
    }

    // n plain focusable rows
    std::vector<RowShape> plainRows(int n)
    {
        RowShape row;
        row.focusable = true;
        return std::vector<RowShape>(static_cast<size_t>(n), row);
    }

    // A provider that says how its statuses read
    class StatusProvider : public Manager::Provider
    {
    public:
        std::string prefix() const override { return "probe"; }
        std::string get(const std::string& key, const Manager::Screen&) override
        {
            return key == "launcher" ? "Won't start" : key == "files" ? "3 files" : "";
        }
        Severity severity(const std::string& key, const Manager::Screen& screen) override
        {
            if (key == "launcher") return Severity::Warning;
            if (key == "files") return screen.param("id") == "broken" ? Severity::Error : Severity::None;
            return Severity::None;
        }
    };
}

static Test::Suite formLookSuite("form look", [] {
    run("form look: severities parse from their names, and anything else is none", [] {
        check(Forms::parseSeverity("ok") == Severity::Ok, "ok");
        check(Forms::parseSeverity("Warning") == Severity::Warning, "Warning, any case");
        check(Forms::parseSeverity(" ERROR ") == Severity::Error, "spaces around");
        check(Forms::parseSeverity("") == Severity::None, "empty");
        check(Forms::parseSeverity("red") == Severity::None, "unknown");
        auto form = parsed(R"(<form id="t"><note value="Careful" severity="warning"/><note value="Plain"/></form>)");
        if (!form) return;
        check(form->fields[0].severity == Severity::Warning, "a note's severity attribute");
        check(form->fields[1].severity == Severity::None, "none by default");
    });

    run("form look: the model's severity reaches providers and wins over a note's attribute", [] {
        std::string root = makeRoot("formlook_severity");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        check(model.addProvider(std::make_unique<StatusProvider>()), "provider added");
        model.setScreen("game", { { "id", "fine" } });
        check(model.severity("probe:launcher") == Severity::Warning, "a provider's warning");
        check(model.severity("probe:files") == Severity::None, "a plain fact");
        model.setScreen("game", { { "id", "broken" } });
        check(model.severity("probe:files") == Severity::Error, "asked for the screen shown");
        check(model.severity("nobody:files") == Severity::None, "no provider: none");
        check(model.severity("draft:name") == Severity::None, "built-in bindings: none");

        auto form = parsed(R"(<form id="t">
              <note bind="probe:launcher" severity="error"/>
              <note bind="probe:missing" severity="ok"/>
              <note bind="probe:files"/>
            </form>)");
        if (!form) return;
        check(Forms::noteSeverity(form->fields[0], model) == Severity::Warning, "the model's word wins");
        check(Forms::noteSeverity(form->fields[1], model) == Severity::Ok, "else the attribute");
        check(Forms::noteSeverity(form->fields[2], model) == Severity::Error, "bound, no attribute");
    });

    run("form look: Add a system's games folder count warns when it finds nothing", [] {
        std::string root = makeRoot("formlook_romcount");
        writeFile(root + "/roms/a.zip", "x");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        model.set("draft:romPath", root + "/nowhere");
        check(model.severity("info:romCount") == Severity::Error, "no such folder: " + model.get("info:romCount"));
        model.set("draft:romPath", root + "/roms");
        model.set("draft:extensions", "nes");
        check(model.severity("info:romCount") == Severity::Warning, "nothing matching: " + model.get("info:romCount"));
        model.set("draft:extensions", "zip");
        check(model.severity("info:romCount") == Severity::None, "found some: " + model.get("info:romCount"));
    });

    run("form look: the first top-level actions and the ones straight after it are pinned", [] {
        auto form = parsed(R"(<form id="t">
              <text label="Title" bind="draft:a"/>
              <section label="More"><actions><button label="Inner" action="x"/></actions></section>
              <actions><button label="Save" action="s"/><button label="Reset" action="r"/></actions>
              <actions><button label="Put back the original" action="o"/></actions>
              <note value="between"/>
              <actions><button label="Later" action="l"/></actions>
            </form>)");
        if (!form) return;
        std::vector<const Forms::Field*> pinned = Forms::pinnedActions(*form);
        check(pinned.size() == 2, "two blocks: " + std::to_string(pinned.size()));
        if (pinned.size() == 2) {
            checkEqual(pinned[0]->children[0].label, "Save", "Save's block first");
            checkEqual(pinned[1]->children[0].label, "Put back the original", "then the one after it");
        }

        auto optOut = parsed(R"(<form id="t">
              <actions pin="no"><button label="Move up" action="u"/></actions>
              <actions><button label="Save" action="s"/></actions>
            </form>)");
        if (optOut) {
            check(Forms::pinnedActions(*optOut).empty(), "pin=\"no\" on the first: nothing pinned");
        }
        auto stops = parsed(R"(<form id="t">
              <actions><button label="Save" action="s"/></actions>
              <actions pin="no"><button label="Delete" action="d"/></actions>
            </form>)");
        if (stops) {
            check(Forms::pinnedActions(*stops).size() == 1, "pin=\"no\" on a later one ends the run");
        }
        auto none = parsed(R"(<form id="t"><section label="S"><actions><button label="Go" action="g"/></actions></section></form>)");
        if (none) {
            check(Forms::pinnedActions(*none).empty(), "only inside a section: nothing pinned");
        }
    });

    run("form look: every built-in form's Save is in its pinned bar", [] {
        // The long forms are the reason for the bar: Save must never be left at the bottom
        for (const std::string& id : Forms::builtinFormIds()) {
            const char* xml = Forms::builtinForm(id);
            std::string error;
            auto form = xml ? Forms::parse(xml, error) : nullptr;
            if (!form) continue;   // FormsTests checks they all parse
            std::vector<const Forms::Field*> pinned = Forms::pinnedActions(*form);
            std::function<void(const std::vector<Forms::Field>&, bool)> walk = [&](const std::vector<Forms::Field>& fields, bool inPinned) {
                for (const Forms::Field& field : fields) {
                    bool pinnedHere = inPinned || std::find(pinned.begin(), pinned.end(), &field) != pinned.end();
                    if (field.type == Forms::FieldType::Button && field.label == "Save") {
                        check(pinnedHere, id + ": Save is pinned");
                    }
                    walk(field.children, pinnedHere);
                }
            };
            walk(form->fields, false);
        }
    });

    run("form look: scrolling keeps the focused row in view above a pinned bar", [] {
        const float row = 50.0f;
        std::vector<RowShape> rows = plainRows(20);
        // 10 rows fit without the bar, 8 with it: the same focus scrolls further
        checkEqual(std::to_string(static_cast<int>(Forms::scrollToShow(rows, 12, 0.0f, row, 500.0f))), "150", "no bar");
        checkEqual(std::to_string(static_cast<int>(Forms::scrollToShow(rows, 12, 0.0f, row, 400.0f))), "250", "with a bar");
        // Focus in the bar: the rows stay where they were, only kept in range
        checkEqual(std::to_string(static_cast<int>(Forms::scrollToShow(rows, -1, 250.0f, row, 400.0f))), "250", "bar focused: unchanged");
        checkEqual(std::to_string(static_cast<int>(Forms::scrollToShow(rows, -1, 5000.0f, row, 400.0f))), "600", "but never past the end");
        // Up again: back to the top
        checkEqual(std::to_string(static_cast<int>(Forms::scrollToShow(rows, 0, 250.0f, row, 400.0f))), "0", "first row");
        // A header comes into view with its field, a note under a field with it
        std::vector<RowShape> grouped = plainRows(12);
        grouped[4].focusable = false;
        grouped[4].header = true;
        grouped[7].focusable = false;
        grouped[7].attached = true;
        checkEqual(std::to_string(static_cast<int>(Forms::scrollToShow(grouped, 5, 300.0f, row, 200.0f))), "200", "header shown");
        checkEqual(std::to_string(static_cast<int>(Forms::scrollToShow(grouped, 6, 0.0f, row, 200.0f))), "200", "note under it shown");
        // The last field shows the notes after it, rounded to whole rows
        std::vector<RowShape> tail = plainRows(6);
        tail[4].focusable = false;
        tail[5].focusable = false;
        checkEqual(std::to_string(static_cast<int>(Forms::scrollToShow(tail, 3, 0.0f, row, 180.0f))), "150", "notes after the last field");
    });

    run("form look: Up and Down go through the rows into the pinned bar and back", [] {
        std::vector<bool> rows = { false, true, true, false, true };   // a title, two fields, a note, a field
        FocusMove m = Forms::moveFocus(rows, 2, false, 2, 1);
        check(!m.bar && m.row == 4, "Down skips the note");
        m = Forms::moveFocus(rows, 4, false, 2, 1);
        check(m.bar && m.row == -1, "Down past the last row: the bar");
        m = Forms::moveFocus(rows, -1, true, 2, -1, 2);
        check(!m.bar && m.row == 2, "Up from the bar: back where it came from");
        m = Forms::moveFocus(rows, -1, true, 2, -1);
        check(!m.bar && m.row == 4, "or the last row");
        m = Forms::moveFocus(rows, -1, true, 2, 1);
        check(!m.bar && m.row == 1, "Down from the bar: round to the first");
        m = Forms::moveFocus(rows, 1, false, 2, -1);
        check(m.bar, "Up past the first row: the bar");
        m = Forms::moveFocus(rows, 4, false, 0, 1);
        check(!m.bar && m.row == 1, "no bar: round as before");
        m = Forms::moveFocus({ false, false }, -1, true, 1, -1);
        check(m.bar, "nothing else takes focus: stays in the bar");
    });

    run("form look: the jump goes to the bar and back, and pages without one", [] {
        std::vector<bool> rows(30, true);
        rows[0] = false;
        FocusMove m = Forms::jumpFocus(rows, 3, false, 2, false, 10);
        check(m.bar, "Page down: into the bar from anywhere");
        m = Forms::jumpFocus(rows, -1, true, 2, true, 10, 3);
        check(!m.bar && m.row == 3, "Page up: back to the row it came from");
        m = Forms::jumpFocus(rows, -1, true, 2, true, 10, 99);
        check(!m.bar && m.row == 29, "a row that went away: the last");
        m = Forms::jumpFocus(rows, -1, true, 2, false, 10);
        check(m.bar, "Page down in the bar: stays");
        m = Forms::jumpFocus(rows, 3, false, 0, false, 10);
        check(!m.bar && m.row == 13, "no bar: a page down");
        m = Forms::jumpFocus(rows, 25, false, 0, false, 10);
        check(m.row == 29, "to the end");
        m = Forms::jumpFocus(rows, 5, false, 2, true, 10);
        check(!m.bar && m.row == 1, "Page up from the rows: a page up, past the title to the first");
        std::vector<bool> sparse = { true, false, false, false, true };
        m = Forms::jumpFocus(sparse, 0, false, 0, false, 2);
        check(m.row == 4, "a page lands on the nearest row that takes focus");
    });

    run("form look: the jump lands on the first button that can run", [] {
        check(Forms::barTarget({}) == -1, "no buttons");
        check(Forms::barTarget({ false, true, true }) == 1, "Save can't run (nothing changed): the next");
        check(Forms::barTarget({ true, true }) == 0, "Save");
        check(Forms::barTarget({ false, false }) == 0, "none can: the first, to say why");
    });

    run("form look: the bar's buttons go left to right and wrap", [] {
        std::vector<Forms::BarSlot> slots = Forms::layoutBar({ 100.0f, 200.0f, 300.0f }, 400.0f, 10.0f);
        check(slots.size() == 3, "three slots");
        if (slots.size() != 3) return;
        check(slots[0].x == 0.0f && slots[0].line == 0, "first");
        check(slots[1].x == 110.0f && slots[1].line == 0, "second beside it, a gap apart");
        check(slots[2].x == 0.0f && slots[2].line == 1, "third doesn't fit: next line");
        slots = Forms::layoutBar({ 900.0f, 50.0f }, 400.0f, 10.0f);
        check(slots[0].width == 400.0f && slots[0].line == 0, "too wide: cut to the width");
        check(slots[1].line == 1, "and the next on a line of its own");
    });

    run("form look: a help panel's pictures pick screens by form id", [] {
        check(Forms::matchesFormId({}, "games"), "no forms: every screen");
        check(Forms::matchesFormId({ "settings", "control*" }, "settings"), "the same id");
        check(Forms::matchesFormId({ "settings", "control*" }, "control-input"), "a trailing * matches the start");
        check(Forms::matchesFormId({ "control*" }, "control"), "and the start alone");
        check(!Forms::matchesFormId({ "settings", "control*" }, "game"), "anything else doesn't");
        check(!Forms::matchesFormId({ "settings" }, "settings-more"), "no * : the whole id");
        auto form = parsed(R"(<form id="t"><note value="Why" panel="yes"/><note value="Plain"/><link label="L" open="x" preview="game:image"/></form>)");
        if (!form) return;
        check(form->fields[0].panel && !form->fields[1].panel, "panel=\"yes\" on a note");
        checkEqual(form->fields[2].preview, "game:image", "a field's preview binding");
    });

    run("form look: a bare install's window fits the desktop at 16:9", [] {
        WindowSize::Size size = WindowSize::automatic(1920, 1032);
        check(size.width == 1600 && size.height == 900, "1080p with a taskbar: " + std::to_string(size.width) + "x" + std::to_string(size.height));
        size = WindowSize::automatic(3840, 2100);
        check(size.width * 9 == size.height * 16 && size.width <= 3456, "4K: 16:9, within the share");
        size = WindowSize::automatic(1366, 728);
        check(size.width <= 1366 && size.height + 32 <= 728 && size.width * 9 == size.height * 16, "a small laptop");
        size = WindowSize::automatic(0, 0);
        check(size.width == 1280 && size.height == 720, "no display size: the old default");
        size = WindowSize::automatic(500, 400);
        check(size.width <= 500 && size.height <= 400, "never larger than the display");
    });
});

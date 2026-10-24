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
// controls.conf tokens (InputToken), "press it now" (InputCapture), the
// controls writer (Editor::setControls) and Settings > Controls (ControlsProvider)

#include "TestSupport.h"
#include "../Source/Control/InputToken.h"
#include "../Source/Control/JoystickSlots.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Library/Edits.h"
#include "../Source/Manager/Controls/ControlsProvider.h"
#include "../Source/Manager/Controls/InputCapture.h"
#include "../Source/Manager/ManagerModel.h"
#include <filesystem>
#include <memory>

using namespace Test;
namespace fs = std::filesystem;

namespace
{
    using InputToken::Kind;

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // OneSauce's controls.conf (the first lines, as shipped), with CRLF and comments
    const char* const oneSauce =
        "up = Up,joyDPUp,joyLeftY-\r\n"
        "down = Down,joyDPDown,joyLeftY+\r\n"
        "left = Left,joyDPLeft,joyLeftX-\r\n"
        "right = Right,joyDPRight,joyLeftX+\r\n"
        "\r\n"
        "pageUp = PageUp\r\n"
        "pageDown = PageDown\r\n"
        "\r\n"
        "letterUp = M,joyRightShoulder\r\n"
        "letterDown = N,joyRightTrigger\r\n"
        "# collectionUp = Right,joyHat0Right,joyAxis0+\r\n"
        "nextPlaylist = F2,Keypad 4,joyX\r\n"
        "addPlaylist = I,joyY\r\n"
        "removePlaylist = O,joyB\r\n"
        "select = 1,Return,joyA,joyStart\r\n"
        "back = F11,joyBack,Escape   # rewind\r\n"
        "quit = Q\r\n"
        "menu = G,joyGuide\r\n"
        "deadZone = 30\r\n";

    struct Fixture
    {
        explicit Fixture(const std::string& root, std::function<std::vector<std::string>(const std::string&)> sameInputs = {})
            : editor(root)
        {
            Configuration::absolutePath = root;
            config = std::make_unique<Configuration>();
            model = std::make_unique<ManagerModel>(*config);
            Manager::Services services{ *config };
            services.editor = &editor;
            services.reloadControls = [this] { reloads++; };
            services.sameInputs = std::move(sameInputs);
            model->addProvider(std::make_unique<Manager::ControlsProvider>(services));
        }

        const Forms::Row* row(const std::string& group, const std::string& id)
        {
            rows = model->rows("control:group:" + group, 200);
            for (const Forms::Row& item : rows.items) {
                if (item.id == id) {
                    return &item;
                }
            }
            return nullptr;
        }

        // Presses an input on the control's screen as ManagerUI does after control.capture
        Forms::Model::Result capture(const std::string& token)
        {
            Forms::Model::Result asked = model->runAction("control.capture");
            check(asked.ok && asked.capture == "control:captured" && asked.captureAction == "control.addCaptured",
                  "control.capture asks for a capture");
            model->set(asked.capture, token);
            return model->runAction(asked.captureAction);
        }

        Library::Editor editor;
        std::unique_ptr<Configuration> config;
        std::unique_ptr<ManagerModel> model;
        Forms::Rows rows;
        int reloads = 0;
    };
}

static Test::Suite controlsSuite("controls", [] {
    run("controls: every token in OneSauce's controls.conf is understood", [] {
        for (const char* token : { "joyDPUp", "joyDPDown", "joyDPLeft", "joyDPRight", "joyLeftY-", "joyLeftY+",
                                   "joyLeftX-", "joyLeftX+", "joyRightShoulder", "joyRightTrigger", "joyX", "joyY",
                                   "joyB", "joyA", "joyStart", "joyBack", "joyGuide", "joyLeftShoulder",
                                   "joyLeftTrigger", "joy0DPDown", "joyHat0Right", "joyAxis0+", "joy0Button1",
                                   "Keypad 4", "F11", "Escape", "1" }) {
            InputToken::Input input = InputToken::parse(token);
            check(input.kind != Kind::Invalid, std::string(token) + " parses (" + input.problem + ")");
        }
        InputToken::Input a = InputToken::parse("joyA");
        check(a.kind == Kind::ControllerButton && a.index == 0 && a.joystick == -1, "joyA is controller button A on any pad");
        InputToken::Input dp = InputToken::parse("joy1dpdown");
        check(dp.kind == Kind::ControllerButton && dp.index == 12 && dp.joystick == 1, "case-insensitive, with a pad number (SDL's DPAD_DOWN = 12)");
        InputToken::Input stick = InputToken::parse("joyLeftY-");
        check(stick.kind == Kind::ControllerAxis && stick.index == InputToken::AxisLeftY && stick.value == -1, "left stick up");
        InputToken::Input trigger = InputToken::parse("joyRightTrigger");
        check(trigger.kind == Kind::ControllerAxis && trigger.index == InputToken::AxisRightTrigger && trigger.value == 1, "a trigger needs no sign");
        checkEqual(InputToken::describe(stick), "Controller left stick up", "described");
        checkEqual(InputToken::describe(InputToken::parse("joyRightShoulder")), "Controller right shoulder", "described");
        checkEqual(InputToken::describe(InputToken::parse("Return")), "Keyboard Return", "a key described");
    });

    run("controls: the older joystick, mouse and keyboard tokens still parse the same", [] {
        InputToken::Input button = InputToken::parse("joy0Button1");
        check(button.kind == Kind::JoyButton && button.joystick == 0 && button.index == 1, "joy0Button1");
        InputToken::Input hat = InputToken::parse("joyHat0LeftUp");
        check(hat.kind == Kind::JoyHat && hat.joystick == -1 && hat.index == 0 && hat.value == InputToken::HatLeftUp, "joyHat0LeftUp");
        InputToken::Input axis = InputToken::parse("JOYAXIS5+");
        check(axis.kind == Kind::JoyAxis && axis.index == 5 && axis.value == 1, "joyAxis5+, any case");
        InputToken::Input mouse = InputToken::parse("mouseButtonRight");
        check(mouse.kind == Kind::Mouse && mouse.index == InputToken::MouseRight, "mouseButtonRight");
        InputToken::Input key = InputToken::parse("  Keypad Enter ");
        check(key.kind == Kind::Keyboard && key.key == "Keypad Enter", "a key name, trimmed");
    });

    run("controls: unknown tokens are invalid with a reason, never a crash", [] {
        for (const char* token : { "joy", "joy0", "joyFoo", "joyA+", "joyLeftX", "joyRightTrigger-", "joyHat0",
                                   "joyHatXUp", "joyHat0Sideways", "joyAxis1", "joyAxisX+", "joyButton", "joyButtonA",
                                   "mouseButtonFourth", "mouse", "#", "" }) {
            InputToken::Input input = InputToken::parse(token);
            check(input.kind == Kind::Invalid && !input.problem.empty(), std::string("\"") + token + "\" is refused with a reason");
        }
        // Keyboard names are checked against SDL's in RetroFE; here, a stand-in
        InputToken::setKeyNameCheck([](const std::string& name) { return name == "Up" || name == "Return"; });
        check(InputToken::parse("Return").kind == Kind::Keyboard, "a known key");
        InputToken::Input typo = InputToken::parse("Retrun");
        check(typo.kind == Kind::Invalid && contains(typo.problem, "Retrun"), "an unknown key name says which");
        InputToken::setKeyNameCheck(nullptr);
    });

    run("controls: tokens are written back as RetroFE reads them", [] {
        for (const char* token : { "joyA", "joy2DPLeft", "joyLeftY-", "joyRightX+", "joyRightTrigger", "joyButton12",
                                   "joy1Hat0RightDown", "joyAxis3-", "mouseButtonX1", "Keypad 4" }) {
            checkEqual(InputToken::format(InputToken::parse(token)), token, "round trip");
        }
        checkEqual(InputToken::format(InputToken::parse("joyrighttrigger+")), "joyRightTrigger", "spelling normalised");
        check(InputToken::same(InputToken::parse("return"), InputToken::parse("Return")), "key names ignore case");
        check(!InputToken::same(InputToken::parse("joyA"), InputToken::parse("joy0A")), "any pad isn't pad 0");
    });

    run("controls: values split and join the way UserInput reads them, comma key included", [] {
        std::vector<std::string> tokens = InputToken::split("Up , joyDPUp,joyLeftY-");
        check(tokens == std::vector<std::string>{ "Up", "joyDPUp", "joyLeftY-" }, "split and trimmed");
        check(InputToken::split(",") == std::vector<std::string>{ "," }, "a lone comma is the comma key");
        check(InputToken::split(",Up") == std::vector<std::string>{ ",", "Up" }, "a leading comma too");
        checkEqual(InputToken::join({ "Up", ",", "joyA" }), ",Up,joyA", "the comma key goes first");
        check(InputToken::split(InputToken::join({ "Up", ",", "joyA" })) == std::vector<std::string>{ ",", "Up", "joyA" },
              "and reads back");
        checkEqual(InputToken::join({ "," }), ",", "alone");
        checkEqual(InputToken::join({}), "", "no inputs");
    });

    run("controls: built-in defaults and the control list", [] {
        checkEqual(ControlList::builtinDefault("select"), "Return,joyA", "from Builtin::controls: pads by controller name");
        checkEqual(ControlList::builtinDefault("manager"), "F10", "the Settings key");
        checkEqual(ControlList::builtinDefault("letterUp"), "", "none");
        int required = 0;
        for (const ControlList::Control& control : ControlList::all()) {
            required += control.required ? 1 : 0;
        }
        checkEqual(std::to_string(required), "4", "up, down, select and back are guarded (Open Settings falls back to F10)");
        check(ControlList::find("quitCombo") && ControlList::find("quitCombo")->combo, "combos are marked");
    });

    run("controls: press it now captures on release, prefers the controller's name, and has ways out", [] {
        using Manager::InputCapture;
        InputCapture capture;
        capture.start(10.0f);
        check(capture.active(), "waiting");
        capture.release(InputToken::parse("Return"), 10.1f);
        check(capture.active(), "letting go of the Select that started it does nothing");
        capture.press(InputToken::parse("joyButton0"), 11.0f);
        capture.press(InputToken::parse("joyA"), 11.01f);   // the same press, as SDL sends it twice
        check(contains(capture.prompt(11.1f), "Let go"), "says to let go");
        capture.release(InputToken::parse("joyButton0"), 11.2f);
        check(capture.state() == InputCapture::State::Captured, "captured on release");
        checkEqual(capture.token(), "joyA", "the controller's name wins");

        capture.start(0.0f);
        capture.press(InputToken::parse("joyB"), 1.0f);
        capture.press(InputToken::parse("joyButton1"), 1.05f);   // controller event first this time
        capture.release(InputToken::parse("joyButton1"), 1.3f);
        check(capture.state() == InputCapture::State::Captured && capture.token() == "joyB", "either order");

        capture.start(0.0f);
        capture.press(InputToken::parse("Escape"), 1.0f);
        capture.update(2.5f);
        check(capture.active(), "still held");
        capture.update(3.0f);
        check(capture.state() == InputCapture::State::Cancelled, "held for 2 seconds cancels");
        check(!capture.endMessage().empty(), "and says so");

        capture.start(0.0f);
        check(contains(capture.prompt(0.2f), "(5 seconds left)") && contains(capture.prompt(4.5f), "(1 second left)"), "counts down");
        capture.update(4.9f);
        check(capture.active(), "not yet");
        capture.update(5.0f);
        check(capture.state() == InputCapture::State::TimedOut, "nothing pressed in 5 seconds");

        capture.start(0.0f);
        capture.press(InputToken::parse("Up"), 1.0f);
        capture.press(InputToken::parse("Down"), 1.1f);
        capture.release(InputToken::parse("Down"), 1.2f);
        check(capture.active(), "another key meanwhile is ignored");
        capture.release(InputToken::parse("Up"), 1.3f);
        checkEqual(capture.token(), "Up", "the first press counts");
    });

    run("controls: the writer keeps overrides, comments and line endings, and backs up", [] {
        std::string root = makeRoot("controls_writer");
        writeFile(root + "/controls.conf", oneSauce);
        writeFile(root + "/controls1.conf", "# local\nselect = Return,joyButton0\n");
        Library::Editor editor(root);

        Library::EditResult result = editor.setControls({ { "select", "Return,joyA" }, { "letterUp", "M,joyRightShoulder,PageUp" } });
        check(result.ok, "saved: " + result.message);
        checkEqual(readFile(root + "/controls1.conf"), "# local\nselect = Return,joyA\n", "select goes where it's overridden");
        std::string base = readFile(root + "/controls.conf");
        check(contains(base, "letterUp = M,joyRightShoulder,PageUp\r\n"), "letterUp in controls.conf, CRLF kept");
        check(contains(base, "back = F11,joyBack,Escape   # rewind\r\n"), "other lines untouched");
        check(contains(base, "# collectionUp = Right"), "comments kept");
        check(fs::exists(root + "/manage/backups/controls.conf") && fs::exists(root + "/manage/backups/controls1.conf"),
              "originals backed up");
        checkEqual(readFile(root + "/manage/backups/controls.conf"), oneSauce, "the original as it was");

        check(!editor.setControls({ { "nonsense", "A" } }).ok, "only controls");
        check(!editor.setControls({ { "up", "joyNope" } }).ok, "only tokens RetroFE reads");
        check(!editor.setControls({ { "deadZone", "300" } }).ok, "dead zone 0-100");
        Library::EditResult locked = editor.setControls({ { "back", "" } });
        check(!locked.ok && contains(locked.message, "Back"), "Back can't be left without an input: " + locked.message);
        checkEqual(Library::Editor::checkControls({ { "manager", "joyGuide" } }), "", "Open Settings with only a pad button: F10 at runtime, not a lock-out");
        checkEqual(Library::Editor::checkControls({ { "manager", "joyGuide,F9" } }), "", "with a key too");
        checkEqual(Library::Editor::checkControls({ { "letterUp", "" } }), "", "optional controls may be empty");
        check(!Library::Editor::checkControls({ { "up", "joyNope" } }).empty(), "a guarded control with nothing that works");
        checkEqual(readFile(root + "/controls1.conf"), "# local\nselect = Return,joyA\n", "select only in the last file that sets it");
        check(contains(readFile(root + "/controls.conf"), "select = 1,Return,joyA,joyStart\r\n"), "controls.conf's select untouched");
        check(contains(readFile(root + "/controls.conf"), "back = F11,joyBack,Escape"), "nothing written by refusals");
    });

    run("controls: with no controls.conf, the first save makes one from the built-in controls", [] {
        std::string root = makeRoot("controls_new");
        Library::Editor editor(root);
        Library::EditResult result = editor.setControls({ { "letterDown", "N" } });
        check(result.ok, "saved: " + result.message);
        std::string text = readFile(root + "/controls.conf");
        check(contains(text, "select = Return,joyA"), "the built-in controls RetroFE was using");
        check(contains(text, "letterDown = N"), "plus the change");
        check(fs::exists(root + "/manage/backups/controls.conf.absent"), "recorded as new, so a reset removes it");
    });

    run("controls: unrecognised inputs are pointed out, with what RetroFE does instead", [] {
        std::string root = makeRoot("controls_unknown");
        writeFile(root + "/controls.conf", oneSauce);
        writeFile(root + "/controls1.conf", "select = joyFoo\nletterDown = N,joyLeftZ+\n");
        Fixture f(root);
        f.model->setScreen("controls", {});
        std::string problems = f.model->get("control:problems");
        check(contains(problems, "Select (joyFoo)") && contains(problems, "Letter down (joyLeftZ+)"), "listed: " + problems);
        f.model->setScreen("control", { { "id", "select" } });
        std::string notes = f.model->get("control:notes");
        check(contains(notes, "RetroFE skips \"joyFoo\"") && contains(notes, "uses its default"), "explained: " + notes);
        f.model->set("control:deadZone", "20");
        checkEqual(f.model->actionProblem("control.save"), "", "unrelated changes aren't held up by a control left as it was");
    });

    run("controls: the Controls screen lists, captures, removes, guards and saves", [] {
        std::string root = makeRoot("controls_screen");
        writeFile(root + "/controls.conf", oneSauce);
        writeFile(root + "/controls1.conf", "letterUp = M,joyButton5\n");
        Fixture f(root);

        f.model->setScreen("controls", {});
        const Forms::Row* select = f.row("Essential", "select");
        check(select && select->detail == "Keyboard 1, Keyboard Return, Controller A, Controller Start", "inputs described");
        const Forms::Row* letter = f.row("Navigation", "letterUp");
        check(letter && contains(letter->detail, "Joystick button 5"), "the override in controls1.conf wins");
        const Forms::Row* manager = f.row("Essential", "manager");
        check(manager && manager->detail == "Keyboard F10", "Open Settings defaults to F10");
        checkEqual(f.model->get("control:problems"), "", "nothing unrecognised in OneSauce's file");
        checkEqual(f.model->get("control:deadZone"), "30", "dead zone");

        f.model->setScreen("control", { { "id", "letterUp" } });
        check(contains(f.model->get("control:notes"), "controls1.conf"), "says where it's set");
        Forms::Model::Result added = f.capture("PageUp");
        check(added.ok && contains(added.message, "Added Keyboard PageUp"), "added: " + added.message);
        check(contains(added.message, "Page up"), "warns PageUp is also Page up's input: " + added.message);
        Forms::Model::Result again = f.capture("PageUp");
        check(!again.ok && contains(again.message, "already"), "not twice");
        checkEqual(f.model->get("control:isDirty"), "yes", "a change waiting");

        f.model->setScreen("control", { { "id", "manager" } });
        Forms::Model::Result pad = f.capture("joyGuide");
        check(!pad.ok && contains(pad.message, "keyboard"), "Open Settings takes only keys");

        f.model->setScreen("control", { { "id", "select" } });
        Forms::Rows inputs = f.model->rows("control:inputs", 200);
        check(inputs.items.size() == 4 && inputs.items[2].label == "Controller A" && inputs.items[2].detail == "joyA",
              "one row per input");
        for (const Forms::Row& input : inputs.items) {
            f.model->setScreen("control-input", { { "id", "select" }, { "input", input.id } });
            checkEqual(f.model->get("control:inputToken"), input.id, "the input's screen");
            Forms::Model::Result removed = f.model->runAction("control.remove");
            check(removed.ok && removed.back, "removed, back to the control: " + removed.message);
        }
        check(!f.model->actionProblem("control.remove").empty(), "gone");
        f.model->setScreen("control", { { "id", "select" } });
        checkEqual(f.model->get("control:hasInputs"), "no", "every input removed");
        check(contains(f.model->actionProblem("control.save"), "Select"), "Save refuses while Select has none");
        Forms::Model::Result back = f.model->runAction("control.default");
        check(back.ok && f.model->get("control:hasInputs") == "yes", "the default put back");
        checkEqual(f.model->actionProblem("control.default"), "It's already RetroFE's default.", "once");

        f.model->setScreen("controls", {});
        check(!f.model->leaving({ "controls", {} }).empty(), "leaving with changes warns");
        checkEqual(f.model->actionProblem("control.save"), "", "can save now");
        Forms::Model::Result saved = f.model->runAction("control.save");
        check(saved.ok && contains(saved.message, "controls1.conf") && contains(saved.message, "controls.conf"), "saved: " + saved.message);
        checkEqual(std::to_string(f.reloads), "1", "RetroFE re-reads its controls");
        checkEqual(readFile(root + "/controls1.conf"), "letterUp = M,joyButton5,PageUp\n", "letterUp in its override");
        check(contains(readFile(root + "/controls.conf"), "select = Return,joyA\r\n"), "select's default in controls.conf");
        checkEqual(f.model->get("control:isDirty"), "no", "nothing waiting");
        check(f.model->leaving({ "controls", {} }).empty(), "leaving is fine now");
    });

    run("controls: key names with a comma can't be written; the comma key itself round-trips", [] {
        InputToken::Input keypad = InputToken::parse("Keypad ,");
        check(keypad.kind == Kind::Invalid && contains(keypad.problem, "comma"), "refused: " + keypad.problem);
        check(InputToken::parse(",").kind == Kind::Keyboard, "the comma key is fine");

        std::string root = makeRoot("controls_comma");
        writeFile(root + "/controls.conf", oneSauce);
        Fixture f(root);
        f.model->setScreen("control", { { "id", "quit" } });
        Forms::Model::Result comma = f.capture(",");
        check(comma.ok, "the comma key captured: " + comma.message);
        Forms::Model::Result keypadComma = f.capture("Keypad ,");
        check(!keypadComma.ok && contains(keypadComma.message, "comma"), "Keypad , isn't added: " + keypadComma.message);
        check(f.model->runAction("control.save").ok, "saved");
        check(contains(readFile(root + "/controls.conf"), "quit = ,Q\r\n"), "written comma first");
        Fixture again(root);
        again.model->setScreen("control", { { "id", "quit" } });
        Forms::Rows rows = again.model->rows("control:inputs", 200);
        check(rows.items.size() == 2 && rows.items[0].id == "," && rows.items[1].id == "Q", "and read back as the comma key and Q");
    });

    run("controls: with no controls.conf the screen shows the built-in controls and saves into a new one", [] {
        std::string root = makeRoot("controls_bare");
        Fixture f(root);
        f.model->setScreen("controls", {});
        const Forms::Row* select = f.row("Essential", "select");
        check(select && select->detail == "Keyboard Return, Controller A", "built-in select");
        const Forms::Row* letter = f.row("Navigation", "letterUp");
        check(letter && letter->detail == "No input", "nothing built in");
        checkEqual(f.model->get("control:deadZone"), "30", "built-in dead zone");
        f.model->setScreen("control", { { "id", "select" } });
        check(contains(f.model->get("control:notes"), "no controls.conf"), "says so");
        check(f.capture("Space").ok, "added");
        check(f.model->runAction("control.save").ok, "saved");
        std::string text = readFile(root + "/controls.conf");
        check(contains(text, "select = Return,joyA,Space") && contains(text, "back = Escape,joyB"), "a full controls.conf: " + text);
    });

    run("controls: an empty value stays empty, and Open Settings without a key is F10", [] {
        std::string root = makeRoot("controls_blank");
        writeFile(root + "/controls.conf", std::string(oneSauce) + "manager = joyGuide\r\n");
        writeFile(root + "/controls1.conf", "quit =\n");
        Fixture f(root);
        f.model->setScreen("control", { { "id", "quit" } });
        checkEqual(f.model->get("control:hasInputs"), "no", "quit = binds nothing, as upstream");
        check(contains(f.model->get("control:notes"), "nothing is bound"), "said: " + f.model->get("control:notes"));
        f.model->setScreen("control", { { "id", "manager" } });
        check(contains(f.model->get("control:notes"), "uses F10"), "said: " + f.model->get("control:notes"));
        f.model->set("control:deadZone", "25");
        checkEqual(f.model->actionProblem("control.save"), "", "neither blocks saving");
    });

    run("controls: the same button under another name on a connected pad is a duplicate too", [] {
        std::string root = makeRoot("controls_twins");
        writeFile(root + "/controls.conf", "select = Return\nback = Escape,joyButton1\nup = Up\ndown = Down\n");
        // A DualShock through DirectInput: Cross (joyA) is joyButton1
        Fixture f(root, [](const std::string& token) {
            if (token == "joyA") return std::vector<std::string>{ "joyButton1" };
            if (token == "joyButton1") return std::vector<std::string>{ "joyA" };
            return std::vector<std::string>{};
        });
        f.model->setScreen("control", { { "id", "select" } });
        Forms::Model::Result added = f.capture("joyA");
        check(added.ok && contains(added.message, "Back"), "warns it's Back's button too: " + added.message);
        check(contains(f.model->get("control:notes"), "Back"), "and the control says so");
    });

    run("controls: joystick slots never give a slot to an invalid id", [] {
        JoystickSlots slots(2);
        checkEqual(std::to_string(slots.slotOf(-1)), "-1", "no slot for -1 (a stale device index)");
        checkEqual(std::to_string(slots.add(-1)), "-1", "nor a new one");
        checkEqual(std::to_string(slots.add(7)), "0", "first pad is joy0");
        checkEqual(std::to_string(slots.add(7)), "0", "told twice, same slot");
        checkEqual(std::to_string(slots.add(9)), "1", "second pad");
        checkEqual(std::to_string(slots.add(11)), "-1", "no room");
        checkEqual(std::to_string(slots.remove(7)), "0", "first unplugged");
        checkEqual(std::to_string(slots.slotOf(-1)), "-1", "a free slot isn't anyone's");
        checkEqual(std::to_string(slots.add(11)), "0", "the free slot is reused");
        checkEqual(std::to_string(slots.remove(42)), "-1", "unknown id");
    });

    run("controls: sticks and triggers count past half way and let go below a quarter", [] {
        using Manager::InputCapture;
        checkEqual(std::to_string(InputCapture::axisState(0, 16000)), "0", "not yet");
        checkEqual(std::to_string(InputCapture::axisState(0, 16384)), "1", "half way");
        checkEqual(std::to_string(InputCapture::axisState(0, -20000)), "-1", "the other way");
        checkEqual(std::to_string(InputCapture::axisState(1, 9000)), "1", "still held above a quarter");
        checkEqual(std::to_string(InputCapture::axisState(1, 8000)), "0", "let go");
        checkEqual(std::to_string(InputCapture::axisState(1, -30000)), "0", "straight across: released first");
        checkEqual(std::to_string(InputCapture::axisState(0, 65535)), "1", "a trigger from its rest at one end");
    });
});
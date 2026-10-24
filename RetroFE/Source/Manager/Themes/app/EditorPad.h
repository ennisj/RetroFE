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

#include "../../ImGuiScreen.h"
#include "../../../Forms/KeyboardKeys.h"
#include "../../../ThemeEditor/EditorNav.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <string>
#include <vector>

// The theme editors with RetroFE's mapped controls (a gamepad, arcade
// controls, a keyboard's mapped keys), shared by the widget editor and the
// layout editor. The screens keep their regions and canvas modes
// (ThemeEditor/EditorNav.h decides what each press does); this is the part
// that talks to ImGui:
//
//   - Panels and dialogs use ImGui's own gamepad navigation. A press the
//     route says to forward is posted to ImGui as a gamepad key (d-pad,
//     A to activate, B to close a drop-down list). ImGui's reading of real
//     gamepads is switched off while an editor is shown, so a gamepad only
//     reaches it through RetroFE's mapping, once.
//   - Items that need more than ImGui's navigation are registered right
//     after they're drawn: text fields (Select opens the on-screen keyboard),
//     numbers and choices (Left / Right step them) and tree nodes (Left /
//     Right fold them). What the user did comes back to the item on a later
//     frame, through the same call, so the screen's code applies it where it
//     applies typing.
//   - A press that moves nothing (the focus didn't change) leaves the panel
//     for the region that way (takeEdge).
//   - The on-screen keyboard is drawn here in ImGui, with the same keys as
//     the layout-drawn one (Forms::KeyboardKeys). Typing on a real keyboard
//     while it's up types into it instead, and the mapped controls stand
//     down until it's closed.
//   - A focus highlight over ImGui's own, and a frame around the focused region.
namespace Themes
{
    class EditorPad
    {
    public:
        using Control = Manager::ImGuiScreen::Control;

        // ---- the screen's lifetime ----
        // ImGui's own gamepad reading off, keyboard navigation off (the arrows
        // nudge on the canvas): called from draw() every frame, cheap
        void attach();
        // Puts ImGui's settings back (retire())
        void detach();
        // ImGuiScreen::beforeFrame: tells ImGui a gamepad is there (RetroFE's controls)
        void beforeFrame();

        // ---- between frames (ImGuiScreen::controlsHeld / control) ----
        void held(const Manager::ImGuiScreen::ControlsHeld& held);
        bool directionHeld() const;   // a mapped direction is down (the arrow keys mustn't also nudge)
        bool selectHeld() const;      // the mapped select is down (Enter mustn't also answer a dialog)
        // A control was pressed: true when the screen should ignore it (a
        // field is being typed into with a real keyboard; the keyboard used it)
        bool takesControl(Control control);
        // A press in a panel or one of the screen's dialogs (layer: EL_PANEL /
        // EL_DIALOG; a drop-down list on top is found here). Forwards, steps
        // or opens the keyboard itself; returns the route, so the screen
        // closes its dialog (ER_CLOSE_DIALOG) or goes back to the canvas
        // (ER_LEAVE_REGION).
        ThemeEditor::EdRoute press(int press, int layer);
        // As ImGui left things last frame
        bool modalOpen() const;   // one of the screen's dialogs
        bool listOpen() const;    // a drop-down list (a combo) on top
        // The last input was a mapped control, not the mouse or typing
        bool controllerActive() const { return controllerActive_; }

        // ---- inside the frame ----
        // First thing in draw(), after ImGui::NewFrame
        void beginFrame();
        // A forwarded press found nothing that way: the region that way takes over
        bool takeEdge(int& dx, int& dy);

        // Registrations, right after the item is drawn. title names the field
        // on the keyboard and identifies the item when the item has no ID of
        // its own (a number with + and - buttons); keep it unique in its ID scope.
        // A text field: true when the keyboard set `value` (apply it as typed)
        bool text(const char* title, std::string& value, bool multiline = false);
        // A number: true when Left / Right or the keyboard changed `value`
        bool number(const char* title, int& value, int step, int lo, int hi);
        bool number(const char* title, double& value, double step, double lo = 0.0, double hi = 0.0);
        // A choice (a combo that's closed now): -1 / +1 when Left / Right asked
        // for the previous / next option, else 0
        int choice(const char* title);
        // A tree node or section header: Left / Right fold it, never leave the panel
        void node();

        // The focused region (EF_TOOLBAR, EF_THEME, EF_CANVAS, EF_PROPS). The
        // focus moves into it with the next beginRegion; -1 is ignored.
        int region() const { return region_; }
        void setRegion(int region);
        // Moves the focus into the region again (after a dialog closed)
        void refocus() { focusRegion_ = region_; }
        // BeginChild for a region: takes the focus when it was moved there,
        // follows the mouse there, and draws the region's frame (highlighted
        // when focused). EndChild as usual.
        void beginRegion(int region, const char* id, const ImVec2& size, ImGuiChildFlags childFlags,
                         ImGuiWindowFlags windowFlags = 0);
        // Last thing in draw(): the focus highlight and the keyboard
        void endFrame(float scale);

        bool keyboardOpen() const { return keyboard_.open; }
        void cancelKeyboard();
        // onScreenKeyboard = no: Select on a text field starts typing into it
        // with a real keyboard instead (ImGui's own editing)
        void setKeyboardEnabled(bool enabled) { keyboardEnabled_ = enabled; }

    private:
        struct Item
        {
            ImGuiID key = 0;
            int kind = ThemeEditor::EIK_OTHER;
            ImRect rect;
            ImGuiID window = 0;
            std::string title;
            std::string value;    // the text a keyboard starts from
            bool numeric = false, decimal = false, multiline = false;
        };
        struct Keyboard
        {
            bool open = false;
            bool typing = false;          // a real keyboard types into it
            ImGuiID key = 0;              // the item it types for
            std::string title;
            Forms::TextEdit text;
            Forms::KeyboardKeys keys;
        };
        struct Delivery
        {
            ImGuiID key = 0;
            int frames = 0;               // dropped when the item isn't drawn again soon
            int dir = 0;                  // a step
            bool typed = false;           // the keyboard's text
            std::string text;
        };

        ImGuiID itemKey(const char* title) const;
        bool focusedHere(const ImRect& rect) const;
        void add(Item item);
        const Item* focusedItem() const;
        void inject(int press);
        void injectKey(ImGuiKey key);
        void openKeyboard(const Item& item);
        void keyboardControl(Control control);
        void keyboardTyping();
        void finishKeyboard(bool keep);
        void drawKeyboard(float scale);
        void drawFocus(float scale);

        void entered();
        void regionFrame(bool focused);

        Manager::ImGuiScreen::ControlsHeld held_{};
        int region_ = ThemeEditor::EF_CANVAS;
        int focusRegion_ = ThemeEditor::EF_CANVAS;
        bool attached_ = false;
        bool keyboardEnabled_ = true;
        bool wasModal_ = false;
        bool savedNavKeyboard_ = false;
        bool controllerActive_ = false;
        std::vector<Item> items_, lastItems_;   // this frame's registrations, and last frame's
        ImGuiID focused_ = 0;                   // the registered item ImGui's focus was on last frame
        ImGuiID focusedThisFrame_ = 0;
        Delivery delivery_;
        Keyboard keyboard_;
        // An edge press waiting to see whether the focus moved
        bool edgeArmed_ = false;
        int edgeFrames_ = 0;
        int edgeDx_ = 0, edgeDy_ = 0;
        ImGuiID edgeFrom_ = 0;
        bool edgeReady_ = false;
    };
}

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

#include <array>
#include <string>

namespace Manager
{
    // A screen ManagerUI draws with its own ImGui code instead of a form,
    // whatever formsRenderer says (the theme editor: a canvas doesn't fit
    // rows of fields). It sits in the navigation stack like any screen: its
    // form id is a real form, opened and left the usual way.
    //
    // The rule that keeps it safe: pages are built and destroyed, and
    // textures created and freed, only in update() and shutdown(), never in
    // draw() or drawTargets(). ManagerUI deletes a screen from update() too.
    //
    // Input reaches it two ways:
    //   - the mouse and keyboard as SDL events, through ImGui's SDL backend;
    //   - RetroFE's mapped controls (controls.conf: up, down, left, right,
    //     select, back, page and letter up/down), when wantsControls(): RetroFE
    //     reads them as it does for the layout-drawn screens (the keyboard
    //     through SDL_GetKeyboardState, joysticks through their events, which
    //     ManagerUI then leaves to RetroFE's input too), and ManagerUI turns
    //     what's held into presses (directions, page and letter up/down repeat
    //     while held) and hands each to control().
    // A key can be both a mapped control and a key ImGui sees (arrows, Enter,
    // letters): a screen that reads keys from ImGui too must not act on such
    // a key twice (controlsHeld says which controls are down).
    class ImGuiScreen
    {
    public:
        enum class Control { Up = 0, Down, Left, Right, Select, Back, PageUp, PageDown, LetterUp, LetterDown, Count };
        using ControlsHeld = std::array<bool, static_cast<size_t>(Control::Count)>;

        virtual ~ImGuiScreen() = default;

        // RetroFE's mapped controls (see above). control() runs between
        // frames, before update() and draw(); repeat is true for a held
        // direction's repeats. controlsHeld() comes first, every frame.
        virtual bool wantsControls() const { return false; }
        virtual void controlsHeld(const ControlsHeld& held) { (void)held; }
        virtual void control(Control control, bool repeat) { (void)control; (void)repeat; }

        // Inside the render pass, after the backends' NewFrame and before
        // ImGui::NewFrame (e.g. to tell ImGui a gamepad is there)
        virtual void beforeFrame() {}

        // Every frame while it's the current screen, outside the render pass
        // and outside SDL::getMutex()
        virtual void update(float dt) = 0;

        // While rendering, under SDL::getMutex() and before the ImGui frame:
        // draw pages onto the screen's own render targets. Leave the render
        // target as it was.
        virtual void drawTargets() {}

        // Inside the ImGui frame
        virtual void draw() = 0;

        // Escape was pressed: true if the screen used it (closing a dialog,
        // cancelling a drag); otherwise the manager goes back
        virtual bool escape() { return false; }

        // The user asked to leave (Back, Escape, the Settings key): true if
        // the screen asks them itself (e.g. Save / Don't save / Cancel) and
        // will go back by itself once they've answered (takeBack). Asked
        // before leaving(), so there's no "leave again to discard".
        virtual bool askToLeave() { return false; }

        // As Provider::leaving / discard
        virtual std::string leaving() { return ""; }
        virtual void discard() {}

        // A message for the user (e.g. why they can't leave yet): an error, a
        // neutral note when nothing happened (isInfo), else a success
        virtual void setStatus(const std::string& message, bool isError, bool isInfo = false)
        {
            (void)message; (void)isError; (void)isInfo;
        }

        // True once when the screen asks to go back (its own Close button)
        virtual bool takeBack() { return false; }

        // It's no longer shown (the user left it, or the manager closed): put
        // back settings it changed, now, before the next screen is built. No
        // SDL work here: it can run inside the render pass.
        virtual void retire() {}

        // Frees pages and textures. Called from update() before the screen is deleted.
        virtual void shutdown() {}
    };
}

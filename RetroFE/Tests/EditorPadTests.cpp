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

// The theme editors' controller glue (Manager/Themes/app/EditorPad) on a
// headless ImGui context: frames are built without a backend, and the
// presses go in as RetroFE's controls would send them.

#include "TestSupport.h"
#include "../Source/Manager/Themes/app/EditorPad.h"
#include <functional>

using namespace ThemeEditor;
using Test::check;
using Control = Manager::ImGuiScreen::Control;

namespace
{
    // An ImGui context with a built font and a display, no backend
    struct Headless
    {
        ImGuiContext* context = nullptr;
        Themes::EditorPad pad;

        Headless()
        {
            context = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.DisplaySize = ImVec2(1280.0f, 720.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            // As the SDL backend reports a connected pad (ImGui clears gamepad keys without it)
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            unsigned char* pixels = nullptr;
            int w = 0, h = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        }
        ~Headless()
        {
            pad.detach();
            ImGui::DestroyContext(context);
        }

        // One frame as the editor screens build theirs
        void frame(const std::function<void()>& body)
        {
            pad.beforeFrame();
            ImGui::NewFrame();
            pad.attach();
            pad.beginFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(600, 400));
            ImGui::Begin("panel", nullptr, ImGuiWindowFlags_NoSavedSettings);
            body();
            ImGui::End();
            pad.endFrame(1.0f);
            ImGui::Render();
        }
        void frames(int n, const std::function<void()>& body)
        {
            for (int i = 0; i < n; ++i) frame(body);
        }
    };
}

static Test::Suite editorPad("editor controller (ImGui)", [] {
    Test::run("editor controller: gamepad keys are released on attach and detach", [] {
        Headless h;
        ImGuiIO& io = ImGui::GetIO();
        // The backend fed A down (the press that opened the editor) and then stops feeding
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true);
        bool downFirst = false, downAfter = true;
        h.frame([&] { downFirst = ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown); });
        h.frame([&] { downAfter = ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown); });
        check(downFirst && !downAfter, "attach lets go of a gamepad key left down: " + std::to_string(downFirst) + std::to_string(downAfter));
        check(!(io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad), "gamepad navigation is off between frames, so the backend doesn't read pads");
        bool onInFrame = false;
        h.frame([&] { onInFrame = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) && (io.BackendFlags & ImGuiBackendFlags_HasGamepad); });
        check(onInFrame, "and on, with a gamepad, while the frame is built");

        io.AddKeyEvent(ImGuiKey_GamepadDpadUp, true);
        h.frame([] {});
        h.pad.detach();
        ImGui::NewFrame();
        const bool stillDown = ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp);
        ImGui::Render();
        check(!stillDown, "detach lets go of them too");
        check((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0, "and puts gamepad navigation back");
    });

    Test::run("editor controller: the keyboard on a number, and leaving the panel", [] {
        Headless h;
        double value = 0.1234567;
        int changes = 0;
        std::string where;
        auto body = [&] {
            ImGui::InputDouble("Value", &value, 0.1, 1.0, "%.3f");
            {
                ImGuiContext& g = *GImGui;
                ImRect nav = ImGui::WindowRectRelToAbs(g.CurrentWindow, g.CurrentWindow->NavRectRel[0]);
                where = "item " + std::to_string(g.LastItemData.Rect.Min.x) + "," + std::to_string(g.LastItemData.Rect.Min.y) + "-" +
                        std::to_string(g.LastItemData.Rect.Max.x) + "," + std::to_string(g.LastItemData.Rect.Max.y) + " nav " +
                        std::to_string(nav.Min.x) + "," + std::to_string(nav.Min.y) + " id " + std::to_string(g.LastItemData.ID);
            }
            if (h.pad.number("Value", value, 0.1)) ++changes;
            ImGui::Button("Below");
        };
        h.frames(2, body);
        // The first press gives the panel a focus; Up then reaches the number at the top
        h.pad.press(EP_DOWN, EL_PANEL);
        h.frames(4, body);
        int dx = 0, dy = 0;
        check(!h.pad.takeEdge(dx, dy), "a press that found an item isn't an edge");
        h.pad.press(EP_UP, EL_PANEL);
        h.frames(4, body);
        h.pad.takeEdge(dx, dy);
        const ImGuiID nav = GImGui->NavId;
        const std::string navWindow = GImGui->NavWindow ? GImGui->NavWindow->Name : "none";
        check(h.pad.press(EP_SELECT, EL_PANEL) == ER_KEYBOARD && h.pad.keyboardOpen(),
              "Select on a number opens the keyboard: nav " + std::to_string(nav) + " in " + navWindow + " " + where);
        // Up from the number pad's first row wraps to Done
        h.pad.takesControl(Control::Up);
        h.pad.takesControl(Control::Select);
        h.frames(4, body);
        check(!h.pad.keyboardOpen() && changes == 0 && value == 0.1234567,
              "Done without typing changes nothing (the value isn't rounded to what %g shows)");

        // Typing a digit after it
        h.pad.press(EP_SELECT, EL_PANEL);
        h.pad.takesControl(Control::Select);   // "1", the pad's first key
        h.pad.takesControl(Control::Up);
        h.pad.takesControl(Control::Select);
        h.frames(4, body);
        check(changes == 1 && value == 0.12345671, "typed: " + std::to_string(value));

        // Left / Right step it
        h.pad.press(EP_RIGHT, EL_PANEL);
        h.frames(2, body);
        check(changes == 2 && value > 0.2234 && value < 0.2235, "Right steps it");

        // Down to the button, then Down again: nothing that way
        h.pad.press(EP_DOWN, EL_PANEL);
        h.frames(4, body);
        check(!h.pad.takeEdge(dx, dy), "the button below takes the focus");
        h.pad.press(EP_DOWN, EL_PANEL);
        h.frames(4, body);
        check(h.pad.takeEdge(dx, dy) && dx == 0 && dy == 1, "past the last row, the panel below takes over");
        h.frames(4, body);
        check(!h.pad.takeEdge(dx, dy), "once");
    });

    Test::run("editor controller: an edge waits for ImGui to see the press", [] {
        Headless h;
        auto body = [] { ImGui::Button("Only"); };
        h.frames(2, body);
        h.pad.press(EP_DOWN, EL_PANEL);
        h.frames(4, body);
        int dx = 0, dy = 0;
        h.pad.takeEdge(dx, dy);
        // The press is posted, but no frame has run yet (a slow frame): no verdict
        h.pad.press(EP_UP, EL_PANEL);
        check(!h.pad.takeEdge(dx, dy), "nothing before ImGui has seen it");
        h.frame(body);
        check(!h.pad.takeEdge(dx, dy), "nor in the frame that sees it (the move ends next frame)");
        h.frame(body);
        check(h.pad.takeEdge(dx, dy) && dy == -1, "then the region above");
    });
});

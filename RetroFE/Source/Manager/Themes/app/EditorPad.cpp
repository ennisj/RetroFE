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
#include "EditorPad.h"
#include <algorithm>
#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace ThemeEditor;

namespace
{
    using Control = Manager::ImGuiScreen::Control;

    // The look of the layout-drawn keyboard (Builtin's keyboard page)
    const ImU32 KEYBOARD_BACKGROUND = IM_COL32(0x0d, 0x10, 0x14, 255);
    const ImU32 KEY = IM_COL32(255, 255, 255, 18);
    const ImU32 KEY_ACTIVE = IM_COL32(255, 255, 255, 54);
    const ImU32 KEY_FOCUS = IM_COL32(0x3d, 0x6f, 0xb6, 200);
    const ImU32 KEY_TEXT = IM_COL32(0xc8, 0xc8, 0xc8, 255);
    const ImU32 KEY_TEXT_FOCUS = IM_COL32(255, 255, 255, 255);
    const ImU32 NOTE = IM_COL32(0xc8, 0xc8, 0xc8, 140);
    // The controller's focus: gold, as the editor's region frames
    const ImU32 FOCUS = IM_COL32(255, 196, 40, 255);

    std::string utf8(unsigned int c)
    {
        std::string out;
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        return out;
    }

    // Text typed on a real keyboard waiting in ImGui's queue (it isn't in
    // InputQueueCharacters until the next frame starts)
    bool textQueued()
    {
        ImGuiContext& g = *GImGui;
        for (const ImGuiInputEvent& e : g.InputEventsQueue) {
            if (e.Type == ImGuiInputEventType_Text) return true;
        }
        return false;
    }


    // The cursor a line up or down, at the same column as far as the line allows
    void moveLine(Forms::TextEdit& t, int dir)
    {
        const std::string& s = t.text;
        size_t cur = std::min(t.cursor, s.size());
        size_t start = cur == 0 ? 0 : s.rfind('\n', cur - 1);
        start = start == std::string::npos ? 0 : (cur == 0 ? 0 : start + 1);
        const size_t column = cur - start;
        if (dir < 0) {
            if (start == 0) { t.cursor = 0; return; }
            size_t prevEnd = start - 1;
            size_t prevStart = prevEnd == 0 ? 0 : s.rfind('\n', prevEnd - 1);
            prevStart = prevStart == std::string::npos ? 0 : (prevEnd == 0 ? 0 : prevStart + 1);
            t.cursor = std::min(prevStart + column, prevEnd);
        }
        else {
            size_t end = s.find('\n', cur);
            if (end == std::string::npos) { t.cursor = s.size(); return; }
            size_t nextStart = end + 1;
            size_t nextEnd = s.find('\n', nextStart);
            if (nextEnd == std::string::npos) nextEnd = s.size();
            t.cursor = std::min(nextStart + column, nextEnd);
        }
    }
}

namespace Themes
{
    // ---- lifetime ------------------------------------------------------------------

    void EditorPad::releaseGamepadKeys()
    {
        // Anything the backend left down (the press that opened the screen) goes up
        ImGuiIO& io = ImGui::GetIO();
        for (int key = ImGuiKey_GamepadStart; key <= ImGuiKey_GamepadRStickDown; ++key) {
            io.AddKeyAnalogEvent(static_cast<ImGuiKey>(key), false, 0.0f);
        }
    }

    void EditorPad::attach()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (!attached_) {
            attached_ = true;
            savedNavKeyboard_ = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
            savedNavGamepad_ = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
            releaseGamepadKeys();
        }
        // The arrow keys nudge on the canvas, so ImGui's keyboard navigation is off
        // (Tab still moves between fields); its gamepad navigation takes the mapped
        // controls while the frame is built (beforeFrame / endFrame)
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }

    void EditorPad::detach()
    {
        if (!attached_ || !ImGui::GetCurrentContext()) return;
        attached_ = false;
        ImGuiIO& io = ImGui::GetIO();
        if (savedNavKeyboard_) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (savedNavGamepad_) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        releaseGamepadKeys();
        keyboard_ = Keyboard();
    }

    void EditorPad::beforeFrame()
    {
        if (!attached_) return;
        ImGuiIO& io = ImGui::GetIO();
        // The SDL backend's NewFrame (just before this) skipped reading real
        // gamepads because gamepad navigation was off (endFrame), so a pad
        // reaches ImGui only through RetroFE's mapping and never acts twice.
        // Now it's on for this frame, with a gamepad present: the mapped controls.
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        // While the keyboard is up the mouse can't reach what's under it
        if (keyboard_.open) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    // ---- between frames ----------------------------------------------------------

    void EditorPad::held(const Manager::ImGuiScreen::ControlsHeld& held)
    {
        held_ = held;
    }

    bool EditorPad::directionHeld() const
    {
        return held_[static_cast<size_t>(Control::Up)] || held_[static_cast<size_t>(Control::Down)] ||
               held_[static_cast<size_t>(Control::Left)] || held_[static_cast<size_t>(Control::Right)];
    }

    bool EditorPad::selectHeld() const
    {
        return held_[static_cast<size_t>(Control::Select)];
    }

    bool EditorPad::takesControl(Control control)
    {
        controllerActive_ = true;
        if (keyboard_.open) {
            // Typing on a real keyboard: its letters may be mapped controls too, so they stand down
            if (keyboard_.typing) return true;
            if (textQueued()) {
                keyboard_.typing = true;
                return true;
            }
            keyboardControl(control);
            return true;
        }
        if (ImGui::GetIO().WantTextInput) {
            // A field typed into with a real keyboard (clicked). Should ImGui's
            // navigation have started one, Back leaves it.
            if (GImGui->ActiveIdSource == ImGuiInputSource_Gamepad && control == Control::Back) injectKey(ImGuiKey_GamepadFaceRight);
            return true;
        }
        return false;
    }

    bool EditorPad::modalOpen() const
    {
        return ImGui::GetTopMostPopupModal() != nullptr;
    }

    bool EditorPad::listOpen() const
    {
        ImGuiContext& g = *GImGui;
        return g.OpenPopupStack.Size > 0 && g.OpenPopupStack.back().Window &&
               !(g.OpenPopupStack.back().Window->Flags & ImGuiWindowFlags_Modal);
    }

    const EditorPad::Item* EditorPad::focusedItem() const
    {
        if (!focused_) return nullptr;
        for (const Item& item : lastItems_) {
            if (item.key == focused_) return &item;
        }
        return nullptr;
    }

    EdRoute EditorPad::press(int press, int layer)
    {
        if (listOpen()) layer = EL_LIST;
        const Item* item = focusedItem();
        const EdRoute route = panelRoute(item ? item->kind : EIK_OTHER, press, layer);
        switch (route) {
        case ER_FORWARD:
            inject(press);
            break;
        case ER_FORWARD_EDGE:
            // Decided once ImGui has seen the press and finished the move it started (beginFrame)
            edgeArmed_ = true;
            edgeSeen_ = false;
            edgeFrames_ = 0;
            edgeKey_ = directionKey(press);
            pressDirection(press, edgeDx_, edgeDy_);
            edgeFrom_ = GImGui->NavId;
            inject(press);
            break;
        case ER_ADJUST:
            delivery_ = Delivery();
            delivery_.key = item->key;
            delivery_.frames = 3;
            delivery_.dir = press == EP_LEFT ? -1 : 1;
            break;
        case ER_KEYBOARD:
            if (keyboardEnabled_) openKeyboard(*item);
            else inject(EP_SELECT);   // ImGui edits the field; the real keyboard types
            break;
        case ER_CLOSE_LIST:
            injectKey(ImGui::GetIO().ConfigNavSwapGamepadButtons ? ImGuiKey_GamepadFaceDown : ImGuiKey_GamepadFaceRight);
            break;
        default:
            break;
        }
        return route;
    }

    ImGuiKey EditorPad::directionKey(int press)
    {
        switch (press) {
        case EP_UP: return ImGuiKey_GamepadDpadUp;
        case EP_DOWN: return ImGuiKey_GamepadDpadDown;
        case EP_LEFT: return ImGuiKey_GamepadDpadLeft;
        case EP_RIGHT: return ImGuiKey_GamepadDpadRight;
        default: return ImGuiKey_None;
        }
    }

    void EditorPad::inject(int press)
    {
        ImGuiKey key = directionKey(press);
        if (press == EP_SELECT) key = ImGui::GetIO().ConfigNavSwapGamepadButtons ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown;
        if (key == ImGuiKey_None) return;
        // Activating needs the focus shown (it hides after a mouse click)
        ImGui::SetNavCursorVisible(true);
        injectKey(key);
    }

    void EditorPad::injectKey(ImGuiKey key)
    {
        // A tap: ImGui spreads the press and release over two frames
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(key, true);
        io.AddKeyEvent(key, false);
    }

    // ---- the frame -----------------------------------------------------------------

    void EditorPad::beginFrame()
    {
        ImGuiIO& io = ImGui::GetIO();
        // The mouse took over
        if (io.MouseClicked[0] || io.MouseClicked[1] || io.MouseWheel != 0.0f ||
            (ImGui::IsMousePosValid() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))) {
            controllerActive_ = false;
        }
        // So did the keyboard: a key that isn't one of the mapped controls (D in
        // Unsaved changes), or typing, while no mapped control is held
        if (!anyHeld() && (io.InputQueueCharacters.Size > 0 || realKeyPressed())) controllerActive_ = false;
        if (keyboard_.open && io.InputQueueCharacters.Size > 0) keyboard_.typing = true;
        // A dialog closed: the focus goes back into the region (ImGui gives it to the editor's window)
        const bool modal = modalOpen();
        if (wasModal_ && !modal) refocus();
        wasModal_ = modal;

        // A forwarded direction: once ImGui has seen it pressed and has finished
        // the move it started (which may take more than a frame, when it
        // scrolls), the focus either moved or there was nothing that way.
        // Frames are counted only to give up on a press ImGui never saw.
        if (edgeArmed_) {
            ImGuiContext& g = *GImGui;
            if (!edgeSeen_) {
                edgeSeen_ = ImGui::IsKeyPressed(edgeKey_, false);
                if (!edgeSeen_ && ++edgeFrames_ > 30) edgeArmed_ = false;
            }
            else if (!g.NavMoveSubmitted && !g.NavMoveForwardToNextFrame) {
                edgeArmed_ = false;
                if (g.NavId == edgeFrom_ && !listOpen() && !modal) edgeReady_ = true;
            }
        }
    }

    bool EditorPad::anyHeld() const
    {
        for (bool h : held_) {
            if (h) return true;
        }
        return false;
    }

    bool EditorPad::realKeyPressed() const
    {
        // Keyboard keys only (the gamepad keys come after them); modifiers don't count
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; ++key) {
            const ImGuiKey k = static_cast<ImGuiKey>(key);
            if (k == ImGuiKey_LeftCtrl || k == ImGuiKey_RightCtrl || k == ImGuiKey_LeftShift || k == ImGuiKey_RightShift ||
                k == ImGuiKey_LeftAlt || k == ImGuiKey_RightAlt || k == ImGuiKey_LeftSuper || k == ImGuiKey_RightSuper) {
                continue;
            }
            if (ImGui::IsKeyPressed(k, false)) return true;
        }
        return false;
    }

    bool EditorPad::takeEdge(int& dx, int& dy)
    {
        if (!edgeReady_) return false;
        edgeReady_ = false;
        dx = edgeDx_;
        dy = edgeDy_;
        return true;
    }

    ImGuiID EditorPad::itemKey(const char* title) const
    {
        const ImGuiID id = ImGui::GetItemID();
        return id ? id : ImGui::GetID(title);
    }

    bool EditorPad::focusedHere(const ImRect& rect) const
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        if (!g.NavId || !window || g.NavWindow != window) return false;
        if (g.LastItemData.ID != 0 && g.LastItemData.ID == g.NavId) return true;
        // An item made of several (a number with - and + buttons): the focus is on one of them
        const ImRect nav = ImGui::WindowRectRelToAbs(window, window->NavRectRel[g.NavLayer]);
        return rect.Contains(nav.GetCenter());
    }

    void EditorPad::add(Item item)
    {
        ImGuiContext& g = *GImGui;
        item.rect = g.LastItemData.Rect;
        item.window = g.CurrentWindow ? g.CurrentWindow->ID : 0;
        if (focusedHere(item.rect)) focusedThisFrame_ = item.key;
        items_.push_back(std::move(item));
    }

    bool EditorPad::text(const char* title, std::string& value, bool multiline)
    {
        Item item;
        item.key = itemKey(title);
        item.kind = EIK_TEXT;
        item.title = title;
        item.value = value;
        item.multiline = multiline;
        const ImGuiID key = item.key;
        add(std::move(item));
        if (delivery_.key == key && delivery_.typed) {
            const bool changed = delivery_.text != value;
            value = delivery_.text;
            delivery_ = Delivery();
            return changed;
        }
        return false;
    }

    bool EditorPad::number(const char* title, int& value, int step, int lo, int hi)
    {
        Item item;
        item.key = ImGui::GetID(title);
        item.kind = EIK_NUMBER;
        item.title = title;
        item.value = std::to_string(value);
        item.numeric = true;
        const ImGuiID key = item.key;
        add(std::move(item));
        if (delivery_.key != key) return false;
        int next = value;
        if (delivery_.typed) {
            char* end = nullptr;
            errno = 0;
            const long typed = std::strtol(delivery_.text.c_str(), &end, 10);
            if (!delivery_.text.empty() && end && *end == '\0' && errno == 0) next = static_cast<int>(typed);
        }
        else {
            next = static_cast<int>(std::lround(numberStep(value, step, delivery_.dir, lo, hi)));
        }
        if (lo < hi) next = std::max(lo, std::min(hi, next));
        delivery_ = Delivery();
        if (next == value) return false;
        value = next;
        return true;
    }

    bool EditorPad::number(const char* title, double& value, double step, double lo, double hi)
    {
        Item item;
        item.key = ImGui::GetID(title);
        item.kind = EIK_NUMBER;
        item.title = title;
        item.value = roundTripNumber(value);   // the exact value: Done without typing changes nothing
        item.numeric = true;
        item.decimal = true;
        const ImGuiID key = item.key;
        add(std::move(item));
        if (delivery_.key != key) return false;
        double next = value;
        if (delivery_.typed) {
            char* end = nullptr;
            const double typed = std::strtod(delivery_.text.c_str(), &end);
            if (!delivery_.text.empty() && end && *end == '\0' && std::isfinite(typed)) next = typed;
        }
        else {
            next = numberStep(value, step, delivery_.dir, lo, hi);
        }
        if (lo < hi) next = std::max(lo, std::min(hi, next));
        delivery_ = Delivery();
        if (next == value) return false;
        value = next;
        return true;
    }

    int EditorPad::choice(const char* title)
    {
        Item item;
        item.key = itemKey(title);
        item.kind = EIK_CHOICE;
        item.title = title;
        const ImGuiID key = item.key;
        add(std::move(item));
        if (delivery_.key == key && !delivery_.typed) {
            const int dir = delivery_.dir;
            delivery_ = Delivery();
            return dir;
        }
        return 0;
    }

    void EditorPad::node()
    {
        Item item;
        item.key = ImGui::GetItemID();
        item.kind = EIK_NODE;
        if (item.key) add(std::move(item));
    }

    void EditorPad::setRegion(int region)
    {
        if (region < 0) return;
        region_ = region;
        focusRegion_ = region;
    }

    void EditorPad::beginRegion(int region, const char* id, const ImVec2& size, ImGuiChildFlags childFlags,
                                ImGuiWindowFlags windowFlags)
    {
        // Not while a dialog is up: it keeps the focus
        const bool focusNow = focusRegion_ == region && !modalOpen() && !listOpen();
        if (focusNow) ImGui::SetNextWindowFocus();
        ImGui::BeginChild(id, size, childFlags, windowFlags);
        if (focusNow) {
            focusRegion_ = -1;
            if (region != EF_CANVAS) entered();
        }
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            region_ = region;
        }
        regionFrame(region_ == region);
    }

    void EditorPad::entered()
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (GImGui->NavWindow == window && window->NavLastIds[0] == 0) ImGui::NavInitWindow(window, false);
        ImGui::SetNavCursorVisible(true);
    }

    void EditorPad::regionFrame(bool focused)
    {
        // A dialog or the keyboard has the focus then, not the region
        if (!focused || !controllerActive_ || keyboard_.open || modalOpen()) return;
        const ImVec2 a = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        const float t = std::max(2.0f, ImGui::GetFontSize() / 7.0f);
        ImGui::GetForegroundDrawList()->AddRect(ImVec2(a.x + t * 0.5f, a.y + t * 0.5f), ImVec2(a.x + size.x - t * 0.5f, a.y + size.y - t * 0.5f),
                                                IM_COL32(255, 196, 40, 150), 4.0f, 0, t);
    }

    void EditorPad::endFrame(float scale)
    {
        // The item the focus was on went away (another widget was selected, a
        // list was filtered): the region's first item takes it
        ImGuiContext& g = *GImGui;
        if (controllerActive_ && g.NavWindow && g.NavId && !g.NavIdIsAlive && !keyboard_.open && !modalOpen() && !listOpen()) {
            ImGui::NavInitWindow(g.NavWindow, true);
        }
        if (keyboard_.open) {
            if (keyboard_.typing) keyboardTyping();
            if (keyboard_.open) drawKeyboard(scale);
        }
        else {
            drawFocus(scale);
        }
        lastItems_ = std::move(items_);
        items_.clear();
        focused_ = focusedThisFrame_;
        focusedThisFrame_ = 0;
        if (delivery_.key && --delivery_.frames < 0) delivery_ = Delivery();
        // Off until beforeFrame, so the SDL backend's next NewFrame doesn't feed
        // ImGui a real gamepad (see beforeFrame)
        if (attached_) ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    }

    void EditorPad::drawFocus(float scale)
    {
        ImGuiContext& g = *GImGui;
        if (!controllerActive_ || !g.NavId || !g.NavCursorVisible || !g.NavWindow || !g.NavIdIsAlive) return;
        ImGuiWindow* window = g.NavWindow;
        ImRect r = ImGui::WindowRectRelToAbs(window, window->NavRectRel[g.NavLayer]);
        const float pad = 3.0f * scale;
        r.Expand(pad);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->PushClipRect(window->ClipRect.Min, window->ClipRect.Max, false);
        dl->AddRect(r.Min, r.Max, FOCUS, 4.0f * scale, 0, std::max(2.0f, 2.5f * scale));
        dl->PopClipRect();
    }

    // ---- the on-screen keyboard -----------------------------------------------------

    void EditorPad::openKeyboard(const Item& item)
    {
        keyboard_.open = true;
        keyboard_.typing = false;
        keyboard_.key = item.key;
        keyboard_.title = item.title;
        keyboard_.text.set(item.value);
        keyboard_.seed = item.value;
        keyboard_.keys.reset(item.numeric, item.multiline, item.decimal);
    }

    void EditorPad::cancelKeyboard()
    {
        finishKeyboard(false);
    }

    void EditorPad::finishKeyboard(bool keep)
    {
        if (!keyboard_.open) return;
        // Done on what the field already had isn't an edit (it would dirty the theme for nothing)
        if (keep && keyboard_.text.text != keyboard_.seed) {
            delivery_ = Delivery();
            delivery_.key = keyboard_.key;
            delivery_.frames = 3;
            delivery_.typed = true;
            delivery_.text = keyboard_.text.text;
        }
        keyboard_.open = false;
        keyboard_.typing = false;
        ImGui::SetNavCursorVisible(true);
    }

    void EditorPad::keyboardControl(Control control)
    {
        using Nav = Forms::KeyboardKeys::Nav;
        Nav nav;
        switch (control) {
        case Control::Up: nav = Nav::Up; break;
        case Control::Down: nav = Nav::Down; break;
        case Control::Left: nav = Nav::Left; break;
        case Control::Right: nav = Nav::Right; break;
        case Control::Select: nav = Nav::Select; break;
        case Control::Back: nav = Nav::Back; break;
        default: return;
        }
        using Result = Forms::KeyboardKeys::Result;
        const Result result = keyboard_.keys.navigate(nav, keyboard_.text);
        if (result == Result::Done) finishKeyboard(true);
        else if (result == Result::Cancel) finishKeyboard(false);
        else if (result == Result::LineUp) moveLine(keyboard_.text, -1);
        else if (result == Result::LineDown) moveLine(keyboard_.text, 1);
    }

    void EditorPad::keyboardTyping()
    {
        ImGuiIO& io = ImGui::GetIO();
        Forms::TextEdit& t = keyboard_.text;
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            const unsigned int c = io.InputQueueCharacters[i];
            if (c < 32 || c == 127) continue;
            if (keyboard_.keys.numeric() && !((c >= '0' && c <= '9') || c == '-' || c == '.' || c == '+')) continue;
            t.insert(utf8(c));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) t.backspace();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) t.erase();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) t.left();
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) t.right();
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) t.cursor = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) t.cursor = t.text.size();
        if (keyboard_.keys.multiline()) {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) moveLine(t, -1);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) moveLine(t, 1);
        }
        const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (enter) {
            // A multi-line text takes Enter as a new line; Ctrl+Enter keeps it
            if (keyboard_.keys.multiline() && !io.KeyCtrl) t.insert("\n");
            else finishKeyboard(true);
        }
    }

    void EditorPad::drawKeyboard(float scale)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        const ImVec2 display = io.DisplaySize;
        dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 120));

        const float size = ImGui::GetFontSize() * 1.15f;
        const float pad = size * 0.6f;
        const float width = std::min(display.x - 32.0f * scale, 940.0f * scale);
        const float height = std::min(display.y - 32.0f * scale, (keyboard_.keys.multiline() ? 470.0f : 420.0f) * scale);
        const ImVec2 a((display.x - width) * 0.5f, display.y - height - 16.0f * scale);
        const ImVec2 b(a.x + width, a.y + height);
        dl->AddRectFilled(a, b, KEYBOARD_BACKGROUND, 8.0f * scale);
        dl->AddRect(a, b, IM_COL32(255, 196, 40, 120), 8.0f * scale, 0, 1.5f * scale);

        const float left = a.x + pad, innerW = width - pad * 2.0f;
        float y = a.y + pad;
        auto textWidth = [&](const std::string& s, float sz) { return font->CalcTextSizeA(sz, FLT_MAX, 0.0f, s.c_str()).x; };

        // The field's name, then what's been typed: the cursor's line, up to the
        // cursor ("_") and as much after it as fits
        std::string title = keyboard_.title;
        const std::string& text = keyboard_.text.text;
        const size_t cursor = std::min(keyboard_.text.cursor, text.size());
        size_t lineStart = cursor == 0 ? 0 : text.rfind('\n', cursor - 1);
        lineStart = lineStart == std::string::npos || cursor == 0 ? 0 : lineStart + 1;
        size_t lineEnd = text.find('\n', cursor);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        if (keyboard_.keys.multiline()) {
            const long line = static_cast<long>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(cursor), '\n')) + 1;
            const long lines = static_cast<long>(std::count(text.begin(), text.end(), '\n')) + 1;
            title += "  (line " + std::to_string(line) + " of " + std::to_string(lines) + ")";
        }
        dl->AddText(font, size * 0.85f, ImVec2(left, y), NOTE, title.c_str());
        y += size * 1.2f;
        const float boxH = size * 1.6f;
        dl->AddRectFilled(ImVec2(left, y), ImVec2(left + innerW, y + boxH), KEY, 4.0f * scale);
        std::string before = text.substr(lineStart, cursor - lineStart) + "_";
        const float room = innerW - pad;
        while (!before.empty() && textWidth(before, size) > room) {
            size_t next = Forms::nextCharacter(before, 0);
            before.erase(0, std::max<size_t>(1, next));
        }
        std::string shown = before;
        std::string after = text.substr(cursor, lineEnd - cursor);
        while (!after.empty() && textWidth(shown + after, size) > room) {
            after.erase(Forms::previousCharacter(after, after.size()));
        }
        shown += after;
        dl->AddText(font, size, ImVec2(left + pad * 0.5f, y + (boxH - size) * 0.5f), KEY_TEXT_FOCUS, shown.c_str());
        y += boxH + pad;

        // The keys fill what's left above the hints
        const float hintsH = size * 1.4f;
        const Forms::KeyboardKeys::Rows& rows = keyboard_.keys.rows();
        const float spacing = size * 0.25f;
        const float gridH = a.y + height - pad - y - hintsH;
        const float keyH = (gridH - spacing * static_cast<float>(rows.size() - 1)) / static_cast<float>(rows.size());
        for (size_t r = 0; r < rows.size(); ++r) {
            const int units = Forms::KeyboardKeys::unitsOf(rows[r]);
            const float unitW = (innerW - spacing * static_cast<float>(units - 1)) / static_cast<float>(units);
            float x = left;
            const float keyY = y + static_cast<float>(r) * (keyH + spacing);
            for (size_t c = 0; c < rows[r].size(); ++c) {
                const Forms::KeyboardKeys::Key& key = rows[r][c];
                const float keyW = static_cast<float>(key.units) * unitW + static_cast<float>(key.units - 1) * spacing;
                const bool focused = static_cast<int>(r) == keyboard_.keys.row() && static_cast<int>(c) == keyboard_.keys.column() && !keyboard_.typing;
                const bool active = key.action == Forms::KeyboardKeys::Action::Shift && keyboard_.keys.shift();
                dl->AddRectFilled(ImVec2(x, keyY), ImVec2(x + keyW, keyY + keyH), focused ? KEY_FOCUS : active ? KEY_ACTIVE : KEY, 4.0f * scale);
                const float labelW = textWidth(key.label, size);
                dl->AddText(font, size, ImVec2(x + (keyW - labelW) * 0.5f, keyY + (keyH - size) * 0.5f), focused ? KEY_TEXT_FOCUS : KEY_TEXT,
                            key.label.c_str());
                x += keyW + spacing;
            }
        }

        std::string hint = keyboard_.typing
            ? (keyboard_.keys.multiline() ? "Typing on the keyboard: Ctrl+Enter keeps it, Esc cancels"
                                          : "Typing on the keyboard: Enter keeps it, Esc cancels")
            : "Select: type   Done: keep   Back: cancel";
        if (!keyboard_.typing && !keyboard_.keys.numeric()) hint += "   Shift: capitals and symbols";
        dl->AddText(font, size * 0.85f, ImVec2(left, a.y + height - pad - hintsH + (hintsH - size * 0.85f) * 0.5f), NOTE, hint.c_str());
    }
}

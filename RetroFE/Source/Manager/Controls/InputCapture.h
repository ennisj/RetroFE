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

#include "../../Control/InputToken.h"
#include <string>

namespace Manager
{
    // "Press it now" (Forms::Model::Result::capture): the next input pressed
    // and released becomes a token. Every input can be captured, Back's and
    // Escape included, so the ways out are waiting (nothing pressed for
    // timeout seconds) or holding an input down for holdToCancel seconds.
    // A press counts on release, so holding is never also taken as a press.
    //
    // A game controller's press arrives twice, as a joystick input and as a
    // controller one (joyButton0 and joyA): the controller's is kept, as it
    // means the same button on any pad. SDL-free; ManagerUI turns SDL events
    // into press/release (Controls/app/CaptureEvents.h).
    class InputCapture
    {
    public:
        static constexpr float timeout = 5.0f;
        static constexpr float holdToCancel = 2.0f;
        // A controller input this soon after a joystick one is the same press
        static constexpr float samePress = 0.25f;

        enum class State { Idle, Waiting, Captured, Cancelled, TimedOut };

        void start(float now);
        void clear() { *this = InputCapture(); }

        // Waiting for an input (Captured, Cancelled and TimedOut are over)
        bool active() const { return state_ == State::Waiting; }
        State state() const { return state_; }

        void press(const InputToken::Input& input, float now);
        void release(const InputToken::Input& input, float now);
        // Time passing: timeout, and holding to cancel
        void update(float now);

        // Captured: the token (InputToken::format)
        std::string token() const { return InputToken::format(input_); }
        const InputToken::Input& input() const { return input_; }

        // What the screen says while waiting, e.g. "Press the input to add
        // now (4 seconds left). Holding one down for 2 seconds cancels."
        std::string prompt(float now) const;
        // Why it ended without an input, for Cancelled and TimedOut
        std::string endMessage() const;

        // A stick or trigger's state, -1, 0 or +1, after it moved delta from
        // where it rests: pressed past half way, let go below a quarter (in
        // between it stays as it was). From one way straight to the other it
        // goes through 0 first, so the release is seen before the new press.
        static int axisState(int held, int delta);

    private:
        static bool isController(const InputToken::Input& input);
        static bool isJoystick(const InputToken::Input& input);

        State state_ = State::Idle;
        float started_ = 0.0f;
        bool holding_ = false;
        float pressed_ = 0.0f;
        InputToken::Input input_;   // the press being held, then the captured one
        InputToken::Input twin_;    // the same press's joystick (or controller) event
    };
}

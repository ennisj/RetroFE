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
#include "InputCapture.h"
#include <cmath>

namespace Manager
{

bool InputCapture::isController(const InputToken::Input& input)
{
    return input.kind == InputToken::Kind::ControllerButton || input.kind == InputToken::Kind::ControllerAxis;
}

bool InputCapture::isJoystick(const InputToken::Input& input)
{
    return input.kind == InputToken::Kind::JoyButton || input.kind == InputToken::Kind::JoyHat
        || input.kind == InputToken::Kind::JoyAxis;
}

void InputCapture::start(float now)
{
    clear();
    state_ = State::Waiting;
    started_ = now;
}

void InputCapture::press(const InputToken::Input& input, float now)
{
    if (state_ != State::Waiting || input.kind == InputToken::Kind::Invalid) {
        return;
    }
    if (!holding_) {
        holding_ = true;
        pressed_ = now;
        input_ = input;
        return;
    }
    // The controller's view of the press being held wins over the joystick's,
    // whichever arrives first; anything else pressed meanwhile is ignored
    if (now - pressed_ <= samePress && twin_.kind == InputToken::Kind::Invalid) {
        if (isController(input) && isJoystick(input_)) {
            twin_ = input_;
            input_ = input;
        }
        else if (isJoystick(input) && isController(input_)) {
            twin_ = input;
        }
    }
}

void InputCapture::release(const InputToken::Input& input, float now)
{
    if (state_ != State::Waiting || !holding_) {
        return;   // e.g. the Select that started the capture being let go
    }
    if (!InputToken::same(input, input_) && !InputToken::same(input, twin_)) {
        return;
    }
    holding_ = false;
    state_ = now - pressed_ >= holdToCancel ? State::Cancelled : State::Captured;
}

void InputCapture::update(float now)
{
    if (state_ != State::Waiting) {
        return;
    }
    if (holding_ && now - pressed_ >= holdToCancel) {
        holding_ = false;
        state_ = State::Cancelled;
    }
    else if (!holding_ && now - started_ >= timeout) {
        state_ = State::TimedOut;
    }
}

std::string InputCapture::prompt(float now) const
{
    if (holding_) {
        return "Let go to add " + InputToken::describe(input_) + ", or keep holding it to cancel.";
    }
    int left = static_cast<int>(std::ceil(timeout - (now - started_)));
    if (left < 1) {
        left = 1;
    }
    return "Press the input to add now (" + std::to_string(left) + (left == 1 ? " second" : " seconds")
        + " left). Holding one down for 2 seconds cancels.";
}

int InputCapture::axisState(int held, int delta)
{
    const int pressedAt = 16384;   // half way
    const int releasedAt = 8192;   // a quarter
    int magnitude = delta < 0 ? -delta : delta;
    int now = held;
    if (magnitude >= pressedAt) {
        now = delta > 0 ? 1 : -1;
    }
    else if (magnitude < releasedAt) {
        now = 0;
    }
    if (held != 0 && now != held) {
        return 0;   // let go (or straight to the other way: released first)
    }
    return now;
}

std::string InputCapture::endMessage() const
{
    if (state_ == State::Cancelled) {
        return "Cancelled: nothing was added.";
    }
    if (state_ == State::TimedOut) {
        return "Nothing was pressed, so nothing was added.";
    }
    return "";
}

}

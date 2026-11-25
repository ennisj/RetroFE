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

#include <string>

namespace Manager
{
    class LaunchNotice
    {
    public:
        void show(const std::string& text)
        {
            text_ = text;
            remaining_ = text.empty() ? 0.0f : 8.0f;
        }

        void update(float dt)
        {
            if (visible() && dt > 0.0f) {
                remaining_ -= dt;
                if (remaining_ <= 0.0f) clear();
            }
        }

        bool visible() const { return remaining_ > 0.0f; }
        const std::string& text() const { return text_; }
        void clear() { remaining_ = 0.0f; text_.clear(); }

    private:
        std::string text_;
        float remaining_ = 0.0f;
    };
}

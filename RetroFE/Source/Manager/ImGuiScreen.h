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
    // A screen ManagerUI draws with its own ImGui code instead of a form,
    // whatever formsRenderer says (the theme editor: a canvas doesn't fit
    // rows of fields). It sits in the navigation stack like any screen: its
    // form id is a real form, opened and left the usual way.
    //
    // The rule that keeps it safe: pages are built and destroyed, and
    // textures created and freed, only in update() and shutdown(), never in
    // draw() or drawTargets(). ManagerUI deletes a screen from update() too.
    class ImGuiScreen
    {
    public:
        virtual ~ImGuiScreen() = default;

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

        // As Provider::leaving / discard
        virtual std::string leaving() { return ""; }
        virtual void discard() {}

        // A message for the user (e.g. why they can't leave yet)
        virtual void setStatus(const std::string& message, bool isError) { (void)message; (void)isError; }

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

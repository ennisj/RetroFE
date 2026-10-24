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

#include "../../Forms/BuiltinForms.h"

namespace Manager
{
    // The RetroArch screens (RetroArchProvider, prefix "retroarch"): a game's
    // (system = false: "game-retroarch", "game-core-options", "game-core-option",
    // "game-controls") or a system's core-wide ones ("system-..."). One set of
    // templates (RetroArchForms.cpp); the Games and Systems tables add them.
    const Forms::BuiltinTable& retroArchForms(bool system);
    // A launcher's core-wide screens ("emulator-retroarch"...; params id + scope),
    // worded as a system's; the Emulators table adds them
    const Forms::BuiltinTable& retroArchLauncherForms();
}

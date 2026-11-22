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

// What Add a system suggests for a launcher's games: the system's name and
// file extensions, from the program the launcher starts. Standalone
// emulators come from a small built-in table; for RetroArch, the .info file
// of the core its arguments load. Nothing is downloaded. No SDL: built into
// retrofe_tests too.

#include "../../Library/Index.h"
#include <string>
#include <vector>

namespace Emulators
{
    // A standalone emulator RetroFE knows by its program's file name
    struct KnownEmulator
    {
        const char* program;      // e.g. "duckstation-qt-x64-ReleaseLTCG.exe"
        const char* system;       // e.g. "PlayStation"
        const char* extensions;   // e.g. "cue,chd,bin"
    };
    const std::vector<KnownEmulator>& knownEmulators();

    // The table's entry for a program (a path or a bare file name, either
    // separator, any case), or nullptr
    const KnownEmulator* findKnownEmulator(const std::string& program);

    struct SystemSuggestion
    {
        std::string name;         // "" when nothing is known
        std::string extensions;   // comma-separated
    };

    // For a launcher: the table's entry for its program, else, for RetroArch,
    // systemname and supported_extensions from <RetroArch's folder>/info/
    // <core>_libretro.info for the core its arguments load (-L ...), when that
    // file is there. root is RetroFE's folder (%RETROFE_PATH%, relative paths).
    SystemSuggestion suggestSystem(const std::string& root, const Library::LauncherDef& launcher);
}

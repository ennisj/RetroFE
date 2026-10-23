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

// What the Emulators screens show about launchers, worked out from a library
// snapshot: where a launcher is defined, whether its program is there, who
// uses it, which names are asked for but not defined, and the argument
// presets for a new launcher. No SDL: built into retrofe_tests too.

#include "../../Library/Index.h"
#include <string>
#include <utility>
#include <vector>

namespace Emulators
{
    // "global", "collection" or "local": the scope parameter of the launcher screens
    std::string scopeKey(Library::LauncherScope scope);
    bool parseScope(const std::string& key, Library::LauncherScope& scope);

    // Where a launcher is defined, as a phrase: "launchers.windows",
    // "Arcade's own launcher file", "Arcade's local launchers"
    std::string whereDefined(const Library::Index& index, const Library::LauncherDef& launcher);

    // Whether a launcher's program is there. Reads the disk: call it when a
    // screen or snapshot changes, not every frame.
    struct ExecutableCheck
    {
        // WindowsOnly: a relative path with a folder in a Linux or macOS launcher,
        // which RetroFE resolves against the working folder there, not its own
        enum class State { Found, OnPath, Missing, WindowsOnly, Unchecked };
        State state = State::Unchecked;
        std::string path;       // the file looked for, variables replaced
        std::string sentence;   // for the screen
    };
    ExecutableCheck checkExecutable(const Library::Index& index, const std::string& executable);

    // Who uses a launcher
    struct Users
    {
        std::vector<int> defaults;       // systems whose games start with it unless they pick another
        std::vector<int> alternatives;   // systems listing it after the default (launcher = a, b)
        std::vector<std::pair<int, std::string>> games;  // (collection, game) that pick it

        size_t systemCount() const;      // distinct systems, defaults and alternatives
        bool empty() const { return defaults.empty() && alternatives.empty() && games.empty(); }
        // "The default of 2 systems, an alternative of 1; 5 games pick it" or "Nothing uses it."
        std::string summary() const;
        // "2 systems, 5 games" or "Unused"
        std::string shortSummary() const;
    };
    Users usersOf(const Library::Index& index, int launcher);
    // Every launcher's users at once (by index into Index::launchers), in one pass
    std::vector<Users> allUsers(const Library::Index& index);

    // Names a system or game asks for that no launcher file defines, with who asks
    struct Unresolved
    {
        std::string name;
        Users users;
    };
    std::vector<Unresolved> unresolvedLaunchers(const Library::Index& index);

    // Arguments for a new launcher
    struct Preset
    {
        std::string id;
        std::string label;
    };
    const std::vector<Preset>& presets();   // file, retroarch, mame, custom
    // The arguments a preset gives. core: RetroArch's core file, as written in the launcher.
    std::string presetArguments(const std::string& preset, const std::string& core);

    // A path as a new launcher stores it: inside RetroFE's folder as
    // %RETROFE_PATH%/..., so the drive can move; elsewhere as it is. A
    // relative path with a folder in it is taken as relative to RetroFE's
    // folder (as RetroFE runs it); a bare program name is left alone.
    // Separators are the os's ("windows" gets backslashes).
    std::string portablePath(const std::string& root, const std::string& path, const std::string& os);
}

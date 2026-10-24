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
#include "Launchers.h"
#include "../../Library/Files.h"
#include "../../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace Emulators
{

std::string scopeKey(Library::LauncherScope scope)
{
    switch (scope) {
    case Library::LauncherScope::Collection: return "collection";
    case Library::LauncherScope::Local: return "local";
    default: return "global";
    }
}

bool parseScope(const std::string& key, Library::LauncherScope& scope)
{
    if (key.empty() || key == "global") {
        scope = Library::LauncherScope::Global;
    }
    else if (key == "collection") {
        scope = Library::LauncherScope::Collection;
    }
    else if (key == "local") {
        scope = Library::LauncherScope::Local;
    }
    else {
        return false;
    }
    return true;
}

std::string whereDefined(const Library::Index& index, const Library::LauncherDef& launcher)
{
    switch (launcher.scope) {
    case Library::LauncherScope::Collection:
        return launcher.collection + "'s own launcher file";
    case Library::LauncherScope::Local:
        return launcher.collection + "'s local launchers";
    default:
        return "launchers." + index.options.os;
    }
}

namespace
{
    bool isFile(const fs::path& path)
    {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    }

    std::string plural(size_t n, const std::string& one, const std::string& many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    // a, b and c
    std::string joinParts(const std::vector<std::string>& parts, const std::string& last)
    {
        std::string text;
        for (size_t i = 0; i < parts.size(); i++) {
            if (i > 0) {
                text += i + 1 == parts.size() ? last : ", ";
            }
            text += parts[i];
        }
        return text;
    }
}

ExecutableCheck checkExecutable(const Library::Index& index, const std::string& executable)
{
    ExecutableCheck check;
    const std::string& root = index.options.root;
    std::string value = Utils::trimEnds(executable);
    if (value.empty()) {
        check.state = ExecutableCheck::State::Missing;
        check.sentence = "No program is set, so its games can't start.";
        return check;
    }
    // The variables Launcher::run replaces; the per-game ones can't be checked
    value = Utils::replace(value, "%RETROFE_PATH%", root);
#ifdef WIN32
    if (const char* comspec = std::getenv("COMSPEC")) {
        value = Utils::replace(value, "%CMD%", comspec);
    }
#endif
    check.path = value;
    if (value.find("%ITEM_") != std::string::npos || value.find("%COLLECTION_PATH%") != std::string::npos) {
        check.sentence = "It depends on the game, so it isn't checked.";
        return check;
    }
    // On Linux and macOS RetroFE changes to the working folder and runs the
    // program from there, so a relative path with a folder isn't found from
    // RetroFE's folder as it is on Windows
    std::string os = index.options.os.empty() ? Utils::getOSType() : index.options.os;
    bool absolute = value[0] == '/' || value[0] == '\\' || (value.size() > 1 && value[1] == ':');
    if (os != "windows" && !absolute && value.find_first_of("/\\") != std::string::npos) {
        check.state = ExecutableCheck::State::WindowsOnly;
        check.sentence = "A relative program path works on Windows only: on " + os
            + " RetroFE runs it from the working folder. Use %RETROFE_PATH%/" + value + " instead.";
        return check;
    }
    if (!index.options.os.empty() && index.options.os != Utils::getOSType()) {
        check.sentence = "This is a " + index.options.os + " launcher, so it isn't checked here.";
        return check;
    }

    // RetroFE runs from its own folder, so a relative path is relative to it
    fs::path path(value);
    if (!path.is_absolute()) {
        path = fs::path(root) / path;
    }
    if (isFile(path)) {
        check.state = ExecutableCheck::State::Found;
        check.path = Library::normalPath(Library::narrow(path));
        check.sentence = "Found.";
        return check;
    }
    // A bare name is looked up the way Launcher::run does (Library::findProgram)
    std::string found = Library::findProgram(value);
    if (!found.empty()) {
        check.state = ExecutableCheck::State::OnPath;
        check.path = found;
        check.sentence = "Not in RetroFE's folder; found on the system path (" + check.path + ").";
        return check;
    }
    check.state = ExecutableCheck::State::Missing;
    check.path = Library::normalPath(Library::narrow(path));
    check.sentence = "Not found: " + check.path + ". Its games won't start.";
    return check;
}

size_t Users::systemCount() const
{
    std::set<int> systems(defaults.begin(), defaults.end());
    systems.insert(alternatives.begin(), alternatives.end());
    return systems.size();
}

std::string Users::summary() const
{
    if (empty()) {
        return "Nothing uses it.";
    }
    std::vector<std::string> systems;
    if (!defaults.empty()) {
        systems.push_back("the default of " + plural(defaults.size(), "system", "systems"));
    }
    if (!alternatives.empty()) {
        systems.push_back("an alternative of " + plural(alternatives.size(), "system", "systems"));
    }
    std::string text = joinParts(systems, ", ");
    if (!games.empty()) {
        text += (text.empty() ? "" : "; ") + plural(games.size(), "game picks", "games pick") + " it";
    }
    text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text + ".";
}

std::string Users::shortSummary() const
{
    if (empty()) {
        return "Unused";
    }
    std::vector<std::string> parts;
    size_t systems = systemCount();
    if (systems > 0) {
        parts.push_back(plural(systems, "system", "systems"));
    }
    if (!games.empty()) {
        parts.push_back(plural(games.size(), "game", "games"));
    }
    return joinParts(parts, ", ");
}

namespace
{
    // Calls found(collection, game, alternative, name, launcher) for every use,
    // alternatives included. The name is only worked out for unresolved uses,
    // and only when names is set.
    template <typename Found>
    void eachUse(const Library::Index& index, bool names, Found found)
    {
        for (const Library::LauncherUse& use : index.launcherUses) {
            std::string name;
            if (use.launcher < 0 && names) {
                name = use.game.empty()
                    ? index.collections[static_cast<size_t>(use.collection)].launcher.name
                    : index.resolveLauncher(use.collection, use.game).name;
            }
            found(use.collection, use.game, false, name, use.launcher);
        }
        for (size_t c = 0; c < index.collections.size(); c++) {
            const Library::CollectionRecord& record = index.collections[c];
            if (!record.is(Library::KindSystem)) {
                continue;
            }
            for (const std::string& alternative : record.launcher.alternatives) {
                found(static_cast<int>(c), std::string(), true, alternative, index.findLauncher(alternative));
            }
        }
    }

    void add(Users& users, int collection, const std::string& game, bool alternative)
    {
        if (!game.empty()) {
            users.games.emplace_back(collection, game);
        }
        else {
            std::vector<int>& list = alternative ? users.alternatives : users.defaults;
            if (std::find(list.begin(), list.end(), collection) == list.end()) {
                list.push_back(collection);
            }
        }
    }
}

Users usersOf(const Library::Index& index, int launcher)
{
    Users users;
    if (launcher < 0) {
        return users;
    }
    eachUse(index, false, [&](int collection, const std::string& game, bool alternative, const std::string&, int used) {
        if (used == launcher) {
            add(users, collection, game, alternative);
        }
    });
    return users;
}

std::vector<Users> allUsers(const Library::Index& index)
{
    std::vector<Users> all(index.launchers.size());
    eachUse(index, false, [&](int collection, const std::string& game, bool alternative, const std::string&, int used) {
        if (used >= 0 && static_cast<size_t>(used) < all.size()) {
            add(all[static_cast<size_t>(used)], collection, game, alternative);
        }
    });
    return all;
}

std::vector<Unresolved> unresolvedLaunchers(const Library::Index& index)
{
    std::map<std::string, Users> byName;
    eachUse(index, true, [&](int collection, const std::string& game, bool alternative, const std::string& name, int used) {
        if (used < 0 && !name.empty()) {
            add(byName[name], collection, game, alternative);
        }
    });
    std::vector<Unresolved> result;
    for (auto& [name, users] : byName) {
        result.push_back(Unresolved{ name, std::move(users) });
    }
    std::sort(result.begin(), result.end(), [](const Unresolved& a, const Unresolved& b) {
        return Library::toLower(a.name) < Library::toLower(b.name);
    });
    return result;
}

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> all = {
        { "file", "The game's file: \"%ITEM_FILEPATH%\"" },
        { "retroarch", "RetroArch: -L <core> \"%ITEM_FILEPATH%\"" },
        { "mame", "MAME: %ITEM_NAME% with the rom path" },
        { "custom", "Custom" },
    };
    return all;
}

std::string presetArguments(const std::string& preset, const std::string& core)
{
    if (preset == "file") {
        return "\"%ITEM_FILEPATH%\"";
    }
    if (preset == "retroarch") {
        return "-L \"" + core + "\" \"%ITEM_FILEPATH%\"";
    }
    if (preset == "mame") {
        // RetroFE ends %ITEM_DIRECTORY% with a separator: on Windows "C:\roms\"
        // would escape the closing quote, so point at "." inside the folder
        return "%ITEM_NAME% -rompath \"%ITEM_DIRECTORY%.\"";
    }
    return "";
}

std::string unknownPlaceholder(const std::string& text)
{
    static const std::set<std::string> known = { "ITEM_FILEPATH", "ITEM_NAME", "ITEM_FILENAME", "ITEM_DIRECTORY",
        "ITEM_COLLECTION_NAME", "RETROFE_PATH", "COLLECTION_PATH", "RETROFE_EXEC_PATH", "CMD" };
    for (size_t start = text.find('%'); start != std::string::npos; start = text.find('%', start + 1)) {
        size_t end = start + 1;
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
            end++;
        }
        // %NAME%: a letter or _ first; anything else (%1, %%, %~dp0) isn't a variable
        if (end < text.size() && text[end] == '%' && end > start + 1
            && !std::isdigit(static_cast<unsigned char>(text[start + 1]))) {
            std::string name = text.substr(start + 1, end - start - 1);
            if (!known.count(name)) {
                return "%" + name + "%";
            }
            start = end;   // the closing % doesn't open another
        }
    }
    return "";
}

bool runsCommandPrompt(const std::string& program)
{
    std::string value = Utils::trimEnds(program);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    size_t slash = value.find_last_of("/\\");
    std::string name = Library::toLower(slash == std::string::npos ? value : value.substr(slash + 1));
    auto endsWith = [&](const std::string& suffix) {
        return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return name == "cmd" || name == "cmd.exe" || name == "%cmd%" || endsWith(".bat") || endsWith(".cmd");
}

std::string checkLauncherValues(const std::string& executable, const std::string& arguments, const std::string& workingFolder,
                                const std::string& program)
{
    bool prompt = runsCommandPrompt(program.empty() ? executable : program);
    const std::string* values[] = { &executable, &arguments, &workingFolder };
    for (const std::string* field : values) {
        const std::string& value = *field;
        if (value.find_first_of("#\r\n") != std::string::npos) {
            return "The program, arguments and working folder can't contain # or a line break.";
        }
        if (prompt && field == &arguments) {
            continue;   // the command prompt expands its own %NAME% variables
        }
        std::string unknown = unknownPlaceholder(value);
        if (!unknown.empty()) {
            return "RetroFE doesn't replace " + unknown + ", so the program would get it as it is. It knows %ITEM_FILEPATH%, "
                   "%ITEM_NAME%, %ITEM_FILENAME%, %ITEM_DIRECTORY%, %ITEM_COLLECTION_NAME%, %RETROFE_PATH%, %COLLECTION_PATH%, "
                   "%RETROFE_EXEC_PATH% and %CMD%.";
        }
    }
    return "";
}

std::string portablePath(const std::string& root, const std::string& path, const std::string& os)
{
    std::string value = Utils::trimEnds(path);
    if (value.empty() || value.find('%') != std::string::npos) {
        return value;
    }
    if (value.find_first_of("/\\") == std::string::npos) {
        return value;   // a program on the system path, e.g. cmd.exe
    }
    fs::path full(value);
    if (!full.is_absolute()) {
        full = fs::path(root) / full;
    }
    std::string normal = Library::normalPath(Library::narrow(full));
    std::string base = Library::normalPath(root);
    std::string result = normal;
    bool inside = normal.size() > base.size() + 1 && (normal[base.size()] == '/' || normal[base.size()] == '\\');
    if (inside) {
#ifdef WIN32
        inside = Library::iequals(normal.substr(0, base.size()), base);
#else
        inside = normal.compare(0, base.size(), base) == 0;
#endif
    }
    if (inside) {
        result = "%RETROFE_PATH%/" + normal.substr(base.size() + 1);
    }
    char separator = os == "windows" ? '\\' : '/';
    std::replace(result.begin(), result.end(), separator == '\\' ? '/' : '\\', separator);
    return result;
}

}

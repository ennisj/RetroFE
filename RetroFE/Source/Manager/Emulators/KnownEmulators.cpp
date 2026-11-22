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
#include "KnownEmulators.h"
#include "../../Library/RetroArch.h"
#include "../../Utility/Utils.h"
#include <algorithm>

namespace
{
    std::string unquoted(const std::string& text)
    {
        std::string value = Utils::trimEnds(text);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }

    // Windows' and other systems' separators alike: launchers are often written on Windows
    std::string fileNameOf(const std::string& path)
    {
        size_t at = path.find_last_of("/\\");
        return at == std::string::npos ? path : path.substr(at + 1);
    }

    std::string folderOf(const std::string& path)
    {
        size_t at = path.find_last_of("/\\");
        return at == std::string::npos ? "" : path.substr(0, at);
    }

    bool isAbsolute(const std::string& path)
    {
        return (!path.empty() && (path[0] == '/' || path[0] == '\\')) || (path.size() > 1 && path[1] == ':');
    }

    // The core the arguments load: -L <core>, --libretro <core>, --libretro=<core>, -L<core>
    std::string coreArgument(const std::string& arguments)
    {
        std::vector<std::string> args = Library::RetroArch::splitArguments(arguments);
        for (size_t i = 0; i < args.size(); i++) {
            const std::string& a = args[i];
            if ((a == "-L" || a == "--libretro") && i + 1 < args.size()) return args[i + 1];
            if (a.compare(0, 11, "--libretro=") == 0) return a.substr(11);
            if (a.size() > 2 && a.compare(0, 2, "-L") == 0 && a[2] != '-') return a.substr(2);
        }
        return "";
    }
}

namespace Emulators
{

const std::vector<KnownEmulator>& knownEmulators()
{
    static const std::vector<KnownEmulator> table = {
        { "mame.exe", "Arcade", "zip,7z,chd" },
        { "duckstation-qt-x64-ReleaseLTCG.exe", "PlayStation", "cue,chd,bin,img,iso,m3u,pbp,ecm,mds,psexe,psf,minipsf" },
        { "pcsx2-qt.exe", "PlayStation 2", "iso,chd,cso,zso,gz,elf,irx,m3u,cue,bin,img,mdf,nrg,dump" },
        { "PPSSPPWindows64.exe", "PSP", "iso,cso,chd,pbp,elf,prx" },
        { "flycast.exe", "Dreamcast", "chd,cdi,gdi,cue,m3u,elf,dat,zip,7z,lst" },
    };
    return table;
}

const KnownEmulator* findKnownEmulator(const std::string& program)
{
    std::string name = Utils::toLower(fileNameOf(unquoted(program)));
    if (name.empty()) {
        return nullptr;
    }
    for (const KnownEmulator& entry : knownEmulators()) {
        if (Utils::toLower(entry.program) == name) {
            return &entry;
        }
    }
    return nullptr;
}

SystemSuggestion suggestSystem(const std::string& root, const Library::LauncherDef& launcher)
{
    SystemSuggestion suggestion;
    if (const KnownEmulator* known = findKnownEmulator(launcher.executable)) {
        suggestion.name = known->system;
        suggestion.extensions = known->extensions;
        return suggestion;
    }
    if (!Library::RetroArch::runsRetroArch(launcher)) {
        return suggestion;
    }
    // RetroArch's folder: the program's, relative paths being relative to RetroFE's folder
    std::string program = Utils::replace(unquoted(launcher.executable), "%RETROFE_PATH%", root);
    std::string folder = folderOf(program);
    std::string stem = Library::RetroArch::coreStem(coreArgument(launcher.arguments));
    if (folder.empty() || stem.empty()) {
        return suggestion;
    }
    std::replace(folder.begin(), folder.end(), '\\', '/');   // Windows takes either; elsewhere only this one
    if (!isAbsolute(folder)) {
        folder = Utils::combinePath(root, folder);
    }
    auto info = Library::RetroArch::readConfigFile(Utils::combinePath(folder, "info", stem + "_libretro.info"));
    suggestion.name = info["systemname"];
    suggestion.extensions = info["supported_extensions"];
    std::replace(suggestion.extensions.begin(), suggestion.extensions.end(), '|', ',');
    return suggestion;
}

}

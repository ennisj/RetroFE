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
#include "Saves.h"
#include "Files.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{
namespace Saves
{

namespace
{
    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    bool isAbsolute(const std::string& path)
    {
        return !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    }

    std::string join(const std::string& folder, const std::string& name)
    {
        if (folder.empty()) return normalPath(name);
        if (name.empty()) return normalPath(folder);
        return normalPath(narrow(fs::path(folder) / fs::path(name)));
    }

    std::string parentOf(const std::string& path)
    {
        return normalPath(narrow(fs::path(path).parent_path()));
    }

    std::string lastPart(std::string path)
    {
        while (!path.empty() && (path.back() == '/' || path.back() == '\\')) path.pop_back();
        size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::string stemOf(const std::string& path)
    {
        std::string name = lastPart(path);
        size_t dot = name.find_last_of('.');
        return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    }

    bool isDir(const std::string& path)
    {
        std::error_code ec;
        return !path.empty() && fs::is_directory(fs::path(path), ec);
    }

    bool isTrue(const std::map<std::string, std::string>& cfg, const std::string& key)
    {
        auto found = cfg.find(key);
        return found != cfg.end() && toLower(trim(found->second)) == "true";
    }

    // One plain path segment: used as a folder name under manage/saves
    bool isPlainSegment(const std::string& name)
    {
        if (name.empty() || name.find_first_of("/\\:*?\"<>|") != std::string::npos) return false;
        for (unsigned char c : name) {
            if (c < 32) return false;
        }
        return name.find_first_not_of('.') != std::string::npos;
    }

    bool allDigits(const std::string& s, size_t from, size_t to)
    {
        if (from >= to) return false;
        for (size_t i = from; i < to; i++) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    }

    // ".state", ".state12", ".state.auto" (without a trailing .png)
    bool isStateName(const std::string& rest)
    {
        if (rest == ".state" || rest == ".state.auto") return true;
        if (rest.compare(0, 6, ".state") == 0 && allDigits(rest, 6, rest.size())) return true;
        return false;
    }

    std::vector<File> filesIn(const std::string& folder, const std::string& stem, Kind wanted, const std::string& skip)
    {
        std::vector<File> out;
        std::error_code ec;
        if (!isDir(folder)) return out;
        for (fs::directory_iterator it(fs::path(folder), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc)) continue;
            std::string name;
            if (!narrowName(it->path().filename(), name)) continue;
            Kind kind;
            if (!belongsTo(name, stem, kind) || kind != wanted) continue;
            std::string path = join(folder, name);
            if (!skip.empty() && iequals(path, normalPath(skip))) continue;
            File file;
            file.path = path;
            file.name = name;
            file.kind = kind;
            file.size = it->file_size(fileEc);
            out.push_back(file);
        }
        std::sort(out.begin(), out.end(), [](const File& a, const File& b) { return a.name < b.name; });
        return out;
    }

    std::string pad(int value, int width)
    {
        std::string text = std::to_string(value);
        while (static_cast<int>(text.size()) < width) text = "0" + text;
        return text;
    }

    std::tm localNow()
    {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        return tm;
    }

    // "20261024-101530" -> "2026-10-24 10:15:30" (a "-2" suffix is dropped)
    std::string whenOf(const std::string& id)
    {
        if (id.size() < 15 || id[8] != '-' || !allDigits(id, 0, 8) || !allDigits(id, 9, 15)) return id;
        return id.substr(0, 4) + "-" + id.substr(4, 2) + "-" + id.substr(6, 2) + " " + id.substr(9, 2) + ":"
            + id.substr(11, 2) + ":" + id.substr(13, 2);
    }

    std::string plural(size_t n, const std::string& one, const std::string& many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    // Copies files into a new backup folder; folder gets its path
    EditResult copyIntoBackup(const std::string& root, const std::string& collection, const std::string& game,
                              const std::vector<File>& files, const std::string& reason, std::string* idOut)
    {
        std::string base = backupRoot(root, collection, game);
        if (base.empty()) {
            return EditResult::fail("This game's name can't be a folder name, so its saves can't be backed up here.");
        }
        std::tm tm = localNow();
        std::string stamp = pad(tm.tm_year + 1900, 4) + pad(tm.tm_mon + 1, 2) + pad(tm.tm_mday, 2) + "-"
            + pad(tm.tm_hour, 2) + pad(tm.tm_min, 2) + pad(tm.tm_sec, 2);
        std::string id = stamp;
        std::error_code ec;
        for (int n = 2; fs::exists(fs::path(join(base, id)), ec) && n < 1000; n++) {
            id = stamp + "-" + std::to_string(n);
        }
        std::string folder = join(base, id);
        EditResult result;
        std::string error;
        for (const File& file : files) {
            std::string to = join(join(folder, file.kind == Kind::Save ? "saves" : "states"), file.name);
            if (!copyFileAtomic(file.path, to, error)) {
                fs::remove_all(fs::path(folder), ec);   // no half backups
                return EditResult::fail("Couldn't back up " + file.name + ": " + error);
            }
        }
        std::string about = "reason = " + reason + "\n";
        if (!writeFileAtomic(join(folder, "backup.txt"), about, error)) {
            fs::remove_all(fs::path(folder), ec);
            return EditResult::fail("Couldn't write the backup: " + error);
        }
        if (idOut) *idOut = id;
        result.ok = true;
        result.files.push_back(folder);
        result.message = "Backed up " + plural(files.size(), "file", "files") + " (" + whenOf(id) + ").";
        return result;
    }
}

Folders forGame(const RetroArch::Setup& setup, const GameRecord& game, const RetroArch::Environment& env)
{
    Folders f;
    if (setup.state == RetroArch::State::NotRetroArch) {
        f.why = "Manage only knows where RetroArch keeps saves. This game's launcher (" + setup.launcher
            + ") runs another program, which keeps them its own way.";
        return f;
    }
    if (setup.configFile.empty()) {
        f.why = setup.why.empty() ? "RetroArch's settings file wasn't found." : setup.why;
        return f;
    }
    if (game.file.empty()) {
        f.why = "This game has no file, so RetroArch has nothing to name its saves after.";
        return f;
    }
    // RetroArch names them after the content file without its extension (an
    // archive's own name), or the name it was handed
    f.contentFile = normalPath(game.file);
    f.stem = setup.passesName ? game.name : stemOf(game.file);
    if (f.stem.empty()) f.stem = game.name;

    std::map<std::string, std::string> cfg = RetroArch::readConfigFile(setup.configFile);
    std::string configFolder = parentOf(setup.configFile);
    std::string programFolder = setup.program.empty() ? configFolder : parentOf(setup.program);
    // The platform's defaults: ":\saves" (RetroArch's folder) on Windows,
    // <config folder>/saves elsewhere
    std::string base = env.os == "windows" ? programFolder : configFolder;
    auto rest = [](const std::string& v) {
        size_t start = v.find_first_not_of("/\\", 1);
        return start == std::string::npos ? std::string() : v.substr(start);
    };
    // "default" or nothing: the default; a path that isn't a folder is ignored (RetroArch warns and keeps the default)
    auto folder = [&](const std::string& key, const std::string& fallback) {
        auto found = cfg.find(key);
        std::string v = found == cfg.end() ? "" : trim(found->second);
        if (v.empty() || v == "default") return normalPath(fallback);
        std::string path;
        if (v[0] == ':') path = join(programFolder, rest(v));
        else if (v[0] == '~') path = join(env.home, rest(v));
        else path = isAbsolute(v) ? normalPath(v) : join(setup.workingFolder, v);
        if (!isDir(path)) {
            f.notes.push_back(key + " (" + path + ") isn't a folder, so RetroArch uses its default.");
            return normalPath(fallback);
        }
        return path;
    };
    std::string content = parentOf(game.file);
    std::string contentName = lastPart(content);
    std::string saves = folder("savefile_directory", join(base, "saves"));
    std::string states = folder("savestate_directory", join(base, "states"));
    if (isTrue(cfg, "savefiles_in_content_dir")) saves = content;
    if (isTrue(cfg, "savestates_in_content_dir")) states = content;

    // Per content folder, then per core (runloop_path_set_redirect's order)
    auto sorted = [&](std::string dir, const char* byContent, const char* byCore, const std::string& what) {
        if (isTrue(cfg, byContent) && !contentName.empty()) dir = join(dir, contentName);
        if (isTrue(cfg, byCore)) {
            if (setup.library.empty()) {
                f.why = "RetroArch sorts " + what + " into a folder per core (" + byCore
                    + "), and this game's core isn't known: " + setup.why;
                return std::string();
            }
            dir = join(dir, setup.library);
        }
        return dir;
    };
    f.saves = sorted(saves, "sort_savefiles_by_content_enable", "sort_savefiles_enable", "save files");
    if (f.saves.empty()) return f;
    f.states = sorted(states, "sort_savestates_by_content_enable", "sort_savestates_enable", "save states");
    if (f.states.empty()) return f;
    if (!setup.libraryCertain && !setup.library.empty()
        && (isTrue(cfg, "sort_savefiles_enable") || isTrue(cfg, "sort_savestates_enable"))) {
        f.notes.push_back("The core's folder name (" + setup.library + ") isn't confirmed yet.");
    }
    f.known = true;
    return f;
}

bool belongsTo(const std::string& fileName, const std::string& stem, Kind& kind)
{
    if (stem.empty() || fileName.size() <= stem.size() || fileName.compare(0, stem.size(), stem) != 0) return false;
    std::string rest = toLower(fileName.substr(stem.size()));
    if (rest[0] != '.') return false;
    static const char* const saveSuffixes[] = { ".srm", ".sav", ".rtc", ".ldci", ".eep", ".sra", ".fla", ".mpk", ".dsv", ".mcr" };
    for (const char* suffix : saveSuffixes) {
        if (rest == suffix) {
            kind = Kind::Save;
            return true;
        }
    }
    // Extra memory cards: <stem>.1.mcr
    if (rest.size() > 5 && rest.compare(rest.size() - 4, 4, ".mcr") == 0 && allDigits(rest, 1, rest.size() - 4)) {
        kind = Kind::Save;
        return true;
    }
    std::string state = rest;
    if (state.size() > 4 && state.compare(state.size() - 4, 4, ".png") == 0) state = state.substr(0, state.size() - 4);
    if (isStateName(state)) {
        kind = Kind::State;
        return true;
    }
    if (rest == ".bsv" || rest == ".replay" || (rest.compare(0, 7, ".replay") == 0 && allDigits(rest, 7, rest.size()))) {
        kind = Kind::State;
        return true;
    }
    return false;
}

std::vector<File> list(const Folders& folders)
{
    std::vector<File> out;
    if (!folders.known) return out;
    out = filesIn(folders.saves, folders.stem, Kind::Save, folders.contentFile);
    std::vector<File> states = filesIn(folders.states, folders.stem, Kind::State, folders.contentFile);
    out.insert(out.end(), states.begin(), states.end());
    return out;
}

std::string backupRoot(const std::string& root, const std::string& collection, const std::string& game)
{
    if (!isPlainSegment(collection) || !isPlainSegment(game)) return "";
    return join(join(join(join(root, "manage"), "saves"), collection), game);
}

bool findBackup(const std::string& root, const std::string& collection, const std::string& game,
                const std::string& id, Backup& backup)
{
    std::string base = backupRoot(root, collection, game);
    if (base.empty() || !isPlainSegment(id) || !isDir(join(base, id))) return false;
    backup = Backup();
    backup.id = id;
    backup.folder = join(base, id);
    backup.when = whenOf(id);
    std::string about;
    if (readFile(join(backup.folder, "backup.txt"), about)) {
        for (const Line& line : Lines::parse(about).lines) {
            size_t eq = line.text.find('=');
            if (eq != std::string::npos && trim(line.text.substr(0, eq)) == "reason") backup.reason = trim(line.text.substr(eq + 1));
        }
    }
    for (Kind kind : { Kind::Save, Kind::State }) {
        std::string sub = join(backup.folder, kind == Kind::Save ? "saves" : "states");
        std::error_code ec;
        if (!isDir(sub)) continue;
        std::vector<File> files;
        for (fs::directory_iterator it(fs::path(sub), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            std::string name;
            if (!it->is_regular_file(fileEc) || !narrowName(it->path().filename(), name)) continue;
            File file;
            file.path = join(sub, name);
            file.name = name;
            file.kind = kind;
            file.size = it->file_size(fileEc);
            backup.size += file.size;
            files.push_back(file);
        }
        std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.name < b.name; });
        backup.files.insert(backup.files.end(), files.begin(), files.end());
    }
    return true;
}

std::vector<Backup> backups(const std::string& root, const std::string& collection, const std::string& game)
{
    std::vector<Backup> out;
    std::string base = backupRoot(root, collection, game);
    std::error_code ec;
    if (base.empty() || !isDir(base)) return out;
    for (fs::directory_iterator it(fs::path(base), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        std::string name;
        if (!it->is_directory(dirEc) || !narrowName(it->path().filename(), name)) continue;
        Backup backup;
        if (findBackup(root, collection, game, name, backup)) out.push_back(backup);
    }
    // Ids sort by time; "-2" comes after the plain one of the same second
    std::sort(out.begin(), out.end(), [](const Backup& a, const Backup& b) {
        std::string ka = a.id.size() < 15 ? a.id : a.id.substr(0, 15);
        std::string kb = b.id.size() < 15 ? b.id : b.id.substr(0, 15);
        if (ka != kb) return ka > kb;
        int na = a.id.size() > 16 ? std::atoi(a.id.c_str() + 16) : 1;
        int nb = b.id.size() > 16 ? std::atoi(b.id.c_str() + 16) : 1;
        return na > nb;
    });
    return out;
}

EditResult backUp(const std::string& root, const std::string& collection, const std::string& game,
                  const Folders& folders, const std::string& reason, std::string* id)
{
    if (!folders.known) return EditResult::fail(folders.why);
    std::vector<File> files = list(folders);
    if (files.empty()) return EditResult::fail("This game has no saves or save states to back up.");
    return copyIntoBackup(root, collection, game, files, reason, id);
}

EditResult restore(const std::string& root, const std::string& collection, const std::string& game,
                   const Folders& folders, const std::string& id)
{
    if (!folders.known) return EditResult::fail(folders.why);
    Backup backup;
    if (!findBackup(root, collection, game, id, backup)) return EditResult::fail("There's no backup called " + id + ".");
    if (backup.files.empty()) return EditResult::fail("This backup has no files.");
    // Only ever the game's own files: a name that isn't is refused, not copied
    for (const File& file : backup.files) {
        Kind kind;
        if (!belongsTo(file.name, folders.stem, kind) || kind != file.kind) {
            return EditResult::fail(file.name + " in this backup isn't one of " + folders.stem
                                    + "'s saves, so the backup isn't restored.");
        }
    }
    std::vector<File> current = list(folders);
    std::string before;
    EditResult result;
    if (!current.empty()) {
        EditResult saved = copyIntoBackup(root, collection, game, current, "Before restoring the backup from " + backup.when, &before);
        if (!saved.ok) return EditResult::fail("Nothing was changed: " + saved.message);
        result.files = saved.files;
    }
    std::error_code ec;
    for (const File& file : current) {
        if (!fs::remove(fs::path(file.path), ec) && ec) {
            return EditResult::fail("Couldn't remove " + file.name + " (" + ec.message() + "). What was there is in the backup from "
                                    + whenOf(before) + ".");
        }
        result.files.push_back(file.path);
    }
    std::string error;
    for (const File& file : backup.files) {
        std::string to = join(file.kind == Kind::Save ? folders.saves : folders.states, file.name);
        if (!copyFileAtomic(file.path, to, error)) {
            return EditResult::fail("Couldn't put back " + file.name + ": " + error
                                    + (before.empty() ? std::string() : " What was there before is in the backup from " + whenOf(before) + "."));
        }
        result.files.push_back(to);
    }
    result.ok = true;
    result.message = "Restored the backup from " + backup.when + " (" + plural(backup.files.size(), "file", "files") + ")."
        + (before.empty() ? std::string() : " What was there before is kept as the backup from " + whenOf(before) + ".");
    return result;
}

EditResult removeAll(const std::string& root, const std::string& collection, const std::string& game, const Folders& folders)
{
    if (!folders.known) return EditResult::fail(folders.why);
    std::vector<File> files = list(folders);
    if (files.empty()) return EditResult::fail("This game has no saves or save states.");
    std::string id;
    EditResult result = copyIntoBackup(root, collection, game, files, "Before deleting", &id);
    if (!result.ok) return EditResult::fail("Nothing was deleted: " + result.message);
    std::error_code ec;
    for (const File& file : files) {
        if (!fs::remove(fs::path(file.path), ec) && ec) {
            return EditResult::fail("Couldn't delete " + file.name + " (" + ec.message() + "). Everything is in the backup from "
                                    + whenOf(id) + ".");
        }
        result.files.push_back(file.path);
    }
    result.message = "Deleted " + plural(files.size(), "file", "files") + ". A copy is kept as the backup from " + whenOf(id) + ".";
    return result;
}

EditResult removeBackup(const std::string& root, const std::string& collection, const std::string& game, const std::string& id)
{
    Backup backup;
    if (!findBackup(root, collection, game, id, backup)) return EditResult::fail("There's no backup called " + id + ".");
    std::error_code ec;
    fs::remove_all(fs::path(backup.folder), ec);
    if (ec) return EditResult::fail("Couldn't delete the backup: " + ec.message());
    // Tidy the game's folder when it was the last one
    std::string base = backupRoot(root, collection, game);
    if (fs::is_empty(fs::path(base), ec)) fs::remove(fs::path(base), ec);
    return EditResult{ true, "Deleted the backup from " + backup.when + ".", { backup.folder } };
}

std::string sizeText(std::uintmax_t bytes)
{
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024) return std::to_string(static_cast<long long>(kb + 0.5)) + " KB";
    double mb = kb / 1024.0;
    long long tenths = static_cast<long long>(mb * 10 + 0.5);
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
}

}
}

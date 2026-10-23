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
#include "SelfUpdate.h"
#include "GitHub.h"
#include "Hash.h"
#include "Index.h"
#include "Installer.h"
#include "../Utility/Log.h"
#include "minizip/unzip.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <spawn.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string utf8(const fs::path& path)
    {
        return path.generic_u8string();
    }

    // Compares paths the way the file system does: Windows ignores case
    std::string pathKey(const fs::path& path)
    {
#ifdef _WIN32
        return lower(utf8(path.lexically_normal()));
#else
        return utf8(path.lexically_normal());
#endif
    }

    bool isInside(const fs::path& path, const fs::path& folder)
    {
        fs::path relative = fs::path(pathKey(path)).lexically_relative(pathKey(folder));
        return !relative.empty() && *relative.begin() != ".." && relative != ".";
    }

    bool samePath(const fs::path& a, const fs::path& b)
    {
        return pathKey(a) == pathKey(b);
    }

    bool readText(const fs::path& path, std::string& text)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        text.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return true;
    }

    bool writeText(const fs::path& path, const std::string& text)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path temp = path;
        temp += ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            file << text;
            if (!file) return false;
        }
        fs::rename(temp, path, ec);
        if (ec) {
            fs::remove(temp, ec);
            return false;
        }
        return true;
    }

    std::string folderName(const std::string& text)
    {
        std::string name;
        for (unsigned char c : text) {
            name += std::isalnum(c) || c == '.' || c == '_' || c == '-' ? static_cast<char>(c) : '_';
        }
        while (!name.empty() && name.back() == '.') name.pop_back();
        return name.empty() || name[0] == '.' ? "previous" + name : name;
    }

    bool defaultRename(const fs::path& from, const fs::path& to)
    {
#ifdef _WIN32
        // No replacing and no copying: a rename within the folder, which works on a running exe
        return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(), 0) != 0;
#else
        std::error_code ec;
        fs::rename(from, to, ec);
        return !ec;
#endif
    }

    bool defaultRemove(const fs::path& file)
    {
        std::error_code ec;
        fs::remove(file, ec);
        return !fs::exists(file, ec);
    }

    // One file of the update
    struct Step
    {
        enum class Kind { Add, Replace, Remove } kind = Kind::Add;
        fs::path staged;        // the new file (Add, Replace)
        fs::path destination;
        fs::path aside;         // where the old file goes (Replace, Remove)
    };

    const char* kindName(Step::Kind kind)
    {
        return kind == Step::Kind::Add ? "add" : kind == Step::Kind::Replace ? "replace" : "remove";
    }

    struct Journal
    {
        std::string version;
        bool done = false;
        std::vector<Step> steps;
    };

    json toJson(const Journal& journal)
    {
        json steps = json::array();
        for (const Step& step : journal.steps) {
            steps.push_back({ { "kind", kindName(step.kind) }, { "staged", utf8(step.staged) },
                              { "destination", utf8(step.destination) }, { "aside", utf8(step.aside) } });
        }
        return { { "version", journal.version }, { "done", journal.done }, { "steps", steps } };
    }

    bool readJournal(const fs::path& file, Journal& journal)
    {
        std::string text;
        if (!readText(file, text)) return false;
        json document = json::parse(text, nullptr, false);
        if (!document.is_object()) return false;
        journal.version = document.value("version", "");
        journal.done = document.value("done", false);
        for (const json& entry : document.value("steps", json::array())) {
            if (!entry.is_object()) return false;
            Step step;
            std::string kind = entry.value("kind", "");
            step.kind = kind == "replace" ? Step::Kind::Replace : kind == "remove" ? Step::Kind::Remove : Step::Kind::Add;
            if (kind != "add" && kind != "replace" && kind != "remove") return false;
            step.staged = fs::u8path(entry.value("staged", ""));
            step.destination = fs::u8path(entry.value("destination", ""));
            step.aside = fs::u8path(entry.value("aside", ""));
            journal.steps.push_back(step);
        }
        return true;
    }

    std::vector<fs::path> readList(const fs::path& file)
    {
        std::vector<fs::path> paths;
        std::string text;
        if (!readText(file, text)) return paths;
        json document = json::parse(text, nullptr, false);
        if (!document.is_array()) return paths;
        for (const json& value : document) {
            if (value.is_string()) paths.push_back(fs::u8path(value.get<std::string>()));
        }
        return paths;
    }

    bool writeList(const fs::path& file, const std::vector<fs::path>& paths)
    {
        std::error_code ec;
        if (paths.empty()) {
            fs::remove(file, ec);
            return true;
        }
        json list = json::array();
        for (const fs::path& path : paths) list.push_back(utf8(path));
        return writeText(file, list.dump(2));
    }

    // A file extracted from the archive, and where it goes
    struct Planned
    {
        fs::path staged;
        fs::path destination;
        Catalog::InstalledFile file;   // target root, path relative to it
    };
}

namespace Catalog
{

std::string currentExecutable()
{
    try {
#ifdef _WIN32
        std::wstring path(32768, L'\0');
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) return "";
        path.resize(length);
        return fs::path(path).string();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string path(size, '\0');
        if (_NSGetExecutablePath(path.data(), &size) != 0) return "";
        path.resize(std::strlen(path.c_str()));
        return fs::weakly_canonical(path).string();
#else
        std::error_code ec;
        fs::path path = fs::read_symlink("/proc/self/exe", ec);
        return ec ? "" : path.string();
#endif
    }
    catch (const std::exception&) {
        return "";   // a name this system's code page can't hold
    }
}

SelfUpdater::SelfUpdater(const std::string& catalogFolder, const AppInstall& app)
    : renameFile(defaultRename)
    , removeFile(defaultRemove)
    , sleep([](int milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); })
    , catalog_(catalogFolder)
    , app_(app)
{
}

fs::path SelfUpdater::updateFolder() const
{
    return fs::path(catalog_) / "update";
}

std::string SelfUpdater::problem() const
{
    if (app_.root.empty() || app_.program.empty() || app_.executable.empty()) {
        return "RetroFE can't tell where its program files are, so it can't update itself.";
    }
    fs::path root(app_.root);
    fs::path program(app_.program);
    if (!isInside(program, root)) {
        return "RetroFE can only update itself when its program files are in a folder of their own under the RetroFE folder (retrofe\\).";
    }
    if (!isInside(app_.executable, program)) {
        return "RetroFE's program isn't in its program folder, so it can't update itself.";
    }
    if (isInside(catalog_, program) || samePath(catalog_, program)) {
        return "The catalog folder can't be inside RetroFE's program folder.";
    }
    return "";
}

bool SelfUpdater::isOurs(const fs::path& path) const
{
    // Only the program folder and shipped layouts: never collections, settings or the catalog
    return (isInside(path, app_.program) || isInside(path, fs::path(app_.root) / "layouts"))
        && !isInside(path, catalog_);
}

bool SelfUpdater::place(const fs::path& staged, const fs::path& destination, std::string& error)
{
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (renameFile(staged, destination)) {
        return true;
    }
    // Another drive: copy next to it, then rename, so the destination is never half written
    fs::path temp = destination;
    temp += ".new";
    fs::remove(temp, ec);
    fs::copy_file(staged, temp, ec);
    if (!ec && renameFile(temp, destination)) {
        fs::remove(staged, ec);
        return true;
    }
    fs::remove(temp, ec);
    error = "Could not write " + destination.string();
    return false;
}

bool SelfUpdater::apply(const Package& package, const std::string& archive, std::string& error)
{
    error = problem();
    if (!error.empty()) {
        return false;
    }
    fs::path root(app_.root);
    fs::path program(app_.program);
    fs::path folder = updateFolder();
    fs::path journalPath = folder / "journal.json";
    std::error_code ec;
    if (fs::exists(journalPath, ec)) {
        error = "An earlier update of RetroFE wasn't finished. Restart RetroFE, then try again.";
        return false;
    }

    // 1. Extract and check, before anything is touched
    fs::path staging = folder / "staged";
    fs::remove_all(staging, ec);
    std::vector<Planned> planned;
    int skipped = 0;
    {
        unzFile zip = unzOpen64(archive.c_str());
        if (!zip) {
            error = "The download isn't a zip archive.";
            return false;
        }
        auto fail = [&](const std::string& message) {
            unzClose(zip);
            fs::remove_all(staging, ec);
            error = message;
            return false;
        };
        std::vector<char> buffer(1 << 16);
        int result = unzGoToFirstFile(zip);
        for (int n = 0; result == UNZ_OK; n++, result = unzGoToNextFile(zip)) {
            unz_file_info64 info;
            char name[4096];
            if (unzGetCurrentFileInfo64(zip, &info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK) {
                return fail("The download is a damaged zip archive.");
            }
            std::string entry = name;
            std::replace(entry.begin(), entry.end(), '\\', '/');
            if (entry.empty() || entry.back() == '/') continue;
            if (!isSafeRelativePath(entry)) {
                return fail("The download contains a file that can't be installed safely: " + entry);
            }
            bool isUtf8 = (info.flag & (1 << 11)) != 0;
            size_t slash = entry.find('/');
            std::string top = slash == std::string::npos ? "" : lower(entry.substr(0, slash));
            std::string rest = slash == std::string::npos ? "" : entry.substr(slash + 1);
            auto toPath = [&](const std::string& text) { return isUtf8 ? fs::u8path(text) : fs::path(text); };

            Planned plan;
            if (top == "retrofe" && !rest.empty()) {
                plan.destination = program / toPath(rest);
            }
            else if (top == "layouts" && rest.find('/') != std::string::npos) {
                if (isExecutableName(rest)) {
                    return fail("The download has a program in a layout (" + entry + "), which RetroFE won't install.");
                }
                plan.destination = root / "layouts" / toPath(rest);
            }
            else {
                skipped++;   // e.g. a shortcut next to the folders, for new installs
                continue;
            }
            if (!isOurs(plan.destination)) {
                return fail("The download would write outside RetroFE's program and layout folders: " + entry);
            }

            plan.staged = staging / std::to_string(n);
            if (unzOpenCurrentFile(zip) != UNZ_OK) {
                return fail("Could not read " + entry + " from the download.");
            }
            fs::create_directories(staging, ec);
            std::ofstream out(plan.staged, std::ios::binary | std::ios::trunc);
            Sha256 hash;
            uint64_t size = 0;
            int read = 0;
            while ((read = unzReadCurrentFile(zip, buffer.data(), static_cast<unsigned>(buffer.size()))) > 0) {
                out.write(buffer.data(), read);
                hash.update(buffer.data(), static_cast<size_t>(read));
                size += static_cast<uint64_t>(read);
                if (stop_ && *stop_) {
                    unzCloseCurrentFile(zip);
                    return fail("Stopped");
                }
            }
            out.close();
            if (read < 0 || unzCloseCurrentFile(zip) != UNZ_OK || !out) {
                return fail("The download has a damaged file: " + entry + " (or the drive is full)");
            }
            plan.file.target = "root";
            plan.file.path = utf8(plan.destination.lexically_relative(root));
            plan.file.size = size;
            plan.file.sha256 = hash.hex();
            planned.push_back(plan);
        }
        unzClose(zip);
        if (result != UNZ_END_OF_LIST_OF_FILE) {
            fs::remove_all(staging, ec);
            error = "The download is a damaged zip archive.";
            return false;
        }
    }
    if (skipped > 0) {
        LOG_INFO("Catalog", "RetroFE update: left out " + std::to_string(skipped) + " files outside retrofe/ and layouts/");
    }

    // If two entries land in the same place, the later one wins
    {
        std::vector<Planned> unique;
        std::set<std::string> seen;
        for (auto it = planned.rbegin(); it != planned.rend(); ++it) {
            if (seen.insert(pathKey(it->destination)).second) unique.insert(unique.begin(), *it);
        }
        planned = unique;
    }
    // A RetroFE package for this system has this program in it
    fs::path executable(app_.executable);
    if (std::none_of(planned.begin(), planned.end(), [&](const Planned& p) { return samePath(p.destination, executable); })) {
        fs::remove_all(staging, ec);
        error = "The download doesn't contain retrofe/" + executable.filename().string() + ", so it isn't RetroFE for this system.";
        return false;
    }

    // 2. Work out what changes, and back up what's replaced or removed
    std::vector<Step> steps;
    std::vector<fs::path> backupsNeeded;
    std::set<std::string> newFiles;
    Manifest manifest;
    manifest.id = appPackageId;
    manifest.name = package.name.empty() ? "RetroFE" : package.name;
    manifest.kind = "app";
    manifest.version = package.version;
    manifest.source = package.source;
    for (const Planned& plan : planned) {
        newFiles.insert(pathKey(plan.destination));
        manifest.files.push_back(plan.file);
        Step step;
        step.staged = plan.staged;
        step.destination = plan.destination;
        if (fs::is_directory(plan.destination, ec)) {
            fs::remove_all(staging, ec);
            error = "Can't update " + plan.file.path + ": a folder is in the way.";
            return false;
        }
        if (fs::exists(plan.destination, ec)) {
            if (sha256File(plan.destination.string()) == plan.file.sha256) continue;   // the same already
            step.kind = Step::Kind::Replace;
        }
        steps.push_back(step);
    }
    // Files the installed version had and this one doesn't, unless they've been changed
    Manifest previous;
    std::string ignored;
    fs::path manifestPath = fs::path(catalog_) / "installed" / (std::string(appPackageId) + ".json");
    if (readManifest(manifestPath.string(), previous, ignored)) {
        for (const InstalledFile& old : previous.files) {
            fs::path path = root / fs::u8path(old.path);
            if (old.target != "root" || newFiles.count(pathKey(path)) || !isOurs(path)) continue;
            if (!fs::is_regular_file(path, ec) || sha256File(path.string()) != old.sha256) continue;
            Step step;
            step.kind = Step::Kind::Remove;
            step.destination = path;
            steps.push_back(step);
        }
    }
    // Removals first, and the program itself last: a failure part way leaves the old program in place
    std::stable_sort(steps.begin(), steps.end(), [&](const Step& a, const Step& b) {
        auto rank = [&](const Step& s) { return s.kind == Step::Kind::Remove ? 0 : samePath(s.destination, executable) ? 2 : 1; };
        return rank(a) < rank(b);
    });

    std::string backupName = folderName(app_.version);
    fs::path backups = fs::path(catalog_) / "backups" / appPackageId;
    fs::path backup = backups / backupName;
    fs::remove_all(backup, ec);
    for (Step& step : steps) {
        if (step.kind == Step::Kind::Add) continue;
        fs::path copy = backup / step.destination.lexically_relative(root);
        fs::create_directories(copy.parent_path(), ec);
        if (!fs::copy_file(step.destination, copy, fs::copy_options::overwrite_existing, ec) || ec) {
            fs::remove_all(staging, ec);
            error = "Could not back up " + step.destination.filename().string() + ", so nothing was changed.";
            return false;
        }
        // Where the old file goes while it may still be in use
        for (int n = 1; n <= 9; n++) {
            fs::path aside = step.destination;
            aside += n == 1 ? ".old" : ".old" + std::to_string(n);
            if (fs::exists(aside, ec)) removeFile(aside);   // left from an earlier update
            if (!fs::exists(aside, ec)) {
                step.aside = aside;
                break;
            }
        }
        if (step.aside.empty()) {
            fs::remove_all(staging, ec);
            error = "Too many old copies of " + step.destination.filename().string() + " are in use.";
            return false;
        }
    }

    // 3. Swap, recording first what's about to happen
    std::string message;
    if (!writeManifest((folder / "manifest.json").string(), manifest, message)) {
        fs::remove_all(staging, ec);
        error = message;
        return false;
    }
    Journal journal;
    journal.version = package.version;
    journal.steps = steps;
    if (!writeText(journalPath, toJson(journal).dump(2))) {
        fs::remove_all(staging, ec);
        error = "Could not write " + journalPath.string() + ", so nothing was changed.";
        return false;
    }
    LOG_NOTICE("Catalog", "Updating RetroFE " + app_.version + " to " + package.version + ": " + std::to_string(steps.size()) + " files change");

    for (const Step& step : steps) {
        bool ok = true;
        std::string problem;
        if (step.kind != Step::Kind::Add && !renameFile(step.destination, step.aside)) {
            ok = false;
            problem = "Could not move " + step.destination.filename().string() + " aside (is it open in another program?)";
        }
        if (ok && step.kind != Step::Kind::Remove && !place(step.staged, step.destination, problem)) {
            ok = false;
        }
        if (!ok) {
            LOG_ERROR("Catalog", "RetroFE update: " + problem + "; putting everything back");
            bool putBack = false;
            error = finishOrUndo(putBack) ? problem + ". Nothing was changed."
                                          : problem + ". Some files couldn't be put back; RetroFE tries again when it next starts.";
            deleteLeftovers(1);
            return false;
        }
    }

    // 4. Done: from here on an interruption is finished rather than undone
    journal.done = true;
    bool putBack = false;
    if (!writeText(journalPath, toJson(journal).dump(2))) {
        // The journal still says it's unfinished, so this undoes it
        LOG_ERROR("Catalog", "RetroFE update: could not mark the update finished; putting everything back");
        error = finishOrUndo(putBack) ? "Could not record the update, so nothing was changed."
                                      : "Could not record the update, and some files couldn't be put back; RetroFE tries again when it next starts.";
        deleteLeftovers(1);
        return false;
    }
    finishOrUndo(putBack);   // records the files and lists the old ones
    deleteLeftovers(1);      // those not in use go now, the rest at the next start
    for (const auto& entry : fs::directory_iterator(backups, ec)) {
        if (entry.path().filename() != backupName) fs::remove_all(entry.path(), ec);
    }
    LOG_NOTICE("Catalog", "Updated RetroFE to " + package.version + "; the files it replaced are in " + backup.string());
    return true;
}

bool SelfUpdater::recover()
{
    bool putBack = false;
    finishOrUndo(putBack);
    deleteLeftovers(removeTries);
    return putBack;
}

bool SelfUpdater::finishOrUndo(bool& putBack)
{
    fs::path folder = updateFolder();
    fs::path journalPath = folder / "journal.json";
    fs::path leftoversPath = folder / "leftovers.json";
    std::error_code ec;
    putBack = false;

    Journal journal;
    if (fs::exists(journalPath, ec)) {
        if (!readJournal(journalPath, journal)) {
            LOG_ERROR("Catalog", journalPath.string() + " is damaged; RetroFE's files may need putting back by hand from "
                      + (fs::path(catalog_) / "backups" / appPackageId).string());
            fs::rename(journalPath, folder / "journal.damaged.json", ec);
            journal = Journal();
        }
        else {
            // Only ever touch RetroFE's own files and this update's staging
            bool valid = std::all_of(journal.steps.begin(), journal.steps.end(), [&](const Step& step) {
                return isOurs(step.destination) && (step.kind == Step::Kind::Add || isOurs(step.aside))
                    && (step.kind == Step::Kind::Remove || isInside(step.staged, folder / "staged"));
            });
            if (!valid) {
                LOG_ERROR("Catalog", journalPath.string() + " names files outside RetroFE's folders; ignoring it");
                fs::rename(journalPath, folder / "journal.damaged.json", ec);
                journal = Journal();
            }
        }
    }

    std::vector<fs::path> leftovers = readList(leftoversPath);
    if (!journal.steps.empty() || fs::exists(journalPath, ec)) {
        if (journal.done) {
            // Every file is in place; record it
            std::string error;
            Manifest manifest;
            fs::path manifestPath = fs::path(catalog_) / "installed" / (std::string(appPackageId) + ".json");
            if (readManifest((folder / "manifest.json").string(), manifest, error)) {
                if (!writeManifest(manifestPath.string(), manifest, error)) LOG_ERROR("Catalog", error);
            }
            for (const Step& step : journal.steps) {
                if (step.kind != Step::Kind::Add) leftovers.push_back(step.aside);
            }
        }
        else {
            // Put back every file the update changed, last first
            bool ok = true;
            for (auto it = journal.steps.rbegin(); it != journal.steps.rend(); ++it) {
                const Step& step = *it;
                bool newPlaced = step.kind != Step::Kind::Remove && fs::exists(step.destination, ec)
                    && (step.kind == Step::Kind::Add ? !fs::exists(step.staged, ec) : fs::exists(step.aside, ec));
                if (newPlaced) {
                    // The new file: delete it, or set it aside too if it's running
                    if (!removeFile(step.destination)) {
                        fs::path junk = step.destination;
                        junk += ".failed";
                        removeFile(junk);
                        if (renameFile(step.destination, junk)) leftovers.push_back(junk);
                        else ok = false;
                    }
                    else if (step.kind == Step::Kind::Add) {
                        // And the folders made for it, if they're empty now
                        for (fs::path parent = step.destination.parent_path(); isOurs(parent / "x") && !samePath(parent, app_.program)
                             && !samePath(parent, fs::path(app_.root) / "layouts")
                             && fs::is_directory(parent, ec) && fs::is_empty(parent, ec); parent = parent.parent_path()) {
                            fs::remove(parent, ec);
                        }
                    }
                    putBack = true;
                }
                if (step.kind != Step::Kind::Add && fs::exists(step.aside, ec) && !fs::exists(step.destination, ec)) {
                    if (renameFile(step.aside, step.destination)) putBack = true;
                    else ok = false;
                }
                fs::path temp = step.destination;
                temp += ".new";
                if (step.kind != Step::Kind::Remove && fs::exists(temp, ec)) removeFile(temp);
                if (step.kind != Step::Kind::Add && !fs::exists(step.destination, ec)) ok = false;
            }
            if (!ok) {
                LOG_ERROR("Catalog", "Could not put back all of RetroFE's files after an unfinished update. The journal is "
                          + journalPath.string() + "; the previous files are in "
                          + (fs::path(catalog_) / "backups" / appPackageId).string());
                writeList(leftoversPath, leftovers);
                return false;
            }
            LOG_WARNING("Catalog", "Put back RetroFE's files after an unfinished update to " + journal.version);
        }
        fs::remove(journalPath, ec);
        fs::remove(folder / "manifest.json", ec);
        fs::remove_all(folder / "staged", ec);
    }
    writeList(leftoversPath, leftovers);
    return true;
}

void SelfUpdater::deleteLeftovers(int tries)
{
    fs::path file = updateFolder() / "leftovers.json";
    std::vector<fs::path> remaining;
    for (const fs::path& path : readList(file)) {
        // Only set-aside copies of RetroFE's own files
        std::string name = lower(path.filename().string());
        if (isOurs(path) && (name.find(".old") != std::string::npos || name.find(".failed") != std::string::npos)
            && !samePath(path, app_.executable)) {
            remaining.push_back(path);
        }
    }
    // The old RetroFE may still be closing, holding its files
    for (int round = 0; round < tries && !remaining.empty(); round++) {
        if (round > 0) sleep(250);
        std::vector<fs::path> still;
        for (const fs::path& path : remaining) {
            std::error_code ec;
            if (fs::exists(path, ec) && !removeFile(path)) still.push_back(path);
        }
        remaining = still;
    }
    writeList(file, remaining);
}

namespace
{
    std::mutex relaunchMutex;
    std::string relaunchExecutable;
}

void requestRelaunch(const std::string& executable)
{
    std::lock_guard<std::mutex> lock(relaunchMutex);
    relaunchExecutable = executable;
}

bool relaunchPending()
{
    std::lock_guard<std::mutex> lock(relaunchMutex);
    return !relaunchExecutable.empty();
}

bool takeRelaunch(std::string& executable)
{
    std::lock_guard<std::mutex> lock(relaunchMutex);
    executable = relaunchExecutable;
    relaunchExecutable.clear();
    return !executable.empty();
}

bool relaunch(const std::string& executable, char** argv, std::string& error)
{
#ifdef _WIN32
    (void)argv;
    // The same command line, run by the program file now in place
    std::wstring line = GetCommandLineW();
    std::vector<wchar_t> buffer(line.begin(), line.end());
    buffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(fs::path(executable).wstring().c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &process)) {
        error = "Could not start " + executable + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    std::vector<char*> args;
    std::string program = executable;
    args.push_back(program.data());
    for (char** arg = argv ? argv + 1 : nullptr; arg && *arg; ++arg) args.push_back(*arg);
    args.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, executable.c_str(), nullptr, nullptr, args.data(), environ) != 0) {
        error = "Could not start " + executable;
        return false;
    }
    return true;
#endif
}

}

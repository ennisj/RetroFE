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
#include "JsonFields.h"
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
#include <cstdio>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;
using nlohmann::json;
namespace J = Catalog::JsonFields;

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
        if (folder.empty()) return false;
        fs::path relative = fs::u8path(pathKey(path)).lexically_relative(fs::u8path(pathKey(folder)));
        return !relative.empty() && *relative.begin() != ".." && relative != ".";
    }

    bool samePath(const fs::path& a, const fs::path& b)
    {
        return pathKey(a) == pathKey(b);
    }

    fs::path withSuffix(fs::path path, const std::string& suffix)
    {
        path += suffix;
        return path;
    }

    // A folder path without a trailing separator
    fs::path folderPath(const fs::path& path)
    {
        fs::path result = path.lexically_normal();
        while (!result.empty() && !result.has_filename() && result.has_parent_path() && result != result.root_path()) {
            result = result.parent_path();
        }
        return result;
    }

    bool readText(const fs::path& path, std::string& text)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        text.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return true;
    }

    // Writes next to the file, makes sure it's on the disk, then swaps it in,
    // so a power cut leaves the old file or the new one, never an empty one
    bool writeText(const fs::path& path, const std::string& text)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path temp = withSuffix(path, ".tmp");
#ifdef _WIN32
        HANDLE file = CreateFileW(temp.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size()
               && FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok || !MoveFileExW(temp.wstring().c_str(), path.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            fs::remove(temp, ec);
            return false;
        }
        return true;
#else
        int file = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (file < 0) return false;
        bool ok = write(file, text.data(), text.size()) == static_cast<ssize_t>(text.size()) && fsync(file) == 0;
        close(file);
        if (!ok || std::rename(temp.c_str(), path.c_str()) != 0) {
            fs::remove(temp, ec);
            return false;
        }
        int folder = open(path.parent_path().c_str(), O_RDONLY);
        if (folder >= 0) {
            fsync(folder);
            close(folder);
        }
        return true;
#endif
    }

    // A folder that's really there: not a link or junction to somewhere else
    bool isRealFolder(const fs::path& path)
    {
        std::error_code ec;
        fs::file_status status = fs::symlink_status(path, ec);
        return !ec && status.type() == fs::file_type::directory;
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

    // What the update itself names files it sets aside: <name>.old, <name>.old2..., <name>.failed
    bool hasSetAsideSuffix(const std::string& fileName)
    {
        std::string name = lower(fileName);
        if (name.size() > 7 && name.compare(name.size() - 7, 7, ".failed") == 0) return true;
        size_t dot = name.rfind(".old");
        if (dot == std::string::npos || dot == 0) return false;
        return name.find_first_not_of("0123456789", dot + 4) == std::string::npos;
    }

    // Names an archive may not use, because updates use them for themselves
    bool isUpdateName(const std::string& fileName)
    {
        std::string name = lower(fileName);
        return hasSetAsideSuffix(name) || (name.size() > 4 && name.compare(name.size() - 4, 4, ".new") == 0)
            || (name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0);
    }

    bool defaultRename(const fs::path& from, const fs::path& to)
    {
#ifdef _WIN32
        // No replacing and no copying: a rename, which works on a running exe and its folder
        return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(), 0) != 0;
#else
        std::error_code ec;
        if (fs::exists(to, ec)) return false;
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

    bool hashIs(const fs::path& path, const std::string& sha256)
    {
        std::error_code ec;
        return !sha256.empty() && fs::is_regular_file(path, ec) && Catalog::sha256File(path) == sha256;
    }

    // Moves a file, or copies it when it has to cross drives; the copy is
    // checked, and the destination is never half written
    bool moveOrCopy(const fs::path& from, const fs::path& to, const std::string& sha256)
    {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::remove(to, ec);
        fs::rename(from, to, ec);
        if (!ec) return true;
        fs::path part = withSuffix(to, ".part");
        fs::copy_file(from, part, fs::copy_options::overwrite_existing, ec);
        if (ec || !hashIs(part, sha256)) {
            fs::remove(part, ec);
            return false;
        }
        fs::rename(part, to, ec);
        if (ec) {
            fs::remove(part, ec);
            return false;
        }
        return true;
    }

    // One file of the update
    struct Step
    {
        enum class Kind { Add, Replace, Remove } kind = Kind::Add;
        fs::path destination;
        fs::path prepared;      // <destination>.new: the new file, ready beside it (Add, Replace)
        fs::path aside;         // <destination>.old: where the old file goes (Replace, Remove)
        std::string newSha;     // the file placed
        std::string oldSha;     // the file replaced, as backed up
        std::string staged;     // where it was extracted (not journaled)
    };

    const char* kindName(Step::Kind kind)
    {
        return kind == Step::Kind::Add ? "add" : kind == Step::Kind::Replace ? "replace" : "remove";
    }

    // <root>/retrofe.update.json
    struct Journal
    {
        std::string version;    // being installed
        std::string from;       // being replaced
        std::string backup;     // where the replaced files were copied
        // preparing (files being put beside where they go), swapping, swapped (to be recorded), cleanup (only leftovers)
        std::string state = "swapping";
        int64_t attempts = 0;   // starts that tried to finish or undo it
        bool folder = false;    // the program folder is swapped whole
        fs::path newFolder;
        fs::path oldFolder;
        std::vector<Step> steps;
        std::vector<fs::path> leftovers;
    };

    json toJson(const Journal& journal)
    {
        json steps = json::array();
        for (const Step& step : journal.steps) {
            steps.push_back({ { "kind", kindName(step.kind) }, { "destination", utf8(step.destination) },
                              { "prepared", utf8(step.prepared) }, { "aside", utf8(step.aside) },
                              { "newSha256", step.newSha }, { "oldSha256", step.oldSha } });
        }
        json leftovers = json::array();
        for (const fs::path& path : journal.leftovers) leftovers.push_back(utf8(path));
        return { { "version", journal.version }, { "from", journal.from }, { "backup", journal.backup },
                 { "state", journal.state }, { "attempts", journal.attempts }, { "folder", journal.folder },
                 { "newFolder", utf8(journal.newFolder) }, { "oldFolder", utf8(journal.oldFolder) },
                 { "steps", steps }, { "leftovers", leftovers } };
    }

    bool readJournal(const fs::path& file, Journal& journal)
    {
        journal = Journal();
        std::string text;
        if (!readText(file, text)) return false;
        json document = json::parse(text, nullptr, false);
        if (!document.is_object()) return false;
        journal.version = J::text(document, "version");
        journal.from = J::text(document, "from");
        journal.backup = J::text(document, "backup");
        journal.state = J::text(document, "state");
        journal.attempts = J::integer(document, "attempts");
        journal.folder = J::flag(document, "folder");
        journal.newFolder = fs::u8path(J::text(document, "newFolder"));
        journal.oldFolder = fs::u8path(J::text(document, "oldFolder"));
        if (journal.state != "preparing" && journal.state != "swapping" && journal.state != "swapped" && journal.state != "cleanup") return false;
        for (const json& entry : J::list(document, "steps")) {
            Step step;
            std::string kind = J::text(entry, "kind");
            if (kind == "add") step.kind = Step::Kind::Add;
            else if (kind == "replace") step.kind = Step::Kind::Replace;
            else if (kind == "remove") step.kind = Step::Kind::Remove;
            else return false;
            step.destination = fs::u8path(J::text(entry, "destination"));
            step.prepared = fs::u8path(J::text(entry, "prepared"));
            step.aside = fs::u8path(J::text(entry, "aside"));
            step.newSha = J::text(entry, "newSha256");
            step.oldSha = J::text(entry, "oldSha256");
            journal.steps.push_back(step);
        }
        for (const json& value : J::list(document, "leftovers")) {
            if (value.is_string()) journal.leftovers.push_back(fs::u8path(value.get<std::string>()));
        }
        return true;
    }

    // A file extracted from the archive, and where it goes
    struct Planned
    {
        fs::path staged;
        fs::path destination;
        bool inProgram = false;
        Catalog::InstalledFile file;   // target root, path relative to it
    };

    std::mutex relaunchMutex;
    std::string relaunchExecutable;
    fs::path relaunchDirectory;
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
        return fs::path(path).u8string();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string path(size, '\0');
        if (_NSGetExecutablePath(path.data(), &size) != 0) return "";
        path.resize(std::strlen(path.c_str()));
        return fs::weakly_canonical(fs::u8path(path)).u8string();
#else
        std::error_code ec;
        fs::path path = fs::read_symlink("/proc/self/exe", ec);
        return ec ? "" : path.u8string();
#endif
    }
    catch (const std::exception&) {
        return "";
    }
}

SelfUpdater::SelfUpdater(const std::string& catalogFolder, const AppInstall& app)
    : renameFile(defaultRename)
    , removeFile(defaultRemove)
    , sleep([](int milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); })
    , log([](const std::string& level, const std::string& text) {
        if (level == "error") LOG_ERROR("Catalog", text);
        else if (level == "warning") LOG_WARNING("Catalog", text);
        else LOG_NOTICE("Catalog", text);
    })
    , catalog_(catalogFolder.empty() ? fs::path() : folderPath(fs::path(catalogFolder)))
    , root_(app.root.empty() ? fs::path() : folderPath(fs::u8path(app.root)))
    , program_(app.program.empty() ? fs::path() : folderPath(fs::u8path(app.program)))
    , executable_(app.executable.empty() ? fs::path() : fs::u8path(app.executable).lexically_normal())
    , version_(app.version)
{
}

fs::path SelfUpdater::journalPath() const
{
    return withSuffix(program_, ".update.json");
}

std::string SelfUpdater::problem() const
{
    if (root_.empty() || program_.empty() || executable_.empty()) {
        return "RetroFE can't tell where its program files are, so it can't update itself.";
    }
    if (!isProgramLayout() || !isRealFolder(program_)) {
        return "RetroFE can only update itself when its program files are in a folder of their own in the RetroFE folder (retrofe\\).";
    }
    if (!isInside(executable_, program_)) {
        return "RetroFE's program isn't in its program folder, so it can't update itself.";
    }
    if (catalog_.empty()) {
        return "RetroFE doesn't know where its catalog folder is.";
    }
    if (isInside(catalog_, program_) || samePath(catalog_, program_)) {
        return "The catalog folder can't be inside RetroFE's program folder.";
    }
    return "";
}

bool SelfUpdater::isOurs(const fs::path& path) const
{
    // Only the program folder and shipped layouts: never collections, settings or the catalog
    return (isInside(path, program_) || isInside(path, root_ / "layouts"))
        && (catalog_.empty() || !isInside(path, catalog_));
}

bool SelfUpdater::isOldFolder(const fs::path& path) const
{
    // <root>/retrofe.old, retrofe.old2...: the program folder an update replaced
    if (!samePath(path.parent_path(), root_)) return false;
    std::string name = lower(path.filename().u8string());
    std::string base = lower(program_.filename().u8string()) + ".old";
    return name.rfind(base, 0) == 0 && name.find_first_not_of("0123456789", base.size()) == std::string::npos;
}

bool SelfUpdater::isProgramLayout() const
{
    // Only <root>\retrofe\ is ever updated (or recovered): never a flat install with the exe in the root
    return !program_.empty() && lower(program_.filename().u8string()) == "retrofe" && samePath(program_.parent_path(), root_);
}

bool SelfUpdater::persist(const std::function<bool()>& attempt)
{
    for (int i = 0; i < std::max(1, renameTries); i++) {
        if (i > 0) sleep(200);
        if (attempt()) return true;
    }
    return false;
}

bool SelfUpdater::removeTree(const fs::path& folder)
{
    // Never through a link or junction: only what's really inside
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(folder, ec))) return true;
    if (!isRealFolder(folder)) return false;
    bool all = true;
    std::vector<fs::path> entries;
    for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        entries.push_back(it->path());
    }
    if (ec) return false;
    for (const fs::path& entry : entries) {
        fs::file_status status = fs::symlink_status(entry, ec);
        if (status.type() == fs::file_type::directory) all = removeTree(entry) && all;
        else if (status.type() == fs::file_type::regular) all = removeFile(entry) && all;
        else all = false;   // a link, junction or anything else stays, and so does the folder
    }
    if (all) fs::remove(folder, ec);
    return !fs::exists(fs::symlink_status(folder, ec));
}

bool SelfUpdater::programFolderMissing() const
{
    std::error_code ec;
    return !program_.empty() && !fs::exists(program_, ec);
}

bool SelfUpdater::repairProgramFolder()
{
    bool putBack = false;
    finishOrUndo(putBack, false);
    if (finishedForward_) {
        recordInstalled();
    }
    return !programFolderMissing();
}

bool SelfUpdater::apply(const Package& package, const std::string& archive, std::string& error)
{
    error = problem();
    if (!error.empty()) {
        return false;
    }
    std::error_code ec;
    fs::path folder = catalog_ / "update";
    fs::path journalFile = journalPath();
    fs::path newFolder = withSuffix(program_, ".new");

    // An earlier update has to be finished or undone first
    std::vector<fs::path> pendingLeftovers;
    if (fs::exists(journalFile, ec)) {
        bool putBack = false;
        finishOrUndo(putBack, false);   // not a start: doesn't count towards giving up
        if (!message_.empty()) {
            error = message_;   // it gave up: the user has to know where the backup is first
            return false;
        }
        recordInstalled();
        deleteLeftovers(1);
        Journal earlier;
        if (readJournal(journalFile, earlier)) {
            if (earlier.state == "swapping" || earlier.state == "preparing") {
                error = "An earlier update of RetroFE couldn't be finished or undone yet (" + std::to_string(earlier.attempts) + " of "
                      + std::to_string(maxRecoveryAttempts) + " starts tried). Restart RetroFE so it can try again. The files it replaced are in "
                      + earlier.backup + ".";
                return false;
            }
            pendingLeftovers = earlier.leftovers;
        }
    }
    auto isLeftover = [&](const fs::path& path) {
        return std::any_of(pendingLeftovers.begin(), pendingLeftovers.end(), [&](const fs::path& p) { return samePath(p, path); });
    };

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
                plan.destination = program_ / toPath(rest);
                plan.inProgram = true;
            }
            else if (top == "layouts" && rest.find('/') != std::string::npos) {
                if (isExecutableName(rest)) {
                    return fail("The download has a program in a layout (" + entry + "), which RetroFE won't install.");
                }
                plan.destination = root_ / "layouts" / toPath(rest);
            }
            else {
                skipped++;   // e.g. a shortcut next to the folders, for new installs
                continue;
            }
            if (!isOurs(plan.destination)) {
                return fail("The download would write outside RetroFE's program and layout folders: " + entry);
            }
            if (isUpdateName(plan.destination.filename().u8string())) {
                return fail("The download has a file named like the ones updates set aside: " + entry);
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
                if (stopped()) {
                    unzCloseCurrentFile(zip);
                    return fail("Stopped");
                }
            }
            out.close();
            if (read < 0 || unzCloseCurrentFile(zip) != UNZ_OK || !out) {
                return fail("The download has a damaged file: " + entry + " (or the drive is full)");
            }
            plan.file.target = "root";
            plan.file.path = utf8(plan.destination.lexically_relative(root_));
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
        log("info", "RetroFE update: left out " + std::to_string(skipped) + " files outside retrofe/ and layouts/");
    }
    {
        // If two entries land in the same place, the later one wins
        std::vector<Planned> unique;
        std::set<std::string> seen;
        for (auto it = planned.rbegin(); it != planned.rend(); ++it) {
            if (seen.insert(pathKey(it->destination)).second) unique.insert(unique.begin(), *it);
        }
        planned = unique;
    }
    // A RetroFE package for this system has this program in it
    if (std::none_of(planned.begin(), planned.end(), [&](const Planned& p) { return samePath(p.destination, executable_); })) {
        fs::remove_all(staging, ec);
        error = "The download doesn't contain retrofe/" + executable_.filename().u8string() + ", so it isn't RetroFE for this system.";
        return false;
    }

    // 2. What changes, and a backup of everything replaced or removed
    std::vector<Step> layoutSteps;
    std::vector<Step> programSteps;
    std::set<std::string> newFiles;
    std::set<std::string> staleProgram;
    Manifest manifest;
    manifest.id = appPackageId;
    manifest.name = "RetroFE";
    manifest.kind = "app";
    manifest.version = package.version;
    manifest.source = package.source;
    for (const Planned& plan : planned) {
        newFiles.insert(pathKey(plan.destination));
        manifest.files.push_back(plan.file);
        Step step;
        step.destination = plan.destination;
        step.staged = utf8(plan.staged);
        step.newSha = plan.file.sha256;
        if (fs::is_directory(plan.destination, ec)) {
            fs::remove_all(staging, ec);
            error = "Can't update " + plan.file.path + ": a folder is in the way.";
            return false;
        }
        if (fs::exists(plan.destination, ec)) {
            step.oldSha = sha256File(plan.destination);
            if (step.oldSha == step.newSha && !plan.inProgram) continue;   // the same already
            step.kind = Step::Kind::Replace;
        }
        if (plan.inProgram) {
            if (step.oldSha != step.newSha) programSteps.push_back(step);
        }
        else {
            layoutSteps.push_back(step);
        }
    }
    // Files the installed version had and this one doesn't, unless they've been changed
    Manifest previous;
    std::string ignored;
    fs::path manifestPath = catalog_ / "installed" / (std::string(appPackageId) + ".json");
    if (readManifest(manifestPath.string(), previous, ignored)) {
        for (const InstalledFile& old : previous.files) {
            fs::path path = root_ / fs::u8path(old.path);
            if (old.target != "root" || newFiles.count(pathKey(path)) || !isOurs(path)) continue;
            if (!fs::is_regular_file(path, ec) || sha256File(path) != old.sha256) continue;
            Step step;
            step.kind = Step::Kind::Remove;
            step.destination = path;
            step.oldSha = old.sha256;
            if (isInside(path, program_)) {
                programSteps.push_back(step);
                staleProgram.insert(pathKey(path));
            }
            else {
                layoutSteps.push_back(step);
            }
        }
    }
    // Removals first, and the program itself last
    std::stable_sort(programSteps.begin(), programSteps.end(), [&](const Step& a, const Step& b) {
        auto rank = [&](const Step& s) { return s.kind == Step::Kind::Remove ? 0 : samePath(s.destination, executable_) ? 2 : 1; };
        return rank(a) < rank(b);
    });

    std::string backupName = folderName(version_);
    fs::path backups = catalog_ / "backups" / appPackageId;
    fs::path backup = backups / backupName;
    fs::remove_all(backup, ec);
    for (std::vector<Step>* list : { &layoutSteps, &programSteps }) {
        for (const Step& step : *list) {
            if (step.kind == Step::Kind::Add) continue;
            fs::path copy = backup / step.destination.lexically_relative(root_);
            fs::create_directories(copy.parent_path(), ec);
            if (!fs::copy_file(step.destination, copy, fs::copy_options::overwrite_existing, ec) || ec || !hashIs(copy, step.oldSha)) {
                fs::remove_all(staging, ec);
                error = "Could not back up " + step.destination.filename().u8string() + ", so nothing was changed.";
                return false;
            }
        }
    }

    // 3. Every name the update will create, recorded before any is: an
    //    interruption from here on is cleaned up from the journal, and only
    //    names it recorded are ever deleted (anyone's own .old file is kept)
    auto chooseAside = [&](Step& step) {
        for (int n = 1; n <= 9; n++) {
            fs::path aside = withSuffix(step.destination, n == 1 ? ".old" : ".old" + std::to_string(n));
            if (fs::exists(fs::symlink_status(aside, ec)) && isLeftover(aside)) removeFile(aside);   // an earlier update's
            if (!fs::exists(fs::symlink_status(aside, ec))) {
                step.aside = aside;
                return true;
            }
        }
        return false;
    };
    auto planNames = [&](Step& step) {
        if (step.kind != Step::Kind::Remove) {
            step.prepared = withSuffix(step.destination, ".new");
            if (fs::exists(fs::symlink_status(step.prepared, ec))) {
                error = "Can't update: " + step.prepared.u8string() + " is in the way, so nothing was changed.";
                return false;
            }
        }
        if (step.kind != Step::Kind::Add && !chooseAside(step)) {
            error = "Too many old copies of " + step.destination.filename().u8string() + " are in the way, so nothing was changed.";
            return false;
        }
        return true;
    };
    for (std::vector<Step>* list : { &layoutSteps, &programSteps }) {
        for (Step& step : *list) {
            if (!planNames(step)) {
                fs::remove_all(staging, ec);
                return false;
            }
        }
    }
    bool wholeFolder = folderSwap;
    if (fs::exists(fs::symlink_status(newFolder, ec))) {
        // Not one an update recorded (those are cleaned up before): leave it alone
        log("warning", "RetroFE update: " + newFolder.u8string() + " is in the way; replacing the program's files one by one");
        wholeFolder = false;
    }
    fs::path oldFolder;
    for (int n = 1; wholeFolder && n <= 9 && oldFolder.empty(); n++) {
        fs::path old = withSuffix(program_, n == 1 ? ".old" : ".old" + std::to_string(n));
        if (fs::exists(fs::symlink_status(old, ec)) && isLeftover(old) && isRealFolder(old)) removeTree(old);
        if (!fs::exists(fs::symlink_status(old, ec))) oldFolder = old;
    }
    if (oldFolder.empty()) wholeFolder = false;

    std::string message;
    if (!writeManifest((folder / "manifest.json").string(), manifest, message)) {
        fs::remove_all(staging, ec);
        error = message;
        return false;
    }
    Journal journal;
    journal.version = package.version;
    journal.from = version_;
    journal.backup = backup.u8string();
    journal.state = "preparing";
    journal.folder = wholeFolder;
    journal.newFolder = wholeFolder ? newFolder : fs::path();
    journal.oldFolder = oldFolder;
    journal.steps = layoutSteps;
    journal.steps.insert(journal.steps.end(), programSteps.begin(), programSteps.end());
    journal.leftovers = pendingLeftovers;
    if (!writeText(journalFile, toJson(journal).dump(2))) {
        fs::remove_all(staging, ec);
        error = "Could not write " + journalFile.u8string() + ", so nothing was changed.";
        return false;
    }
    // Gives up before anything is swapped: removes what was prepared, going by the journal
    auto abandon = [&](const std::string& problem) {
        bool putBack = false;
        finishOrUndo(putBack, false);
        fs::remove_all(staging, ec);
        error = problem;
        return false;
    };

    // 4. Prepare everything beside where it goes, so the swap is renames only
    auto prepare = [&](const Step& step) {
        return step.kind == Step::Kind::Remove || moveOrCopy(fs::u8path(step.staged), step.prepared, step.newSha);
    };
    for (const Step& step : layoutSteps) {
        if (stopped()) return abandon("Stopped");
        if (!prepare(step)) return abandon("Could not prepare " + step.destination.filename().u8string() + ", so nothing was changed.");
    }
    if (wholeFolder) {
        // The whole new program folder: unchanged files linked in from the current one
        std::set<std::string> replaced;
        for (const Step& step : programSteps) replaced.insert(pathKey(step.destination));
        std::vector<std::pair<fs::path, fs::path>> folders = { { program_, newFolder } };
        while (wholeFolder && !folders.empty()) {
            auto [from, to] = folders.back();
            folders.pop_back();
            fs::create_directories(to, ec);
            for (auto it = fs::directory_iterator(from, ec); wholeFolder && !ec && it != fs::directory_iterator(); it.increment(ec)) {
                fs::path target = to / it->path().filename();
                fs::file_type type = it->symlink_status(ec).type();
                if (type == fs::file_type::directory) {
                    folders.emplace_back(it->path(), target);
                    continue;
                }
                if (type != fs::file_type::regular) {
                    wholeFolder = false;   // a link or junction can't be carried over safely
                    break;
                }
                // Everything else is carried over, anyone's own .old files included; only an earlier update's leftovers stay behind
                if (replaced.count(pathKey(it->path())) || newFiles.count(pathKey(it->path())) || isLeftover(it->path())) {
                    continue;
                }
                std::error_code linkError;
                fs::create_hard_link(it->path(), target, linkError);
                if (linkError) fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, linkError);
                if (linkError || stopped()) wholeFolder = false;
            }
            if (ec) wholeFolder = false;
        }
        for (const Planned& plan : planned) {
            if (!wholeFolder) break;
            if (plan.inProgram && !moveOrCopy(plan.staged, newFolder / plan.destination.lexically_relative(program_), plan.file.sha256)) {
                wholeFolder = false;
            }
        }
        if (!wholeFolder) {
            // Give the new files back to the staging folder, for swapping file by file
            for (const Planned& plan : planned) {
                fs::path target = newFolder / plan.destination.lexically_relative(program_);
                if (plan.inProgram && !fs::exists(plan.staged, ec) && fs::exists(target, ec) && !moveOrCopy(target, plan.staged, plan.file.sha256)) {
                    return abandon("Could not prepare RetroFE's new files, so nothing was changed.");
                }
            }
            removeTree(newFolder);
            if (stopped()) return abandon("Stopped");
        }
    }
    if (!wholeFolder) {
        for (const Step& step : programSteps) {
            if (stopped()) return abandon("Stopped");
            if (!prepare(step)) return abandon("Could not prepare RetroFE's new files, so nothing was changed.");
        }
    }

    // 5. The journal says what's about to happen, before the first rename
    journal.state = "swapping";
    journal.folder = wholeFolder;
    if (!writeText(journalFile, toJson(journal).dump(2))) {
        return abandon("Could not write " + journalFile.u8string() + ", so nothing was changed.");
    }
    fs::path note = withSuffix(program_, "-UPDATE-INTERRUPTED.txt");
    std::string programName = program_.filename().u8string();
    writeText(note, "RetroFE was updating itself from " + version_ + " to " + package.version + ".\r\n\r\n"
              "If this file is still here and RetroFE won't start, the update was interrupted. To get RetroFE back:\r\n"
              "- If there's no folder " + programName + " but there's " + programName + ".old: rename " + programName + ".old to "
              + programName + ". (Or, if there's " + programName + ".new, rename that to " + programName + " for the new version.)\r\n"
              "- If there's a folder " + programName + ".old next to " + programName + ": delete " + programName
              + ", then rename " + programName + ".old to " + programName + ".\r\n"
              "- Otherwise copy everything in " + backup.u8string() + " over " + root_.u8string() + ".\r\n\r\n"
              "RetroFE deletes this file once the update is finished or undone.\r\n");

    // The program folder can't be renamed while it's the working folder; the
    // restarted RetroFE gets the original one back
    fs::path workingFolder = fs::current_path(ec);
    if (wholeFolder && !ec && (samePath(workingFolder, program_) || isInside(workingFolder, program_))) {
        std::lock_guard<std::mutex> lock(relaunchMutex);
        if (relaunchDirectory.empty()) relaunchDirectory = workingFolder;
        fs::current_path(root_, ec);
    }

    // 6. Swap: renames only, layouts first, the program folder last
    auto undo = [&](const std::string& problem) {
        log("error", "RetroFE update: " + problem + "; putting everything back");
        bool putBack = false;
        bool ok = finishOrUndo(putBack, false);
        deleteLeftovers(1);
        fs::remove_all(staging, ec);
        error = ok ? problem + ". Nothing was changed."
                   : problem + ". Some files couldn't be put back yet; RetroFE tries again when it starts. The files it replaced are in "
                     + backup.u8string() + ".";
        return false;
    };
    // Each rename is retried too: a virus scanner can hold a file it's just seen for a moment
    auto swap = [&](const Step& step) {
        if (step.kind != Step::Kind::Add && !persist([&] { return renameFile(step.destination, step.aside); })) {
            return "Could not move " + step.destination.filename().u8string() + " aside (is it open in another program?)";
        }
        if (step.kind != Step::Kind::Remove && !persist([&] { return renameFile(step.prepared, step.destination); })) {
            return "Could not put the new " + step.destination.filename().u8string() + " in place";
        }
        return std::string();
    };
    for (const Step& step : layoutSteps) {
        std::string problem = swap(step);
        if (!problem.empty()) return undo(problem);
    }
    bool committed = false;
    if (wholeFolder) {
        if (persist([&] { return renameFile(program_, oldFolder); })) {
            // A virus scanner looking at the new files can hold up the next rename for a while: keep trying,
            // then put the old folder back, and failing that, the complete new one after all
            committed = persist([&] { return renameFile(newFolder, program_); });
            bool restored = !committed && persist([&] { return renameFile(oldFolder, program_); });
            if (!committed && !restored) committed = persist([&] { return renameFile(newFolder, program_); });
            if (!committed && !restored) {
                // No program folder at all: the Catalog keeps trying, and so does RetroFE as it exits and starts
                fs::remove_all(staging, ec);
                error = "RetroFE's program folder couldn't be put back after the update failed (another program may be using its files). "
                        "RetroFE keeps trying. If RetroFE won't start afterwards, follow " + note.u8string() + ".";
                log("error", "RetroFE update: " + error);
                return false;
            }
        }
        if (!committed) {
            // Something has a file in the program folder open: swap file by file instead.
            // The names are in the journal already, so an interruption is cleaned up.
            log("warning", "RetroFE update: the program folder couldn't be renamed; replacing its files one by one");
            for (const Step& step : programSteps) {
                if (step.kind == Step::Kind::Remove) continue;
                fs::create_directories(step.prepared.parent_path(), ec);
                fs::rename(newFolder / step.destination.lexically_relative(program_), step.prepared, ec);
                if (ec) return undo("Could not prepare RetroFE's new files");
            }
            journal.folder = wholeFolder = false;
            if (!writeText(journalFile, toJson(journal).dump(2))) return undo("Could not write " + journalFile.u8string());
            removeTree(newFolder);   // what's left is linked to the running files; anything still held goes later
        }
    }
    if (!committed) {
        for (const Step& step : programSteps) {
            std::string problem = swap(step);
            if (!problem.empty()) return undo(problem);
        }
    }

    // 7. Done: record it, and clear away what was set aside
    journal.state = "swapped";
    if (wholeFolder) journal.leftovers.push_back(oldFolder);
    else if (fs::exists(fs::symlink_status(newFolder, ec)) && !journal.newFolder.empty()) journal.leftovers.push_back(newFolder);
    for (const Step& step : journal.steps) {
        // Only what this update really set aside
        if (step.kind != Step::Kind::Add && hashIs(step.aside, step.oldSha)) journal.leftovers.push_back(step.aside);
    }
    if (!writeText(journalFile, toJson(journal).dump(2))) {
        if (!wholeFolder) return undo("Could not record that the update finished");
        // The folder swap is the commit point: the next start finishes it
        log("warning", "RetroFE update: could not mark the update finished; RetroFE finishes it when it starts");
    }
    fs::remove(note, ec);
    fs::remove(withSuffix(program_, ".update.damaged.txt"), ec);
    recordInstalled();
    deleteLeftovers(1);   // what isn't in use goes now, the rest at the next start
    fs::remove_all(staging, ec);
    for (const auto& entry : fs::directory_iterator(backups, ec)) {
        if (entry.path().filename() != backupName) fs::remove_all(entry.path(), ec);
    }
    log("notice", "Updated RetroFE " + version_ + " to " + package.version + (wholeFolder ? " (program folder swapped)" : " (files swapped)")
                  + "; the files it replaced are in " + backup.u8string());
    return true;
}

bool SelfUpdater::recover()
{
    bool putBack = false;
    finishOrUndo(putBack, false);   // main() has had its try at this start already
    recordInstalled();
    deleteLeftovers(removeTries);
    return putBack;
}

bool SelfUpdater::recoverAtStartup()
{
    bool putBack = false;
    finishOrUndo(putBack, true);
    deleteLeftovers(1);   // no waiting: the Catalog tries again in the background
    return putBack;
}

bool SelfUpdater::finishOrUndo(bool& putBack, bool countAttempt)
{
    putBack = false;
    finishedForward_ = false;
    message_.clear();
    std::error_code ec;
    fs::path file = journalPath();
    if (!isProgramLayout() || !fs::exists(file, ec)) {
        return true;
    }
    Journal journal;
    bool readable = readJournal(file, journal);
    fs::path backupFolder = !journal.backup.empty() ? fs::u8path(journal.backup)
                          : catalog_.empty() ? fs::path("catalog/backups/retrofe") : catalog_ / "backups" / appPackageId;
    // Sets the journal aside; only when RetroFE may really be broken is the user told to restore the backup
    auto giveUp = [&](const std::string& reason, bool adviseRestore) {
        fs::path damaged = withSuffix(program_, ".update.damaged.json");
        fs::remove(damaged, ec);
        fs::rename(file, damaged, ec);
        if (adviseRestore) {
            message_ = reason + " The files the update replaced are in " + backupFolder.u8string() + "; if RetroFE doesn't work properly, "
                     "copy them over " + root_.u8string() + " to get the previous version back. (The update's record is now "
                     + damaged.u8string() + ".)";
            writeText(withSuffix(program_, ".update.damaged.txt"), message_);
            log("error", message_);
        }
        else {
            log("warning", reason + " RetroFE started, so nothing needs doing. (It's now " + damaged.u8string() + ".)");
        }
        return false;
    };
    if (!readable) {
        // A record cut short by a power cut, say. The program folder is there and running, so it's no reason to restore anything.
        return giveUp("RetroFE's update record " + file.u8string() + " couldn't be read and was set aside.", !fs::exists(program_, ec));
    }

    // Only ever RetroFE's own files, under the names updates give them, and never through a link
    fs::path newFolder = withSuffix(program_, ".new");
    bool valid = std::all_of(journal.steps.begin(), journal.steps.end(), [&](const Step& step) {
        bool placed = step.kind != Step::Kind::Remove;
        bool replaces = step.kind != Step::Kind::Add;
        return isOurs(step.destination)
            && (!placed || (samePath(step.prepared, withSuffix(step.destination, ".new")) && !step.newSha.empty()))
            && (!replaces || (isOurs(step.aside) && samePath(step.aside.parent_path(), step.destination.parent_path())
                              && hasSetAsideSuffix(step.aside.filename().u8string()) && !step.oldSha.empty()));
    });
    auto folderOk = [&](const fs::path& path) { return !fs::exists(fs::symlink_status(path, ec)) || isRealFolder(path); };
    if (!journal.newFolder.empty()) valid = valid && samePath(journal.newFolder, newFolder) && folderOk(newFolder);
    if (journal.folder) valid = valid && !journal.newFolder.empty() && isOldFolder(journal.oldFolder) && folderOk(journal.oldFolder);
    if (!valid) {
        return giveUp("RetroFE's update record " + file.u8string() + " names files outside RetroFE's folders, so it was ignored.", false);
    }
    if (journal.state == "swapped" || journal.state == "cleanup") {
        return true;   // finished: recordInstalled() and deleteLeftovers() do the rest
    }

    // Deletes a file the update placed, or sets it aside if it's running
    auto discard = [&](const fs::path& path) {
        if (removeFile(path)) return true;
        fs::path junk = withSuffix(path, ".failed");
        if (fs::exists(fs::symlink_status(junk, ec))) return false;
        if (!renameFile(path, junk)) return false;
        journal.leftovers.push_back(junk);
        return true;
    };
    // The new folder, only ever as recorded: deleted, or set aside for later if it's linked to running files
    auto discardNewFolder = [&] {
        if (journal.newFolder.empty() || !isRealFolder(newFolder) || removeTree(newFolder)) return;
        for (int n = 2; n < 10; n++) {
            fs::path aside = withSuffix(program_, ".old" + std::to_string(n));
            if (!fs::exists(fs::symlink_status(aside, ec)) && renameFile(newFolder, aside)) {
                journal.leftovers.push_back(aside);
                return;
            }
        }
    };
    auto finishUndone = [&](const std::string& logLine) {
        journal.state = "cleanup";
        journal.steps.clear();
        if (journal.leftovers.empty()) fs::remove(file, ec);
        else writeText(file, toJson(journal).dump(2));
        fs::remove(withSuffix(program_, "-UPDATE-INTERRUPTED.txt"), ec);
        if (!catalog_.empty()) fs::remove(catalog_ / "update" / "manifest.json", ec);
        if (!logLine.empty()) log("warning", logLine);
        return true;
    };

    if (journal.state == "preparing") {
        // Stopped before anything was swapped: only what was being prepared goes
        for (const Step& step : journal.steps) {
            if (step.kind == Step::Kind::Remove) continue;
            if (hashIs(step.prepared, step.newSha)) removeFile(step.prepared);
            fs::path part = withSuffix(step.prepared, ".part");
            if (fs::is_regular_file(fs::symlink_status(part, ec))) removeFile(part);
        }
        discardNewFolder();
        return finishUndone("");
    }

    // Swapping. Only starts count towards giving up: Install doesn't use the tries up.
    if (countAttempt) {
        journal.attempts++;
        if (journal.attempts > maxRecoveryAttempts) {
            return giveUp("RetroFE tried " + std::to_string(maxRecoveryAttempts) + " times to finish or undo an interrupted update and couldn't.", true);
        }
        writeText(file, toJson(journal).dump(2));
    }

    bool programSwapped = false;
    if (journal.folder) {
        bool oldExists = isRealFolder(journal.oldFolder);
        bool programExists = fs::exists(program_, ec);
        if (oldExists && programExists) {
            programSwapped = true;
        }
        else if (oldExists) {
            // Stopped between the two folder renames: the old folder back, or else the complete new one forward
            if (persist([&] { return renameFile(journal.oldFolder, program_); })) {
                putBack = true;
            }
            else if (isRealFolder(newFolder) && persist([&] { return renameFile(newFolder, program_); })) {
                programSwapped = true;
            }
            else {
                log("error", "Could not put a program folder back in place; see " + withSuffix(program_, "-UPDATE-INTERRUPTED.txt").u8string());
                return false;
            }
        }
    }

    if (programSwapped) {
        // The new program folder is in place, which is the commit point: finish
        bool ok = true;
        for (const Step& step : journal.steps) {
            if (isInside(step.destination, program_)) continue;   // the folder swap took care of those
            if (step.kind != Step::Kind::Add && hashIs(step.destination, step.oldSha)
                && (step.kind == Step::Kind::Remove || hashIs(step.prepared, step.newSha))) {
                ok = renameFile(step.destination, step.aside) && ok;
            }
            if (step.kind != Step::Kind::Remove && !fs::exists(step.destination, ec) && hashIs(step.prepared, step.newSha)) {
                ok = renameFile(step.prepared, step.destination) && ok;
            }
            if (step.kind != Step::Kind::Add && hashIs(step.aside, step.oldSha)) journal.leftovers.push_back(step.aside);
        }
        if (!ok) return false;
        journal.state = "swapped";
        journal.leftovers.push_back(journal.oldFolder);
        writeText(file, toJson(journal).dump(2));
        fs::remove(withSuffix(program_, "-UPDATE-INTERRUPTED.txt"), ec);
        finishedForward_ = true;
        log("notice", "Finished the interrupted update of RetroFE to " + journal.version);
        return true;
    }

    // Undo, last step first, touching only files that are what the journal says
    bool ok = true;
    for (auto it = journal.steps.rbegin(); it != journal.steps.rend(); ++it) {
        const Step& step = *it;
        bool inProgram = isInside(step.destination, program_);
        if (step.kind != Step::Kind::Remove) {
            if (fs::exists(step.prepared, ec)) {
                // Never moved in
                if (hashIs(step.prepared, step.newSha)) removeFile(step.prepared);
            }
            else if (hashIs(step.destination, step.newSha)
                     && (step.kind == Step::Kind::Add || hashIs(step.aside, step.oldSha))) {
                if (!discard(step.destination)) ok = false;
                else if (inProgram) putBack = true;
            }
        }
        if (step.kind != Step::Kind::Add) {
            if (!fs::exists(step.destination, ec) && hashIs(step.aside, step.oldSha)) {
                if (renameFile(step.aside, step.destination)) {
                    if (inProgram) putBack = true;
                }
                else {
                    ok = false;
                }
            }
            if (!fs::exists(step.destination, ec)) ok = false;
        }
    }
    discardNewFolder();
    if (!ok) {
        writeText(file, toJson(journal).dump(2));
        log("error", "Could not put back all of RetroFE's files after an unfinished update (" + std::to_string(journal.attempts)
                     + " of " + std::to_string(maxRecoveryAttempts) + " starts tried). The previous files are in " + journal.backup);
        return false;
    }
    return finishUndone("Put back RetroFE's files after an unfinished update to " + journal.version);
}

void SelfUpdater::recordInstalled()
{
    std::error_code ec;
    Journal journal;
    if (catalog_.empty() || !readJournal(journalPath(), journal) || journal.state != "swapped") return;
    fs::path pending = catalog_ / "update" / "manifest.json";
    Manifest manifest;
    std::string error;
    if (readManifest(pending.string(), manifest, error) && manifest.version == journal.version) {
        if (!writeManifest((catalog_ / "installed" / (std::string(appPackageId) + ".json")).string(), manifest, error)) {
            log("error", error);
            return;
        }
        fs::remove(pending, ec);
    }
    journal.state = "cleanup";
    if (journal.leftovers.empty()) fs::remove(journalPath(), ec);
    else writeText(journalPath(), toJson(journal).dump(2));
}

void SelfUpdater::deleteLeftovers(int tries)
{
    std::error_code ec;
    Journal journal;
    if (!isProgramLayout() || !readJournal(journalPath(), journal) || journal.state == "swapping" || journal.state == "preparing") return;

    // Only what updates set aside, as the journal lists it: the replaced
    // program folder (or an unused new one), and files named <name>.old, .oldN or .failed
    fs::path newFolder = withSuffix(program_, ".new");
    std::vector<fs::path> remaining;
    for (const fs::path& path : journal.leftovers) {
        if (isOldFolder(path) || samePath(path, newFolder) || (isOurs(path) && hasSetAsideSuffix(path.filename().u8string()))) {
            remaining.push_back(path);
        }
        else {
            log("warning", "Not deleting " + path.u8string() + ": updates don't set aside files there");
        }
    }
    auto removeOne = [&](const fs::path& path) {
        fs::file_type type = fs::symlink_status(path, ec).type();
        if (type == fs::file_type::not_found) return true;
        if (type == fs::file_type::directory) return removeTree(path);
        if (type == fs::file_type::regular) return removeFile(path);
        return true;   // a link or junction: never followed or deleted; forgotten
    };
    // The old RetroFE may still be closing, holding its files
    for (int round = 0; round < tries && !remaining.empty(); round++) {
        if (round > 0) sleep(250);
        std::vector<fs::path> still;
        for (const fs::path& path : remaining) {
            if (!removeOne(path)) still.push_back(path);
        }
        remaining = still;
    }
    journal.leftovers = remaining;
    if (journal.state == "cleanup" && remaining.empty()) fs::remove(journalPath(), ec);
    else writeText(journalPath(), toJson(journal).dump(2));
}

namespace
{
    AppInstall appAt(const std::string& executable)
    {
        fs::path program = fs::u8path(executable).lexically_normal().parent_path();
        AppInstall app;
        app.program = program.u8string();
        app.root = program.parent_path().u8string();
        app.executable = executable;
        return app;
    }
}

StartupRecovery recoverAtStart(const std::string& executable)
{
    StartupRecovery result;
    try {
        if (executable.empty()) return result;
        AppInstall app = appAt(executable);
        std::error_code ec;
        if (!fs::exists(withSuffix(folderPath(fs::u8path(app.program)), ".update.json"), ec)) return result;
        SelfUpdater updater("", app);
        updater.log = [&](const std::string& level, const std::string& text) { result.log.emplace_back(level, text); };
        result.restart = updater.recoverAtStartup();
        result.message = updater.message();
    }
    catch (const std::exception& e) {
        result.log.emplace_back("error", std::string("Checking for an interrupted update failed: ") + e.what());
    }
    return result;
}

std::string checkProgramFolderAtExit(const std::string& executable)
{
    try {
        if (executable.empty()) return "";
        AppInstall app = appAt(executable);
        SelfUpdater updater("", app);
        if (!updater.programFolderMissing() || updater.repairProgramFolder()) return "";
        fs::path program = fs::u8path(app.program);
        return "An update of RetroFE couldn't finish, and its program folder " + program.u8string() + " is missing. "
               "RetroFE tries to fix this when it can start again; until then, follow the instructions in "
               + withSuffix(program, "-UPDATE-INTERRUPTED.txt").u8string() + ": rename " + program.filename().u8string()
               + ".old (the previous version) or " + program.filename().u8string() + ".new (the new one) to "
               + program.filename().u8string() + ".";
    }
    catch (const std::exception&) {
        return "";
    }
}

void showMessage(const std::string& title, const std::string& text, int timeoutSeconds)
{
#ifdef _WIN32
    std::wstring wideTitle = fs::u8path(title).wstring();
    std::wstring wideText = fs::u8path(text).wstring();
    UINT style = MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND;
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (!user32) return;
    // MessageBoxTimeoutW has been in user32 since Windows XP, though it isn't documented
    using Timeout = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT, WORD, DWORD);
    using Plain = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
    auto timed = reinterpret_cast<Timeout>(reinterpret_cast<void*>(GetProcAddress(user32, "MessageBoxTimeoutW")));
    auto plain = reinterpret_cast<Plain>(reinterpret_cast<void*>(GetProcAddress(user32, "MessageBoxW")));
    if (timeoutSeconds > 0 && timed) {
        timed(nullptr, wideText.c_str(), wideTitle.c_str(), style, 0, static_cast<DWORD>(timeoutSeconds) * 1000);
    }
    else if (plain) {
        plain(nullptr, wideText.c_str(), wideTitle.c_str(), style);
    }
    FreeLibrary(user32);
#else
    (void)timeoutSeconds;
    fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
#endif
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
    fs::path directory;
    {
        std::lock_guard<std::mutex> lock(relaunchMutex);
        directory = relaunchDirectory;
    }
    std::error_code ec;
    if (!directory.empty() && !fs::is_directory(directory, ec)) directory.clear();
#ifdef _WIN32
    (void)argv;
    // The same command line, run by the program file now in place
    std::wstring line = GetCommandLineW();
    std::vector<wchar_t> buffer(line.begin(), line.end());
    buffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring folder = directory.wstring();
    if (!CreateProcessW(fs::u8path(executable).wstring().c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        folder.empty() ? nullptr : folder.c_str(), &startup, &process)) {
        error = "Could not start " + executable + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    if (!directory.empty()) fs::current_path(directory, ec);
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

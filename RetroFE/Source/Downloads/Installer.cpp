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
#include "Installer.h"
#include "Archive.h"
#include "Emulators.h"
#include "Hash.h"
#include "JsonFields.h"
#include "../Utility/Log.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>

namespace fs = std::filesystem;
using nlohmann::json;
namespace J = Downloads::JsonFields;

namespace
{
    // What staging records for each file: where it is now and where it goes
    struct PlannedFile
    {
        std::string staged;   // under the staging folder, UTF-8
        Downloads::InstalledFile file;
    };

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string utf8(const fs::path& path)
    {
        return path.generic_u8string();
    }

    fs::path fromUtf8(const std::string& path)
    {
        return fs::u8path(path);
    }

    std::string joinPath(const std::string& folder, const std::string& name)
    {
        if (folder.empty()) return name;
        std::string base = folder;
        std::replace(base.begin(), base.end(), '\\', '/');
        while (!base.empty() && base.back() == '/') base.pop_back();
        return base.empty() ? name : base + "/" + name;
    }

    // Identifies a destination whatever the case of its path
    std::string fileKey(const Downloads::InstalledFile& file)
    {
        return lower(file.path);
    }

    bool readText(const fs::path& path, std::string& text)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        text.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return true;
    }

    // Write next to the file, then swap it in, so a crash can't leave half of it
    bool writeText(const fs::path& path, const std::string& text, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path temp = path;
        temp += ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            file << text;
            if (!file) {
                error = "Could not write " + path.string();
                return false;
            }
        }
        fs::rename(temp, path, ec);
        if (ec) {
            fs::remove(temp, ec);
            error = "Could not replace " + path.string();
            return false;
        }
        return true;
    }

    // Moves a file, copying when it has to cross drives
    bool moveFile(const fs::path& from, const fs::path& to, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        if (!ec) return true;
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Could not write " + to.string() + ": " + ec.message();
            return false;
        }
        fs::remove(from, ec);
        return true;
    }

    bool copyOver(const fs::path& from, const fs::path& to)
    {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        return fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec) && !ec;
    }

    // Remove folders a removal emptied, up to but not including emulators/
    void removeEmptyFolders(fs::path folder, const fs::path& stopAt)
    {
        std::error_code ec;
        fs::path stop = stopAt.lexically_normal();
        while (!folder.empty() && folder.lexically_normal() != stop && fs::is_directory(folder, ec) && fs::is_empty(folder, ec)) {
            fs::remove(folder, ec);
            folder = folder.parent_path();
        }
    }

    bool isInside(const fs::path& path, const fs::path& folder)
    {
        fs::path relative = path.lexically_normal().lexically_relative(folder.lexically_normal());
        return !relative.empty() && *relative.begin() != ".." && *relative.begin() != ".";
    }

    // The file name a package file was published under
    std::string publishedName(const std::string& url)
    {
        std::string name = url.substr(0, url.find_first_of("?#"));
        size_t slash = name.find_last_of("/\\");
        return slash == std::string::npos ? name : name.substr(slash + 1);
    }

    // A name for a backup folder from a version, which could contain anything
    std::string folderName(const std::string& text)
    {
        std::string name;
        for (unsigned char c : text) {
            name += std::isalnum(c) || c == '.' || c == '_' || c == '-' ? static_cast<char>(c) : '_';
        }
        while (!name.empty() && name.back() == '.') name.pop_back();
        return name.empty() || name[0] == '.' ? "_" + name : name;
    }

    // Whether a folder has anything in it
    bool hasContent(const fs::path& folder)
    {
        std::error_code ec;
        return fs::exists(folder, ec) && (!fs::is_directory(folder, ec) || !fs::is_empty(folder, ec));
    }

    bool extractArchive(const std::string& path, const fs::path& stagingRoot, const fs::path& into,
                        const Downloads::PackageFile& source, std::vector<PlannedFile>& planned,
                        const std::atomic<bool>* stop, const std::string& topFolder, std::string& error)
    {
        Downloads::Archive archive;
        if (!archive.open(path, publishedName(source.url), error)) return false;
        for (size_t i = 0; i < archive.entries().size(); ++i) {
            if (stop && *stop) { error = "Stopped"; return false; }
            const auto& entry = archive.entries()[i];
            std::string name = entry.name;
            if (entry.directory && !name.empty() && name.back() == '/') name.pop_back();
            // Every name, before anything else: no name may reach outside the folder
            if (name.empty() || !Downloads::isSafeRelativePath(name)) {
                error = publishedName(source.url) + " contains a file that can't be installed safely: " + name;
                return false;
            }
            if (!topFolder.empty()) {
                if (entry.directory && name + "/" == topFolder) continue;
                if (name.rfind(topFolder, 0) != 0) { error = "Archive entry is outside " + topFolder + ": " + name; return false; }
                name.erase(0, topFolder.size());
                if (name.empty() || !Downloads::isSafeRelativePath(name)) { error = "Invalid archive entry"; return false; }
            }
            if (entry.directory) continue;
            fs::path entryPath = fs::u8path(name);
            fs::path out = into / entryPath;
            std::error_code ec;
            fs::create_directories(out.parent_path(), ec);
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            Downloads::Sha256 hash;
            Downloads::Crc32 crc;
            uint64_t size = 0;
            if (!archive.extract(i, [&](const void* data, size_t count) {
                file.write(static_cast<const char*>(data), static_cast<std::streamsize>(count));
                hash.update(data, count);
                crc.update(data, count);
                size += count;
                return static_cast<bool>(file);
            }, stop, error)) return false;
            file.close();
            if (!file || size != entry.size) { error = "Damaged file " + name + " in " + publishedName(source.url); return false; }
            // A core's CRC-32 in libretro's list is the unpacked DLL's
            if (source.crc32 && crc.value() != *source.crc32) {
                error = "The core doesn't match libretro's list";
                return false;
            }
            PlannedFile plannedFile;
            plannedFile.staged = utf8(out.lexically_relative(stagingRoot));
            plannedFile.file.path = joinPath(source.folder, utf8(entryPath));
            plannedFile.file.size = size;
            plannedFile.file.sha256 = hash.hex();
            planned.push_back(plannedFile);
        }
        return true;
    }

    json toJson(const Downloads::InstalledFile& file)
    {
        json value = { { "path", file.path }, { "size", file.size }, { "sha256", file.sha256 } };
        if (!file.backup.empty()) {
            value["backup"] = file.backup;
        }
        if (file.portable) value["portable"] = true;
        return value;
    }

    bool fromJson(const json& value, Downloads::InstalledFile& file)
    {
        if (!value.is_object()) return false;
        file.path = J::text(value, "path");
        file.size = 0;
        J::count(value, "size", file.size);
        file.sha256 = J::text(value, "sha256");
        file.backup = J::text(value, "backup");
        file.portable = J::flag(value, "portable");
        return !file.path.empty() && Downloads::isSafeRelativePath(file.path)
            && (file.backup.empty() || folderName(file.backup) == file.backup);
    }

    // Written before the first file is moved: which destinations held a file
    // already, and the backups/<id>/ folder those files were copied to
    struct Journal
    {
        std::string backup;
        std::set<std::string> existing;   // fileKey()s
    };

    bool readJournal(const fs::path& path, Journal& journal)
    {
        std::string text;
        if (!readText(path, text)) return false;
        json document = json::parse(text, nullptr, false);
        if (!document.is_object()) return false;
        journal.backup = J::text(document, "backup");
        for (const json& key : J::list(document, "existing")) {
            if (key.is_string()) journal.existing.insert(key.get<std::string>());
        }
        return !journal.backup.empty() && folderName(journal.backup) == journal.backup;
    }
}

namespace Downloads
{

bool readManifest(const std::string& file, Manifest& manifest, std::string& error)
{
    std::string text;
    if (!readText(fs::u8path(file), text)) {
        error = "Could not read " + file;
        return false;
    }
    json document = json::parse(text, nullptr, false);
    if (!document.is_object()) {
        error = file + " isn't valid JSON";
        return false;
    }
    manifest = Manifest();
    manifest.id = J::text(document, "id");
    manifest.name = J::text(document, "name");
    manifest.kind = J::text(document, "kind");
    manifest.version = J::text(document, "version");
    manifest.launcherPath = J::text(document, "launcherPath");
    manifest.launcherSha256 = J::text(document, "launcherSha256");
    manifest.launcherName = J::text(document, "launcherName");
    manifest.removed = J::flag(document, "removed");
    for (const json& value : J::list(document, "files")) {
        InstalledFile installedFile;
        if (fromJson(value, installedFile)) {
            manifest.files.push_back(installedFile);
        }
    }
    if (!isValidId(manifest.id)) {
        error = file + " has no valid package id";
        return false;
    }
    return true;
}

bool writeManifest(const std::string& file, const Manifest& manifest, std::string& error)
{
    json files = json::array();
    for (const InstalledFile& installedFile : manifest.files) {
        files.push_back(toJson(installedFile));
    }
    json document = { { "id", manifest.id }, { "name", manifest.name }, { "kind", manifest.kind },
                      { "version", manifest.version }, { "files", files } };
    document["launcherPath"] = manifest.launcherPath;
    document["launcherSha256"] = manifest.launcherSha256;
    document["launcherName"] = manifest.launcherName;
    if (manifest.removed) document["removed"] = true;
    return writeText(fs::u8path(file), document.dump(2), error);
}

Installer::Installer(const std::string& downloadsFolder, const std::string& emulatorsFolder)
    : folder_(downloadsFolder)
    , emulators_(emulatorsFolder)
{
    // Its own records are never package content
    protectedFolders_.push_back(downloadsFolder);
}

std::string Installer::manifestPath(const std::string& id) const
{
    return (fs::path(folder_) / "installed" / (id + ".json")).string();
}

std::string Installer::refusal(const InstalledFile& file, const std::string& id, const std::string& kind) const
{
    fs::path root(emulators_);
    fs::path destination = root / fromUtf8(file.path);
    if (emulators_.empty() || file.path.empty() || !isSafeRelativePath(file.path) || !isInside(destination, root)) {
        return "Refusing to write outside emulators: " + file.path;
    }
    // Only the built-in list's own code decides these, never the package's files
    const Emulator* entry = kind == "emulator" ? findEmulator(id) : nullptr;
    if (entry) {
        if (file.path.rfind(entry->folder + "/", 0) != 0) {
            return entry->name + " may only write in emulators/" + entry->folder + ": " + file.path;
        }
    }
    else if (kind == "core" && isCoreId(id)) {
        if (file.path != "RetroArch/cores/" + id.substr(5) + "_libretro.dll") return "Cores may only write their own RetroArch core DLL";
    }
    else {
        return "Only emulators of RetroFE's built-in list may install programs";
    }
    for (const std::string& folder : protectedFolders_) {
        if (!folder.empty() && (isInside(destination, folder) || destination.lexically_normal() == fs::path(folder).lexically_normal())) {
            return "Refusing to write into " + fs::path(folder).filename().string() + ": " + file.path;
        }
    }
    return "";
}

std::map<std::string, Manifest> Installer::installed() const
{
    std::map<std::string, Manifest> result;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::path(folder_) / "installed", ec)) {
        if (entry.path().extension() != ".json") continue;
        Manifest manifest;
        std::string error;
        if (!readManifest(entry.path().string(), manifest, error)) {
            LOG_WARNING("Downloads", error);
            continue;
        }
        if (!manifest.removed && entry.path().stem().string() == manifest.id) {
            result[manifest.id] = manifest;
        }
    }
    return result;
}

std::string Installer::folderRefusal(const Package& package) const
{
    if (const Emulator* entry = package.kind == "emulator" ? findEmulator(package.id) : nullptr) {
        // The folder must be this module's: one it installed, or one it removed and left settings in
        Manifest old;
        std::string ignored;
        bool owns = readManifest(manifestPath(package.id), old, ignored)
            && (old.removed || std::any_of(old.files.begin(), old.files.end(), [&](const InstalledFile& file) {
                return file.path.rfind(entry->folder + "/", 0) == 0;
            }));
        if (!owns && hasContent(fs::path(emulators_) / fromUtf8(entry->folder))) {
            return "emulators/" + entry->folder + " is already there. Rename it, or add a launcher for it instead.";
        }
        return "";
    }
    if (package.kind == "core" && !installed().count("emulator-retroarch")) {
        return "Install RetroArch first";
    }
    return "";
}

bool Installer::install(const Package& package, const std::vector<std::string>& downloads, std::string& error)
{
    error.clear();
    installMessage_.clear();
    if (!isValidId(package.id)) {
        error = "Invalid package id";
        return false;
    }
    if (downloads.size() != package.files.size()) {
        error = "Expected " + std::to_string(package.files.size()) + " downloaded files";
        return false;
    }
    error = checkPackage(package);
    if (!error.empty()) return false;

    error = folderRefusal(package);
    if (!error.empty()) return false;

    std::string staging = (fs::path(folder_) / "staging" / package.id).string();
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (!stage(package, downloads, staging, error)) {
        fs::remove_all(staging, ec);
        return false;
    }

    // From here on an interruption is finished by recover()
    if (!writeText(fs::path(staging) / ".ready", "", error)) {
        fs::remove_all(staging, ec);
        return false;
    }
    if (!commit(staging, error)) {
        fs::remove_all(staging, ec);
        return false;
    }
    return true;
}

bool Installer::stage(const Package& package, const std::vector<std::string>& downloads, const std::string& staging, std::string& error)
{
    std::vector<PlannedFile> planned;
    const Emulator* emulator = package.kind == "emulator" ? findEmulator(package.id) : nullptr;
    const bool core = package.kind == "core" && isCoreId(package.id);
    if (!emulator && !core) {
        error = "Only emulators of RetroFE's built-in list may install programs";
        return false;
    }
    if (core) {
        // libretro's zip of one DLL, named for the core, with the CRC-32 its list gives
        const std::string dll = package.id.substr(5) + "_libretro.dll";
        uint64_t offset = 0;
        if (Archive::detect(downloads[0], offset) != Archive::Form::Zip) { error = "A core download must be a ZIP archive"; return false; }
        Archive archive;
        if (!archive.open(downloads[0], package.files[0].url, error)) return false;
        const auto& entries = archive.entries();
        if (entries.size() != 1 || entries[0].directory || entries[0].name != dll) {
            error = "A core archive must contain only its core DLL";
            return false;
        }
        if (entries[0].crc32 != *package.files[0].crc32) {
            error = "The core doesn't match libretro's list";
            return false;
        }
    }
    for (size_t i = 0; i < package.files.size(); i++) {
        const PackageFile& source = package.files[i];
        fs::path into = fs::path(staging) / "files" / std::to_string(i);
        uint64_t archiveOffset;
        if (Archive::detect(downloads[i], archiveOffset) == Archive::Form::None) {
            error = publishedName(source.url) + " isn't an archive RetroFE can unpack";
            return false;
        }
        if (!extractArchive(downloads[i], staging, into, source, planned, stop_, emulator ? emulator->topFolder : "", error)) {
            return false;
        }
    }

    if (emulator) {
        if (std::none_of(planned.begin(), planned.end(), [&](const PlannedFile& file) {
            return file.file.path == emulator->folder + "/" + emulator->program;
        })) {
            error = package.name + " archive does not contain " + emulator->program;
            return false;
        }
        for (const auto& name : emulator->portableFiles) {
            std::string path = emulator->folder + "/" + name;
            std::error_code ec;
            if (fs::exists(fs::path(emulators_) / fromUtf8(path), ec)) continue;
            if (std::any_of(planned.begin(), planned.end(), [&](const PlannedFile& file) { return fileKey(file.file) == lower(path); })) continue;
            fs::path staged = fs::path(staging) / "files" / "portable" / fromUtf8(name);
            if (!writeText(staged, "", error)) return false;
            PlannedFile file;
            file.staged = utf8(staged.lexically_relative(staging));
            file.file.path = path;
            file.file.sha256 = sha256File(staged);
            file.file.portable = true;
            planned.push_back(file);
        }
    }
    // If two files land in the same place, the later one wins
    std::vector<PlannedFile> unique;
    std::set<std::string> seen;
    for (auto it = planned.rbegin(); it != planned.rend(); ++it) {
        if (seen.insert(fileKey(it->file)).second) {
            unique.insert(unique.begin(), *it);
        }
    }

    // Refuse the whole package before anything is moved
    json files = json::array();
    for (const PlannedFile& plannedFile : unique) {
        std::string problem = refusal(plannedFile.file, package.id, package.kind);
        if (!problem.empty()) {
            error = problem;
            return false;
        }
        json entry = toJson(plannedFile.file);
        entry["staged"] = plannedFile.staged;
        files.push_back(entry);
    }
    json plan = { { "id", package.id }, { "name", package.name }, { "kind", package.kind },
                  { "version", package.version }, { "files", files } };
    return writeText(fs::path(staging) / "plan.json", plan.dump(2), error);
}

bool Installer::commit(const std::string& staging, std::string& error)
{
    std::string text;
    json plan;
    if (!readText(fs::path(staging) / "plan.json", text) || !(plan = json::parse(text, nullptr, false)).is_object()) {
        error = "The staged install is incomplete";
        return false;
    }

    Manifest manifest;
    manifest.id = J::text(plan, "id");
    manifest.name = J::text(plan, "name");
    manifest.kind = J::text(plan, "kind");
    manifest.version = J::text(plan, "version");
    if (!isValidId(manifest.id) || manifest.id != fs::path(staging).filename().string()) {
        error = "The staged install has no valid package id";
        return false;
    }

    Manifest previous;
    std::string ignored;
    bool updating = readManifest(manifestPath(manifest.id), previous, ignored) && !previous.removed;
    if (!updating) previous = Manifest();
    std::map<std::string, InstalledFile> previousFiles;
    for (const InstalledFile& old : previous.files) {
        previousFiles[fileKey(old)] = old;
    }

    // Check every file before touching anything
    struct Step
    {
        InstalledFile file;
        fs::path staged;
        fs::path destination;
    };
    std::vector<Step> steps;
    int changed = 0;
    for (const json& entry : J::list(plan, "files")) {
        Step step;
        if (!fromJson(entry, step.file)) {
            error = "The staged install has an invalid file";
            return false;
        }
        std::string problem = refusal(step.file, manifest.id, manifest.kind);
        if (!problem.empty()) {
            error = problem;
            return false;
        }
        // Staged files can only come from this install's own staging folder
        std::string staged = J::text(entry, "staged");
        if (staged.rfind("files/", 0) != 0 || !isSafeRelativePath(staged)) {
            error = "The staged install has an invalid file";
            return false;
        }
        step.staged = fs::path(staging) / fromUtf8(staged);
        step.destination = fs::path(emulators_) / fromUtf8(step.file.path);
        auto old = previousFiles.find(fileKey(step.file));
        if (old != previousFiles.end()) {
            std::error_code ec;
            if (fs::exists(step.destination, ec) && sha256File(step.destination) != old->second.sha256) {
                changed++;
            }
        }
        steps.push_back(step);
    }

    // The journal is written once, before the first move: a resumed commit
    // must know which files were there before this install began
    fs::path packageBackups = fs::path(folder_) / "backups" / manifest.id;
    fs::path journalPath = fs::path(staging) / "commit.json";
    Journal journal;
    std::error_code ec;
    if (!readJournal(journalPath, journal)) {
        journal = Journal();
        std::string base = folderName(updating ? (previous.version.empty() ? "previous" : previous.version)
                                               : "before-" + (manifest.version.empty() ? std::string("install") : manifest.version));
        journal.backup = base;
        for (int n = 2; fs::exists(packageBackups / journal.backup, ec); n++) {
            journal.backup = base + "-" + std::to_string(n);
        }
        fs::path backups = packageBackups / journal.backup;
        json existing = json::array();
        for (const Step& step : steps) {
            if (!fs::exists(step.destination, ec)) continue;
            if (!fs::is_regular_file(step.destination, ec)) {
                error = "Can't install " + step.file.path + ": a folder or something else is in the way";
                return false;
            }
            if (!copyOver(step.destination, backups / fromUtf8(step.file.path))) {
                error = "Could not back up " + step.file.path + ", so nothing was changed";
                return false;
            }
            journal.existing.insert(fileKey(step.file));
            existing.push_back(fileKey(step.file));
        }
        if (!writeText(journalPath, json{ { "backup", journal.backup }, { "existing", existing } }.dump(2), error)) {
            return false;
        }
    }
    fs::path backups = packageBackups / journal.backup;

    // Puts every destination back the way the journal says it was
    auto rollBack = [&] {
        for (const Step& step : steps) {
            std::error_code rec;
            if (journal.existing.count(fileKey(step.file))) {
                copyOver(backups / fromUtf8(step.file.path), step.destination);
            }
            else if (fs::exists(step.destination, rec) && sha256File(step.destination) == step.file.sha256) {
                fs::remove(step.destination, rec);
                removeEmptyFolders(step.destination.parent_path(), emulators_);
            }
        }
        LOG_WARNING("Downloads", "Put back the files " + manifest.id + " " + manifest.version + " would have changed");
    };

    for (Step& step : steps) {
        if (fs::exists(step.staged, ec)) {
            if (!moveFile(step.staged, step.destination, error)) {
                rollBack();
                error += ". Nothing was changed.";
                return false;
            }
        }
        else if (!fs::exists(step.destination, ec)) {
            // Moved already by an earlier, interrupted commit; otherwise it's lost
            rollBack();
            error = "A staged file is missing: " + step.file.path + ". Nothing was changed.";
            return false;
        }
        // Remember the user's own file this replaces, all the way back to the first install
        std::string key = fileKey(step.file);
        auto old = previousFiles.find(key);
        if (old != previousFiles.end()) {
            step.file.backup = old->second.backup;
        }
        else if (journal.existing.count(key)) {
            step.file.backup = journal.backup;
        }
        manifest.files.push_back(step.file);
    }

    if (updating) {
        manifest.launcherPath = previous.launcherPath;
        manifest.launcherSha256 = previous.launcherSha256;
        manifest.launcherName = previous.launcherName;
        // Portable files this module created stay its own across updates
        for (const auto& old : previous.files) {
            if (old.portable && std::none_of(manifest.files.begin(), manifest.files.end(), [&](const InstalledFile& file) { return fileKey(file) == fileKey(old); })) manifest.files.push_back(old);
        }
    }
    if (!writeManifest(manifestPath(manifest.id), manifest, error)) {
        rollBack();
        error += ". Nothing was changed.";
        return false;
    }

    // Files the old version had and the new one doesn't, unless they've been
    // changed since; ones that had replaced a file of the user's put it back
    std::set<std::string> newFiles;
    for (const InstalledFile& file : manifest.files) {
        newFiles.insert(fileKey(file));
    }
    for (const InstalledFile& old : previous.files) {
        if (newFiles.count(fileKey(old)) || !refusal(old, manifest.id, manifest.kind).empty()) continue;
        fs::path path = fs::path(emulators_) / fromUtf8(old.path);
        if (!fs::exists(path, ec) || sha256File(path) != old.sha256) continue;
        fs::path original = packageBackups / old.backup / fromUtf8(old.path);
        if (!old.backup.empty() && fs::exists(original, ec)) {
            copyOver(original, path);
        }
        else {
            fs::remove(path, ec);
            removeEmptyFolders(path.parent_path(), emulators_);
        }
    }

    // Keep the backups still needed: the user's originals, and this update's for undoing it
    std::set<std::string> needed = { journal.backup };
    for (const InstalledFile& file : manifest.files) {
        if (!file.backup.empty()) needed.insert(file.backup);
    }
    for (const auto& entry : fs::directory_iterator(packageBackups, ec)) {
        if (!needed.count(entry.path().filename().string())) {
            fs::remove_all(entry.path(), ec);
        }
    }

    if (changed) installMessage_ = " Replaced " + std::to_string(changed) + (changed == 1 ? " changed file; backups are in " : " changed files; backups are in ") + backups.string() + ".";
    fs::remove_all(staging, ec);
    LOG_NOTICE("Downloads", "Installed " + manifest.id + " " + manifest.version + " (" + std::to_string(manifest.files.size()) + " files)");
    return true;
}

bool Installer::remove(const std::string& id, Removal& removal, std::string& error)
{
    removal = Removal();
    if (id == "emulator-retroarch") {
        std::string names;
        for (const auto& item : installed()) if (item.second.kind == "core") {
            if (!names.empty()) names += ", ";
            names += item.second.name;
        }
        if (!names.empty()) { error = "Remove its cores first: " + names; return false; }
    }
    Manifest manifest;
    if (!isValidId(id) || !readManifest(manifestPath(id), manifest, error) || manifest.removed) {
        error = "That isn't installed.";
        return false;
    }
    fs::path packageBackups = fs::path(folder_) / "backups" / id;
    bool allRestored = true;
    for (const InstalledFile& file : manifest.files) {
        if (!refusal(file, manifest.id, manifest.kind).empty()) {
            continue;
        }
        fs::path path = fs::path(emulators_) / fromUtf8(file.path);
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            continue;
        }
        // Leave anything that's been changed since it was installed
        if (sha256File(path) != file.sha256) {
            removal.kept++;
            continue;
        }
        // Put back what it replaced, or take it away
        fs::path original = packageBackups / file.backup / fromUtf8(file.path);
        if (!file.backup.empty() && fs::exists(original, ec)) {
            if (copyOver(original, path)) {
                removal.restored++;
            }
            else {
                allRestored = false;
            }
            continue;
        }
        fs::remove(path, ec);
        removeEmptyFolders(path.parent_path(), emulators_);
    }

    // What's still in the emulator's folder: its settings and saves, files it changed
    std::error_code ec;
    if (const Emulator* entry = manifest.kind == "emulator" ? findEmulator(id) : nullptr) {
        fs::path folder = fs::path(emulators_) / fromUtf8(entry->folder);
        for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec)) removal.left.push_back(utf8(fs::path(entry->folder) / it->path().lexically_relative(folder)));
        }
        std::sort(removal.left.begin(), removal.left.end());
    }
    if (removal.left.empty()) {
        fs::remove(manifestPath(id), ec);
    }
    else {
        // The folder stays this module's, so installing again can use it
        Manifest left;
        left.id = manifest.id;
        left.name = manifest.name;
        left.kind = manifest.kind;
        left.version = manifest.version;
        left.removed = true;
        std::string ignored;
        if (!writeManifest(manifestPath(id), left, ignored)) fs::remove(manifestPath(id), ec);
    }
    if (allRestored) {
        fs::remove_all(packageBackups, ec);
    }
    LOG_NOTICE("Downloads", "Removed " + id + " (put back " + std::to_string(removal.restored) + ", kept "
               + std::to_string(removal.kept) + " changed files)");
    return true;
}

void Installer::recover()
{
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::path(folder_) / "staging", ec)) {
        if (!entry.is_directory()) continue;
        if (fs::exists(entry.path() / ".ready", ec)) {
            std::string error;
            if (commit(entry.path().string(), error)) {
                LOG_NOTICE("Downloads", "Finished an interrupted install of " + entry.path().filename().string());
                continue;
            }
            // commit() has put back anything it changed; don't try again every time
            LOG_ERROR("Downloads", "Could not finish installing " + entry.path().filename().string() + ": " + error);
        }
        fs::remove_all(entry.path(), ec);
    }
}

}

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
#include "Hash.h"
#include "Index.h"
#include "../Utility/Log.h"
#include "minizip/unzip.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    // What staging records for each file: where it is now and where it goes
    struct PlannedFile
    {
        std::string staged;   // under the staging folder, UTF-8
        Catalog::InstalledFile file;
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
    std::string fileKey(const Catalog::InstalledFile& file)
    {
        return file.target + "/" + lower(file.path);
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

    // Remove folders a removal emptied, up to but not including the target folder
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
        return !relative.empty() && *relative.begin() != "..";
    }

    // The file name a package file was published under
    std::string publishedName(const std::string& url)
    {
        std::string name = url.substr(0, url.find_first_of("?#"));
        size_t slash = name.find_last_of("/\\");
        return slash == std::string::npos ? name : name.substr(slash + 1);
    }

    bool isZip(const std::string& url, const std::string& download)
    {
        return lower(fs::path(publishedName(url)).extension().string()) == ".zip"
            || lower(fs::path(download).extension().string()) == ".zip";
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

    bool extractZip(const std::string& archive, const fs::path& stagingRoot, const fs::path& into,
                    const Catalog::PackageFile& source, std::vector<PlannedFile>& planned,
                    const std::atomic<bool>* stop, std::string& error)
    {
        unzFile zip = unzOpen64(archive.c_str());
        if (!zip) {
            error = "Not a zip archive: " + publishedName(source.url);
            return false;
        }

        std::vector<char> buffer(1 << 16);
        int result = unzGoToFirstFile(zip);
        while (result == UNZ_OK) {
            unz_file_info64 info;
            char name[4096];
            if (unzGetCurrentFileInfo64(zip, &info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK) {
                error = "Damaged zip archive: " + publishedName(source.url);
                unzClose(zip);
                return false;
            }
            std::string entry = name;
            std::replace(entry.begin(), entry.end(), '\\', '/');

            if (!entry.empty() && entry.back() != '/') {
                // Nothing may land outside its target, or be a program
                if (!Catalog::isSafeRelativePath(entry)) {
                    error = publishedName(source.url) + " contains a file that can't be installed safely: " + entry;
                    unzClose(zip);
                    return false;
                }
                if (Catalog::isExecutableName(entry)) {
                    error = publishedName(source.url) + " contains a program or script (" + entry + "), which packages may not install";
                    unzClose(zip);
                    return false;
                }
                bool isUtf8 = (info.flag & (1 << 11)) != 0;
                fs::path entryPath = isUtf8 ? fs::u8path(entry) : fs::path(entry);
                fs::path out = into / entryPath;

                std::error_code ec;
                fs::create_directories(out.parent_path(), ec);
                if (unzOpenCurrentFile(zip) != UNZ_OK) {
                    error = "Could not read " + entry + " from " + publishedName(source.url);
                    unzClose(zip);
                    return false;
                }
                std::ofstream file(out, std::ios::binary | std::ios::trunc);
                Catalog::Sha256 hash;
                uint64_t size = 0;
                int read = 0;
                while ((read = unzReadCurrentFile(zip, buffer.data(), static_cast<unsigned>(buffer.size()))) > 0) {
                    file.write(buffer.data(), read);
                    hash.update(buffer.data(), static_cast<size_t>(read));
                    size += static_cast<uint64_t>(read);
                    if (stop && *stop) {
                        error = "Stopped";
                        unzCloseCurrentFile(zip);
                        unzClose(zip);
                        return false;
                    }
                }
                file.close();
                // Closing checks the CRC
                if (read < 0 || unzCloseCurrentFile(zip) != UNZ_OK || !file) {
                    error = "Damaged file " + entry + " in " + publishedName(source.url);
                    unzClose(zip);
                    return false;
                }

                PlannedFile plannedFile;
                plannedFile.staged = utf8(out.lexically_relative(stagingRoot));
                plannedFile.file.target = source.target;
                plannedFile.file.path = joinPath(source.path, utf8(entryPath));
                plannedFile.file.size = size;
                plannedFile.file.sha256 = hash.hex();
                planned.push_back(plannedFile);
            }
            result = unzGoToNextFile(zip);
        }
        unzClose(zip);
        if (result != UNZ_END_OF_LIST_OF_FILE) {
            error = "Damaged zip archive: " + publishedName(source.url);
            return false;
        }
        return true;
    }

    json toJson(const Catalog::InstalledFile& file)
    {
        json value = { { "target", file.target }, { "path", file.path }, { "size", file.size }, { "sha256", file.sha256 } };
        if (!file.backup.empty()) {
            value["backup"] = file.backup;
        }
        return value;
    }

    bool fromJson(const json& value, Catalog::InstalledFile& file)
    {
        if (!value.is_object()) return false;
        file.target = value.value("target", "");
        file.path = value.value("path", "");
        file.size = value.value("size", uint64_t{ 0 });
        file.sha256 = value.value("sha256", "");
        file.backup = value.value("backup", "");
        return Catalog::isKnownTarget(file.target) && !file.path.empty() && Catalog::isSafeRelativePath(file.path)
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
        journal.backup = document.value("backup", "");
        for (const json& key : document.value("existing", json::array())) {
            if (key.is_string()) journal.existing.insert(key.get<std::string>());
        }
        return !journal.backup.empty() && folderName(journal.backup) == journal.backup;
    }
}

namespace Catalog
{

bool isExecutableName(const std::string& name)
{
    static const std::set<std::string> extensions = {
        ".exe", ".dll", ".com", ".scr", ".cpl", ".msi", ".msp", ".sys", ".drv", ".ocx",
        ".bat", ".cmd", ".ps1", ".psm1", ".vbs", ".vbe", ".js", ".jse", ".wsf", ".wsh", ".hta",
        ".lnk", ".url", ".pif", ".reg", ".inf", ".sh", ".command", ".so", ".dylib", ".app", ".jar"
    };
    std::string file = name.substr(name.find_last_of("/\\") == std::string::npos ? 0 : name.find_last_of("/\\") + 1);
    size_t dot = file.rfind('.');
    return dot != std::string::npos && extensions.count(lower(file.substr(dot)));
}

std::string Targets::resolve(const std::string& target) const
{
    if (target == "root") return root;
    if (target == "collections") return collections;
    if (target == "items") return items;
    if (target == "media") return media;
    if (target == "layouts") return layouts;
    if (target == "meta") return meta;
    return "";
}

bool readManifest(const std::string& file, Manifest& manifest, std::string& error)
{
    std::string text;
    if (!readText(file, text)) {
        error = "Could not read " + file;
        return false;
    }
    json document = json::parse(text, nullptr, false);
    if (!document.is_object()) {
        error = file + " isn't valid JSON";
        return false;
    }
    manifest = Manifest();
    manifest.id = document.value("id", "");
    manifest.name = document.value("name", "");
    manifest.kind = document.value("kind", "");
    manifest.version = document.value("version", "");
    manifest.source = document.value("source", "");
    if (auto files = document.find("files"); files != document.end() && files->is_array()) {
        for (const json& value : *files) {
            InstalledFile installedFile;
            if (fromJson(value, installedFile)) {
                manifest.files.push_back(installedFile);
            }
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
                      { "version", manifest.version }, { "source", manifest.source }, { "files", files } };
    return writeText(file, document.dump(2), error);
}

Installer::Installer(const std::string& catalogFolder, const Targets& targets)
    : catalog_(catalogFolder)
    , targets_(targets)
{
    // Its own records are never package content
    targets_.protectedFolders.push_back(catalogFolder);
}

std::string Installer::manifestPath(const std::string& id) const
{
    return (fs::path(catalog_) / "installed" / (id + ".json")).string();
}

std::string Installer::refusal(const InstalledFile& file) const
{
    fs::path root = targets_.resolve(file.target);
    fs::path destination = root / fromUtf8(file.path);
    if (root.empty() || !isInside(destination, root)) {
        return "Refusing to write outside " + file.target + ": " + file.path;
    }
    if (isExecutableName(file.path)) {
        return "Packages may not install programs or scripts: " + file.path;
    }
    for (const std::string& folder : targets_.protectedFolders) {
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
    for (const auto& entry : fs::directory_iterator(fs::path(catalog_) / "installed", ec)) {
        if (entry.path().extension() != ".json") continue;
        Manifest manifest;
        std::string error;
        if (readManifest(entry.path().string(), manifest, error)) {
            result[manifest.id] = manifest;
        }
        else {
            LOG_WARNING("Catalog", error);
        }
    }
    return result;
}

bool Installer::install(const Package& package, const std::vector<std::string>& downloads, std::string& error)
{
    if (!isValidId(package.id)) {
        error = "Invalid package id";
        return false;
    }
    if (downloads.size() != package.files.size()) {
        error = "Expected " + std::to_string(package.files.size()) + " downloaded files";
        return false;
    }

    std::string staging = (fs::path(catalog_) / "staging" / package.id).string();
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
    for (size_t i = 0; i < package.files.size(); i++) {
        const PackageFile& source = package.files[i];
        if (targets_.resolve(source.target).empty()) {
            error = "Unknown target " + source.target;
            return false;
        }
        fs::path into = fs::path(staging) / "files" / std::to_string(i);

        if (isZip(source.url, downloads[i])) {
            if (!extractZip(downloads[i], staging, into, source, planned, stop_, error)) {
                return false;
            }
        }
        else {
            std::string name = publishedName(source.url);
            if (!isSafeRelativePath(name) || name.empty()) {
                error = "Can't tell what to call " + source.url;
                return false;
            }
            std::error_code ec;
            fs::create_directories(into, ec);
            fs::copy_file(downloads[i], into / name, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "Could not stage " + name + ": " + ec.message();
                return false;
            }
            PlannedFile plannedFile;
            plannedFile.staged = utf8((into / name).lexically_relative(staging));
            plannedFile.file.target = source.target;
            plannedFile.file.path = joinPath(source.path, name);
            plannedFile.file.size = fs::file_size(into / name, ec);
            plannedFile.file.sha256 = sha256File((into / name).string());
            planned.push_back(plannedFile);
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
        std::string problem = refusal(plannedFile.file);
        if (!problem.empty()) {
            error = problem;
            return false;
        }
        json entry = toJson(plannedFile.file);
        entry["staged"] = plannedFile.staged;
        files.push_back(entry);
    }
    json plan = { { "id", package.id }, { "name", package.name }, { "kind", package.kind },
                  { "version", package.version }, { "source", package.source }, { "files", files } };
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
    manifest.id = plan.value("id", "");
    manifest.name = plan.value("name", "");
    manifest.kind = plan.value("kind", "");
    manifest.version = plan.value("version", "");
    manifest.source = plan.value("source", "");
    if (!isValidId(manifest.id) || manifest.id != fs::path(staging).filename().string()) {
        error = "The staged install has no valid package id";
        return false;
    }

    Manifest previous;
    std::string ignored;
    bool updating = readManifest(manifestPath(manifest.id), previous, ignored);
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
    for (const json& entry : plan.value("files", json::array())) {
        Step step;
        if (!fromJson(entry, step.file)) {
            error = "The staged install has an invalid file";
            return false;
        }
        std::string problem = refusal(step.file);
        if (!problem.empty()) {
            error = problem;
            return false;
        }
        // Staged files can only come from this install's own staging folder
        std::string staged = entry.value("staged", "");
        if (staged.rfind("files/", 0) != 0 || !isSafeRelativePath(staged)) {
            error = "The staged install has an invalid file";
            return false;
        }
        step.staged = fs::path(staging) / fromUtf8(staged);
        step.destination = fs::path(targets_.resolve(step.file.target)) / fromUtf8(step.file.path);
        steps.push_back(step);
    }

    // The journal is written once, before the first move: a resumed commit
    // must know which files were there before this install began
    fs::path packageBackups = fs::path(catalog_) / "backups" / manifest.id;
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
            if (!copyOver(step.destination, backups / step.file.target / fromUtf8(step.file.path))) {
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
                copyOver(backups / step.file.target / fromUtf8(step.file.path), step.destination);
            }
            else if (fs::exists(step.destination, rec) && sha256File(step.destination.string()) == step.file.sha256) {
                fs::remove(step.destination, rec);
                removeEmptyFolders(step.destination.parent_path(), targets_.resolve(step.file.target));
            }
        }
        LOG_WARNING("Catalog", "Put back the files " + manifest.id + " " + manifest.version + " would have changed");
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
        if (newFiles.count(fileKey(old)) || !refusal(old).empty()) continue;
        fs::path root = targets_.resolve(old.target);
        fs::path path = root / fromUtf8(old.path);
        if (!fs::exists(path, ec) || sha256File(path.string()) != old.sha256) continue;
        fs::path original = packageBackups / old.backup / old.target / fromUtf8(old.path);
        if (!old.backup.empty() && fs::exists(original, ec)) {
            copyOver(original, path);
        }
        else {
            fs::remove(path, ec);
            removeEmptyFolders(path.parent_path(), root);
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

    fs::remove_all(staging, ec);
    LOG_NOTICE("Catalog", "Installed " + manifest.id + " " + manifest.version + " (" + std::to_string(manifest.files.size()) + " files)");
    return true;
}

bool Installer::remove(const std::string& id, Removal& removal, std::string& error)
{
    removal = Removal();
    Manifest manifest;
    if (!readManifest(manifestPath(id), manifest, error)) {
        return false;
    }
    fs::path packageBackups = fs::path(catalog_) / "backups" / id;
    bool allRestored = true;
    for (const InstalledFile& file : manifest.files) {
        if (!refusal(file).empty()) {
            continue;
        }
        fs::path root = targets_.resolve(file.target);
        fs::path path = root / fromUtf8(file.path);
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            continue;
        }
        // Leave anything that's been changed since it was installed
        if (sha256File(path.string()) != file.sha256) {
            removal.kept++;
            continue;
        }
        // Put back what it replaced, or take it away
        fs::path original = packageBackups / file.backup / file.target / fromUtf8(file.path);
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
        removeEmptyFolders(path.parent_path(), root);
    }
    std::error_code ec;
    fs::remove(manifestPath(id), ec);
    if (allRestored) {
        fs::remove_all(packageBackups, ec);
    }
    LOG_NOTICE("Catalog", "Removed " + id + " (put back " + std::to_string(removal.restored) + ", kept "
               + std::to_string(removal.kept) + " changed files)");
    return true;
}

void Installer::recover()
{
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::path(catalog_) / "staging", ec)) {
        if (!entry.is_directory()) continue;
        if (fs::exists(entry.path() / ".ready", ec)) {
            std::string error;
            if (commit(entry.path().string(), error)) {
                LOG_NOTICE("Catalog", "Finished an interrupted install of " + entry.path().filename().string());
                continue;
            }
            // commit() has put back anything it changed; don't try again every start
            LOG_ERROR("Catalog", "Could not finish installing " + entry.path().filename().string() + ": " + error);
        }
        fs::remove_all(entry.path(), ec);
    }
}

}

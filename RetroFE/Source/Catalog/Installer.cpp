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

    bool extractZip(const std::string& archive, const fs::path& stagingRoot, const fs::path& into,
                    const Catalog::PackageFile& source, std::vector<PlannedFile>& planned, std::string& error)
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
                // Nothing may land outside its target
                if (!Catalog::isSafeRelativePath(entry)) {
                    error = publishedName(source.url) + " contains a file outside its folder: " + entry;
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
        return json{ { "target", file.target }, { "path", file.path }, { "size", file.size }, { "sha256", file.sha256 } };
    }

    bool fromJson(const json& value, Catalog::InstalledFile& file)
    {
        if (!value.is_object()) return false;
        file.target = value.value("target", "");
        file.path = value.value("path", "");
        file.size = value.value("size", uint64_t{ 0 });
        file.sha256 = value.value("sha256", "");
        return Catalog::isKnownTarget(file.target) && !file.path.empty() && Catalog::isSafeRelativePath(file.path);
    }
}

namespace Catalog
{

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
}

std::string Installer::manifestPath(const std::string& id) const
{
    return (fs::path(catalog_) / "installed" / (id + ".json")).string();
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
    return commit(staging, error);
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
            if (!extractZip(downloads[i], staging, into, source, planned, error)) {
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
        if (seen.insert(it->file.target + "/" + lower(it->file.path)).second) {
            unique.insert(unique.begin(), *it);
        }
    }

    json files = json::array();
    for (const PlannedFile& plannedFile : unique) {
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
    if (!isValidId(manifest.id)) {
        error = "The staged install has no package id";
        return false;
    }

    Manifest previous;
    std::string ignored;
    bool updating = readManifest(manifestPath(manifest.id), previous, ignored);
    std::string backupName = updating ? (previous.version.empty() ? "previous" : previous.version)
                                      : "before-" + (manifest.version.empty() ? std::string("install") : manifest.version);
    fs::path backups = fs::path(catalog_) / "backups" / manifest.id / backupName;

    std::set<std::string> newFiles;
    for (const json& entry : plan.value("files", json::array())) {
        InstalledFile installedFile;
        if (!fromJson(entry, installedFile)) {
            error = "The staged install has an invalid file";
            return false;
        }
        fs::path root = targets_.resolve(installedFile.target);
        fs::path destination = root / fromUtf8(installedFile.path);
        if (root.empty() || !isInside(destination, root)) {
            error = "Refusing to write outside " + installedFile.target + ": " + installedFile.path;
            return false;
        }

        // Keep what's being replaced so it can be put back. A resumed commit
        // mustn't overwrite the backup with the new file it already moved here.
        std::error_code ec;
        if (fs::exists(destination, ec)) {
            fs::path backup = backups / installedFile.target / fromUtf8(installedFile.path);
            fs::create_directories(backup.parent_path(), ec);
            fs::copy_file(destination, backup, fs::copy_options::skip_existing, ec);
        }

        fs::path staged = fs::path(staging) / fromUtf8(entry.value("staged", ""));
        if (fs::exists(staged, ec)) {
            if (!moveFile(staged, destination, error)) {
                return false;
            }
        }
        else if (!fs::exists(destination, ec)) {
            // Moved already by an earlier, interrupted commit; otherwise it's lost
            error = "A staged file is missing: " + installedFile.path;
            return false;
        }
        manifest.files.push_back(installedFile);
        newFiles.insert(installedFile.target + "/" + lower(installedFile.path));
    }

    // Files the old version had and the new one doesn't, unless they've been changed since
    if (updating) {
        for (const InstalledFile& old : previous.files) {
            if (newFiles.count(old.target + "/" + lower(old.path))) continue;
            fs::path root = targets_.resolve(old.target);
            fs::path path = root / fromUtf8(old.path);
            std::error_code ec;
            if (!root.empty() && isInside(path, root) && fs::exists(path, ec) && sha256File(path.string()) == old.sha256) {
                fs::remove(path, ec);
                removeEmptyFolders(path.parent_path(), root);
            }
        }
    }

    if (!writeManifest(manifestPath(manifest.id), manifest, error)) {
        return false;
    }
    std::error_code ec;
    fs::remove_all(staging, ec);
    LOG_NOTICE("Catalog", "Installed " + manifest.id + " " + manifest.version + " (" + std::to_string(manifest.files.size()) + " files)");
    return true;
}

bool Installer::remove(const std::string& id, int& kept, std::string& error)
{
    kept = 0;
    Manifest manifest;
    if (!readManifest(manifestPath(id), manifest, error)) {
        return false;
    }
    for (const InstalledFile& file : manifest.files) {
        fs::path root = targets_.resolve(file.target);
        fs::path path = root / fromUtf8(file.path);
        std::error_code ec;
        if (root.empty() || !isInside(path, root) || !fs::exists(path, ec)) {
            continue;
        }
        // Leave anything that's been changed since it was installed
        if (sha256File(path.string()) != file.sha256) {
            kept++;
            continue;
        }
        fs::remove(path, ec);
        removeEmptyFolders(path.parent_path(), root);
    }
    std::error_code ec;
    fs::remove(manifestPath(id), ec);
    LOG_NOTICE("Catalog", "Removed " + id + (kept ? " (kept " + std::to_string(kept) + " changed files)" : ""));
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
            }
            else {
                LOG_ERROR("Catalog", "Could not finish installing " + entry.path().filename().string() + ": " + error);
            }
        }
        else {
            fs::remove_all(entry.path(), ec);
        }
    }
}

}

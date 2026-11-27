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
#include "../Library/Files.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <vector>
#ifdef __APPLE__
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;
#endif

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

    // RetroFE path strings are native narrow; only archive and manifest relative names are UTF-8.
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

    std::string pathForMessage(const fs::path& path)
    {
        std::string result;
        return Library::narrowName(path, result) ? result : "a path not representable in the current code page";
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
                error = "Could not write " + pathForMessage(path);
                return false;
            }
        }
        fs::rename(temp, path, ec);
        if (ec) {
            fs::remove(temp, ec);
            error = "Could not replace " + pathForMessage(path);
            return false;
        }
        return true;
    }

    // What's at a path, without following a link
    fs::file_status statusOf(const fs::path& path)
    {
        std::error_code ec;
        return fs::symlink_status(path, ec);
    }

    // Whether anything is at a path, a link included (fs::exists follows links)
    bool present(const fs::path& path)
    {
        return fs::exists(statusOf(path));
    }

    bool isLink(const fs::path& path)
    {
        return fs::is_symlink(statusOf(path));
    }

    // A link's target, '/' separated UTF-8, or "" if it isn't one
    std::string linkTarget(const fs::path& path)
    {
        std::error_code ec;
        fs::path target = fs::read_symlink(path, ec);
        return ec ? std::string() : target.generic_u8string();
    }

    // Whether relative, a path under base, leads somewhere strictly inside base
    // when it's followed as the system follows it: through every link on the
    // way (a link's ".." is from where it leads, not where it is), with what
    // doesn't exist yet taken as written. A link to an absolute path, or more
    // than 40 links, doesn't. base itself may be a link (the user's).
    bool staysInside(const fs::path& base, const std::string& relative)
    {
        auto split = [](const std::string& text) {
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= text.size()) {
                size_t slash = text.find('/', start);
                if (slash == std::string::npos) slash = text.size();
                if (slash > start) parts.push_back(text.substr(start, slash - start));
                start = slash + 1;
            }
            return parts;
        };
        std::vector<std::string> todo = split(relative);
        std::reverse(todo.begin(), todo.end());   // a stack: the next part last
        std::vector<std::string> at;              // where it's got to, under base
        int links = 0;
        while (!todo.empty()) {
            std::string part = todo.back();
            todo.pop_back();
            if (part == ".") continue;
            if (part == "..") {
                if (at.empty()) return false;
                at.pop_back();
                continue;
            }
            at.push_back(part);
            fs::path here = base;
            for (const std::string& name : at) here /= fs::u8path(name);
            if (!isLink(here)) continue;
            std::string target = linkTarget(here);
            if (++links > 40 || target.empty() || target[0] == '/' || target.find_first_of("\\:") != std::string::npos) return false;
            at.pop_back();
            std::vector<std::string> more = split(target);
            todo.insert(todo.end(), more.rbegin(), more.rend());
        }
        return !at.empty();
    }

    // Whether what's at path is still what was installed there: the same link,
    // or a file with the same content. A link is never followed.
    bool matches(const fs::path& path, const Downloads::InstalledFile& file)
    {
        fs::file_status status = statusOf(path);
        if (!file.link.empty()) return fs::is_symlink(status) && linkTarget(path) == file.link;
        return fs::is_regular_file(status) && Downloads::sha256File(path) == file.sha256;
    }

    // Makes a symbolic link at `at` to a relative target (never on Windows)
    bool makeLink(const std::string& target, const fs::path& at, std::string& error)
    {
#ifdef _WIN32
        (void)target;
        (void)at;
        error = "Links aren't installed on Windows";
        return false;
#else
        std::error_code ec;
        fs::create_directories(at.parent_path(), ec);
        if (present(at) && !fs::is_directory(statusOf(at))) fs::remove(at, ec);
        fs::create_symlink(fs::u8path(target), at, ec);
        if (ec) error = "Could not make the link " + at.filename().string() + ": " + ec.message();
        return !ec;
#endif
    }

    // Permission bits for a file off Windows (0 leaves them as created)
    void applyMode(const fs::path& path, uint32_t mode)
    {
#ifdef _WIN32
        (void)path;
        (void)mode;
#else
        if (mode == 0 || !fs::is_regular_file(statusOf(path))) return;
        std::error_code ec;
        fs::permissions(path, static_cast<fs::perms>(mode & 0777), fs::perm_options::replace, ec);
#endif
    }

    // What's kept of an archive's stored mode: its executable bits, read and
    // write for the owner, never write for others, never setuid, setgid or sticky
    uint32_t keptMode(uint32_t stored)
    {
#ifdef _WIN32
        (void)stored;
        return 0;
#else
        return stored == 0 ? 0 : ((stored & 0755) | 0600);
#endif
    }

    // Moves a file or link, copying when it has to cross drives. A link where
    // it goes is replaced, never written through.
    bool moveFile(const fs::path& from, const fs::path& to, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        if (!ec) return true;
        if (isLink(from)) {
            if (!makeLink(linkTarget(from), to, error)) return false;
            fs::remove(from, ec);
            return true;
        }
        if (isLink(to)) fs::remove(to, ec);
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Could not write " + pathForMessage(to) + ": " + ec.message();
            return false;
        }
        fs::remove(from, ec);
        return true;
    }

    // Copies a file, or a link as a link; never writes through a link at `to`
    bool copyOver(const fs::path& from, const fs::path& to)
    {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        if (isLink(to)) fs::remove(to, ec);
        if (isLink(from)) {
            std::string ignored;
            return makeLink(linkTarget(from), to, ignored);
        }
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

    // One package file going into staging, with the checks every form shares:
    // each name stays inside, nothing goes inside a link (or a link over what's
    // there), and links stay inside the emulator's folder
    struct Staging
    {
        fs::path root;                      // staging/<id>
        fs::path into;                      // where this file's names go: the emulator's folder, as staged
        const Downloads::PackageFile& source;
        std::vector<PlannedFile>& planned;
        std::string published;              // the download's name, for messages
        std::set<std::string> names;        // lower case: what's staged so far
        std::set<std::string> links;        // lower case

        Staging(const fs::path& stagingRoot, const fs::path& folder, const Downloads::PackageFile& file,
                std::vector<PlannedFile>& plan)
            : root(stagingRoot), into(folder), source(file), planned(plan), published(publishedName(file.url))
        {
        }

        // May name (relative to the emulator's folder) be staged, as a link or not?
        bool allowed(const std::string& name, bool link, std::string& error)
        {
            const std::string key = lower(name);
            for (size_t slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash + 1)) {
                if (links.count(key.substr(0, slash))) {
                    error = published + " has a file inside a link, which can't be installed safely: " + name;
                    return false;
                }
            }
            if (link) {
                auto after = names.lower_bound(key + "/");
                if (after != names.end() && after->compare(0, key.size() + 1, key + "/") == 0) {
                    error = published + " has a link over a folder, which can't be installed safely: " + name;
                    return false;
                }
#ifdef _WIN32
                error = published + " contains a link, which RetroFE doesn't install on Windows: " + name;
                return false;
#else
                links.insert(key);
#endif
            }
            names.insert(key);
            return true;
        }

        void add(const std::string& name, const fs::path& staged, uint64_t size, const std::string& sha256,
                 uint32_t mode, const std::string& link = "")
        {
            PlannedFile plannedFile;
            plannedFile.staged = utf8(staged.lexically_relative(root));
            plannedFile.file.path = joinPath(source.folder, name);
            plannedFile.file.size = size;
            plannedFile.file.sha256 = sha256;
            plannedFile.file.mode = mode;
            plannedFile.file.link = link;
            planned.push_back(plannedFile);
        }

        // A link to target at name, if it stays inside the emulator's folder
        bool addLink(const std::string& name, const std::string& target, std::string& error)
        {
            if (!Downloads::isSafeLink(name, target)) {
                error = published + " has a link that leads outside its folder, so it can't be installed safely: " + name + " -> " + target;
                return false;
            }
            fs::path out = into / fs::u8path(name);
            if (!makeLink(target, out, error)) return false;
            Downloads::Sha256 hash;
            hash.update(target.data(), target.size());
            add(name, out, target.size(), hash.hex(), 0, target);
            return true;
        }

        // Once everything's staged: every link, followed as the system would
        // (through other links too), still leads inside the emulator's folder
        bool linksStayInside(std::string& error) const
        {
            for (const PlannedFile& plannedFile : planned) {
                if (plannedFile.file.link.empty() || plannedFile.file.path.size() <= source.folder.size() + 1) continue;
                if (!staysInside(into, plannedFile.file.path.substr(source.folder.size() + 1))) {
                    error = published + " has a link that leads outside its folder, so it can't be installed safely: " + plannedFile.file.path;
                    return false;
                }
            }
            return true;
        }
    };

    bool extractArchive(const std::string& path, Staging& staging, const std::atomic<bool>* stop,
                        const std::string& topFolder, std::string& error)
    {
        const Downloads::PackageFile& source = staging.source;
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
            if (!staging.allowed(name, entry.symlink, error)) return false;
            fs::path entryPath = fs::u8path(name);
            if (entry.symlink) {
                // A link's data is its target
                std::string target;
                if (entry.size > 4096 || !archive.extract(i, [&](const void* data, size_t count) {
                    target.append(static_cast<const char*>(data), count);
                    return target.size() <= 4096;
                }, stop, error)) {
                    if (error.empty()) error = "Damaged link " + name + " in " + publishedName(source.url);
                    return false;
                }
                if (!staging.addLink(name, target, error)) return false;
                continue;
            }
            fs::path out = staging.into / entryPath;
            std::error_code ec;
            fs::create_directories(out.parent_path(), ec);
            if (isLink(out)) fs::remove(out, ec);   // an earlier entry of the same name: never write through it
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
            const uint32_t mode = keptMode(entry.mode);
            applyMode(out, mode);
            staging.add(name, out, size, hash.hex(), mode);
        }
        return staging.linksStayInside(error);
    }

    // An AppImage is the program itself: copied in as it is, mode 0755
    bool stageAppImage(const std::string& path, Staging& staging, const std::string& program, std::string& error)
    {
        if (!Downloads::isElfFile(path)) {
            error = staging.published + " isn't an AppImage: it doesn't start as a Linux program does";
            return false;
        }
        if (program.empty() || program.find('/') != std::string::npos || !Downloads::isSafeRelativePath(program)) {
            error = "Invalid program name";
            return false;
        }
        if (!staging.allowed(program, false, error)) return false;
        fs::path out = staging.into / fs::u8path(program);
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        if (!fs::copy_file(fs::u8path(path), out, fs::copy_options::overwrite_existing, ec) || ec) {
            error = "Could not stage " + staging.published + ": " + ec.message();
            return false;
        }
        uint64_t size = fs::file_size(out, ec);
        const uint32_t mode = keptMode(0755);
        applyMode(out, mode);
        staging.add(program, out, ec ? 0 : size, Downloads::sha256File(out), mode);
        return true;
    }

    // Copies appFolder (e.g. RetroArch.app) out of an attached disk image,
    // with the same checks as an archive's names. Nothing in it is run.
    bool copyFromImage(const fs::path& mount, const std::string& appFolder, Staging& staging,
                       const std::atomic<bool>* stop, std::string& error)
    {
        if (appFolder.empty() || appFolder.find('/') != std::string::npos || !Downloads::isSafeRelativePath(appFolder)) {
            error = "Invalid application folder";
            return false;
        }
        fs::path app = mount / fs::u8path(appFolder);
        if (!fs::is_directory(statusOf(app))) {
            error = staging.published + " has no " + appFolder;
            return false;
        }
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(app, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (stop && *stop) { error = "Stopped"; return false; }
            const fs::path& from = it->path();
            std::string name = utf8(fs::u8path(appFolder) / from.lexically_relative(app));
            if (name.empty() || !Downloads::isSafeRelativePath(name)) {
                error = staging.published + " contains a file that can't be installed safely: " + name;
                return false;
            }
            fs::file_status status = statusOf(from);
            if (fs::is_directory(status)) continue;
            const bool link = fs::is_symlink(status);
            if (!link && !fs::is_regular_file(status)) {
                error = staging.published + " contains something that isn't a file: " + name;
                return false;
            }
            if (!staging.allowed(name, link, error)) return false;
            if (link) {
                if (!staging.addLink(name, linkTarget(from), error)) return false;
                continue;
            }
            fs::path out = staging.into / fs::u8path(name);
            std::error_code copyError;
            fs::create_directories(out.parent_path(), copyError);
            if (!fs::copy_file(from, out, fs::copy_options::overwrite_existing, copyError) || copyError) {
                error = "Could not copy " + name + " from " + staging.published + ": " + copyError.message();
                return false;
            }
            const uint32_t mode = keptMode(static_cast<uint32_t>(status.permissions()) & 07777);
            applyMode(out, mode);
            uint64_t size = fs::file_size(out, copyError);
            staging.add(name, out, copyError ? 0 : size, Downloads::sha256File(out), mode);
        }
        if (ec) {
            error = "Could not read " + staging.published + ": " + ec.message();
            return false;
        }
        return staging.linksStayInside(error);
    }

#ifdef __APPLE__
    // Runs /usr/bin/hdiutil with these arguments: no shell, nothing to read,
    // its output discarded, stopped after `limit` seconds
    bool hdiutil(const std::vector<std::string>& arguments, int limit)
    {
        const std::string program = "/usr/bin/hdiutil";
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const std::string& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        if (posix_spawn_file_actions_init(&actions) != 0) return false;
        posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
        pid_t pid = 0;
        int spawned = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (spawned != 0) return false;
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(limit);
        int status = 0;
        while (true) {
            pid_t done = waitpid(pid, &status, WNOHANG);
            if (done == pid) break;
            if (done < 0 && errno != EINTR) return false;
            if (std::chrono::steady_clock::now() > until) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
#endif

    json toJson(const Downloads::InstalledFile& file)
    {
        json value = { { "path", file.path }, { "size", file.size }, { "sha256", file.sha256 } };
        if (!file.backup.empty()) {
            value["backup"] = file.backup;
        }
        if (file.portable) value["portable"] = true;
        if (!file.link.empty()) value["link"] = file.link;
        if (file.mode) value["mode"] = file.mode;
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
        file.link = J::text(value, "link");
        uint64_t mode = 0;
        J::count(value, "mode", mode);
        file.mode = static_cast<uint32_t>(mode);
        return !file.path.empty() && Downloads::isSafeRelativePath(file.path) && mode <= 0777
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
    if (!readText(fs::path(file), text)) {
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
    return writeText(fs::path(file), document.dump(2), error);
}

Installer::Installer(const std::string& downloadsFolder, const std::string& emulatorsFolder)
    : folder_(downloadsFolder)
    , emulators_(emulatorsFolder)
    , imageOpener_(systemImageOpener())
{
    // Its own records are never package content
    protectedFolders_.push_back(downloadsFolder);
}

ImageOpener systemImageOpener()
{
    ImageOpener opener;
#ifdef __APPLE__
    opener.attach = [](const std::string& image, const std::string& mountPoint, std::string& error) {
        // Full paths only, so neither can be taken for an option
        if (image.empty() || image[0] != '/' || mountPoint.empty() || mountPoint[0] != '/') {
            error = "Could not open the disk image";
            return false;
        }
        if (hdiutil({ "attach", "-readonly", "-nobrowse", "-noautoopen", "-mountpoint", mountPoint, image }, 300)) return true;
        error = "Could not open the disk image";
        return false;
    };
    opener.detach = [](const std::string& mountPoint) {
        if (!hdiutil({ "detach", mountPoint }, 120) && !hdiutil({ "detach", "-force", mountPoint }, 120)) {
            LOG_WARNING("Downloads", "Could not detach the disk image at " + mountPoint);
        }
    };
#endif
    return opener;
}

bool isElfFile(const std::string& path)
{
    std::ifstream file(fs::u8path(path), std::ios::binary);
    char magic[4] = {};
    file.read(magic, 4);
    return file.gcount() == 4 && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

bool isDiskImage(const std::string& path)
{
    std::ifstream file(fs::u8path(path), std::ios::binary | std::ios::ate);
    std::streamoff size = file ? static_cast<std::streamoff>(file.tellg()) : 0;
    if (size < 512) return false;
    file.seekg(size - 512);
    char magic[4] = {};
    file.read(magic, 4);
    return file.gcount() == 4 && magic[0] == 'k' && magic[1] == 'o' && magic[2] == 'l' && magic[3] == 'y';
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
        // A link only to somewhere inside the emulator's own folder, and never on Windows
        if (!file.link.empty()) {
#ifdef _WIN32
            return "Links aren't installed on Windows: " + file.path;
#else
            if (!isSafeLink(file.path.substr(entry->folder.size() + 1), file.link)) {
                return "Refusing a link that leads outside emulators/" + entry->folder + ": " + file.path;
            }
#endif
        }
    }
    else if (kind == "core" && isCoreId(id)) {
        if (file.path != "RetroArch/cores/" + coreLibrary(id.substr(5), platform()) || !file.link.empty()) {
            return "Cores may only write their own RetroArch core library";
        }
    }
    else {
        return "Only emulators of RetroFE's built-in list may install programs";
    }
    if (file.mode > 0777) return "Refusing special permissions for " + file.path;
#ifndef _WIN32
    // Nothing is written through a link inside the emulator's folder (one an
    // earlier version installed); the folder itself may be the user's link
    const fs::path relative = fromUtf8(file.path);
    fs::path parent = root;
    auto last = std::prev(relative.end());
    for (auto part = relative.begin(); part != relative.end() && part != last; ++part) {
        parent /= *part;
        if (part != relative.begin() && isLink(parent)) {
            return "Refusing to write through the link " + pathForMessage(parent.lexically_relative(root)) + ": " + file.path;
        }
    }
#endif
    for (const std::string& folder : protectedFolders_) {
        if (!folder.empty() && (isInside(destination, folder) || destination.lexically_normal() == fs::path(folder).lexically_normal())) {
            return "Refusing to write into " + pathForMessage(fs::path(folder).filename()) + ": " + file.path;
        }
    }
    return "";
}

std::map<std::string, Manifest> Installer::installed() const
{
    std::map<std::string, Manifest> result;
    std::error_code ec;
    for (auto it = fs::directory_iterator(fs::path(folder_) / "installed", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const auto& entry = *it;
        if (entry.path().extension() != ".json") continue;
        std::string filename, stem;
        if (!Library::narrowName(entry.path(), filename) || !Library::narrowName(entry.path().stem(), stem)) {
            LOG_WARNING("Downloads", "Cannot read a manifest whose path is not representable in the current code page");
            continue;
        }
        Manifest manifest;
        std::string error;
        if (!readManifest(filename, manifest, error)) {
            LOG_WARNING("Downloads", error);
            continue;
        }
        if (!manifest.removed && stem == manifest.id) {
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
            && (std::any_of(old.files.begin(), old.files.end(), [&](const InstalledFile& file) {
                return file.path.rfind(entry->folder + "/", 0) == 0;
            }));
        if (owns && old.removed) {
            std::set<std::string> left;
            for (const auto& file : old.files) left.insert(file.path);
            std::error_code ec;
            fs::path folder = fs::path(emulators_) / fromUtf8(entry->folder);
            for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!left.count(utf8(fs::path(entry->folder) / it->path().lexically_relative(folder)))) owns = false;
            }
            if (ec) { std::error_code existsError; if (fs::exists(folder, existsError) || existsError) owns = false; }
        }
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
        const std::string dll = coreLibrary(package.id.substr(5), platform());
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
        Staging files(staging, into, source, planned);
        const std::string form = emulator ? emulator->archive : "";
        if (form == "appimage") {
            if (!stageAppImage(downloads[i], files, emulator->program, error)) return false;
            continue;
        }
        if (form == "dmg") {
            if (!isDiskImage(downloads[i])) {
                error = files.published + " isn't a disk image";
                return false;
            }
            if (!imageOpener_.attach || !imageOpener_.detach) {
                error = "A disk image can only be installed on macOS";
                return false;
            }
            // Attached read-only in staging, the .app copied out, and always detached
            fs::path mount = fs::path(staging) / "mount";
            std::error_code ec;
            fs::create_directories(mount, ec);
            const std::string mountPoint = mount.string();
            if (!imageOpener_.attach(downloads[i], mountPoint, error)) {
                if (error.empty()) error = "Could not open " + files.published;
                fs::remove(mount, ec);
                return false;
            }
            struct Detach
            {
                const ImageOpener& opener;
                const std::string& at;
                ~Detach() { opener.detach(at); }
            } detach{ imageOpener_, mountPoint };
            if (!copyFromImage(mount, emulator->appFolder, files, stop_, error)) return false;
            continue;
        }
        uint64_t archiveOffset;
        if (Archive::detect(downloads[i], archiveOffset) == Archive::Form::None) {
            error = publishedName(source.url) + " isn't an archive RetroFE can unpack";
            return false;
        }
        if (!extractArchive(downloads[i], files, stop_, emulator ? emulator->topFolder : "", error)) {
            return false;
        }
    }

    if (emulator) {
        auto program = std::find_if(planned.rbegin(), planned.rend(), [&](const PlannedFile& file) {
            return file.file.path == emulator->folder + "/" + emulator->program;
        });
        if (program == planned.rend() || !program->file.link.empty()) {
            error = package.name + " archive does not contain " + emulator->program;
            return false;
        }
#ifndef _WIN32
        // The program runs, whatever the archive stored (still never setuid)
        if ((program->file.mode & 0100) == 0) {
            program->file.mode = (program->file.mode ? program->file.mode : 0644) | 0755;
            applyMode(fs::path(staging) / fromUtf8(program->staged), program->file.mode);
        }
#endif
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
            if (present(step.destination) && !matches(step.destination, old->second)) {
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
            if (!present(step.destination)) continue;
            fs::file_status there = statusOf(step.destination);
            if (!fs::is_regular_file(there) && !fs::is_symlink(there)) {
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
            else if (matches(step.destination, step.file)) {
                fs::remove(step.destination, rec);
                removeEmptyFolders(step.destination.parent_path(), emulators_);
            }
        }
        LOG_WARNING("Downloads", "Put back the files " + manifest.id + " " + manifest.version + " would have changed");
    };

    for (Step& step : steps) {
        if (present(step.staged)) {
            if (!moveFile(step.staged, step.destination, error)) {
                rollBack();
                error += ". Nothing was changed.";
                return false;
            }
            applyMode(step.destination, step.file.mode);
        }
        else if (!present(step.destination)) {
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

    // Every link, followed as the system will, leads inside its emulator's folder
    for (const InstalledFile& file : manifest.files) {
        if (file.link.empty()) continue;
        const Emulator* entry = manifest.kind == "emulator" ? findEmulator(manifest.id) : nullptr;
        if (!entry || file.path.size() <= entry->folder.size() + 1
            || !staysInside(fs::path(emulators_) / fromUtf8(entry->folder), file.path.substr(entry->folder.size() + 1))) {
            rollBack();
            error = "Refusing a link that leads outside its folder: " + file.path + ". Nothing was changed.";
            return false;
        }
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
        if (!matches(path, old)) continue;
        fs::path original = packageBackups / old.backup / fromUtf8(old.path);
        if (!old.backup.empty() && present(original)) {
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
    for (auto it = fs::directory_iterator(packageBackups, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const auto& entry = *it;
        std::string name;
        if (!Library::narrowName(entry.path().filename(), name)) continue;
        if (!needed.count(name)) {
            fs::remove_all(entry.path(), ec);
        }
    }

    if (changed) installMessage_ = " Replaced " + std::to_string(changed) + (changed == 1 ? " changed file; backups are in " : " changed files; backups are in ") + pathForMessage(backups) + ".";
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
        if (!present(path)) {
            continue;
        }
        // Leave anything that's been changed since it was installed (a link is
        // compared as a link, never followed)
        if (!matches(path, file)) {
            removal.kept++;
            continue;
        }
        // Put back what it replaced, or take it away
        fs::path original = packageBackups / file.backup / fromUtf8(file.path);
        if (!file.backup.empty() && present(original)) {
            if (copyOver(original, path)) {
                removal.restored++;
            }
            else {
                allRestored = false;
            }
            continue;
        }
        fs::remove(path, ec);   // a link itself, not what it leads to
        removeEmptyFolders(path.parent_path(), emulators_);
    }

    // What's still in the emulator's folder: its settings and saves, files it changed
    std::error_code ec;
    if (const Emulator* entry = manifest.kind == "emulator" ? findEmulator(id) : nullptr) {
        fs::path folder = fs::path(emulators_) / fromUtf8(entry->folder);
        for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_symlink(ec) || it->is_regular_file(ec)) removal.left.push_back(utf8(fs::path(entry->folder) / it->path().lexically_relative(folder)));
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
        for (const auto& path : removal.left) { InstalledFile file; file.path = path; left.files.push_back(file); }
        if (const Emulator* entry = findEmulator(id)) {
            fs::path folder = fs::path(emulators_) / fromUtf8(entry->folder);
            for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_symlink(ec) && it->is_directory(ec)) {
                    InstalledFile file;
                    file.path = utf8(fs::path(entry->folder) / it->path().lexically_relative(folder));
                    left.files.push_back(file);
                }
            }
        }
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
    for (auto it = fs::directory_iterator(fs::path(folder_) / "staging", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const auto& entry = *it;
        std::string path, name;
        if (!Library::narrowName(entry.path(), path) || !Library::narrowName(entry.path().filename(), name)) {
            LOG_WARNING("Downloads", "Cannot recover a staging path not representable in the current code page");
            continue;
        }
        if (!entry.is_directory(ec)) continue;
        if (fs::exists(entry.path() / ".ready", ec)) {
            std::string error;
            if (commit(path, error)) {
                LOG_NOTICE("Downloads", "Finished an interrupted install of " + name);
                continue;
            }
            // commit() has put back anything it changed; don't try again every time
            LOG_ERROR("Downloads", "Could not finish installing " + name + ": " + error);
        }
        fs::remove_all(entry.path(), ec);
    }
}

}

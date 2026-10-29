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
#include "GameImport.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace
{
    // Publish without replacing a file that appeared while we were copying.
    bool publish(const fs::path& from, const fs::path& into)
    {
#ifdef _WIN32
        if (MoveFileExW(from.c_str(), into.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
        auto error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) return false;
        throw fs::filesystem_error("Could not finish the copy", from, into, std::error_code(static_cast<int>(error), std::system_category()));
#else
#if defined(__linux__) && defined(SYS_renameat2)
        if (syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, into.c_str(), 1 /* RENAME_NOREPLACE */) == 0) return true;
        if (errno == EEXIST) return false;
        if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP)
            throw fs::filesystem_error("Could not finish the copy", from, into, std::error_code(errno, std::generic_category()));
#endif
        std::error_code ec;
        fs::create_hard_link(from, into, ec);
        if (ec == std::errc::file_exists) return false;
        if (ec) throw fs::filesystem_error("Could not finish the copy", from, into, ec);
        fs::remove(from);
        return true;
#endif
    }

    std::string lower(std::string s)
    {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return s;
    }
    bool below(const fs::path& path)
    {
        if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
        for (const auto& part : path) if (part == "..") return false;
        return true;
    }
    std::vector<fs::path> references(const fs::path& path)
    {
        std::vector<fs::path> result;
        std::string ext = lower(path.extension().string());
        if (ext != ".cue" && ext != ".gdi" && ext != ".m3u") return result;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream row(line);
            std::string name, word;
            if (ext == ".cue") {
                row >> word;
                if (lower(word) != "file") continue;
                row >> std::quoted(name);
            }
            else if (ext == ".gdi") {
                int track, type, sector;
                uint64_t lba;
                if (!(row >> track >> lba >> type >> sector >> std::quoted(name))) continue;
            }
            else {
                size_t first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos || line[first] == '#') continue;
                name = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
            }
            std::replace(name.begin(), name.end(), '\\', '/');
            if (!name.empty()) result.emplace_back(name);
        }
        return result;
    }
}

Manager::GameImport::~GameImport() { stop(); }
void Manager::GameImport::stop()
{
    stop_ = true;
    if (worker_.joinable()) worker_.join();
}
void Manager::GameImport::reset()
{
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    unsigned generation = status_.generation + 1;
    status_ = {};
    status_.generation = generation;
}
Manager::GameImport::Status Manager::GameImport::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
void Manager::GameImport::start(Request request, bool copy)
{
    stop();
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        unsigned generation = status_.generation + 1;
        size_t found = copy ? status_.found : 0;
        status_ = {};
        status_.found = found;
        status_.generation = generation;
        status_.running = true;
        status_.copying = copy;
        status_.message = "Counting games...";
    }
    worker_ = std::thread([this, request, copy] {
        Status done = run(request, copy, stop_, {}, [this](const std::string& message) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.message = message;
            status_.generation++;
        });
        std::lock_guard<std::mutex> lock(mutex_);
        done.generation = status_.generation + 1;
        status_ = std::move(done);
    });
}

Manager::GameImport::Status Manager::GameImport::run(const Request& r, bool copy, std::atomic<bool>& stop,
                                                     Space space, Progress progress)
{
    Status result;
    result.copying = copy;
    std::vector<std::string> notes;
    fs::path partial;
    try {
        if (!fs::is_directory(r.from)) throw std::runtime_error("Choose a folder that can be read.");
        std::vector<fs::path> games;
        auto accept = [&](const fs::directory_entry& entry) {
            if (stop || entry.is_symlink() || !entry.is_regular_file()) return;
            std::string ext = entry.path().extension().string();
            if (!ext.empty()) ext.erase(0, 1);
            if (std::find(r.extensions.begin(), r.extensions.end(), ext) != r.extensions.end()) games.push_back(entry.path());
        };
        if (r.subfolders) {
            for (fs::recursive_directory_iterator it(r.from), end; it != end && !stop; ++it) accept(*it);
        }
        else for (const auto& entry : fs::directory_iterator(r.from)) { if (stop) break; accept(entry); }
        std::sort(games.begin(), games.end());
        result.found = games.size();
        if (!copy) { result.message = std::to_string(result.found) + (result.found == 1 ? " game found" : " games found"); return result; }
        if (stop) { result.message = "Stopped. No games were copied."; return result; }
        struct File { fs::path source, target; bool game; };
        std::vector<File> files;
        std::map<std::string, fs::path> targets;
        std::set<std::pair<fs::path, fs::path>> visited;
        std::function<void(fs::path, fs::path, bool)> add = [&](fs::path source, fs::path target, bool game) {
            if (stop) return;
            source = source.lexically_normal();
            target = target.lexically_normal();
            if (!visited.emplace(source, target).second) {
                if (game) for (auto& file : files) if (file.source == source && file.target == target) file.game = true;
                return;
            }
            if (!fs::is_regular_file(source) || fs::is_symlink(source)) {
                result.missing++; if (notes.size() < 6) notes.push_back("Missing: " + source.string()); return;
            }
            // Windows filenames collide regardless of case, also when preparing on Linux.
            std::string key = lower(target.lexically_normal().generic_string());
            auto existing = targets.find(key);
            if (existing != targets.end()) {
                if (fs::file_size(existing->second) == fs::file_size(source)) result.already++;
                else { result.conflicts++; if (notes.size() < 6) notes.push_back("Kept existing, skipped: " + source.string()); }
                return;
            }
            targets[key] = source;
            files.push_back({ source, target, game });
            for (const auto& ref : references(source)) {
                if (!below(ref)) {
                    result.missing++; if (notes.size() < 6) notes.push_back("Reference outside its folder: " + ref.string()); continue;
                }
                fs::path candidate = source.parent_path() / ref;
                fs::path relative = fs::weakly_canonical(candidate).lexically_relative(fs::weakly_canonical(source.parent_path()));
                if (!below(relative)) {
                    result.missing++; if (notes.size() < 6) notes.push_back("Reference outside its folder: " + ref.string()); continue;
                }
                add(candidate, target.parent_path() / ref, false);
            }
        };
        for (const auto& game : games) add(game, r.into / (r.hierarchy ? game.lexically_relative(r.from) : game.filename()), true);
        uintmax_t bytes = 0;
        for (const auto& file : files) if (!stop && !fs::exists(file.target)) {
            uintmax_t size = fs::file_size(file.source);
            if (UINTMAX_MAX - bytes < size) throw std::runtime_error("The files are too large to copy.");
            bytes += size;
        }
        fs::path drive = fs::absolute(r.into);
        while (!fs::exists(drive) && drive.has_parent_path() && drive != drive.parent_path()) drive = drive.parent_path();
        uintmax_t available = stop ? 0 : space ? space(drive) : fs::space(drive).available;
        if (!stop && bytes > available) throw std::runtime_error("Not enough free space in the destination. Need " + std::to_string(bytes) + " bytes; " + std::to_string(available) + " available.");
        size_t at = 0;
        for (const auto& file : files) {
            if (stop) break;
            std::string message = "Copying " + std::to_string(++at) + " of " + std::to_string(files.size()) + ": " + file.source.filename().string();
            if (progress) progress(message);
            if (fs::exists(file.target)) {
                if (fs::is_regular_file(file.target) && fs::file_size(file.target) == fs::file_size(file.source)) result.already++;
                else { result.conflicts++; if (notes.size() < 6) notes.push_back("Kept " + file.target.string() + "; skipped " + file.source.string()); }
                continue;
            }
            fs::path relative = fs::weakly_canonical(file.target).lexically_relative(fs::weakly_canonical(r.into));
            if (!below(relative)) throw std::runtime_error("Destination leaves the games folder: " + file.target.string());
            fs::create_directories(file.target.parent_path());
            fs::path temporary = file.target;
            temporary += ".retrofe-part";
            if (fs::exists(temporary)) throw std::runtime_error("A partial file already exists: " + temporary.string());
            partial = temporary;
            std::ifstream in(file.source, std::ios::binary);
            std::ofstream out(partial, std::ios::binary);
            if (!in || !out) throw std::runtime_error("Could not open " + file.source.string() + " for copying.");
            std::array<char, 256 * 1024> buffer;
            while (!stop && in) {
                in.read(buffer.data(), buffer.size());
                out.write(buffer.data(), in.gcount());
                if (!out || in.bad()) throw std::runtime_error("Could not copy " + file.source.string());
                if (progress) progress(message);
            }
            out.close();
            if (!out) throw std::runtime_error("Could not finish " + file.target.string());
            if (stop) { fs::remove(partial); partial.clear(); break; }
            if (!publish(partial, file.target)) {
                fs::remove(partial); partial.clear();
                result.conflicts++;
                if (notes.size() < 6) notes.push_back("Kept " + file.target.string() + "; skipped " + file.source.string());
                continue;
            }
            partial.clear();
            result.changed = true;
            if (file.game) result.copied++;
        }
        result.message = std::string(stop ? "Stopped. " : "") + "Copied " + std::to_string(result.copied) + (result.copied == 1 ? " game into " : " games into ") + (r.destinationLabel.empty() ? r.into.string() : r.destinationLabel)
            + "; " + std::to_string(result.already) + (result.already == 1 ? " was already there; " : " were already there; ") + std::to_string(result.conflicts)
            + " conflicts; " + std::to_string(result.missing) + " missing references.";
    }
    catch (const std::exception& e) {
        result.message = e.what();
        if (!partial.empty()) { std::error_code ec; fs::remove(partial, ec); }
    }
    for (const auto& note : notes) result.message += " " + note;
    return result;
}

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
#include "Backups.h"
#include "Files.h"
#include "../Utility/Log.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace Library
{

namespace
{
    const char* const absentSuffix = ".absent";
}

Backups::Backups(const std::string& root)
    : root_(normalPath(root))
    , folder_(narrow((fs::path(normalPath(root)) / "manage" / "backups").make_preferred()))
{
}

std::string Backups::backupPath(const std::string& path) const
{
    fs::path target(normalPath(path));
    fs::path relative = target.lexically_relative(fs::path(root_));
    if (relative.empty() || *relative.begin() == "..") {
        // Outside the root: keep the whole path, with the drive as a folder
        std::string external;
        for (const fs::path& part : target.relative_path()) {
            external += (external.empty() ? "" : "/") + narrow(part);
        }
        std::string rootName = narrow(target.root_name());
        for (char& c : rootName) {
            if (c == ':' || c == '\\' || c == '/') {
                c = '_';
            }
        }
        relative = fs::path("_external") / (rootName.empty() ? "root" : rootName) / external;
    }
    return narrow((fs::path(folder_) / relative).make_preferred());
}

bool Backups::hasOriginal(const std::string& path) const
{
    std::error_code ec;
    std::string backup = backupPath(path);
    return fs::exists(backup, ec) || fs::exists(backup + absentSuffix, ec);
}

bool Backups::beforeChange(const std::string& path, std::string& error)
{
    if (hasOriginal(path)) {
        return true;
    }
    std::error_code ec;
    std::string backup = backupPath(path);
    fs::create_directories(fs::path(backup).parent_path(), ec);
    if (fs::is_regular_file(path, ec)) {
        if (!copyFileAtomic(path, backup, error)) {
            error = "Could not keep a copy of " + path + " before changing it";
            return false;
        }
    }
    else if (!writeFileAtomic(backup + absentSuffix, "", error)) {
        return false;
    }
    LOG_INFO("Library", "Kept the original of " + path + " in " + backup);
    return true;
}

namespace
{
    bool sameContent(const std::string& a, const std::string& b)
    {
        std::error_code ec;
        if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) {
            return false;
        }
        std::ifstream left(a, std::ios::binary);
        std::ifstream right(b, std::ios::binary);
        if (!left || !right) {
            return false;
        }
        std::vector<char> x(1 << 16);
        std::vector<char> y(1 << 16);
        while (left && right) {
            left.read(x.data(), static_cast<std::streamsize>(x.size()));
            right.read(y.data(), static_cast<std::streamsize>(y.size()));
            if (left.gcount() != right.gcount() || !std::equal(x.begin(), x.begin() + left.gcount(), y.begin())) {
                return false;
            }
        }
        return true;
    }

    // Moves a file, copying (flushed) then deleting when it has to cross drives
    bool moveFile(const std::string& from, const std::string& to, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(fs::path(to).parent_path(), ec);
        fs::rename(from, to, ec);
        if (!ec) {
            return true;
        }
        if (!copyFileAtomic(from, to, error)) {
            return false;
        }
        fs::remove(from, ec);
        if (ec) {
            error = "Could not delete " + from;
            return false;
        }
        return true;
    }
}

bool Backups::keepIfChanged(const std::string& path, std::string& error)
{
    // The live file is about to go (deleted or overwritten). Once an original
    // is kept, it may go only if it's that original; anything else (an
    // earlier Manage change, a content update, a file copied in by hand) is
    // kept beside the original as "<name> (2).<ext>", "(3)", ...
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return true;
    }
    if (isOriginal(path)) {
        return true;
    }
    std::string copy = numberedCopyPath(path);
    if (copy.empty()) {
        error = "Too many kept copies of " + path;
        return false;
    }
    if (!copyFileAtomic(path, copy, error)) {
        return false;
    }
    LOG_INFO("Library", "Kept the changed " + path + " as " + copy);
    return true;
}

bool Backups::isOriginal(const std::string& path) const
{
    std::error_code ec;
    std::string backup = backupPath(path);
    return fs::exists(backup, ec) && sameContent(path, backup);
}

std::string Backups::numberedCopyPath(const std::string& path) const
{
    std::error_code ec;
    fs::path original(backupPath(path));
    for (int n = 2; n < 10000; n++) {
        fs::path numbered = original.parent_path()
            / (narrow(original.stem()) + " (" + std::to_string(n) + ")" + narrow(original.extension()));
        if (!fs::exists(numbered, ec)) {
            return narrow(numbered);
        }
    }
    return "";
}

bool Backups::removeKeepingOriginal(const std::string& path, std::string& error)
{
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return true;
    }
    if (!hasOriginal(path)) {
        // First change: the file itself becomes the original (a cheap rename for big media)
        return moveFile(path, backupPath(path), error);
    }
    if (!isOriginal(path)) {
        // Changed since the original was kept: move it beside the original
        std::string copy = numberedCopyPath(path);
        if (copy.empty()) {
            error = "Too many kept copies of " + path;
            return false;
        }
        LOG_INFO("Library", "Kept the changed " + path + " as " + copy);
        return moveFile(path, copy, error);
    }
    fs::remove(path, ec);
    if (ec) {
        error = "Could not delete " + path;
        return false;
    }
    return true;
}

bool Backups::restore(const std::string& path, std::string& error)
{
    std::error_code ec;
    std::string backup = backupPath(path);
    bool original = fs::exists(backup, ec);
    bool absent = fs::exists(backup + absentSuffix, ec);
    if (!original && !absent) {
        error = "No original was kept for " + path;
        return false;
    }
    // What's there now is kept too, unless it is the original
    if (!keepIfChanged(path, error)) {
        return false;
    }
    if (original) {
        return copyFileAtomic(backup, path, error);
    }
    fs::remove(path, ec);
    if (ec) {
        error = "Could not delete " + path;
        return false;
    }
    return true;
}

}

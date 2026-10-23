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
#include <filesystem>
#include <system_error>

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

bool Backups::removeKeepingOriginal(const std::string& path, std::string& error)
{
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return true;
    }
    if (!hasOriginal(path)) {
        std::string backup = backupPath(path);
        fs::create_directories(fs::path(backup).parent_path(), ec);
        fs::rename(path, backup, ec);
        if (!ec) {
            return true;
        }
        // Another drive: copy, then delete
        if (!beforeChange(path, error)) {
            return false;
        }
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
    if (fs::exists(backup, ec)) {
        return copyFileAtomic(backup, path, error);
    }
    if (fs::exists(backup + absentSuffix, ec)) {
        fs::remove(path, ec);
        if (ec) {
            error = "Could not delete " + path;
            return false;
        }
        return true;
    }
    error = "No original was kept for " + path;
    return false;
}

}

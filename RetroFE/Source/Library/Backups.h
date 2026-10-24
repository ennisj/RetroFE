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
#pragma once

#include <string>

namespace Library
{
    // Keeps the original of every file the Manage screens change, taken just
    // before the first change, so it can be put back ("Reset").
    //
    // Originals live in <root>/manage/backups/ at the file's path relative to
    // the root, e.g. collections/MAME/settings.conf ->
    // <root>/manage/backups/collections/MAME/settings.conf. Files outside the
    // root (media under an external baseMediaPath) go under
    // manage/backups/_external/<drive or root>/<path>. A file that didn't exist
    // before the first change is recorded by an empty "<name>.absent" marker,
    // so a reset deletes it. RetroFE never reads anything under manage/.
    class Backups
    {
    public:
        explicit Backups(const std::string& root);

        const std::string& folder() const { return folder_; }

        // Where the original of path is (or would be) kept
        std::string backupPath(const std::string& path) const;

        // True once an original (or an "absent" marker) has been recorded
        bool hasOriginal(const std::string& path) const;

        // Call before changing or creating path: records the original the first time only
        bool beforeChange(const std::string& path, std::string& error);

        // Call instead of deleting path: the first time the file is moved into
        // the backups (cheap for big media files). Later, it's deleted only if
        // it's identical to the kept original; otherwise it's kept as a
        // numbered copy beside it ("name (2).ext") first. Nothing that differs
        // from what's kept is ever lost.
        bool removeKeepingOriginal(const std::string& path, std::string& error);

        // Puts the original back (or deletes the file if it didn't exist);
        // the current file is kept as a numbered copy if it differs
        bool restore(const std::string& path, std::string& error);

        // Keeps a numbered copy of path unless it's identical to its kept original
        bool keepIfChanged(const std::string& path, std::string& error);

        // True when an original is kept and path is byte for byte the same
        bool isOriginal(const std::string& path) const;

        // True when backupFile (a file in the backups) is one of the numbered
        // copies made here ("name (2).ext"), not the original of a file that is
        // itself named like that: each folder of the backups lists the copies
        // made in it in a ".numbered" file
        bool isNumberedCopy(const std::string& backupFile) const;

    private:
        std::string numberedCopyPath(const std::string& path) const;
        void recordNumberedCopy(const std::string& copy);

        std::string root_;
        std::string folder_;
    };
}

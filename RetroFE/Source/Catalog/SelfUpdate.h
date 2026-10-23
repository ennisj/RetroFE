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

#include "Package.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

// RetroFE updating itself from an "app" package (see docs/design/catalog-sources.md).
//
// The package's archive holds retrofe/ (the program: exe, DLLs, plugins) and
// optionally layouts/<name>/. retrofe/ maps onto the program folder and
// layouts/<name>/ onto <root>/layouts/<name>/; nothing else in the archive is
// installed, and nothing outside those folders is touched.
//
// Windows can't overwrite a running exe or a loaded DLL, but it can rename
// them. So the update is:
//   1. Extract into catalog/update/staged/ and check it (it must contain the
//      running program's file; layouts may not hold programs).
//   2. Copy every file about to be replaced or removed to
//      catalog/backups/retrofe/<running version>/.
//   3. Write catalog/update/journal.json, then for each file rename the old
//      one to <name>.old and move the new one in, the program file last.
//      If any step fails, every step so far is undone.
//   4. Mark the journal done, record the files in catalog/installed/retrofe.json,
//      and list the .old files in catalog/update/leftovers.json.
//   5. RetroFE starts the new version and exits; on its next start recover()
//      deletes the .old files (once the old process has let go of them).
// If RetroFE stops in the middle of step 3, recover() puts the old files back
// on the next start and asks for a restart, so the old version runs again.
namespace Catalog
{
    // Where RetroFE's own files are
    struct AppInstall
    {
        std::string root;        // the RetroFE root
        std::string program;     // the folder holding the program and its libraries (<root>/retrofe on Windows)
        std::string executable;  // the program file that's running
        std::string version;     // the running version
    };

    // The running program's file, or "" if it can't be found. Ask before an
    // update renames it.
    std::string currentExecutable();

    class SelfUpdater
    {
    public:
        SelfUpdater(const std::string& catalogFolder, const AppInstall& app);

        // Installs the package from its downloaded archive. On failure nothing
        // is changed and error says why.
        bool apply(const Package& package, const std::string& archive, std::string& error);

        // At start: finishes or undoes an interrupted update, and deletes what
        // the last one set aside. True if files were put back, so RetroFE
        // should restart to run the version that's now there.
        bool recover();

        // "" if RetroFE can update itself in this install, otherwise why not
        std::string problem() const;

        // While *stop is set, extracting gives up (safe until files are swapped)
        void setStopFlag(const std::atomic<bool>* stop) { stop_ = stop; }

        // For tests: how files are renamed and deleted, to act like files in use
        std::function<bool(const std::filesystem::path& from, const std::filesystem::path& to)> renameFile;
        std::function<bool(const std::filesystem::path& file)> removeFile;
        std::function<void(int milliseconds)> sleep;
        int removeTries = 20;    // rounds of trying to delete set-aside files at start, 250 ms apart

    private:
        std::filesystem::path updateFolder() const;
        bool place(const std::filesystem::path& staged, const std::filesystem::path& destination, std::string& error);
        // Finishes a done update or undoes an unfinished one; false if files couldn't be put back
        bool finishOrUndo(bool& putBack);
        void deleteLeftovers(int tries);
        bool isOurs(const std::filesystem::path& path) const;

        std::string catalog_;
        AppInstall app_;
        const std::atomic<bool>* stop_ = nullptr;
    };

    // After an update RetroFE exits and starts the new version: the main
    // loop asks for it, and main() starts it once everything is shut down
    void requestRelaunch(const std::string& executable);
    bool relaunchPending();
    bool takeRelaunch(std::string& executable);

    // Starts executable with this process's arguments (argv is used off Windows)
    bool relaunch(const std::string& executable, char** argv, std::string& error);
}

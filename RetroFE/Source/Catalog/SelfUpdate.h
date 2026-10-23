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
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// RetroFE updating itself from an "app" package (see docs/design/catalog-sources.md,
// "Updating RetroFE in place").
//
// The archive holds retrofe/ (the program) and optionally layouts/<name>/.
// retrofe/ becomes the program folder and layouts/<name>/ goes to
// <root>/layouts/<name>/; nothing else is installed or touched.
//
//   1. Extract into catalog/update/staged/ and check it.
//   2. Back up what will be replaced or removed to catalog/backups/retrofe/<running version>/.
//   3. Write the journal <root>/retrofe.update.json (beside the program folder,
//      where it's found before anything is configured), state "preparing":
//      every name the update will create or set aside, with the SHA-256 of
//      each file placed and replaced. Only names it records are ever deleted.
//   4. Prepare everything next to where it goes, so the swap is renames only:
//      <root>/retrofe.new/ is the complete new program folder (other files
//      hard-linked, or copied), and each changed layout file gets <file>.new.
//   5. Journal "swapping", then swap: layout files (rename the old one to
//      <file>.old, the .new in), then the program folder in two renames, each
//      retried: retrofe -> retrofe.old, retrofe.new -> retrofe. That last
//      rename is the commit point. If the folder can't be renamed (something
//      has a file in it open), the program's changed files are swapped one by
//      one the same way instead, the program file last.
//   6. Mark the journal swapped, record catalog/installed/retrofe.json, and
//      delete what was set aside once nothing holds it (at the next start).
// recoverAtStart(), first thing in main(), finishes or undoes an interrupted
// swap, touching only files whose SHA-256 is what the journal says, and gives
// up after a few attempts, leaving the journal as retrofe.update.damaged.json.
namespace Catalog
{
    // Where RetroFE's own files are. Paths are UTF-8.
    struct AppInstall
    {
        std::string root;        // the RetroFE root
        std::string program;     // the folder holding the program and its libraries (<root>/retrofe on Windows)
        std::string executable;  // the program file that's running
        std::string version;     // the running version
    };

    // The running program's file (UTF-8), or "" if it can't be found. Ask
    // before an update renames it.
    std::string currentExecutable();

    class SelfUpdater
    {
    public:
        // catalogFolder may be "" when it isn't known yet (at start); then the
        // installed record isn't written and backups aren't named
        SelfUpdater(const std::string& catalogFolder, const AppInstall& app);

        // Installs the package from its downloaded archive. On failure nothing
        // is changed and error says why.
        bool apply(const Package& package, const std::string& archive, std::string& error);

        // Finishes or undoes an interrupted update, records a finished one,
        // and deletes what the last one set aside. True if RetroFE's program
        // files were put back, so it should restart to run what's there now.
        bool recover();

        // The same at start, before anything else: without waiting for
        // set-aside files or recording the install (the catalog isn't known yet).
        // Only these count towards maxRecoveryAttempts.
        bool recoverAtStartup();

        // After apply() left the program folder missing (both folder renames
        // kept failing): tries again to put a program folder in place, the old
        // one or else the complete new one. True once there is one.
        bool repairProgramFolder();
        bool programFolderMissing() const;
        // Whether the last recovery finished the update rather than undoing it
        bool finishedForward() const { return finishedForward_; }

        // "" if RetroFE can update itself in this install, otherwise why not
        std::string problem() const;

        // Something for the user from the last recover(), e.g. that it gave up
        const std::string& message() const { return message_; }

        // Checked while extracting and preparing: true gives up (nothing has been swapped yet)
        std::function<bool()> shouldStop;

        // For tests: how files and folders are renamed and files deleted,
        // to act like files in use
        std::function<bool(const std::filesystem::path& from, const std::filesystem::path& to)> renameFile;
        std::function<bool(const std::filesystem::path& file)> removeFile;
        std::function<void(int milliseconds)> sleep;
        int removeTries = 20;    // rounds of deleting set-aside files at start, 250 ms apart
        int renameTries = 10;    // tries of each folder rename, 200 ms apart (e.g. while a virus scanner looks at new files)
        bool folderSwap = true;  // for tests: false swaps program files one by one

        // Where log lines go; the Catalog's log by default (before logging is
        // set up, main() collects them instead)
        std::function<void(const std::string& level, const std::string& text)> log;

        static constexpr int maxRecoveryAttempts = 3;

    private:
        struct Plan;
        std::filesystem::path journalPath() const;
        bool finishOrUndo(bool& putBack, bool countAttempt);
        void deleteLeftovers(int tries);
        void recordInstalled();
        bool isOurs(const std::filesystem::path& path) const;
        bool isOldFolder(const std::filesystem::path& path) const;
        bool isProgramLayout() const;
        bool persist(const std::function<bool()>& attempt);
        bool removeTree(const std::filesystem::path& folder);
        bool stopped() const { return shouldStop && shouldStop(); }

        std::filesystem::path catalog_;
        std::filesystem::path root_;
        std::filesystem::path program_;
        std::filesystem::path executable_;
        std::string version_;
        std::string message_;
        bool finishedForward_ = false;
    };

    // What main() learns from recoverAtStart()
    struct StartupRecovery
    {
        bool restart = false;               // program files were put back: start again
        std::string message;                // for the user, e.g. where the backup is
        std::vector<std::pair<std::string, std::string>> log;   // level, text: for the log once it's set up
    };

    // First thing in main(): deals with an interrupted update of the program
    // in executable's folder, before anything else loads
    StartupRecovery recoverAtStart(const std::string& executable);

    // Last thing in main(): if an update left no program folder, one more try
    // at putting one back; "" if there is one, otherwise what to tell the user
    std::string checkProgramFolderAtExit(const std::string& executable);

    // A message box that closes by itself after timeoutSeconds (0: waits for
    // OK), so a cabinet without a keyboard isn't stuck on it. Windows only;
    // elsewhere it's written to stderr.
    void showMessage(const std::string& title, const std::string& text, int timeoutSeconds);

    // After an update RetroFE exits and starts the new version: the main
    // loop asks for it, and main() starts it once everything is shut down
    void requestRelaunch(const std::string& executable);
    bool relaunchPending();
    bool takeRelaunch(std::string& executable);

    // Starts executable (UTF-8) with this process's arguments, in the folder
    // RetroFE was started in (argv is used off Windows)
    bool relaunch(const std::string& executable, char** argv, std::string& error);
}

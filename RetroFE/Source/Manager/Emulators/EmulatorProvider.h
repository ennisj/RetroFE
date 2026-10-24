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

#include "Launchers.h"
#include "../EditBuffer.h"
#include "../../Library/Edits.h"
#include "../Provider.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

// The Emulators section (prefix "emulator"): RetroFE has no emulator entity,
// so these screens are about launchers (an executable plus arguments).
//
// Screens and parameters:
//   emulators          the list: emulator:query, emulator:search rows (id + scope),
//                      emulator:unresolved rows (names used but not defined)
//   emulator           one launcher: param id (the name a launch looks up) and
//                      scope (global, collection, local; default global)
//   emulator-users     the same launcher's systems and games (emulator:systems,
//                      emulator:games rows, opening the system / game screens)
//   emulator-new       add a launcher; param name prefills the name (from an
//                      unresolved row), params from + fromScope copy a launcher
//   emulator-edit      change a launcher in place (id + scope): program,
//                      arguments, and for global launchers the working folder
//                      and restart; Save / Reset / Put back the original
//   emulator-rename    rename a global launcher and every reference to it,
//                      with a preview of the files that change
//
// Writes go through Library::Editor::addLauncher / removeLauncher /
// editLauncher / renameLauncher.
class EmulatorProvider : public Manager::Provider
{
public:
    explicit EmulatorProvider(const Manager::Services& services);

    std::string prefix() const override { return "emulator"; }
    void refresh() override;
    void enter(const Manager::Screen& screen) override;
    std::string leaving(const Manager::Screen& screen) override;
    void discard(const Manager::Screen& screen) override;
    unsigned generation() const override;

    std::string get(const std::string& key, const Manager::Screen& screen) override;
    void set(const std::string& key, const std::string& value, const Manager::Screen& screen) override;
    Forms::Rows rows(const std::string& key, size_t limit, const Manager::Screen& screen) override;
    std::vector<std::string> list(const std::string& key, const Manager::Screen& screen) override;
    std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Manager::Screen& screen) override;
    std::string actionProblem(const std::string& action, const Manager::Screen& screen) override;
    Forms::Model::Result runAction(const std::string& action, const Manager::Screen& screen) override;

private:
    struct Entry
    {
        int launcher = -1;        // index into the snapshot's launchers; -1 for one created since
        Library::LauncherScope scope = Library::LauncherScope::Global;
        Forms::Row row;
        std::string searchText;   // lower case
    };
    static Entry makeEntry(int launcher, const std::string& id, const std::string& name, Library::LauncherScope scope,
                           const std::string& collection, const std::string& executable,
                           Emulators::ExecutableCheck::State state, const std::string& users);
    void sortEntries();
    // After a write, before the library has a snapshot with it: show it in the list now
    void patchCreated(const std::string& name, const std::string& executable);
    void patchDeleted(Library::LauncherScope scope, const std::string& id);

    // One launcher's screen, worked out when it or the snapshot changes
    struct Details
    {
        std::shared_ptr<const Library::Index> index;
        std::string scope;
        std::string id;
        int launcher = -1;
        Emulators::ExecutableCheck executable;
        Emulators::Users users;
        std::vector<std::string> fileLines;
        bool genericFileBehind = false;   // a collection's launcher.<os>.conf with a launcher.conf beside it
        bool runsInRoot = false;          // with no currentDirectory it runs in RetroFE's folder (a bare cmd.exe)
    };

    // The latest snapshot; rebuilds the list when it changed. nullptr while there's none yet.
    const Library::Index* current();
    const Details* details(const Manager::Screen& screen);
    const Library::LauncherDef* launcher(const Manager::Screen& screen);
    std::string detailValue(const std::string& key, const Manager::Screen& screen);
    std::string waitingText() const;

    // emulator-new
    std::string draftKey(const Manager::Screen& screen) const;
    std::map<std::string, std::string> draftDefaults(const Manager::Screen& screen);
    std::string draftExecutable();   // as it would be written
    std::string draftArguments();
    std::string createProblem();
    Forms::Model::Result create();

    // emulator-edit: the buffer holds the file's values (read from disk, so
    // a save shows at once, before the index catches up)
    std::string editKey(const Manager::Screen& screen) const;
    bool syncEdit(const Manager::Screen& screen);   // false while the launcher can't be found
    void loadEdit(const Manager::Screen& screen);
    std::string editExecutable();   // as it would be written
    std::string editArguments();
    Library::Editor::LauncherChange editChange();
    std::string saveProblem(const Manager::Screen& screen);
    Forms::Model::Result saveEdit(const Manager::Screen& screen);
    Forms::Model::Result restoreEdit(const Manager::Screen& screen);

    // emulator-rename
    const Library::Editor::RenamePlan& renamePlan(const Manager::Screen& screen);
    Forms::Model::Result rename(const Manager::Screen& screen);

    Manager::Services services_;
    std::shared_ptr<const Library::Index> snapshot_;
    std::vector<Entry> entries_;
    std::vector<Emulators::Unresolved> unresolved_;

    std::string query_;
    std::string searched_ = "\x01";
    const Library::Index* searchedIn_ = nullptr;
    std::vector<size_t> matches_;

    // A write was made and the list was patched for it; the library is still
    // building the snapshot that includes it
    bool updating_ = false;
    unsigned patches_ = 0;   // bumps generation() when the list is patched

    Details details_;
    Manager::EditBuffer draft_;
    std::string checkedExecutable_ = "\x01";
    Emulators::ExecutableCheck executableCheck_;

    Manager::EditBuffer edit_;
    std::string editFile_;
    bool editGlobal_ = false;
    bool editHasOriginal_ = false;      // a copy of the file is kept and the file differs from it
    std::vector<std::string> editOtherPlatforms_;   // launchers.<other os> folders with a file of the same name
    std::string editCheckedExecutable_ = "\x01";
    Emulators::ExecutableCheck editExecutableCheck_;

    std::string renameTo_;
    std::string renameFor_;
    std::string plannedKey_ = "\x01";
    const Library::Index* plannedIn_ = nullptr;
    Library::Editor::RenamePlan plan_;
    const Library::Index* scannedIn_ = nullptr;
    Library::Editor::RenameScan scan_;

public:
    // How many times the rename screen read the collections' settings (tests)
    unsigned renameScans() const { return scans_; }

private:
    unsigned scans_ = 0;
};

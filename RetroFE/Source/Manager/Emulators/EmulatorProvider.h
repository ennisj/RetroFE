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
//
// Existing launchers are read-only (their program and arguments are never
// changed); a copy can be changed before it's created. Writes go through
// Library::Editor::addLauncher / removeLauncher.
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
    std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Manager::Screen& screen) override;
    std::string actionProblem(const std::string& action, const Manager::Screen& screen) override;
    Forms::Model::Result runAction(const std::string& action, const Manager::Screen& screen) override;

private:
    struct Entry
    {
        int launcher = -1;
        Forms::Row row;
        std::string searchText;   // lower case
    };

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

    Manager::Services services_;
    std::shared_ptr<const Library::Index> snapshot_;
    std::vector<Entry> entries_;
    std::vector<Emulators::Unresolved> unresolved_;

    std::string query_;
    std::string searched_ = "\x01";
    const Library::Index* searchedIn_ = nullptr;
    std::vector<size_t> matches_;

    Details details_;
    Manager::EditBuffer draft_;
    std::string checkedExecutable_ = "\x01";
    Emulators::ExecutableCheck executableCheck_;
};

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
#include "EmulatorProvider.h"
#include "../../Library/Edits.h"
#include "../../Library/Files.h"
#include "../../Library/Indexer.h"
#include "../../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

using Library::LauncherScope;

namespace
{
    const char* const unsavedWarning =
        "The launcher hasn't been created yet. Create it, or leave again to discard it.";
    const char* const updatingText = "Updating the list...";

    // The last part of a path, either separator
    std::string fileName(const std::string& path)
    {
        size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    bool isNewForm(const Manager::Screen& screen) { return screen.form == "emulator-new"; }

    bool isLauncherForm(const Manager::Screen& screen)
    {
        return screen.form == "emulator" || screen.form == "emulator-users" || screen.form == "emulator-file";
    }

    // The draft field a binding edits: newName -> name, newWorkingFolder -> workingFolder
    std::string draftField(const std::string& key)
    {
        if (key.size() <= 3) {
            return key;
        }
        return std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(key[3])))) + key.substr(4);
    }

    int scopeOrder(LauncherScope scope)
    {
        return scope == LauncherScope::Global ? 0 : scope == LauncherScope::Collection ? 1 : 2;
    }
}

EmulatorProvider::EmulatorProvider(const Manager::Services& services)
    : services_(services)
{
}

void EmulatorProvider::refresh()
{
    snapshot_.reset();
    entries_.clear();
    unresolved_.clear();
    searchedIn_ = nullptr;
    details_ = Details();
    checkedExecutable_ = "\x01";
    updating_ = false;
}

unsigned EmulatorProvider::generation() const
{
    return (services_.library ? services_.library->status().generation : 0) + patches_;
}

void EmulatorProvider::patchCreated(const std::string& name, const std::string& executable)
{
    current();
    bool listed = std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.scope == LauncherScope::Global && Library::iequals(entry.row.id, name);
    });
    if (!listed) {
        std::string users = "Unused";
        for (auto it = unresolved_.begin(); it != unresolved_.end(); ++it) {
            if (it->name == name) {
                users = it->users.shortSummary();
                unresolved_.erase(it);
                break;
            }
        }
        entries_.push_back(makeEntry(-1, name, name, LauncherScope::Global, "", executable, executableCheck_.state, users));
        sortEntries();
    }
    updating_ = true;
    patches_++;
}

void EmulatorProvider::patchDeleted(LauncherScope scope, const std::string& id)
{
    current();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.scope == scope && entry.row.id == id;
    }), entries_.end());
    searchedIn_ = nullptr;
    updating_ = true;
    patches_++;
}

std::string EmulatorProvider::waitingText() const
{
    if (!services_.library) {
        return "The launchers can't be read here.";
    }
    Library::Indexer::Status status = services_.library->status();
    if (status.state == Library::Indexer::State::Failed) {
        return "The install couldn't be read: " + status.error;
    }
    return "Reading the launchers... " + std::to_string(static_cast<int>(status.progress * 100)) + "%";
}

const Library::Index* EmulatorProvider::current()
{
    if (!services_.library) {
        return nullptr;
    }
    std::shared_ptr<const Library::Index> index = services_.library->index();
    if (index == snapshot_) {
        return snapshot_.get();
    }
    snapshot_ = index;
    entries_.clear();
    unresolved_.clear();
    searchedIn_ = nullptr;
    checkedExecutable_ = "\x01";
    updating_ = false;   // a new snapshot includes every write made so far
    if (!index) {
        return nullptr;
    }

    // Once per snapshot: every launcher's row, with its program checked on disk
    std::vector<Emulators::Users> users = Emulators::allUsers(*index);
    for (size_t i = 0; i < index->launchers.size(); i++) {
        const Library::LauncherDef& def = index->launchers[i];
        Emulators::ExecutableCheck check = Emulators::checkExecutable(*index, def.executable);
        entries_.push_back(makeEntry(static_cast<int>(i), def.id, def.name, def.scope, def.collection,
                                     def.executable, check.state, users[i].shortSummary()));
    }
    sortEntries();
    unresolved_ = Emulators::unresolvedLaunchers(*index);
    return snapshot_.get();
}

EmulatorProvider::Entry EmulatorProvider::makeEntry(int launcher, const std::string& id, const std::string& name,
                                                    LauncherScope scope, const std::string& collection,
                                                    const std::string& executable, Emulators::ExecutableCheck::State state,
                                                    const std::string& users)
{
    std::string program = executable.empty() ? "no program" : fileName(executable);
    if (state == Emulators::ExecutableCheck::State::Missing) {
        program += " (missing)";
    }
    else if (state == Emulators::ExecutableCheck::State::WindowsOnly) {
        program += " (Windows only)";
    }
    // Short: details share a row with the name
    std::string where = scope == LauncherScope::Global ? "Global"
        : scope == LauncherScope::Collection ? collection + "'s own file"
        : collection + ", local";

    Entry entry;
    entry.launcher = launcher;
    entry.scope = scope;
    entry.row.id = id;
    entry.row.label = scope == LauncherScope::Local ? name : id;
    entry.row.detail = where + ", " + program + ", " + users;
    entry.row.params = { { "scope", Emulators::scopeKey(scope) } };
    entry.searchText = Library::toLower(id + "\n" + name + "\n" + collection + "\n" + program + "\n" + where);
    return entry;
}

void EmulatorProvider::sortEntries()
{
    std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        std::string la = Library::toLower(a.row.label);
        std::string lb = Library::toLower(b.row.label);
        if (la != lb) {
            return la < lb;
        }
        return scopeOrder(a.scope) < scopeOrder(b.scope);
    });
    searchedIn_ = nullptr;   // matches_ are positions in entries_
}

const Library::LauncherDef* EmulatorProvider::launcher(const Manager::Screen& screen)
{
    const Details* found = details(screen);
    if (!found || found->launcher < 0) {
        return nullptr;
    }
    return &found->index->launchers[static_cast<size_t>(found->launcher)];
}

const EmulatorProvider::Details* EmulatorProvider::details(const Manager::Screen& screen)
{
    const Library::Index* index = current();
    if (!index) {
        return nullptr;
    }
    std::string id = screen.param("id");
    std::string scope = screen.param("scope");
    if (details_.index == snapshot_ && details_.id == id && details_.scope == scope) {
        return &details_;
    }
    details_ = Details();
    details_.index = snapshot_;
    details_.id = id;
    details_.scope = scope;
    LauncherScope parsed;
    if (Emulators::parseScope(scope, parsed)) {
        details_.launcher = index->findLauncher(parsed, id);
    }
    if (details_.launcher >= 0) {
        const Library::LauncherDef& def = index->launchers[static_cast<size_t>(details_.launcher)];
        details_.executable = Emulators::checkExecutable(*index, def.executable);
        details_.users = Emulators::usersOf(*index, details_.launcher);
        std::string content;
        if (Library::readFile(def.file, content)) {
            for (const Library::Line& line : Library::Lines::parse(content).lines) {
                details_.fileLines.push_back(line.text);
            }
        }
        if (def.scope == LauncherScope::Collection && !Library::iequals(fileName(def.file), "launcher.conf")) {
            std::error_code ec;
            details_.genericFileBehind = std::filesystem::is_regular_file(
                Utils::combinePath(Utils::getDirectory(def.file), "launcher.conf"), ec);
        }
    }
    return &details_;
}

void EmulatorProvider::enter(const Manager::Screen& screen)
{
    if (isNewForm(screen)) {
        std::string key = draftKey(screen);
        if (key != draft_.id()) {
            draft_.load(key, draftDefaults(screen));
        }
    }
}

std::string EmulatorProvider::leaving(const Manager::Screen& screen)
{
    return isNewForm(screen) && draft_.isDirty() ? unsavedWarning : "";
}

void EmulatorProvider::discard(const Manager::Screen& screen)
{
    if (isNewForm(screen)) {
        draft_.load("", {});   // the next visit starts afresh
    }
}

std::string EmulatorProvider::get(const std::string& key, const Manager::Screen& screen)
{
    if (key == "query") {
        return query_;
    }
    if (key == "status") {
        if (!current()) {
            return waitingText();
        }
        return updating_ ? updatingText : "";
    }
    if (key == "hasUnresolved") {
        return current() && !unresolved_.empty() ? "yes" : "no";
    }
    if (key == "unresolvedNote") {
        if (!current() || unresolved_.empty()) {
            return "";
        }
        return unresolved_.size() == 1
            ? "A system or game asks for this launcher, but no launcher file defines it, so those games won't start."
            : "Systems or games ask for these " + std::to_string(unresolved_.size())
                + " launchers, but no launcher file defines them, so those games won't start.";
    }
    if (key.compare(0, 3, "new") == 0 || key == "needsCore" || key == "isCustom") {
        if (key == "newIntro") {
            const Library::Index* index = current();
            std::string from = screen.param("from");
            std::string name = screen.param("name");
            if (!from.empty()) {
                return "A copy of " + from + ". Change anything before you create it: once systems or games use a "
                       "launcher, it can't be changed here, so make changes in a copy like this one.";
            }
            if (!name.empty()) {
                for (const Emulators::Unresolved& missing : unresolved_) {
                    if (index && missing.name == name) {
                        return "\"" + name + "\" is asked for but not defined. " + missing.users.summary()
                            + " Create it here and they'll use it.";
                    }
                }
            }
            return "A launcher runs a program with the game's file. A system starts its games with its "
                   "default launcher, and a game can pick another.";
        }
        if (key == "newCommand") {
            std::string executable = draftExecutable();
            if (executable.empty()) {
                return "";
            }
            std::string arguments = draftArguments();
            return "\"" + executable + "\"" + (arguments.empty() ? "" : " " + arguments);
        }
        if (key == "needsCore") {
            return draft_.get("preset") == "retroarch" ? "yes" : "no";
        }
        if (key == "isCustom") {
            return draft_.get("preset") == "custom" ? "yes" : "no";
        }
        if (key == "newIsDirty") {
            return draft_.isDirty() ? "yes" : "no";
        }
        return draft_.get(draftField(key));
    }
    return detailValue(key, screen);
}

std::string EmulatorProvider::detailValue(const std::string& key, const Manager::Screen& screen)
{
    if (!isLauncherForm(screen)) {
        return "";
    }
    const Details* found = details(screen);
    const Library::LauncherDef* def = launcher(screen);
    if (key == "title") {
        return def ? def->id : screen.param("id");
    }
    if (key == "problem") {
        if (!found) {
            return waitingText();
        }
        if (def) {
            return "";
        }
        return updating_ ? updatingText : "There's no launcher called \"" + screen.param("id") + "\" any more.";
    }
    if (!def) {
        return "";
    }
    if (key == "name") {
        return def->name;
    }
    if (key == "where") {
        return Emulators::whereDefined(*found->index, *def);
    }
    if (key == "file") {
        return def->file;
    }
    if (key == "fileName") {
        return fileName(def->file);
    }
    if (key == "executable") {
        return def->executable.empty() ? "None" : def->executable;
    }
    if (key == "executableStatus") {
        switch (found->executable.state) {
        case Emulators::ExecutableCheck::State::Found: return "Yes";
        case Emulators::ExecutableCheck::State::Missing:
            return def->executable.empty() ? found->executable.sentence : "No, so its games won't start.";
        default: return found->executable.sentence;
        }
    }
    if (key == "fileText") {
        // One note line per file line (a note starts a new line at each line break)
        std::string text;
        for (size_t i = 0; i < found->fileLines.size(); i++) {
            text += (i > 0 ? "\n" : "") + found->fileLines[i];
        }
        return found->fileLines.empty() ? "The file is empty." : text;
    }
    if (key == "arguments") {
        return def->arguments.empty() ? "None" : def->arguments;
    }
    // Launcher::run reads currentDirectory and reboot as launchers.<name>.*
    // only, so a collection or local launcher's own values are never used
    bool global = def->scope == LauncherScope::Global;
    if (key == "workingFolder") {
        if (def->currentDirectory.empty()) {
            return "The program's folder";
        }
        return global ? def->currentDirectory : def->currentDirectory + " (ignored for this launcher: the program's folder is used)";
    }
    if (key == "reboot") {
        if (!def->reboot) {
            return "";
        }
        return global ? "Yes" : "Yes (ignored for this launcher)";
    }
    if (key == "usersSummary") {
        return found->users.summary();
    }
    if (key == "usersStatus") {
        return found->users.shortSummary();
    }
    if (key == "deleteNote") {
        // When it's used, the greyed-out button says why
        return found->users.empty() ? "Nothing uses it, so it can be deleted. A copy of the file is kept in manage/backups." : "";
    }
    return "";
}

void EmulatorProvider::set(const std::string& key, const std::string& value, const Manager::Screen& screen)
{
    (void)screen;
    if (key == "query") {
        query_ = value;
    }
    else if (key == "newName" || key == "newExecutable" || key == "newPreset" || key == "newCore"
             || key == "newArguments" || key == "newWorkingFolder" || key == "newReboot") {
        std::string field = draftField(key);
        draft_.set(field, field == "arguments" ? value : Utils::trimEnds(value));
    }
}

Forms::Rows EmulatorProvider::rows(const std::string& key, size_t limit, const Manager::Screen& screen)
{
    Forms::Rows result;
    const Library::Index* index = current();
    if (!index) {
        return result;
    }
    if (key == "search") {
        if (query_ != searched_ || searchedIn_ != index) {
            searched_ = query_;
            searchedIn_ = index;
            matches_.clear();
            std::string needle = Library::toLower(Utils::trimEnds(query_));
            for (size_t i = 0; i < entries_.size(); i++) {
                if (needle.empty() || entries_[i].searchText.find(needle) != std::string::npos) {
                    matches_.push_back(i);
                }
            }
        }
        result.total = matches_.size();
        for (size_t i = 0; i < matches_.size() && i < limit; i++) {
            result.items.push_back(entries_[matches_[i]].row);
        }
        return result;
    }
    if (key == "unresolved") {
        result.total = unresolved_.size();
        for (size_t i = 0; i < unresolved_.size() && i < limit; i++) {
            Forms::Row row;
            row.id = unresolved_[i].name;
            row.label = unresolved_[i].name;
            row.detail = "Not defined, " + unresolved_[i].users.shortSummary();
            result.items.push_back(row);
        }
        return result;
    }
    if (!isLauncherForm(screen)) {
        return result;
    }
    const Details* found = details(screen);
    if (!found || found->launcher < 0) {
        return result;
    }
    if (key == "systems") {
        std::vector<Forms::Row> all;
        auto addSystem = [&](int c, const std::string& how) {
            const Library::CollectionRecord& record = index->collections[static_cast<size_t>(c)];
            Forms::Row row;
            row.id = record.name;
            row.label = record.name;
            row.detail = how;
            all.push_back(row);
        };
        for (int c : found->users.defaults) {
            const Library::CollectionRecord& record = index->collections[static_cast<size_t>(c)];
            addSystem(c, record.launcher.via == Library::LauncherVia::CollectionFile ? "Default, its own launcher file" : "Default");
        }
        for (int c : found->users.alternatives) {
            addSystem(c, "Alternative");
        }
        result.total = all.size();
        all.resize(std::min(all.size(), limit));
        result.items = std::move(all);
        return result;
    }
    if (key == "games") {
        result.total = found->users.games.size();
        for (size_t i = 0; i < found->users.games.size() && i < limit; i++) {
            const auto& [c, game] = found->users.games[i];
            const std::string& collection = index->collections[static_cast<size_t>(c)].name;
            int g = index->findGame(collection, game);
            Forms::Row row;
            row.id = game;
            row.label = g >= 0 ? index->games[static_cast<size_t>(g)].title : game;
            row.detail = collection;
            row.params = { { "collection", collection } };
            result.items.push_back(row);
        }
        return result;
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> EmulatorProvider::options(const std::string& key, const Manager::Screen& screen)
{
    (void)screen;
    std::vector<std::pair<std::string, std::string>> result;
    if (key == "presets") {
        for (const Emulators::Preset& preset : Emulators::presets()) {
            result.emplace_back(preset.id, preset.label);
        }
    }
    return result;
}

// --- emulator-new ---------------------------------------------------------------

std::string EmulatorProvider::draftKey(const Manager::Screen& screen) const
{
    return "new\n" + screen.param("name") + "\n" + screen.param("from") + "\n" + screen.param("fromScope");
}

std::map<std::string, std::string> EmulatorProvider::draftDefaults(const Manager::Screen& screen)
{
    std::map<std::string, std::string> values = { { "preset", "file" }, { "name", screen.param("name") } };
    std::string from = screen.param("from");
    const Library::Index* index = current();
    LauncherScope scope;
    if (from.empty() || !index || !Emulators::parseScope(screen.param("fromScope"), scope)) {
        return values;
    }
    int found = index->findLauncher(scope, from);
    if (found < 0) {
        return values;
    }
    const Library::LauncherDef& def = index->launchers[static_cast<size_t>(found)];
    // A name that's free: "<name> copy", "<name> copy 2", ...
    auto taken = [&](const std::string& name) { return !Library::Editor::checkNewLauncherName(*index, name).empty(); };
    std::string name = def.name + " copy";
    for (int n = 2; taken(name) && n < 1000; n++) {
        name = def.name + " copy " + std::to_string(n);
    }
    values["name"] = name;
    values["executable"] = def.executable;
    values["preset"] = "custom";
    values["arguments"] = def.arguments;
    values["workingFolder"] = def.currentDirectory;
    values["reboot"] = def.reboot ? "yes" : "no";   // e.g. a theme switcher that needs RetroFE to restart
    return values;
}

std::string EmulatorProvider::draftExecutable()
{
    const Library::Index* index = current();
    std::string executable = draft_.get("executable");
    return index ? Emulators::portablePath(index->options.root, executable, index->options.os) : executable;
}

std::string EmulatorProvider::draftArguments()
{
    std::string preset = draft_.get("preset");
    if (preset == "custom") {
        return Utils::trimEnds(draft_.get("arguments"));
    }
    const Library::Index* index = current();
    std::string core = draft_.get("core");
    if (index) {
        core = Emulators::portablePath(index->options.root, core, index->options.os);
    }
    return Emulators::presetArguments(preset, core);
}

std::string EmulatorProvider::createProblem()
{
    const Library::Index* index = current();
    if (!index) {
        return waitingText();
    }
    if (!services_.editor) {
        return "Launchers can't be added here.";
    }
    std::string name = draft_.get("name");
    if (name.empty()) {
        return "Give the launcher a name.";
    }
    std::string problem = Library::Editor::checkNewLauncherName(*index, name);
    if (!problem.empty()) {
        return problem;
    }
    // Created since this snapshot, and not in it yet
    for (const Entry& entry : entries_) {
        if (entry.launcher < 0 && Library::iequals(entry.row.id, name)) {
            return "There's already a launcher called \"" + entry.row.id + "\".";
        }
    }
    std::string executable = draftExecutable();
    if (executable.empty()) {
        return "Choose the program the launcher runs.";
    }
    if (executable != checkedExecutable_) {
        // Reads the disk: only when the program changes
        checkedExecutable_ = executable;
        executableCheck_ = Emulators::checkExecutable(*index, executable);
    }
    if (executableCheck_.state == Emulators::ExecutableCheck::State::Missing) {
        return "The program isn't there: " + executableCheck_.path + ".";
    }
    std::string preset = draft_.get("preset");
    if (preset == "retroarch" && draft_.get("core").empty()) {
        return "Choose RetroArch's core file.";
    }
    for (const std::string& value : { executable, draftArguments(), draft_.get("workingFolder") }) {
        if (value.find_first_of("#\r\n") != std::string::npos) {
            return "The program, arguments and working folder can't contain # or a line break.";
        }
    }
    return "";
}

Forms::Model::Result EmulatorProvider::create()
{
    Forms::Model::Result result;
    result.message = createProblem();
    if (!result.message.empty()) {
        return result;
    }
    std::shared_ptr<const Library::Index> index = snapshot_;
    Library::Editor::NewLauncher launcher;
    launcher.name = draft_.get("name");
    launcher.executable = draftExecutable();
    launcher.arguments = draftArguments();
    launcher.currentDirectory = Emulators::portablePath(index->options.root, draft_.get("workingFolder"), index->options.os);
    launcher.reboot = Forms::isTruthy(draft_.get("reboot"));
    Library::EditResult edit = services_.editor->addLauncher(*index, launcher);
    result.message = edit.message;
    if (!edit.ok) {
        return result;
    }
    patchCreated(launcher.name, launcher.executable);
    services_.library->changed("launcher " + launcher.name);
    draft_.load("", {});   // created: the next visit starts afresh
    result.ok = true;
    result.back = true;
    return result;
}

// --- Actions ----------------------------------------------------------------------

std::string EmulatorProvider::actionProblem(const std::string& action, const Manager::Screen& screen)
{
    if (action == "create") {
        return createProblem();
    }
    if (action == "duplicate" || action == "delete") {
        const Details* found = details(screen);
        if (!found) {
            return waitingText();
        }
        if (found->launcher < 0) {
            return "There's no launcher called \"" + screen.param("id") + "\".";
        }
        if (action == "delete") {
            if (!services_.editor) {
                return "Launchers can't be deleted here.";
            }
            if (!found->users.empty()) {
                return "It's still used. " + found->users.summary() + " Give them another launcher first.";
            }
            if (found->genericFileBehind) {
                const Library::LauncherDef& def = found->index->launchers[static_cast<size_t>(found->launcher)];
                return def.collection + " also has launcher.conf, which RetroFE would use in its place, so deleting "
                    + fileName(def.file) + " wouldn't remove " + def.collection + "'s launcher.";
            }
        }
        return "";
    }
    return Provider::actionProblem(action, screen);
}

Forms::Model::Result EmulatorProvider::runAction(const std::string& action, const Manager::Screen& screen)
{
    if (action == "create") {
        return create();
    }
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) {
        return result;
    }
    const Library::LauncherDef* def = launcher(screen);
    if (action == "duplicate") {
        result.ok = true;
        result.open = "emulator-new";
        result.params = { { "from", def->id }, { "fromScope", Emulators::scopeKey(def->scope) } };
        return result;
    }
    if (action == "delete") {
        std::shared_ptr<const Library::Index> index = details_.index;   // keeps def alive
        LauncherScope scope = def->scope;
        std::string id = def->id;
        Library::EditResult edit = services_.editor->removeLauncher(*index, scope, id);
        result.message = edit.message;
        if (edit.ok) {
            patchDeleted(scope, id);
            services_.library->changed("launcher " + id);
            result.ok = true;
            result.back = true;
        }
        return result;
    }
    return result;
}

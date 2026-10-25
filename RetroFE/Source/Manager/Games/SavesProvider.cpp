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
#include "SavesProvider.h"
#include "../../Library/Files.h"
#include "../../Library/Indexer.h"
#include <cctype>

namespace Saves = Library::Saves;
namespace RA = Library::RetroArch;

namespace Manager
{

namespace
{
    std::string plural(size_t n, const std::string& one, const std::string& many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    std::string yesNo(bool value) { return value ? "yes" : "no"; }
}

SavesProvider::SavesProvider(const Services& services, RA::Environment environment)
    : services_(services)
    , environment_(std::move(environment))
{
}

bool SavesProvider::isSavesForm(const std::string& form)
{
    return form == "game" || form == "game-saves" || form == "game-saves-files" || form == "game-saves-backup";
}

void SavesProvider::refresh()
{
    // RetroArch writes saves while the manager is closed
    context_ = Context();
    setups_.clear();
    setupsIndex_.reset();
    generation_++;
}

void SavesProvider::sync(const Screen& screen)
{
    std::string key = screen.param("collection") + "/" + screen.param("id");
    IndexPtr idx = services_.library ? services_.library->index() : nullptr;
    if (key == context_.key && idx == context_.index) {
        return;
    }
    context_ = Context();
    context_.key = key;
    context_.index = idx;
    generation_++;
    if (!idx) {
        context_.why = "Reading the library...";
        return;
    }
    int g = idx->findGame(screen.param("collection"), screen.param("id"));
    if (g < 0) {
        context_.why = "This game isn't in the library.";
        return;
    }
    const Library::GameRecord& game = idx->games[static_cast<size_t>(g)];
    context_.found = true;
    context_.root = idx->options.root;
    context_.collection = idx->collections[static_cast<size_t>(game.collection)].name;
    context_.game = game.name;
    context_.title = game.title;
    Library::LauncherResolution resolution = idx->resolveLauncher(game.collection, game.name);
    if (resolution.launcher < 0) {
        context_.folders.why = "It has no launcher, so where its saves go isn't known. "
            + (resolution.problem.empty() ? std::string() : resolution.problem);
    }
    else {
        if (setupsIndex_ != idx) {
            setups_.clear();
            setupsIndex_ = idx;
        }
        auto found = setups_.find(resolution.launcher);
        if (found == setups_.end()) {
            found = setups_.emplace(resolution.launcher,
                                    RA::detect(idx->options.root, idx->launchers[static_cast<size_t>(resolution.launcher)], environment_)).first;
        }
        context_.folders = Saves::forGame(found->second, game, environment_);
    }
    // Another game named the same whose saves land in the same folders shares these files
    if (context_.folders.known) {
        std::vector<std::string> sharing;
        for (size_t other = 0; other < idx->games.size(); other++) {
            const Library::GameRecord& o = idx->games[other];
            if (static_cast<int>(other) == g || o.file.empty()) continue;
            // A file system that ignores case shares Sonic.srm between Sonic.md and sonic.sms
            bool sameName = context_.folders.ignoreCase ? Library::iequals(o.name, game.name) : o.name == game.name;
            if (!sameName) continue;
            Library::LauncherResolution r = idx->resolveLauncher(o.collection, o.name);
            if (r.launcher < 0) continue;
            auto setup = setups_.find(r.launcher);
            if (setup == setups_.end()) {
                setup = setups_.emplace(r.launcher, RA::detect(idx->options.root, idx->launchers[static_cast<size_t>(r.launcher)], environment_)).first;
            }
            Saves::Folders theirs = Saves::forGame(setup->second, o, environment_);
            bool sameStem = context_.folders.ignoreCase ? Library::iequals(theirs.stem, context_.folders.stem)
                                                        : theirs.stem == context_.folders.stem;
            if (theirs.known && sameStem
                && (Library::iequals(theirs.saves, context_.folders.saves) || Library::iequals(theirs.states, context_.folders.states))) {
                sharing.push_back(idx->collections[static_cast<size_t>(o.collection)].name);
            }
        }
        if (!sharing.empty()) {
            std::string where;
            for (size_t i = 0; i < sharing.size(); i++) where += (i ? ", " : "") + sharing[i];
            context_.folders.notes.push_back("A game of the same name in " + where
                + " keeps its saves in the same place, so these files are its too: backing up, restoring or deleting "
                  "them changes that game's saves as well.");
        }
    }
    scan();
}

void SavesProvider::scan()
{
    context_.files = Saves::list(context_.folders);
    context_.backups = context_.found ? Saves::backups(context_.root, context_.collection, context_.game)
                                      : std::vector<Saves::Backup>();
    generation_++;
}

void SavesProvider::enter(const Screen& screen)
{
    if (!isSavesForm(screen.form)) return;
    bool same = context_.key == screen.param("collection") + "/" + screen.param("id");
    sync(screen);
    // Back on a screen: RetroArch may have written saves since
    if (same && context_.found) scan();
}

std::string SavesProvider::get(const std::string& key, const Screen& screen)
{
    if (!isSavesForm(screen.form)) return "";
    sync(screen);
    const Saves::Folders& f = context_.folders;
    if (key == "status") {
        if (!context_.found) return "";
        std::string backups = context_.backups.empty() ? "" : ", " + plural(context_.backups.size(), "backup", "backups");
        if (!f.known) return "Not known for this emulator" + backups;
        return (context_.files.empty() ? std::string("None yet") : plural(context_.files.size(), "file", "files")) + backups;
    }
    if (key == "screenTitle") return context_.title;
    if (key == "why") {
        if (!context_.found) return context_.why;
        return f.known ? "" : f.why;
    }
    if (key == "isKnown") return yesNo(context_.found && f.known);
    if (key == "hasFiles") return yesNo(!context_.files.empty());
    if (key == "hasBackups") return yesNo(!context_.backups.empty());
    if (key == "savesFolder") return f.known ? f.saves : "";
    if (key == "statesFolder") return f.known ? f.states : "";
    if (key == "notes") {
        std::string text;
        for (const std::string& note : f.notes) text += (text.empty() ? "" : " ") + note;
        return text;
    }
    if (key == "filesText" || key == "backupFilesText") {
        // One note line per file
        std::vector<std::string> lines = list(key == "filesText" ? "files" : "backupFiles", screen);
        if (lines.empty()) return key == "filesText" ? "No saves or save states yet." : "No files.";
        std::string text;
        for (const std::string& line : lines) text += (text.empty() ? "" : "\n") + line;
        return text;
    }
    if (key == "filesNote") {
        if (!context_.found || !f.known) return "";
        return "Files named " + f.stem + " with a save or save state ending (.srm, .state, .state1, .state.auto, "
            "their .png pictures...). Other games' files are never counted.";
    }
    if (key == "filesStatus") {
        // The Files link: how many, the list itself is a screen of its own
        if (!context_.found || !f.known) return "";
        return context_.files.empty() ? "None yet" : plural(context_.files.size(), "file", "files");
    }
    if (key == "backupsNote") {
        if (!context_.found) return "";
        std::string problem = Saves::backupProblem(context_.collection, context_.game);
        if (!problem.empty()) return problem;
        return "Restoring one backs up the current files first.";
    }
    if (key == "backupsFolder") {
        // Where Back up now writes (Saved in)
        if (!context_.found || !Saves::backupProblem(context_.collection, context_.game).empty()) return "";
        return Saves::backupRoot(context_.root, context_.collection, context_.game);
    }
    // One backup (param backup)
    const Saves::Backup* backup = nullptr;
    for (const Saves::Backup& b : context_.backups) {
        if (b.id == screen.param("backup")) backup = &b;
    }
    if (key == "backupProblem") {
        if (!context_.found) return context_.why;
        return backup ? "" : "There's no backup called " + screen.param("backup") + " any more.";
    }
    if (!backup) return "";
    if (key == "backupTitle") return context_.title + ", " + backup->when;
    if (key == "backupWhen") return backup->when;
    if (key == "backupReason") return backup->reason.empty() ? "" : backup->reason;
    if (key == "backupFolder") return backup->folder;
    if (key == "backupSize") return plural(backup->files.size(), "file", "files") + ", " + Saves::sizeText(backup->size);
    return "";
}

std::vector<std::string> SavesProvider::list(const std::string& key, const Screen& screen)
{
    std::vector<std::string> out;
    if (!isSavesForm(screen.form)) return out;
    sync(screen);
    auto line = [](const Saves::File& file) {
        bool picture = file.name.size() > 4 && Library::toLower(file.name.substr(file.name.size() - 4)) == ".png";
        std::string what = file.kind == Saves::Kind::Save ? "save file" : picture ? "a state's picture" : "save state";
        return file.name + " (" + what + ", " + Saves::sizeText(file.size) + ")";
    };
    if (key == "files") {
        for (const Saves::File& file : context_.files) out.push_back(line(file));
    }
    else if (key == "backupFiles") {
        for (const Saves::Backup& b : context_.backups) {
            if (b.id != screen.param("backup")) continue;
            for (const Saves::File& file : b.files) out.push_back(line(file));
        }
    }
    return out;
}

Forms::Rows SavesProvider::rows(const std::string& key, size_t limit, const Screen& screen)
{
    Forms::Rows out;
    if (key != "backups" || screen.form != "game-saves") return out;
    sync(screen);
    out.total = context_.backups.size();
    for (const Saves::Backup& b : context_.backups) {
        if (out.items.size() >= limit) break;
        Forms::Row row;
        row.id = b.id;
        row.label = b.when;
        std::string reason = b.reason;
        if (!reason.empty()) reason[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(reason[0])));
        row.detail = plural(b.files.size(), "file", "files") + (reason.empty() ? "" : ", " + reason);
        row.params = { { "collection", screen.param("collection") }, { "id", screen.param("id") } };
        out.items.push_back(row);
    }
    return out;
}

std::string SavesProvider::actionProblem(const std::string& action, const Screen& screen)
{
    if (!isSavesForm(screen.form)) return Provider::actionProblem(action, screen);
    sync(screen);
    if (!context_.found) return context_.why;
    std::string folderProblem = Saves::backupProblem(context_.collection, context_.game);
    if (!folderProblem.empty() && action != "deleteBackup") return folderProblem;
    if (action == "backup" || action == "delete") {
        if (!context_.folders.known) return context_.folders.why;
        if (context_.files.empty()) return "This game has no saves or save states yet.";
        return "";
    }
    if (action == "restore" || action == "deleteBackup") {
        bool exists = false;
        for (const Saves::Backup& b : context_.backups) {
            if (b.id == screen.param("backup")) exists = true;
        }
        if (!exists) return "There's no backup called " + screen.param("backup") + ".";
        if (action == "restore" && !context_.folders.known) return context_.folders.why;
        return "";
    }
    return Provider::actionProblem(action, screen);
}

Forms::Model::Result SavesProvider::runAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) return result;
    // The files may have changed since the screen was drawn: go by what's there now
    scan();
    Library::EditResult edit;
    if (action == "backup") {
        edit = Saves::backUp(context_.root, context_.collection, context_.game, context_.folders, "Backed up by hand");
    }
    else if (action == "delete") {
        edit = Saves::removeAll(context_.root, context_.collection, context_.game, context_.folders);
    }
    else if (action == "restore") {
        edit = Saves::restore(context_.root, context_.collection, context_.game, context_.folders, screen.param("backup"));
    }
    else {
        edit = Saves::removeBackup(context_.root, context_.collection, context_.game, screen.param("backup"));
    }
    scan();
    // RetroFE doesn't read saves: nothing to reload
    result.ok = edit.ok;
    result.message = edit.message;
    result.back = edit.ok && (action == "restore" || action == "deleteBackup");
    return result;
}

}

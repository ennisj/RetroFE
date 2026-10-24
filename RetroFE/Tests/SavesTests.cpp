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

// A game's saves (Library/Saves, Manager/Games/SavesProvider): where
// RetroArch keeps them, which files are the game's, backups, restore, delete.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Library/Saves.h"
#include "../Source/Manager/Games/SavesProvider.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Utility/Utils.h"
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
namespace RA = Library::RetroArch;
namespace Saves = Library::Saves;
using namespace Test;

namespace
{
    std::string os() { return Utils::getOSType(); }

    RA::Environment noEnvironment()
    {
        RA::Environment env;
        env.os = os();
        return env;
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    bool exists(const std::string& path)
    {
        std::error_code ec;
        return fs::exists(path, ec);
    }

    // A fake RetroArch (emu/RetroArch, saves and states beside it), a MegaDrive
    // system on Genesis Plus GX with sonic.md and "sonic 2.md", and Notes on notepad
    std::string makeInstall(const std::string& name, const std::string& extraCfg = "")
    {
        std::string root = makeRoot(name);
        std::string ra = root + "/emu/RetroArch/";
        writeFile(ra + "retroarch.exe", "x");
        writeFile(ra + "retroarch.cfg", "savefile_directory = \":/saves\"\nsavestate_directory = \":/states\"\n" + extraCfg);
        writeFile(ra + "cores/genesis_plus_gx_libretro.dll", "x");
        fs::create_directories(ra + "saves");
        fs::create_directories(ra + "states");
        std::string launchers = root + "/launchers." + os() + "/";
        writeFile(launchers + "genesis.conf",
                  "executable = %RETROFE_PATH%/emu/RetroArch/retroarch.exe\narguments = -L cores/genesis_plus_gx_libretro.dll \"%ITEM_FILEPATH%\"\n");
        writeFile(launchers + "notepad.conf", "executable = notepad.exe\narguments = \"%ITEM_FILEPATH%\"\n");
        writeFile(root + "/collections/MegaDrive/settings.conf", "list.extensions = md\nlauncher = genesis\n");
        writeFile(root + "/collections/MegaDrive/roms/sonic.md", "rom");
        writeFile(root + "/collections/MegaDrive/roms/sonic 2.md", "rom");
        writeFile(root + "/collections/Notes/settings.conf", "list.extensions = txt\nlauncher = notepad\n");
        writeFile(root + "/collections/Notes/roms/readme.txt", "x");
        return root;
    }

    Library::LauncherDef genesis(const std::string& root)
    {
        Library::LauncherDef def;
        def.id = "genesis";
        def.executable = "%RETROFE_PATH%/emu/RetroArch/retroarch.exe";
        def.arguments = "-L cores/genesis_plus_gx_libretro.dll \"%ITEM_FILEPATH%\"";
        (void)root;
        return def;
    }

    Library::GameRecord game(const std::string& root, const std::string& file)
    {
        Library::GameRecord g;
        g.name = fs::path(file).stem().string();
        g.directory = root + "/collections/MegaDrive/roms";
        g.file = g.directory + "/" + file;
        return g;
    }

    Saves::Folders foldersOf(const std::string& root, const std::string& file = "sonic.md")
    {
        return Saves::forGame(RA::detect(root, genesis(root), noEnvironment()), game(root, file), noEnvironment());
    }

    std::string norm(const std::string& path) { return Library::normalPath(path); }

    struct Fixture
    {
        explicit Fixture(const std::string& root)
            : indexer(options(root))
            , editor(root)
        {
            Configuration::absolutePath = root;
            config = std::make_unique<Configuration>();
            model = std::make_unique<ManagerModel>(*config);
            Manager::Services services{ *config };
            services.library = &indexer;
            services.editor = &editor;
            model->addProvider(std::make_unique<Manager::SavesProvider>(services, noEnvironment()));
            indexer.rebuild();
            indexer.wait(10000);
        }

        static Library::ScanOptions options(const std::string& root)
        {
            Library::ScanOptions scan;
            scan.root = root;
            return scan;
        }

        Library::Indexer indexer;
        Library::Editor editor;
        std::unique_ptr<Configuration> config;
        std::unique_ptr<ManagerModel> model;
    };
}

static Test::Suite saves("saves", [] {
    run("saves: a file is the game's only when its exact name has a save ending", [] {
        Saves::Kind kind;
        check(Saves::belongsTo("Sonic.srm", "Sonic", kind) && kind == Saves::Kind::Save, ".srm is a save file");
        check(Saves::belongsTo("Sonic.rtc", "Sonic", kind) && kind == Saves::Kind::Save, ".rtc too");
        check(Saves::belongsTo("Sonic.1.mcr", "Sonic", kind) && kind == Saves::Kind::Save, "an extra memory card");
        check(Saves::belongsTo("Sonic.state", "Sonic", kind) && kind == Saves::Kind::State, ".state is a save state");
        check(Saves::belongsTo("Sonic.state12", "Sonic", kind) && kind == Saves::Kind::State, "a numbered slot");
        check(Saves::belongsTo("Sonic.state.auto", "Sonic", kind), "the auto state");
        check(Saves::belongsTo("Sonic.state3.png", "Sonic", kind) && kind == Saves::Kind::State, "a slot's picture");
        check(Saves::belongsTo("Sonic.STATE", "Sonic", kind), "the ending ignores case");
        check(Saves::belongsTo("Sonic.replay2", "Sonic", kind) && kind == Saves::Kind::State, "a replay");
        check(!Saves::belongsTo("Sonic 2.srm", "Sonic", kind), "another game whose name starts the same");
        check(!Saves::belongsTo("Sonic.2.srm", "Sonic", kind), "not a save ending");
        check(!Saves::belongsTo("sonic.srm", "Sonic", kind), "the name is exact");
        check(!Saves::belongsTo("Sonic.md", "Sonic", kind), "the game itself");
        check(!Saves::belongsTo("Sonic.state1x", "Sonic", kind), "not a slot number");
        check(!Saves::belongsTo("Sonic.png", "Sonic", kind), "a picture that isn't a state's");
        check(!Saves::belongsTo(".srm", "", kind), "no name");
    });

    run("saves: the folders follow retroarch.cfg as RetroArch does", [] {
        std::string root = makeInstall("saves-folders");
        std::string ra = root + "/emu/RetroArch";
        Saves::Folders plain = foldersOf(root);
        check(plain.known, "known: " + plain.why);
        checkEqual(plain.saves, norm(ra + "/saves"), "\":\" is RetroArch's folder");
        checkEqual(plain.states, norm(ra + "/states"), "states");
        checkEqual(plain.stem, "sonic", "named after the file without its extension");

        writeFile(ra + "/retroarch.cfg", "savefile_directory = \":/saves\"\nsavestate_directory = \":/states\"\n"
                  "sort_savefiles_enable = \"true\"\nsort_savestates_enable = \"true\"\nsort_savestates_by_content_enable = \"true\"\n");
        Saves::Folders sorted = foldersOf(root);
        checkEqual(sorted.saves, norm(ra + "/saves/Genesis Plus GX"), "a folder per core");
        checkEqual(sorted.states, norm(ra + "/states/roms/Genesis Plus GX"), "the content folder's name first, then the core");

        writeFile(ra + "/retroarch.cfg", "savefiles_in_content_dir = \"true\"\nsavestate_directory = \"default\"\n");
        Saves::Folders content = foldersOf(root);
        checkEqual(content.saves, norm(root + "/collections/MegaDrive/roms"), "beside the game");
        checkEqual(content.states, norm(ra + "/states"), "\"default\" is the platform's default");

        writeFile(ra + "/retroarch.cfg", "savefile_directory = \"" + root + "/nowhere\"\n");
        Saves::Folders missing = foldersOf(root);
        checkEqual(missing.saves, norm(ra + "/saves"), "a folder that isn't there is ignored, as RetroArch does");
        check(!missing.notes.empty() && contains(missing.notes[0], "isn't a folder"), "and said");

        Library::LauncherDef notepad;
        notepad.id = "notepad";
        notepad.executable = "notepad.exe";
        notepad.arguments = "\"%ITEM_FILEPATH%\"";
        Saves::Folders other = Saves::forGame(RA::detect(root, notepad, noEnvironment()), game(root, "sonic.md"), noEnvironment());
        check(!other.known && contains(other.why, "another program"), "other emulators aren't known: " + other.why);
    });

    run("saves: only the game's files are listed, never its own file or another game's", [] {
        std::string root = makeInstall("saves-list", "savefiles_in_content_dir = \"true\"\n");
        std::string roms = root + "/collections/MegaDrive/roms/";
        std::string states = root + "/emu/RetroArch/states/";
        writeFile(roms + "sonic.srm", "save");
        writeFile(roms + "sonic 2.srm", "other");
        writeFile(states + "sonic.state", "s0");
        writeFile(states + "sonic.state1", "s1");
        writeFile(states + "sonic.state1.png", "p");
        writeFile(states + "sonic 2.state", "other");
        std::vector<Saves::File> files = Saves::list(foldersOf(root));
        check(files.size() == 4, "sonic.srm and three state files: " + std::to_string(files.size()));
        check(files.size() == 4 && files[0].name == "sonic.srm" && files[0].kind == Saves::Kind::Save
              && files[1].name == "sonic.state" && files[3].name == "sonic.state1.png", "saves first, by name");
        for (const Saves::File& f : files) {
            check(!contains(f.name, "sonic 2") && f.name != "sonic.md", "not " + f.name);
        }
    });

    run("saves: back up, restore and delete touch only the game's files", [] {
        std::string root = makeInstall("saves-backup");
        std::string saves = root + "/emu/RetroArch/saves/";
        std::string states = root + "/emu/RetroArch/states/";
        writeFile(saves + "sonic.srm", "v1");
        writeFile(states + "sonic.state", "s1");
        writeFile(saves + "sonic 2.srm", "untouched");
        Saves::Folders folders = foldersOf(root);

        std::string first;
        Library::EditResult backup = Saves::backUp(root, "MegaDrive", "sonic", folders, "Backed up by hand", &first);
        check(backup.ok && contains(backup.message, "2 files"), backup.message);
        std::string base = Saves::backupRoot(root, "MegaDrive", "sonic");
        checkEqual(norm(base), norm(root + "/manage/saves/MegaDrive/sonic"), "kept under manage/saves/<collection>/<game>");
        checkEqual(readFile(base + "/" + first + "/saves/sonic.srm"), "v1", "a copy of the save");
        checkEqual(readFile(base + "/" + first + "/states/sonic.state"), "s1", "and of the state");

        // Played on: the save changed and a new slot appeared
        writeFile(saves + "sonic.srm", "v2");
        writeFile(states + "sonic.state2", "s2");
        Library::EditResult restored = Saves::restore(root, "MegaDrive", "sonic", folders, first);
        check(restored.ok && contains(restored.message, "What was there before"), restored.message);
        checkEqual(readFile(saves + "sonic.srm"), "v1", "the backup's save is back");
        check(!exists(states + "sonic.state2"), "a file the backup didn't have is gone (into the new backup)");
        checkEqual(readFile(saves + "sonic 2.srm"), "untouched", "another game's file is never touched");
        std::vector<Saves::Backup> all = Saves::backups(root, "MegaDrive", "sonic");
        check(all.size() == 2 && contains(all[0].reason, "Before restoring") && all[0].files.size() == 3,
              "the current files were backed up first, newest listed first");
        check(all.size() == 2 && all[1].id == first && all[1].reason == "Backed up by hand", "with the reason");

        Library::EditResult removed = Saves::removeAll(root, "MegaDrive", "sonic", folders);
        check(removed.ok && !exists(saves + "sonic.srm") && !exists(states + "sonic.state"), removed.message);
        checkEqual(readFile(saves + "sonic 2.srm"), "untouched", "still untouched");
        all = Saves::backups(root, "MegaDrive", "sonic");
        check(all.size() == 3 && all[0].reason == "Before deleting", "deleting backs up first");
        check(!Saves::removeAll(root, "MegaDrive", "sonic", folders).ok, "nothing left to delete");
        check(!Saves::backUp(root, "MegaDrive", "sonic", folders, "x").ok, "nothing to back up");

        // A backup holding another game's file is refused, not copied
        writeFile(base + "/" + first + "/saves/sonic 2.srm", "planted");
        Library::EditResult refused = Saves::restore(root, "MegaDrive", "sonic", folders, first);
        check(!refused.ok && contains(refused.message, "isn't one of"), refused.message);
        checkEqual(readFile(saves + "sonic 2.srm"), "untouched", "the planted file wasn't written");

        check(Saves::removeBackup(root, "MegaDrive", "sonic", first).ok && !exists(base + "/" + first), "a backup can be deleted");
        check(!Saves::restore(root, "MegaDrive", "sonic", folders, "../x").ok, "only backup folder names");
        checkEqual(Saves::backupRoot(root, "MegaDrive", ".."), "", "a game name that isn't a folder name");
    });

    run("saves: the game's Saves screens", [] {
        std::string root = makeInstall("saves-screens");
        std::string saves = root + "/emu/RetroArch/saves/";
        writeFile(saves + "sonic.srm", "v1");
        writeFile(root + "/emu/RetroArch/states/sonic.state.auto", "auto");
        // Another system's sonic on the same core keeps its saves in the same place
        writeFile(root + "/collections/Genesis/settings.conf", "list.extensions = gen\nlauncher = genesis\n");
        writeFile(root + "/collections/Genesis/roms/sonic.gen", "rom");
        Fixture f(root);
        Forms::Params sonic = { { "collection", "MegaDrive" }, { "id", "sonic" } };
        f.model->setScreen("game", sonic);
        checkEqual(f.model->get("saves:status"), "2 files", "the link's status");
        f.model->setScreen("game-saves", sonic);
        checkEqual(f.model->get("saves:isKnown"), "yes", "known");
        checkEqual(norm(f.model->get("saves:savesFolder")), norm(saves), "where save files are");
        check(contains(f.model->get("saves:notes"), "same name in Genesis"), "a game sharing the files is named: " + f.model->get("saves:notes"));
        check(contains(f.model->get("saves:filesText"), "sonic.srm (save file"), "the files as note lines");
        std::vector<std::string> files = f.model->list("saves:files");
        check(files.size() == 2 && contains(files[0], "sonic.srm (save file") && contains(files[1], "save state"), "the files");
        Forms::Model::Result backup = f.model->runAction("saves.backup");
        check(backup.ok, backup.message);
        Forms::Rows rows = f.model->rows("saves:backups", 50);
        check(rows.items.size() == 1 && rows.items[0].params.at("collection") == "MegaDrive" && contains(rows.items[0].detail, "2 files"),
              "the backup is listed, opening with the game's params");

        writeFile(saves + "sonic.srm", "v2");
        Forms::Params one = sonic;
        one["backup"] = rows.items[0].id;
        f.model->setScreen("game-saves-backup", one);
        check(f.model->list("saves:backupFiles").size() == 2, "the backup's files");
        checkEqual(f.model->get("saves:backupReason"), "Backed up by hand", "why it was made");
        Forms::Model::Result restored = f.model->runAction("saves.restore");
        check(restored.ok && restored.back, restored.message);
        checkEqual(readFile(saves + "sonic.srm"), "v1", "restored");

        f.model->setScreen("game-saves", sonic);
        check(f.model->rows("saves:backups", 50).items.size() == 2, "plus the one made before restoring");
        Forms::Model::Result deleted = f.model->runAction("saves.delete");
        check(deleted.ok && !exists(saves + "sonic.srm"), deleted.message);
        checkEqual(f.model->get("saves:hasFiles"), "no", "nothing left");
        check(!f.model->actionProblem("saves.backup").empty(), "nothing to back up");

        f.model->setScreen("game", { { "collection", "Notes" }, { "id", "readme" } });
        checkEqual(f.model->get("saves:status"), "Not known for this emulator", "another emulator");
        f.model->setScreen("game-saves", { { "collection", "Notes" }, { "id", "readme" } });
        check(contains(f.model->get("saves:why"), "another program"), "says why");
        check(!f.model->actionProblem("saves.backup").empty(), "and can't back up");
    });
});

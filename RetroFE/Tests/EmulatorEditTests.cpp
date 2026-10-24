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

// Changing launchers in place (Library::Editor::editLauncher / renameLauncher,
// the Emulators edit and rename screens) and a launcher's RetroArch link.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Manager/Emulators/EmulatorProvider.h"
#include "../Source/Manager/Emulators/Launchers.h"
#include "../Source/Manager/Games/RetroArchProvider.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Utility/Utils.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    std::string os() { return Utils::getOSType(); }
    std::string otherOs() { return os() == "linux" ? "windows" : "linux"; }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    bool exists(const std::string& path)
    {
        std::error_code ec;
        return fs::exists(path, ec);
    }

    const char* const retroConf =
        "# RetroArch for the NES\r\n"
        "executable = %RETROFE_PATH%/emu/RetroArch/retroarch.exe  # the program\r\n"
        "arguments = -L cores/fceumm_libretro.dll \"%ITEM_FILEPATH%\"\r\n"
        "LEDBlinkyEmulator = fceumm\r\n";

    // launchers.<os>: retro (also in the other platform's folder), notepad
    //   Arcade: launcher = retro, notepad; a.zip picks retro, b.zip notepad
    //   NES:    launcher = retro; mario picks "retro", which is NES's local launcher of that name
    //   SNES:   launcher = retro, set in info.conf
    //   Tools:  its own launcher.conf
    std::string makeInstall(const std::string& name)
    {
        std::string root = makeRoot(name);
        writeFile(root + "/emu/RetroArch/retroarch.exe", "x");
        writeFile(root + "/emu/RetroArch/retroarch.cfg", "rgui_config_directory = \":/config\"\n");
        writeFile(root + "/emu/RetroArch/cores/fceumm_libretro.dll", "x");
        writeFile(root + "/emu/other.exe", "x");
        writeFile(root + "/launchers." + os() + "/retro.conf", retroConf);
        writeFile(root + "/launchers." + otherOs() + "/retro.conf", "executable = retroarch\n");
        writeFile(root + "/launchers." + os() + "/notepad.conf", "executable = %RETROFE_PATH%/emu/other.exe\n");

        writeFile(root + "/collections/Arcade/settings.conf", "# arcade\r\nlist.extensions = zip\r\nlauncher = retro, notepad\r\n");
        writeFile(root + "/collections/Arcade/roms/a.zip", "x");
        writeFile(root + "/collections/Arcade/roms/b.zip", "x");
        writeFile(root + "/collections/Arcade/launchers/a.conf", "retro\r\nkept\r\n");
        writeFile(root + "/collections/Arcade/launchers/b.conf", "notepad\n");
        writeFile(root + "/collections/NES/settings.conf", "list.extensions = nes\nlauncher = retro\n");
        writeFile(root + "/collections/NES/launchers.local/retro.conf", "executable = emu/other.exe\n");
        writeFile(root + "/collections/NES/roms/mario.nes", "x");
        writeFile(root + "/collections/NES/launchers/mario.conf", "retro\n");
        writeFile(root + "/collections/SNES/settings.conf", "list.extensions = sfc\n");
        writeFile(root + "/collections/SNES/info.conf", "title = Super NES\nlauncher = retro\n");
        writeFile(root + "/collections/SNES/roms/zelda.sfc", "x");
        writeFile(root + "/collections/Tools/launcher.conf", "executable = emu/other.exe\ncurrentDirectory = emu\n");
        return root;
    }

    Library::RetroArch::Environment noEnvironment()
    {
        Library::RetroArch::Environment env;
        env.os = os();
        return env;
    }

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
            auto emulators = std::make_unique<EmulatorProvider>(services);
            provider = emulators.get();
            model->addProvider(std::move(emulators));
            model->addProvider(std::make_unique<Manager::RetroArchProvider>(services, noEnvironment()));
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
        EmulatorProvider* provider = nullptr;
    };

    // Every file under root but manage/ (the backups), with its bytes
    std::map<std::string, std::string> snapshot(const std::string& root)
    {
        std::map<std::string, std::string> files;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            std::string relative = fs::relative(entry.path(), root).generic_string();
            if (relative.compare(0, 7, "manage/") == 0 || relative == "manage") continue;
            files[relative] = entry.is_regular_file() ? readFile(entry.path().string()) : std::string("<folder>");
        }
        return files;
    }

    std::shared_ptr<const Library::Index> build(const std::string& root)
    {
        Library::ScanOptions scan;
        scan.root = root;
        return std::shared_ptr<const Library::Index>(Library::buildIndex(scan));
    }
}

static Test::Suite emulatorEdit("emulator-edit", [] {
    run("emulator edit: placeholders RetroFE knows", [] {
        checkEqual(Emulators::unknownPlaceholder("-L x \"%ITEM_FILEPATH%\" \"%ITEM_DIRECTORY%.\" %RETROFE_PATH%"), "", "known ones");
        checkEqual(Emulators::unknownPlaceholder("%ITEM_PATH% x"), "%ITEM_PATH%", "a misspelt one");
        checkEqual(Emulators::unknownPlaceholder("%APPDATA%\\x"), "%APPDATA%", "environment variables aren't replaced");
        checkEqual(Emulators::unknownPlaceholder("100% %1 %~dp0 %%"), "", "a percent sign alone, %1, %~dp0 aren't variables");
        check(contains(Emulators::checkLauncherValues("a.exe", "%GAME%", ""), "doesn't replace %GAME%"), "said");
        check(contains(Emulators::checkLauncherValues("a.exe", "x # y", ""), "can't contain #"), "# starts a comment");
    });

    run("emulator edit: the file is changed in place, keeping everything else", [] {
        std::string root = makeInstall("edit-library");
        auto index = build(root);
        Library::Editor editor(root);
        std::string file = root + "/launchers." + os() + "/retro.conf";
        Library::Editor::LauncherChange change;
        change.arguments = "-L cores/fceumm_libretro.dll -f \"%ITEM_FILEPATH%\"";
        change.currentDirectory = "%RETROFE_PATH%/emu";
        change.reboot = true;
        Library::EditResult r = editor.editLauncher(*index, Library::LauncherScope::Global, "retro", change);
        check(r.ok, r.message);
        checkEqual(readFile(file),
                   "# RetroArch for the NES\r\n"
                   "executable = %RETROFE_PATH%/emu/RetroArch/retroarch.exe  # the program\r\n"
                   "arguments = -L cores/fceumm_libretro.dll -f \"%ITEM_FILEPATH%\"\r\n"
                   "LEDBlinkyEmulator = fceumm\r\n"
                   "currentDirectory = %RETROFE_PATH%/emu\r\n"
                   "reboot = yes\r\n",
                   "comments, unknown keys and CRLF kept; new keys appended");
        checkEqual(readFile(editor.backups().backupPath(file)), retroConf, "the original is kept");

        Library::Editor::LauncherChange back;
        back.arguments = "";
        back.reboot = false;
        r = editor.editLauncher(*index, Library::LauncherScope::Global, "retro", back);
        check(r.ok && !contains(readFile(file), "arguments") && !contains(readFile(file), "reboot"), "empty values remove their line");

        Library::Editor::LauncherChange bad;
        bad.executable = "a.exe # b";
        check(!editor.editLauncher(*index, Library::LauncherScope::Global, "retro", bad).ok, "# refused");
        Library::Editor::LauncherChange folder;
        folder.currentDirectory = "emu";
        r = editor.editLauncher(*index, Library::LauncherScope::Collection, "Tools", folder);
        check(!r.ok && contains(r.message, "global launchers only"), r.message);
        check(!editor.editLauncher(*index, Library::LauncherScope::Global, "retro", {}).ok, "nothing to change");
        check(editor.restoreOriginal(file).ok && readFile(file) == retroConf, "the original can be put back");
    });

    run("emulator edit: the edit screen saves, resets and puts back the original", [] {
        std::string root = makeInstall("edit-screen");
        std::string file = root + "/launchers." + os() + "/retro.conf";
        Fixture f(root);
        Forms::Params retro = { { "id", "retro" }, { "scope", "global" } };
        f.model->setScreen("emulator-edit", retro);
        checkEqual(f.model->get("emulator:editExecutable"), "%RETROFE_PATH%/emu/RetroArch/retroarch.exe", "the program as written");
        checkEqual(f.model->get("emulator:editPreset"), "custom", "the arguments as custom text");
        checkEqual(f.model->get("emulator:editIsGlobal"), "yes", "a global launcher has a working folder and restart");
        check(contains(f.model->get("emulator:editIntro"), "The default of 3 systems") && contains(f.model->get("emulator:editIntro"), "1 game picks it"),
              "says who's affected: " + f.model->get("emulator:editIntro"));
        checkEqual(f.model->actionProblem("emulator.save"), "No changes to save.", "nothing changed yet");

        f.model->set("emulator:editArguments", "-L cores/fceumm_libretro.dll \"%ITEM_PATH%\"");
        check(contains(f.model->actionProblem("emulator.save"), "%ITEM_PATH%"), "an unknown variable is refused");
        check(!f.model->leaving(f.model->screen()).empty(), "unsaved changes warn");
        f.model->runAction("emulator.resetEdit");
        checkEqual(f.model->get("emulator:editIsDirty"), "no", "reset");

        f.model->set("emulator:editExecutable", root + "/emu/missing.exe");
        check(contains(f.model->actionProblem("emulator.save"), "isn't there"), "a missing program is refused");
        f.model->set("emulator:editExecutable", root + "/emu/other.exe");
        f.model->set("emulator:editReboot", "yes");
        check(contains(f.model->get("emulator:editCommand"), "%RETROFE_PATH%") && contains(f.model->get("emulator:editCommand"), "other.exe\" -L"),
              "the command shown: " + f.model->get("emulator:editCommand"));
        Forms::Model::Result saved = f.model->runAction("emulator.save");
        check(saved.ok, saved.message);
        std::string text = readFile(file);
        check(contains(text, "executable = %RETROFE_PATH%") && contains(text, "other.exe  # the program\r\n"),
              "the program written as a portable path, its comment kept: " + text);
        check(contains(text, "reboot = yes") && contains(text, "LEDBlinkyEmulator = fceumm"), "restart added, unknown key kept");
        check(contains(text, "arguments = -L cores/fceumm_libretro.dll \"%ITEM_FILEPATH%\""), "untouched arguments unchanged");
        checkEqual(f.model->get("emulator:editIsDirty"), "no", "saved");
        checkEqual(f.model->get("emulator:editHasOriginal"), "yes", "the original can be put back");
        Forms::Model::Result restored = f.model->runAction("emulator.restore");
        check(restored.ok && readFile(file) == retroConf, restored.message);
        checkEqual(f.model->get("emulator:editHasOriginal"), "no", "nothing left to put back");

        // The preset gives the arguments
        f.model->set("emulator:editPreset", "retroarch");
        check(contains(f.model->actionProblem("emulator.save"), "core"), "the RetroArch preset needs a core");
        f.model->set("emulator:editCore", root + "/emu/RetroArch/cores/fceumm_libretro.dll");
        check(f.model->runAction("emulator.save").ok, "saved with a preset");
        check(contains(readFile(file), "arguments = -L \"%RETROFE_PATH%"), "the preset's arguments: " + readFile(file));

        f.model->setScreen("emulator-edit", { { "id", "Tools" }, { "scope", "collection" } });
        checkEqual(f.model->get("emulator:editIsGlobal"), "no", "a collection's launcher has no working folder or restart here");
        checkEqual(f.model->get("emulator:editWorkingFolder"), "emu", "its file's value is still read (and kept)");
    });

    run("emulator rename: the plan, the files it changes and the screen", [] {
        std::string root = makeInstall("edit-rename");
        auto index = build(root);
        Library::Editor editor(root);
        check(contains(editor.planLauncherRename(*index, "retro", "notepad").problem, "already a launcher"), "a taken name");
        check(contains(editor.planLauncherRename(*index, "retro", "retro").problem, "already"), "the same name");
        check(contains(editor.planLauncherRename(*index, "retro", "Retro").problem, "case"), "only the case");
        check(contains(editor.planLauncherRename(*index, "Tools", "x").problem, "global"), "only global launchers");
        Library::Editor::RenamePlan plan = editor.planLauncherRename(*index, "retro", "RetroArch NES");
        check(plan.problem.empty(), plan.problem);
        check(plan.renames.size() == 2, "both platforms' files");
        check(plan.settingsFiles.size() == 3, "Arcade's and NES's settings.conf, SNES's info.conf");
        check(plan.gameFiles.size() == 1, "a.zip's launcher file; not mario's, which means NES's local launcher");

        Library::EditResult r = editor.renameLauncher(*index, "retro", "RetroArch NES");
        check(r.ok, r.message);
        std::string launchers = root + "/launchers." + os() + "/";
        check(!exists(launchers + "retro.conf") && readFile(launchers + "RetroArch NES.conf") == retroConf,
              "the file is renamed, byte for byte");
        check(exists(root + "/launchers." + otherOs() + "/RetroArch NES.conf"), "and the other platform's");
        checkEqual(readFile(root + "/collections/Arcade/settings.conf"),
                   "# arcade\r\nlist.extensions = zip\r\nlauncher = RetroArch NES, notepad\r\n", "the default renamed, the rest kept");
        checkEqual(readFile(root + "/collections/SNES/info.conf"), "title = Super NES\nlauncher = RetroArch NES\n", "where it's set");
        checkEqual(readFile(root + "/collections/NES/settings.conf"), "list.extensions = nes\nlauncher = RetroArch NES\n",
                   "a system's default always means a global launcher");
        checkEqual(readFile(root + "/collections/NES/launchers/mario.conf"), "retro\n", "a game's pick of NES's local launcher stays");
        checkEqual(readFile(root + "/collections/Arcade/launchers/a.conf"), "RetroArch NES\nkept\r\n", "a game's pick, rest kept");
        checkEqual(readFile(root + "/collections/Arcade/launchers/b.conf"), "notepad\n", "another pick untouched");
        check(exists(editor.backups().backupPath(launchers + "retro.conf")), "the old file is kept in the backups");

        auto after = build(root);
        int arcade = after->findCollection("Arcade");
        std::string unresolved;
        for (const std::string& n : after->unresolvedLaunchers) unresolved += n + ";";
        check(after->resolveLauncher(arcade).launcher >= 0 && after->resolveLauncher(arcade, "a").launcher >= 0
              && after->unresolvedLaunchers.empty(), "everything still resolves: " + unresolved);

        // A reference outside a collection's folder can't be changed here: refused, nothing written
        std::string root3 = makeInstall("edit-rename-global");
        writeFile(root3 + "/settings.conf", "collections.GBA.launcher = retro\n");
        writeFile(root3 + "/collections/GBA/settings.conf", "list.extensions = gba\n");
        writeFile(root3 + "/collections/GBA/roms/x.gba", "x");
        auto index3 = build(root3);
        Library::Editor editor3(root3);
        Library::EditResult refused = editor3.renameLauncher(*index3, "retro", "other");
        check(!refused.ok && contains(refused.message, "outside its own folder"), refused.message);
        check(exists(root3 + "/launchers." + os() + "/retro.conf") && !exists(root3 + "/launchers." + os() + "/other.conf"),
              "nothing renamed");

        // The screen
        std::string root2 = makeInstall("edit-rename-screen");
        Fixture f(root2);
        f.model->setScreen("emulator", { { "id", "Tools" }, { "scope", "collection" } });
        checkEqual(f.model->get("emulator:isGlobal"), "no", "a collection's launcher has no Rename");
        check(!f.model->actionProblem("emulator.startRename").empty(), "and can't be renamed");
        f.model->setScreen("emulator", { { "id", "retro" }, { "scope", "global" } });
        Forms::Model::Result start = f.model->runAction("emulator.startRename");
        check(start.ok && start.open == "emulator-rename" && start.replace && start.keepParams,
              "the rename screen takes the launcher screen's place");
        f.model->setScreen("emulator-rename", { { "id", "retro" }, { "scope", "global" } });
        checkEqual(f.model->get("emulator:canRename"), "no", "nothing typed");
        f.model->set("emulator:renameTo", "notepad");
        check(contains(f.model->get("emulator:renameProblem"), "already"), "a taken name is said at once");
        f.model->set("emulator:renameTo", "NES RetroArch");
        checkEqual(f.model->get("emulator:canRename"), "yes", "can rename");
        check(f.model->list("emulator:renameChanges").size() == 6, "two files, three settings, one game");
        Forms::Model::Result renamed = f.model->runAction("emulator.rename");
        check(renamed.ok && renamed.open == "emulator" && renamed.replace && renamed.params["id"] == "NES RetroArch",
              "the launcher's screen comes back under its new name: " + renamed.message);
        check(contains(f.model->get("emulator:renameIntro"), "Type"), "the name is cleared");
        f.indexer.wait(10000);
        f.model->setScreen("emulator", renamed.params);
        checkEqual(f.model->get("emulator:name"), "NES RetroArch", "found once the index has it");
        checkEqual(f.model->get("retroarch:runs"), "yes", "with its RetroArch link");
    });

    run("emulator retroarch: a launcher that runs RetroArch links to its core's settings", [] {
        std::string root = makeInstall("edit-retroarch");
        Fixture f(root);
        f.model->setScreen("emulator", { { "id", "retro" }, { "scope", "global" } });
        checkEqual(f.model->get("retroarch:runs"), "yes", "the link shows");
        check(contains(f.model->get("retroarch:status"), "FCEUmm"), "with the core: " + f.model->get("retroarch:status"));
        f.model->setScreen("emulator-retroarch", { { "id", "retro" }, { "scope", "global" } });
        checkEqual(f.model->get("retroarch:isReady"), "yes", "ready: " + f.model->get("retroarch:why"));
        check(contains(f.model->get("retroarch:file"), "FCEUmm.cfg"), "the core-wide file: " + f.model->get("retroarch:file"));
        check(contains(f.model->get("retroarch:reaches"), "Arcade"), "reaches the systems on the core");
        f.model->setScreen("emulator", { { "id", "notepad" }, { "scope", "global" } });
        checkEqual(f.model->get("retroarch:runs"), "no", "not for another program");
    });

    run("emulator edit: a launcher Manage created has no original to put back", [] {
        std::string root = makeInstall("edit-no-original");
        {
            auto index = build(root);
            Library::Editor editor(root);
            Library::Editor::NewLauncher added;
            added.name = "fresh";
            added.executable = "%RETROFE_PATH%/emu/other.exe";
            check(editor.addLauncher(*index, added).ok, "added");
            check(editor.renameLauncher(*index, "retro", "renamed").ok, "renamed");
        }
        Fixture f(root);
        for (const char* id : { "fresh", "renamed" }) {
            f.model->setScreen("emulator-edit", { { "id", id }, { "scope", "global" } });
            checkEqual(f.model->get("emulator:editIsReady"), "yes", std::string(id) + " is there");
            f.model->set("emulator:editArguments", "\"%ITEM_FILEPATH%\"");
            check(f.model->runAction("emulator.save").ok, std::string(id) + " saved");
            checkEqual(f.model->get("emulator:editHasOriginal"), "no",
                       std::string(id) + ": its original is an absent marker, and putting that back would delete it");
            check(!f.model->actionProblem("emulator.restore").empty(), "so there's nothing to put back");
        }
        check(exists(root + "/launchers." + os() + "/renamed.conf"), "the renamed launcher is still there");
    });

    run("emulator edit: says another platform's file isn't changed", [] {
        Fixture f(makeInstall("edit-other-platform"));
        f.model->setScreen("emulator-edit", { { "id", "retro" }, { "scope", "global" } });
        check(contains(f.model->get("emulator:editIntro"), "launchers." + otherOs()), f.model->get("emulator:editIntro"));
        f.model->setScreen("emulator-edit", { { "id", "notepad" }, { "scope", "global" } });
        check(!contains(f.model->get("emulator:editIntro"), "Only this file"), "nothing to say without one");
    });

    run("emulator edit: the command prompt's own variables are allowed", [] {
        checkEqual(Emulators::checkLauncherValues("", "/c \"%APPDATA%\\tool.bat\" \"%ITEM_FILEPATH%\"", "", "%CMD%"), "",
                   "%CMD% expands %APPDATA% itself");
        checkEqual(Emulators::checkLauncherValues("C:\\Tools\\run.bat", "%USERPROFILE%", ""), "", "so does a batch file");
        check(!Emulators::checkLauncherValues("emu.exe", "%APPDATA%", "").empty(), "another program gets it as it is");
        check(!Emulators::checkLauncherValues("", "", "%APPDATA%", "cmd.exe").empty(), "the working folder is RetroFE's to replace");
    });

    run("emulator rename: a failure part way leaves every file as it was", [] {
        std::string root = makeInstall("edit-rename-rollback");
        auto index = build(root);
        // a.conf's temp file can't be written: the rename fails after the new
        // launcher files and the settings files were written
        fs::create_directories(root + "/collections/Arcade/launchers/a.conf.tmp");
        std::map<std::string, std::string> before = snapshot(root);
        Library::Editor editor(root);
        Library::EditResult r = editor.renameLauncher(*index, "retro", "RetroArch NES");
        check(!r.ok && contains(r.message, "Nothing was changed"), r.message);
        std::map<std::string, std::string> after = snapshot(root);
        check(after == before, "every file byte for byte, and no new ones");
        for (const auto& [path, bytes] : after) {
            if (!before.count(path)) check(false, "new: " + path);
            else if (before.at(path) != bytes) check(false, "changed: " + path);
        }
    });

    run("emulator rename: settings.conf and info.conf both setting the launcher", [] {
        // info.conf is read last, so it wins: RetroFE starts NES with what info.conf names
        std::string root = makeInstall("edit-rename-both");
        writeFile(root + "/collections/NES/info.conf", "launcher = notepad\n");
        auto index = build(root);
        checkEqual(index->properties.getOr("collections.NES.launcher"), "notepad", "info.conf wins");
        Library::Editor editor(root);
        Library::Editor::RenamePlan plan = editor.planLauncherRename(*index, "retro", "other");
        check(plan.problem.empty(), plan.problem);
        check(std::any_of(plan.settingsFiles.begin(), plan.settingsFiles.end(),
                          [](const std::string& p) { return contains(p, "NES") && contains(p, "settings.conf"); }),
              "the overridden settings.conf line is renamed too, so it still works if info.conf goes");

        std::string root2 = makeInstall("edit-rename-both-2");
        writeFile(root2 + "/collections/NES/settings.conf", "list.extensions = nes\nlauncher = notepad\n");
        writeFile(root2 + "/collections/NES/info.conf", "launcher = retro\n");
        auto index2 = build(root2);
        Library::Editor editor2(root2);
        Library::EditResult r = editor2.renameLauncher(*index2, "retro", "other");
        check(r.ok, r.message);
        checkEqual(readFile(root2 + "/collections/NES/info.conf"), "launcher = other\n", "the effective one, in info.conf");
        checkEqual(readFile(root2 + "/collections/NES/settings.conf"), "list.extensions = nes\nlauncher = notepad\n", "the other untouched");
        auto after = build(root2);
        checkEqual(after->resolveLauncher(after->findCollection("NES")).name, "other", "NES now starts with the new name");
    });

    run("emulator rename: the collections are read once per launcher, not per name typed", [] {
        Fixture f(makeInstall("edit-rename-scan"));
        f.model->setScreen("emulator-rename", { { "id", "retro" }, { "scope", "global" } });
        for (const char* name : { "a", "ab", "notepad", "abc" }) {
            f.model->set("emulator:renameTo", name);
            f.model->get("emulator:renameProblem");
            f.model->list("emulator:renameChanges");
        }
        check(f.provider->renameScans() == 1, "one scan for four names: " + std::to_string(f.provider->renameScans()));
        check(contains(f.model->get("emulator:renameChangesText"), "becomes abc.conf"), "the last name's plan");
    });
});

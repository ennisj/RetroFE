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

// The Emulators section (Manager/Emulators): the launcher list, details,
// users, unresolved names, adding, duplicating and deleting launchers.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Manager/Emulators/EmulatorProvider.h"
#include "../Source/Manager/Emulators/Launchers.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Utility/Utils.h"
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    std::string os() { return Utils::getOSType(); }

    // An install with every way to define and use a launcher:
    //   launchers.<os>: retro (program present), notepad (program missing), spare (unused)
    //   Arcade: launcher = retro, notepad, nowhere; a.zip picks notepad, b.zip picks "lost"
    //   NES:    its own launcher.conf (beats launcher = retro); local launcher "special" picked by mario,
    //           local launcher "unused"
    //   SNES:   launcher = missingone
    //   Tools:  only a launcher.conf; Both: launcher.<os>.conf and launcher.conf (both unused)
    std::string makeInstall(const std::string& name)
    {
        std::string root = makeRoot(name);
        std::string launchers = root + "/launchers." + os() + "/";
        writeFile(root + "/emu/retroarch.exe", "x");
        writeFile(root + "/emu/cores/snes.dll", "x");
        writeFile(launchers + "retro.conf",
                  "# RetroArch\r\nexecutable = %RETROFE_PATH%/emu/retroarch.exe\r\narguments = -L cores/fceumm.dll \"%ITEM_FILEPATH%\"\r\n");
        // %RETROFE_PATH%: a bare relative path with a folder is "Windows only" elsewhere, not missing
        writeFile(launchers + "notepad.conf", "executable = %RETROFE_PATH%/emu/missing.exe\narguments = \"%ITEM_FILEPATH%\"\nreboot = yes\n");
        writeFile(launchers + "spare.conf", "executable = emu/retroarch.exe\n");

        writeFile(root + "/collections/Arcade/settings.conf", "list.extensions = zip\nlauncher = retro, notepad, nowhere\n");
        writeFile(root + "/collections/Arcade/roms/a.zip", "x");
        writeFile(root + "/collections/Arcade/roms/b.zip", "x");
        writeFile(root + "/collections/Arcade/launchers/a.conf", "notepad\n");
        writeFile(root + "/collections/Arcade/launchers/b.conf", "lost\n");

        writeFile(root + "/collections/NES/settings.conf", "list.extensions = nes\nlauncher = retro\n");
        writeFile(root + "/collections/NES/launcher.conf", "executable = emu/retroarch.exe\n");
        writeFile(root + "/collections/NES/launchers.local/special.conf",
                  "executable = emu/retroarch.exe\ncurrentDirectory = emu\nreboot = yes\n");
        writeFile(root + "/collections/NES/launchers.local/unused.conf", "executable = emu/tool.exe\n");
        // Collections with launcher files and no games: nothing uses their launchers
        writeFile(root + "/collections/Tools/launcher.conf", "executable = emu/tool.exe\n");
        writeFile(root + "/collections/Both/launcher." + os() + ".conf", "executable = emu/tool.exe\n");
        writeFile(root + "/collections/Both/launcher.conf", "executable = emu/tool.exe\n");
        writeFile(root + "/collections/NES/launchers/mario.conf", "special\n");
        writeFile(root + "/collections/NES/roms/mario.nes", "x");

        writeFile(root + "/collections/SNES/settings.conf", "list.extensions = sfc\nlauncher = missingone\n");
        writeFile(root + "/collections/SNES/roms/f-zero.sfc", "x");
        return root;
    }

    // A model with the Emulators provider over a built index
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
            model->addProvider(std::make_unique<EmulatorProvider>(services));
            indexer.rebuild();
            indexer.wait(10000);
        }

        static Library::ScanOptions options(const std::string& root)
        {
            Library::ScanOptions scan;
            scan.root = root;
            return scan;
        }

        const Forms::Row* row(const std::string& key, const std::string& id, const std::string& scope = "")
        {
            rows = model->rows(key, 200);
            for (const Forms::Row& item : rows.items) {
                auto found = item.params.find("scope");
                if (item.id == id && (scope.empty() || (found != item.params.end() && found->second == scope))) {
                    return &item;
                }
            }
            return nullptr;
        }

        Library::Indexer indexer;
        Library::Editor editor;
        std::unique_ptr<Configuration> config;
        std::unique_ptr<ManagerModel> model;
        Forms::Rows rows;
    };

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }
}

static Test::Suite emulators("emulators", [] {
    run("emulators: hub sees launchers after a catalog rescan", [] {
        auto root = makeRoot("emu-hub-rescan");
        Fixture f(root);
        checkEqual(f.model->get("info:hasLaunchers"), "no", "bare install");
        writeFile(root + "/launchers." + os() + "/Downloaded.conf", "executable = emulators/program.exe\n");
        f.indexer.changed("Catalog changed library files");
        check(f.indexer.wait(10000), "rescan finished");
        checkEqual(f.model->get("info:hasLaunchers"), "yes", "hub sees downloaded launcher");
        check(f.row("emulator:search", "Downloaded") != nullptr, "launcher screen sees it too");
    });

    run("emulators: the list shows where each launcher is, its program and who uses it", [] {
        Fixture f(makeInstall("emu-list"));
        f.model->setScreen("emulators", {});
        checkEqual(f.model->get("emulator:status"), "", "nothing to wait for once the index is built");
        Forms::Rows all = f.model->rows("emulator:search", 200);
        check(all.total == 8, "retro, notepad, spare, NES's own and two local launchers, Tools', Both's: " + std::to_string(all.total));

        const Forms::Row* retro = f.row("emulator:search", "retro", "global");
        check(retro && contains(retro->detail, "Global") && contains(retro->detail, "retroarch.exe")
              && contains(retro->detail, "1 system") && !contains(retro->detail, "missing"),
              "retro: where, program and users");
        const Forms::Row* notepad = f.row("emulator:search", "notepad", "global");
        check(notepad && contains(notepad->detail, "missing.exe (missing)") && contains(notepad->detail, "1 system, 1 game"),
              "notepad: missing program, an alternative of Arcade and a.zip's pick");
        const Forms::Row* spare = f.row("emulator:search", "spare");
        check(spare && contains(spare->detail, "Unused"), "spare is unused");
        const Forms::Row* nes = f.row("emulator:search", "NES", "collection");
        check(nes && contains(nes->detail, "NES's own file"), "a collection's own launcher file");
        const Forms::Row* local = f.row("emulator:search", "NES.special", "local");
        check(local && local->label == "special" && contains(local->detail, "NES, local") && contains(local->detail, "1 game"),
              "a collection-local launcher");

        f.model->set("emulator:query", "RETROARCH");
        check(f.model->rows("emulator:search", 200).total == 4, "search matches the program's name, ignoring case");
        f.model->set("emulator:query", "nes");
        check(f.model->rows("emulator:search", 200).total == 3, "and the collection");

        checkEqual(f.model->get("emulator:hasUnresolved"), "yes", "some names are used but not defined");
        Forms::Rows unresolved = f.model->rows("emulator:unresolved", 200);
        check(unresolved.items.size() == 3 && unresolved.items[0].id == "lost" && unresolved.items[1].id == "missingone"
              && unresolved.items[2].id == "nowhere", "a game's pick, a default and an alternative, by name");
        check(unresolved.items.size() == 3 && contains(unresolved.items[0].detail, "1 game")
              && contains(unresolved.items[1].detail, "1 system"), "with who asks for them");
        // A few are listed on Emulators itself; more than ten get a link with their count
        checkEqual(f.model->get("emulator:hasFewUnresolved"), "yes", "three: inline");
        checkEqual(f.model->get("emulator:hasManyUnresolved"), "no", "no link");
        checkEqual(f.model->get("emulator:unresolvedCount"), "3 names", "the count");
        std::string error;
        auto form = Forms::parse(Forms::builtinForm("emulators"), error);
        check(form && form->fields.size() > 1 && form->fields[1].open == "emulator-new", "Add a launcher comes first");
        check(Forms::builtinForm("emulators-unresolved") != nullptr, "the screen the link opens");
    });

    run("emulators: many names used but not defined become one link", [] {
        std::string root = makeInstall("emu-many-unresolved");
        for (int i = 0; i < 9; i++) {
            std::string system = "S" + std::to_string(i);
            writeFile(root + "/collections/" + system + "/settings.conf", "list.extensions = bin\nlauncher = gone" + std::to_string(i) + "\n");
        }
        Fixture f(root);
        f.model->setScreen("emulators", {});
        checkEqual(f.model->get("emulator:unresolvedCount"), "12 names", "three and nine more");
        checkEqual(f.model->get("emulator:hasManyUnresolved"), "yes", "a link");
        checkEqual(f.model->get("emulator:hasFewUnresolved"), "no", "not listed inline");
        f.model->setScreen("emulators-unresolved", {});
        check(f.model->rows("emulator:unresolved", 500).items.size() == 12, "every one on the link's screen");
    });

    run("emulators: a launcher's facts, file and users", [] {
        std::string root = makeInstall("emu-details");
        Fixture f(root);
        f.model->setScreen("emulator", { { "id", "retro" }, { "scope", "global" } });
        checkEqual(f.model->get("emulator:title"), "retro", "title");
        checkEqual(f.model->get("emulator:executableStatus"), "Yes", "the program is there");
        checkEqual(f.model->get("emulator:problem"), "", "no problem");
        checkEqual(f.model->get("emulator:workingFolder"), "The program's folder", "RetroFE's default working folder");
        checkEqual(f.model->get("emulator:reboot"), "", "no restart: hidden");
        check(contains(f.model->get("emulator:where"), "launchers." + os()), "defined globally");
        f.model->setScreen("emulator-file", { { "id", "retro" }, { "scope", "global" } });
        checkEqual(f.model->get("emulator:fileText"),
                   "# RetroArch\nexecutable = %RETROFE_PATH%/emu/retroarch.exe\narguments = -L cores/fceumm.dll \"%ITEM_FILEPATH%\"",
                   "the file verbatim, comments too, a note line per line");
        f.model->setScreen("emulator", { { "id", "retro" }, { "scope", "global" } });
        checkEqual(f.model->get("emulator:usersSummary"), "The default of 1 system.", "summary");
        Forms::Rows systems = f.model->rows("emulator:systems", 200);
        check(systems.items.size() == 1 && systems.items[0].id == "Arcade" && systems.items[0].detail == "Default",
              "Arcade uses it by default; NES's own file wins over its setting");

        f.model->setScreen("emulator", { { "id", "notepad" }, { "scope", "global" } });
        check(contains(f.model->get("emulator:executableStatus"), "won't start"), "a missing program is flagged");
        checkEqual(f.model->get("emulator:fileName"), "notepad.conf", "the file's name, for the link");
        checkEqual(f.model->get("emulator:reboot"), "Yes", "reboot");
        checkEqual(f.model->get("emulator:usersSummary"), "An alternative of 1 system; 1 game picks it.", "summary");
        f.model->setScreen("emulator-users", { { "id", "notepad" }, { "scope", "global" } });
        Forms::Rows games = f.model->rows("emulator:games", 200);
        check(games.items.size() == 1 && games.items[0].id == "a" && games.items[0].params.at("collection") == "Arcade",
              "a game row opens the game screen by collection and id");
        Forms::Rows alternatives = f.model->rows("emulator:systems", 200);
        check(alternatives.items.size() == 1 && alternatives.items[0].detail == "Alternative", "an alternative");

        f.model->setScreen("emulator", { { "id", "NES" }, { "scope", "collection" } });
        check(contains(f.model->get("emulator:where"), "NES's own launcher file"), "collection launcher");
        Forms::Rows nes = f.model->rows("emulator:systems", 200);
        check(nes.items.size() == 1 && nes.items[0].detail == "Default, its own launcher file", "used through its own file");

        f.model->setScreen("emulator", { { "id", "gone" }, { "scope", "global" } });
        check(contains(f.model->get("emulator:problem"), "no launcher called"), "an unknown launcher says so");
        check(!f.model->actionProblem("emulator.delete").empty(), "and can't be deleted");
    });

    run("emulators: adding a launcher", [] {
        std::string root = makeInstall("emu-add");
        Fixture f(root);
        f.model->setScreen("emulator-new", {});
        checkEqual(f.model->get("emulator:newPreset"), "file", "the game's file is the default preset");
        checkEqual(f.model->actionProblem("emulator.create"), "Give the launcher a name.", "a name is needed");
        f.model->set("emulator:newName", "RETRO");
        check(contains(f.model->actionProblem("emulator.create"), "already a launcher called \"retro\""), "names are unique, ignoring case");
        f.model->set("emulator:newName", "bad/name");
        check(contains(f.model->actionProblem("emulator.create"), "can't contain"), "a name is a file name");
        f.model->set("emulator:newName", "SNES9x");
        checkEqual(f.model->actionProblem("emulator.create"), "Choose the program the launcher runs.", "a program is needed");
        f.model->set("emulator:newExecutable", root + "/emu/nothere.exe");
        check(contains(f.model->actionProblem("emulator.create"), "isn't there"), "the program must exist");
        f.model->set("emulator:newExecutable", root + "/emu/retroarch.exe");
        f.model->set("emulator:newPreset", "retroarch");
        checkEqual(f.model->get("emulator:needsCore"), "yes", "RetroArch asks for a core");
        checkEqual(f.model->actionProblem("emulator.create"), "Choose RetroArch's core file.", "core needed");
        f.model->set("emulator:newCore", root + "/emu/cores/snes.dll");
        checkEqual(f.model->actionProblem("emulator.create"), "", "ready");
        check(!f.model->leaving(f.model->screen()).empty(), "leaving warns while it isn't created");
        std::string command = f.model->get("emulator:newCommand");
        check(contains(command, "%RETROFE_PATH%") && contains(command, "-L \"%RETROFE_PATH%") && contains(command, "\"%ITEM_FILEPATH%\""),
              "paths inside RetroFE's folder are stored relative to it: " + command);

        Forms::Model::Result result = f.model->runAction("emulator.create");
        check(result.ok && result.back, "created, and back: " + result.message);
        std::string file = root + "/launchers." + os() + "/SNES9x.conf";
        std::string content = readFile(file);
        check(content.rfind("executable = %RETROFE_PATH%", 0) == 0 && contains(content, "arguments = -L \"%RETROFE_PATH%")
              && !contains(content, "currentDirectory"), "the new file: " + content);
        check(fs::exists(root + "/manage/backups/launchers." + os() + "/SNES9x.conf.absent"), "recorded as new in the backups");
        check(f.indexer.changesPending(), "RetroFE reloads when the manager closes");
        checkEqual(f.model->leaving(f.model->screen()), "", "nothing left to lose");
        f.model->setScreen("emulators", {});
        check(f.row("emulator:search", "SNES9x", "global") != nullptr, "listed straight away, before the index is rebuilt");

        f.indexer.wait(10000);
        f.model->setScreen("emulators", {});
        check(f.row("emulator:search", "SNES9x", "global") != nullptr, "listed once the index is rebuilt");
        f.model->setScreen("emulator", { { "id", "SNES9x" }, { "scope", "global" } });
        checkEqual(f.model->get("emulator:executableStatus"), "Yes", "RetroFE finds its program through %RETROFE_PATH%");

        // The next visit starts afresh
        f.model->setScreen("emulator-new", {});
        checkEqual(f.model->get("emulator:newName"), "", "the form is empty again");
        f.model->set("emulator:newName", "Custom");
        f.model->set("emulator:newExecutable", root + "/emu/retroarch.exe");
        f.model->set("emulator:newPreset", "custom");
        f.model->set("emulator:newArguments", "\"%ITEM_FILEPATH%\" # no");
        check(contains(f.model->actionProblem("emulator.create"), "can't contain #"), "# would start a comment");
    });

    run("emulators: an unresolved name prefills the add form", [] {
        Fixture f(makeInstall("emu-unresolved"));
        f.model->setScreen("emulators", {});
        f.model->rows("emulator:unresolved", 200);
        f.model->setScreen("emulator-new", { { "name", "lost" } });
        checkEqual(f.model->get("emulator:newName"), "lost", "named after the missing launcher");
        check(contains(f.model->get("emulator:newIntro"), "1 game picks it"), "says who asks for it");
        checkEqual(f.model->leaving(f.model->screen()), "", "nothing typed yet: no warning");
    });

    run("emulators: duplicate and edit leaves the original alone", [] {
        std::string root = makeInstall("emu-duplicate");
        Fixture f(root);
        std::string original = readFile(root + "/launchers." + os() + "/retro.conf");
        f.model->setScreen("emulator", { { "id", "retro" }, { "scope", "global" } });
        Forms::Model::Result duplicate = f.model->runAction("emulator.duplicate");
        check(duplicate.ok && duplicate.open == "emulator-new" && duplicate.params["from"] == "retro"
              && duplicate.params["fromScope"] == "global", "opens the add form with the launcher to copy");

        f.model->setScreen("emulator-new", duplicate.params);
        checkEqual(f.model->get("emulator:newName"), "retro copy", "a free name");
        checkEqual(f.model->get("emulator:newPreset"), "custom", "its arguments as custom text");
        checkEqual(f.model->get("emulator:newArguments"), "-L cores/fceumm.dll \"%ITEM_FILEPATH%\"", "copied");
        checkEqual(f.model->get("emulator:newExecutable"), "%RETROFE_PATH%/emu/retroarch.exe", "copied as written");
        checkEqual(f.model->leaving(f.model->screen()), "", "unchanged copy: no warning");
        f.model->set("emulator:newArguments", "-L cores/snes9x.dll \"%ITEM_FILEPATH%\"");
        check(!f.model->leaving(f.model->screen()).empty(), "an edited copy warns");
        Forms::Model::Result created = f.model->runAction("emulator.create");
        check(created.ok, "created: " + created.message);
        check(contains(readFile(root + "/launchers." + os() + "/retro copy.conf"), "snes9x"), "the copy has the change");
        checkEqual(readFile(root + "/launchers." + os() + "/retro.conf"), original, "the original is byte for byte the same");

        f.indexer.wait(10000);
        f.model->setScreen("emulator", { { "id", "retro" }, { "scope", "global" } });
        f.model->runAction("emulator.duplicate");
        f.model->setScreen("emulator-new", duplicate.params);
        checkEqual(f.model->get("emulator:newName"), "retro copy 2", "the next copy gets another name");
    });

    run("emulators: a copy of a launcher that restarts RetroFE restarts it too", [] {
        std::string root = makeInstall("emu-duplicate-reboot");
        // A program on this system's path, so creating a launcher with it is allowed
#ifdef _WIN32
        const std::string program = "cmd.exe";
#else
        const std::string program = "sh";
#endif
        writeFile(root + "/launchers." + os() + "/Switch.conf",
                  "executable = " + program + "\narguments = /c switch.bat\nreboot = yes\n");
        Fixture f(root);
        f.model->setScreen("emulator", { { "id", "Switch" }, { "scope", "global" } });
        Forms::Model::Result duplicate = f.model->runAction("emulator.duplicate");
        check(duplicate.ok, "duplicated: " + duplicate.message);
        f.model->setScreen("emulator-new", duplicate.params);
        checkEqual(f.model->get("emulator:newReboot"), "yes", "the copy restarts RetroFE as the original does");
        Forms::Model::Result created = f.model->runAction("emulator.create");
        check(created.ok, "created: " + created.message);
        check(contains(readFile(root + "/launchers." + os() + "/Switch copy.conf"), "reboot = yes"), "written to the copy");

        f.model->setScreen("emulator-new", {});
        f.model->set("emulator:newName", "Plain");
        f.model->set("emulator:newExecutable", program);
        Forms::Model::Result plain = f.model->runAction("emulator.create");
        check(plain.ok, "created: " + plain.message);
        check(!contains(readFile(root + "/launchers." + os() + "/Plain.conf"), "reboot"), "a new launcher doesn't restart RetroFE");
    });

    run("emulators: only unused launchers can be deleted", [] {
        std::string root = makeInstall("emu-delete");
        Fixture f(root);
        f.model->setScreen("emulator", { { "id", "notepad" }, { "scope", "global" } });
        check(contains(f.model->actionProblem("emulator.delete"), "still used"), "notepad is used");
        f.model->setScreen("emulator", { { "id", "NES.special" }, { "scope", "local" } });
        check(!f.model->actionProblem("emulator.delete").empty(), "a local launcher a game picks is used");

        auto index = f.indexer.index();
        Library::EditResult refused = f.editor.removeLauncher(*index, Library::LauncherScope::Global, "notepad");
        check(!refused.ok && contains(refused.message, "1 system and 1 game"), "the library refuses too: " + refused.message);

        f.model->setScreen("emulator", { { "id", "spare" }, { "scope", "global" } });
        checkEqual(f.model->actionProblem("emulator.delete"), "", "spare is unused");
        Forms::Model::Result result = f.model->runAction("emulator.delete");
        std::string file = root + "/launchers." + os() + "/spare.conf";
        check(result.ok && result.back && !fs::exists(file), "deleted, and back: " + result.message);
        check(fs::exists(root + "/manage/backups/launchers." + os() + "/spare.conf"), "kept in the backups");
        f.model->setScreen("emulators", {});
        std::string status = f.model->get("emulator:status");
        check(status == "Updating the list..." || f.indexer.index() != index, "says it's updating until the new index is in: " + status);
        check(f.row("emulator:search", "spare") == nullptr, "gone from the list straight away");
        check(f.row("emulator:search", "notepad") != nullptr, "the others stay");
        f.indexer.wait(10000);
        f.model->setScreen("emulators", {});
        check(f.row("emulator:search", "spare") == nullptr, "gone from the list");
    });

    run("emulators: library checks for new launchers", [] {
        std::string root = makeInstall("emu-library");
        Fixture f(root);
        auto index = f.indexer.index();
        checkEqual(Library::Editor::checkLauncherName("ok name"), "", "spaces inside are fine");
        check(!Library::Editor::checkLauncherName("a,b").empty(), "a comma would split launcher = a, b");
        check(!Library::Editor::checkLauncherName("con").empty(), "Windows device names");
        check(!Library::Editor::checkLauncherName(".hidden").empty(), "a leading dot");
        // Written by hand after the snapshot, in another case
        writeFile(root + "/launchers." + os() + "/Fresh.conf", "executable = x.exe\n");
        Library::Editor::NewLauncher fresh;
        fresh.name = "fresh";
        fresh.executable = "x.exe";
        Library::EditResult result = f.editor.addLauncher(*index, fresh);
        check(!result.ok && contains(result.message, "Fresh.conf"), "never overwrites a file that appeared since: " + result.message);
        checkEqual(readFile(root + "/launchers." + os() + "/Fresh.conf"), "executable = x.exe\n", "left alone");
    });

    run("emulators: the MAME preset as written", [] {
        std::string root = makeInstall("emu-mame");
        Fixture f(root);
        f.model->setScreen("emulator-new", {});
        f.model->set("emulator:newName", "MAME");
        f.model->set("emulator:newExecutable", root + "/emu/retroarch.exe");
        f.model->set("emulator:newPreset", "mame");
        Forms::Model::Result result = f.model->runAction("emulator.create");
        check(result.ok, "created: " + result.message);
        std::string content = readFile(root + "/launchers." + os() + "/MAME.conf");
        check(contains(content, "arguments = %ITEM_NAME% -rompath \"%ITEM_DIRECTORY%.\"" + std::string(Library::defaultNewline())),
              "the rom path stays one argument: " + content);
    });

    run("emulators: a new launcher can't be hidden by a collection or local launcher", [] {
        std::string root = makeInstall("emu-shadow");
        Fixture f(root);
        auto index = f.indexer.index();
        check(contains(Library::Editor::checkNewLauncherName(*index, "nes"), "own launcher file"),
              "NES's launcher.conf answers to \"NES\" first, in any case");
        check(contains(Library::Editor::checkNewLauncherName(*index, "nes.SPECIAL"), "local launcher of NES"),
              "a local launcher's id");
        check(contains(Library::Editor::checkNewLauncherName(*index, "NES.other"), "NES's local launchers"),
              "<collection>.<name> is kept for local launchers");
        checkEqual(Library::Editor::checkNewLauncherName(*index, "Arcade"), "",
                   "a collection's name is fine when it has no launcher file");
        checkEqual(Library::Editor::checkNewLauncherName(*index, "v1.2"), "", "a dot is fine when it names no collection");

        Library::Editor::NewLauncher shadowed;
        shadowed.name = "Tools";
        shadowed.executable = "x.exe";
        Library::EditResult refused = f.editor.addLauncher(*index, shadowed);
        check(!refused.ok && !fs::exists(root + "/launchers." + os() + "/Tools.conf"), "the library refuses it: " + refused.message);

        f.model->setScreen("emulator-new", {});
        f.model->set("emulator:newName", "NES");
        check(contains(f.model->actionProblem("emulator.create"), "never run"), "and the form says why");
    });

    run("emulators: deleting collection and local launchers", [] {
        std::string root = makeInstall("emu-delete-scopes");
        Fixture f(root);
        auto index = f.indexer.index();
        Library::EditResult used = f.editor.removeLauncher(*index, Library::LauncherScope::Collection, "NES");
        check(!used.ok && fs::exists(root + "/collections/NES/launcher.conf"), "NES's own launcher runs its games: " + used.message);

        f.model->setScreen("emulator", { { "id", "Both" }, { "scope", "collection" } });
        check(contains(f.model->actionProblem("emulator.delete"), "also has launcher.conf"), "launcher.conf would take over");
        Library::EditResult both = f.editor.removeLauncher(*index, Library::LauncherScope::Collection, "Both");
        check(!both.ok && fs::exists(root + "/collections/Both/launcher." + os() + ".conf"), "refused: " + both.message);

        f.model->setScreen("emulator", { { "id", "Tools" }, { "scope", "collection" } });
        checkEqual(f.model->actionProblem("emulator.delete"), "", "Tools' launcher is unused");
        Forms::Model::Result tools = f.model->runAction("emulator.delete");
        check(tools.ok && !fs::exists(root + "/collections/Tools/launcher.conf"), "a collection launcher file: " + tools.message);

        f.model->setScreen("emulator", { { "id", "NES.unused" }, { "scope", "local" } });
        Forms::Model::Result local = f.model->runAction("emulator.delete");
        check(local.ok && !fs::exists(root + "/collections/NES/launchers.local/unused.conf"), "a local launcher: " + local.message);
        check(fs::exists(root + "/manage/backups/collections/NES/launchers.local/unused.conf"), "kept in the backups");

        f.model->setScreen("emulators", {});
        check(!f.row("emulator:search", "Tools", "collection") && !f.row("emulator:search", "NES.unused", "local"),
              "both gone from the list at once");
    });

    run("emulators: restoring the original of a created or deleted launcher", [] {
        std::string root = makeInstall("emu-restore");
        Fixture f(root);
        auto index = f.indexer.index();
        Library::Editor::NewLauncher made;
        made.name = "made";
        made.executable = "x.exe";
        Library::EditResult created = f.editor.addLauncher(*index, made);
        std::string file = root + "/launchers." + os() + "/made.conf";
        check(created.ok && fs::exists(file), "created");
        check(f.editor.restoreOriginal(file).ok && !fs::exists(file), "restoring a created launcher deletes it");

        std::string spare = root + "/launchers." + os() + "/spare.conf";
        std::string original = readFile(spare);
        check(f.editor.removeLauncher(*index, Library::LauncherScope::Global, "spare").ok && !fs::exists(spare), "deleted");
        check(f.editor.restoreOriginal(spare).ok, "restored");
        checkEqual(readFile(spare), original, "restoring a deleted launcher brings it back as it was");
    });

    run("emulators: launcher names Windows or RetroFE can't use", [] {
        for (const char* name : { "con", "con.txt", "AUX.v2", "com1", "Lpt9.conf", "nul .x" }) {
            check(!Library::Editor::checkLauncherName(name).empty(), std::string("a device name: ") + name);
        }
        for (const char* name : { "console", "auxiliary", "com10", "prn2" }) {
            checkEqual(Library::Editor::checkLauncherName(name), "", std::string("not a device name: ") + name);
        }
        check(!Library::Editor::checkLauncherName("Caf\xc3\xa9").empty(), "plain ASCII only");
    });

    run("emulators: settings RetroFE ignores for collection and local launchers", [] {
        Fixture f(makeInstall("emu-ignored"));
        f.model->setScreen("emulator", { { "id", "NES.special" }, { "scope", "local" } });
        check(contains(f.model->get("emulator:workingFolder"), "ignored for this launcher"), "currentDirectory");
        checkEqual(f.model->get("emulator:reboot"), "Yes (ignored for this launcher)", "reboot");
        f.model->setScreen("emulator", { { "id", "notepad" }, { "scope", "global" } });
        checkEqual(f.model->get("emulator:reboot"), "Yes", "a global launcher's are used");
    });

    run("emulators: presets, portable paths and program checks", [] {
        checkEqual(Emulators::presetArguments("file", ""), "\"%ITEM_FILEPATH%\"", "file preset");
        checkEqual(Emulators::presetArguments("retroarch", "c.dll"), "-L \"c.dll\" \"%ITEM_FILEPATH%\"", "RetroArch preset");
        checkEqual(Emulators::presetArguments("mame", ""), "%ITEM_NAME% -rompath \"%ITEM_DIRECTORY%.\"",
                   "MAME preset: the folder ends in a separator, which mustn't escape the closing quote");
        std::string root = makeRoot("emu-paths");
        checkEqual(Emulators::portablePath(root, root + "/emu/a.exe", "linux"), "%RETROFE_PATH%/emu/a.exe", "inside the root");
        checkEqual(Emulators::portablePath(root, "emu/a.exe", "windows"), "%RETROFE_PATH%\\emu\\a.exe", "relative to the root");
        checkEqual(Emulators::portablePath(root, "cmd.exe", "windows"), "cmd.exe", "a bare program name stays");
        checkEqual(Emulators::portablePath(root, "%RETROFE_PATH%/x", "windows"), "%RETROFE_PATH%/x", "already portable");
        check(!contains(Emulators::portablePath(root, root + "-other/a.exe", "linux"), "%RETROFE_PATH%"),
              "a sibling folder with the same prefix isn't inside");
        checkEqual(Emulators::portablePath(root, root + "/my emus/Retro Arch.exe", "linux"),
                   "%RETROFE_PATH%/my emus/Retro Arch.exe", "spaces are kept");
#ifdef _WIN32
        std::string lowerDrive = root;
        lowerDrive[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowerDrive[0])));
        checkEqual(Emulators::portablePath(root, lowerDrive + "\\my emus\\a.exe", "windows"),
                   "%RETROFE_PATH%\\my emus\\a.exe", "the drive letter's case doesn't matter on Windows");
#endif

        Library::Index index;
        index.options.root = root;
        index.options.os = os();
        check(Emulators::checkExecutable(index, "%ITEM_DIRECTORY%/run.exe").state == Emulators::ExecutableCheck::State::Unchecked,
              "per-game programs aren't checked");
        check(Emulators::checkExecutable(index, "").state == Emulators::ExecutableCheck::State::Missing, "no program");
#ifdef _WIN32
        const char* onPath = "cmd.exe";
#else
        const char* onPath = "sh";
#endif
        Emulators::ExecutableCheck bare = Emulators::checkExecutable(index, onPath);
        check(bare.state == Emulators::ExecutableCheck::State::OnPath && contains(bare.sentence, "system path"),
              std::string("a bare program name is found on the system path: ") + bare.sentence);
        check(Emulators::checkExecutable(index, "no-such-program-here").state == Emulators::ExecutableCheck::State::Missing,
              "a bare name that's nowhere is missing");
        Library::Index cabinet;
        cabinet.options.root = root;
        cabinet.options.os = "linux";
        check(Emulators::checkExecutable(cabinet, "emu/retroarch").state == Emulators::ExecutableCheck::State::WindowsOnly,
              "a relative program path in a Linux launcher works on Windows only");
        check(Emulators::checkExecutable(cabinet, "/usr/bin/retroarch").state != Emulators::ExecutableCheck::State::WindowsOnly,
              "an absolute one is fine");
        index.options.os = os() == "linux" ? "windows" : "linux";
        check(Emulators::checkExecutable(index, "/bin/x").state == Emulators::ExecutableCheck::State::Unchecked,
              "another platform's launchers aren't checked here");
    });
});

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

// A whole first setup, as Scripts/test-build/README.md's Starter checklist
// has the owner do it on the PC: from an empty root to games that start.
// One install is built up step by step through Manage's model, each step
// checked before the next. RetroArch is retrofe_fake_emulator under
// RetroArch's name, which writes down how it was started.

#include "TestSupport.h"
#include "StarterFixtures.h"
#include "../Source/Builtin/Builtin.h"
#include "../Source/Collection/CollectionInfo.h"
#include "../Source/Collection/CollectionInfoBuilder.h"
#include "../Source/Collection/Item.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Database/DB.h"
#include "../Source/Database/MetadataDatabase.h"
#include "../Source/Execute/LaunchCommand.h"
#include "../Source/Forms/Form.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Manager/Emulators/EmulatorProvider.h"
#include "../Source/Manager/Games/GameProvider.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Systems/SystemProvider.h"
#include "../Source/Utility/Utils.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

#ifdef _WIN32
    const char* const programName = "retroarch.exe";
    const char* const coreExtension = ".dll";
#else
    const char* const programName = "retroarch";
    const char* const coreExtension = ".so";
#endif

    // The Starter systems, with what their RetroArch core's .info file says
    // (as RetroArch ships it) and what the checklist expects
    struct System
    {
        std::string name;          // typed in Add a system, as the staged folder is called
        std::string core;          // cores/<core>_libretro
        std::string launcher;      // the name typed in Launchers > Add a launcher
        std::string infoName;      // systemname in the .info file
        std::string infoExtensions;
        std::string suggestedName;  // what Add a system offers: systemname, cleaned
        std::string suggestedExtensions;
        std::string extensions;    // what the owner leaves in the form
        size_t games;
    };

    const std::vector<System>& systems()
    {
        static const std::vector<System> all = {
            { "Nintendo Entertainment System", "fceumm", "RetroArch NES",
              "Nintendo Entertainment System", "fds|nes|unf|unif",
              "Nintendo Entertainment System", "fds,nes,unf,unif", "fds,nes,unf,unif,dat", 12 },
            { "Sega Genesis", "genesis_plus_gx", "RetroArch Genesis",
              "Sega 8/16-bit (Various)", "mdx|md|smd|gen|bin|cue|iso|chd|bms|sms|gg|sg|68k|sgd|m3u",
              // A system name can't hold "/" (round 18): each becomes "-"
              "Sega 8-16-bit (Various)", "mdx,md,smd,gen,bin,cue,iso,chd,bms,sms,gg,sg,68k,sgd,m3u",
              "mdx,md,smd,gen,bin,cue,iso,chd,bms,sms,gg,sg,68k,sgd,m3u", 10 },
            { "MAME", "mame2003_plus", "RetroArch MAME",
              "Arcade (various)", "zip",
              "Arcade (various)", "zip", "zip", 10 },
        };
        return all;
    }

    // starter.txt's games of a system: their paths under downloads/<system>/roms
    std::vector<std::string> starterGames(const std::string& system)
    {
        std::vector<std::string> games;
        std::string prefix = system + "/roms/";
        for (const std::string& target : StarterFixtures::targets()) {
            if (target.compare(0, prefix.size(), prefix) == 0) {
                games.push_back(target.substr(prefix.size()));
            }
        }
        return games;
    }

    // A game's name in RetroFE: its file name without the extension
    std::string gameName(const std::string& file)
    {
        return fs::path(file).stem().string();
    }

    std::vector<std::string> lines(const std::string& text)
    {
        std::vector<std::string> result;
        std::string line;
        for (char c : text) {
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                result.push_back(line);
                line.clear();
            }
            else {
                line += c;
            }
        }
        if (!line.empty()) result.push_back(line);
        return result;
    }

    bool samePath(const std::string& a, const fs::path& b)
    {
        std::error_code ec;
        return !a.empty() && fs::equivalent(fs::path(a), b, ec);
    }

    // RetroFE's configuration as it reads it when it starts (Main.cpp's
    // ImportConfiguration): settings.conf or the built-in defaults, the
    // launchers, and each collection's settings, info and launcher files.
    // Without the log setting: it starts logging, which sends this process's
    // output to the install's log.txt.
    void loadConfiguration(Configuration& config, const std::string& root)
    {
        std::string settings = Utils::combinePath(root, "settings.conf");
        std::string text = fs::exists(settings) ? readFile(settings) : std::string(Builtin::settings);
        std::string kept;
        for (const std::string& line : lines(text)) {
            if (Utils::trimEnds(line.substr(0, line.find('='))) != "log") kept += line + "\n";
        }
        config.importString("", kept);
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(Utils::combinePath(root, "launchers." + Utils::getOSType()), ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".conf") {
                config.import("launchers." + entry.path().stem().string(), entry.path().string());
            }
        }
        std::string collections = Utils::combinePath(root, "collections");
        for (const auto& entry : fs::directory_iterator(collections, ec)) {
            std::string collection = entry.path().filename().string();
            if (!entry.is_directory() || collection.empty() || collection[0] == '_') continue;
            std::string prefix = "collections." + collection;
            for (const char* file : { "settings.conf", "info.conf" }) {
                std::string path = Utils::combinePath(collections, collection, file);
                if (fs::exists(path)) config.import(collection, prefix, path, false);
            }
            std::string own = Utils::combinePath(collections, collection, "launcher." + Utils::getOSType() + ".conf");
            if (!fs::exists(own)) own = Utils::combinePath(collections, collection, "launcher.conf");
            if (fs::exists(own)) {
                config.import(collection, "collectionLaunchers." + collection, own, false);
                if (!config.propertyExists(prefix + ".launcher")) config.setProperty(prefix + ".launcher", collection);
            }
        }
    }

    // How the fake RetroArch was started: one entry per run of it
    struct Launch
    {
        std::string folder;
        std::vector<std::string> arguments;
    };

    std::vector<Launch> readLaunches(const std::string& file)
    {
        std::vector<Launch> launches;
        bool inside = false;
        bool folder = false;
        for (const std::string& line : lines(readFile(file))) {
            if (!inside && line == "launch") {
                launches.emplace_back();
                inside = folder = true;
            }
            else if (inside && folder) {
                launches.back().folder = line;
                folder = false;
            }
            else if (inside && line == "end") {
                inside = false;
            }
            else if (inside) {
                launches.back().arguments.push_back(line);
            }
        }
        return launches;
    }

    // Starts the command as Launcher::execute() does at its core, and waits:
    // CreateProcess with its command line on Windows, fork and execvp with
    // its argv elsewhere, both in its current directory. "" when it ran and
    // exited 0, else what happened.
    std::string startAndWait(const LaunchCommand::Command& command)
    {
#ifdef _WIN32
        std::string commandLine = LaunchCommand::windowsCommandLine(command.executable, command.arguments);
        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessA(nullptr, &commandLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            command.currentDirectory.c_str(), &startupInfo, &processInfo)) {
            return "CreateProcess failed, error " + std::to_string(GetLastError());
        }
        WaitForSingleObject(processInfo.hProcess, 30000);
        DWORD exitCode = 1;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return exitCode == 0 ? "" : "exit code " + std::to_string(exitCode);
#else
        std::vector<std::string> argv = LaunchCommand::unixArgv(command.executable, command.arguments);
        std::vector<char*> args;
        for (std::string& arg : argv) {
            args.push_back(&arg[0]);
        }
        args.push_back(nullptr);
        pid_t pid = fork();
        if (pid == 0) {
            if (!command.currentDirectory.empty() && chdir(command.currentDirectory.c_str()) != 0) _exit(126);
            execvp(args[0], args.data());
            _exit(127);
        }
        if (pid < 0) {
            return "fork failed";
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status)) return "ended by signal " + std::to_string(WTERMSIG(status));
        // 126: chdir failed; 127: execvp failed
        return WEXITSTATUS(status) == 0 ? "" : "exit code " + std::to_string(WEXITSTATUS(status));
#endif
    }

    // The install and Manage as it runs on it: one model with the sections
    // the checklist uses (Launchers, Systems, Games), on one library
    struct Setup
    {
        std::string base;
        std::string root;        // RetroFE's root
        std::string downloads;   // what a first setup downloaded: <root>/../downloads/<system>/...
        Configuration config;
        Library::Indexer indexer;
        Library::Editor editor;
        ManagerModel model;
        Manager::GameProvider* games = nullptr;

        explicit Setup(const std::string& base)
            : base(base)
            , root(useRoot(base + "/RetroFE"))
            , downloads(base + "/downloads")
            , indexer(options(root))
            , editor(root)
            , model(config)
        {
            Manager::Services services{ config };
            services.library = &indexer;
            services.editor = &editor;
            model.addProvider(std::make_unique<EmulatorProvider>(services));
            model.addProvider(std::make_unique<Manager::SystemProvider>(services, false));
            model.addProvider(std::make_unique<Manager::SystemProvider>(services, true));
            auto owned = std::make_unique<Manager::GameProvider>(services);
            games = owned.get();
            model.addProvider(std::move(owned));
            indexer.rebuild();
            settle();
            model.refresh();
        }

        static std::string useRoot(const std::string& root)
        {
            Configuration::absolutePath = root;
            return root;
        }

        static Library::ScanOptions options(const std::string& root)
        {
            Library::ScanOptions scan;
            scan.root = root;
            return scan;
        }

        std::string program() const { return Utils::combinePath(root, "emulators", "RetroArch", programName); }
        std::string core(const System& system) const
        {
            return Utils::combinePath(root, "emulators", "RetroArch", "cores", system.core + "_libretro" + coreExtension);
        }
        std::string launches() const { return Utils::combinePath(root, "emulators", "RetroArch", "launches.txt"); }

        void settle()
        {
            indexer.wait(20000);
            games->update(0);
            games->finishPendingWork();
        }

        // Manage closed and opened again after files changed by hand
        void rescan()
        {
            settle();
            indexer.rebuild();
            settle();
        }

        void open(const std::string& form, const Forms::Params& params)
        {
            settle();
            model.setScreen(form, params);
            games->update(0);
            games->finishPendingWork();
        }

        // Add games' worker: counting, or copying
        void waitForImport()
        {
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (model.get("system:importRunning") == "yes" && std::chrono::steady_clock::now() < until) {
                model.update(0.01f);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            model.update(0.01f);
            settle();
        }

        std::vector<std::string> ids(const std::string& key)
        {
            std::vector<std::string> result;
            for (const Forms::Row& row : model.rows(key, 500).items) {
                result.push_back(row.id);
            }
            return result;
        }

        size_t gameCount(const std::string& system)
        {
            auto index = indexer.index();
            int c = index ? index->findCollection(system) : -1;
            return c >= 0 ? index->collections[static_cast<size_t>(c)].gameCount : 0;
        }

        // The hub's link that opens form, shown or not
        bool hubShows(const std::string& form)
        {
            std::string error;
            auto hub = Forms::parse(Forms::builtinForm("manage"), error);
            const Forms::Field* link = hub ? Forms::findField(*hub, form) : nullptr;
            return link && Forms::isVisible(*link, model);
        }
    };

    bool has(const std::vector<std::string>& values, const std::string& value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }
}

static Test::Suite firstSetup("first setup", [] {
    std::unique_ptr<Setup> s;
    std::string base;

    // Checklist step 1 (what the owner installs first): an empty root as a
    // release ships it, RetroArch with its three cores and their .info files,
    // and the Starter downloads beside the root
    run("first setup a: an empty root, RetroArch and the downloads", [&] {
        base = makeRoot("first-setup");
        std::string root = base + "/RetroFE";
        fs::create_directories(root);
        check(!fs::exists(root + "/settings.conf") && !fs::exists(root + "/collections"), "no settings.conf, no collections");

        std::string retroarch = Utils::combinePath(root, "emulators", "RetroArch");
        fs::create_directories(retroarch);
        std::error_code ec;
#ifdef _WIN32
        fs::copy_file(RETROFE_FAKE_EMULATOR, Utils::combinePath(retroarch, programName), fs::copy_options::overwrite_existing, ec);
#else
        // A symlink rather than a copy: the temp folder may be mounted noexec
        // (it is on the test worker); the fake writes beside the path it's started by
        fs::create_symlink(fs::absolute(RETROFE_FAKE_EMULATOR), Utils::combinePath(retroarch, programName), ec);
#endif
        check(!ec, "the fake RetroArch is in place: " + ec.message());
        for (const System& system : systems()) {
            writeFile(Utils::combinePath(retroarch, "cores", system.core + "_libretro" + coreExtension), system.core);
            writeFile(Utils::combinePath(retroarch, "info", system.core + "_libretro.info"),
                      "systemname = \"" + system.infoName + "\"\nsupported_extensions = \"" + system.infoExtensions + "\"\n");
        }

        // A stand-in for each download: small files, each with its own content
        // (its path); a folder of pictures gets one picture
        auto targets = StarterFixtures::targets();
        check(targets.size() > 100, "the Starter list is read");
        for (const std::string& target : targets) {
            if (!target.empty() && target.back() == '/') {
                writeFile(base + "/downloads/" + target + "1.png", target + "1.png");
            }
            else {
                writeFile(base + "/downloads/" + target, target);
            }
        }
        check(fs::exists(base + "/downloads/Nintendo Entertainment System/roms/More games/Contra (USA).nes"), "Contra in More games");
    });

    // Manage, opened on the bare install: the hub starts with the first launcher
    run("first setup b: the hub offers the first launcher, and there's no system", [&] {
        s = std::make_unique<Setup>(base);
        s->open("manage", {});
        checkEqual(s->model.get("info:hasLaunchers"), "no", "no launcher");
        check(s->hubShows("emulator-new"), "Add your first launcher/emulator is shown");
        checkEqual(s->model.get("system:hasSystems"), "no", "no system");
        checkEqual(s->model.get("info:hasCollections"), "no", "no collections");
        check(s->hubShows("system-add"), "Add your first system is shown too, below it");
        s->open("systems", {});
        check(s->ids("system:search").empty(), "Systems lists nothing");
    });

    // Checklist step 2: Manage > Launchers, a launcher for each core
    run("first setup c: a RetroArch launcher for each core", [&] {
        if (!s) return;
        for (const System& system : systems()) {
            s->open("emulator-new", {});
            s->model.set("emulator:newName", system.launcher);
            s->model.set("emulator:newExecutable", s->program());
            s->model.set("emulator:newPreset", "retroarch");
            checkEqual(s->model.get("emulator:needsCore"), "yes", system.launcher + ": RetroArch asks for a core");
            s->model.set("emulator:newCore", s->core(system));
            checkEqual(s->model.actionProblem("emulator.create"), "", system.launcher + ": ready");
            Forms::Model::Result created = s->model.runAction("emulator.create");
            check(created.ok && created.back, system.launcher + " created: " + created.message);
            std::string file = s->root + "/launchers." + Utils::getOSType() + "/" + system.launcher + ".conf";
            std::string content = readFile(file);
            check(contains(content, "executable = %RETROFE_PATH%") && contains(content, "-L \"%RETROFE_PATH%")
                  && contains(content, system.core + "_libretro" + coreExtension + "\" \"%ITEM_FILEPATH%\""),
                  system.launcher + ": its file: " + content);
            s->settle();
        }
        s->open("emulators", {});
        std::vector<std::string> listed = s->ids("emulator:search");
        for (const System& system : systems()) {
            check(has(listed, system.launcher), system.launcher + " is listed");
            s->open("emulator", { { "id", system.launcher }, { "scope", "global" } });
            checkEqual(s->model.get("emulator:executableStatus"), "Yes", system.launcher + ": its program is found");
        }
        s->open("manage", {});
        checkEqual(s->model.get("info:hasLaunchers"), "yes", "the hub sees the launchers");
        check(!s->hubShows("emulator-new"), "the first-launcher link is gone");
        check(s->hubShows("system-add"), "the first-system link is still there");
    });

    // Checklist step 3: Systems > Add a system with each launcher; the name and
    // extensions come from the core's .info file; the owner types the names
    // of the staged folders and adds dat for NES
    run("first setup d: a system per launcher, suggested from its core's .info file", [&] {
        if (!s) return;
        for (const System& system : systems()) {
            s->open("system-add", {});
            s->model.set("system:addLauncher", system.launcher);
            checkEqual(s->model.get("system:addName"), system.suggestedName, system.name + ": the suggested name");
            checkEqual(s->model.get("system:addExtensions"), system.suggestedExtensions, system.name + ": the suggested extensions");
            s->model.set("system:addName", system.name);
            s->model.set("system:addExtensions", system.extensions);
            checkEqual(s->model.get("system:addMenu"), "Main", system.name + ": listed in the top menu by default");
            checkEqual(s->model.actionProblem("system.create"), "", system.name + ": ready");
            Forms::Model::Result created = s->model.runAction("system.create");
            check(created.ok && created.open == "system" && created.params["id"] == system.name, system.name + " created: " + created.message);
            s->settle();

            std::string settings = readFile(s->root + "/collections/" + system.name + "/settings.conf");
            check(contains(settings, "list.extensions = " + system.extensions + "\n")
                  || contains(settings, "list.extensions = " + system.extensions + "\r\n"), system.name + ": extensions saved: " + settings);
            check(contains(settings, "launcher = " + system.launcher), system.name + ": launcher saved: " + settings);
            check(!contains(settings, "list.path"), system.name + ": the default games folder, so no list.path");
        }
        std::vector<std::string> menu = lines(readFile(s->root + "/collections/Main/menu.txt"));
        s->open("systems", {});
        std::vector<std::string> listed = s->ids("system:search");
        for (const System& system : systems()) {
            check(has(menu, system.name), system.name + " is in the top menu (Main/menu.txt)");
            check(has(listed, system.name), system.name + " is on the Systems screen");
        }
        s->open("manage", {});
        checkEqual(s->model.get("system:hasSystems"), "yes", "the hub sees the systems");
        check(!s->hubShows("system-add"), "the first-system link is gone");
    });

    // Checklist step 4: Add games for each system from its staged roms folder,
    // Include subfolders on; a second Copy finds them all there
    run("first setup e: Add games copies 12, 10 and 10 games", [&] {
        if (!s) return;
        for (const System& system : systems()) {
            std::string count = std::to_string(system.games);
            s->open("system-add-games", { { "id", system.name } });
            checkEqual(s->model.get("system:importSubfolders"), "yes", system.name + ": Include subfolders is on");
            checkEqual(s->model.get("system:importInto"), (fs::path("collections") / system.name / "roms").make_preferred().string(),
                       system.name + ": into its games folder");
            s->model.set("system:importFrom", s->downloads + "/" + system.name + "/roms");
            s->waitForImport();
            checkEqual(s->model.get("system:importMessage"), count + " games found", system.name + ": found");
            check(s->model.runAction("system.copyGames").ok, system.name + ": Copy starts");
            s->waitForImport();
            check(contains(s->model.get("system:importMessage"), "Copied " + count + " games into"),
                  system.name + ": copied: " + s->model.get("system:importMessage"));
            check(s->gameCount(system.name) == system.games,
                  system.name + ": the library has them: " + std::to_string(s->gameCount(system.name)));
            check(s->model.runAction("system.copyGames").ok, system.name + ": a second Copy starts");
            s->waitForImport();
            check(contains(s->model.get("system:importMessage"), "No new games: all " + count + " are already in"),
                  system.name + ": all already there: " + s->model.get("system:importMessage"));
        }
        // Not kept in More games: a new system doesn't read subfolders, so Add games puts every game in one folder
        check(fs::exists(s->root + "/collections/Nintendo Entertainment System/roms/Contra (USA).nes"), "Contra is beside the others");
    });

    // Checklist step 5, Browse: each collection as RetroFE builds it when it loads it
    run("first setup f: Browse lists every game by its exact file name", [&] {
        if (!s) return;
        Configuration::absolutePath = s->root;
        Configuration config;
        loadConfiguration(config, s->root);
        DB db(base + "/meta.db");
        db.initialize();
        MetadataDatabase meta(db, config);
        CollectionInfoBuilder builder(config, meta);
        for (const System& system : systems()) {
            std::unique_ptr<CollectionInfo> collection(builder.buildCollection(system.name));
            std::vector<std::string> items;
            for (const Item* item : collection->items) {
                items.push_back(item->name);
            }
            check(items.size() == system.games, system.name + ": " + std::to_string(items.size()) + " games");
            checkEqual(collection->launcher, system.launcher, system.name + ": its launcher");
            for (const std::string& file : starterGames(system.name)) {
                check(has(items, gameName(file)), system.name + " lists \"" + gameName(file) + "\"");
            }
        }
    });

    // Manage > Games: each game by its exact name
    run("first setup g: Games finds each game by its exact name", [&] {
        if (!s) return;
        s->open("games", {});
        for (const System& system : systems()) {
            for (const std::string& file : starterGames(system.name)) {
                std::string name = gameName(file);
                s->model.set("game:query", name);
                check(has(s->ids("game:search"), name), "searching for \"" + name + "\" finds it");
            }
        }
        s->model.set("game:query", "");
    });

    // Checklist step 5, start one game from each system (and Contra, which was
    // in More games, and Little Lancelot, a .dat): the launch command RetroFE
    // builds, run as RetroFE runs it, reaches the fake RetroArch unchanged
    run("first setup h: a game from each system starts with its core and file", [&] {
        if (!s) return;
        Configuration::absolutePath = s->root;
        Configuration config;
        loadConfiguration(config, s->root);
        DB db(base + "/meta.db");
        db.initialize();
        MetadataDatabase meta(db, config);
        CollectionInfoBuilder builder(config, meta);
        struct Start { size_t system; std::string file; };
        const std::vector<Start> starts = {
            { 0, "Bill & Ted's Excellent Video Game Adventure (USA).nes" },
            { 1, "Awesome Possum... ...Kicks Dr. Machino's Butt (USA).gen" },
            { 2, "1942.zip" },
            { 0, "Contra (USA).nes" },
            { 0, "Little Lancelot (NES Rom Hack).dat" },
        };
        fs::path retroarch = fs::path(s->root) / "emulators" / "RetroArch";
        for (const Start& start : starts) {
            const System& system = systems()[start.system];
            std::string name = gameName(start.file);
            std::unique_ptr<CollectionInfo> collection(builder.buildCollection(system.name));
            const Item* item = nullptr;
            for (const Item* candidate : collection->items) {
                if (candidate->name == name) item = candidate;
            }
            check(item != nullptr, name + " is in " + system.name);
            if (!item) continue;

            // As Launcher::run() reads the item
            LaunchCommand::Game game;
            game.collection = item->collectionInfo->name;
            game.itemCollection = item->collectionInfo->name;
            game.itemLauncher = item->collectionInfo->launcher;
            game.name = item->name;
            game.file = item->file;
            game.filepath = item->filepath;
            LaunchCommand::Command command = LaunchCommand::build(config, game);
            checkEqual(command.failure, "", name + ": it can start");
            checkEqual(command.launcher, system.launcher, name + ": with its system's launcher");
            check(samePath(command.executable, retroarch / programName), name + ": RetroArch: " + command.executable);
            // RetroFE today: a launcher without currentDirectory runs in its program's folder
            check(samePath(command.currentDirectory, retroarch), name + ": in RetroArch's folder: " + command.currentDirectory);

            std::error_code ec;
            fs::remove(s->launches(), ec);
            checkEqual(startAndWait(command), "", name + ": the fake RetroArch ran and exited 0");
            std::vector<Launch> launches = readLaunches(s->launches());
            check(launches.size() == 1, name + ": one launch written down");
            if (launches.size() != 1) continue;
            const Launch& launch = launches.front();
            check(samePath(launch.folder, retroarch), name + ": started in RetroArch's folder: " + launch.folder);
            const std::vector<std::string>& args = launch.arguments;
            check(args.size() == 3, name + ": three arguments, got " + std::to_string(args.size()));
            if (args.size() != 3) continue;
            checkEqual(args[0], "-L", name + ": -L");
            check(fs::path(args[1]) == fs::path(s->core(system)), name + ": the core's full path as one argument: " + args[1]);
            fs::path expected = fs::path(s->root) / "collections" / system.name / "roms" / start.file;
            check(fs::path(args[2]) == expected, name + ": the game's full path as one argument: " + args[2]);
            // Unchanged: the very file Add games copied
            std::string staged = system.name + "/roms/" + (name == "Contra (USA)" ? "More games/" : "") + start.file;
            checkEqual(readFile(args[2]), staged, name + ": names the copied game");
        }

        // What the OS gets, from the same functions Launcher::execute() uses
        checkEqual(LaunchCommand::windowsCommandLine("C:\\RetroFE\\emulators\\RetroArch\\retroarch.exe", "-L \"a b.dll\" \"c & d.nes\""),
                   "\"C:\\RetroFE\\emulators\\RetroArch\\retroarch.exe\" -L \"a b.dll\" \"c & d.nes\"", "the Windows command line");
        const std::string cmdArgs = "/c \"a.bat\" \"b\"";
        check(Library::cmdArguments(cmdArgs) != cmdArgs, "cmd.exe would drop quotes from these");
        checkEqual(LaunchCommand::windowsCommandLine("cmd.exe", cmdArgs), "\"cmd.exe\" " + Library::cmdArguments(cmdArgs),
                   "cmd.exe's arguments made safe");
        checkEqual(LaunchCommand::windowsCommandLine("x.exe", ""), "\"x.exe\"", "no arguments: just the program");
        std::vector<std::string> argv = LaunchCommand::unixArgv("/emu/retroarch", "-L \"/emu/a b.so\" \"/roms/Bill & Ted's (USA).nes\"");
        check(argv == std::vector<std::string>({ "/emu/retroarch", "-L", "/emu/a b.so", "/roms/Bill & Ted's (USA).nes" }), "the argv elsewhere");
    });

    // Checklist step 6: the artwork copied by hand into collections/<System>/,
    // then a game's and a system's Media screens
    run("first setup i: artwork copied by hand shows on the Media screens", [&] {
        if (!s) return;
        for (const System& system : systems()) {
            for (const char* folder : { "medium_artwork", "system_artwork" }) {
                std::error_code ec;
                fs::copy(s->downloads + "/" + system.name + "/" + folder, s->root + "/collections/" + system.name + "/" + folder,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                check(!ec, system.name + ": " + folder + " copied: " + ec.message());
            }
        }
        s->rescan();

        const std::string nes = "Nintendo Entertainment System";
        const std::map<std::string, std::string> extensions = {
            { "artwork_front", ".jpg" }, { "logo", ".png" }, { "video", ".mp4" }, { "story", ".txt" }
        };
        for (const std::string& name : { std::string("Addams Family, The (USA)"), std::string("Bill & Ted's Excellent Video Game Adventure (USA)") }) {
            Forms::Params params = { { "collection", nes }, { "id", name } };
            s->open("game-media", params);
            std::map<std::string, std::string> details;
            for (const Forms::Row& row : s->model.rows("game:media", 100).items) {
                details[row.id] = row.detail;
            }
            for (const auto& [type, extension] : extensions) {
                checkEqual(details[type], "Its own file", name + ": " + type + " on the Media screen");
                s->open("game-media-slot", { { "collection", nes }, { "id", name }, { "type", type } });
                checkEqual(s->model.get("game:slot:status"), "Its own file", name + ": " + type);
                std::string file = s->model.get("game:slot:file");
                fs::path expected = fs::path(s->root) / "collections" / nes / "medium_artwork" / type / (name + extension);
                check(fs::path(file) == expected, name + ": " + type + " is " + file);
                checkEqual(readFile(file), nes + "/medium_artwork/" + type + "/" + name + extension, name + ": " + type + " is its own file");
            }
        }
        const std::string excitebike = "Excitebike (Japan, USA)";
        s->open("game-media", { { "collection", nes }, { "id", excitebike } });
        std::map<std::string, std::string> details;
        for (const Forms::Row& row : s->model.rows("game:media", 100).items) {
            details[row.id] = row.detail;
        }
        for (const auto& entry : extensions) {
            checkEqual(details[entry.first], "Missing", excitebike + ": " + entry.first);
        }

        for (const System& system : systems()) {
            s->open("system-media", { { "id", system.name } });
            bool logo = false;
            for (const Forms::Row& row : s->model.rows("system:media", 50).items) {
                if (row.id == "logo") {
                    logo = fs::path(row.image) == fs::path(s->root) / "collections" / system.name / "system_artwork" / "logo.png";
                }
            }
            check(logo, system.name + ": the system's Media shows its logo");
        }
    });

    s.reset();
});

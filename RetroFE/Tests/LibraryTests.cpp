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

// Tests for the Manage screens' data layer (Source/Library): file editing,
// scanning, launcher and media resolution, edits, metadata overrides.

#include "TestSupport.h"
#include "../Source/Collection/CollectionInfo.h"
#include "../Source/Collection/CollectionInfoBuilder.h"
#include "../Source/Collection/Item.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Database/DB.h"
#include "../Source/Database/MetadataDatabase.h"
#include "../Source/Library/Backups.h"
#include "../Source/Library/ConfFile.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Index.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Library/LineList.h"
#include "../Source/Library/Media.h"
#include "../Source/Library/MetadataOverrides.h"
#include "../Source/Library/Search.h"
#include "../Source/Manager/Setup.h"
#include "../Source/Utility/Utils.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace Library;

namespace
{
    std::string os()
    {
        return Utils::getOSType();
    }

    // A small install with every kind of collection:
    //   Main (menu.txt): Arcade, Consoles, Ghost (no folder)
    //   Consoles (menu/ folder): NES, SNES, Main (a loop back up)
    //   Arcade (system): roms/ a.zip b.zip c.ZIP d.7z, exclude.txt b, launcher mame,
    //                    launchers/a.conf -> fbneo
    //   NES (system, own launcher.conf beats "launcher = mame"; local launcher "special")
    //   SNES (system with launcher "fbneo, mame, lr-x": OneSauce's list, first wins)
    //   All (merged): Arcade.sub (empty = all), NES.sub (mario), playlist
    std::string makeInstall(const std::string& name)
    {
        std::string root = makeRoot(name);
        writeFile(root + "/settings.conf", "firstCollection = Main\r\nlayout = Neat\r\n");
        writeFile(root + "/launchers." + os() + "/mame.conf", "executable = mame.exe\narguments = %ITEM_NAME%\n");
        writeFile(root + "/launchers." + os() + "/fbneo.conf", "executable = fbneo.exe\n");

        writeFile(root + "/collections/Main/menu.txt", "Arcade\r\n# not this one\r\nConsoles\r\nGhost\r\n");
        writeFile(root + "/collections/Consoles/menu/NES.txt", "");
        writeFile(root + "/collections/Consoles/menu/SNES.txt", "");
        writeFile(root + "/collections/Consoles/menu/Main.txt", "");

        writeFile(root + "/collections/Arcade/settings.conf",
                  "# Arcade games\r\nlist.extensions = zip # zip only\r\nlauncher = mame\r\n");
        writeFile(root + "/collections/Arcade/info.conf", "title = Arcade Games\r\n");
        for (const char* rom : { "a.zip", "b.zip", "c.ZIP", "d.7z" }) {
            writeFile(root + "/collections/Arcade/roms/" + rom, "x");
        }
        writeFile(root + "/collections/Arcade/exclude.txt", "b\n");
        writeFile(root + "/collections/Arcade/launchers/a.conf", "fbneo\n");

        writeFile(root + "/collections/NES/settings.conf", "list.extensions = nes\nlauncher = mame\n");
        writeFile(root + "/collections/NES/launcher.conf", "executable = nestopia.exe\n");
        writeFile(root + "/collections/NES/launchers.local/special.conf", "executable = special.exe\n");
        // LF: a CRLF override only resolves on Windows (see the launcher resolution test)
        writeFile(root + "/collections/NES/launchers/mario.conf", "special\n");
        writeFile(root + "/collections/NES/roms/mario.nes", "x");
        writeFile(root + "/collections/NES/roms/zelda.nes", "x");

        writeFile(root + "/collections/SNES/settings.conf", "list.extensions = sfc\nlauncher = fbneo, mame, lr-x\n");
        writeFile(root + "/collections/SNES/roms/f-zero.sfc", "x");

        writeFile(root + "/collections/All/Arcade.sub", "");
        writeFile(root + "/collections/All/NES.sub", "mario\n");
        writeFile(root + "/collections/All/playlists/best.txt", "_Arcade:a\n");
        writeFile(root + "/collections/All/info.conf", "title = Everything\n");

        writeFile(root + "/layouts/Neat/layout.xml", "<?xml version=\"1.0\"?>\n<layout width=\"1920\" height=\"1080\" font=\"x\">\n</layout>\n");
        writeFile(root + "/layouts/Neat/splash.xml", "<layout width=\"1920\" height=\"1080\"/>");
        writeFile(root + "/layouts/Neat/collections/Arcade/layout/layout.xml", "<layout/>");
        writeFile(root + "/layouts/Neat/forms/settings.xml", "<form/>");
        return root;
    }

    std::shared_ptr<const Index> scan(const std::string& root)
    {
        ScanOptions options;
        options.root = root;
        return std::shared_ptr<const Index>(buildIndex(options).release());
    }

    std::vector<std::string> names(const Index& index, const std::vector<ItemRef>& items)
    {
        std::vector<std::string> result;
        for (const ItemRef& item : items) {
            result.push_back(index.collections[static_cast<size_t>(item.collection)].name + ":" + item.name);
        }
        return result;
    }
}

static void runLibraryTests();
static Test::Suite librarySuite("library", runLibraryTests);

static void runLibraryTests()
{
    std::string error;

    // --- File primitives ------------------------------------------------------------

    run("library: cmd.exe /c commands keep their quotes, and bare program names are found", [&] {
        checkEqual(cmdArguments("/c \"%RETROFE_PATH%/scripts/theme_switch.bat\" \"%ITEM_FILEPATH%\""),
                   "/c \"\"%RETROFE_PATH%/scripts/theme_switch.bat\" \"%ITEM_FILEPATH%\"\"",
                   "more than two quotes after /c: wrapped in one more pair");
        checkEqual(cmdArguments("/K \"a.bat\" \"b\"  "), "/K \"\"a.bat\" \"b\"\"", "/k too, any case, trailing spaces dropped");
        checkEqual(cmdArguments("/c \"\"a.bat\" \"b\"\""), "/c \"\"a.bat\" \"b\"\"", "already wrapped: unchanged");
        checkEqual(cmdArguments("/c \"a b.bat\""), "/c \"a b.bat\"", "one quoted path: unchanged");
        checkEqual(cmdArguments("/c dir"), "/c dir", "no quotes: unchanged");
        checkEqual(cmdArguments("/s /c \"a\" \"b\""), "/s /c \"a\" \"b\"", "/s first: left to the author");
        checkEqual(cmdArguments("\"a\" \"b\""), "\"a\" \"b\"", "not a /c command: unchanged");
        checkEqual(cmdArguments(""), "", "empty");

        checkEqual(findProgram(""), "", "no name");
        checkEqual(findProgram("folder/x.exe"), "", "a path isn't looked up");
        checkEqual(findProgram("no-such-program-qa-7f3e"), "", "an unknown name isn't found");
#ifdef WIN32
        std::string cmd = findProgram("cmd.exe");
        check(!cmd.empty() && iequals(fs::path(cmd).filename().string(), "cmd.exe"), "cmd.exe is found: " + cmd);
        check(iequals(findProgram("cmd"), cmd), ".exe is added to a name without an extension");
#else
        check(!findProgram("sh").empty(), "sh is found on PATH");
#endif
    });
    run("library: a launcher's program runs in its own folder; a system tool or one not found in RetroFE's", [&] {
        std::string root = makeRoot("lib-progfolder");
        checkEqual(programFolder("", root), root, "no program");
        checkEqual(programFolder("no-such-program-qa-7f3e.exe", root), root, "a name that isn't found: root");
        checkEqual(programFolder("/rfe/emulators/mame/mame", root), "/rfe/emulators/mame", "a path: its folder");
        checkEqual(programFolder("emulators/mame/mame.exe", root), "emulators/mame", "a relative path: its folder, taken from root");
        writeFile(root + "/qa-in-root.exe", "x");
        checkEqual(programFolder("qa-in-root.exe", root), root, "a bare name of a program in root: root");
#ifdef WIN32
        checkEqual(programFolder("E:\\rfe/emulators/mame.exe", root), "E:\\rfe/emulators",
                   "%RETROFE_PATH%/... on Windows: the last separator of either kind");
        checkEqual(programFolder("C:\\Emus\\mame.exe", root), "C:\\Emus", "backslashes");
        checkEqual(programFolder("cmd.exe", root), root, "cmd.exe (a Windows tool on the system path): root");
        checkEqual(programFolder("cmd", root), root, "cmd, .exe added: root");
        // An emulator on PATH: its own folder
        std::string emus = makeRoot("lib-progfolder-path");
        writeFile(emus + "/qa-fake-emulator-9d1.exe", "x");
        const char* old = std::getenv("PATH");
        std::string saved = old ? old : "";
        _putenv_s("PATH", (fs::path(emus).make_preferred().string() + ";" + saved).c_str());
        std::string folder = programFolder("qa-fake-emulator-9d1.exe", root);
        _putenv_s("PATH", saved.c_str());
        check(iequals(normalPath(folder), normalPath(emus)), "a program found on PATH: its folder (" + folder + ")");
#else
        checkEqual(programFolder("sh", root), root, "on PATH elsewhere (/bin/sh): root");
#endif
    });
    run("library: reads allow concurrent atomic replacement", [&] {
        std::string path = makeRoot("lib-read-replace") + "/playlist.txt";
        const std::string original(1024 * 1024, 'a');
        const std::string replacement(1024 * 1024, 'b');
        writeFile(path, original);
        std::atomic<bool> started{ false };
        std::atomic<bool> stop{ false };
        bool readsOk = true;
        size_t reads = 0;
        std::thread reader([&] {
            started = true;
            do {
                std::string content;
                readsOk &= Library::readFile(path, content) && (content == original || content == replacement);
                reads++;
            } while (!stop);
        });
        while (!started) {
            std::this_thread::yield();
        }
        bool writesOk = true;
        std::string writeError;
        for (int i = 0; i < 32; i++) {
            // On Windows an ifstream reader denies the delete access needed
            // by MoveFileEx, even when it permits other readers and writers.
            if (!writeFileAtomic(path, i % 2 == 0 ? replacement : original, writeError)) {
                writesOk = false;
                break;
            }
        }
        stop = true;
        reader.join();
        check(writesOk, "replaced while being read: " + writeError);
        check(reads > 0 && readsOk, "each read sees one complete version");
    });

    run("library: conf edits keep comments, spacing and CRLF", [&] {
        std::string root = makeRoot("lib-conf");
        std::string path = root + "/info.conf";
        writeFile(path, "# Info\r\ntitle\t=  Old   # shown on the wheel\r\nyear = 1980\r\nyear = 1981");
        ConfFile file(path);
        file.load();
        checkEqual(file.get("year", ""), "1981", "last definition wins");
        check(file.set("title", "New", error), "set title");
        check(file.set("genre", "Shooter", error), "add genre");
        check(file.save(error), "save: " + error);
        checkEqual(readFile(path), "# Info\r\ntitle\t=  New   # shown on the wheel\r\nyear = 1980\r\nyear = 1981\r\ngenre = Shooter\r\n",
                   "only the edited line and the new one differ");
        check(file.remove("year") && file.save(error), "remove every definition");
        checkEqual(readFile(path), "# Info\r\ntitle\t=  New   # shown on the wheel\r\ngenre = Shooter\r\n", "both years gone");
        check(!file.set("title", "a # b", error), "a value with # is refused");

        ConfFile first(path, ConfFile::Wins::First);
        writeFile(path, "executable = a\nexecutable = b\n");
        first.load();
        checkEqual(first.get("executable", ""), "a", "first-wins reader");
    });

    run("library: line lists keep everything else byte for byte", [&] {
        std::string root = makeRoot("lib-lines");
        std::string path = root + "/menu.txt";
        writeFile(path, "# top\r\nArcade\r\n\r\nConsoles # mine\r\nComputers");
        LineList list(path);
        list.load();
        check(list.entries() == std::vector<std::string>({ "Arcade", "Consoles ", "Computers" }), "entries are untrimmed, comments dropped");
        check(list.move(2, 0), "move");
        check(list.save(error), "save");
        checkEqual(readFile(path), "# top\r\nComputers\r\n\r\nArcade\r\nConsoles # mine", "entries swap slots; comments and endings stay");
        list.load();
        check(list.add("Handhelds", error, 1) && list.save(error), "insert");
        checkEqual(readFile(path), "# top\r\nComputers\r\n\r\nHandhelds\r\nArcade\r\nConsoles # mine", "inserted before the second entry");
        check(list.remove("arcade", LineList::Match::IgnoreCase) && list.add("Last", error) && list.save(error), "remove and append");
        checkEqual(readFile(path), "# top\r\nComputers\r\n\r\nHandhelds\r\nConsoles # mine\r\nLast\r\n",
                   "the old last line gets a line break before the new one");
        check(!list.add("bad # entry", error), "entries with # are refused");

        LineList::Qualified q = LineList::parseQualified("_MAME:pacman", "All");
        check(q.qualified && q.collection == "MAME" && q.name == "pacman", "qualified entry");
        checkEqual(LineList::qualify("MAME", "pacman", "All"), "_MAME:pacman", "qualify for another collection");
        checkEqual(LineList::qualify("MAME", "pacman", "MAME"), "pacman", "plain for its own");
    });

    run("library: originals are kept once and can be put back", [&] {
        std::string root = makeRoot("lib-backups");
        std::string path = root + "/collections/A/settings.conf";
        writeFile(path, "original\n");
        Backups backups(root);
        check(backups.beforeChange(path, error), "first change");
        writeFile(path, "first edit\n");
        check(backups.beforeChange(path, error), "second change");
        checkEqual(readFile(backups.backupPath(path)), "original\n", "the original, not the first edit");
        check(fs::path(backups.backupPath(path)).string().find("manage") != std::string::npos, "kept under manage/");
        check(backups.restore(path, error), "restore");
        checkEqual(readFile(path), "original\n", "restored");

        std::string created = root + "/collections/A/menu.txt";
        check(backups.beforeChange(created, error), "a new file");
        writeFile(created, "x\n");
        check(backups.restore(created, error) && !fs::exists(created), "a reset removes a file that didn't exist");
    });

    // --- Scanning -------------------------------------------------------------------

    run("library: collections, kinds and the menu tree", [&] {
        std::string root = makeInstall("lib-scan");
        auto index = scan(root);
        check(index != nullptr, "built");
        if (!index) {
            return;
        }
        checkEqual(index->topMenu, "Main", "top menu");
        auto kinds = [&](const std::string& name) {
            int c = index->findCollection(name);
            return c < 0 ? 0u : index->collections[static_cast<size_t>(c)].kinds;
        };
        check(kinds("Main") == KindMenu, "Main is a menu");
        check(kinds("Consoles") == KindMenu, "Consoles is a menu (menu folder)");
        check(kinds("Arcade") == KindSystem, "Arcade is a system");
        check(kinds("All") == KindMerged, "All is merged");
        check(index->findCollection("arcade") == index->findCollection("Arcade"), "names match ignoring case");

        const CollectionRecord& arcade = index->collections[static_cast<size_t>(index->findCollection("Arcade"))];
        checkEqual(arcade.title, "Arcade Games", "info.conf title");
        check(arcade.parents.size() == 1 && index->collections[static_cast<size_t>(arcade.parents[0])].name == "Main", "parent menu");

        std::vector<MenuRow> tree = index->menuTree();
        std::vector<std::string> rows;
        for (const MenuRow& row : tree) {
            rows.push_back(std::string(static_cast<size_t>(row.depth), '-') + row.entry + (row.cycle ? "*" : "")
                           + (row.collection < 0 ? "?" : ""));
        }
        std::vector<std::string> expected = { "Main", "-Arcade", "-Consoles", "--Main*", "--NES", "--SNES", "-Ghost?" };
        check(rows == expected, "tree in menu order, folder menus sorted, loop and missing entry marked");

        int layout = index->findLayout("Neat");
        check(layout >= 0 && index->layouts[static_cast<size_t>(layout)].active && index->layouts[static_cast<size_t>(layout)].width == 1920
              && index->layouts[static_cast<size_t>(layout)].height == 1080, "layout facts");
        check(layout >= 0 && index->layouts[static_cast<size_t>(layout)].collectionLayouts == std::vector<std::string>({ "Arcade" })
              && index->layouts[static_cast<size_t>(layout)].forms == std::vector<std::string>({ "settings" }), "collection layouts and forms");
    });

    run("library: games follow extensions, exclude.txt and .sub filters", [&] {
        std::string root = makeInstall("lib-games");
        writeFile(root + "/collections/NES/include.txt", "mario\nzelda\nmetroid\n");
        auto index = scan(root);
        if (!index) {
            check(false, "built");
            return;
        }
        int arcade = index->findCollection("Arcade");
        check(names(*index, index->itemsOf(arcade)) == std::vector<std::string>({ "Arcade:a" }),
              "b excluded, c.ZIP and d.7z not matched");
        const CollectionRecord& record = index->collections[static_cast<size_t>(arcade)];
        check(record.caseMismatchCount == 1 && record.caseMismatches[0] == "c.ZIP", "the case-only mismatch is flagged");

        int all = index->findCollection("All");
        // Folders list in directory order, which is only sorted on Windows
        std::vector<std::string> allGames = names(*index, index->itemsOf(all));
        std::sort(allGames.begin(), allGames.end());
        check(allGames == std::vector<std::string>({ "Arcade:a", "NES:mario", "NES:zelda" }),
              "empty .sub = every game; NES.sub's filter is joined with NES's include.txt (metroid has no file)");

        // includeMissingItems lists include.txt entries without files
        writeFile(root + "/collections/NES/settings.conf", "list.extensions = nes\nlist.includeMissingItems = true\n");
        index = scan(root);
        int nes = index->findCollection("NES");
        std::vector<std::string> nesGames = names(*index, index->itemsOf(nes));
        std::sort(nesGames.begin(), nesGames.end());
        check(nesGames == std::vector<std::string>({ "NES:mario", "NES:metroid", "NES:zelda" }), "missing items listed");
        int metroid = index->findGame("NES", "metroid");
        check(metroid >= 0 && index->games[static_cast<size_t>(metroid)].missing, "metroid is marked missing");
    });

    run("library: launcher resolution follows Launcher::run", [&] {
        std::string root = makeInstall("lib-launchers");
        auto index = scan(root);
        if (!index) {
            check(false, "built");
            return;
        }
        auto resolved = [&](const std::string& collection, const std::string& game) {
            LauncherResolution r = index->resolveLauncher(index->findCollection(collection), game);
            return r.launcher < 0 ? std::string("?") + r.name : index->launchers[static_cast<size_t>(r.launcher)].id;
        };
        checkEqual(resolved("Arcade", ""), "mame", "the collection's launcher");
        checkEqual(resolved("Arcade", "a"), "fbneo", "a game's override");
        checkEqual(resolved("NES", "zelda"), "NES", "the collection's launcher.conf beats launcher = mame");
        checkEqual(resolved("NES", "mario"), "NES.special", "an override naming a local launcher");
        checkEqual(resolved("SNES", ""), "fbneo", "a comma list uses its first name (fork compat)");
        LauncherResolution snes = index->resolveLauncher(index->findCollection("SNES"));
        check(snes.configured == "fbneo" && snes.alternatives == std::vector<std::string>({ "mame", "lr-x" }) && snes.problem.empty(),
              "the rest are alternatives");
        std::vector<LauncherUse> uses = index->usesOf(index->findLauncher("fbneo"));
        check(uses.size() == 2, "fbneo is SNES's default and game a's pick");

        // Override files with Windows line endings break on Linux (text-mode getline keeps '\r')
        writeFile(root + "/launchers.linux/fbneo.conf", "executable = fbneo\n");
        writeFile(root + "/launchers.windows/fbneo.conf", "executable = fbneo.exe\n");
        writeFile(root + "/collections/Arcade/launchers/a.conf", "fbneo\r\n");
        for (const char* platform : { "windows", "linux" }) {
            ScanOptions options;
            options.root = root;
            options.os = platform;
            auto scanned = std::shared_ptr<const Index>(buildIndex(options).release());
            LauncherResolution r = scanned->resolveLauncher(scanned->findCollection("Arcade"), "a");
            if (std::string(platform) == "windows") {
                check(r.launcher >= 0 && r.name == "fbneo", "CRLF override on Windows resolves");
            }
            else {
                check(r.launcher < 0 && r.name == "fbneo\r" && r.problem.find("carriage return") != std::string::npos,
                      "CRLF override on Linux keeps the '\\r' and says so");
            }
        }
    });

    run("library: RetroFE honours the OneSauce launcher list and include/exclude files", [&] {
        // CollectionInfoBuilder (RetroFE itself), against the same fixture as the library
        std::string root = makeInstall("lib-forkcompat");
        writeFile(root + "/collections/NES/settings1.conf",
                  "list.includeFile = content/NES/include.txt\nlist.excludeFile = " + root + "/content/NES/exclude.txt\n");
        writeFile(root + "/content/NES/include.txt", "mario\nzelda\n");
        writeFile(root + "/content/NES/exclude.txt", "zelda\n");
        writeFile(root + "/collections/NES/include.txt", "nothing\n");  // replaced by list.includeFile
        Configuration::absolutePath = root;
        Configuration config;
        for (const char* name : { "NES", "SNES" }) {
            std::string folder = root + "/collections/" + name;
            for (const char* file : { "/settings.conf", "/settings1.conf" }) {
                if (fs::exists(folder + file)) {
                    config.import(name, std::string("collections.") + name, folder + file, false);
                }
            }
        }
        DB db(root + "/meta.db");
        db.initialize();
        MetadataDatabase meta(db, config);
        CollectionInfoBuilder builder(config, meta);
        std::unique_ptr<CollectionInfo> nes(builder.buildCollection("NES"));
        std::vector<std::string> nesItems;
        for (const Item* item : nes->items) {
            nesItems.push_back(item->name);
        }
        check(nesItems == std::vector<std::string>({ "mario" }), "list.includeFile and list.excludeFile are read (relative and absolute)");
        std::unique_ptr<CollectionInfo> snes(builder.buildCollection("SNES"));
        checkEqual(snes->launcher, "fbneo", "launcher = fbneo, mame, lr-x launches with fbneo");

        // The library reads them the same way, %BASE_ITEM_PATH%/%ITEM_COLLECTION_NAME% included
        writeFile(root + "/settings1.conf", "baseItemPath = content\n");
        writeFile(root + "/collections/NES/settings1.conf",
                  "list.includeFile = %BASE_ITEM_PATH%/%ITEM_COLLECTION_NAME%/include.txt\n"
                  "list.excludeFile = %BASE_ITEM_PATH%/%ITEM_COLLECTION_NAME%/exclude.txt\n"
                  "list.path = collections/NES/roms\n");
        auto index = scan(root);
        int c = index->findCollection("NES");
        check(names(*index, index->itemsOf(c)) == std::vector<std::string>({ "NES:mario" }), "library: same games");
        check(fs::path(index->collections[static_cast<size_t>(c)].includeFile).parent_path().filename() == "NES"
              && fs::path(index->collections[static_cast<size_t>(c)].includeFile).parent_path().parent_path().filename() == "content",
              "include file expanded from %BASE_ITEM_PATH%/%ITEM_COLLECTION_NAME%");
    });

    run("library: media lookup and fallbacks", [&] {
        std::string root = makeInstall("lib-media");
        writeFile(root + "/collections/Arcade/medium_artwork/logo/a.png", "png");
        writeFile(root + "/collections/Arcade/medium_artwork/marquee/default.jpg", "jpg");
        writeFile(root + "/collections/Arcade/system_artwork/logo.png", "png");
        writeFile(root + "/collections/NES/settings1.conf", "media.logo = " + root + "/nes-logos\n");
        writeFile(root + "/nes-logos/mario.gif", "gif");
        writeFile(root + "/collections/SNES/settings1.conf", "media.basePath = " + root + "/content/SNES\n");
        writeFile(root + "/content/SNES/medium_artwork/logo/f-zero.png", "png");
        writeFile(root + "/collections/Arcade/medium_artwork/story/a.txt", "story");
        auto index = scan(root);
        if (!index) {
            check(false, "built");
            return;
        }
        int a = index->findGame("Arcade", "a");
        MediaSlot logo = gameMedia(*index, a, "logo", MediaKind::Image);
        check(logo.own && fs::path(logo.file).filename() == "a.png", "own logo");

        MediaSlot merged = gameMedia(*index, a, "logo", MediaKind::Image, index->findCollection("All"));
        check(!merged.own && !merged.file.empty() && merged.fallback == "game's collection",
              "on the merged page the slot is All's folder; Arcade's file is a fallback");
        check(fs::path(merged.directory).parent_path().parent_path().filename() == "All", "slot folder is All's");

        MediaSlot marquee = gameMedia(*index, a, "marquee", MediaKind::Image);
        check(!marquee.own && fs::path(marquee.file).filename() == "default.jpg", "default art is a fallback");

        MediaSlot nes = gameMedia(*index, index->findGame("NES", "mario"), "logo", MediaKind::Image);
        check(nes.own && fs::path(nes.file).filename() == "mario.gif", "media.<type> override");

        MediaSlot snes = gameMedia(*index, index->findGame("SNES", "f-zero"), "logo", MediaKind::Image);
        check(snes.own && fs::path(snes.file).filename() == "f-zero.png", "media.basePath (fork compat)");

        MediaSlot story = gameMedia(*index, a, "story", MediaKind::Text);
        check(story.own && fs::path(story.file).filename() == "a.txt", "story text");

        MediaSlot system = collectionMedia(*index, index->findCollection("Arcade"), "logo", MediaKind::Image);
        check(system.own && fs::path(system.file).filename() == "logo.png", "system art");

        MediaSlot entry = menuEntryMedia(*index, index->findCollection("Main"), "Arcade", "logo", MediaKind::Image);
        check(!entry.own && fs::path(entry.file).filename() == "logo.png" && entry.fallback == "child's collection art",
              "a menu entry falls back to the child's system art");
    });

    // --- Edits ----------------------------------------------------------------------

    run("library: collection settings go where RetroFE reads them", [&] {
        std::string root = makeInstall("lib-settings");
        writeFile(root + "/collections/All/info.conf", "title = Everything\r\nlauncher = mame\r\n");
        auto index = scan(root);
        Editor editor(root);
        EditResult r = editor.setCollectionSetting(*index, "arcade", "list.extensions", "zip,7z");
        check(r.ok, "set extensions: " + r.message);
        checkEqual(readFile(root + "/collections/Arcade/settings.conf"),
                   "# Arcade games\r\nlist.extensions = zip,7z # zip only\r\nlauncher = mame\r\n", "edited in place");
        check(fs::exists(root + "/manage/backups/collections/Arcade/settings.conf"), "original kept");
        r = editor.setCollectionSetting(*index, "Arcade", "genre", "Shooter");
        check(r.ok && readFile(root + "/collections/Arcade/info.conf") == "title = Arcade Games\r\ngenre = Shooter\r\n", "info keys go to info.conf");
        r = editor.setCollectionSetting(*index, "All", "launcher", "fbneo");
        check(r.ok && readFile(root + "/collections/All/info.conf").find("launcher = fbneo") != std::string::npos,
              "a key set in info.conf (which wins) is edited there");
        check(!editor.setCollectionSetting(*index, "Arcade", "list.path", "C:/x").ok, "paths can't be edited");
        check(!editor.setCollectionSetting(*index, "Arcade", "launcher", "nope").ok, "unknown launchers are refused");
        check(!editor.setCollectionSetting(*index, "Arcade", "list.extensions", ".zip").ok, "dotted extensions are refused");
        check(editor.removeCollectionSetting(*index, "Arcade", "genre").ok
              && readFile(root + "/collections/Arcade/info.conf") == "title = Arcade Games\r\n", "removed");
    });

    run("library: menu edits", [&] {
        std::string root = makeInstall("lib-menus");
        writeFile(root + "/collections/Main/menu_supported.txt", "Arcade\nConsoles\n");
        writeFile(root + "/collections/Extra/settings.conf", "list.extensions = zip\n");
        auto index = scan(root);
        Editor editor(root);
        check(editor.addMenuChild(*index, "Main", "extra", 1).ok, "add");
        checkEqual(readFile(root + "/collections/Main/menu.txt"), "Arcade\r\n# not this one\r\nExtra\r\nConsoles\r\nGhost\r\n",
                   "inserted with the folder's spelling, before the second entry");
        checkEqual(readFile(root + "/collections/Main/menu_supported.txt"), "Arcade\nExtra\nConsoles\n", "menu_supported.txt kept in step");
        index = scan(root);
        check(editor.moveMenuChild(*index, "Main", "Ghost", 0).ok, "move");
        checkEqual(readFile(root + "/collections/Main/menu.txt"), "Ghost\r\n# not this one\r\nArcade\r\nExtra\r\nConsoles\r\n", "moved to the top");
        check(editor.removeMenuChild(*index, "Main", "Ghost").ok, "remove");
        checkEqual(readFile(root + "/collections/Main/menu.txt"), "# not this one\r\nArcade\r\nExtra\r\nConsoles\r\n", "removed");

        index = scan(root);
        check(editor.addMenuChild(*index, "Consoles", "Arcade").ok && fs::exists(root + "/collections/Consoles/menu/Arcade.txt")
              && !fs::exists(root + "/collections/Consoles/menu.txt"), "a folder menu gets a menu/<child>.txt, not a menu.txt");
        check(!editor.moveMenuChild(*index, "Consoles", "NES", 0).ok, "a folder menu can't be reordered");
        index = scan(root);
        check(!editor.addMenuChild(*index, "Arcade", "Main").ok, "a loop is refused");
        check(!editor.addMenuChild(*index, "Main", "Main").ok, "a menu can't list itself");

        // Setup's helpers now use firstCollection
        writeFile(root + "/settings.conf", "firstCollection = Top\r\n");
        Configuration::absolutePath = root;
        check(Setup::addToMainMenu("Arcade", error), "Setup adds to the top menu: " + error);
        checkEqual(readFile(root + "/collections/Top/menu.txt"), std::string("Arcade") + defaultNewline(), "Top/menu.txt");
        check(fs::exists(root + "/collections/Top/settings.conf"), "a new top menu gets a settings.conf");
        check(Setup::addToMainMenu("Top", error) && readFile(root + "/collections/Top/menu.txt").find("Top") == std::string::npos,
              "the top menu isn't added to itself");
    });

    run("library: game launcher, playlist and .sub edits", [&] {
        std::string root = makeInstall("lib-lists");
        auto index = scan(root);
        Editor editor(root);
        check(editor.setGameLauncher(*index, "Arcade", "a", "mame").ok && !fs::exists(root + "/collections/Arcade/launchers/a.conf"),
              "choosing the collection's launcher removes the override");
        check(fs::exists(root + "/manage/backups/collections/Arcade/launchers/a.conf"), "the old override is kept");
        index = scan(root);
        check(editor.setGameLauncher(*index, "NES", "zelda", "special").ok
              && readFile(root + "/collections/NES/launchers/zelda.conf") == "special\n",
              "an override file with the launcher's name, ending in \\n on every platform");
        check(!editor.setGameLauncher(*index, "NES", "zelda", "nothing").ok, "unknown launcher refused");
        writeFile(root + "/collections/NES/launchers/mario.conf", "special\r\n# keep me\r\n");
        index = scan(root);
        check(editor.setGameLauncher(*index, "NES", "mario", "fbneo").ok
              && readFile(root + "/collections/NES/launchers/mario.conf") == "fbneo\n# keep me\r\n",
              "only the first line changes (and loses its \\r, which Linux would read as part of the name)");

        check(editor.addToPlaylist(*index, "All", "best", "NES", "mario").ok
              && readFile(root + "/collections/All/playlists/best.txt") == "_Arcade:a\n_NES:mario\n", "qualified playlist entry");
        check(!editor.addToPlaylist(*index, "All", "best", "NES", "zelda").ok, "a game the page doesn't show is refused");
        check(!editor.addToPlaylist(*index, "All", "lastplayed", "NES", "mario").ok, "lastplayed is RetroFE's");
        index = scan(root);
        check(editor.removeFromPlaylist(*index, "All", "best", "Arcade", "a").ok
              && readFile(root + "/collections/All/playlists/best.txt") == "_NES:mario\n", "removed from the playlist");

        check(editor.addToSub(*index, "All", "Arcade", "a").ok && readFile(root + "/collections/All/Arcade.sub").empty(),
              "an empty .sub already shows everything");
        check(editor.addToSub(*index, "All", "NES", "zelda").ok
              && readFile(root + "/collections/All/NES.sub") == "mario\nzelda\n", "added to the .sub");
        index = scan(root);
        check(!editor.removeFromSub(*index, "All", "Arcade", "a").ok, "can't remove one game from an empty .sub");
        check(editor.removeFromSub(*index, "All", "NES", "zelda").ok, "removed");
        index = scan(root);
        EditResult last = editor.removeFromSub(*index, "All", "NES", "mario");
        check(last.ok && !fs::exists(root + "/collections/All/NES.sub"), "removing the last entry removes the .sub");
    });

    run("library: media replace and clear", [&] {
        std::string root = makeInstall("lib-media-edit");
        writeFile(root + "/collections/Arcade/medium_artwork/logo/a.jpg", "old");
        writeFile(root + "/collections/Arcade/medium_artwork/marquee/default.png", "default");
        writeFile(root + "/incoming/new logo.PNG", "new");
        writeFile(root + "/incoming/clip.mp4", "clip");
        auto index = scan(root);
        Editor editor(root);
        int a = index->findGame("Arcade", "a");
        MediaSlot logo = gameMedia(*index, a, "logo", MediaKind::Image);
        EditResult r = editor.replaceMedia(logo, root + "/incoming/new logo.PNG");
        check(r.ok, "replace: " + r.message);
        check(readFile(root + "/collections/Arcade/medium_artwork/logo/a.png") == "new"
              && !fs::exists(root + "/collections/Arcade/medium_artwork/logo/a.jpg"), "a.png in, a.jpg out");
        check(readFile(root + "/manage/backups/collections/Arcade/medium_artwork/logo/a.jpg") == "old", "the old file is kept");
        logo = gameMedia(*index, a, "logo", MediaKind::Image);
        check(logo.own && fs::path(logo.file).filename() == "a.png", "the slot sees the new file");
        check(!editor.replaceMedia(logo, root + "/incoming/clip.mp4").ok, "a video can't go in an image slot");

        MediaSlot marquee = gameMedia(*index, a, "marquee", MediaKind::Image);
        check(!editor.clearMedia(marquee).ok, "clearing a fallback is refused");
        check(fs::exists(root + "/collections/Arcade/medium_artwork/marquee/default.png"), "the fallback is untouched");
        check(editor.clearMedia(logo).ok && !fs::exists(root + "/collections/Arcade/medium_artwork/logo/a.png"), "own file cleared");
        check(editor.restoreOriginal(root + "/collections/Arcade/medium_artwork/logo/a.jpg").ok
              && readFile(root + "/collections/Arcade/medium_artwork/logo/a.jpg") == "old", "the original comes back");
    });

    run("library: a media folder stands for a missing file (fork compat)", [&] {
        std::string root = makeInstall("lib-media-folders");
        const std::string art = root + "/collections/Arcade/system_artwork";
        for (const char* name : { "ARCADES 10.mp4", "ARCADES 2.mp4", "ARCADES 1.mp4", "notes.txt" }) {
            writeFile(art + "/video/" + name, "v");
        }
        writeFile(art + "/logo.png", "png");
        writeFile(art + "/logo/other.png", "png");          // beside a file: the file wins
        writeFile(art + "/story/a.txt", "text");            // text slots don't use folders
        fs::create_directories(art + "/fanart");            // an empty folder
        writeFile(root + "/collections/Arcade/medium_artwork/video/a/clip 1.mp4", "v");
        writeFile(root + "/collections/Arcade/medium_artwork/marquee/default/m1.png", "png");
        writeFile(root + "/collections/Arcade/medium_artwork/marquee/default/m2.png", "png");
        writeFile(root + "/collections/Arcade/roms/screenshot/shot.png", "png");   // the game's ROM folder: not art
        auto index = scan(root);
        int arcade = index->findCollection("Arcade");
        int a = index->findGame("Arcade", "a");

        MediaSlot video = collectionMedia(*index, arcade, "video", MediaKind::Video);
        check(video.own && video.fromFolder() && video.shownFolderFiles == 3, "a folder of three videos is its own video");
        check(fs::path(video.file).filename() == "ARCADES 1.mp4", "the first, in natural order, stands for them: " + video.file);
        check(video.folderFiles.size() == 3 && fs::path(video.folderFiles[2]).filename() == "ARCADES 10.mp4", "its files, listed");
        checkEqual(mediaShownText(video), std::string("a folder of 3 files (one shown at random)"), "shown as");
        check(mediaFolderNote(video).find("at random") != std::string::npos, "the note says one is picked at random");
        check(!video.fileBesideFolder(), "no file beside it");

        MediaSlot logo = collectionMedia(*index, arcade, "logo", MediaKind::Image);
        check(logo.own && !logo.fromFolder() && fs::path(logo.file).filename() == "logo.png", "a file beside the folder wins");
        check(logo.fileBesideFolder() && logo.folderFiles.size() == 1, "and the folder is known");
        check(mediaFolderNote(logo).find("The file wins") != std::string::npos, "the note says the file wins: " + mediaFolderNote(logo));

        MediaSlot story = collectionMedia(*index, arcade, "story", MediaKind::Text);
        check(story.file.empty() && story.folder.empty(), "a text slot never reads a folder");

        MediaSlot fanart = collectionMedia(*index, arcade, "fanart", MediaKind::Image);
        check(fanart.file.empty() && !fanart.own && !fanart.folder.empty() && fanart.folderFiles.empty(), "an empty folder shows nothing");
        check(mediaFolderNote(fanart).find("holds no") != std::string::npos, "and says so");

        MediaSlot clip = gameMedia(*index, a, "video", MediaKind::Video);
        check(clip.own && clip.fromFolder() && fs::path(clip.file).filename() == "clip 1.mp4", "a game's own folder");
        MediaSlot marquee = gameMedia(*index, a, "marquee", MediaKind::Image);
        check(!marquee.own && marquee.fromFolder() && marquee.shownFolderFiles == 2 && marquee.fallback == "game's collection (default)",
              "a default/ folder is a fallback: " + marquee.fallback);
        check(marquee.folder.empty(), "and not the slot's own folder");
        MediaSlot shot = gameMedia(*index, a, "screenshot", MediaKind::Image);
        check(shot.file.empty(), "a folder in the game's ROM folder isn't art");

        // A menu entry's art falls back to the child's folder too
        MediaSlot entry = menuEntryMedia(*index, index->findCollection("Main"), "Arcade", "video", MediaKind::Video);
        check(!entry.own && entry.fromFolder() && entry.fallback == "child's collection art", "a menu entry: the child's folder");

        // Replace and Clear never remove a folder
        Editor editor(root);
        check(!editor.clearMedia(video).ok, "clearing a folder slot as one file is refused");
        check(fs::exists(art + "/video/ARCADES 1.mp4"), "the folder is untouched");
        writeFile(root + "/incoming/new.mp4", "new");
        EditResult r = editor.replaceMedia(video, root + "/incoming/new.mp4");
        check(r.ok && readFile(art + "/video.mp4") == "new" && r.message.find("wins over the folder") != std::string::npos,
              "replacing puts a file beside the folder, which wins: " + r.message);
        check(mediaFolderFiles(art, "video", MediaKind::Video).size() == 3 && fs::exists(art + "/video/notes.txt"), "the folder stays");
        video = collectionMedia(*index, arcade, "video", MediaKind::Video);
        check(video.fileBesideFolder(), "now both are there");
        r = editor.clearMedia(video);
        check(r.ok && !fs::exists(art + "/video.mp4") && r.message.find("3 files show now") != std::string::npos,
              "clearing the file shows the folder again: " + r.message);
        check(mediaFolderFiles(art, "video", MediaKind::Video).size() == 3, "the folder is untouched");
    });

    run("library: media folder edits keep every file", [&] {
        std::string root = makeInstall("lib-media-folder-edits");
        const std::string art = root + "/collections/Arcade/system_artwork";
        for (const char* name : { "one.mp4", "two.mp4", "three.mp4" }) {
            writeFile(art + "/video/" + name, name);
        }
        writeFile(art + "/video/README.txt", "Put videos here.");
        writeFile(root + "/incoming/four.mp4", "four");
        writeFile(root + "/incoming/pic.png", "png");
        auto index = scan(root);
        int arcade = index->findCollection("Arcade");
        Editor editor(root);
        auto slot = [&] { return collectionMedia(*index, arcade, "video", MediaKind::Video); };

        EditResult r = editor.addMediaFolderFile(slot(), root + "/incoming/four.mp4");
        check(r.ok && readFile(art + "/video/four.mp4") == "four" && r.message.find("4 files") != std::string::npos, "added: " + r.message);
        check(!editor.addMediaFolderFile(slot(), root + "/incoming/four.mp4").ok, "a name that's taken is refused");
        check(!editor.addMediaFolderFile(slot(), root + "/incoming/pic.png").ok, "a picture can't go in a video folder");
        check(!editor.addMediaFolderFile(collectionMedia(*index, arcade, "logo", MediaKind::Image), root + "/incoming/pic.png").ok,
              "a slot without a folder can't get one this way");

        r = editor.removeMediaFolderFile(slot(), "two.mp4");
        check(r.ok && !fs::exists(art + "/video/two.mp4") && r.message.find("3 files left") != std::string::npos, "removed: " + r.message);
        check(readFile(root + "/manage/backups/collections/Arcade/system_artwork/video/two.mp4") == "two.mp4", "and kept in the backups");
        check(!editor.removeMediaFolderFile(slot(), "two.mp4").ok, "a file that isn't there");
        check(!editor.removeMediaFolderFile(slot(), "README.txt").ok, "only files of the slot's kind");
        check(!editor.removeMediaFolderFile(slot(), "../video.mp4").ok, "only files in the folder");

        std::vector<std::string> originals = editor.mediaFolderOriginals(slot());
        std::sort(originals.begin(), originals.end());
        check(originals.size() == 2 && fs::path(originals[0]).filename() == "four.mp4" && fs::path(originals[1]).filename() == "two.mp4",
              "what Put back would restore: the added and the removed file (" + std::to_string(originals.size()) + ")");

        // The folder becomes one file
        r = editor.mediaFolderToFile(slot(), "three.mp4");
        check(r.ok && readFile(art + "/video.mp4") == "three.mp4", "three.mp4 is now video.mp4: " + r.message);
        check(mediaFolderFiles(art, "video", MediaKind::Video).empty() && fs::exists(art + "/video/README.txt"),
              "the folder's videos went, its other files stay");
        check(readFile(root + "/manage/backups/collections/Arcade/system_artwork/video/one.mp4") == "one.mp4"
              && readFile(root + "/manage/backups/collections/Arcade/system_artwork/video/three.mp4") == "three.mp4",
              "every video is kept");
        MediaSlot single = slot();
        check(single.own && !single.fromFolder() && single.folderFiles.empty(), "the slot is one file now");

        // Put back: the folder's files and video.mp4 (which didn't exist)
        std::vector<std::string> restore = editor.mediaFolderOriginals(single);
        restore.push_back(art + "/video.mp4");
        for (const std::string& file : restore) {
            check(editor.restoreOriginal(file).ok, "restored " + file);
        }
        check(!fs::exists(art + "/video.mp4"), "the single file is gone again");
        std::vector<std::string> names;
        for (const std::string& file : mediaFolderFiles(art, "video", MediaKind::Video)) {
            names.push_back(fs::path(file).filename().string());
        }
        check(names == std::vector<std::string>({ "one.mp4", "three.mp4", "two.mp4" }), "the folder is as it was");
        check(editor.mediaFolderOriginals(slot()).empty(), "nothing left to put back");

        // Clearing: every video to the backups, and the emptied folder goes
        fs::remove(art + "/video/README.txt");
        r = editor.clearMediaFolder(slot());
        check(r.ok && !fs::exists(art + "/video") && r.message.find("3 files") != std::string::npos, "cleared: " + r.message);
        check(slot().file.empty() && slot().folder.empty(), "nothing shows");
        check(!editor.clearMediaFolder(slot()).ok, "no folder to clear");
    });

    run("library: a media folder made one file where a file already was, and put back", [&] {
        std::string root = makeInstall("lib-media-folder-beside");
        const std::string art = root + "/collections/Arcade/system_artwork";
        writeFile(art + "/video.mp4", "old single");
        writeFile(art + "/video/one.mp4", "one");
        writeFile(art + "/video/two.mp4", "two");
        writeFile(art + "/logo.png", "png");
        fs::create_directories(art + "/logo");   // an empty folder beside a file
        auto index = scan(root);
        int arcade = index->findCollection("Arcade");
        Editor editor(root);
        auto slot = [&] { return collectionMedia(*index, arcade, "video", MediaKind::Video); };
        check(slot().fileBesideFolder(), "the file wins over the folder");
        MediaSlot logo = collectionMedia(*index, arcade, "logo", MediaKind::Image);
        check(!logo.fileBesideFolder() && !logo.folder.empty(), "an empty folder isn't a folder the file wins over");

        EditResult r = editor.mediaFolderToFile(slot(), "two.mp4");
        check(r.ok && readFile(art + "/video.mp4") == "two" && !fs::exists(art + "/video"), "two.mp4 is the file now: " + r.message);
        const std::string kept = root + "/manage/backups/collections/Arcade/system_artwork";
        check(readFile(kept + "/video.mp4") == "old single" && readFile(kept + "/video/one.mp4") == "one",
              "the old single file and the folder's files are kept");

        // Put back: the single file as it was, and the folder
        std::vector<std::string> restore = editor.mediaFolderOriginals(slot());
        check(restore.size() == 2, "the folder's two files to put back: " + std::to_string(restore.size()));
        restore.push_back(art + "/video.mp4");
        for (const std::string& file : restore) {
            check(editor.restoreOriginal(file).ok, "restored " + file);
        }
        check(readFile(art + "/video.mp4") == "old single" && readFile(art + "/video/one.mp4") == "one"
              && readFile(art + "/video/two.mp4") == "two", "both as they were");
        check(editor.mediaFolderOriginals(slot()).empty(), "nothing left to put back");
        // A later change to a put-back file shows again (decided by size and time, then content)
        writeFile(art + "/video/one.mp4", "one, edited by hand");
        check(editor.mediaFolderOriginals(slot()).size() == 1, "an edited file differs from its original");
        writeFile(art + "/video/one.mp4", "one");
        check(editor.mediaFolderOriginals(slot()).empty(), "and the same bytes again don't");
    });

    run("library: media folder files named like numbered copies, and odd cases", [&] {
        std::string root = makeInstall("lib-media-folder-names");
        const std::string folder = root + "/collections/Arcade/system_artwork/video";
        writeFile(folder + "/Racing.mp4", "racing");
        writeFile(folder + "/Racing (2).mp4", "racing two");
        writeFile(folder + "/Other.mp4", "other");
        writeFile(root + "/incoming/clip.MP4", "clip");
        writeFile(root + "/incoming2/CLIP.mp4", "CLIP");   // apart: on Windows the two names are one file
        auto index = scan(root);
        int arcade = index->findCollection("Arcade");
        Editor editor(root);
        auto slot = [&] { return collectionMedia(*index, arcade, "video", MediaKind::Video); };

        // Both taken out: "Racing (2).mp4" is a real file, not a kept copy
        check(editor.removeMediaFolderFile(slot(), "Racing.mp4").ok && editor.removeMediaFolderFile(slot(), "Racing (2).mp4").ok, "taken out");
        std::vector<std::string> originals = editor.mediaFolderOriginals(slot());
        check(originals.size() == 2, "both are put back: " + std::to_string(originals.size()));
        for (const std::string& file : originals) {
            editor.restoreOriginal(file);
        }
        check(readFile(folder + "/Racing.mp4") == "racing" && readFile(folder + "/Racing (2).mp4") == "racing two", "both back");

        // A real kept copy: Other.mp4 taken out, a different Other.mp4 copied in by hand and taken out too
        check(editor.removeMediaFolderFile(slot(), "Other.mp4").ok, "first take-out");
        writeFile(folder + "/Other.mp4", "other, newer");
        check(editor.removeMediaFolderFile(slot(), "Other.mp4").ok, "second take-out");
        const std::string kept = root + "/manage/backups/collections/Arcade/system_artwork/video";
        check(readFile(kept + "/Other (2).mp4") == "other, newer", "the newer one is a numbered copy");
        originals = editor.mediaFolderOriginals(slot());
        check(originals.size() == 1 && fs::path(originals[0]).filename() == "Other.mp4", "the copy isn't offered as an original");

        // Extensions in any case; names that differ only in case
        EditResult r = editor.addMediaFolderFile(slot(), root + "/incoming/clip.MP4");
        check(r.ok && readFile(folder + "/clip.MP4") == "clip", "an upper-case extension is taken, the name kept: " + r.message);
        const MediaSlot withClip = slot();
        check(std::any_of(withClip.folderFiles.begin(), withClip.folderFiles.end(),
                          [](const std::string& f) { return fs::path(f).filename() == "clip.MP4"; }), "and listed");
        // It depends on the drive, not the OS: Windows and most macOS volumes ignore case
        std::error_code caseEc;
        const bool ignoresCase = fs::exists(folder + "/CLIP.mp4", caseEc);
        r = editor.addMediaFolderFile(slot(), root + "/incoming2/CLIP.mp4");
        if (ignoresCase) {
            check(!r.ok && readFile(folder + "/clip.MP4") == "clip", "a drive that ignores case: the same file, refused");
        } else {
            check(r.ok && readFile(folder + "/CLIP.mp4") == "CLIP" && readFile(folder + "/clip.MP4") == "clip",
                  "a case-sensitive drive: another file, as RetroFE lists it: " + r.message);
        }
    });

    run("library: a media folder that can't be emptied leaves the new file winning", [&] {
        std::string root = makeInstall("lib-media-folder-fail");
        const std::string art = root + "/collections/Arcade/system_artwork";
        writeFile(art + "/video/one.mp4", "one");
        writeFile(art + "/video/two.mp4", "two");
        // The backups can't take the folder's files: a file where their folder would go
        writeFile(root + "/manage/backups/collections/Arcade/system_artwork/video", "in the way");
        auto index = scan(root);
        Editor editor(root);
        MediaSlot video = collectionMedia(*index, index->findCollection("Arcade"), "video", MediaKind::Video);
        EditResult r = editor.mediaFolderToFile(video, "one.mp4");
        check(!r.ok && r.message.find("couldn't be emptied") != std::string::npos, "says it failed part way: " + r.message);
        check(!r.files.empty() && readFile(art + "/video.mp4") == "one", "the file is in place, and listed as changed");
        check(fs::exists(art + "/video/one.mp4") && fs::exists(art + "/video/two.mp4"), "the folder keeps its files");
    });

    run("library: hidden games are read with their metadata, apart from the games", [&] {
        // Arcade's exclude.txt names b
        std::string root = makeInstall("lib-hidden");
        writeFile(root + "/meta/hyperlist/Arcade.xml",
                  "<?xml version=\"1.0\"?>\n<menu>\n<game name=\"b\"><description>Bravo</description><year>1982</year></game>\n</menu>\n");
        Configuration::absolutePath = root;
        Configuration config;
        {
            DB db(root + "/meta.db");
            check(db.initialize(), "open meta.db");
            MetadataDatabase meta(db, config);
            check(meta.initialize(), "import");
        }
        auto index = scan(root);
        check(index->findGame("Arcade", "b") == -1, "not one of the games");
        std::vector<std::string> all = names(*index, index->itemsOf(index->findCollection("All")));
        check(std::find(all.begin(), all.end(), "Arcade:b") == all.end() && !all.empty(), "nor on a merged page");
        int h = index->findHiddenGame("Arcade", "b");
        check(h >= 0 && index->hiddenGames.size() == 1, "one hidden game");
        if (h >= 0) {
            const GameRecord& b = index->hiddenGames[static_cast<size_t>(h)];
            checkEqual(b.title, "Bravo", "its title from meta.db");
            checkEqual(b.meta.year, "1982", "and the rest of its metadata");
            check(b.hasMetadata && fs::path(b.file).filename() == "b.zip", "with its file");
            check(b.searchText.find("bravo") != std::string::npos, "searchable by title");
        }
        check(index->findHiddenGame("Arcade", "a") == -1, "a listed game isn't hidden");
    });

    run("library: RetroFE expects no settings of menus and merged collections", [&] {
        // Main.cpp logs a missing settings.conf as an error only for other folders
        std::string root = makeInstall("lib-nosettings");
        writeFile(root + "/collections/Legacy/menu.xml", "<menu/>");
        writeFile(root + "/collections/Empty/readme.txt", "x");
        for (const char* name : { "Main", "Consoles", "All", "Legacy" }) {
            check(CollectionInfoBuilder::isMenuOrMergedFolder(root + "/collections/" + name), std::string(name) + " needs none");
        }
        for (const char* name : { "Arcade", "Empty", "Nope" }) {
            check(!CollectionInfoBuilder::isMenuOrMergedFolder(root + "/collections/" + name), std::string(name) + " should have some");
        }

        // A menu that has games of its own too still needs its settings
        writeFile(root + "/collections/MenuGames/menu.txt", "Arcade\n");
        writeFile(root + "/collections/MenuGames/roms/x.zip", "x");
        writeFile(root + "/collections/MenuKeys/menu.txt", "Arcade\n");
        writeFile(root + "/collections/MenuKeys/info.conf", "list.extensions = zip\n");
        // A system whose list.path is wrong: no games, but still a system
        writeFile(root + "/collections/Broken/settings.conf", "list.path = nowhere\n");
        Configuration::absolutePath = root;
        Configuration config;
        for (const char* name : { "Arcade", "MenuKeys", "Broken" }) {
            for (const char* file : { "/settings.conf", "/info.conf" }) {
                std::string path = root + "/collections/" + name + file;
                if (fs::exists(path)) {
                    config.import(name, std::string("collections.") + name, path, false);
                }
            }
        }
        for (const char* name : { "Main", "Consoles", "All", "Legacy" }) {
            check(CollectionInfoBuilder::needsNoSettings(config, name), std::string(name) + ": a missing settings.conf is expected");
        }
        for (const char* name : { "MenuGames", "MenuKeys", "Arcade", "Empty" }) {
            check(!CollectionInfoBuilder::needsNoSettings(config, name), std::string(name) + ": a missing settings.conf is a problem");
        }
        // Who gets the "launcher isn't set" notice: systems, found games or not
        for (const char* name : { "Arcade", "Broken", "MenuKeys" }) {
            check(CollectionInfoBuilder::isSystem(config, name), std::string(name) + " is a system");
        }
        for (const char* name : { "Main", "Consoles", "All", "Home", "Empty" }) {
            check(!CollectionInfoBuilder::isSystem(config, name), std::string(name) + " is no system");
        }
    });

    // --- Metadata overrides ---------------------------------------------------------

    run("library: metadata changes survive a re-import", [&] {
        std::string root = makeInstall("lib-metadata");
        writeFile(root + "/meta/hyperlist/Arcade.xml",
                  "<?xml version=\"1.0\"?>\n<menu>\n<game name=\"a\"><description>Alpha</description><year>1980</year></game>\n</menu>\n");
        Configuration::absolutePath = root;
        Configuration config;
        {
            DB db(root + "/meta.db");
            check(db.initialize(), "open meta.db");
            MetadataDatabase meta(db, config);
            check(meta.initialize(), "import");
        }
        auto index = scan(root);
        int a = index->findGame("Arcade", "a");
        checkEqual(index->games[static_cast<size_t>(a)].title, "Alpha", "shipped title read from meta.db");

        Editor editor(root);
        EditResult r = editor.setMetadata(*index, "Arcade", "a", "description", "Alpha & Omega");
        check(r.ok, "set: " + r.message);
        check(readFile(MetadataOverrides::fileFor(root, "Arcade")).find("<description>Alpha &amp; Omega</description>") != std::string::npos,
              "override file in hyperlist format");
        check(editor.setMetadata(*index, "Arcade", "a", "genre", "Maze").ok, "set genre");
        check(!editor.setMetadata(*index, "Arcade", "a", "description", "").ok, "a blank title is refused");
        index = scan(root);
        a = index->findGame("Arcade", "a");
        checkEqual(index->games[static_cast<size_t>(a)].title, "Alpha & Omega", "meta.db updated straight away");

        // A full re-import (as after a content update or -rebuilddb) keeps the change
        auto injected = [&](std::string& title, std::string& year, std::string& genre) {
            DB db(root + "/meta.db");
            db.initialize();
            MetadataDatabase meta(db, config);
            meta.resetDatabase();
            CollectionInfo info(config, "Arcade", "", "zip", "Arcade", "");
            Item* item = new Item();
            item->name = "a";
            item->collectionInfo = &info;
            info.items.push_back(item);
            meta.injectMetadata(&info);
            title = item->title;
            year = item->year;
            genre = item->genre;
        };
        std::string title, year, genre;
        injected(title, year, genre);
        checkEqual(title, "Alpha & Omega", "title after re-import");
        checkEqual(year, "1980", "shipped year still applies");
        checkEqual(genre, "Maze", "genre after re-import");

        index = scan(root);
        check(editor.clearMetadata(*index, "Arcade", "a", "description").ok, "clear");
        injected(title, year, genre);
        checkEqual(title, "Alpha", "back to the shipped title");
        checkEqual(genre, "Maze", "the other change stays");
    });

    // --- Indexer and search -----------------------------------------------------------

    run("library: background index and search", [&] {
        std::string root = makeInstall("lib-indexer");
        ScanOptions options;
        options.root = root;
        Indexer indexer(options);
        indexer.rebuild();
        check(indexer.wait(10000), "built in time");
        auto index = indexer.index();
        check(index && indexer.status().state == Indexer::State::Ready && indexer.status().generation == 1, "ready");
        if (!index) {
            return;
        }
        Search search;
        GameQuery all;
        check(search.games(index, all).total == 4, "a, mario, zelda, f-zero");
        GameQuery query;
        query.text = "MAR";
        const Results<int>& found = search.games(index, query);
        check(found.total == 1 && index->games[static_cast<size_t>(found.rows[0])].name == "mario", "case-insensitive search");
        GameQuery merged;
        merged.collection = "All";
        check(search.games(index, merged).total == 2, "a merged collection's filter covers its sources' games");
        GameQuery top;
        top.collection = "Main";
        check(search.games(index, top).total == 4, "a menu filter covers everything below it");
        GameQuery limited;
        limited.limit = 1;
        const Results<int>& page = search.games(index, limited);
        check(page.rows.size() == 1 && page.total == 4, "limit with total");

        CollectionQuery menus;
        menus.kinds = KindMenu;
        check(search.collections(index, menus).total == 2, "Main and Consoles");
        CollectionQuery under;
        under.under = "Consoles";
        check(search.collections(index, under).total == 4, "NES, SNES, and Main and Arcade through the loop back up");

        indexer.changed("test");
        check(indexer.changesPending() && indexer.wait(10000) && indexer.status().generation == 2, "a change rebuilds and is pending");
    });

    run("library: a file that changed since its original was kept is never deleted", [&] {
        std::string root = makeRoot("lib-backups-changed");
        std::string path = root + "/collections/A/medium_artwork/logo/a.png";
        writeFile(path, "original");
        Backups backups(root);
        check(backups.removeKeepingOriginal(path, error) && !fs::exists(path), "first removal moves the original aside");
        writeFile(path, "copied in later");  // a content update or a hand copy
        check(backups.removeKeepingOriginal(path, error) && !fs::exists(path), "removed again");
        std::string kept = (fs::path(backups.backupPath(path)).parent_path() / "a (2).png").string();
        checkEqual(readFile(kept), "copied in later", "the changed file is kept as a numbered copy");
        checkEqual(readFile(backups.backupPath(path)), "original", "the original is untouched");
        writeFile(path, "original");
        check(backups.removeKeepingOriginal(path, error) && !fs::exists((fs::path(kept).parent_path() / "a (3).png")),
              "a file identical to the original is simply removed");
        writeFile(path, "edited");
        check(backups.restore(path, error) && readFile(path) == "original"
              && readFile((fs::path(kept).parent_path() / "a (3).png").string()) == "edited",
              "a reset keeps what it replaces");
    });

    run("library: clearing metadata restores mamelist values, or drops user-only rows", [&] {
        std::string root = makeInstall("lib-metadata-mame");
        writeFile(root + "/meta/mamelist/Arcade.xml",
                  "<mame><machine name=\"a\" cloneof=\"z\"><description>Alpha</description><year>1981</year>"
                  "<manufacturer>Namco</manufacturer><input players=\"2\" buttons=\"1\"/></machine></mame>");
        Configuration::absolutePath = root;
        Configuration config;
        {
            DB db(root + "/meta.db");
            db.initialize();
            MetadataDatabase meta(db, config);
            meta.initialize();
        }
        std::map<std::string, std::string> shippedRow;
        check(MetadataOverrides::shipped(root, "Arcade", "a", shippedRow) == MetadataOverrides::Shipped::Found
              && shippedRow["description"] == "Alpha" && shippedRow["manufacturer"] == "Namco" && shippedRow["players"] == "2"
              && shippedRow["cloneof"] == "z", "shipped() reads the mamelist row");

        Editor editor(root);
        auto index = scan(root);
        check(editor.setMetadata(*index, "Arcade", "a", "manufacturer", "Midway").ok, "set");
        check(editor.clearMetadata(*index, "Arcade", "a", "manufacturer").ok, "clear");
        index = scan(root);
        const GameRecord& a = index->games[static_cast<size_t>(index->findGame("Arcade", "a"))];
        check(a.meta.manufacturer == "Namco" && a.title == "Alpha", "back to the mamelist value, not blank");

        // A game no shipped file lists: its row was the user's alone
        check(editor.setMetadata(*index, "NES", "zelda", "genre", "Adventure").ok, "metadata for an unlisted game");
        index = scan(root);
        check(index->games[static_cast<size_t>(index->findGame("NES", "zelda"))].hasMetadata, "it has a row now");
        check(editor.clearMetadata(*index, "NES", "zelda", "genre").ok, "clear it");
        index = scan(root);
        check(!index->games[static_cast<size_t>(index->findGame("NES", "zelda"))].hasMetadata, "the row is gone again");

        // A shipped file that can't be read: meta.db is left alone and a re-import is asked for
        check(editor.setMetadata(*index, "Arcade", "a", "year", "1999").ok, "set year");
        writeFile(root + "/meta/hyperlist/Arcade.xml", "<menu><game name=\"a\"><year>1980</year>");  // broken
        check(editor.clearMetadata(*index, "Arcade", "a", "year").ok, "clear with an unreadable shipped file");
        index = scan(root);
        checkEqual(index->games[static_cast<size_t>(index->findGame("Arcade", "a"))].meta.year, "1999", "meta.db not guessed at");
        check(MetadataOverrides::newestChange(root) > fs::last_write_time(root + "/meta.db"), "RetroFE will re-import");
    });

    run("library: a burst of changes still publishes snapshots", [&] {
        std::string root = makeInstall("lib-burst");
        ScanOptions options;
        options.root = root;
        Indexer indexer(options);
        indexer.rebuild();
        // Changes every millisecond for half a second: snapshots must still come out
        auto start = std::chrono::steady_clock::now();
        int changes = 0;
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
            indexer.changed("edit " + std::to_string(changes++));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(indexer.status().generation >= 2, "snapshots were published during the burst ("
              + std::to_string(indexer.status().generation) + ")");
        writeFile(root + "/collections/NES/roms/metroid.nes", "x");
        indexer.changed("edit " + std::to_string(changes++));
        check(indexer.wait(20000), "settles");
        auto index = indexer.index();
        check(index && indexer.status().state == Indexer::State::Ready && indexer.status().generation >= 1,
              "builds finished and published instead of being cancelled");
        check(index && index->findGame("NES", "metroid") >= 0, "the last snapshot has every change");
        check(indexer.pendingChanges().size() == static_cast<size_t>(changes), "all changes recorded as pending");
    });

    run("library: new systems go to a menu, never into a games collection", [&] {
        std::string root = makeInstall("lib-top-menu");
        Configuration::absolutePath = root;
        writeFile(root + "/settings.conf", "firstCollection = Arcade\r\n");
        checkEqual(Setup::firstCollection(), "Arcade", "Browse opens Arcade");
        checkEqual(Setup::topMenu(), "Main", "but Arcade has games, so new systems go to Main");
        check(Setup::addToMainMenu("Extra", error) && readFile(root + "/collections/Main/menu.txt").find("Extra") != std::string::npos
              && !fs::exists(root + "/collections/Arcade/menu.txt"), "added to Main, Arcade untouched");
        writeFile(root + "/settings.conf", "firstCollection = Consoles\r\n");
        checkEqual(Setup::topMenu(), "Consoles", "a menu is used as it is");
        Configuration live;
        live.setProperty("firstCollection", std::string("All"));  // a literal would pick the bool overload
        Setup::setConfiguration(&live);
        checkEqual(Setup::firstCollection(), "All", "the live value wins (command-line overrides)");
        checkEqual(Setup::topMenu(), "Main", "All shows .sub games, so it isn't used as the menu");
        Setup::setConfiguration(nullptr);
        check(!Setup::addToMainMenu("../evil", error) && !error.empty(), "names that aren't folder names are refused");
        check(!Setup::validate(Setup::NewCollection{ "Consoles", root, "zip", "", "" }).empty(), "the top menu's name is taken");
    });

    run("library: emuarc filters on file names; precomputed membership", [&] {
        std::string root = makeInstall("lib-emuarc");
        writeFile(root + "/collections/Discs/settings.conf", "list.extensions = chd\nlist.emuarc = true\n");
        writeFile(root + "/collections/Discs/roms/Game One/disc1.chd", "x");
        writeFile(root + "/collections/Discs/roms/Game One/disc2.chd", "x");
        writeFile(root + "/collections/Discs/roms/Game Two/disc1.chd", "x");
        writeFile(root + "/collections/Discs/exclude.txt", "disc1\n");
        auto index = scan(root);
        int discs = index->findCollection("Discs");
        check(names(*index, index->itemsOf(discs)) == std::vector<std::string>({ "Discs:Game One" }),
              "exclude.txt matches file names: Game One still has disc2, Game Two has nothing left");

        int a = index->findGame("Arcade", "a");
        const auto& merged = index->mergedCollectionsShowing(a);
        check(merged.size() == 1 && index->collections[static_cast<size_t>(merged[0])].name == "All", "a is shown by All");
        const auto& playlists = index->playlistsListing(a);
        check(playlists.size() == 1 && playlists[0].playlist == "best", "and listed in All's best playlist");

        Editor editor(root);
        check(!editor.removeMenuChild(*index, "Main", "Nowhere").ok, "removing a child that isn't listed fails");
        EditResult added = editor.addMenuChild(*index, "Consoles", "Discs", 0);
        check(added.ok && added.message.find("name order") != std::string::npos, "a folder menu says the position doesn't apply");
    });

    // --- Optional read-only smoke run over a real install -------------------------------
    // Set RETROFE_LIBRARY_SMOKE=<root> (e.g. K:\retrofe-data\appdata\retrofe) to time a scan.
    if (const char* smoke = std::getenv("RETROFE_LIBRARY_SMOKE")) {
        run("library: smoke scan (read only)", [&] {
            ScanOptions options;
            options.root = smoke;
            if (const char* smokeOs = std::getenv("RETROFE_LIBRARY_SMOKE_OS")) {
                options.os = smokeOs;
            }
            auto started = std::chrono::steady_clock::now();
            auto index = std::shared_ptr<const Index>(buildIndex(options).release());
            double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            check(index != nullptr, "built");
            if (!index) {
                return;
            }
            size_t menus = 0, systems = 0, merged = 0, withMeta = 0, overrides = 0;
            for (const CollectionRecord& record : index->collections) {
                menus += record.is(KindMenu);
                systems += record.is(KindSystem);
                merged += record.is(KindMerged);
                overrides += record.launcherOverrides.size();
            }
            for (const GameRecord& game : index->games) {
                withMeta += game.hasMetadata;
            }
            std::cout << "  smoke: " << index->collections.size() << " collections (" << menus << " menus, " << systems
                      << " systems, " << merged << " merged), " << index->games.size() << " games (" << withMeta
                      << " with metadata), " << index->launchers.size() << " launchers, " << overrides << " game overrides, "
                      << index->unresolvedLaunchers.size() << " unresolved launcher names, " << index->layouts.size()
                      << " layouts, menu tree " << index->menuTree().size() << " rows; built in " << seconds << " s" << std::endl;

            Search search;
            GameQuery query;
            query.text = "pac";
            auto t0 = std::chrono::steady_clock::now();
            const Results<int>& first = search.games(index, query);
            double searchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            t0 = std::chrono::steady_clock::now();
            search.games(index, query);
            double cachedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            GameQuery under;
            under.collection = index->topMenu;
            t0 = std::chrono::steady_clock::now();
            size_t underTop = searchGames(*index, under).total;
            double maskMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::cout << "  smoke: search \"pac\" " << first.total << " hits in " << searchMs << " ms (cached " << cachedMs
                      << " ms); everything under " << index->topMenu << ": " << underTop << " games in " << maskMs << " ms" << std::endl;

            std::vector<int> samples;
            if (!first.rows.empty()) {
                samples.push_back(first.rows[0]);
            }
            // A game of the biggest system, preferring one with a launcher override
            const CollectionRecord* biggest = nullptr;
            for (const CollectionRecord& record : index->collections) {
                if (!biggest || record.gameCount > biggest->gameCount) {
                    biggest = &record;
                }
            }
            if (biggest && biggest->gameCount) {
                int pick = static_cast<int>(biggest->gameBegin);
                for (const auto& [game, line] : biggest->launcherOverrides) {
                    int found = index->findGame(biggest->name, game);
                    if (found >= 0) {
                        pick = found;
                        break;
                    }
                }
                samples.push_back(pick);
                MediaSlot logo = gameMedia(*index, pick, "logo", MediaKind::Image);
                std::cout << "  smoke: " << biggest->name << " logo folder " << logo.directory << " -> "
                          << (logo.file.empty() ? "(none)" : logo.file) << std::endl;
            }
            for (int sample : samples) {
                t0 = std::chrono::steady_clock::now();
                size_t found = 0;
                for (const std::string& type : mediaTypes(*index, index->games[static_cast<size_t>(sample)].collection)) {
                    found += !gameMedia(*index, sample, type, defaultKind(type)).file.empty();
                }
                double mediaMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                const GameRecord& game = index->games[static_cast<size_t>(sample)];
                LauncherResolution launcher = index->resolveLauncher(game.collection, game.name);
                std::cout << "  smoke: " << game.name << " (" << game.title << ") in "
                          << index->collections[static_cast<size_t>(game.collection)].name << ": media in " << found
                          << " slots, looked up in " << mediaMs << " ms; launcher \"" << launcher.name << "\" "
                          << (launcher.launcher >= 0 ? "resolved" : "unresolved: " + launcher.problem) << std::endl;
            }
            for (const std::string& problem : index->problems) {
                std::cout << "  smoke problem: " << problem << std::endl;
            }
        });
    }
}

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
#include "../Source/Collection/Item.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Database/DB.h"
#include "../Source/Database/MetadataDatabase.h"
#include "../Source/Library/Backups.h"
#include "../Source/Library/ConfFile.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Index.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Library/LineList.h"
#include "../Source/Library/Media.h"
#include "../Source/Library/MetadataOverrides.h"
#include "../Source/Library/Search.h"
#include "../Source/Manager/Setup.h"
#include "../Source/Utility/Utils.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
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
    //   SNES (system with launcher "lr-a, lr-b")
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
        writeFile(root + "/collections/NES/launchers/mario.conf", "special\r\n");
        writeFile(root + "/collections/NES/roms/mario.nes", "x");
        writeFile(root + "/collections/NES/roms/zelda.nes", "x");

        writeFile(root + "/collections/SNES/settings.conf", "list.extensions = sfc\nlauncher = lr-a, lr-b\n");
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

void runLibraryTests()
{
    std::string error;

    // --- File primitives ------------------------------------------------------------

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
        check(names(*index, index->itemsOf(all)) == std::vector<std::string>({ "Arcade:a", "NES:mario", "NES:zelda" }),
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
        checkEqual(resolved("SNES", ""), "?lr-a, lr-b", "a comma list doesn't resolve");
        LauncherResolution snes = index->resolveLauncher(index->findCollection("SNES"));
        check(snes.problem.find("list of launchers") != std::string::npos, "and says why");
        check(std::find(index->unresolvedLaunchers.begin(), index->unresolvedLaunchers.end(), "lr-a, lr-b")
              != index->unresolvedLaunchers.end(), "listed as unresolved");
        std::vector<LauncherUse> uses = index->usesOf(index->findLauncher("fbneo"));
        check(uses.size() == 1 && uses[0].game == "a", "fbneo is used by one game");
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
              && readFile(root + "/collections/NES/launchers/zelda.conf") == std::string("special") + (Utils::getOSType() == "windows" ? "\r\n" : "\n"),
              "an override file with the launcher's name");
        check(!editor.setGameLauncher(*index, "NES", "zelda", "nothing").ok, "unknown launcher refused");
        writeFile(root + "/collections/NES/launchers/mario.conf", "special\r\n# keep me\r\n");
        index = scan(root);
        check(editor.setGameLauncher(*index, "NES", "mario", "fbneo").ok
              && readFile(root + "/collections/NES/launchers/mario.conf") == "fbneo\r\n# keep me\r\n", "only the first line changes");

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

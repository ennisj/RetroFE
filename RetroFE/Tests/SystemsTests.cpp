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
#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Forms/Form.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Systems/SystemForms.h"
#include "../Source/Manager/Systems/SystemProvider.h"
#include "../Source/Utility/Utils.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    std::string os()
    {
        return Utils::getOSType();
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // Main (menu): Arcade, Consoles, Clips;  Consoles (menu): NES, Empty
    //   Arcade: roms a.zip b.zip C.ZIP, "launcher = mame, fbneo", list.menuSort = true
    //   NES: mario.nes, no launcher, system_artwork/logo.png
    //   Lonely: in no menu, a folder that isn't there, launcher "ghost"
    //   Empty: made by Setup, no games, launcher mame
    //   Clips: a video collection (info.conf type = videos)
    std::string makeInstall(const std::string& name)
    {
        std::string root = makeRoot(name);
        auto roms = [&](const std::string& system) { return Utils::combinePath(root, "roms", system); };
        writeFile(root + "/settings.conf", "firstCollection = Main\n");
        writeFile(root + "/mame.exe", "x");
        writeFile(root + "/launchers." + os() + "/mame.conf", "executable = " + Utils::combinePath(root, "mame.exe") + "\n");
        writeFile(root + "/launchers." + os() + "/fbneo.conf", "executable = fbneo.exe\n");

        writeFile(root + "/collections/Main/menu.txt", "Arcade\r\nConsoles\r\nClips\r\n");
        writeFile(root + "/collections/Consoles/menu.txt", "NES\r\nEmpty\r\n");

        writeFile(root + "/collections/Arcade/settings.conf",
                  "list.path = " + roms("Arcade") + "\nlist.extensions = zip\nlauncher = mame, fbneo\nlist.menuSort = true\n");
        writeFile(root + "/collections/Arcade/info.conf", "title = Arcade Games\n");
        for (const char* rom : { "a.zip", "b.zip", "C.ZIP" }) {
            writeFile(roms("Arcade") + "/" + rom, "x");
        }

        writeFile(root + "/collections/NES/settings.conf", "list.path = " + roms("NES") + "\nlist.extensions = nes\n");
        writeFile(roms("NES") + "/mario.nes", "x");
        writeFile(root + "/collections/NES/system_artwork/logo.png", "old logo");
        writeFile(root + "/collections/NES/launchers.local/special.conf", "executable = special.exe\n");

        writeFile(root + "/collections/Lonely/settings.conf",
                  "list.path = " + roms("Nowhere") + "\nlist.extensions = bin\nlauncher = ghost\n");

        writeFile(root + "/collections/Empty/settings.conf",
                  "# Created by RetroFE setup\nlist.path = " + roms("Empty") + "\nlist.extensions = zip\nlauncher = mame\n");
        fs::create_directories(roms("Empty"));

        writeFile(root + "/collections/Clips/settings.conf",
                  "list.path = " + roms("Clips") + "\nlist.extensions = mp4\nlauncher = mame\n");
        writeFile(root + "/collections/Clips/info.conf", "type = videos\n");
        writeFile(roms("Clips") + "/intro.mp4", "x");
        return root;
    }

    // A model with both providers on a fresh index of root
    struct Fixture
    {
        std::string root;
        Configuration config;
        Library::Indexer indexer;
        Library::Editor editor;
        ManagerModel model;

        explicit Fixture(const std::string& name)
            : root(useRoot(makeInstall(name)))
            , indexer(options(root))
            , editor(root)
            , model(config)
        {
            Manager::Services services{ config };
            services.library = &indexer;
            services.editor = &editor;
            model.addProvider(std::make_unique<Manager::SystemProvider>(services, false));
            model.addProvider(std::make_unique<Manager::SystemProvider>(services, true));
            indexer.rebuild();
            settle();
        }

        // RetroFE's root, set before the model reads its settings
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

        void settle()
        {
            indexer.wait(20000);
        }

        // After a test changed the files by hand
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
        }

        std::vector<std::string> ids(const std::string& key)
        {
            std::vector<std::string> result;
            for (const Forms::Row& row : model.rows(key, 200).items) {
                result.push_back(row.id);
            }
            return result;
        }

        bool anyFinding(const std::string& prefix, const std::string& part)
        {
            for (int i = 0; i < 12; i++) {
                if (contains(model.get(prefix + ":finding" + std::to_string(i)), part)) {
                    return true;
                }
            }
            return false;
        }
    };

    bool has(const std::vector<std::string>& values, const std::string& value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    // Where a deleted collection's folder was kept: manage/backups/deleted/<name> <time>
    std::string keptFolder(const std::string& root, const std::string& name)
    {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root + "/manage/backups/deleted", ec)) {
            if (entry.path().filename().string().compare(0, name.size() + 1, name + " ") == 0) {
                return entry.path().string();
            }
        }
        return "";
    }
}

static void runSystemsTests();
static Test::Suite systemsSuite("systems", runSystemsTests);

static void runSystemsTests()
{
    run("systems: extensions are normalised, keeping their case", [] {
        checkEqual(Manager::normaliseExtensions(" .zip, ZIP;zip  7z,,"), "zip,ZIP,7z", "split, dots dropped, duplicates dropped");
        checkEqual(Manager::normaliseExtensions(" , ."), "", "nothing left");
    });

    run("systems: both sets of forms parse and bind their own prefix", [] {
        for (bool videos : { false, true }) {
            for (const Forms::BuiltinXml& entry : Manager::systemForms(videos)) {
                std::string error;
                std::unique_ptr<Forms::Form> form = Forms::parse(entry.xml, error);
                check(form && form->id == entry.id, std::string(entry.id) + " parses: " + error);
                check(!contains(entry.xml, "{"), std::string(entry.id) + " has no placeholders left");
                check(contains(entry.xml, videos ? "video:" : "system:"), std::string(entry.id) + " binds its prefix");
            }
        }
        check(contains(Forms::builtinForm("video-add"), "mp4,mkv,avi"), "the video preset");
    });

    run("library: playlists are created, renamed and deleted, never the reserved ones", [] {
        std::string root = makeInstall("sys-lib-playlists");
        Library::ScanOptions scan;
        scan.root = root;
        auto index = std::shared_ptr<const Library::Index>(Library::buildIndex(scan).release());
        Library::Editor editor(root);
        std::string folder = root + "/collections/Arcade/playlists/";
        check(editor.createPlaylist(*index, "Arcade", "Best").ok && fs::exists(folder + "Best.txt"), "created");
        check(!editor.createPlaylist(*index, "Arcade", "best").ok, "the same name in another case is refused");
        check(!editor.createPlaylist(*index, "Arcade", "Favorites").ok, "a reserved name is refused");
        check(!editor.createPlaylist(*index, "Arcade", "a/b").ok, "a path is refused");
        writeFile(folder + "Best.txt", "a\n# mine\n");
        Library::EditResult renamed = editor.renamePlaylist(*index, "Arcade", "Best", "Top");
        check(renamed.ok && fs::exists(folder + "Top.txt") && !fs::exists(folder + "Best.txt"), "renamed: " + renamed.message);
        checkEqual(readFile(folder + "Top.txt"), "a\n# mine\n", "content kept byte for byte");
        check(editor.renamePlaylist(*index, "Arcade", "Top", "TOP").ok, "a change of case alone: " + renamed.message);
        check(!editor.renamePlaylist(*index, "Arcade", "TOP", "lastplayed").ok, "can't take a reserved name");
        writeFile(folder + "lastplayed.txt", "a\n");
        check(!editor.deletePlaylist(*index, "Arcade", "lastplayed").ok && fs::exists(folder + "lastplayed.txt"), "reserved ones stay");
        check(editor.deletePlaylist(*index, "Arcade", "TOP").ok, "deleted");
        bool gone = true;
        for (const auto& entry : fs::directory_iterator(folder)) {
            gone &= Utils::toLower(entry.path().filename().string()) != "top.txt";
        }
        check(gone, "the file is gone");
        // Best.txt was made here (an "absent" original), then changed by hand: that content is kept
        checkEqual(readFile(root + "/manage/backups/collections/Arcade/playlists/Best (2).txt"), "a\n# mine\n",
                   "the renamed file's content is kept");
    });

    run("systems: the lists split systems from video collections, filtered by menu", [] {
        Fixture f("sys-list");
        f.open("systems", {});
        std::vector<std::string> systems = f.ids("system:search");
        check(has(systems, "Arcade") && has(systems, "NES") && has(systems, "Lonely") && has(systems, "Empty"), "every system");
        check(!has(systems, "Clips") && !has(systems, "Main"), "no video collections or plain menus");
        f.model.set("system:menu", "Consoles");
        systems = f.ids("system:search");
        check(systems.size() == 2 && has(systems, "NES") && has(systems, "Empty"), "under Consoles only");
        f.model.set("system:query", "ne");
        checkEqual(f.ids("system:search").size() == 1 ? f.ids("system:search")[0] : "", "NES", "searching too");
        Forms::Rows rows = f.model.rows("system:search", 200);
        check(!rows.items.empty() && contains(rows.items[0].detail, "1 game"), "rows show the game count");

        f.open("videos", {});
        std::vector<std::string> videos = f.ids("video:search");
        check(videos.size() == 1 && videos[0] == "Clips", "Videos lists the marked collection");
        check(contains(f.model.rows("video:search", 200).items[0].detail, "1 video"), "worded for videos");
    });

    run("systems: details, the Games link and the check", [] {
        Fixture f("sys-details");
        f.open("system", { { "id", "Arcade" } });
        checkEqual(f.model.get("system:title"), "Arcade Games", "title from info.conf");
        checkEqual(f.model.get("system:name"), "Arcade", "folder shown when the title differs");
        checkEqual(f.model.get("system:menus"), "Main", "menus it's in");
        checkEqual(f.model.get("system:gameCount"), "2 games", "C.ZIP isn't listed");
        check(contains(f.model.get("system:launcherInEffect"), "mame (set in settings.conf)"), "launcher and its source: "
              + f.model.get("system:launcherInEffect"));
        Forms::Rows games = f.model.rows("system:games", 10);
        check(games.items.size() == 1 && games.items[0].id == "Arcade", "one row for the Games screen");
        Forms::Field field;
        field.type = Forms::FieldType::Results;
        field.param = "collection";
        checkEqual(Forms::rowParams(field, games.items[0])["collection"], "Arcade", "it opens games with collection=Arcade");
        checkEqual(f.model.get("system:canDelete"), "no", "a system with games that wasn't made here stays");
        check(contains(f.model.actionProblem("system.delete"), "2 games"), "and says why");

        f.open("system-check", { { "id", "Arcade" } });
        check(f.anyFinding("system", "C.ZIP"), "a wrong-case extension");
        check(contains(f.model.get("system:checkSummary"), "Arcade: "), "a summary");

        f.open("system-check", { { "id", "Lonely" } });
        check(f.anyFinding("system", "No menu lists Lonely"), "not in a menu");
        check(f.anyFinding("system", "doesn't exist"), "missing folder");
        check(f.anyFinding("system", "ghost"), "launcher not installed");
        check(contains(f.model.get("system:checkSummary"), "in no menu"), "summary says where it is");

        f.open("system-check", { { "id", "NES" } });
        check(f.anyFinding("system", "No launcher is set"), "no launcher");
        check(f.anyFinding("system", "meta/hyperlist/NES.xml"), "no metadata file");
    });

    run("systems: settings save allow-listed keys in the file's own style", [] {
        Fixture f("sys-settings");
        f.open("system-settings", { { "id", "Arcade" } });
        checkEqual(f.model.get("system:extensions"), "zip", "extensions");
        checkEqual(f.model.get("system:menuSort"), "yes", "menu sort");
        f.model.set("system:extensions", " .zip, ZIP");
        f.model.set("system:menuSort", "no");
        check(!f.model.leaving(f.model.screen()).empty(), "unsaved changes warn");
        Forms::Model::Result saved = f.model.runAction("system.saveSettings");
        check(saved.ok, "saved: " + saved.message);
        std::string settings = readFile(f.root + "/collections/Arcade/settings.conf");
        check(contains(settings, "list.extensions = zip,ZIP"), "normalised extensions written");
        check(contains(settings, "list.menuSort = false"), "true/false style kept");
        check(contains(settings, "list.path = "), "the path is untouched");
        check(f.model.leaving(f.model.screen()).empty(), "nothing left unsaved");
        f.model.set("system:extensions", ",");
        check(!f.model.actionProblem("system.saveSettings").empty(), "no extensions is refused");
        f.model.runAction("system.resetSettings");
        f.open("system", { { "id", "Arcade" } });
        checkEqual(f.model.get("system:gameCount"), "3 games", "C.ZIP is listed now");
    });

    run("systems: the launcher keeps a comma list's alternatives", [] {
        Fixture f("sys-launcher");
        f.open("system-launcher", { { "id", "Arcade" } });
        checkEqual(f.model.get("system:launcher"), "mame", "the first is the default");
        checkEqual(f.model.get("system:alternatives"), "fbneo", "the rest are alternatives");
        auto options = f.model.options("system:launchers");
        check(std::any_of(options.begin(), options.end(), [](const auto& o) { return o.first == "fbneo"; }), "installed launchers offered");
        f.model.set("system:launcher", "fbneo");
        Forms::Model::Result saved = f.model.runAction("system.saveLauncher");
        check(saved.ok, "saved: " + saved.message);
        check(contains(readFile(f.root + "/collections/Arcade/settings.conf"), "launcher = fbneo, mame"), "pick moved to the front");
        f.open("system-launcher", { { "id", "Lonely" } });
        f.model.set("system:launcher", "ghost2");
        check(!f.model.actionProblem("system.saveLauncher").empty(), "an unknown launcher is refused");
    });

    run("systems: info is written to info.conf; an empty field is taken out", [] {
        Fixture f("sys-info");
        f.open("system-info", { { "id", "NES" } });
        f.model.set("system:infoTitle", "Nintendo Entertainment System");
        f.model.set("system:manufacturer", "Nintendo");
        check(f.model.runAction("system.saveInfo").ok, "saved");
        std::string info = readFile(f.root + "/collections/NES/info.conf");
        check(contains(info, "title = Nintendo Entertainment System") && contains(info, "manufacturer = Nintendo"), "written: " + info);
        f.open("system-info", { { "id", "NES" } });
        f.model.set("system:manufacturer", "");
        check(f.model.runAction("system.saveInfo").ok, "saved again");
        check(!contains(readFile(f.root + "/collections/NES/info.conf"), "manufacturer"), "removed");
    });

    run("systems: art is replaced, put back, and a missing one can't be cleared", [] {
        Fixture f("sys-media");
        f.open("system-media", { { "id", "NES" } });
        Forms::Rows rows = f.model.rows("system:media", 50);
        check(!rows.items.empty() && rows.items[0].id == "logo" && contains(rows.items[0].image, "logo.png"), "logo with a thumbnail");
        f.open("system-medium", { { "id", "NES" }, { "slot", "marquee" } });
        checkEqual(f.model.get("system:hasOwnMedia"), "no", "no marquee");
        check(!f.model.actionProblem("system.clearMedia").empty(), "nothing to clear");

        f.open("system-medium", { { "id", "NES" }, { "slot", "logo" } });
        writeFile(f.root + "/new.png", "new logo");
        check(!f.model.actionProblem("system.replaceMedia").empty(), "a file must be chosen first");
        f.model.set("system:mediaFile", f.root + "/new.png");
        Forms::Model::Result replaced = f.model.runAction("system.replaceMedia");
        check(replaced.ok, "replaced: " + replaced.message);
        checkEqual(readFile(f.root + "/collections/NES/system_artwork/logo.png"), "new logo", "new file in place");
        checkEqual(f.model.get("system:canRestoreMedia"), "yes", "the original can be put back");
        check(f.model.runAction("system.restoreMedia").ok, "put back");
        checkEqual(readFile(f.root + "/collections/NES/system_artwork/logo.png"), "old logo", "original again");
        checkEqual(f.model.get("system:canRestoreMedia"), "no", "nothing more to put back");
    });

    run("systems: playlists and their entries", [] {
        Fixture f("sys-playlists");
        f.open("system-playlists", { { "id", "Arcade" } });
        f.model.set("system:newPlaylist", "favorites");
        check(!f.model.actionProblem("system.createPlaylist").empty(), "a reserved name is refused");
        f.model.set("system:newPlaylist", "Best");
        Forms::Model::Result created = f.model.runAction("system.createPlaylist");
        check(created.ok && created.open == "system-playlist" && created.params["playlist"] == "Best", "created and opened");

        f.open("system-playlist-entry", { { "id", "Arcade" }, { "playlist", "Best" }, { "game", "a" } });
        checkEqual(f.model.get("system:isInPlaylist"), "no", "not in it yet");
        Forms::Model::Result added = f.model.runAction("system.addEntry");
        check(added.ok && added.back, "added: " + added.message);
        checkEqual(readFile(f.root + "/collections/Arcade/playlists/Best.txt"), "a" + std::string(Library::defaultNewline()), "written");

        f.open("system-playlist", { { "id", "Arcade" }, { "playlist", "Best" } });
        check(f.ids("system:entries") == std::vector<std::string>{ "a" }, "entries listed");
        std::vector<std::string> candidates = f.ids("system:candidates");
        check(has(candidates, "b"), "games to add");
        f.model.set("system:playlistName", "Top");
        Forms::Model::Result renamed = f.model.runAction("system.renamePlaylist");
        check(renamed.ok && renamed.back && fs::exists(f.root + "/collections/Arcade/playlists/Top.txt"), "renamed: " + renamed.message);

        f.open("system-playlist-entry", { { "id", "Arcade" }, { "playlist", "Top" }, { "game", "a" } });
        checkEqual(f.model.get("system:isInPlaylist"), "yes", "in it");
        check(f.model.runAction("system.removeEntry").ok, "taken out");

        f.open("system-playlist", { { "id", "Arcade" }, { "playlist", "Top" } });
        check(f.model.runAction("system.deletePlaylist").ok && !fs::exists(f.root + "/collections/Arcade/playlists/Top.txt"), "deleted");
        f.open("system-playlist", { { "id", "Arcade" }, { "playlist", "lastplayed" } });
        checkEqual(f.model.get("system:canEditPlaylist"), "no", "reserved lists are read-only");
        check(!f.model.actionProblem("system.deletePlaylist").empty(), "and can't be deleted");
    });

    run("systems: adding a system with an installed launcher under any menu", [] {
        Fixture f("sys-add");
        writeFile(f.root + "/roms/Genesis/sonic.md", "x");
        f.open("system-add", {});
        checkEqual(f.model.get("system:addMenu"), "Main", "the top menu by default");
        checkEqual(f.model.get("system:addIsNewLauncher"), "yes", "a new emulator by default");
        f.model.set("system:addName", "Arcade");
        check(contains(f.model.actionProblem("system.create"), "already"), "an existing name is refused");
        f.model.set("system:addName", "Genesis");
        f.model.set("system:addMenu", "Consoles");
        f.model.set("system:addFolder", Utils::combinePath(f.root, "roms", "Genesis"));
        f.model.set("system:addExtensions", ".md");
        checkEqual(f.model.get("system:addFolderCount"), "1 matching file", "files counted");
        f.model.set("system:addLauncher", "mame");
        checkEqual(f.model.actionProblem("system.create"), "", "ready");
        Forms::Model::Result created = f.model.runAction("system.create");
        check(created.ok && created.open == "system" && created.replace && created.params["id"] == "Genesis", "created, in place of the add form: " + created.message);
        std::string settings = readFile(f.root + "/collections/Genesis/settings.conf");
        check(contains(settings, "list.extensions = md") && contains(settings, "launcher = mame"), "settings: " + settings);
        check(contains(readFile(f.root + "/collections/Consoles/menu.txt"), "Genesis"), "listed under Consoles");
        f.open("system", { { "id", "Genesis" } });
        checkEqual(f.model.get("system:gameCount"), "1 game", "indexed");
        checkEqual(f.model.get("system:canDelete"), "yes", "made here, so it can be deleted");
    });

    run("videos: adding a video collection makes a VideoPlayer launcher", [] {
        Fixture f("sys-add-video");
        writeFile(f.root + "/roms/Music/song.mp4", "x");
        writeFile(f.root + "/vlc.exe", "x");
        f.open("video-add", {});
        checkEqual(f.model.get("video:addIsNewLauncher"), "yes", "no player yet, so a new one");
        f.model.set("video:addName", "Music");
        f.model.set("video:addFolder", Utils::combinePath(f.root, "roms", "Music"));
        f.model.set("video:addExtensions", "mp4,mkv,avi");
        f.model.set("video:addProgram", Utils::combinePath(f.root, "vlc.exe"));
        check(contains(f.model.get("video:addArguments"), "--play-and-exit"), "VLC's arguments");
        Forms::Model::Result created = f.model.runAction("video.create");
        check(created.ok && created.open == "video", "created: " + created.message);
        std::string launcher = readFile(f.root + "/launchers." + os() + "/VideoPlayer.conf");
        check(contains(launcher, "vlc.exe") && contains(launcher, "%ITEM_FILEPATH%"), "player launcher: " + launcher);
        checkEqual(readFile(f.root + "/collections/Music/info.conf"), "type = videos" + std::string(Library::defaultNewline()), "marked");
        check(contains(readFile(f.root + "/collections/Music/settings.conf"), "launcher = VideoPlayer"), "uses it");
        f.open("videos", {});
        check(has(f.ids("video:search"), "Music"), "listed in Videos");
        f.open("systems", {});
        check(!has(f.ids("system:search"), "Music"), "not in Systems");
        f.open("video-add", {});
        checkEqual(f.model.get("video:addLauncher"), "VideoPlayer", "the next one uses the player");
    });

    run("systems: deleting an empty system keeps its files in the backups", [] {
        Fixture f("sys-delete");
        f.open("system", { { "id", "Empty" } });
        checkEqual(f.model.get("system:canDelete"), "yes", "empty, so it can go");
        Forms::Model::Result deleted = f.model.runAction("system.delete");
        check(deleted.ok && deleted.back, "deleted: " + deleted.message);
        check(contains(deleted.message, "isn't touched"), "says what stays");
        check(!fs::exists(f.root + "/collections/Empty"), "folder gone");
        check(!contains(readFile(f.root + "/collections/Consoles/menu.txt"), "Empty"), "menu line gone");
        check(!keptFolder(f.root, "Empty").empty() && fs::exists(keptFolder(f.root, "Empty") + "/settings.conf"),
              "the whole folder is kept in manage/backups/deleted");
        check(fs::is_directory(Utils::combinePath(f.root, "roms", "Empty")), "games folder untouched");
    });

    run("systems: delete takes a system out of every menu, and refuses a menu.xml one", [] {
        Fixture f("sys-delete-menus");
        writeFile(f.root + "/collections/Twice/settings.conf", "# Created by RetroFE Manage\nlist.path = " + f.root + "/roms/Twice\nlist.extensions = zip\n");
        writeFile(f.root + "/collections/Pair/settings.conf", "# Created by RetroFE Manage\nlist.path = " + f.root + "/roms/Pair\nlist.extensions = zip\n");
        writeFile(f.root + "/collections/Main/menu.txt", "Arcade\r\nConsoles\r\nClips\r\nTwice\r\nPair\r\n");
        writeFile(f.root + "/collections/Consoles/menu.txt", "NES\r\nEmpty\r\nTwice\r\n");
        writeFile(f.root + "/collections/Xml/menu.xml", "<menu><item collection=\"Pair\"/></menu>");
        f.rescan();
        f.open("system", { { "id", "Twice" } });
        checkEqual(f.model.get("system:menus"), "Consoles, Main", "listed twice");
        Forms::Model::Result deleted = f.model.runAction("system.delete");
        check(deleted.ok, "deleted: " + deleted.message);
        check(!contains(readFile(f.root + "/collections/Main/menu.txt"), "Twice")
              && !contains(readFile(f.root + "/collections/Consoles/menu.txt"), "Twice"), "out of both menus");
        f.open("system", { { "id", "Pair" } });
        checkEqual(f.model.get("system:canDelete"), "no", "a menu.xml lists it");
        check(contains(f.model.actionProblem("system.delete"), "menu.xml"), "and says so: " + f.model.actionProblem("system.delete"));
    });

    run("systems: delete never takes games: files inside its folder, or anything but settings and art", [] {
        Fixture f("sys-delete-inside");
        // Its games folder is inside it, and holds a file the extensions don't match: 0 games
        writeFile(f.root + "/collections/Inside/settings.conf",
                  "# Created by RetroFE Manage\nlist.path = " + f.root + "/collections/Inside/roms\nlist.extensions = zip\n");
        writeFile(f.root + "/collections/Inside/roms/game.ZIP", "x");
        // Made here, games elsewhere, but something else in its folder
        writeFile(f.root + "/collections/Junk/settings.conf",
                  "# Created by RetroFE Manage\nlist.path = " + f.root + "/roms/Arcade\nlist.extensions = zip\n");
        writeFile(f.root + "/collections/Junk/notes.zip", "mine");
        writeFile(f.root + "/collections/Junk/system_artwork/logo.png", "art");
        f.rescan();
        f.open("system", { { "id", "Inside" } });
        checkEqual(f.model.get("system:canDelete"), "no", "games folder inside");
        check(contains(f.model.actionProblem("system.delete"), "inside its own folder"), f.model.actionProblem("system.delete"));
        f.open("system", { { "id", "Junk" } });
        checkEqual(f.model.get("system:canDelete"), "no", "a stray file");
        check(contains(f.model.actionProblem("system.delete"), "notes.zip"), f.model.actionProblem("system.delete"));
        auto index = f.indexer.index();
        check(!Manager::madeHere(index->collections[static_cast<size_t>(index->findCollection("Junk"))]),
              "a header alone doesn't make it made here");
        check(Manager::madeHere(index->collections[static_cast<size_t>(index->findCollection("Inside"))]) == false,
              "nor with a roms folder in it");
    });

    run("systems: a failed add leaves nothing behind", [] {
        Fixture f("sys-add-fails");
        writeFile(f.root + "/roms/Genesis/sonic.md", "x");
        std::string menu = f.root + "/collections/Consoles/menu.txt";
        fs::permissions(menu, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove);
        if (std::ofstream(menu, std::ios::app)) {
            // root (e.g. in a container) writes read-only files anyway: nothing fails to test
            fs::permissions(menu, fs::perms::owner_write, fs::perm_options::add);
            std::cout << "  (skipped: read-only files are writable here)" << std::endl;
            return;
        }
        f.open("system-add", {});
        f.model.set("system:addName", "Genesis");
        f.model.set("system:addMenu", "Consoles");
        f.model.set("system:addFolder", Utils::combinePath(f.root, "roms", "Genesis"));
        f.model.set("system:addExtensions", "md");
        f.model.set("system:addLauncher", "mame");
        Forms::Model::Result created = f.model.runAction("system.create");
        fs::permissions(menu, fs::perms::owner_write, fs::perm_options::add);
        check(!created.ok && contains(created.message, "Nothing was added"), "refused: " + created.message);
        check(!fs::exists(f.root + "/collections/Genesis"), "no half-made folder");
        check(!contains(readFile(menu), "Genesis"), "menu unchanged");
    });

    run("systems: a name differing only in case from a folder is refused", [] {
        Fixture f("sys-add-case");
        f.open("system-add", {});
        f.model.set("system:addName", "arcade");
        check(contains(f.model.actionProblem("system.create"), "\"Arcade\""), f.model.actionProblem("system.create"));
    });

    run("systems: toggles keep yes/no and true/false styles", [] {
        Fixture f("sys-style");
        writeFile(f.root + "/collections/NES/settings.conf",
                  "list.path = " + f.root + "/roms/NES\nlist.extensions = nes\nlist.includeMissingItems = no\n");
        writeFile(f.root + "/collections/Arcade/settings.conf",
                  "list.path = " + f.root + "/roms/Arcade\nlist.extensions = zip\nlist.includeMissingItems = false\n");
        f.rescan();
        for (const char* system : { "NES", "Arcade" }) {
            f.open("system-settings", { { "id", system } });
            checkEqual(f.model.get("system:includeMissing"), "no", std::string(system) + " starts off");
            f.model.set("system:includeMissing", "yes");
            check(f.model.runAction("system.saveSettings").ok, "saved");
        }
        check(contains(readFile(f.root + "/collections/NES/settings.conf"), "list.includeMissingItems = yes"), "yes/no kept");
        check(contains(readFile(f.root + "/collections/Arcade/settings.conf"), "list.includeMissingItems = true"), "true/false kept");
    });

    run("systems: a local launcher is offered by the name RetroFE looks up", [] {
        Fixture f("sys-local-launcher");
        f.open("system-launcher", { { "id", "NES" } });
        auto options = f.model.options("system:launchers");
        check(std::any_of(options.begin(), options.end(), [](const auto& o) { return o.first == "NES.special"; }), "NES.special offered");
        check(std::none_of(options.begin(), options.end(), [](const auto& o) { return o.first == "special"; }), "not the bare name");
        auto index = f.indexer.index();
        check(contains(Library::Editor::checkSetting(*index, "NES", "launcher", "special"), "NES.special"), "the bare name is refused, saying why");
        f.model.set("system:launcher", "NES.special");
        check(f.model.runAction("system.saveLauncher").ok, "saved");
        f.open("system", { { "id", "NES" } });
        std::string inEffect = f.model.get("system:launcherInEffect");
        check(contains(inEffect, "NES.special") && !contains(inEffect, "isn't installed"), "it resolves: " + inEffect);
    });

    run("systems: playlist edits work before the index catches up", [] {
        std::string root = makeInstall("sys-stale-library");
        Library::ScanOptions scan;
        scan.root = root;
        auto stale = std::shared_ptr<const Library::Index>(Library::buildIndex(scan).release());
        Library::Editor editor(root);
        check(editor.createPlaylist(*stale, "Arcade", "Fresh").ok, "created");
        check(editor.addToPlaylist(*stale, "Arcade", "Fresh", "Arcade", "a").ok, "added with the old index");
        check(editor.removeFromPlaylist(*stale, "Arcade", "Fresh", "Arcade", "a").ok, "removed with the old index");
        check(editor.renamePlaylist(*stale, "Arcade", "Fresh", "Newer").ok, "renamed");
        check(editor.addToPlaylist(*stale, "Arcade", "Newer", "Arcade", "b").ok
              && editor.removeFromPlaylist(*stale, "Arcade", "Newer", "Arcade", "b").ok, "the renamed one too");

        Fixture f("sys-stale");
        f.open("system-playlists", { { "id", "Arcade" } });
        f.model.set("system:newPlaylist", "Quick");
        check(f.model.runAction("system.createPlaylist").ok, "created");
        f.model.setScreen("system-playlists", { { "id", "Arcade" } });   // no waiting for the index
        check(has(f.ids("system:playlists"), "Quick"), "listed at once");
        f.model.setScreen("system-playlist-entry", { { "id", "Arcade" }, { "playlist", "Quick" }, { "game", "a" } });
        check(f.model.runAction("system.addEntry").ok, "added");
        f.model.setScreen("system-playlist-entry", { { "id", "Arcade" }, { "playlist", "Quick" }, { "game", "a" } });
        Forms::Model::Result removed = f.model.runAction("system.removeEntry");
        check(removed.ok, "removed: " + removed.message);
        f.model.setScreen("system-playlist", { { "id", "Arcade" }, { "playlist", "Quick" } });
        f.model.set("system:playlistName", "Quicker");
        check(f.model.runAction("system.renamePlaylist").ok, "renamed");
        f.model.setScreen("system-playlists", { { "id", "Arcade" } });
        std::vector<std::string> names = f.ids("system:playlists");
        check(has(names, "Quicker") && !has(names, "Quick"), "the new name at once");
    });

    run("systems: cyclePlaylist decides which playlists RetroFE shows", [] {
        Fixture f("sys-cycle");
        writeFile(f.root + "/collections/Arcade/settings.conf",
                  "list.path = " + f.root + "/roms/Arcade\nlist.extensions = zip\ncyclePlaylist = Fighters\n");
        writeFile(f.root + "/collections/Arcade/playlists/Fighters.txt", "a\n");
        writeFile(f.root + "/collections/Arcade/playlists/Best.txt", "b\n");
        writeFile(f.root + "/collections/Arcade/playlists/Year.txt", "a\n");
        f.rescan();
        auto index = f.indexer.index();
        const Library::CollectionRecord& arcade = index->collections[static_cast<size_t>(index->findCollection("Arcade"))];
        check(arcade.cyclePlaylist == std::vector<std::string>{ "Fighters" } && arcade.cyclePlaylistOwn, "read into the library");
        f.open("system-check", { { "id", "Arcade" } });
        check(f.anyFinding("system", "The playlist Best isn't in cyclePlaylist"), "a finding");
        f.open("system-playlist", { { "id", "Arcade" }, { "playlist", "Year" } });
        check(contains(f.model.get("system:playlistNote"), "sorts a playlist called Year by Year"), f.model.get("system:playlistNote"));
        f.open("system-playlist", { { "id", "Arcade" }, { "playlist", "Best" } });
        check(contains(f.model.get("system:playlistNote"), "isn't in cyclePlaylist"), "the playlist says so");
        checkEqual(f.model.get("system:canShowPlaylist"), "yes", "and offers to fix it");
        Forms::Model::Result shown = f.model.runAction("system.showPlaylist");
        check(shown.ok, "shown: " + shown.message);
        check(contains(readFile(f.root + "/collections/Arcade/settings.conf"), "cyclePlaylist = Fighters,Best"), "appended");
        check(contains(f.model.runAction("system.showPlaylist").message, "already"), "not twice");
        f.open("system-playlists", { { "id", "Arcade" } });
        f.model.set("system:newPlaylist", "Fresh");
        check(contains(f.model.runAction("system.createPlaylist").message, "cyclePlaylist"), "creating says it won't show");
    });

    run("systems: reserved playlist names match RetroFE's, in any case", [] {
        Fixture f("sys-reserved");
        writeFile(f.root + "/collections/Arcade/playlists/LastPlayed.txt", "a\n");
        writeFile(f.root + "/collections/Arcade/playlists/favourites.txt", "a\n");
        f.rescan();
        auto index = f.indexer.index();
        for (const Library::PlaylistInfo& playlist : index->collections[static_cast<size_t>(index->findCollection("Arcade"))].playlists) {
            check(playlist.reserved == (playlist.name == "LastPlayed"), playlist.name + " reserved as RetroFE treats it");
        }
        check(!Library::Editor::isReservedPlaylist("favourites") && Library::Editor::isReservedPlaylist("ALL"), "the same names for edits");
    });

    run("systems: one finding for missing extensions; metadata names every source", [] {
        Fixture f("sys-findings");
        writeFile(f.root + "/collections/Bare/settings.conf", "list.path = " + f.root + "/roms/NES\nlauncher = mame\n");
        writeFile(f.root + "/collections/Main/menu.txt", "Arcade\r\nConsoles\r\nClips\r\nBare\r\n");
        f.rescan();
        f.open("system-check", { { "id", "Bare" } });
        int mentions = 0;
        for (int i = 0; i < 12; i++) {
            mentions += contains(f.model.get("system:finding" + std::to_string(i)), "extensions") ? 1 : 0;
        }
        check(mentions == 1, "said once, not " + std::to_string(mentions) + " times");
        f.open("system-check", { { "id", "Arcade" } });
        check(f.anyFinding("system", "meta/mamelist"), "mamelist mentioned");
    });

    run("systems: the info screen marks a system as a video collection", [] {
        Fixture f("sys-mark-video");
        f.open("system-info", { { "id", "NES" } });
        checkEqual(f.model.get("system:isVideo"), "no", "a system");
        f.model.set("system:isVideo", "yes");
        Forms::Model::Result saved = f.model.runAction("system.saveInfo");
        check(saved.ok && contains(saved.message, "Videos"), "saved: " + saved.message);
        check(contains(readFile(f.root + "/collections/NES/info.conf"), "type = videos"), "written");
        f.open("videos", {});
        check(has(f.ids("video:search"), "NES"), "listed under Videos");
        f.open("video-info", { { "id", "NES" } });
        f.model.set("video:isVideo", "no");
        check(f.model.runAction("video.saveInfo").ok, "unmarked");
        check(!contains(readFile(f.root + "/collections/NES/info.conf"), "type"), "taken out");
        auto index = f.indexer.index();
        check(!Library::Editor::checkSetting(*index, "NES", "type", "movies").empty(), "only videos or nothing");
    });
}

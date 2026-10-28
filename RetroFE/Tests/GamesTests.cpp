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

// Tests for the Games screens (Manager/Games): search, details, metadata,
// launcher, media, playlists and story text, through ManagerModel as the
// screens use them, on a small install in a temp folder.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Database/DB.h"
#include "../Source/Database/MetadataDatabase.h"
#include "../Source/Forms/Form.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Library/Media.h"
#include "../Source/Library/MetadataOverrides.h"
#include "../Source/Manager/Games/GameProvider.h"
#include "../Source/Manager/LaunchQueue.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Utility/Utils.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    // Main (menu): Arcade, Consoles; Consoles (menu): NES
    // Arcade: a b c (zip), launcher mame; metadata for a and b; a has a logo and a story
    // NES: mario zelda, launcher mame
    // All (merged): every Arcade game (empty Arcade.sub); playlist best = a
    std::string makeInstall(const std::string& name)
    {
        std::string root = makeRoot(name);
        std::string os = Utils::getOSType();
        writeFile(root + "/settings.conf", "firstCollection = Main\n");
        writeFile(root + "/launchers." + os + "/mame.conf", "executable = mame.exe\narguments = %ITEM_NAME%\n");
        writeFile(root + "/launchers." + os + "/fbneo.conf", "executable = fbneo.exe\n");
        writeFile(root + "/collections/Main/menu.txt", "Arcade\nConsoles\n");
        writeFile(root + "/collections/Consoles/menu.txt", "NES\n");
        writeFile(root + "/collections/Arcade/settings.conf", "list.extensions = zip\nlauncher = mame\n");
        for (const char* rom : { "a.zip", "b.zip", "c.zip" }) {
            writeFile(root + "/collections/Arcade/roms/" + rom, "12345");
        }
        writeFile(root + "/collections/NES/settings.conf", "list.extensions = nes\nlauncher = mame\n");
        writeFile(root + "/collections/NES/roms/mario.nes", "x");
        writeFile(root + "/collections/NES/roms/zelda.nes", "x");
        writeFile(root + "/collections/All/Arcade.sub", "");
        writeFile(root + "/collections/All/playlists/best.txt", "_Arcade:a\n");
        writeFile(root + "/collections/Arcade/medium_artwork/logo/a.png", "logo");
        writeFile(root + "/collections/Arcade/medium_artwork/marquee/default.png", "marquee");
        writeFile(root + "/collections/Arcade/medium_artwork/story/a.txt", "Line one\r\nLine two\r\n");
        writeFile(root + "/meta/hyperlist/Arcade.xml",
                  "<?xml version=\"1.0\"?>\n<menu>\n"
                  "<game name=\"a\"><description>Alpha</description><year>1980</year><manufacturer>Acme</manufacturer></game>\n"
                  "<game name=\"b\"><description>Beta</description><year>1981</year></game>\n"
                  "</menu>\n");
        writeFile(root + "/import/new.png", "new logo");
        writeFile(root + "/import/new.txt", "From a file\n");

        // meta.db as RetroFE builds it
        Configuration::absolutePath = root;
        Configuration config;
        DB db(root + "/meta.db");
        db.initialize();
        MetadataDatabase meta(db, config);
        meta.initialize();
        return root;
    }

    // The library, editor and model the Games screens run on
    struct Fixture
    {
        explicit Fixture(const std::string& root)
            : indexer(options(root))
            , editor(root)
            , model(config)
        {
            Configuration::absolutePath = root;
            indexer.rebuild();
            indexer.wait(10000);
            Manager::Services services{ config, nullptr };
            services.library = &indexer;
            services.editor = &editor;
            services.requestLaunch = [this](const std::string& collection, const std::string& game) {
                launched.push_back(collection + "/" + game);
            };
            services.launchProblem = [this] { return launchProblem; };
            auto owned = std::make_unique<Manager::GameProvider>(services);
            games = owned.get();
            model.addProvider(std::move(owned));
            model.refresh();
        }

        static Library::ScanOptions options(const std::string& root)
        {
            Library::ScanOptions o;
            o.root = root;
            return o;
        }

        void open(const std::string& form, const Forms::Params& params)
        {
            model.setScreen(form, params);
            games->update(0);
            games->finishPendingWork();   // the media worker's lookups
        }

        // An edit asks for a new index; wait for it like the screens do
        void settle()
        {
            indexer.wait(10000);
            games->update(0);
            games->finishPendingWork();
        }

        std::shared_ptr<const Library::Index> scan()
        {
            return std::shared_ptr<const Library::Index>(Library::buildIndex(indexer.options()).release());
        }

        Configuration config;
        Library::Indexer indexer;
        Library::Editor editor;
        ManagerModel model;
        Manager::GameProvider* games = nullptr;
        std::vector<std::string> launched;   // requestLaunch calls: "<collection>/<game>"
        std::string launchProblem;           // what launchProblem says
    };

    Forms::Params gameParams(const std::string& collection, const std::string& id)
    {
        return { { "collection", collection }, { "id", id } };
    }

    std::vector<std::string> rowIds(const Forms::Rows& rows)
    {
        std::vector<std::string> ids;
        for (const Forms::Row& row : rows.items) {
            ids.push_back(row.id);
        }
        return ids;
    }

    bool hasOption(const std::vector<std::pair<std::string, std::string>>& options, const std::string& value)
    {
        return std::any_of(options.begin(), options.end(), [&](const auto& o) { return o.first == value; });
    }
}

static void runGamesTests();
static Test::Suite gamesSuite("games", runGamesTests);

static void runGamesTests()
{
    run("games: search, filters and the opened row", [] {
        std::string root = makeInstall("games-search");
        Fixture f(root);
        f.open("games", {});
        checkEqual(f.model.get("game:status"), "", "no status once the library is read");
        Forms::Rows all = f.model.rows("game:search", 100);
        check(all.total == 5, "every game: " + std::to_string(all.total));

        f.model.set("game:query", "zel");
        Forms::Rows rows = f.model.rows("game:search", 100);
        check(rows.items.size() == 1 && rows.items[0].id == "zelda", "matches the name");
        if (rows.items.size() == 1) {
            check(rows.items[0].detail.find("NES") != std::string::npos
                  && rows.items[0].detail.find("zelda.nes") != std::string::npos, "row shows system and file: " + rows.items[0].detail);
            Forms::Field results;
            results.type = Forms::FieldType::Results;
            Forms::Params opened = Forms::rowParams(results, rows.items[0]);
            check(opened["collection"] == "NES" && opened["id"] == "zelda", "a row opens its own game");
        }
        f.model.set("game:query", "alpha");
        check(rowIds(f.model.rows("game:search", 100)) == std::vector<std::string>{ "a" }, "matches the title");

        // A menu covers everything below it; Systems opens the screen filtered with param collection
        f.model.set("game:query", "");
        f.open("games", { { "collection", "Consoles" } });
        checkEqual(f.model.get("game:filter"), "Consoles", "param:collection is the first filter");
        check(rowIds(f.model.rows("game:search", 100)) == std::vector<std::string>{ "mario", "zelda" }, "only games under Consoles");
        check(hasOption(f.model.options("game:filters"), "Main") && hasOption(f.model.options("game:filters"), "NES"),
              "menus and systems to filter by");
        f.model.set("game:filter", "Arcade");
        f.open("games", { { "collection", "Consoles" } });   // back to the same screen
        checkEqual(f.model.get("game:filter"), "Arcade", "returning keeps the user's filter");

        f.model.set("game:filter", "");
        f.model.set("game:onlyNoMetadata", "yes");
        std::vector<std::string> bare = rowIds(f.model.rows("game:search", 100));
        check(std::find(bare.begin(), bare.end(), "a") == bare.end() && std::find(bare.begin(), bare.end(), "c") != bare.end(),
              "only games without metadata");

        // Capped, with the total for the "showing N of M" note
        f.model.set("game:onlyNoMetadata", "no");
        Forms::Rows two = f.model.rows("game:search", 2);
        check(two.items.size() == 2 && two.total == 5, "limit honoured, total kept");

        // Thumbnails come from the game's own logo, found a few per frame
        f.model.set("game:query", "alpha");
        f.model.rows("game:search", 100);
        f.games->finishPendingWork();
        Forms::Rows thumbs = f.model.rows("game:search", 100);
        check(!thumbs.items.empty() && Library::iequals(thumbs.items[0].image, Utils::combinePath(root, "collections", "Arcade", "medium_artwork", "logo", "a.png")),
              "thumbnail from the logo");
    });

    run("games: a game's details and links", [] {
        std::string root = makeInstall("games-details");
        Fixture f(root);
        f.open("game", gameParams("Arcade", "a"));
        checkEqual(f.model.get("game:title"), "Alpha", "title");
        checkEqual(f.model.get("game:isFound"), "yes", "found");
        checkEqual(f.model.get("game:system"), "Arcade", "system");
        checkEqual(f.model.get("game:file"), "a.zip", "file");
        checkEqual(f.model.get("game:size"), "5 bytes", "size");
        checkEqual(f.model.get("game:year"), "1980", "year");
        checkEqual(f.model.get("game:shownIn"), "All", "merged collections showing it");
        check(Library::iequals(f.model.get("game:image"), Utils::combinePath(root, "collections", "Arcade", "medium_artwork", "logo", "a.png")),
              "the logo as its picture");
        checkEqual(f.model.get("game:launcherStatus"), "mame", "launcher status");
        checkEqual(f.model.get("game:metadataStatus"), "From Arcade", "metadata status");
        checkEqual(f.model.get("game:descriptionStatus"), "Yes", "has a story");
        checkEqual(f.model.get("game:playlistsStatus"), "1 playlist, in 1 collection", "playlists status");
        f.games->finishPendingWork();
        check(f.model.get("game:mediaStatus").find("2 of") == 0, "own media counted: " + f.model.get("game:mediaStatus"));

        f.open("game", gameParams("Arcade", "gone"));
        checkEqual(f.model.get("game:isFound"), "no", "an unknown game");
        check(!f.model.get("game:notFound").empty(), "says so");
        check(!f.model.actionProblem("game.saveMetadata").empty(), "nothing can be changed");
    });

    run("games: metadata edits, the leave warning and back to shipped", [] {
        std::string root = makeInstall("games-metadata");
        Fixture f(root);
        Manager::Screen screen{ "game-metadata", gameParams("Arcade", "a") };
        f.open(screen.form, screen.params);
        checkEqual(f.model.get("game:meta:description"), "Alpha", "current title");
        checkEqual(f.model.actionProblem("game.saveMetadata"), "No changes to save.", "nothing to save yet");

        f.model.set("game:meta:year", "1982");
        f.model.set("game:meta:genre", " Maze ");
        checkEqual(f.model.get("game:isMetadataDirty"), "yes", "dirty");
        checkEqual(f.model.get("game:metaStatus:year"), "Not saved yet.", "unsaved field");
        check(!f.model.leaving(screen).empty(), "leaving warns");
        Forms::Model::Result saved = f.model.runAction("game.saveMetadata");
        check(saved.ok, "saved: " + saved.message);
        check(f.model.leaving(screen).empty(), "nothing unsaved");
        std::string overrides = Test::readFile(Library::MetadataOverrides::fileFor(root, "Arcade"));
        check(overrides.find("<year>1982</year>") != std::string::npos && overrides.find("<genre>Maze</genre>") != std::string::npos,
              "kept in the override layer, trimmed");
        f.settle();
        checkEqual(f.model.get("game:meta:year"), "1982", "field shows the saved value");
        checkEqual(f.model.get("game:isChanged:year"), "yes", "marked as changed here");
        checkEqual(f.model.get("game:hasOverrides"), "yes", "the put-back section shows");
        checkEqual(f.model.get("game:metaStatus:year"), "Changed here. Shipped: 1980", "shipped value shown");

        // A blank title is refused, and other edits stay unsaved
        f.model.set("game:meta:description", "");
        f.model.set("game:meta:players", "2");
        Forms::Model::Result refused = f.model.runAction("game.saveMetadata");
        check(!refused.ok, "blank title refused");
        checkEqual(f.model.get("game:isMetadataDirty"), "yes", "edits kept");
        f.model.runAction("game.resetMetadata");
        checkEqual(f.model.get("game:meta:description"), "Alpha", "reset");

        // Leaving again discards
        f.model.set("game:meta:rating", "5");
        f.model.discard(screen);
        checkEqual(f.model.get("game:meta:rating"), "", "discarded");

        // Back to the shipped value, per field
        Forms::Model::Result unset = f.model.runAction("game.unset.year");
        check(unset.ok, "unset: " + unset.message);
        checkEqual(f.model.get("game:meta:year"), "1980", "shipped year back in the field");
        f.settle();
        checkEqual(f.model.get("game:isChanged:year"), "no", "no longer changed");
        check(!f.model.actionProblem("game.unset.year").empty(), "nothing left to put back");
        f.open("game", gameParams("Arcade", "a"));
        checkEqual(f.model.get("game:year"), "1980", "details show the shipped year");
        checkEqual(f.model.get("game:metadataStatus"), "Changed here", "genre is still changed");
    });

    run("games: launcher choice", [] {
        std::string root = makeInstall("games-launcher");
        Fixture f(root);
        f.open("game-launcher", gameParams("Arcade", "c"));
        checkEqual(f.model.get("game:launcher"), "", "uses the system's");
        checkEqual(f.model.get("game:systemLauncher"), "mame", "system's launcher");
        auto options = f.model.options("game:launchers");
        check(hasOption(options, "") && hasOption(options, "mame") && hasOption(options, "fbneo"), "installed launchers offered");
        f.model.set("game:launcher", "fbneo");
        Forms::Model::Result saved = f.model.runAction("game.saveLauncher");
        check(saved.ok, "saved: " + saved.message);
        std::string file = root + "/collections/Arcade/launchers/c.conf";
        checkEqual(Test::readFile(file), "fbneo\n", "override file");
        f.settle();
        checkEqual(f.model.get("game:hasLauncherOverride"), "yes", "has its own launcher");
        check(f.model.get("game:gameLauncher").find("fbneo") == 0, "starts with fbneo");
        check(f.model.runAction("game.useSystemLauncher").ok, "back to the system's");
        check(!fs::exists(file), "override removed");
        f.settle();
        checkEqual(f.model.get("game:launcher"), "", "choice back to the system's");

        // A name that isn't installed is shown and warned about
        Test::writeFile(file, "ghost\n");
        f.indexer.changed();
        f.settle();
        f.model.refresh();
        f.open("game-launcher", gameParams("Arcade", "c"));
        checkEqual(f.model.get("game:launcher"), "ghost", "the override's name");
        check(hasOption(f.model.options("game:launchers"), "ghost"), "listed so the choice shows it");
        std::string problem = f.model.get("game:launcherProblem");
        check(problem.find("won't start") != std::string::npos && problem.find("ghost") != std::string::npos,
              "one line says it won't start, and why: " + problem);
        Forms::Rows add = f.model.rows("game:addLaunchers", 10);
        check(add.items.size() == 1 && add.items[0].id == "ghost" && add.items[0].link
              && add.items[0].label == "Add a launcher called ghost", "a link to add the launcher it asks for");
        if (add.items.size() == 1) {
            Forms::Field results;
            results.type = Forms::FieldType::Results;
            results.param = "name";
            checkEqual(Forms::rowParams(results, add.items[0])["name"], "ghost", "opens Add a launcher with the name");
        }
        checkEqual(f.model.get("game:launcherFile"), "collections/Arcade/launchers/c.conf", "Saved in: the game's launcher file");

        // A local launcher of the system hides the global one of the same name: offered once
        Test::writeFile(root + "/collections/Arcade/launchers.local/fbneo.conf", "executable = fbneo-arcade.exe\n");
        f.indexer.changed();
        f.settle();
        f.model.refresh();
        f.open("game-launcher", gameParams("Arcade", "c"));
        auto deduped = f.model.options("game:launchers");
        size_t fbneo = std::count_if(deduped.begin(), deduped.end(), [](const auto& o) { return o.first == "fbneo"; });
        check(fbneo == 1, "fbneo offered once: " + std::to_string(fbneo));
        for (const auto& option : deduped) {
            if (option.first == "fbneo") {
                check(option.second.find("hides the global fbneo") != std::string::npos, "says it hides the global: " + option.second);
            }
            if (option.first == "mame") {
                check(option.second.find("same as using the system's") != std::string::npos, "the system's own is marked: " + option.second);
            }
        }
    });

    run("games: media slots, replace, clear and put back", [] {
        std::string root = makeInstall("games-media");
        Fixture f(root);
        f.open("game-media", gameParams("Arcade", "a"));
        f.games->finishPendingWork();
        Forms::Rows rows = f.model.rows("game:media", 100);
        std::vector<std::string> types = rowIds(rows);
        check(types.size() > 8 && types[0] == "logo" && types[1] == "artwork_front", "slots in order");
        auto marquee = std::find_if(rows.items.begin(), rows.items.end(), [](const Forms::Row& r) { return r.id == "marquee"; });
        check(marquee != rows.items.end() && marquee->detail.find("Fallback") == 0, "fallback shown: "
              + (marquee != rows.items.end() ? marquee->detail : std::string()));
        if (!rows.items.empty()) {
            Forms::Field results;
            results.param = "type";
            Forms::Params opened = Forms::rowParams(results, rows.items[0]);
            check(opened["type"] == "logo" && opened["id"] == "a" && opened["collection"] == "Arcade", "a slot opens with the game");
        }

        // A fallback can't be cleared
        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "a" }, { "type", "marquee" } });
        check(f.model.get("game:slot:status").find("fallback") != std::string::npos, "fallback status");
        check(!f.model.actionProblem("game.clearMedia").empty(), "clearing a fallback refused");
        checkEqual(f.model.actionProblem("game.replaceMedia"), "Choose a file first.", "replace needs a file");

        // Replace the logo, then put the original back
        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "a" }, { "type", "logo" } });
        checkEqual(f.model.get("game:slot:status"), "Its own file", "own logo");
        checkEqual(f.model.get("game:slot:hasOriginal"), "no", "nothing changed yet");
        f.model.set("game:slot:newFile", root + "/import/new.png");
        Forms::Model::Result replaced = f.model.runAction("game.replaceMedia");
        check(replaced.ok, "replaced: " + replaced.message);
        check(replaced.back, "back to the media list, which shows the new picture");
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/logo/a.png"), "new logo", "new file in place");
        checkEqual(f.model.get("game:slot:status"), "Still looking for its files...", "the slot is looked up again, on the worker");
        f.games->finishPendingWork();
        checkEqual(f.model.get("game:slot:hasOriginal"), "yes", "original kept");
        checkEqual(f.model.get("game:slot:newFile"), "", "file field cleared");
        check(f.model.runAction("game.restoreMedia").ok, "put back");
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/logo/a.png"), "logo", "original back");
        f.games->finishPendingWork();

        // A file of the wrong kind is refused
        f.model.set("game:slot:newFile", root + "/import/new.txt");
        check(!f.model.runAction("game.replaceMedia").ok, "a text file isn't a logo");

        check(f.model.runAction("game.clearMedia").ok, "own file cleared");
        check(!fs::exists(root + "/collections/Arcade/medium_artwork/logo/a.png"), "gone (kept in backups)");
        f.games->finishPendingWork();
        checkEqual(f.model.get("game:slot:status"), "Missing", "slot looked up again");
    });

    run("games: a media slot kept as a folder of files (OneSauce)", [] {
        std::string root = makeInstall("games-media-folder");
        const std::string videos = root + "/collections/Arcade/medium_artwork/video/a";
        writeFile(videos + "/a_1.mp4", "one");
        writeFile(videos + "/a_2.mp4", "two");
        writeFile(root + "/collections/Arcade/medium_artwork/logo/a/other.png", "png");   // beside a.png, which wins
        Fixture f(root);
        f.open("game-media", gameParams("Arcade", "a"));
        Forms::Rows rows = f.model.rows("game:media", 100);
        auto video = std::find_if(rows.items.begin(), rows.items.end(), [](const Forms::Row& r) { return r.id == "video"; });
        check(video != rows.items.end() && video->detail == "Its own folder (2)", "the video row: "
              + (video != rows.items.end() ? video->detail : std::string()));
        // Grouped: what shows, and what's missing
        std::vector<std::string> shown = rowIds(f.model.rows("game:mediaShown", 100));
        std::vector<std::string> missing = rowIds(f.model.rows("game:mediaMissing", 100));
        check(std::find(shown.begin(), shown.end(), "video") != shown.end()
              && std::find(shown.begin(), shown.end(), "logo") != shown.end(), "the video and logo show");
        check(std::find(missing.begin(), missing.end(), "screenshot") != missing.end()
              && std::find(missing.begin(), missing.end(), "video") == missing.end(), "a screenshot is missing");
        check(shown.size() + missing.size() == rows.items.size(), "every slot in one of the two");

        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "a" }, { "type", "video" } });
        checkEqual(f.model.get("game:slot:status"), "Its own folder: a folder of 2 files (one shown at random)", "status");
        checkEqual(f.model.get("game:slot:hasFolder"), "yes", "it has a folder");
        checkEqual(f.model.get("game:slot:canClear"), "no", "not one file to clear");
        check(f.model.actionProblem("game.clearMedia").find("folder") != std::string::npos, "Clear says it's a folder");
        Forms::Rows files = f.model.rows("game:slot:folderFiles", 100);
        check(rowIds(files) == std::vector<std::string>({ "a_1.mp4", "a_2.mp4" }), "its files");
        if (!files.items.empty()) {
            Forms::Field results;
            results.param = "file";
            Forms::Params opened = Forms::rowParams(results, files.items[0]);
            check(opened["file"] == "a_1.mp4" && opened["type"] == "video" && opened["id"] == "a" && opened["collection"] == "Arcade",
                  "a file opens with the game and slot");
        }

        // Take one out, from its own screen
        f.open("game-media-folder-file", { { "collection", "Arcade" }, { "id", "a" }, { "type", "video" }, { "file", "a_2.mp4" } });
        check(f.model.get("game:folderFile:note").find("at random") != std::string::npos, "its note");
        Forms::Model::Result removed = f.model.runAction("game.removeFolderFile");
        check(removed.ok && removed.back && !fs::exists(videos + "/a_2.mp4"), "taken out: " + removed.message);
        f.settle();

        // The slot screen: put it back
        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "a" }, { "type", "video" } });
        checkEqual(f.model.get("game:slot:hasOriginal"), "yes", "the removed file can be put back");
        check(f.model.runAction("game.restoreMedia").ok && fs::exists(videos + "/a_2.mp4"), "put back");
        f.settle();

        // A file beside the folder wins, and the screen says so
        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "a" }, { "type", "logo" } });
        checkEqual(f.model.get("game:slot:status"), "Its own file (it wins over the folder beside it)", "logo status");
        check(f.model.get("game:slot:folderNote").find("The file wins") != std::string::npos, "the note: " + f.model.get("game:slot:folderNote"));
        Forms::Rows logos = f.model.rows("game:slot:folderFiles", 100);
        check(logos.items.size() == 1 && logos.items[0].detail == "not shown", "the folder's file isn't shown");
        Forms::Model::Result cleared = f.model.runAction("game.clearFolder");
        check(cleared.ok && !fs::exists(root + "/collections/Arcade/medium_artwork/logo/a")
              && fs::exists(root + "/collections/Arcade/medium_artwork/logo/a.png"), "folder cleared, file kept: " + cleared.message);
    });

    run("games: keep only one file of a media folder, then put it all back", [] {
        std::string root = makeInstall("games-media-folder-keep");
        const std::string videos = root + "/collections/Arcade/medium_artwork/video";
        writeFile(videos + "/a/a_1.mp4", "one");
        writeFile(videos + "/a/a_2.mp4", "two");
        writeFile(root + "/collections/Arcade/roms/default.zip", "x");        // a game named "default"
        writeFile(videos + "/default/d1.mp4", "d1");                        // ... whose folder is everyone's default
        Fixture f(root);
        f.open("game-media-folder-file", { { "collection", "Arcade" }, { "id", "a" }, { "type", "video" }, { "file", "a_2.mp4" } });
        Forms::Model::Result kept = f.model.runAction("game.keepFolderFile");
        check(kept.ok && kept.back, "kept: " + kept.message);
        check(Test::readFile(videos + "/a.mp4") == "two" && !fs::exists(videos + "/a"), "a.mp4 is a_2, the folder gone");
        f.settle();
        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "a" }, { "type", "video" } });
        checkEqual(f.model.get("game:slot:status"), "Its own file", "one file now");
        checkEqual(f.model.get("game:slot:hasFolder"), "no", "no folder");
        checkEqual(f.model.get("game:slot:hasOriginal"), "yes", "Put back is offered");
        check(f.model.actionProblem("game.restoreMedia").empty(), "and allowed");
        Forms::Model::Result back = f.model.runAction("game.restoreMedia");
        check(back.ok && !fs::exists(videos + "/a.mp4") && Test::readFile(videos + "/a/a_1.mp4") == "one"
              && Test::readFile(videos + "/a/a_2.mp4") == "two", "the folder as it was: " + back.message);
        f.settle();
        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "a" }, { "type", "video" } });
        checkEqual(f.model.get("game:slot:hasOriginal"), "no", "nothing left to put back");

        // The game named default: its folder is the collection's default, not emptied from here
        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "default" }, { "type", "video" } });
        check(f.model.get("game:slot:folderNote").find("shares this folder") != std::string::npos, "the note says it's shared");
        check(f.model.actionProblem("game.clearFolder").find("default") != std::string::npos, "Clear the folder is refused");
        check(fs::exists(videos + "/default/d1.mp4"), "the default folder is untouched");
    });

    run("games: favourites, playlists and merged collections", [] {
        std::string root = makeInstall("games-playlists");
        Fixture f(root);
        f.open("game-playlists", gameParams("Arcade", "b"));
        checkEqual(f.model.get("game:isFavourite"), "no", "not a favourite");
        Forms::Model::Result fav = f.model.runAction("game.addFavourite");
        check(fav.ok, "favourite: " + fav.message);
        check(Test::readFile(root + "/collections/Arcade/playlists/favorites.txt").find("b") != std::string::npos, "favorites.txt");
        {
            // Again before the index has caught up (else it's refused up front): the
            // library says it's there already, and that's what's shown, as a neutral note
            Forms::Model::Result again = f.model.runAction("game.addFavourite");
            if (again.ok) {
                check(again.info && again.message.find("Added") == std::string::npos && again.message.find("already") != std::string::npos,
                      "a favourite already isn't reported as added: " + again.message);
            }
        }
        f.settle();
        checkEqual(f.model.get("game:isFavourite"), "yes", "now a favourite");
        check(f.model.runAction("game.removeFavourite").ok, "unfavourite");

        // Playlists of the pages showing it: All shows every Arcade game
        auto playlists = f.model.options("game:playlists");
        check(hasOption(playlists, "All/best"), "All's playlist offered");
        f.model.set("game:playlist", "All/best");
        checkEqual(f.model.get("game:isInPlaylist"), "no", "not in best");
        check(f.model.runAction("game.addToPlaylist").ok, "added to best");
        check(Test::readFile(root + "/collections/All/playlists/best.txt").find("_Arcade:b") != std::string::npos, "qualified entry");
        f.settle();
        checkEqual(f.model.get("game:isInPlaylist"), "yes", "in best");
        check(f.model.runAction("game.removeFromPlaylist").ok, "removed from best");

        // An empty .sub shows every game: taking one out is refused
        f.model.set("game:merged", "All");
        checkEqual(f.model.get("game:isInMerged"), "yes", "All shows it");
        check(f.model.get("game:mergedStatus").find("every other game") != std::string::npos, "says why");
        check(f.model.actionProblem("game.removeFromMerged").find("can't be taken out") != std::string::npos,
              "the button is greyed out with the reason: " + f.model.actionProblem("game.removeFromMerged"));
        check(!f.model.runAction("game.removeFromMerged").ok, "refused for an empty .sub");

        // Favourites are kept per page: on All's page RetroFE writes All's favorites.txt
        auto pages = f.model.options("game:favouritePages");
        check(hasOption(pages, "Arcade") && hasOption(pages, "All"), "its own system and the merged collection showing it");
        checkEqual(f.model.get("game:hasSeveralPages"), "yes", "the page choice shows");
        f.model.set("game:favouritePage", "All");
        checkEqual(f.model.get("game:isFavourite"), "no", "not a favourite on All");
        check(f.model.runAction("game.addFavourite").ok, "favourite on All");
        check(Test::readFile(root + "/collections/All/playlists/favorites.txt").find("_Arcade:b") != std::string::npos,
              "All's favorites.txt, qualified as RetroFE writes it");
        check(!fs::exists(root + "/collections/Arcade/playlists/favorites.txt")
              || Test::readFile(root + "/collections/Arcade/playlists/favorites.txt").find("b") == std::string::npos,
              "not in Arcade's");
        f.settle();
        checkEqual(f.model.get("game:isFavourite"), "yes", "a favourite on All");
        f.model.set("game:favouritePage", "Arcade");
        checkEqual(f.model.get("game:isFavourite"), "no", "but not on Arcade");
        f.open("game", gameParams("Arcade", "b"));
        checkEqual(f.model.get("game:favourite"), "In All", "details say where");
        f.open("game-playlists", gameParams("Arcade", "b"));

        // A NES game can be added to All, which then lists it in NES.sub
        f.open("game-playlists", gameParams("NES", "zelda"));
        f.model.set("game:merged", "All");
        checkEqual(f.model.get("game:isInMerged"), "no", "not shown in All");
        Forms::Model::Result added = f.model.runAction("game.addToMerged");
        check(added.ok, "added: " + added.message);
        checkEqual(Test::readFile(root + "/collections/All/NES.sub"), std::string("zelda") + Library::defaultNewline(), "NES.sub");
        f.settle();
        checkEqual(f.model.get("game:isInMerged"), "yes", "now shown");
        check(f.model.runAction("game.removeFromMerged").ok, "taken out");
        check(!fs::exists(root + "/collections/All/NES.sub"), "the last entry removes the .sub");
    });

    run("games: favourites with globalFavLast", [] {
        std::string root = makeInstall("games-globalfav");
        Test::writeFile(root + "/settings.conf", "firstCollection = Main\nglobalFavLast = yes\n");
        Test::writeFile(root + "/collections/Favorites/playlists/favorites.txt", "_Arcade:a\n");
        Fixture f(root);
        f.open("game-playlists", gameParams("Arcade", "a"));
        checkEqual(f.model.get("game:favouriteText"), "Yes, in the Favorites collection", "read from the Favorites collection");
        checkEqual(f.model.get("game:isFavourite"), "yes", "a favourite");
        checkEqual(f.model.get("game:canFavourite"), "no", "not changed here");
        checkEqual(f.model.get("game:hasSeveralPages"), "no", "no page choice");
        check(!f.model.actionProblem("game.removeFavourite").empty(), "removing refused");
        f.open("game", gameParams("Arcade", "a"));
        checkEqual(f.model.get("game:favourite"), "Yes (in the Favorites collection)", "details");
        f.open("game-playlists", gameParams("Arcade", "b"));
        checkEqual(f.model.get("game:favouriteText"), "No", "b isn't");
        check(!f.model.actionProblem("game.addFavourite").empty(), "adding refused");
    });

    run("games: a game that vanishes while an action runs", [] {
        std::string root = makeInstall("games-vanish");
        Fixture f(root);
        auto before = f.scan();
        fs::remove(root + "/collections/Arcade/roms/c.zip");
        auto after = f.scan();
        check(before && after && before->findGame("Arcade", "c") >= 0 && after->findGame("Arcade", "c") < 0, "two snapshots");

        f.games->setIndexSource([&] { return before; });
        f.open("game-launcher", gameParams("Arcade", "c"));
        f.model.set("game:launcher", "fbneo");
        checkEqual(f.model.actionProblem("game.saveLauncher"), "", "can be saved");

        // A new snapshot is published after the first look: the action keeps the one it checked
        int calls = 0;
        f.games->setIndexSource([&] { return calls++ == 0 ? before : after; });
        Forms::Model::Result kept = f.model.runAction("game.saveLauncher");
        check(kept.ok, "checked and written against one snapshot: " + kept.message);

        // Once it's gone, every action says so instead of failing
        f.games->setIndexSource([&] { return after; });
        for (const char* action : { "game.saveLauncher", "game.useSystemLauncher", "game.saveMetadata", "game.addFavourite",
                                    "game.saveStory", "game.clearMedia" }) {
            Forms::Model::Result gone = f.model.runAction(action);
            check(!gone.ok && gone.message == "This game isn't in the library any more.", std::string(action) + ": " + gone.message);
        }
        checkEqual(f.model.get("game:isFound"), "no", "the screen says it's gone");

        // Typed before the library is read: kept until it is
        f.games->setIndexSource([] { return std::shared_ptr<const Library::Index>(); });
        f.open("game-metadata", gameParams("Arcade", "b"));
        f.model.set("game:meta:year", "1999");
        f.games->setIndexSource(nullptr);
        checkEqual(f.model.get("game:meta:year"), "1999", "kept");
        checkEqual(f.model.get("game:isMetadataDirty"), "yes", "and unsaved");
    });

    run("games: story text", [] {
        using Manager::GameProvider;
        checkEqual(GameProvider::storyToText("Line one\r\nLine two\r\n"), "Line one\nLine two", "lines, without their endings");
        checkEqual(GameProvider::textToStory("One\nTwo\nThree", "a\r\nb\r\n"), "One\r\nTwo\r\nThree\r\n", "file's CRLF kept");
        checkEqual(GameProvider::textToStory("One\nTwo", "a\nb"), "One\nTwo", "no final break kept");
        for (const std::string& text : { std::string("a\n\nb\n"), std::string("a\r\n  indented\r\n\r\nlast"),
                                         std::string("trailing space \nmixed\r\nends\n\n"), std::string("Pac-Man | Namco\n  1980\n") }) {
            checkEqual(GameProvider::textToStory(GameProvider::storyToText(text), text), text,
                       "unchanged text survives a round trip byte for byte");
        }
        checkEqual(GameProvider::textToStory("a\n  indented\n\nchanged", "a\r\n  indented\r\n\r\nlast"),
                   "a\r\n  indented\r\n\r\nchanged", "one line changed, the others as they were");
        checkEqual(GameProvider::textToStory("a\nnew\nb", "a\r\nb\r\n"), "a\r\nnew\r\nb\r\n", "a new line gets the file's ending");

        std::string root = makeInstall("games-story");
        Fixture f(root);
        Manager::Screen screen{ "game-description", gameParams("Arcade", "a") };
        f.open(screen.form, screen.params);
        checkEqual(f.model.get("game:story:text"), "Line one\nLine two", "current text, one line each");
        f.model.set("game:story:text", "New one\nNew two");
        check(!f.model.leaving(screen).empty(), "unsaved text warns");
        Forms::Model::Result saved = f.model.runAction("game.saveStory");
        check(saved.ok, "saved: " + saved.message);
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/story/a.txt"), "New one\r\nNew two\r\n", "written");
        checkEqual(f.model.get("game:story:hasOriginal"), "yes", "original kept");
        check(f.model.runAction("game.restoreStory").ok, "put back");
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/story/a.txt"), "Line one\r\nLine two\r\n", "original text");
        checkEqual(f.model.get("game:story:text"), "Line one\nLine two", "field shows it again");

        // A game without a story gets its own file
        f.open("game-description", gameParams("Arcade", "b"));
        f.model.set("game:story:text", "Beta's story");
        check(f.model.runAction("game.saveStory").ok, "new story");
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/story/b.txt"),
                   std::string("Beta's story") + Library::defaultNewline(), "new file");
        Forms::Model::Result open = f.model.runAction("game.replaceStory");
        check(open.ok && open.open == "game-media-slot" && open.params["type"] == "story" && open.keepParams, "replace opens the slot");

        // A story with a | of its own is edited like any other (the one-line field used | between paragraphs)
        std::string piped = root + "/collections/Arcade/medium_artwork/story/c.txt";
        Test::writeFile(piped, "Pac-Man | Namco\n  1980\n");
        f.open("game-description", gameParams("Arcade", "c"));
        checkEqual(f.model.get("game:story:canEdit"), "yes", "editable");
        checkEqual(f.model.get("game:story:text"), "Pac-Man | Namco\n  1980", "shown as it is");
        f.model.set("game:story:text", "Pac-Man | Namco\n  1980\nmore");
        Forms::Model::Result pipedSaved = f.model.runAction("game.saveStory");
        check(pipedSaved.ok, "saved: " + pipedSaved.message);
        checkEqual(Test::readFile(piped), "Pac-Man | Namco\n  1980\nmore\n", "unchanged lines kept byte for byte");

        // A new line typed at the end is an empty last line; an emptied story is Clear's job
        checkEqual(GameProvider::textToStory("Line one\nLine two\n", "Line one\r\nLine two\r\n"), "Line one\r\nLine two\r\n\r\n",
                   "a trailing new line adds an empty line, in the file's style");
        checkEqual(GameProvider::textToStory("", "a\nb\n"), "\n", "empty text is one empty line (Save refuses it)");
        f.model.set("game:story:text", " \n\n ");
        check(f.model.actionProblem("game.saveStory").find("Clear") != std::string::npos, "save refuses an emptied story");
        check(!f.model.runAction("game.saveStory").ok, "not saved");
        checkEqual(Test::readFile(piped), "Pac-Man | Namco\n  1980\nmore\n", "the file is as it was");
        f.model.runAction("game.resetStory");

        // The form edits it as a multi-line field
        std::string error;
        auto form = Forms::parse(Forms::builtinForm("game-description"), error);
        bool multiline = false;
        for (const Forms::Field& field : form ? form->fields : std::vector<Forms::Field>()) {
            multiline |= field.bind == "game:story:text" && field.multiline && field.rows > 1;
        }
        check(multiline, "the story is a multi-line field");

        // The library refuses text in an image slot, and empty text
        Library::MediaSlot logo;
        logo.type = "logo";
        logo.kind = Library::MediaKind::Image;
        logo.directory = root;
        logo.basename = "x";
        check(!f.editor.writeMediaText(logo, "text").ok, "not a text slot");
        Library::MediaSlot story;
        story.type = "story";
        story.kind = Library::MediaKind::Text;
        story.directory = root + "/collections/Arcade/medium_artwork/story";
        story.basename = "d";
        check(!f.editor.writeMediaText(story, "  ").ok, "empty text refused");
        check(f.editor.writeMediaText(story, "C\n").ok && Test::readFile(story.directory + "/d.txt") == "C\n", "written as given");
        check(f.editor.backups().hasOriginal(story.directory + "/d.txt"), "a new file is recorded, so a reset removes it");
    });

    run("games: hide a game and show it again", [] {
        std::string root = makeInstall("games-hide");
        Test::writeFile(root + "/collections/Arcade/roms/Dk.zip", "x");
        Fixture f(root);
        std::string exclude = root + "/collections/Arcade/exclude.txt";

        f.open("game", gameParams("Arcade", "Dk"));
        checkEqual(f.model.get("game:isHidden"), "no", "shown");
        checkEqual(f.model.actionProblem("game.unhide"), "It isn't hidden.", "nothing to show again");
        checkEqual(f.model.actionProblem("game.hide"), "", "can be hidden");
        Forms::Model::Result hid = f.model.runAction("game.hide");
        check(hid.ok, "hidden: " + hid.message);
        checkEqual(Test::readFile(exclude), std::string("Dk") + Library::defaultNewline(), "exclude.txt names its file, case as on disk");
        check(f.editor.backups().hasOriginal(exclude), "the original (none) is recorded");

        // Still findable: the game screen and the search
        f.settle();
        checkEqual(f.model.get("game:isFound"), "no", "not in RetroFE's list");
        checkEqual(f.model.get("game:isHidden"), "yes", "but hidden");
        checkEqual(f.model.get("game:notFound"), "", "not reported as gone");
        check(f.model.get("game:hiddenNote").find("exclude.txt") != std::string::npos, "says which list hides it");
        checkEqual(f.model.get("game:system"), "Arcade", "its system");
        checkEqual(f.model.get("game:file"), "Dk.zip", "its file");
        checkEqual(f.model.actionProblem("game.launch"), "It's hidden: show this game again first.", "can't be started");
        checkEqual(f.model.actionProblem("game.hide"), "It's hidden already.", "nor hidden again");
        checkEqual(f.model.get("game:isKnown"), "yes", "its metadata, media and story links stay");

        f.open("games", {});
        check(rowIds(f.model.rows("game:search", 100)).size() == 5, "the search leaves it out");
        f.model.set("game:onlyHidden", "yes");
        Forms::Rows hidden = f.model.rows("game:search", 100);
        check(rowIds(hidden) == std::vector<std::string>{ "Dk" }, "only hidden games: it");
        if (hidden.items.size() == 1) {
            checkEqual(hidden.items[0].params["collection"], "Arcade", "opens its own system's game");
        }
        f.model.set("game:filter", "Consoles");
        check(f.model.rows("game:search", 100).items.empty(), "not under Consoles");
        f.model.set("game:filter", "Main");
        check(rowIds(f.model.rows("game:search", 100)) == std::vector<std::string>{ "Dk" }, "a menu covers it");
        f.model.set("game:filter", "All");
        check(rowIds(f.model.rows("game:search", 100)) == std::vector<std::string>{ "Dk" }, "a merged collection covers its sources");
        f.model.set("game:query", "zzz");
        check(f.model.rows("game:search", 100).items.empty(), "the text filters them too");

        f.open("game", gameParams("Arcade", "Dk"));
        Forms::Model::Result shown = f.model.runAction("game.unhide");
        check(shown.ok, "shown again: " + shown.message);
        checkEqual(Test::readFile(exclude), "", "out of exclude.txt");
        f.settle();
        checkEqual(f.model.get("game:isFound"), "yes", "listed again");

        // The editor's checks
        auto idx = f.scan();
        check(!f.editor.setGameHidden(*idx, "Arcade", "nope", true).ok, "an unknown game can't be hidden");
        check(!f.editor.setGameHidden(*idx, "Main", "a", true).ok, "only a system's games");
        check(!f.editor.setGameHidden(*idx, "Arcade", "a", false).ok, "a game that isn't hidden can't be shown again");

        // list.excludeFile (LB12) is the list when it's set
        Test::writeFile(root + "/collections/Arcade/settings.conf", "list.extensions = zip\nlauncher = mame\nlist.excludeFile = lists/arcade.txt\n");
        Test::writeFile(root + "/lists/arcade.txt", "# hidden here\r\nc\r\n");
        f.indexer.rebuild();
        f.settle();
        f.open("game", gameParams("Arcade", "c"));
        checkEqual(f.model.get("game:isHidden"), "yes", "hidden by list.excludeFile's list");
        f.open("game", gameParams("Arcade", "b"));
        check(f.model.runAction("game.hide").ok, "hidden");
        checkEqual(Test::readFile(root + "/lists/arcade.txt"), "# hidden here\r\nc\r\nb\r\n", "written to that list, in its style");
        checkEqual(Test::readFile(exclude), "", "exclude.txt untouched");
    });

    run("games: a hidden game keeps its title and metadata", [] {
        std::string root = makeInstall("games-hide-title");
        Fixture f(root);
        f.open("game", gameParams("Arcade", "b"));
        check(f.model.runAction("game.hide").ok, "hidden");
        f.settle();
        checkEqual(f.model.get("game:isHidden"), "yes", "hidden");
        checkEqual(f.model.get("game:title"), "Beta", "the screen is titled by its metadata, not its file");
        checkEqual(f.model.get("game:year"), "1981", "its year");
        checkEqual(f.model.get("game:file"), "b.zip", "its file");

        f.open("games", {});
        f.model.set("game:query", "beta");
        check(f.model.rows("game:search", 100).items.empty(), "not in the normal search");
        f.model.set("game:onlyHidden", "yes");
        Forms::Rows hidden = f.model.rows("game:search", 100);
        check(rowIds(hidden) == std::vector<std::string>{ "b" }, "found by its title among the hidden games");
        if (hidden.items.size() == 1) {
            checkEqual(hidden.items[0].label, "Beta", "listed by its title");
            check(hidden.items[0].detail.find("b.zip") != std::string::npos, "the file in the detail: " + hidden.items[0].detail);
        }
        checkEqual(f.model.get("game:filterSummary"), "Hidden games", "the Filters link says so");
        checkEqual(f.model.get("game:filterCount"), "1 game matching \"beta\"", "and how many match");
        f.model.set("game:onlyHidden", "no");
        f.model.set("game:query", "");

        // Its metadata and media can be changed while it's hidden
        Manager::Screen metadata{ "game-metadata", gameParams("Arcade", "b") };
        f.open(metadata.form, metadata.params);
        checkEqual(f.model.get("game:isKnown"), "yes", "the screen works");
        checkEqual(f.model.get("game:notFound"), "", "not reported as gone");
        checkEqual(f.model.get("game:meta:description"), "Beta", "its title in the field");
        f.model.set("game:meta:year", "1990");
        Forms::Model::Result saved = f.model.runAction("game.saveMetadata");
        check(saved.ok, "saved while hidden: " + saved.message);
        check(Test::readFile(Library::MetadataOverrides::fileFor(root, "Arcade")).find("<year>1990</year>") != std::string::npos,
              "in the override layer");
        checkEqual(f.model.actionProblem("game.saveLauncher"), "It's hidden: show this game again first.",
                   "the launcher needs it listed");

        f.open("game-media-slot", { { "collection", "Arcade" }, { "id", "b" }, { "type", "logo" } });
        checkEqual(f.model.get("game:slot:status"), "Missing", "its logo slot is looked up");
        f.model.set("game:slot:newFile", root + "/import/new.png");
        Forms::Model::Result replaced = f.model.runAction("game.replaceMedia");
        check(replaced.ok, "a logo given while hidden: " + replaced.message);
        check(fs::exists(root + "/collections/Arcade/medium_artwork/logo/b.png"), "written under its name");
        f.open("game-media", gameParams("Arcade", "b"));
        std::vector<std::string> shownNow = rowIds(f.model.rows("game:mediaShown", 100));
        check(std::find(shownNow.begin(), shownNow.end(), "logo") != shownNow.end(), "the logo shows now");
    });

    run("games: hiding a folder game (emuarc) and a listed game with no file", [] {
        std::string root = makeInstall("games-hide-more");
        // Saturn: list.emuarc names each game after its folder; every file of it is a line
        Test::writeFile(root + "/collections/Saturn/settings.conf", "list.extensions = cue\nlauncher = mame\nlist.emuarc = true\n");
        Test::writeFile(root + "/collections/Saturn/roms/Panzer/Disc 1.cue", "x");
        Test::writeFile(root + "/collections/Saturn/roms/Panzer/Disc 2.cue", "x");
        // NES: include.txt with includeMissingItems lists "ghost", which has no file
        Test::writeFile(root + "/collections/NES/settings.conf", "list.extensions = nes\nlauncher = mame\nlist.includeMissingItems = true\n");
        Test::writeFile(root + "/collections/NES/include.txt", "mario\nghost\n");
        Fixture f(root);

        f.open("game", gameParams("Saturn", "Panzer"));
        checkEqual(f.model.get("game:isFound"), "yes", "the folder is a game");
        check(f.model.runAction("game.hide").ok, "hidden");
        std::string saturnExclude = Test::readFile(root + "/collections/Saturn/exclude.txt");
        check(saturnExclude.find("Disc 1") != std::string::npos && saturnExclude.find("Disc 2") != std::string::npos,
              "both files' names: " + saturnExclude);
        f.settle();
        checkEqual(f.model.get("game:isHidden"), "yes", "hidden once, as one game");
        check(f.model.runAction("game.unhide").ok, "shown again");
        checkEqual(Test::readFile(root + "/collections/Saturn/exclude.txt"), "", "both lines gone");
        f.settle();
        checkEqual(f.model.get("game:isFound"), "yes", "listed again");

        f.open("game", gameParams("NES", "ghost"));
        checkEqual(f.model.get("game:isFound"), "yes", "listed without a file");
        check(f.model.runAction("game.hide").ok, "hidden");
        checkEqual(Test::readFile(root + "/collections/NES/exclude.txt"), std::string("ghost") + Library::defaultNewline(), "by its name");
        f.settle();
        checkEqual(f.model.get("game:isHidden"), "yes", "found as a hidden game");
        checkEqual(f.model.get("game:file"), "", "with no file");
        f.open("games", {});
        f.model.set("game:onlyHidden", "yes");
        check(rowIds(f.model.rows("game:search", 100)) == std::vector<std::string>{ "ghost" }, "in the hidden games");
        f.model.set("game:onlyHidden", "no");
        f.open("game", gameParams("NES", "ghost"));
        check(f.model.runAction("game.unhide").ok, "shown again");
        f.settle();
        checkEqual(f.model.get("game:isFound"), "yes", "listed again");

        // A file include.txt doesn't name isn't hidden by the exclude list (it wouldn't show anyway)
        Test::writeFile(root + "/collections/NES/exclude.txt", "zelda\n");
        f.indexer.rebuild();
        f.settle();
        f.open("game", gameParams("NES", "zelda"));
        checkEqual(f.model.get("game:isHidden"), "no", "not a hidden game");
    });

    run("games: the launch request between Manage and RetroFE", [] {
        using Manager::LaunchQueue;
        using Take = LaunchQueue::Take;
        LaunchQueue::Request request;
        auto closes = [](bool result) { return [result](bool) { return result; }; };

        check(LaunchQueue::problem(true, false).empty() && LaunchQueue::problem(false, true).empty()
              && !LaunchQueue::problem(true, true).empty(), "refused only when a reload is due and the Catalog is busy");

        // Not a launch: handled as usual
        LaunchQueue q;
        check(!q.handle(true, closes(true)), "no request: an ordinary result");

        // The manager closes: RetroFE starts it once it's idle and nothing's due
        q.want({ "Arcade", "a" });
        check(q.handle(true, closes(true)) && q.hasPending(), "pending once closed");
        check(q.take(true, false, false, request) == Take::None, "not while the manager is open");
        check(q.take(false, true, false, request) == Take::None && q.hasPending(), "a reload first");
        check(q.take(false, false, false, request) == Take::Start && request.game == "a" && !q.hasPending(), "then it starts");
        check(q.take(false, false, false, request) == Take::None, "once");

        // The title rides along for "Back from <title>", and isn't part of what the request is
        q.want({ "Arcade", "a", "Alpha" });
        check(q.handle(true, closes(true)) && q.take(false, false, false, request) == Take::Start
              && request.title == "Alpha", "the title comes back with the request");
        check(LaunchQueue::Request{ "Arcade", "a", "Alpha" } == LaunchQueue::Request{ "Arcade", "a", "" }, "same game, same request");

        // A failed action asks nothing
        q.want({ "Arcade", "a" });
        check(!q.handle(false, closes(true)) && !q.hasPending(), "a failed action: no launch");

        // A screen with unsaved changes stops the close; the same request again leaves them
        std::vector<bool> discards;
        auto refuseFirst = [&](bool discard) { discards.push_back(discard); return discard; };
        q.want({ "Arcade", "a" });
        check(q.handle(true, refuseFirst) && !q.hasPending(), "refused: nothing pending");
        q.want({ "Arcade", "a" });
        check(q.handle(true, refuseFirst) && q.hasPending(), "asked again: it closes");
        check(discards == std::vector<bool>{ false, true }, "the second close leaves the changes");
        q.take(false, false, false, request);
        q.want({ "Arcade", "a" });
        q.handle(true, refuseFirst);
        q.screenChanged();
        q.want({ "Arcade", "a" });
        discards.clear();
        q.handle(true, refuseFirst);
        check(discards == std::vector<bool>{ false }, "after another screen it warns again");

        // A reload that can't happen (the Catalog installs) drops it: no surprise launch later
        LaunchQueue busy;
        busy.want({ "Arcade", "a" });
        busy.handle(true, closes(true));
        check(busy.take(false, true, true, request) == Take::Dropped && !busy.hasPending(), "dropped");
        check(busy.take(false, false, false, request) == Take::None, "and never started");

        // Kept across the reload it waits for, dropped by any other shutdown
        LaunchQueue reload;
        reload.want({ "Arcade", "a" });
        reload.handle(true, closes(true));
        reload.reloading();
        reload.shutdown();
        check(reload.hasPending(), "the reload keeps it");
        reload.shutdown();
        check(!reload.hasPending(), "a later shutdown doesn't");
        reload.want({ "Arcade", "a" });
        reload.handle(true, closes(true));
        reload.shutdown();
        check(!reload.hasPending(), "quitting drops it");
    });

    run("games: start a game from its screen", [] {
        std::string root = makeInstall("games-launch");
        Fixture f(root);
        f.open("game", gameParams("Arcade", "a"));
        check(f.model.actionProblem("game.launch").find("mame's program isn't there") != std::string::npos,
              "a missing program is said up front: " + f.model.actionProblem("game.launch"));
        Test::writeFile(root + "/mame.exe", "program");
        f.games->refresh();   // the manager opening again re-checks programs
        f.open("game", gameParams("Arcade", "a"));
        checkEqual(f.model.actionProblem("game.launch"), "", "can start");
        Forms::Model::Result started = f.model.runAction("game.launch");
        check(started.ok && started.message.find("Alpha") != std::string::npos, "starting: " + started.message);
        check(f.launched == std::vector<std::string>{ "Arcade/a" }, "asked RetroFE to start it from its own system");

        // RetroFE has to reload first and can't while the Catalog installs: refused, not delayed
        f.launchProblem = Manager::LaunchQueue::problem(true, true);
        checkEqual(f.model.actionProblem("game.launch"), f.launchProblem, "the Catalog is installing");
        check(!f.model.runAction("game.launch").ok && f.launched.size() == 1, "not asked");
        f.launchProblem.clear();

        // A game whose launcher isn't installed can't start
        Test::writeFile(root + "/collections/Arcade/launchers/c.conf", "nosuch\n");
        f.indexer.rebuild();
        f.settle();
        f.open("game", gameParams("Arcade", "c"));
        check(f.model.actionProblem("game.launch").find("It won't start") == 0, "says why: " + f.model.actionProblem("game.launch"));
        check(!f.model.runAction("game.launch").ok && f.launched.size() == 1, "not started");
    });

    run("games: play count and time played", [] {
        using Manager::GameProvider;
        std::string root = makeInstall("games-plays");
        Test::writeFile(root + "/settings.conf", "firstCollection = Main\nlastPlayedSkipCollection = THEMES\n");
        Test::writeFile(root + "/collections/playCount.txt", "a;1;1700000000\n_Arcade:a;3;1700000000\n_NES:mario;1;\n# comment\n");
        Test::writeFile(root + "/collections/timeSpent.txt", "_Arcade:a;4380\n_Arcade:b;oops\n");
        GameProvider::PlayStats a = GameProvider::readPlayStats(root, "Arcade", "a");
        check(a.count == 3 && a.lastPlayed == "1700000000" && a.seconds == 4380, "the _<system>:<game> line wins");
        check(GameProvider::readPlayStats(root, "NES", "mario").count == 1, "no time: still counted");
        check(GameProvider::readPlayStats(root, "Arcade", "b").count == 0, "never played");
        checkEqual(GameProvider::describeTime(42), "42 s", "seconds");
        checkEqual(GameProvider::describeTime(600), "10 min", "minutes");
        checkEqual(GameProvider::describeTime(4380), "1 h 13 min", "hours");

        Fixture f(root);
        f.open("game", gameParams("Arcade", "a"));
        check(f.model.get("game:played").rfind("3 times, last on 20", 0) == 0, "played: " + f.model.get("game:played"));
        checkEqual(f.model.get("game:timePlayed"), "1 h 13 min", "time played");
        f.open("game", gameParams("NES", "mario"));
        checkEqual(f.model.get("game:played"), "Once", "once, no date");
        f.open("game", gameParams("Arcade", "b"));
        checkEqual(f.model.get("game:played"), "Never", "never");
        checkEqual(f.model.get("game:timePlayed"), "", "no time");
    });

    run("games: a game with no plays says when RetroFE doesn't count them", [] {
        using Manager::GameProvider;
        checkEqual(GameProvider::playsNotCounted("", "10", "Arcade"),
                   "Not counted (set lastPlayedSkipCollection to count plays)", "not set: upstream counts nothing");
        checkEqual(GameProvider::playsNotCounted("  ", "10", "Arcade"),
                   "Not counted (set lastPlayedSkipCollection to count plays)", "blank");
        checkEqual(GameProvider::playsNotCounted("THEMES", "10", "Arcade"), "", "set, another collection: counted");
        checkEqual(GameProvider::playsNotCounted("THEMES, Arcade", "10", "Arcade"),
                   "Not counted (lastPlayedSkipCollection skips Arcade)", "its system is skipped");
        checkEqual(GameProvider::playsNotCounted("THEMES", "0", "Arcade"), "Not counted (lastplayedSize is 0)", "size 0");
        checkEqual(GameProvider::playsNotCounted("THEMES", "", "Arcade"), "", "size unset: the default 10");

        std::string root = makeInstall("games-plays-off");   // settings.conf without lastPlayedSkipCollection
        Test::writeFile(root + "/collections/playCount.txt", "_Arcade:a;2;1700000000\n");
        Fixture f(root);
        f.open("game", gameParams("Arcade", "b"));
        checkEqual(f.model.get("game:played"), "Not counted (set lastPlayedSkipCollection to count plays)", "the game screen");
        f.open("game", gameParams("Arcade", "a"));
        check(f.model.get("game:played").rfind("2 times", 0) == 0, "plays counted before still show: " + f.model.get("game:played"));
    });

    run("games: the Filters link sums up the search's filters", [] {
        std::string root = makeInstall("games-filters");
        Fixture f(root);
        f.open("games", {});
        checkEqual(f.model.get("game:filterSummary"), "All games", "no filter");
        checkEqual(f.model.get("game:filterCount"), "5 games", "every game");
        f.open("games-filters", {});
        f.model.set("game:filter", "Arcade");
        f.model.set("game:onlyNoMetadata", "yes");
        checkEqual(f.model.get("game:filterSummary"), "Arcade, without metadata", "a system, without metadata");
        checkEqual(f.model.get("game:filterCount"), "1 game", "only c");
        f.open("games", {});
        check(rowIds(f.model.rows("game:search", 100)) == std::vector<std::string>{ "c" }, "the search uses them");
        // The form: results straight after Search, the filters behind one link
        std::string error;
        auto form = Forms::parse(Forms::builtinForm("games"), error);
        bool ordered = form && form->fields.size() >= 4 && form->fields[1].bind == "game:query"
            && form->fields[2].type == Forms::FieldType::Link && form->fields[2].open == "games-filters"
            && form->fields[3].type == Forms::FieldType::Results;
        check(ordered, "Search, Filters, results");
    });

    run("games: genre, players and rating are chosen from the system's values", [] {
        std::string root = makeInstall("games-choices");
        Test::writeFile(root + "/meta/hyperlist/Arcade.xml",
                        "<?xml version=\"1.0\"?>\n<menu>\n"
                        "<game name=\"a\"><description>Alpha</description><genre>Maze</genre><players>2</players></game>\n"
                        "<game name=\"b\"><description>Beta</description><genre>Shooter</genre><players>10</players></game>\n"
                        "<game name=\"c\"><description>Gamma</description><genre>Maze</genre><players>1</players></game>\n"
                        "</menu>\n");
        {
            // meta.db again, from this list
            fs::remove(root + "/meta.db");
            Configuration::absolutePath = root;
            Configuration config;
            DB db(root + "/meta.db");
            db.initialize();
            MetadataDatabase meta(db, config);
            meta.initialize();
        }
        Fixture f(root);
        f.open("game-metadata", gameParams("Arcade", "a"));
        auto genres = f.model.options("game:metaChoices:genre");
        check(genres.size() == 4 && genres[0].first.empty() && genres[2].first == "Maze" && genres[3].first == "Shooter"
              && genres[1].second == "Other...", "None, Other..., each genre once");
        auto players = f.model.options("game:metaChoices:players");
        check(players.size() == 5 && players[2].first == "1" && players[3].first == "2" && players[4].first == "10",
              "numbers in order");
        checkEqual(f.model.get("game:metaPick:genre"), "Maze", "the game's genre is picked");
        f.model.set("game:metaPick:genre", "Shooter");
        checkEqual(f.model.get("game:meta:genre"), "Shooter", "picking edits the field");
        checkEqual(f.model.get("game:metaStatus:genre"), "Not saved yet.", "not saved yet");

        // Other... shows a text field to type one the system doesn't have
        checkEqual(f.model.get("game:metaTyping:genre"), "no", "no text field");
        f.model.set("game:metaPick:genre", genres[1].first);
        checkEqual(f.model.get("game:metaTyping:genre"), "yes", "Other... types instead");
        f.model.set("game:meta:genre", "Puzzle");
        auto now = f.model.options("game:metaChoices:genre");
        check(hasOption(now, "Puzzle"), "the typed genre is offered too");
        check(f.model.runAction("game.saveMetadata").ok, "saved");
        checkEqual(f.model.get("game:metaTyping:genre"), "no", "the choice shows it once saved");
        checkEqual(f.model.get("game:metaPick:genre"), "Puzzle", "picked");
        checkEqual(f.model.get("game:metadataFile"), "manage/metadata/Arcade.xml", "Saved in");
    });

    run("games: the favourite toggle, and a new playlist from the game", [] {
        std::string root = makeInstall("games-favourite-toggle");
        Fixture f(root);
        f.open("game-playlists", gameParams("NES", "mario"));
        checkEqual(f.model.get("game:favouriteToggle"), "no", "not a favourite");
        f.model.set("game:favouriteToggle", "yes");
        check(Test::readFile(root + "/collections/NES/playlists/favorites.txt").find("mario") != std::string::npos,
              "switching it on adds it straight away");
        check(f.model.get("game:favouriteStatus").find("Added mario") == 0, "and says so: " + f.model.get("game:favouriteStatus"));
        f.settle();
        checkEqual(f.model.get("game:favouriteToggle"), "yes", "on");
        f.model.set("game:favouriteToggle", "no");
        f.settle();
        checkEqual(f.model.get("game:favouriteToggle"), "no", "off again");
        check(Test::readFile(root + "/collections/NES/playlists/favorites.txt").find("mario") == std::string::npos, "removed");

        // NES has no playlists: a link to make one on its Playlists screen
        f.open("game-playlists", gameParams("NES", "zelda"));
        checkEqual(f.model.get("game:hasPlaylists"), "no", "no playlists");
        checkEqual(f.model.get("game:canAddToPlaylist"), "no", "nothing to add to");
        Forms::Rows add = f.model.rows("game:newPlaylist", 10);
        check(add.items.size() == 1 && add.items[0].id == "NES" && add.items[0].link, "New playlist... opens NES's playlists");
    });
}

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
        }

        // An edit asks for a new index; wait for it like the screens do
        void settle()
        {
            indexer.wait(10000);
            games->update(0);
        }

        Configuration config;
        Library::Indexer indexer;
        Library::Editor editor;
        ManagerModel model;
        Manager::GameProvider* games = nullptr;
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
        check(f.model.get("game:unresolvedNote").find("ghost") != std::string::npos, "unresolved name warned about");
        check(!f.model.get("game:launcherProblem").empty(), "won't start");
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
        checkEqual(f.model.get("game:slot:hasOriginal"), "yes", "original kept");
        checkEqual(f.model.get("game:slot:newFile"), "", "file field cleared");
        check(f.model.runAction("game.restoreMedia").ok, "put back");
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/logo/a.png"), "logo", "original back");

        // A file of the wrong kind is refused
        f.model.set("game:slot:newFile", root + "/import/new.txt");
        check(!f.model.runAction("game.replaceMedia").ok, "a text file isn't a logo");

        check(f.model.runAction("game.clearMedia").ok, "own file cleared");
        check(!fs::exists(root + "/collections/Arcade/medium_artwork/logo/a.png"), "gone (kept in backups)");
        checkEqual(f.model.get("game:slot:status"), "Missing", "slot looked up again");
    });

    run("games: favourites, playlists and merged collections", [] {
        std::string root = makeInstall("games-playlists");
        Fixture f(root);
        f.open("game-playlists", gameParams("Arcade", "b"));
        checkEqual(f.model.get("game:isFavourite"), "no", "not a favourite");
        Forms::Model::Result fav = f.model.runAction("game.addFavourite");
        check(fav.ok, "favourite: " + fav.message);
        check(Test::readFile(root + "/collections/Arcade/playlists/favorites.txt").find("b") != std::string::npos, "favorites.txt");
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
        check(!f.model.runAction("game.removeFromMerged").ok, "refused for an empty .sub");

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

    run("games: story text", [] {
        checkEqual(Manager::GameProvider::storyToLine("Line one\r\nLine two\r\n"), "Line one | Line two", "lines joined");
        checkEqual(Manager::GameProvider::lineToStory("One|Two  |  Three", "a\r\nb\r\n"), "One\r\nTwo\r\nThree\r\n", "file's CRLF kept");
        checkEqual(Manager::GameProvider::lineToStory("One | Two", "a\nb"), "One\nTwo", "no final break kept");
        checkEqual(Manager::GameProvider::lineToStory(Manager::GameProvider::storyToLine("a\n\nb\n"), "a\n\nb\n"), "a\n\nb\n",
                   "blank paragraphs survive a round trip");

        std::string root = makeInstall("games-story");
        Fixture f(root);
        Manager::Screen screen{ "game-description", gameParams("Arcade", "a") };
        f.open(screen.form, screen.params);
        checkEqual(f.model.get("game:story:text"), "Line one | Line two", "current text");
        f.model.set("game:story:text", "New one | New two");
        check(!f.model.leaving(screen).empty(), "unsaved text warns");
        Forms::Model::Result saved = f.model.runAction("game.saveStory");
        check(saved.ok, "saved: " + saved.message);
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/story/a.txt"), "New one\r\nNew two\r\n", "written");
        checkEqual(f.model.get("game:story:hasOriginal"), "yes", "original kept");
        check(f.model.runAction("game.restoreStory").ok, "put back");
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/story/a.txt"), "Line one\r\nLine two\r\n", "original text");
        checkEqual(f.model.get("game:story:text"), "Line one | Line two", "field shows it again");

        // A game without a story gets its own file
        f.open("game-description", gameParams("Arcade", "b"));
        f.model.set("game:story:text", "Beta's story");
        check(f.model.runAction("game.saveStory").ok, "new story");
        checkEqual(Test::readFile(root + "/collections/Arcade/medium_artwork/story/b.txt"),
                   std::string("Beta's story") + Library::defaultNewline(), "new file");
        Forms::Model::Result open = f.model.runAction("game.replaceStory");
        check(open.ok && open.open == "game-media-slot" && open.params["type"] == "story" && open.keepParams, "replace opens the slot");

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
        story.basename = "c";
        check(!f.editor.writeMediaText(story, "  ").ok, "empty text refused");
        check(f.editor.writeMediaText(story, "C\n").ok && Test::readFile(story.directory + "/c.txt") == "C\n", "written as given");
        check(f.editor.backups().hasOriginal(story.directory + "/c.txt"), "a new file is recorded, so a reset removes it");
    });
}

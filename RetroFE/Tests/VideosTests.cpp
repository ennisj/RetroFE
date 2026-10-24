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

// Video collections playing in RetroFE: the play switch
// (Collection/VideoCollections), the _videos and player pages
// (Graphics/LayoutPages, Builtin), and the Videos screens' choices.

#include "TestSupport.h"
#include "../Source/Builtin/Builtin.h"
#include "../Source/Collection/VideoCollections.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Forms/Form.h"
#include "../Source/Graphics/LayoutPages.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Systems/SystemProvider.h"
#include "../Source/Utility/Utils.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // Main (menu): Clips, Films
    //   Clips: a video collection from before "play" (launcher = mame, which is installed)
    //   Films: plays in RetroFE, .mp4 and .webm files, no launcher
    struct Fixture
    {
        std::string root;
        Configuration config;
        Library::Indexer indexer;
        Library::Editor editor;
        ManagerModel model;

        explicit Fixture(const std::string& name)
            : root(install(name))
            , indexer(options(root))
            , editor(root)
            , model(config)
        {
            Manager::Services services{ config };
            services.library = &indexer;
            services.editor = &editor;
            model.addProvider(std::make_unique<Manager::SystemProvider>(services, true));
            indexer.rebuild();
            indexer.wait(20000);
        }

        static std::string install(const std::string& name)
        {
            std::string root = makeRoot(name);
            Configuration::absolutePath = root;
            std::string os = Utils::getOSType();
            writeFile(root + "/settings.conf", "firstCollection = Main\n");
            writeFile(root + "/mame.exe", "x");
            writeFile(root + "/launchers." + os + "/mame.conf", "executable = " + Utils::combinePath(root, "mame.exe") + "\n");
            writeFile(root + "/collections/Main/menu.txt", "Clips\nFilms\n");
            writeFile(root + "/collections/Clips/settings.conf", "list.path = " + root + "/clips\nlist.extensions = mp4\nlauncher = mame\n");
            writeFile(root + "/collections/Clips/info.conf", "type = videos\n");
            writeFile(root + "/clips/intro.mp4", "x");
            writeFile(root + "/collections/Films/settings.conf", "list.path = " + root + "/films\nlist.extensions = mp4,webm\n");
            writeFile(root + "/collections/Films/info.conf", "type = videos\nplay = here\n");
            writeFile(root + "/films/one.mp4", "x");
            writeFile(root + "/films/two.webm", "x");
            return root;
        }

        static Library::ScanOptions options(const std::string& root)
        {
            Library::ScanOptions scan;
            scan.root = root;
            return scan;
        }

        void open(const std::string& form, const Forms::Params& params)
        {
            indexer.wait(20000);
            model.setScreen(form, params);
        }

        void rescan()
        {
            indexer.wait(20000);
            indexer.rebuild();
            indexer.wait(20000);
        }

        bool anyFinding(const std::string& part)
        {
            for (int i = 0; i < 12; i++) {
                if (contains(model.get("video:finding" + std::to_string(i)), part)) {
                    return true;
                }
            }
            return false;
        }
    };

    void runVideosTests()
    {
        run("videos: only play = here plays in RetroFE; older collections keep their launcher", [] {
            check(VideoCollections::isVideoType(" Videos "), "type videos, any case");
            check(!VideoCollections::isVideoType("video"), "not another word");
            check(VideoCollections::playsHere("videos", "here"), "here");
            check(VideoCollections::playsHere("videos", " HERE "), "any case and spacing");
            check(!VideoCollections::playsHere("videos", ""), "no play line: its launcher, as before");
            check(!VideoCollections::playsHere("videos", "launcher"), "launcher");
            check(!VideoCollections::playsHere("", "here"), "only for video collections");
            check(VideoCollections::isPlayValue("here") && VideoCollections::isPlayValue("Launcher"), "the two values");
            check(!VideoCollections::isPlayValue("") && !VideoCollections::isPlayValue("vlc"), "nothing else");
            check(VideoCollections::isPlayableExtension("mp4") && VideoCollections::isPlayableExtension("mkv"), "what VideoBuilder opens");
            check(!VideoCollections::isPlayableExtension("webm") && !VideoCollections::isPlayableExtension("mov"), "not others");
        });

        run("videos: the setting from RetroFE's configuration, and its own files as its video media", [] {
            std::string root = makeRoot("videos-config");
            Configuration::absolutePath = root;
            Configuration config;
            config.setProperty("collections.Films.type", std::string("videos"));
            config.setProperty("collections.Films.play", std::string("here"));
            config.setProperty("collections.Films.list.path", root + "/films;" + root + "/more");
            config.setProperty("collections.Clips.type", std::string("videos"));
            config.setProperty("collections.Clips.list.path", root + "/clips");
            config.setProperty("collections.Arcade.list.path", root + "/roms");
            check(VideoCollections::isVideoCollection(config, "Films") && VideoCollections::playsHere(config, "Films"), "Films plays here");
            check(VideoCollections::isVideoCollection(config, "Clips") && !VideoCollections::playsHere(config, "Clips"), "Clips: its launcher");
            check(!VideoCollections::isVideoCollection(config, "Arcade"), "a system isn't one");

            check(VideoCollections::useItemsAsVideoMedia(config, "Films"), "Films' videos are its video media");
            std::string media;
            config.getProperty("collections.Films.media.video", media);
            checkEqual(fs::path(media).lexically_normal().string(), fs::path(root + "/films").lexically_normal().string(),
                       "the first list.path folder");
            check(!VideoCollections::useItemsAsVideoMedia(config, "Films"), "set once");
            check(!VideoCollections::useItemsAsVideoMedia(config, "Arcade"), "not for a system");

            fs::create_directories(root + "/collections/Clips/medium_artwork/video");
            check(!VideoCollections::useItemsAsVideoMedia(config, "Clips"), "a collection with video media of its own keeps it");
            config.setProperty("collections.Other.type", std::string("videos"));
            config.setProperty("collections.Other.list.path", root + "/other");
            config.setProperty("collections.Other.media.video", root + "/previews");
            check(!VideoCollections::useItemsAsVideoMedia(config, "Other"), "nor one whose media.video is set");
        });

        run("videos: a list.path with %BASE_ITEM_PATH% or a relative one becomes an absolute media folder", [] {
            std::string root = makeRoot("videos-config-base");
            Configuration::absolutePath = root;
            Configuration config;
            config.setProperty("collections.Based.type", std::string("videos"));
            config.setProperty("collections.Based.list.path", std::string("%BASE_ITEM_PATH%/films"));
            config.setProperty("collections.Relative.type", std::string("videos"));
            config.setProperty("collections.Relative.list.path", std::string("videos/clips"));

            // getProperty expands %BASE_ITEM_PATH% (RetroFE fixes the base on its first call)
            std::string expanded;
            config.getProperty("collections.Based.list.path", expanded);
            check(VideoCollections::useItemsAsVideoMedia(config, "Based"), "set");
            std::string media;
            config.getProperty("collections.Based.media.video", media);
            check(media.find('%') == std::string::npos, "no variable left: " + media);
            check(fs::path(media).is_absolute(), "absolute: " + media);
            checkEqual(fs::path(media).lexically_normal().string(),
                       fs::path(Configuration::convertToAbsolutePath(root, expanded)).lexically_normal().string(),
                       "the expanded folder");

            check(VideoCollections::useItemsAsVideoMedia(config, "Relative"), "set for a relative path too");
            config.getProperty("collections.Relative.media.video", media);
            checkEqual(fs::path(media).lexically_normal().string(), fs::path(root + "/videos/clips").lexically_normal().string(),
                       "under RetroFE's folder");
        });

        run("videos: a video collection's pages come from _videos, after its own", [] {
            std::string layout = makeRoot("videos-layoutpages");
            fs::create_directories(Utils::combinePath(layout, "collections", "_menu", "layout"));
            checkEqual(LayoutPages::collectionFolder(layout, "Films", false, true), layout, "no _videos: the layout's pages");
            fs::create_directories(Utils::combinePath(layout, "collections", "_videos", "layout"));
            fs::create_directories(Utils::combinePath(layout, "collections", "Clips", "layout"));
            checkEqual(LayoutPages::collectionFolder(layout, "Films", false, true), Utils::combinePath(layout, "collections", "_videos", "layout"), "_videos");
            checkEqual(LayoutPages::collectionFolder(layout, "Films", true, true), Utils::combinePath(layout, "collections", "_videos", "layout"), "before _menu");
            checkEqual(LayoutPages::collectionFolder(layout, "Clips", false, true), Utils::combinePath(layout, "collections", "Clips", "layout"), "its own first");
            checkEqual(LayoutPages::collectionFolder(layout, "Arcade", false, false), layout, "not for a system");
        });

        run("videos: the player page follows the page shape, the collection's own first", [] {
            std::string layout = makeRoot("videos-player");
            std::string videos = Utils::combinePath(layout, "collections", "_videos", "layout");
            std::string own = Utils::combinePath(layout, "collections", "Films", "layout");
            checkEqual(LayoutPages::playerFile(layout, "Films", "layout"), "", "none: the built-in player");
            writeFile(Utils::combinePath(videos, "player.xml"), "<layout/>");
            checkEqual(LayoutPages::playerFile(layout, "Films", "layout 4x3"), Utils::combinePath(videos, "player.xml"), "_videos' player.xml");
            writeFile(Utils::combinePath(videos, "player 4x3.xml"), "<layout/>");
            checkEqual(LayoutPages::playerFile(layout, "Films", "layout 4x3"), Utils::combinePath(videos, "player 4x3.xml"), "the 4x3 one");
            checkEqual(LayoutPages::playerFile(layout, "Films", "layout"), Utils::combinePath(videos, "player.xml"), "16:9");
            writeFile(Utils::combinePath(own, "player.xml"), "<layout/>");
            checkEqual(LayoutPages::playerFile(layout, "Films", "layout 4x3"), Utils::combinePath(own, "player.xml"), "its own before _videos'");
            checkEqual(LayoutPages::playerFile(layout, "Clips", "random"), Utils::combinePath(videos, "player.xml"), "other page names: player.xml");
        });

        run("videos: the built-in layout has a video list and a player page", [] {
            std::string list = Builtin::layoutXml("videos") ? Builtin::layoutXml("videos") : "";
            std::string player = Builtin::layoutXml("player") ? Builtin::layoutXml("player") : "";
            check(contains(list, "<menu") && contains(list, "reloadableVideo type=\"video\""), "a list and the selected video");
            check(contains(player, "<menu") && contains(player, "jukebox=\"true\""), "the player follows a menu and can pause");
            check(contains(player, "type=\"isPaused\"") && contains(player, "type=\"current\""), "it shows pause and time");
        });

        run("videos: adding one plays it in RetroFE by default, with no player program", [] {
            Fixture f("videos-add-here");
            writeFile(f.root + "/music/song.mp4", "x");
            f.open("video-add", {});
            checkEqual(f.model.get("video:addPlay"), "here", "in RetroFE by default");
            checkEqual(f.model.get("video:addUsesLauncher"), "no", "no player to choose");
            checkEqual(f.model.get("video:addIsNewLauncher"), "no", "no program to choose");
            f.model.set("video:addName", "Music");
            f.model.set("video:addFolder", Utils::combinePath(f.root, "music"));
            f.model.set("video:addExtensions", "mp4,webm");
            check(contains(f.model.actionProblem("video.create"), "webm"), "a type RetroFE can't play is refused: "
                  + f.model.actionProblem("video.create"));
            f.model.set("video:addExtensions", "mp4,mkv");
            checkEqual(f.model.actionProblem("video.create"), "", "ready without a program");
            Forms::Model::Result created = f.model.runAction("video.create");
            check(created.ok && contains(created.message, "plays its videos itself"), "added: " + created.message);
            std::string nl = Library::defaultNewline();
            checkEqual(readFile(f.root + "/collections/Music/info.conf"), "type = videos" + nl + "play = here" + nl, "info.conf");
            check(!contains(readFile(f.root + "/collections/Music/settings.conf"), "launcher"), "no launcher line");
            check(!fs::exists(f.root + "/launchers." + Utils::getOSType() + "/VideoPlayer.conf"), "no player launcher made");
            f.rescan();
            f.open("videos", {});
            bool listed = false;
            for (const Forms::Row& row : f.model.rows("video:search", 50).items) {
                listed |= row.id == "Music" && contains(row.detail, "plays in RetroFE");
            }
            check(listed, "listed, saying it plays in RetroFE");
        });

        run("videos: Playback switches between RetroFE and the external player", [] {
            Fixture f("videos-playback");
            f.open("video", { { "id", "Clips" } });
            checkEqual(f.model.get("video:playsText"), "With an external player (its launcher)", "an older collection");
            checkEqual(f.model.get("video:usesLauncher"), "yes", "its launcher is shown");
            f.open("video-launcher", { { "id", "Clips" } });
            checkEqual(f.model.get("video:play"), "launcher", "the choice as it is");
            f.model.set("video:play", "here");
            checkEqual(f.model.get("video:usesLauncher"), "no", "the player hides while In RetroFE is chosen");
            Forms::Model::Result saved = f.model.runAction("video.saveLauncher");
            check(saved.ok && contains(saved.message, "itself"), "saved: " + saved.message);
            check(contains(readFile(f.root + "/collections/Clips/info.conf"), "play = here"), "written to info.conf");
            check(contains(readFile(f.root + "/collections/Clips/settings.conf"), "launcher = mame"), "its launcher is kept");
            f.rescan();
            f.open("video", { { "id", "Clips" } });
            checkEqual(f.model.get("video:launcherStatus"), "In RetroFE", "the details say so");

            f.open("video-launcher", { { "id", "Films" } });
            f.model.set("video:play", "launcher");
            check(contains(f.model.actionProblem("video.saveLauncher"), "Choose the video player"), "an external player needs one: "
                  + f.model.actionProblem("video.saveLauncher"));
            f.model.set("video:launcher", "mame");
            checkEqual(f.model.actionProblem("video.saveLauncher"), "", "with one it saves");
            saved = f.model.runAction("video.saveLauncher");
            check(saved.ok && contains(saved.message, "mame"), "saved: " + saved.message);
            check(contains(readFile(f.root + "/collections/Films/info.conf"), "play = launcher"), "play = launcher");
            check(contains(readFile(f.root + "/collections/Films/settings.conf"), "launcher = mame"), "and the launcher");
        });

        run("videos: the check knows a collection RetroFE plays needs no launcher", [] {
            Fixture f("videos-check");
            f.open("video-check", { { "id", "Films" } });
            check(!f.anyFinding("launcher"), "no launcher findings");
            check(f.anyFinding(".webm"), "but its .webm files won't play here");
            f.open("video", { { "id", "Films" } });
            check(contains(f.model.get("video:playsText"), ".webm"), "the details say which don't play");
        });

        run("videos: play is allow-listed with its two values", [] {
            std::string root = Fixture::install("videos-editor");
            Library::ScanOptions scan;
            scan.root = root;
            auto index = std::shared_ptr<const Library::Index>(Library::buildIndex(scan).release());
            check(Library::Editor::checkSetting(*index, "Films", "play", "here").empty(), "here");
            check(Library::Editor::checkSetting(*index, "Films", "play", "launcher").empty(), "launcher");
            check(!Library::Editor::checkSetting(*index, "Films", "play", "vlc").empty(), "nothing else");
        });
    }

    Test::Suite videos("videos", runVideosTests);
}

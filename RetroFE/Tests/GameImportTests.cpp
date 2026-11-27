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
#include "../Source/Manager/Systems/GameImport.h"
#include "../Source/Database/MetadataRefresh.h"
#include "../Source/Library/Properties.h"
#include <atomic>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;
using namespace Test;
using Manager::GameImport;

static Test::Suite imports("game imports", [] {
    run("metadata refresh respects Manage and lock", [] {
        using T = fs::file_time_type;
        auto old = T::min() + std::chrono::seconds(10);
        auto newer = old + std::chrono::seconds(1);
        check(MetadataRefresh::needed(true, 1, old, old, old, newer), "Manage overrides lock");
        check(!MetadataRefresh::needed(true, 0, old, newer, newer, old), "lock skips ordinary changes and empty database");
        check(MetadataRefresh::needed(false, 0, old, old, old, old), "empty database imports");
        check(MetadataRefresh::needed(false, 1, old, newer, old, old), "new executable imports");
        check(!MetadataRefresh::needed(false, 1, newer, old, old, old), "old executable does not repeatedly import");
    });
    run("metadata siblings, invalid types and trailing separator", [] {
        std::string root = makeRoot("import-metadata-siblings");
        fs::path source = fs::path(root) / "source";
        writeFile((source / "collections/S/roms/a.nes").string(), "game");
        writeFile((source / "collections/S/medium_artwork/logo/a.png").string(), "art");
        writeFile((source / "meta/hyperlist/S.xml").string(), "one");
        writeFile((source / "meta/hyperlist/S.extra.xml").string(), "two");
        GameImport::Request r;
        r.from = (source / "collections/S/roms").string() + "/";
        r.installRoot = fs::path(root) / "dest"; r.into = r.installRoot / "roms";
        r.extensions = { "nes" }; r.metadataType = "T"; r.copyMetadata = true; r.copyArtwork = true;
        r.mediaPath = [root](const std::string& type, bool) { return fs::path(root) / "media" / type; };
        std::atomic<bool> stop{ false };
        auto count = GameImport::run(r, false, stop);
        check(count.metadataFound && count.artworkFound, "trailing separator finds siblings");
        auto first = GameImport::run(r, true, stop);
        check(first.copied == 1 && first.artworkCopied == 1 && fs::exists(r.installRoot / "manage/metadata/.reimport"), "copy requests import");
        auto again = GameImport::run(r, true, stop);
        check(again.message.find("T.xml was already there") != std::string::npos && again.message.find("T.extra.xml was already there") != std::string::npos && again.message.find("kept your") == std::string::npos, "both siblings already present");
        writeFile((r.installRoot / "meta/hyperlist/T.xml").string(), "different size");
        again = GameImport::run(r, true, stop);
        check(again.message.find("kept your meta/hyperlist/T.xml") != std::string::npos, "same name different size kept");
        r.metadataType = "MAME 0.139";
        count = GameImport::run(r, false, stop);
        check(!count.metadataFound && count.message.find("cannot match") != std::string::npos, "invalid type hidden and explained");
        again = GameImport::run(r, true, stop);
        check(again.already == 1, "metadata never enters game counts");
    });
    run("an artwork name Windows can't narrow doesn't stop the games", [] {
        // U+014C isn't in the usual ANSI code pages: on Windows path::string() throws for it
        std::string root = makeRoot("import-art-unusable");
        writeFile(root + "/source/roms/a.nes", "game");
        writeFile(root + "/source/medium_artwork/logo/a.png", "art");
        fs::path odd = fs::path(root) / "source" / "medium_artwork" / "logo" / fs::u8path(u8"\u014Ckami.png");
        { std::ofstream out(odd, std::ios::binary); out << "x"; }
        check(fs::exists(odd), "the oddly named file exists");
        GameImport::Request r;
        r.from = root + "/source/roms"; r.into = root + "/dest/roms"; r.extensions = { "nes" }; r.copyArtwork = true;
        r.mediaPath = [root](const std::string& type, bool) { return fs::path(root) / "media" / type; };
        std::atomic<bool> stop{ false };
        auto count = GameImport::run(r, false, stop);
        checkEqual(count.found, size_t(1), "counted: " + count.message);
        check(count.artworkFound, "artwork found beside it");
        auto done = GameImport::run(r, true, stop);
        check(done.copied == 1 && done.artworkCopied == 1, "game and its art copied: " + done.message);
        // Unusable where the code page can't hold it, else simply art for another game
        check(done.artworkUnusable + done.artworkLeft == 1, "the odd file is left: " + done.message);
    });
    run("artwork failure preserves game result", [] {
        std::string root = makeRoot("import-art-failure");
        writeFile(root + "/source/roms/a.nes", "game");
        writeFile(root + "/source/medium_artwork/logo/a.png", "art");
        writeFile(root + "/blocked", "file");
        GameImport::Request r;
        r.from = root + "/source/roms"; r.into = root + "/dest/roms"; r.extensions = { "nes" }; r.copyArtwork = true;
        r.mediaPath = [root](const std::string&, bool) { return fs::path(root) / "blocked" / "logo"; };
        std::atomic<bool> stop{ false };
        auto done = GameImport::run(r, true, stop);
        check(done.copied == 1 && done.artworkConflicts == 1 && done.message.find("Artwork skipped") != std::string::npos, "game and failed art reported");
    });
    run("add games: copied message", [] {
        std::string root = makeRoot("import-message-copied");
        writeFile(root + "/source/a.md", "a");
        GameImport::Request r;
        r.from = root + "/source"; r.into = root + "/roms"; r.extensions = { "md" };
        r.destinationLabel = "collections\\Genesis\\roms";
        std::atomic<bool> stop{ false };
        checkEqual(GameImport::run(r, true, stop).message, "Copied 1 game into " + r.destinationLabel + ".", "singular copied with no zero counts");
        r.into = root + "/more-roms";
        writeFile(root + "/source/b.md", "b");
        checkEqual(GameImport::run(r, true, stop).message, "Copied 2 games into " + r.destinationLabel + ".", "plural copied");
    });
    run("add games: copied and already there message", [] {
        std::string root = makeRoot("import-message-already");
        writeFile(root + "/source/a.md", "a");
        writeFile(root + "/source/b.md", "b");
        writeFile(root + "/roms/a.md", "a");
        GameImport::Request r;
        r.from = root + "/source"; r.into = root + "/roms"; r.extensions = { "md" };
        std::atomic<bool> stop{ false };
        checkEqual(GameImport::run(r, true, stop).message, "Copied 1 game into " + r.into.string() + "; 1 was already there.", "singular already there");
        writeFile(root + "/source/c.md", "c");
        checkEqual(GameImport::run(r, true, stop).message, "Copied 1 game into " + r.into.string() + "; 2 were already there.", "plural already there");
    });
    run("add games: skipped and missing message with notes", [] {
        std::string root = makeRoot("import-message-notes");
        writeFile(root + "/source/a.cue", "FILE missing.bin BINARY\n");
        writeFile(root + "/source/b.cue", "new");
        writeFile(root + "/roms/b.cue", "existing");
        GameImport::Request r;
        r.from = root + "/source"; r.into = root + "/roms"; r.extensions = { "cue" };
        std::atomic<bool> stop{ false };
        r.destinationLabel = "collections\\CD\\roms";
        // The notes name files with the system's own separators, so only their names are checked
        std::string message = GameImport::run(r, true, stop).message;
        std::string summary = "Copied 1 game into collections\\CD\\roms; 1 has the name of a different file already there, so it was skipped; 1 file a game refers to is missing.";
        checkEqual(message.substr(0, summary.size()), summary, "singular skipped and missing");
        check(message.find(" Missing: ") != std::string::npos && message.find("missing.bin") != std::string::npos
            && message.find(" Kept ") != std::string::npos && message.find("b.cue") != std::string::npos, "notes name the files: " + message);
        r.into = root + "/plural-roms";
        writeFile(root + "/source/a.cue", "FILE missing.bin BINARY\nFILE absent.bin BINARY\n");
        writeFile(root + "/source/c.cue", "new");
        writeFile((r.into / "b.cue").string(), "existing");
        writeFile((r.into / "c.cue").string(), "existing");
        auto done = GameImport::run(r, true, stop);
        check(done.message.find("; 2 have the names of different files already there, so they were skipped; 2 files games refer to are missing.") != std::string::npos, "plural skipped and missing");
    });
    run("add games: stopped message", [] {
        std::string root = makeRoot("import-message-stopped");
        writeFile(root + "/source/a.md", "a");
        writeFile(root + "/source/b.md", "b");
        GameImport::Request r;
        r.from = root + "/source"; r.into = root + "/roms"; r.extensions = { "md" };
        std::atomic<bool> stop{ false };
        auto done = GameImport::run(r, true, stop, {}, [&](const std::string&) {
            if (fs::exists(r.into / "a.md")) stop = true;
        });
        checkEqual(done.message, "Stopped. Copied 1 game into " + r.into.string() + ".", "stopped after one finished game");
    });
    run("add games: no new games message", [] {
        std::string root = makeRoot("import-message-no-new");
        GameImport::Request r;
        r.from = root + "/source"; r.into = root + "/roms"; r.extensions = { "md" };
        std::atomic<bool> stop{ false };
        for (int i = 0; i < 12; ++i) {
            std::string name = std::to_string(i) + ".md";
            writeFile((r.from / name).string(), "a");
            writeFile((r.into / name).string(), "a");
            if (i == 0) checkEqual(GameImport::run(r, true, stop).message, "No new games: 1 is already in " + r.into.string() + ".", "one already there");
        }
        checkEqual(GameImport::run(r, true, stop).message, "No new games: all 12 are already in " + r.into.string() + ".", "all already there");
    });
    run("add games: count, defaults, collisions and hierarchy", [] {
        std::string root = makeRoot("import-count");
        writeFile(root + "/source/a.md", "abc");
        writeFile(root + "/source/upper.MD", "abc");
        writeFile(root + "/source/sub/b.md", "def");
        GameImport::Request r;
        r.from = root + "/source"; r.into = root + "/collections/Genesis/roms"; r.extensions = { "md" };
        std::atomic<bool> stop{ false };
        check(GameImport::run(r, false, stop).found == 2, "recursive count follows exact extension case, as Browse does");
        r.subfolders = false;
        check(GameImport::run(r, false, stop).found == 1, "top folder only");
        r.subfolders = true;
        auto done = GameImport::run(r, true, stop);
        check(done.copied == 2 && fs::exists(r.into / "b.md"), "default destination created and flattened");
        check(readFile((r.from / "a.md").string()) == "abc", "source unchanged");
        done = GameImport::run(r, true, stop);
        check(done.already == 2 && done.copied == 0, "same name and size skipped");
        writeFile((r.into / "a.md").string(), "keep me");
        done = GameImport::run(r, true, stop);
        check(done.conflicts == 1 && done.message.find("a.md") != std::string::npos, "conflict counted and named");
        checkEqual(readFile((r.into / "a.md").string()), "keep me", "existing file kept");
        r.into = root + "/race";
        done = GameImport::run(r, true, stop, {}, [&](const std::string&) {
            if (fs::exists(r.into / "a.md.retrofe-part")) writeFile((r.into / "a.md").string(), "user file");
        });
        checkEqual(readFile((r.into / "a.md").string()), "user file", "a file created mid-copy is kept");
        check(done.conflicts == 1 && !fs::exists(r.into / "a.md.retrofe-part"), "publication conflict counted and partial removed");
        r.into = root + "/hierarchy"; r.hierarchy = true;
        done = GameImport::run(r, true, stop);
        check(done.copied == 2 && fs::exists(r.into / "sub/b.md"), "hierarchy preserved when Browse recurses");
    });
    run("add games: disc and playlist references, free space and Stop", [] {
        std::string root = makeRoot("import-companions");
        writeFile(root + "/source/a.cue", "FILE \"tracks/audio.bin\" BINARY\nFILE \"missing.bin\" BINARY\n");
        writeFile(root + "/source/tracks/audio.bin", "audio");
        writeFile(root + "/source/b.gdi", "1\n1 0 4 2352 \"disc.raw\" 0\n");
        writeFile(root + "/source/disc.raw", "disc");
        writeFile(root + "/source/0.m3u", "# playlist\n./a.cue\nb.gdi\n");
        GameImport::Request r;
        r.from = root + "/source"; r.into = root + "/roms"; r.extensions = { "cue", "gdi", "m3u" };
        std::atomic<bool> stop{ false };
        auto done = GameImport::run(r, true, stop, [](const fs::path&) { return uintmax_t(0); });
        check(done.message.find("Not enough free space") != std::string::npos && !fs::exists(r.into), "free space refusal before writing");
        done = GameImport::run(r, true, stop);
        check(done.copied == 3 && fs::exists(r.into / "tracks/audio.bin") && fs::exists(r.into / "disc.raw"), "all descriptor types and companions copied");
        check(done.missing == 1 && done.message.find("missing.bin") != std::string::npos, "missing companion named");
        r.into = root + "/stopped";
        done = GameImport::run(r, true, stop, {}, [&](const std::string& message) {
            if (message.find("audio.bin") != std::string::npos && fs::exists(r.into / "tracks/audio.bin.retrofe-part")) stop = true;
        });
        check(done.changed && fs::exists(r.into / "a.cue"), "Stop keeps finished file");
        check(!fs::exists(r.into / "tracks/audio.bin") && !fs::exists(r.into / "tracks/audio.bin.retrofe-part"), "Stop removes partial file");
        check(done.message.find("Stopped. Copied 2 games into " + r.into.string() + "; 1 file a game refers to is missing.") == 0, "Stop and missing file reported");
    });
    run("add games: artwork", [] {
        std::string root = makeRoot("import-artwork");
        writeFile(root + "/source/roms/sub/Contra.nes", "game");
        writeFile(root + "/source/medium_artwork/logo/Contra.png", "logo");
        writeFile(root + "/source/medium_artwork/fanart/Contra/one.jpg", "picture");
        writeFile(root + "/source/medium_artwork/logo/Other.png", "other");
        writeFile(root + "/source/system_artwork/nested/logo.png", "system");
        GameImport::Request r;
        r.from = root + "/source/roms"; r.into = root + "/dest/roms"; r.extensions = { "nes" };
        r.hierarchy = true; r.copyArtwork = true;
        Library::Properties properties(root);
        properties.set("collections.NES.media.logo", root + "/custom/logo");
        properties.set("collections.NES.media.fanart", root + "/custom/fanart");
        properties.set("collections.NES.media.system_artwork", root + "/custom/system");
        r.mediaPath = [properties](const std::string& type, bool system) { return fs::path(properties.mediaPath("NES", type, system)); };
        std::atomic<bool> stop{ false };
        auto count = GameImport::run(r, false, stop);
        check(count.artworkFound && count.artworkGames == 1 && count.systemArtwork && count.artworkLeft == 1, "count matching and unrelated artwork");
        auto noSpace = GameImport::run(r, true, stop, [](const fs::path&) { return uintmax_t(5); });
        check(noSpace.message.find("Not enough free space") != std::string::npos && !fs::exists(r.into), "space includes artwork before copying");
        auto done = GameImport::run(r, true, stop);
        check(done.copied == 1 && done.artworkCopied == 3 && done.artworkLeft == 1, "game and selected artwork copied");
        check(fs::exists(fs::path(root) / "custom/logo/Contra.png") && fs::exists(fs::path(root) / "custom/fanart/Contra/one.jpg") && fs::exists(fs::path(root) / "custom/system/nested/logo.png"), "custom destinations and media folder");
        check(!fs::exists(fs::path(root) / "custom/logo/Other.png"), "other game artwork left");
        writeFile(root + "/custom/logo/Contra.png", "different size");
        done = GameImport::run(r, true, stop);
        check(done.artworkAlready == 2 && done.artworkConflicts == 1 && done.message.find("Contra.png") != std::string::npos, "artwork already and conflict counted and named");
        r.into = root + "/off/roms"; r.copyArtwork = false;
        r.mediaPath = [root](const std::string& type, bool) { return fs::path(root) / "off" / type; };
        done = GameImport::run(r, true, stop);
        check(done.copied == 1 && !fs::exists(fs::path(root) / "off/logo"), "toggle off");
        r.into = root + "/stop/roms"; r.copyArtwork = true;
        r.mediaPath = [root](const std::string& type, bool system) { return fs::path(root) / "stop" / (system ? "system" : type); };
        done = GameImport::run(r, true, stop, {}, [&](const std::string& message) {
            if (message.find("one.jpg") != std::string::npos && fs::exists(fs::path(root) / "stop/fanart/Contra/one.jpg.retrofe-part")) stop = true;
        });
        check(stop && !fs::exists(fs::path(root) / "stop/fanart/Contra/one.jpg.retrofe-part") && !fs::exists(fs::path(root) / "stop/fanart/Contra/one.jpg"), "stop removes partial artwork");
    });
    run("add games: no artwork beside the games folder", [] {
        std::string root = makeRoot("import-no-artwork");
        writeFile(root + "/source/roms/Contra.nes", "game");
        writeFile(root + "/source/artwork/logo/Contra.png", "not RetroFE's layout");
        GameImport::Request r;
        r.from = root + "/source/roms"; r.into = root + "/dest/roms"; r.extensions = { "nes" };
        r.copyArtwork = true;
        r.mediaPath = [root](const std::string& type, bool) { return fs::path(root) / "media" / type; };
        std::atomic<bool> stop{ false };
        auto count = GameImport::run(r, false, stop);
        check(!count.artworkFound && count.artworkGames == 0, "nothing found");
        auto done = GameImport::run(r, true, stop);
        checkEqual(done.message, "Copied 1 game into " + r.into.string() + ".", "the message is as before");
        check(!fs::exists(fs::path(root) / "media"), "no media folder made");
    });
    run("add games: the artwork message", [] {
        std::string root = makeRoot("import-artwork-message");
        writeFile(root + "/source/roms/Contra.nes", "game");
        writeFile(root + "/source/medium_artwork/logo/Contra.png", "logo");
        writeFile(root + "/source/medium_artwork/logo/Other.png", "other");
        GameImport::Request r;
        r.from = root + "/source/roms"; r.into = root + "/dest/roms"; r.extensions = { "nes" };
        r.copyArtwork = true;
        r.mediaPath = [root](const std::string& type, bool) { return fs::path(root) / "media" / type; };
        std::atomic<bool> stop{ false };
        auto done = GameImport::run(r, true, stop);
        checkEqual(done.message, "Copied 1 game into " + r.into.string() + ". Artwork: copied 1 file; 1 file for other games was left.",
                   "one of each, in words");
    });
    run("add games: metadata layouts and safeguards", [] {
        std::string root = makeRoot("import-metadata");
        fs::path source = fs::path(root) / "plain";
        writeFile((source / "collections/Source/roms/a.nes").string(), "game");
        writeFile((source / "meta/hyperlist/Source.xml").string(), "metadata content");
        writeFile((source / "meta/mamelist/Source.extra.XML").string(), "mame content");
        writeFile((source / "meta/hyperlist/Other.xml").string(), "other");
        fs::last_write_time(source / "meta/hyperlist/Source.xml", fs::file_time_type::clock::now() - std::chrono::hours(240));
        GameImport::Request r;
        r.from = source / "collections/Source/roms"; r.into = fs::path(root) / "dest/roms";
        r.installRoot = fs::path(root) / "dest"; r.metadataType = "Target"; r.extensions = { "nes" }; r.copyMetadata = true;
        std::atomic<bool> stop{ false };
        auto count = GameImport::run(r, false, stop);
        check(count.metadataFound && count.metadataDescription.find("Source.xml") != std::string::npos, "metadata discovered");
        auto noSpace = GameImport::run(r, true, stop, [](const fs::path&) { return uintmax_t(5); });
        check(noSpace.message.find("Not enough free space") != std::string::npos && !fs::exists(r.into), "metadata included in space check");
        auto done = GameImport::run(r, true, stop);
        check(fs::exists(r.installRoot / "meta/hyperlist/Target.xml") && fs::exists(r.installRoot / "meta/mamelist/Target.extra.xml"), "target type and both folders");
        check(!fs::exists(r.installRoot / "meta/hyperlist/Other.xml"), "other type excluded");
        check(fs::last_write_time(r.installRoot / "meta/hyperlist/Target.xml") > fs::file_time_type::clock::now() - std::chrono::hours(1), "copied timestamp is recent");
        done = GameImport::run(r, true, stop);
        check(done.message.find("Target.xml was already there") != std::string::npos, "same file reported");
        fs::remove(r.installRoot / "meta/hyperlist/Target.xml");
        writeFile((r.installRoot / "meta/hyperlist/Target.custom.xml").string(), "mine");
        done = GameImport::run(r, true, stop);
        check(done.message.find("kept your meta/hyperlist/Target.custom.xml") != std::string::npos && !fs::exists(r.installRoot / "meta/hyperlist/Target.xml"), "different target file kept and named");
        r.installRoot = fs::path(root) / "off"; r.copyMetadata = false;
        GameImport::run(r, true, stop);
        check(!fs::exists(r.installRoot / "meta"), "metadata toggle off");
        r.from = fs::path(root) / "outside/roms"; writeFile((r.from / "a.nes").string(), "game");
        check(!GameImport::run(r, false, stop).metadataFound, "outside collections is invisible");
        fs::path install = fs::path(root) / "split";
        r.from = install / "content/retrofe/collections/Source/roms";
        writeFile((r.from / "a.nes").string(), "game");
        writeFile((install / "appdata/retrofe/collections/Source/settings.conf").string(), "metadata.type = Custom\n");
        writeFile((install / "appdata/retrofe/meta/hyperlist/Custom.xml").string(), "split metadata");
        r.installRoot = fs::path(root) / "splitdest"; r.into = r.installRoot / "roms"; r.copyMetadata = true;
        done = GameImport::run(r, true, stop);
        check(done.metadataFound && fs::exists(r.installRoot / "meta/hyperlist/Target.xml"), "split layout and source setting honoured");
        r.installRoot = fs::path(root) / "stopped-meta"; r.into = r.installRoot / "roms";
        done = GameImport::run(r, true, stop, {}, [&](const std::string& message) {
            if (message.find("Custom.xml") != std::string::npos && fs::exists(r.installRoot / "meta/hyperlist/Target.xml.retrofe-part")) stop = true;
        });
        check(stop && !fs::exists(r.installRoot / "meta/hyperlist/Target.xml.retrofe-part") && !fs::exists(r.installRoot / "meta/hyperlist/Target.xml"), "stop removes partial metadata");
    });
});

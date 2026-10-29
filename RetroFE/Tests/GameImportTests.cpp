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
#include <atomic>

namespace fs = std::filesystem;
using namespace Test;
using Manager::GameImport;

static Test::Suite imports("game imports", [] {
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
        std::string notes = " Missing: " + (r.from / "missing.bin").string() + " Kept " + (r.into / "b.cue").string() + "; skipped " + (r.from / "b.cue").string();
        checkEqual(GameImport::run(r, true, stop).message, "Copied 1 game into " + r.into.string()
            + "; 1 has the name of a different file already there, so it was skipped; 1 file a game refers to is missing." + notes, "singular skipped and missing with filenames");
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
});

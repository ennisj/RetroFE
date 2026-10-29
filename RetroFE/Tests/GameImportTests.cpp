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
        check(done.message.find("Stopped") != std::string::npos, "Stop reported");
    });
});

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
#include "../Source/Catalog/Archive.h"
#include "../Source/Catalog/Hash.h"
#include "../Source/Catalog/Installer.h"
#include "../Source/Catalog/Index.h"
#include "minizip/zip.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>

using namespace Test;
using namespace Catalog;
namespace fs = std::filesystem;

namespace
{
    std::string fixture(const std::string& name) { return std::string(RETROFE_TEST_FIXTURES) + "/7z/" + name; }
    std::string digest(const std::string& bytes) { Sha256 hash; hash.update(bytes.data(), bytes.size()); return hash.hex(); }
    std::string lf(std::string text) { text.erase(std::remove(text.begin(), text.end(), '\r'), text.end()); return text; }
}

static Suite archives("archives", [] {
    auto expected = nlohmann::json::parse(readFile(fixture("expected.json")));
    run("archive forms come from content, including a validated SFX header", [&] {
        uint64_t offset;
        for (const char* name : { "plain.7z", "bcj-lzma2.7z", "unsafe.7z", "corrupt.7z", "lzma-sdk-2604.7z" })
            check(Archive::detect(fixture(name), offset) == Archive::Form::SevenZip && offset == 0, name);
        check(Archive::detect(fixture("sfx.exe"), offset) == Archive::Form::SelfExtracting && offset == expected["sfxStubSize"].get<uint64_t>(), "stray signature skipped");
        std::string root = makeRoot("archive-detection");
        std::string zip = root + "/download.part";
        zipFile file = zipOpen64(zip.c_str(), APPEND_STATUS_CREATE);
        zipClose(file, nullptr);
        check(Archive::detect(zip, offset) == Archive::Form::Zip, "empty zip");
        Archive empty;
        std::string emptyError;
        check(empty.open(zip, "empty.zip", emptyError) && empty.entries().empty(), "empty zip opens: " + emptyError);
        file = zipOpen64(zip.c_str(), APPEND_STATUS_CREATE);
        zip_fileinfo info{};
        zipOpenNewFileInZip(file, "a.txt", &info, nullptr, 0, nullptr, 0, nullptr, 0, 0);
        zipWriteInFileInZip(file, "hello", 5);
        zipCloseFileInZip(file);
        zipClose(file, nullptr);
        check(Archive::detect(zip, offset) == Archive::Form::Zip, "zip with content and no zip extension");
        Archive archive;
        std::string error, bytes;
        check(archive.open(zip, "published.zip", error) && archive.extract(0, [&](const void* p, size_t n) { bytes.append(static_cast<const char*>(p), n); return true; }, nullptr, error) && bytes == "hello", "zip reader");
        writeFile(root + "/fake.7z", "ordinary text");
        check(Archive::detect(root + "/fake.7z", offset) == Archive::Form::None, "extension is not detection");
    });
    run("7z and SFX decode exact names, sizes and hashes", [&] {
        for (const char* name : { "plain.7z", "bcj-lzma2.7z", "sfx.exe" }) {
            Archive archive;
            std::string error;
            check(archive.open(fixture(name), name, error), std::string(name) + ": " + error);
            std::map<std::string, std::string> files;
            for (size_t i = 0; i < archive.entries().size(); ++i) {
                const auto& entry = archive.entries()[i];
                if (entry.directory) continue;
                std::string bytes;
                check(archive.extract(i, [&](const void* p, size_t n) { bytes.append(static_cast<const char*>(p), n); return true; }, nullptr, error), entry.name + ": " + error);
                files[entry.name] = bytes;
                check(expected["files"].contains(entry.name), "expected name " + entry.name);
                if (!expected["files"].contains(entry.name)) continue;
                const auto& want = expected["files"][entry.name];
                check(entry.size == want["size"] && bytes.size() == entry.size, "size " + entry.name);
                checkEqual(digest(bytes), want["sha256"], "hash " + entry.name);
            }
            check(files.size() == expected["files"].size(), "exact file set");
        }
    });
    run("unsafe 7z names are visible to the installer path checks", [&] {
        Archive archive;
        std::string error;
        check(archive.open(fixture("unsafe.7z"), "unsafe.7z", error), error);
        size_t refused = 0;
        for (const auto& entry : archive.entries()) {
            if (entry.directory) continue;
            bool unsafe = std::find(expected["unsafe"].begin(), expected["unsafe"].end(), entry.name) != expected["unsafe"].end();
            check(isSafeRelativePath(entry.name) != unsafe, "path check: " + entry.name);
            if (unsafe) ++refused;
        }
        check(refused == expected["unsafe"].size(), "both device and stream names checked");
    });
    run("SDK archive exercises BCJ2 and matches the vendored decoder", [&] {
        Archive archive;
        std::string error;
        check(archive.open(fixture("lzma-sdk-2604.7z"), "sdk.7z", error), error);
        bool source = false, program = false;
        for (size_t i = 0; i < archive.entries().size(); ++i) {
            const auto& entry = archive.entries()[i];
            if (entry.name != "C/7zDec.c" && (program || entry.name.find("bin/") != 0 || fs::path(entry.name).extension() != ".exe")) continue;
            std::string bytes;
            check(archive.extract(i, [&](const void* p, size_t n) { bytes.append(static_cast<const char*>(p), n); return true; }, nullptr, error), entry.name + ": " + error);
            check(bytes.size() == entry.size, "SDK entry size");
            if (entry.name == "C/7zDec.c") {
                source = true;
                check(lf(bytes) == lf(readFile(std::string(RETROFE_TEST_SDK) + "/7zDec.c")), "decoder source matches");
            } else program = true;
        }
        check(source && program, "source and BCJ2 program found (never executed)");
    });
    run("7z extraction observes stop before and during output", [&] {
        Archive archive;
        std::string error;
        check(archive.open(fixture("plain.7z"), "plain.7z", error), error);
        std::atomic<bool> stop{true};
        size_t calls = 0;
        auto writer = [&](const void*, size_t) { ++calls; stop = true; return true; };
        check(!archive.extract(0, writer, &stop, error) && error == "Stopped" && calls == 0, "already stopped");
        stop = false;
        for (size_t i = 0; i < archive.entries().size(); ++i) {
            if (archive.entries()[i].directory || archive.entries()[i].size == 0) continue;
            check(!archive.extract(i, writer, &stop, error) && error == "Stopped" && calls == 1, "stop during output");
            break;
        }
    });
});

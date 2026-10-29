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

// Tests for the Catalog: hashes, versions, index parsing, sources and installing.

#include "TestSupport.h"
#include "../Source/Catalog/Download.h"
#include "../Source/Catalog/Hash.h"
#include "../Source/Catalog/Http.h"
#include "../Source/Catalog/Index.h"
#include "../Source/Catalog/Installer.h"
#include "../Source/Catalog/Package.h"
#include "../Source/Catalog/Service.h"
#include "../Source/Catalog/Sources.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Setup.h"
#include "minizip/zip.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace Catalog;

namespace
{
    // Writes a zip archive with these (name, content) entries
    bool makeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& entries)
    {
        fs::create_directories(fs::path(path).parent_path());
        zipFile zip = zipOpen64(path.c_str(), APPEND_STATUS_CREATE);
        if (!zip) return false;
        for (const auto& [name, content] : entries) {
            zip_fileinfo info{};
            if (zipOpenNewFileInZip64(zip, name.c_str(), &info, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0) != ZIP_OK) {
                zipClose(zip, nullptr);
                return false;
            }
            zipWriteInFileInZip(zip, content.data(), static_cast<unsigned>(content.size()));
            zipCloseFileInZip(zip);
        }
        return zipClose(zip, nullptr) == ZIP_OK;
    }

    // Serves fixed documents; anything else fails, as if offline
    class FakeHttp : public Http
    {
    public:
        std::map<std::string, std::string> documents;

        bool get(const std::string& url, std::string& body, std::string& error) override
        {
            auto it = documents.find(url);
            if (it == documents.end()) {
                error = "offline";
                return false;
            }
            body = it->second;
            return true;
        }

        bool download(const std::string&, const std::string&, uint64_t, const Progress&, Transfer& transfer) override
        {
            transfer.error = "not used";
            return false;
        }
    };

    // Serves files for download like a web server, with scripted failures
    class ServingHttp : public Http
    {
    public:
        std::map<std::string, std::string> files;
        std::vector<size_t> dropAfter;      // for each next request: lose the connection after this many bytes
        bool ranges = true;                 // whether resuming is supported
        std::map<std::string, std::string> etags;   // with one, a resume only continues the same version
        std::vector<uint64_t> offsets;      // the offset each request asked for

        bool get(const std::string&, std::string&, std::string& error) override
        {
            error = "not used";
            return false;
        }

        bool download(const std::string& url, const std::string& file, uint64_t offset,
                      const Progress& progress, Transfer& transfer) override
        {
            std::string validator = transfer.validator;
            transfer = Transfer();
            offsets.push_back(offset);
            auto it = files.find(url);
            if (it == files.end()) {
                transfer.error = "The server answered 404";
                return false;
            }
            const std::string& body = it->second;
            uint64_t start = ranges ? std::min<uint64_t>(offset, body.size()) : 0;
            auto etag = etags.find(url);
            if (etag != etags.end()) {
                if (validator != etag->second) start = 0;   // If-Range didn't match: the whole new file
                transfer.validator = etag->second;
            }
            transfer.resumed = start > 0;
            std::string rest = body.substr(start);
            bool drop = !dropAfter.empty();
            if (drop) {
                rest = rest.substr(0, std::min(rest.size(), dropAfter.front()));
                dropAfter.erase(dropAfter.begin());
            }
            std::ofstream out(file, std::ios::binary | (transfer.resumed ? std::ios::app : std::ios::trunc));
            out << rest;
            out.close();
            if (progress && !progress(start + rest.size(), body.size())) {
                transfer.cancelled = true;
                transfer.error = "Cancelled";
                return false;
            }
            if (drop) {
                transfer.retry = true;
                transfer.error = "The connection was lost";
                return false;
            }
            return true;
        }
    };

    std::string sha256Of(const std::string& text)
    {
        Sha256 hash;
        hash.update(text.data(), text.size());
        return hash.hex();
    }

    // A Downloader that doesn't wait between retries; delays records what it would have waited
    Downloader testDownloader(Http* http, const std::string& folder, std::vector<int>& delays)
    {
        Downloader downloader(http, folder);
        downloader.sleep = [&delays](int ms) { delays.push_back(ms); };
        downloader.freeSpace = [](const std::string&, uint64_t& available) {
            available = 1ull << 40;
            return true;
        };
        return downloader;
    }

    Targets targetsUnder(const std::string& root)
    {
        Targets targets;
        targets.root = root;
        targets.collections = root + "/collections";
        targets.items = root + "/roms";
        targets.media = root + "/media";
        targets.layouts = root + "/layouts";
        targets.meta = root + "/meta";
        return targets;
    }

    Package zipPackage(const std::string& id, const std::string& version, const std::string& zip, const std::string& target, const std::string& path)
    {
        Package package;
        package.id = id;
        package.name = id;
        package.kind = "collection";
        package.version = version;
        PackageFile file;
        file.url = zip;
        file.target = target;
        file.path = path;
        package.files.push_back(file);
        return package;
    }

    // A package of one file to download, with its size and hash
    Package remotePackage(const std::string& id, const std::string& url, const std::string& content)
    {
        Package package = zipPackage(id, "1", url, "root", "");
        package.files[0].size = content.size();
        package.files[0].sha256 = sha256Of(content);
        return package;
    }
}

void runCatalogTests()
{
    std::string error;

    // --- Hashes ----------------------------------------------------------------

    run("SHA-256 and MD5 match the standard test vectors", [&] {
        auto sha = [](const std::string& s) { Sha256 h; h.update(s.data(), s.size()); return h.hex(); };
        auto md5 = [](const std::string& s) { Md5 h; h.update(s.data(), s.size()); return h.hex(); };
        checkEqual(sha(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
        checkEqual(sha("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
        checkEqual(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                   "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 448 bits");
        checkEqual(sha(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 million a");
        checkEqual(md5(""), "d41d8cd98f00b204e9800998ecf8427e", "md5 empty");
        checkEqual(md5("abc"), "900150983cd24fb0d6963f7d28e17f72", "md5 abc");
        checkEqual(md5("message digest"), "f96b697d7cb7938d525a2f31aaf161d0", "md5 message digest");
        checkEqual(md5("12345678901234567890123456789012345678901234567890123456789012345678901234567890"),
                   "57edf4a22be3c955ac49da2e2107b67a", "md5 80 digits");
    });

    run("hashing in pieces matches hashing at once", [&] {
        std::string data;
        for (int i = 0; i < 5000; i++) data += static_cast<char>(i * 7);
        Sha256 whole; whole.update(data.data(), data.size());
        std::string expected = whole.hex();
        Sha256 pieces;
        for (size_t i = 0; i < data.size(); i += 61) pieces.update(data.data() + i, std::min<size_t>(61, data.size() - i));
        checkEqual(pieces.hex(), expected, "sha256");
        std::string root = makeRoot("hash-file");
        writeFile(root + "/f.bin", data);
        checkEqual(sha256File(root + "/f.bin"), expected, "sha256File");
        checkEqual(sha256File(root + "/missing.bin"), "", "missing file");
    });

    // --- Versions and paths ----------------------------------------------------

    run("versions compare by their numbers", [&] {
        check(compareVersions("1.10", "1.9") > 0, "1.10 > 1.9");
        check(compareVersions("1.0", "1") == 0, "1.0 == 1");
        check(compareVersions("v2.0b13", "v2.0b9") > 0, "v2.0b13 > v2.0b9");
        check(compareVersions("2.1.0", "2.1.1") < 0, "2.1.0 < 2.1.1");
        check(compareVersions("", "1") < 0, "no version is older");
        check(satisfies("1.2", ">=1.0") && !satisfies("0.9", ">=1.0"), ">=");
        check(satisfies("1.2", "1.2") && !satisfies("1.3", "1.2"), "exact");
        check(satisfies("5", ""), "anything");
    });

    run("index references resolve against where the index is", [&] {
        checkEqual(resolveLocation("https://h.org/x/catalog.json", "packs/a.zip"), "https://h.org/x/packs/a.zip", "relative");
        checkEqual(resolveLocation("https://h.org/x/y/catalog.json", "../a.zip"), "https://h.org/x/a.zip", "parent");
        checkEqual(resolveLocation("https://h.org/x/catalog.json?v=2", "./a.zip"), "https://h.org/x/a.zip", "query and ./");
        checkEqual(resolveLocation("https://h.org/x/catalog.json", "/a.zip"), "https://h.org/a.zip", "host-relative");
        checkEqual(resolveLocation("https://h.org/x/catalog.json", "https://cdn.org/a.zip"), "https://cdn.org/a.zip", "absolute");
#ifdef _WIN32
        checkEqual(resolveLocation("C:\\packs\\catalog.json", "art\\a.png"), (fs::path("C:\\packs") / "art\\a.png").lexically_normal().string(), "file path");
#else
        checkEqual(resolveLocation("/packs/catalog.json", "art/a.png"), "/packs/art/a.png", "file path");
#endif
    });

    run("ids and paths that could escape are refused", [&] {
        check(isValidId("sega-genesis_2.1"), "normal id");
        check(!isValidId("") && !isValidId("../x") && !isValidId("a/b") && !isValidId(".hidden"), "bad ids");
        check(isSafeRelativePath("") && isSafeRelativePath("Sega Genesis/roms"), "safe paths");
        check(!isSafeRelativePath("../x") && !isSafeRelativePath("a/../../x") && !isSafeRelativePath("/x")
              && !isSafeRelativePath("C:\\x") && !isSafeRelativePath("a\\..\\b"), "unsafe paths");
    });

    // --- Index -----------------------------------------------------------------

    run("index packages get defaults, and bad ones are skipped", [&] {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        bool ok = parseIndex(R"({
            "format": 1,
            "packages": [
              { "id": "genesis", "name": "Sega Genesis", "kind": "collection", "version": "2.1",
                "artwork": { "logo": "art/genesis.png" },
                "requires": [ "emu", { "id": "core", "version": ">=1.0" } ],
                "files": [ { "url": "packs/genesis.zip", "size": 10, "sha256": "AB12AB12AB12AB12AB12AB12AB12AB12AB12AB12AB12AB12AB12AB12AB12AB12", "path": "Sega Genesis" } ] },
              { "id": "theme", "kind": "layout", "files": [ { "url": "t.zip" } ] },
              { "id": "../bad", "files": [ { "url": "x.zip" } ] },
              { "id": "nofiles" },
              { "id": "escape", "files": [ { "url": "x.zip", "path": "../../Windows" } ] },
              { "id": "badtarget", "files": [ { "url": "x.zip", "target": "system32" } ] },
              { "id": "genesis", "files": [ { "url": "dupe.zip" } ] }
            ] })", "https://h.org/catalog.json", "community", packages, warnings, error);
        check(ok, "parsed: " + error);
        check(packages.size() == 2, "two good packages, got " + std::to_string(packages.size()));
        check(warnings.size() == 5, "five warnings, got " + std::to_string(warnings.size()));
        if (packages.size() != 2) return;
        const Package& genesis = packages[0];
        checkEqual(genesis.source, "community", "source");
        checkEqual(genesis.files[0].url, "https://h.org/packs/genesis.zip", "file url");
        checkEqual(genesis.files[0].target, "collections", "target from kind");
        checkEqual(genesis.files[0].sha256.substr(0, 4), "ab12", "hash lowercased");
        checkEqual(genesis.artwork.at("logo"), "https://h.org/art/genesis.png", "artwork url");
        check(genesis.requirements.size() == 2 && genesis.requirements[1].version == ">=1.0", "requires");
        checkEqual(packages[1].name, "theme", "name defaults to id");
        checkEqual(packages[1].files[0].target, "layouts", "layout target");
    });

    run("a broken index is an error", [&] {
        std::vector<Package> packages;
        std::vector<std::string> warnings;
        check(!parseIndex("{ not json", "x", "s", packages, warnings, error), "bad JSON");
        check(!parseIndex(R"({"name":"x"})", "x", "s", packages, warnings, error), "no packages list");
    });

    // --- Sources ---------------------------------------------------------------

    run("sources.conf round-trips, keeping # in URLs", [&] {
        std::string root = makeRoot("sources");
        std::vector<SourceConfig> sources(2);
        sources[0].name = "community"; sources[0].type = "index"; sources[0].url = "https://h.org/catalog.json#main";
        sources[1].name = "usb"; sources[1].type = "folder"; sources[1].path = "E:\\packs"; sources[1].enabled = false;
        check(saveSources(root + "/sources.conf", sources, error), "saved: " + error);
        std::vector<SourceConfig> loaded;
        check(loadSources(root + "/sources.conf", loaded, error), "loaded: " + error);
        check(loaded.size() == 2, "two sources");
        if (loaded.size() != 2) return;
        checkEqual(loaded[0].url, "https://h.org/catalog.json#main", "url with #");
        checkEqual(loaded[1].path, "E:\\packs", "folder path");
        check(!loaded[1].enabled, "disabled kept");
        std::vector<SourceConfig> none;
        check(loadSources(root + "/missing.conf", none, error) && none.empty(), "missing file is no sources");
    });

    run("new sources are checked before they're added", [&] {
        std::string root = makeRoot("validate");
        std::vector<SourceConfig> existing(1);
        existing[0].name = "community";
        SourceConfig source;
        source.type = "index";
        source.name = "Community"; source.url = "https://h.org/c.json";
        check(!validateSource(source, existing).empty(), "duplicate name, any case");
        source.name = "has space";
        check(!validateSource(source, existing).empty(), "invalid name");
        source.name = "other";
        checkEqual(validateSource(source, existing), "", "https index");
        source.url = "http://example.org/c.json";
        check(!validateSource(source, existing).empty(), "plain http on the internet");
        source.url = "http://192.168.1.5:8000/c.json";
        checkEqual(validateSource(source, existing), "", "plain http on the LAN");
        source.type = "folder"; source.path = root;
        checkEqual(validateSource(source, existing), "", "folder");
        source.path = root + "/nope";
        check(!validateSource(source, existing).empty(), "missing folder");
    });

    run("folder sources read sidecars, file names and catalog.json", [&] {
        std::string root = makeRoot("folder");
        makeZip(root + "/Sega Genesis v2.1.zip", { { "a.txt", "a" } });
        makeZip(root + "/theme.zip", { { "b.txt", "b" } });
        writeFile(root + "/theme.zip.package.json", R"({ "id": "blue-theme", "name": "Blue", "kind": "layout", "version": "3" })");
        writeFile(root + "/notes.txt", "ignored");
        SourceConfig source;
        source.name = "usb"; source.type = "folder"; source.path = root;
        SourceContents contents = readSource(source, root + "/cache", nullptr);
        checkEqual(contents.error, "", "no error");
        check(contents.packages.size() == 2, "two packages");
        if (contents.packages.size() == 2) {
            const Package& genesis = contents.packages[0];
            checkEqual(genesis.id, "sega-genesis", "id from name");
            checkEqual(genesis.name, "Sega Genesis", "name");
            checkEqual(genesis.version, "2.1", "version from name");
            check(genesis.files[0].size > 0 && genesis.files[0].target == "root", "file defaults");
            const Package& theme = contents.packages[1];
            checkEqual(theme.id, "blue-theme", "sidecar id");
            checkEqual(theme.files[0].target, "layouts", "sidecar kind's target");
            check(fs::path(theme.files[0].url).filename() == "theme.zip", "sidecar defaults to its archive");
        }

        writeFile(root + "/catalog.json", R"({ "packages": [ { "id": "x", "files": [ { "url": "theme.zip" } ] } ] })");
        contents = readSource(source, root + "/cache", nullptr);
        check(contents.packages.size() == 1 && contents.packages[0].id == "x", "catalog.json wins");
    });

    run("index sources use the cache when offline", [&] {
        std::string root = makeRoot("index-cache");
        FakeHttp http;
        const std::string url = "https://h.org/catalog.json";
        http.documents[url] = R"({ "packages": [ { "id": "a", "files": [ { "url": "a.zip" } ] } ] })";
        SourceConfig source;
        source.name = "community"; source.type = "index"; source.url = url;

        SourceContents online = readSource(source, root + "/cache", &http);
        check(online.packages.size() == 1 && !online.fromCache, "online");
        check(fs::exists(root + "/cache/community/index.json"), "cached");

        http.documents.clear();
        SourceContents offline = readSource(source, root + "/cache", &http);
        check(offline.packages.size() == 1 && offline.fromCache, "from cache");
        checkEqual(offline.packages.empty() ? "" : offline.packages[0].files[0].url, "https://h.org/a.zip", "cached urls still resolve");

        SourceContents noHttp = readSource(source, root + "/nocache", nullptr);
        check(!noHttp.error.empty(), "no HTTP and no cache is an error");
    });

    // --- Installing ------------------------------------------------------------

    run("install extracts into the target and writes a manifest", [&] {
        std::string root = makeRoot("install");
        makeZip(root + "/dl/genesis.zip", { { "settings.conf", "list.extensions = md" }, { "roms/readme.txt", "put roms here" } });
        Installer installer(root + "/catalog", targetsUnder(root));
        Package package = zipPackage("genesis", "1.0", "genesis.zip", "collections", "Sega Genesis");
        check(installer.install(package, { root + "/dl/genesis.zip" }, error), "installed: " + error);
        checkEqual(readFile(root + "/collections/Sega Genesis/settings.conf"), "list.extensions = md", "file content");
        check(fs::exists(root + "/collections/Sega Genesis/roms/readme.txt"), "nested file");
        auto installed = installer.installed();
        check(installed.count("genesis") == 1, "manifest");
        if (installed.count("genesis")) {
            const Manifest& manifest = installed["genesis"];
            checkEqual(manifest.version, "1.0", "version");
            check(manifest.files.size() == 2, "two files");
            check(!manifest.files.empty() && manifest.files[0].sha256.size() == 64, "hashed");
        }
        check(!fs::exists(root + "/catalog/staging/genesis"), "staging cleaned up");
    });

    run("7z packages install with manifests and refuse unsafe or damaged archives", [&] {
        std::string root = makeRoot("install-7z");
        Installer installer(root + "/catalog", targetsUnder(root));
        std::string fixtures = std::string(RETROFE_TEST_FIXTURES) + "/7z/";
        auto expected = nlohmann::json::parse(readFile(fixtures + "expected.json"));
        check(installer.install(zipPackage("seven", "1", "published.7z", "collections", "Seven"), { fixtures + "plain.7z" }, error), error);
        auto manifests = installer.installed();
        check(manifests.count("seven") && manifests["seven"].files.size() == expected["files"].size(), "manifest file count");
        for (const auto& file : manifests["seven"].files) {
            std::string name = file.path.substr(6);
            check(expected["files"].contains(name), "manifest name");
            if (!expected["files"].contains(name)) continue;
            check(file.size == expected["files"][name]["size"] && file.sha256 == expected["files"][name]["sha256"], "manifest size and hash");
            check(sha256File(fs::u8path(root + "/collections/" + file.path)) == file.sha256, "installed bytes");
        }
        for (const char* bad : { "unsafe.7z", "corrupt.7z" }) {
            error.clear();
            check(!installer.install(zipPackage("bad", "1", bad, "collections", "Bad"), { fixtures + bad }, error), "refused " + std::string(bad));
            check(error.find(std::string(bad) == "unsafe.7z" ? "safely" : "Damaged") != std::string::npos, "clear error: " + error);
            check(!fs::exists(root + "/collections/Bad") && !fs::exists(root + "/catalog/installed/bad.json"), "no files outside staging or manifest");
        }
        writeFile(root + "/fake", "not an archive");
        check(!installer.install(zipPackage("fake", "1", "fake.7z", "root", ""), { root + "/fake" }, error) && error.find("Not an archive") != std::string::npos, "published archive must be an archive");
    });

    run("updates replace files, drop old ones and keep changed ones", [&] {
        std::string root = makeRoot("update");
        Installer installer(root + "/catalog", targetsUnder(root));
        makeZip(root + "/dl/v1.zip", { { "a.txt", "one" }, { "old.txt", "old" }, { "mine.txt", "original" } });
        check(installer.install(zipPackage("p", "1", "v1.zip", "root", ""), { root + "/dl/v1.zip" }, error), "v1: " + error);
        writeFile(root + "/mine.txt", "changed by me");

        makeZip(root + "/dl/v2.zip", { { "a.txt", "two" }, { "new.txt", "new" } });
        check(installer.install(zipPackage("p", "2", "v2.zip", "root", ""), { root + "/dl/v2.zip" }, error), "v2: " + error);
        checkEqual(readFile(root + "/a.txt"), "two", "replaced");
        check(fs::exists(root + "/new.txt"), "added");
        check(!fs::exists(root + "/old.txt"), "dropped");
        checkEqual(readFile(root + "/mine.txt"), "changed by me", "changed file kept");
        checkEqual(readFile(root + "/catalog/backups/p/1/root/a.txt"), "one", "backup of the replaced file");
        checkEqual(installer.installed()["p"].version, "2", "manifest updated");
    });

    run("installing over files backs them up", [&] {
        std::string root = makeRoot("backup");
        writeFile(root + "/layouts/Blue/layout.xml", "my own");
        makeZip(root + "/dl/blue.zip", { { "layout.xml", "theirs" } });
        Installer installer(root + "/catalog", targetsUnder(root));
        check(installer.install(zipPackage("blue", "3", "blue.zip", "layouts", "Blue"), { root + "/dl/blue.zip" }, error), "installed: " + error);
        checkEqual(readFile(root + "/layouts/Blue/layout.xml"), "theirs", "installed");
        checkEqual(readFile(root + "/catalog/backups/blue/before-3/layouts/Blue/layout.xml"), "my own", "backed up");
    });

    run("remove deletes installed files but keeps changed ones", [&] {
        std::string root = makeRoot("remove");
        Installer installer(root + "/catalog", targetsUnder(root));
        makeZip(root + "/dl/p.zip", { { "x/a.txt", "a" }, { "x/b.txt", "b" } });
        check(installer.install(zipPackage("p", "1", "p.zip", "media", "Pack"), { root + "/dl/p.zip" }, error), "installed: " + error);
        writeFile(root + "/media/Pack/x/b.txt", "edited");
        Installer::Removal removal;
        check(installer.remove("p", removal, error), "removed: " + error);
        check(!fs::exists(root + "/media/Pack/x/a.txt"), "file removed");
        check(fs::exists(root + "/media/Pack/x/b.txt") && removal.kept == 1, "changed file kept");
        check(installer.installed().empty(), "manifest removed");
        check(!installer.remove("p", removal, error), "removing again fails");
    });

    run("removing puts back the user's files it replaced, even after updates", [&] {
        std::string root = makeRoot("remove-restores");
        writeFile(root + "/collections/Genesis/settings.conf", "my settings");
        Installer installer(root + "/catalog", targetsUnder(root));
        makeZip(root + "/dl/v1.zip", { { "settings.conf", "theirs v1" }, { "menu.txt", "v1" } });
        check(installer.install(zipPackage("g", "1", "v1.zip", "collections", "Genesis"), { root + "/dl/v1.zip" }, error), "v1: " + error);
        makeZip(root + "/dl/v2.zip", { { "settings.conf", "theirs v2" }, { "menu.txt", "v2" } });
        check(installer.install(zipPackage("g", "2", "v2.zip", "collections", "Genesis"), { root + "/dl/v2.zip" }, error), "v2: " + error);
        checkEqual(readFile(root + "/collections/Genesis/settings.conf"), "theirs v2", "updated");
        check(fs::exists(root + "/catalog/backups/g/before-1") && fs::exists(root + "/catalog/backups/g/1"), "original and undo backups kept");

        Installer::Removal removal;
        check(installer.remove("g", removal, error), "removed: " + error);
        checkEqual(readFile(root + "/collections/Genesis/settings.conf"), "my settings", "the user's file is back");
        check(!fs::exists(root + "/collections/Genesis/menu.txt") && removal.restored == 1, "the package's own file is gone");
        check(!fs::exists(root + "/catalog/backups/g"), "backups cleared");
    });

    run("updates prune old backups", [&] {
        std::string root = makeRoot("backup-prune");
        Installer installer(root + "/catalog", targetsUnder(root));
        for (const char* version : { "1", "2", "3", "4" }) {
            makeZip(root + "/dl/p.zip", { { "a.txt", std::string("v") + version } });
            check(installer.install(zipPackage("p", version, "p.zip", "root", ""), { root + "/dl/p.zip" }, error), std::string("v") + version + ": " + error);
        }
        int folders = 0;
        for (const auto& entry : fs::directory_iterator(root + "/catalog/backups/p")) {
            (void)entry;
            folders++;
        }
        check(folders == 1 && fs::exists(root + "/catalog/backups/p/3"), "only the last update's backup");
    });

    run("packages can't write programs or into protected folders", [&] {
        std::string root = makeRoot("refused");
        Targets targets = targetsUnder(root);
        targets.protectedFolders.push_back(root + "/retrofe");
        Installer installer(root + "/catalog", targets);
        makeZip(root + "/dl/exe.zip", { { "readme.txt", "hi" }, { "tools/run.BAT", "format c:" } });
        check(!installer.install(zipPackage("exe", "1", "exe.zip", "root", ""), { root + "/dl/exe.zip" }, error)
              && error.find("program or script") != std::string::npos, "scripts refused: " + error);
        makeZip(root + "/dl/catalog.zip", { { "catalog/sources.conf", "sources.evil.type = index" } });
        check(!installer.install(zipPackage("cat", "1", "catalog.zip", "root", ""), { root + "/dl/catalog.zip" }, error), "catalog folder refused");
        check(!fs::exists(root + "/catalog/sources.conf"), "sources untouched");
        makeZip(root + "/dl/prog.zip", { { "retrofe/SDL2.txt", "x" } });
        check(!installer.install(zipPackage("prog", "1", "prog.zip", "root", ""), { root + "/dl/prog.zip" }, error), "program folder refused");
        check(!fs::exists(root + "/readme.txt") && installer.installed().empty(), "nothing installed");
    });

    run("a recovery can't move files from outside its staging folder", [&] {
        std::string root = makeRoot("recover-staged");
        writeFile(root + "/settings.conf", "the user's settings");
        Installer installer(root + "/catalog", targetsUnder(root));
        std::string sha = sha256File(root + "/settings.conf");
        writeFile(root + "/catalog/staging/p/plan.json",
                  R"({"id":"p","name":"p","version":"1","files":[{"target":"root","path":"stolen.txt","size":1,"sha256":")" + sha
                  + R"(","staged":"../../../settings.conf"}]})");
        writeFile(root + "/catalog/staging/p/.ready", "");
        installer.recover();
        checkEqual(readFile(root + "/settings.conf"), "the user's settings", "not moved");
        check(!fs::exists(root + "/stolen.txt") && !fs::exists(root + "/catalog/staging/p"), "refused and not retried");
    });

    run("an install that can't be completed changes nothing", [&] {
        std::string root = makeRoot("rollback");
        writeFile(root + "/a.txt", "the user's a");
        fs::create_directories(root + "/b.txt");   // a folder where a file should go
        Installer installer(root + "/catalog", targetsUnder(root));
        makeZip(root + "/dl/p.zip", { { "a.txt", "theirs" }, { "b.txt", "theirs" } });
        check(!installer.install(zipPackage("p", "1", "p.zip", "root", ""), { root + "/dl/p.zip" }, error), "failed: " + error);
        checkEqual(readFile(root + "/a.txt"), "the user's a", "untouched");
        check(installer.installed().empty() && !fs::exists(root + "/catalog/staging/p"), "no manifest, nothing left staged");
    });

    run("names Windows treats specially are refused", [&] {
        for (const char* path : { "CON", "nul.txt", "a/Aux", "COM1.dat", "lpt9", "name.", "dir /x", "a|b", "x\ty" }) {
            check(!isSafeRelativePath(path), std::string("refused: ") + path);
        }
        for (const char* path : { "console.txt", "com10", "a/./b.txt", "Sega Genesis/roms/a.zip" }) {
            check(isSafeRelativePath(path), std::string("allowed: ") + path);
        }
        check(!isValidId("con") && !isValidId("pack.") && isValidId("pack.v2"), "ids");
    });

    run("plain http only on the local network", [&] {
        for (const char* url : { "http://localhost:8080/i.json", "http://127.0.0.1/", "http://192.168.1.20/x", "http://10.0.0.5",
                                 "http://172.20.1.1/", "http://nas/packs/", "http://retro-pc.local/i.json", "http://[::1]:80/" }) {
            check(isLocalAddress(url), std::string("local: ") + url);
        }
        for (const char* url : { "http://10.evil.com/", "http://192.168.evil.com/", "http://172.40.0.1/", "http://example.org/",
                                 "http://user@evil.com/", "http://1.2.3.4/" }) {
            check(!isLocalAddress(url), std::string("not local: ") + url);
        }
        check(isAllowedLocation("https://example.org/a.zip") && !isAllowedLocation("http://example.org/a.zip")
              && isAllowedLocation("E:/packs/a.zip"), "downloads");
    });

    run("archives that reach outside their folder are refused", [&] {
        std::string root = makeRoot("zip-slip");
        makeZip(root + "/dl/evil.zip", { { "ok.txt", "fine" }, { "../../escaped.txt", "bad" } });
        Installer installer(root + "/catalog", targetsUnder(root));
        check(!installer.install(zipPackage("evil", "1", "evil.zip", "collections", "X"), { root + "/dl/evil.zip" }, error), "refused");
        check(!error.empty(), "says why: " + error);
        check(!fs::exists(root + "/collections/X/ok.txt"), "nothing installed");
        check(!fs::exists(fs::path(root).parent_path() / "escaped.txt") && !fs::exists(root + "/escaped.txt"), "nothing escaped");
        check(installer.installed().empty(), "no manifest");
    });

    run("an interrupted install is finished or discarded on the next start", [&] {
        std::string root = makeRoot("recover");
        Installer installer(root + "/catalog", targetsUnder(root));
        makeZip(root + "/dl/p.zip", { { "a.txt", "a" } });
        check(installer.install(zipPackage("p", "1", "p.zip", "root", ""), { root + "/dl/p.zip" }, error), "installed: " + error);

        // Put the install back into staging as if it stopped right after being marked ready
        fs::create_directories(root + "/catalog/staging/p/files/0");
        fs::rename(root + "/a.txt", root + "/catalog/staging/p/files/0/a.txt");
        fs::remove(root + "/catalog/installed/p.json");
        std::string sha = sha256File(root + "/catalog/staging/p/files/0/a.txt");
        writeFile(root + "/catalog/staging/p/plan.json",
                  R"({"id":"p","name":"p","kind":"other","version":"1","source":"","files":[{"target":"root","path":"a.txt","size":1,"sha256":")" + sha + R"(","staged":"files/0/a.txt"}]})");
        writeFile(root + "/catalog/staging/p/.ready", "");
        // And one that never got that far
        writeFile(root + "/catalog/staging/q/files/0/b.txt", "b");

        installer.recover();
        check(fs::exists(root + "/a.txt"), "ready install finished");
        check(installer.installed().count("p") == 1, "and recorded");
        check(!fs::exists(root + "/catalog/staging/q"), "unfinished one discarded");
    });

    run("files that aren't archives are copied", [&] {
        std::string root = makeRoot("plain-file");
        writeFile(root + "/dl/download.tmp", "<layout/>");
        Installer installer(root + "/catalog", targetsUnder(root));
        Package package = zipPackage("one", "1", "https://h.org/files/extra.xml", "layouts", "Blue");
        check(installer.install(package, { root + "/dl/download.tmp" }, error), "installed: " + error);
        checkEqual(readFile(root + "/layouts/Blue/extra.xml"), "<layout/>", "copied under its published name");
    });

    // --- Downloading -----------------------------------------------------------

    run("partial downloads are continued, kept or started again", [&] {
        check(planResume(false, 0, 100) == Resume::Fresh, "nothing there");
        check(planResume(true, 0, 100) == Resume::Fresh, "empty");
        check(planResume(true, 40, 100) == Resume::Continue, "part of it");
        check(planResume(true, 100, 100) == Resume::Complete, "all of it");
        check(planResume(true, 140, 100) == Resume::Discard, "too much");
        check(planResume(true, 40, 0) == Resume::Continue, "size unknown");
    });

    run("retries wait 2 s, doubling to 60 s", [&] {
        check(backoffDelay(1) == 2000 && backoffDelay(2) == 4000 && backoffDelay(5) == 32000, "doubling");
        check(backoffDelay(6) == 60000 && backoffDelay(20) == 60000, "capped");
        uint64_t mb = 1024 * 1024;
        check(spaceNeeded(100 * mb, 0) == 264 * mb, "download, extract and margin");
        check(spaceNeeded(100 * mb, 40 * mb) == 224 * mb, "less what's downloaded");
    });

    run("a download is verified and kept for the next try", [&] {
        std::string root = makeRoot("download");
        std::string content = "a package's bytes";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        Package package = remotePackage("p", "https://h.org/p.zip", content);

        std::vector<std::pair<uint64_t, uint64_t>> reports;
        Downloader::Result result;
        check(downloader.fetch(package, [&](uint64_t received, uint64_t total) {
            reports.push_back({ received, total });
            return true;
        }, result), "fetched: " + result.error);
        check(result.files.size() == 1, "one file");
        if (result.files.size() == 1) {
            checkEqual(readFile(result.files[0]), content, "content");
            check(fs::path(result.files[0]).extension() != ".part", "no longer partial");
        }
        check(!reports.empty() && reports.back().first == content.size() && reports.back().second == content.size(), "progress reaches the total");

        Downloader::Result again;
        check(downloader.fetch(package, nullptr, again) && http.offsets.size() == 1, "not downloaded twice");
        downloader.discard("p");
        check(!fs::exists(root + "/downloads/p"), "discarded");
    });

    run("a dropped connection resumes where it stopped", [&] {
        std::string root = makeRoot("download-resume");
        std::string content = "0123456789abcdefghij";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        http.dropAfter = { 5, 4 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        Downloader::Result result;
        check(downloader.fetch(remotePackage("p", "https://h.org/p.zip", content), nullptr, result), "fetched: " + result.error);
        check(http.offsets == std::vector<uint64_t>({ 0, 5, 9 }), "continued from 5, then 9");
        check(delays == std::vector<int>({ 2000, 4000 }), "waited between tries");
        check(result.files.size() == 1 && readFile(result.files[0]) == content, "whole file");
    });

    run("after five tries it gives up, keeping what it has", [&] {
        std::string root = makeRoot("download-give-up");
        std::string content = "0123456789";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        http.dropAfter = { 1, 1, 1, 1, 1 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        Package package = remotePackage("p", "https://h.org/p.zip", content);
        Downloader::Result result;
        check(!downloader.fetch(package, nullptr, result), "failed");
        check(http.offsets.size() == 5 && delays.size() == 4, "five tries");
        check(result.error.find("connection was lost") != std::string::npos, "says why: " + result.error);

        Downloader::Result later;
        check(downloader.fetch(package, nullptr, later), "finished later: " + later.error);
        check(http.offsets.back() == 5, "from where it stopped");
    });

    run("servers that can't resume start again", [&] {
        std::string root = makeRoot("download-no-ranges");
        std::string content = "0123456789";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        http.ranges = false;
        http.dropAfter = { 4 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        Downloader::Result result;
        check(downloader.fetch(remotePackage("p", "https://h.org/p.zip", content), nullptr, result), "fetched: " + result.error);
        check(result.files.size() == 1 && readFile(result.files[0]) == content, "whole file, not doubled");
    });

    run("a damaged download is never used", [&] {
        std::string root = makeRoot("download-damaged");
        ServingHttp http;
        http.files["https://h.org/p.zip"] = "not what was published";
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        Package package = remotePackage("p", "https://h.org/p.zip", "what was published!!!");
        package.files[0].size = 0;   // only the hash can tell
        Downloader::Result result;
        check(!downloader.fetch(package, nullptr, result), "refused");
        check(result.error.find("checksum") != std::string::npos, "says why: " + result.error);
        check(http.offsets.size() == 1, "not retried: a fresh download that's wrong stays wrong");
        check(fs::is_empty(root + "/downloads/p"), "nothing left behind");

        Package sized = remotePackage("q", "https://h.org/p.zip", "too short");
        Downloader::Result wrongSize;
        check(!downloader.fetch(sized, nullptr, wrongSize) && wrongSize.error.find("should be") != std::string::npos,
              "wrong size: " + wrongSize.error);
    });

    run("a partial joined to a changed file is downloaded again", [&] {
        std::string root = makeRoot("download-changed");
        std::string oldContent = "OLDOLDOLDO";
        std::string newContent = "new bytes!";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = oldContent;
        http.dropAfter = { 5 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        downloader.tries = 1;
        Package package = remotePackage("p", "https://h.org/p.zip", newContent);
        Downloader::Result first;
        check(!downloader.fetch(package, nullptr, first), "first try dropped");

        http.files["https://h.org/p.zip"] = newContent;
        downloader.tries = 5;
        Downloader::Result result;
        check(downloader.fetch(package, nullptr, result), "fetched: " + result.error);
        check(http.offsets == std::vector<uint64_t>({ 0, 5, 0 }), "continued, found wrong, started again");
        check(result.files.size() == 1 && readFile(result.files[0]) == newContent, "the new file");
    });

    run("a file changed on the server isn't joined to the old partial", [&] {
        std::string root = makeRoot("download-if-range");
        std::string oldContent = "OLDOLDOLDO";
        std::string newContent = "new bytes!";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = oldContent;
        http.etags["https://h.org/p.zip"] = "\"v1\"";
        http.dropAfter = { 5 };
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        downloader.tries = 1;
        Package package = remotePackage("p", "https://h.org/p.zip", newContent);
        package.files[0].sha256.clear();   // no hash to catch it: only the size, which is the same
        Downloader::Result first;
        check(!downloader.fetch(package, nullptr, first), "first try dropped");

        http.files["https://h.org/p.zip"] = newContent;
        http.etags["https://h.org/p.zip"] = "\"v2\"";
        Downloader::Result result;
        check(downloader.fetch(package, nullptr, result), "fetched: " + result.error);
        check(result.files.size() == 1 && readFile(result.files[0]) == newContent, "the whole new file");
    });

    run("downloads check for space and can be stopped", [&] {
        std::string root = makeRoot("download-space");
        std::string content = "0123456789";
        ServingHttp http;
        http.files["https://h.org/p.zip"] = content;
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        downloader.freeSpace = [](const std::string&, uint64_t& available) {
            available = 1000;
            return true;
        };
        Package package = remotePackage("p", "https://h.org/p.zip", content);
        Downloader::Result full;
        check(!downloader.fetch(package, nullptr, full), "refused");
        check(full.error.find("Not enough free space") != std::string::npos && http.offsets.empty(), "before downloading: " + full.error);

        Downloader stoppable = testDownloader(&http, root + "/downloads", delays);
        Downloader::Result stopped;
        check(!stoppable.fetch(package, [](uint64_t, uint64_t) { return false; }, stopped) && stopped.cancelled, "stopped");
    });

    run("folder source files are used where they are", [&] {
        std::string root = makeRoot("download-local");
        writeFile(root + "/share/p.zip", "zip bytes");
        std::vector<int> delays;
        Downloader downloader = testDownloader(nullptr, root + "/downloads", delays);
        Package package = remotePackage("p", root + "/share/p.zip", "zip bytes");
        Downloader::Result result;
        check(downloader.fetch(package, nullptr, result), "fetched: " + result.error);
        check(result.files.size() == 1 && result.files[0] == root + "/share/p.zip", "in place");
        check(!fs::exists(root + "/downloads/p"), "nothing copied");

        Package missing = remotePackage("m", root + "/share/gone.zip", "x");
        Downloader::Result gone;
        check(!downloader.fetch(missing, nullptr, gone) && gone.error.find("Can't find") != std::string::npos, "missing: " + gone.error);

        Package remote = remotePackage("r", "https://h.org/r.zip", "x");
        Downloader::Result unsupported;
        check(!downloader.fetch(remote, nullptr, unsupported) && !unsupported.error.empty(), "no HTTP: " + unsupported.error);
    });

    run("old versions' downloads are cleared out", [&] {
        std::string root = makeRoot("download-stale");
        writeFile(root + "/downloads/p/0-oldversion.part", "stale");
        ServingHttp http;
        http.files["https://h.org/p.zip"] = "fresh";
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/downloads", delays);
        Downloader::Result result;
        check(downloader.fetch(remotePackage("p", "https://h.org/p.zip", "fresh"), nullptr, result), "fetched: " + result.error);
        check(!fs::exists(root + "/downloads/p/0-oldversion.part"), "stale partial removed");
    });

    run("downloaded archives install", [&] {
        std::string root = makeRoot("download-install");
        makeZip(root + "/server/pack.zip", { { "settings.conf", "list.extensions = zip" } });
        std::string zip = readFile(root + "/server/pack.zip");
        ServingHttp http;
        http.files["https://h.org/packs/pack.zip"] = zip;
        std::vector<int> delays;
        Downloader downloader = testDownloader(&http, root + "/catalog/downloads", delays);
        Package package = remotePackage("pack", "https://h.org/packs/pack.zip", zip);
        package.files[0].target = "collections";
        package.files[0].path = "Pack";
        Downloader::Result result;
        check(downloader.fetch(package, nullptr, result), "fetched: " + result.error);
        Installer installer(root + "/catalog", targetsUnder(root));
        check(installer.install(package, result.files, error), "installed: " + error);
        checkEqual(readFile(root + "/collections/Pack/settings.conf"), "list.extensions = zip", "installed content");
    });

    // --- The Catalog service and its screens ---------------------------------------

    // A catalog folder with a local index offering two packages, one needing the other
    auto makeCatalog = [&](const std::string& name) {
        std::string root = makeRoot(name);
        makeZip(root + "/share/base.zip", { { "base.txt", "base" } });
        makeZip(root + "/share/pack.zip", { { "pack.txt", "pack" } });
        writeFile(root + "/share/index.json", R"({"packages": [
            {"id": "base", "name": "Base", "version": "1.0", "files": [{"url": "base.zip", "target": "root"}]},
            {"id": "pack", "name": "Pack", "version": "2.0", "kind": "media",
             "requires": [{"id": "base", "version": ">=1.0"}],
             "files": [{"url": "pack.zip", "target": "root"}]}
        ]})");
        return root;
    };
    auto localSource = [](const std::string& root) {
        SourceConfig source;
        source.name = "local";
        source.type = "index";
        source.url = root + "/share/index.json";
        return source;
    };

    run("the service lists, installs and removes packages", [&] {
        std::string root = makeCatalog("service");
        Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        service.waitUntilIdle();
        check(service.entries().empty(), "no sources, no packages");

        check(service.addSource(localSource(root), error), "added: " + error);
        service.waitUntilIdle();
        check(fs::exists(root + "/catalog/sources.conf"), "sources saved");
        std::vector<Entry> entries = service.entries();
        check(entries.size() == 2, "two packages");

        bool ok = true;
        std::string message;
        service.install("pack");
        service.waitUntilIdle();
        check(service.outcome("pack", ok, message) && !ok && message == "Install Base >=1.0 first.", "needs base: " + message);

        service.install("base");
        service.waitUntilIdle();
        check(service.outcome("base", ok, message) && ok, "installed: " + message);
        checkEqual(readFile(root + "/base.txt"), "base", "file installed");
        Entry base;
        check(service.entry("base", base) && base.installedVersion == "1.0" && !base.updateAvailable, "shown as installed");
        Service::Changes changes;
        check(service.takeChanges(changes) && !service.takeChanges(changes), "changes reported once");

        service.install("pack");
        service.waitUntilIdle();
        check(service.outcome("pack", ok, message) && ok, "then pack installs: " + message);

        service.remove("base");
        service.waitUntilIdle();
        check(service.outcome("base", ok, message) && ok && !fs::exists(root + "/base.txt"), "removed: " + message);
        check(service.installed().size() == 1, "one left");

        std::string sourceError;
        check(!service.addSource(localSource(root), sourceError), "the same name twice is refused");
        check(service.removeSource("local", error), "source removed: " + error);
        service.waitUntilIdle();
        check(service.entries().empty(), "its packages are gone");
    });

    run("installed collections are reported for the Main menu", [&] {
        std::string root = makeRoot("service-collections");
        makeZip(root + "/share/demo.zip", { { "settings.conf", "list.extensions = txt" } });
        writeFile(root + "/share/index.json", R"({"packages": [
            {"id": "demo", "name": "Demo", "version": "1", "kind": "collection",
             "files": [{"url": "demo.zip", "target": "collections", "path": "Demo"}]}]})");
        Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        bool added = service.addSource(localSource(root), error);
        check(added, "source: " + error);
        service.waitUntilIdle();
        service.install("demo");
        service.waitUntilIdle();
        Service::Changes changes;
        check(service.takeChanges(changes) && changes.addedCollections == std::set<std::string>({ "Demo" }), "Demo added");
        service.remove("demo");
        service.waitUntilIdle();
        check(service.takeChanges(changes) && changes.removedCollections == std::set<std::string>({ "Demo" })
              && changes.addedCollections.empty(), "Demo removed");

        Configuration::absolutePath = root;
        writeFile(root + "/collections/Main/menu.txt", "Arcade\r\n# mine\r\n");
        check(Setup::addToMainMenu("Demo", error) && Setup::addToMainMenu("Demo", error), "added twice: " + error);
        checkEqual(readFile(root + "/collections/Main/menu.txt"), "Arcade\r\n# mine\r\nDemo\r\n", "listed once, with the file's line endings");
        check(Setup::removeFromMainMenu("Demo", error), "removed: " + error);
        checkEqual(readFile(root + "/collections/Main/menu.txt"), "Arcade\r\n# mine\r\n", "other lines kept");
    });

    run("another source offering the same package can't replace it", [&] {
        std::string root = makeCatalog("service-other-source");
        Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        bool added = service.addSource(localSource(root), error);
        check(added, "source: " + error);
        service.waitUntilIdle();
        service.install("base");
        service.waitUntilIdle();

        // The first source goes away; another offers a newer "base"
        makeZip(root + "/other/base.zip", { { "base.txt", "someone else's" } });
        writeFile(root + "/other/index.json", R"({"packages": [{"id": "base", "name": "Base", "version": "9", "files": [{"url": "base.zip", "target": "root"}]}]})");
        check(service.removeSource("local", error), "removed: " + error);
        SourceConfig other = localSource(root);
        other.name = "other";
        other.url = root + "/other/index.json";
        added = service.addSource(other, error);
        check(added, "other: " + error);
        service.waitUntilIdle();
        Entry entry;
        check(service.entry("base", entry) && !entry.updateAvailable && entry.installedSource == "local", "no update offered");
        service.install("base");
        service.waitUntilIdle();
        bool ok = true;
        std::string message;
        check(service.outcome("base", ok, message) && !ok && message.find("Remove it first") != std::string::npos, "refused: " + message);
        checkEqual(readFile(root + "/base.txt"), "base", "files untouched");
    });

    run("the service reports unreachable sources and keeps sources across restarts", [&] {
        std::string root = makeCatalog("service-restart");
        {
            Service service(root + "/catalog", targetsUnder(root), nullptr);
            service.start();
            writeFile(root + "/usb/index.json", R"({"packages": []})");
            SourceConfig missing = localSource(root);
            missing.name = "usb";
            missing.url = root + "/usb/index.json";
            bool added = service.addSource(missing, error);
            check(added, "added: " + error);
            added = service.addSource(localSource(root), error);
            check(added, "added: " + error);
            service.waitUntilIdle();
            fs::remove_all(root + "/usb");   // the stick was taken out
            service.refresh();
            service.waitUntilIdle();
            check(!service.warnings().empty(), "the missing one is reported");
        }
        Service again(root + "/catalog", targetsUnder(root), nullptr);
        again.start();
        again.waitUntilIdle();
        check(again.sources().size() == 2 && again.entries().size() == 2, "read again on start");
    });

    run("the package and sources screens are bound to the service", [&] {
        std::string root = makeCatalog("service-model");
        Configuration::absolutePath = root;
        Configuration config;
        Service service(root + "/catalog", targetsUnder(root), nullptr);
        service.start();
        ManagerModel model(config);
        model.setCatalog(&service);

        checkEqual(model.get("info:hasSources"), "no", "no sources yet");
        checkEqual(model.actionProblem("source.add"), "Give the source a name.", "name first");
        model.set("draft:sourceName", "local");
        model.set("draft:sourceType", "index");
        model.set("draft:sourceUrl", root + "/share/index.json");
        checkEqual(model.actionProblem("source.add"), "Confirm that you trust this source.", "trust asked for");
        model.set("draft:sourceTrusted", "yes");
        Forms::Model::Result added = model.runAction("source.add");
        check(added.ok, "added: " + added.message);
        service.waitUntilIdle();
        checkEqual(model.get("info:catalogStatus"), "2 packages from 1 source.", "status");
        checkEqual(model.get("draft:removeSource"), "local", "removal starts on the first source");

        model.setScreen("package", { { "id", "base" } });
        checkEqual(model.get("package:name"), "Base", "name");
        checkEqual(model.get("package:canInstall"), "yes", "can install");
        checkEqual(model.get("package:canRemove"), "no", "nothing to remove");
        check(model.get("package:checksum").find("no checksums") != std::string::npos, "unchecked downloads are pointed out");
        check(model.runAction("package.install").ok, "install started");
        service.waitUntilIdle();
        checkEqual(model.get("package:progress"), "Installed Base 1.0.", "outcome shown");
        checkEqual(model.get("package:installedStatus"), "", "installed, which the outcome already says");
        checkEqual(model.actionProblem("package.install"), "Installed, and up to date.", "nothing to do");
        checkEqual(model.get("package:canRemove"), "yes", "can remove");
        check(model.list("info:installed") == std::vector<std::string>({ "Base 1.0" }), "installed list");

        model.setScreen("package", { { "id", "pack" } });
        checkEqual(model.get("package:requires"), "Needs Base >=1.0", "requirements");

        Forms::Model::Result opened = model.runAction("open.sources");
        check(opened.ok && opened.open == "sources" && !opened.back, "open.sources opens the Sources screen");
    });

    // --- Real HTTP, against a test server ----------------------------------------
    // Start Tests/test_server.py and set RETROFE_TEST_SERVER to its address
    // (e.g. http://127.0.0.1:8765) to run these.
    // The server serves 4 MB at /file.bin, the same cut off once at /drop/,
    // ignoring ranges at /norange/, after two 503s at /flaky/, and an index at /index.json.

    const char* server = std::getenv("RETROFE_TEST_SERVER");
    if (!server || !*server) {
        return;
    }
    std::unique_ptr<Http> http = createHttp();
    check(http != nullptr, "this platform has HTTP");
    if (!http) return;
    std::string base = server;

    Package big;
    run("a real index is read", [&] {
        std::string root = makeRoot("live-index");
        SourceConfig source;
        source.name = "live";
        source.type = "index";
        source.url = base + "/index.json";
        SourceContents contents = readSource(source, root + "/cache", http.get());
        check(contents.error.empty() && contents.packages.size() == 1, "one package: " + contents.error);
        if (contents.packages.size() == 1) {
            big = contents.packages[0];
            checkEqual(big.files[0].url, base + "/file.bin", "url resolved");
        }
        std::string body;
        check(!http->get(base + "/missing.json", body, error) && error.find("404") != std::string::npos, "404: " + error);
    });
    if (big.files.empty()) return;

    auto liveFetch = [&](const std::string& name, const std::string& url, Downloader::Result& result, int& calls) {
        Package package = big;
        package.id = name;
        package.files[0].url = url;
        Downloader downloader(http.get(), makeRoot(name) + "/downloads");
        calls = 0;
        return downloader.fetch(package, [&](uint64_t, uint64_t) { ++calls; return true; }, result);
    };

    for (const char* path : { "/file.bin", "/drop/file.bin", "/norange/file.bin", "/flaky/file.bin" }) {
        run(std::string("a real download: ") + path, [&] {
            Downloader::Result result;
            int calls = 0;
            check(liveFetch("live" + std::to_string(std::string(path).size()), base + path, result, calls), "fetched: " + result.error);
            check(result.files.size() == 1 && sha256File(result.files[0]) == big.files[0].sha256, "verified");
            check(calls > 1, "progress reported");
        });
    }

    run("a real download that isn't there", [&] {
        Downloader::Result result;
        int calls = 0;
        check(!liveFetch("live-missing", base + "/nothing.bin", result, calls), "failed");
        check(result.error.find("404") != std::string::npos, "not retried, says why: " + result.error);
    });
}

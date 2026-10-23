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
#include "../Source/Catalog/Hash.h"
#include "../Source/Catalog/Http.h"
#include "../Source/Catalog/Index.h"
#include "../Source/Catalog/Installer.h"
#include "../Source/Catalog/Package.h"
#include "../Source/Catalog/Sources.h"
#include "minizip/zip.h"
#include <algorithm>
#include <filesystem>
#include <map>
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

        bool download(const std::string&, const std::string&, uint64_t, const Progress&, bool&, std::string& error) override
        {
            error = "not used";
            return false;
        }
    };

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
        checkEqual(resolveLocation("C:\\packs\\catalog.json", "art\\a.png"), (fs::path("C:\\packs") / "art\\a.png").lexically_normal().string(), "file path");
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
        int kept = 0;
        check(installer.remove("p", kept, error), "removed: " + error);
        check(!fs::exists(root + "/media/Pack/x/a.txt"), "file removed");
        check(fs::exists(root + "/media/Pack/x/b.txt") && kept == 1, "changed file kept");
        check(installer.installed().empty(), "manifest removed");
        check(!installer.remove("p", kept, error), "removing again fails");
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
}

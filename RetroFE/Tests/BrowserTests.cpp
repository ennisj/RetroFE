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
// The file and folder browser behind <folder> and <file> fields: listing and
// filtering (PathBrowser), starting places, what a field gets back
// (relative or absolute), and the browse: provider driving the file-browser form.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Forms/Form.h"
#include "../Source/Manager/Browser/BrowserProvider.h"
#include "../Source/Manager/Browser/PathBrowser.h"
#include "../Source/Manager/ManagerModel.h"
#include <filesystem>
#include <memory>

using namespace Test;
namespace fs = std::filesystem;

namespace
{
    std::string joined(const std::vector<std::string>& items)
    {
        std::string text;
        for (const std::string& item : items) {
            text += (text.empty() ? "" : ",") + item;
        }
        return text;
    }

    std::string names(const Browser::Listing& listing)
    {
        std::vector<std::string> all;
        for (const Browser::Entry& entry : listing.entries) {
            all.push_back(entry.name + (entry.folder ? "/" : ""));
        }
        return joined(all);
    }

    std::string native(const std::string& generic)
    {
        fs::path p(generic);
        p.make_preferred();
        return p.string();
    }

    // A root with a roms folder, media, an emulator and some clutter
    std::string makeInstall(const std::string& name)
    {
        std::string root = makeRoot(name);
        writeFile(root + "/roms/nes/mario.zip", "x");
        writeFile(root + "/roms/nes/zelda.ZIP", "x");
        writeFile(root + "/roms/nes/readme.txt", "x");
        writeFile(root + "/roms/snes/f-zero.zip", "x");
        writeFile(root + "/roms/empty/.keep", "");
        writeFile(root + "/emulators/fake/fake.exe", "x");
        writeFile(root + "/emulators/fake/fake.bat", "x");
        writeFile(root + "/emulators/fake/notes.txt", "x");
        writeFile(root + "/collections/Arcade/medium_artwork/video/Sonic.mp4", "xxxx");
        writeFile(root + "/collections/Arcade/medium_artwork/logo/Sonic.png", "x");
        writeFile(root + "/.hidden/secret.zip", "x");
        return root;
    }

    struct Fixture
    {
        explicit Fixture(const std::string& root)
        {
            Configuration::absolutePath = root;
            config = std::make_unique<Configuration>();
            model = std::make_unique<ManagerModel>(*config);
            Manager::Services services{ *config };
            auto made = std::make_unique<Browser::BrowserProvider>(services);
            provider = made.get();
            provider->setRoot(root);
            provider->setPlaceSources([] { return std::vector<Browser::Drive>{}; },
                                      [] { return std::vector<Browser::Place>{}; });
            model->addProvider(std::move(made));
        }

        void show(const Forms::Params& params)
        {
            this->params = params;
            model->setScreen(Browser::formId, params);
        }

        const Forms::Row* row(const std::string& list, const std::string& id)
        {
            rows = model->rows("browse:" + list, 200);
            for (const Forms::Row& item : rows.items) {
                if (item.id == id) {
                    return &item;
                }
            }
            return nullptr;
        }

        std::unique_ptr<Configuration> config;
        std::unique_ptr<ManagerModel> model;
        Browser::BrowserProvider* provider = nullptr;
        Forms::Params params;
        Forms::Rows rows;
    };

    Forms::Params request(bool folder, const std::string& bind, const std::string& pattern, const std::string& value,
                          const std::string& start = "", const std::string& matching = "")
    {
        Browser::Request r;
        r.bind = bind;
        r.folder = folder;
        r.label = folder ? "Games folder" : "Program";
        r.filterName = folder ? "" : "Programs";
        r.pattern = pattern;
        r.value = value;
        r.start = start;
        r.matching = matching;
        return Browser::screenParams(r);
    }
}

static Test::Suite browserSuite("browser", [] {
    run("browser: extensions come from a pattern or a list, lower case and without dots", [] {
        checkEqual(joined(Browser::parseExtensions("*.exe;*.bat;*.cmd")), "exe,bat,cmd", "a field's pattern");
        checkEqual(joined(Browser::parseExtensions("zip, 7z")), "zip,7z", "a system's extensions");
        checkEqual(joined(Browser::parseExtensions(".ZIP .zip,*.Zip")), "zip", "dots, case and duplicates");
        checkEqual(joined(Browser::parseExtensions("*.*")), "", "any file");
        checkEqual(joined(Browser::parseExtensions("")), "", "nothing");
        check(Browser::matches("Mario.ZIP", { "zip" }), "case doesn't matter");
        check(!Browser::matches("zip", { "zip" }), "a name without an extension");
        check(!Browser::matches(".zip", { "zip" }), "a dot file has no extension");
        check(Browser::matches("anything", {}), "no extensions: every file");
        checkEqual(Browser::extensionList({ "exe", "bat", "cmd" }), "exe, bat or cmd", "listed for people");
    });

    run("browser: a folder lists folders first, then matching files, without hidden ones", [] {
        std::string root = makeInstall("browser_list");
        Browser::Listing listing = Browser::listFolder(root, {}, true);
        check(listing.ok, "the root is read");
        checkEqual(names(listing), "collections/,emulators/,roms/", "folders only (no files at the top), .hidden left out");
        Browser::Listing nes = Browser::listFolder(fs::path(root) / "roms" / "nes", { "zip" }, true);
        checkEqual(names(nes), "mario.zip,zelda.ZIP", "matching files, any case, by name");
        check(nes.files == 3 && nes.matching == 2 && nes.folders == 0, "every file is counted");
        Browser::Listing foldersOnly = Browser::listFolder(fs::path(root) / "roms", { "zip" }, false);
        checkEqual(names(foldersOnly), "empty/,nes/,snes/", "a folder field lists folders only");
        Browser::Listing missing = Browser::listFolder(fs::path(root) / "nope", {}, true);
        check(!missing.ok && !missing.error.empty(), "a missing folder says why");
        check(Browser::isHidden("$RECYCLE.BIN") && Browser::isHidden("System Volume Information") && !Browser::isHidden("roms"),
              "Windows' own folders are hidden");
    });

    run("browser: folders say what they hold", [] {
        std::string root = makeInstall("browser_count");
        Browser::Count nes = Browser::countFolder(fs::path(root) / "roms" / "nes", { "zip" });
        checkEqual(Browser::countText(nes, { "zip" }), "2 files matching zip", "matching files");
        Browser::Count roms = Browser::countFolder(fs::path(root) / "roms", { "zip" });
        checkEqual(Browser::countText(roms, { "zip" }), "3 folders", "only folders: how many");
        Browser::Count fake = Browser::countFolder(fs::path(root) / "emulators" / "fake", { "zip" });
        checkEqual(Browser::countText(fake, { "zip" }), "No files matching zip", "files, none matching");
        checkEqual(Browser::countText(fake, {}), "3 files", "without extensions: files");
        checkEqual(Browser::countText(roms, {}), "3 folders", "without extensions: folders");
        Browser::Count empty = Browser::countFolder(fs::path(root) / "roms" / "empty", {});
        checkEqual(Browser::countText(empty, {}), "Empty", "an empty folder (its dot file aside)");
        checkEqual(Browser::countText(Browser::Count{}, {}), "Can't be read", "a folder that can't be read");
        Browser::Count capped = Browser::countFolder(fs::path(root) / "roms" / "nes", { "zip" }, 1);
        check(capped.capped, "counting stops at the cap");
        checkEqual(Browser::sizeText(512), "512 bytes", "bytes");
        checkEqual(Browser::sizeText(1536), "1.5 KB", "kilobytes");
        checkEqual(Browser::sizeText(35 * 1024 * 1024), "35 MB", "megabytes");
        check(Browser::isPicture("a.PNG") && !Browser::isPicture("a.mp4"), "pictures get thumbnails");
    });

    run("browser: values relative to RetroFE's folder stay relative, others are absolute", [] {
        std::string root = makeInstall("browser_paths");
        fs::path exe = fs::path(root) / "emulators" / "fake" / "fake.exe";
        check(Browser::isRelativeStyle("emulators/x.exe") && Browser::isRelativeStyle("%RETROFE_PATH%/x.exe")
              && !Browser::isRelativeStyle(root + "/x.exe") && !Browser::isRelativeStyle("") && !Browser::isRelativeStyle("%CMD%"),
              "which values are relative");
        checkEqual(Browser::storedValue(exe, root, "emulators/old/old.exe"), "emulators/fake/fake.exe", "relative stays relative, same separator");
        checkEqual(Browser::storedValue(exe, root, "emulators\\old.exe"), "emulators\\fake\\fake.exe", "backslashes kept");
        checkEqual(Browser::storedValue(exe, root, "%RETROFE_PATH%/old.exe"), "%RETROFE_PATH%/emulators/fake/fake.exe",
                   "%RETROFE_PATH% kept");
        checkEqual(Browser::storedValue(exe, root, ""), exe.lexically_normal().make_preferred().string(), "an empty field gets the absolute path");
        checkEqual(Browser::storedValue(exe, root, "C:/elsewhere/old.exe"), exe.lexically_normal().make_preferred().string(),
                   "an absolute value stays absolute");
        fs::path outside = fs::path(root).parent_path() / "other" / "x.exe";
        checkEqual(Browser::storedValue(outside, root, "emulators/old.exe"), outside.lexically_normal().make_preferred().string(),
                   "outside RetroFE's folder: absolute even for a relative value");
        checkEqual(Browser::resolve("emulators/fake/fake.exe", root).string(), exe.lexically_normal().string(), "relative values resolve from the root");
        checkEqual(Browser::resolve("\"%RETROFE_PATH%\\emulators\\fake\\fake.exe\"", root).string(), exe.lexically_normal().string(),
                   "quoted %RETROFE_PATH% values too");
        checkEqual(Browser::displayPath(exe.parent_path(), root, true), native("emulators/fake"), "shown relative for a relative field");
        checkEqual(Browser::displayPath(root, root, true), "RetroFE's folder", "the root itself");
        checkEqual(Browser::displayPath(exe.parent_path(), root, false), exe.parent_path().lexically_normal().make_preferred().string(),
                   "shown absolute otherwise");
        fs::path rest;
        check(Browser::insideRoot(fs::path(root) / "roms", root, &rest) && rest == fs::path("roms"), "inside the root");
        check(!Browser::insideRoot(fs::path(root).parent_path(), root), "its parent isn't");
#ifdef _WIN32
        std::string upper = root;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        check(Browser::insideRoot(fs::path(upper) / "roms", root), "case doesn't matter on Windows");
#endif
        checkEqual(Browser::shortPath("D:\\Games\\collections\\Arcade\\medium_artwork\\video", 34), "D:\\...\\Arcade\\medium_artwork\\video",
                   "a long path keeps its drive and last folders");
        checkEqual(Browser::shortPath("D:\\roms", 34), "D:\\roms", "a short path stays");
    });

    run("browser: a browse opens where the value is, else at the field's start", [] {
        std::string root = makeInstall("browser_open");
        fs::path video = fs::path(root) / "collections" / "Arcade" / "medium_artwork" / "video";
        checkEqual(Browser::valueFolder((video / "Sonic.mp4").string(), false, root).string(), video.lexically_normal().string(),
                   "a file field: the file's folder");
        checkEqual(Browser::valueFolder((video / "Sonic.*").string(), false, root).string(), video.lexically_normal().string(),
                   "a slot's place: its folder");
        checkEqual(Browser::valueFolder("roms/nes", true, root).string(), (fs::path(root) / "roms" / "nes").lexically_normal().string(),
                   "a folder field: the folder, relative to the root");
        checkEqual(Browser::openingFolder("", (video / "Sonic.*").string(), false, root).string(), video.lexically_normal().string(),
                   "empty field: the slot's folder");
        fs::path missing = fs::path(root) / "collections" / "Arcade" / "medium_artwork" / "screenshot" / "Sonic.*";
        checkEqual(Browser::openingFolder("", missing.string(), false, root).string(),
                   (fs::path(root) / "collections" / "Arcade" / "medium_artwork").lexically_normal().string(),
                   "a slot folder that isn't there yet: the nearest one that is");
        checkEqual(Browser::openingFolder("emulators/fake/fake.exe", (video / "Sonic.*").string(), false, root).string(),
                   (fs::path(root) / "emulators" / "fake").lexically_normal().string(), "the value wins over the start");
        check(Browser::openingFolder("", "", true, root).empty(), "neither: the starting places");
        check(Browser::openingFolder((fs::path(root) / "gone").string(), "", true, root).empty(), "a value that isn't there: the places");
    });

    run("browser: starting places come in order, each folder once", [] {
        std::string root = makeInstall("browser_places");
        Browser::PlacesInput input;
        input.root = root;
        input.value = (fs::path(root) / "roms" / "nes").string();
        input.folderField = true;
        input.start = (fs::path(root) / "roms" / "nes").string();   // the same folder: once
        input.recent = { (fs::path(root) / "roms" / "snes").string(), root, (fs::path(root) / "gone").string() };
        Browser::Place downloads;
        downloads.path = fs::path(root) / "emulators";
        downloads.detail = "Downloads";
        input.userFolders = { downloads };
        Browser::Drive usb;
        usb.path = "U:\\";
        usb.name = "STICK";
        usb.kind = Browser::Drive::Kind::Removable;
        usb.free = 2048;
        input.drives = { usb };
        std::vector<Browser::Place> places = Browser::startingPlaces(input);
        std::vector<std::string> details;
        for (const Browser::Place& place : places) details.push_back(place.detail);
        checkEqual(joined(details), "Where it is now,RetroFE's folder,Chosen before,Downloads,Removable drive, 2.0 KB free",
                   "value, root, recent (missing ones and repeats left out), user folders, drives");
        checkEqual(places.back().label, "U:\\  STICK", "a drive shows its label");

        std::vector<std::string> recent;
        Browser::remember(recent, fs::path(root) / "a", 2);
        Browser::remember(recent, fs::path(root) / "b", 2);
        Browser::remember(recent, fs::path(root) / "a", 2);
        Browser::remember(recent, fs::path(root) / "c", 2);
        check(recent.size() == 2 && fs::path(recent[0]).filename() == "c" && fs::path(recent[1]).filename() == "a",
              "recent picks: newest first, no repeats, at most max");

        std::vector<Browser::Entry> entries(3);
        entries[0].name = "Mario.zip";
        entries[1].name = "Zelda.zip";
        entries[2].name = "Super Mario.zip";
        check(Browser::filterEntries(entries, "mario").size() == 2 && Browser::filterEntries(entries, "").size() == 3,
              "a search matches part of the name, case aside");
    });

    run("browser: letter jump moves between groups of first letters", [] {
        std::vector<std::string> labels = { "..", "alpha", "apple", "Banana", "berry", "cherry", "1942" };
        check(Forms::letterJump(labels, 1, 1) == 3, "a to b");
        check(Forms::letterJump(labels, 3, 1) == 5, "b to c");
        check(Forms::letterJump(labels, 0, 1) == 1, "from .. to the first letter");
        check(Forms::letterJump(labels, 6, 1) == 6, "nowhere after the last");
        check(Forms::letterJump(labels, 4, -1) == 3, "up: the start of its own group first");
        check(Forms::letterJump(labels, 3, -1) == 1, "then the previous group's start");
        check(Forms::letterJump(labels, 1, -1) == 1, "nothing before the first group but ..");
        check(Forms::letterJump({}, 0, 1) == 0, "an empty list");
    });

    run("browser: fields' start, matching and jump attributes are read", [] {
        std::string error;
        auto form = Forms::parse(R"(<form id="f">
            <folder label="Games" bind="draft:a" matching="draft:exts"/>
            <file label="Art" bind="draft:b" start="draft:folder" pattern="*.png"/>
            <results bind="draft:c" open="x" jump="letter"/>
          </form>)", error);
        check(form && form->fields.size() == 3, "parsed");
        if (form && form->fields.size() == 3) {
            checkEqual(form->fields[0].matching, "draft:exts", "matching");
            checkEqual(form->fields[1].start, "draft:folder", "start");
            check(form->fields[2].letterJump && !form->fields[0].letterJump, "jump=letter on results");
        }
        check(Forms::builtinForm(Browser::formId) != nullptr, "the browser's form is built in");
    });

    run("browser: the starting places screen, then a folder of folders for a folder field", [] {
        std::string root = makeInstall("browser_provider_folder");
        Fixture f(root);
        Forms::Params first = request(true, "system:addFolder", "", "", "", "zip");
        f.show(first);
        checkEqual(f.model->get("browse:title"), "Choose a folder", "title");
        checkEqual(f.model->get("browse:isTop"), "yes", "the places");
        const Forms::Row* rootRow = f.row("places", fs::path(root).lexically_normal().make_preferred().string());
        check(rootRow && rootRow->detail == "RetroFE's folder", "RetroFE's folder is a place");
        if (!rootRow) return;
        check(rootRow->action.rfind("browse.enter:", 0) == 0, "choosing a place enters it");

        Forms::Model::Result entered = f.model->runAction(rootRow->action);
        check(entered.ok && entered.open == Browser::formId && entered.keepParams && !entered.replace
              && entered.params["level"] == "1", "entering pushes a screen one level down");
        f.show(Forms::openParams(first, entered));
        check(f.model->get("browse:canUseFolder") == "yes", "Use this folder is offered");
        f.row("entries", "");
        check(f.rows.items.size() == 4 && f.rows.items[0].id == "..", "up, then the three folders (no files for a folder field)");

        Forms::Params roms = first;
        roms["dir"] = (fs::path(root) / "roms").string();
        roms["level"] = "2";
        f.show(roms);
        const Forms::Row* nes = f.row("entries", "nes");
        check(nes && nes->detail.empty(), "a folder's count comes later");
        f.provider->countAll();
        nes = f.row("entries", "nes");
        check(nes && nes->detail == "2 files matching zip", "then says how many files match");
        checkEqual(f.model->get("browse:summary"), "3 folders", "the folder's own summary");
        Forms::Model::Result up = f.model->runAction("browse.up");
        check(up.ok && up.replace && up.params["dir"] == fs::path(root).lexically_normal().string(), "up replaces this screen with the parent");

        Forms::Params nesScreen = first;
        nesScreen["dir"] = (fs::path(root) / "roms" / "nes").string();
        f.show(nesScreen);
        checkEqual(f.model->get("browse:summary"), "2 files matching zip", "inside: what it holds");
        Forms::Model::Result use = f.model->runAction("browse.use");
        check(use.ok && use.pickFor == "system:addFolder", "Use this folder picks for the field");
        checkEqual(use.pick, (fs::path(root) / "roms" / "nes").lexically_normal().make_preferred().string(),
                   "an empty field gets the absolute path, as the system dialog gave");
        check(!Browser::BrowserProvider::recent().empty(), "the folder is remembered");
        checkEqual(f.model->get("browse:hasSystemDialog"), "no", "no system dialog in tests");
    });

    run("browser: a file field lists matching files and picks one, keeping a relative value relative", [] {
        std::string root = makeInstall("browser_provider_file");
        Fixture f(root);
        Forms::Params first = request(false, "emulator:editExecutable", "*.exe;*.bat", "emulators/old/old.exe");
        Forms::Params folder;
        check(!Browser::openingParams(first, root, folder), "a value whose folder is gone starts at the places");
        first["value"] = "emulators/fake/missing.exe";
        check(Browser::openingParams(first, root, folder) && folder["level"] == "1"
              && fs::path(folder["dir"]) == (fs::path(root) / "emulators" / "fake").lexically_normal(),
              "a value's folder that is there: the browse opens in it");
        f.show(folder);
        checkEqual(f.model->get("browse:where"), native("emulators/fake"), "shown relative, like the value");
        checkEqual(f.model->get("browse:note"), "Showing folders and Programs (exe or bat). Choose one to use it.", "what is shown");
        checkEqual(f.model->get("browse:canUseFolder"), "no", "no Use this folder for a file");
        const Forms::Row* exe = f.row("entries", "fake.exe");
        check(exe != nullptr && f.row("entries", "notes.txt") == nullptr, "only matching files");
        exe = f.row("entries", "fake.exe");
        if (!exe) return;
        check(!exe->detail.empty() && exe->image.empty(), "a size and date, no thumbnail for a program");
        Forms::Model::Result picked = f.model->runAction(exe->action);
        check(picked.ok && picked.pickFor == "emulator:editExecutable", "the file is picked for the field");
        checkEqual(picked.pick, "emulators/fake/fake.exe", "relative, like the value it replaces");
    });

    run("browser: media files show thumbnails, and a large folder offers a search", [] {
        std::string root = makeInstall("browser_provider_media");
        for (int i = 0; i < 205; i++) {
            writeFile(root + "/collections/Arcade/medium_artwork/logo/game" + std::to_string(1000 + i) + ".png", "x");
        }
        Fixture f(root);
        Forms::Params first = request(false, "game:slot:newFile", "*.png;*.jpg", "",
                                      root + "/collections/Arcade/medium_artwork/logo/Sonic.*");
        Forms::Params folder;
        check(Browser::openingParams(first, root, folder), "an empty media field opens in its slot's folder");
        f.show(folder);
        const Forms::Row* sonic = f.row("entries", "Sonic.png");
        check(f.rows.total == 207, "up, then every picture counted");
        check(f.rows.items.size() == 200, "but 200 shown");
        checkEqual(f.model->get("browse:canSearch"), "yes", "a search for the rest");
        f.model->set("browse:query", "soni");
        sonic = f.row("entries", "Sonic.png");
        check(sonic && f.rows.total == 2, "the search finds it (up stays)");
        if (sonic) {
            check(fs::path(sonic->image) == fs::path(root) / "collections" / "Arcade" / "medium_artwork" / "logo" / "Sonic.png",
                  "a picture's row shows it");
        }
        f.model->set("browse:query", "zzz");
        checkEqual(f.model->get("browse:emptyNote"), "Nothing here matches the search.", "nothing found");
    });
});

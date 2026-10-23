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

// Tests for the Menus section (Manager/Menus): the menu tree, details,
// entries, sources, info and artwork, creating and deleting menus, through
// ManagerModel as the screens use it, on small installs in temp folders.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Forms/Form.h"
#include "../Source/Library/Backups.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Menus/MenuProvider.h"
#include "../Source/Utility/Utils.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;

namespace
{
    // Main (menu.txt): Consoles, # comment, Arcade, Favs, Ghost (no folder), nes (folder is NES)
    // Consoles (menu.txt): NES, SNES, Main (a loop back up)
    // Folders (menu/ folder, listed by no menu): Arcade, NES
    // Spare (empty menu.txt, listed by no menu)
    // Arcade, NES, SNES: systems.  Favs: merged, Arcade.sub (all), NES.sub (mario), a logo
    std::string makeInstall(const std::string& name)
    {
        std::string root = makeRoot(name);
        writeFile(root + "/settings.conf", "firstCollection = Main\n");
        writeFile(root + "/collections/Main/menu.txt", "Consoles\r\n# comment\r\nArcade\r\nFavs\r\nGhost\r\nnes\r\n");
        writeFile(root + "/collections/Main/settings.conf", "list.menuSort = yes\n");
        writeFile(root + "/collections/Consoles/menu.txt", "NES\nSNES\nMain\n");
        writeFile(root + "/collections/Folders/menu/Arcade.txt", "");
        writeFile(root + "/collections/Folders/menu/NES.txt", "");
        writeFile(root + "/collections/Spare/menu.txt", "");
        writeFile(root + "/collections/Spare/info.conf", "title = Spare\n");
        writeFile(root + "/collections/Arcade/settings.conf", "list.extensions = zip\n");
        writeFile(root + "/collections/Arcade/roms/a.zip", "x");
        writeFile(root + "/collections/Arcade/roms/b.zip", "x");
        writeFile(root + "/collections/NES/settings.conf", "list.extensions = nes\n");
        writeFile(root + "/collections/NES/roms/mario.nes", "x");
        writeFile(root + "/collections/NES/roms/zelda.nes", "x");
        writeFile(root + "/collections/SNES/settings.conf", "list.extensions = sfc\n");
        writeFile(root + "/collections/SNES/roms/f.sfc", "x");
        writeFile(root + "/collections/Favs/Arcade.sub", "");
        writeFile(root + "/collections/Favs/NES.sub", "mario\n");
        writeFile(root + "/collections/Favs/info.conf", "title = Favourites\n");
        writeFile(root + "/collections/Favs/system_artwork/logo.png", "old logo");
        writeFile(root + "/collections/Favs/system_artwork/story.txt", "All the best ones.");
        return root;
    }

    // RetroFE's manager as the Menus screens see it: the shared index and editor
    struct Rig
    {
        std::string root;
        std::unique_ptr<Configuration> config;
        std::unique_ptr<Library::Indexer> indexer;
        std::unique_ptr<Library::Editor> editor;
        std::unique_ptr<ManagerModel> model;

        explicit Rig(const std::string& r)
            : root(r)
        {
            Configuration::absolutePath = root;
            config = std::make_unique<Configuration>();
            Library::ScanOptions options;
            options.root = root;
            indexer = std::make_unique<Library::Indexer>(options);
            indexer->rebuild();
            indexer->wait(10000);
            editor = std::make_unique<Library::Editor>(root);
            model = std::make_unique<ManagerModel>(*config);
            Manager::Services services{ *config };
            services.library = indexer.get();
            services.editor = editor.get();
            check(model->addProvider(std::make_unique<Manager::MenuProvider>(services)), "menu provider added");
        }

        ~Rig()
        {
            model.reset();
            editor.reset();
            indexer.reset();
        }

        void screen(const std::string& form, const Forms::Params& params = {}) { model->setScreen(form, params); }
        std::string get(const std::string& key) { return model->get("menu:" + key); }
        void set(const std::string& key, const std::string& value) { model->set("menu:" + key, value); }
        std::string problem(const std::string& action) { return model->actionProblem("menu." + action); }
        Forms::Rows rows(const std::string& key) { return model->rows("menu:" + key, 200); }

        Forms::Model::Result run(const std::string& action)
        {
            Forms::Model::Result result = model->runAction("menu." + action);
            indexer->wait(10000);
            return result;
        }

        std::string file(const std::string& relative) { return readFile(root + "/" + relative); }
        bool exists(const std::string& relative) { return fs::exists(root + "/" + relative); }
    };

    const Forms::Row* findRow(const Forms::Rows& rows, const std::string& id)
    {
        for (const Forms::Row& row : rows.items) {
            if (row.id == id) {
                return &row;
            }
        }
        return nullptr;
    }

    bool hasOption(const std::vector<std::pair<std::string, std::string>>& options, const std::string& value)
    {
        return std::any_of(options.begin(), options.end(), [&](const auto& o) { return o.first == value; });
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }
}

static void runMenusTests();
static Test::Suite menusSuite("menus", runMenusTests);

static void runMenusTests()
{
    run("menus: every Menus form parses", [] {
        for (const char* id : { "menus", "menu", "menu-entries", "menu-entry", "menu-sources", "menu-source", "menu-info",
                                "menu-media", "menu-medium", "menu-actions", "menu-new" }) {
            std::string error;
            const char* xml = Forms::builtinForm(id);
            check(xml && Forms::parse(xml, error) != nullptr, std::string(id) + " parses " + error);
        }
    });

    run("menus: the tree from Browse down flags missing folders, misspellings and loops", [] {
        Rig rig(makeInstall("menus-tree"));
        rig.screen("menus");
        check(contains(rig.get("browseNote"), "Browse opens Main"), "says what Browse opens");
        Forms::Rows tree = rig.rows("tree");
        check(tree.items.size() >= 9 && tree.items[0].id == "Main" && contains(tree.items[0].detail, "Browse"),
              "the top menu first");
        checkEqual(tree.items[1].label, "      Consoles", "children are indented");
        check(contains(tree.items[1].detail, "menu, 3 entries"), "kind and count: " + tree.items[1].detail);
        bool loop = false, missing = false, misspelt = false;
        for (const Forms::Row& row : tree.items) {
            loop |= row.id == "Main" && contains(row.detail, "loop") && row.label == "            Main";
            missing |= row.id == "Ghost" && contains(row.detail, "no such folder");
            misspelt |= row.id == "NES" && row.label == "      nes" && contains(row.detail, "folder is NES");
            if (row.label != "Main") {
                check(!row.params.at("via").empty(), "rows say which menu they're in");
            }
        }
        check(loop && missing && misspelt, "loop, missing and misspelt entries are flagged");
        const Forms::Row* favs = findRow(tree, "Favs");
        check(favs && contains(favs->detail, "merged collection, 3 games"), "a merged collection's games");

        Forms::Rows unlisted = rig.rows("unlisted");
        check(findRow(unlisted, "Folders") && findRow(unlisted, "Spare") && !findRow(unlisted, "Arcade"),
              "menus no menu reaches are listed apart; systems aren't");
        checkEqual(rig.get("hasUnlisted"), "yes", "hasUnlisted");

        rig.set("query", "snes");
        Forms::Rows found = rig.rows("tree");
        check(found.total == 1 && found.items[0].label == "Main > Consoles > SNES", "search shows the path");
    });

    run("menus: details of a menu, a merged collection and a missing entry", [] {
        Rig rig(makeInstall("menus-details"));
        rig.screen("menu", { { "id", "Main" } });
        check(contains(rig.get("browse"), "Browse opens this menu"), "the top menu says so");
        checkEqual(rig.get("entryCount"), "5", "entries");
        checkEqual(rig.get("isMenu"), "yes", "isMenu");

        rig.screen("menu", { { "id", "Favs" } });
        checkEqual(rig.get("title"), "Favourites", "title from info.conf");
        checkEqual(rig.get("kind"), "merged collection", "kind");
        checkEqual(rig.get("parents"), "Main", "listed in");
        checkEqual(rig.get("sources"), "Arcade (all games), NES (1 game listed)", "sources, an empty .sub being all games");
        checkEqual(rig.get("gameCount"), "3", "a, b and mario");
        check(contains(rig.get("logo"), "logo.png"), "its logo");
        checkEqual(rig.get("isMerged"), "yes", "isMerged");

        rig.screen("menu", { { "id", "Spare" } });
        checkEqual(rig.get("parents"), "No menu lists it", "an unlisted menu");

        rig.screen("menu", { { "id", "Ghost" }, { "via", "Main" } });
        checkEqual(rig.get("isFound"), "no", "not found");
        check(contains(rig.get("missing"), "Main lists Ghost"), "says who lists it");
        Forms::Model::Result fix = rig.run("openVia");
        check(fix.ok && fix.open == "menu-entries" && fix.params["id"] == "Main", "goes to the menu's entries");

        rig.screen("menu", { { "id", "Arcade" } });
        Forms::Model::Result open = rig.run("openSystem");
        check(open.ok && open.open == "system" && open.params["id"] == "Arcade", "a system opens in Systems");
    });

    run("menus: entries move, keep comments and line endings, and can't move in a folder menu", [] {
        Rig rig(makeInstall("menus-move"));
        rig.screen("menu-entries", { { "id", "Main" } });
        Forms::Rows entries = rig.rows("entries");
        check(entries.total == 5 && entries.items[0].label == "1. Consoles", "entries in order");
        check(findRow(entries, "Ghost") && contains(findRow(entries, "Ghost")->detail, "no such folder"), "a missing folder is flagged");
        check(findRow(entries, "nes") && contains(findRow(entries, "nes")->detail, "spelt NES"), "a misspelt one too");

        rig.screen("menu-entry", { { "id", "Main" }, { "entry", "Consoles" } });
        check(!rig.problem("moveUp").empty(), "the first can't move up");
        checkEqual(rig.get("entryPosition"), "1 of 5", "position");

        rig.screen("menu-entry", { { "id", "Main" }, { "entry", "Arcade" } });
        Forms::Model::Result moved = rig.run("moveUp");
        check(moved.ok, "moved: " + moved.message);
        checkEqual(rig.file("collections/Main/menu.txt"), "Arcade\r\n# comment\r\nConsoles\r\nFavs\r\nGhost\r\nnes\r\n",
                   "swapped; the comment and CRLFs stay");
        check(rig.indexer->changesPending(), "RetroFE reloads when the manager closes");
        rig.screen("menu-entry", { { "id", "Main" }, { "entry", "Arcade" } });
        checkEqual(rig.get("entryPosition"), "1 of 5", "the new position shows after the update");
        check(rig.exists("manage/backups/collections/Main/menu.txt"), "the original is kept");

        rig.screen("menu-entry", { { "id", "Folders" }, { "entry", "NES" } });
        check(contains(rig.problem("moveUp"), "by name"), "a menu folder is sorted by RetroFE");
    });

    run("menus: fix an entry's spelling, remove one, add one", [] {
        Rig rig(makeInstall("menus-entries"));
        rig.screen("menu-entry", { { "id", "Main" }, { "entry", "nes" } });
        checkEqual(rig.get("entryMisspelt"), "yes", "misspelt");
        Forms::Model::Result fixed = rig.run("respell");
        check(fixed.ok && fixed.back, "respelt: " + fixed.message);
        checkEqual(rig.file("collections/Main/menu.txt"), "Consoles\r\n# comment\r\nArcade\r\nFavs\r\nGhost\r\nNES\r\n",
                   "spelt as the folder, in the same place");

        rig.screen("menu-entry", { { "id", "Main" }, { "entry", "Ghost" } });
        Forms::Model::Result removed = rig.run("removeEntry");
        check(removed.ok && removed.back && contains(removed.message, "unchanged"), "removed: " + removed.message);
        check(!contains(rig.file("collections/Main/menu.txt"), "Ghost"), "Ghost is gone");

        rig.screen("menu-entries", { { "id", "Main" } });
        auto addable = rig.model->options("menu:addable");
        check(hasOption(addable, "SNES") && hasOption(addable, "Spare") && hasOption(addable, "Folders"), "unlisted collections offered");
        check(!hasOption(addable, "Consoles") && !hasOption(addable, "NES") && !hasOption(addable, "Main"),
              "not ones already listed, nor the menu itself");
        rig.set("addEntry", "Spare");
        Forms::Model::Result added = rig.run("addEntry");
        check(added.ok, "added: " + added.message);
        check(contains(rig.file("collections/Main/menu.txt"), "NES\r\nSpare\r\n"), "appended with the file's line endings");

        rig.screen("menu-entries", { { "id", "Spare" } });
        addable = rig.model->options("menu:addable");
        check(!hasOption(addable, "Main") && hasOption(addable, "Arcade"), "a menu that leads here isn't offered (a loop)");

        rig.screen("menu-entries", { { "id", "Folders" } });
        rig.set("addEntry", "SNES");
        check(rig.run("addEntry").ok && rig.exists("collections/Folders/menu/SNES.txt") && !rig.exists("collections/Folders/menu.txt"),
              "a folder menu gets menu/<name>.txt, not a menu.txt that would hide the folder");
    });

    run("menus: menu_supported.txt is kept in step", [] {
        std::string root = makeInstall("menus-supported");
        writeFile(root + "/collections/Consoles/menu_supported.txt", "NES\nSNES\nMain\n");
        Rig rig(root);
        rig.screen("menu-entry", { { "id", "Consoles" }, { "entry", "SNES" } });
        check(rig.run("moveUp").ok, "moved");
        checkEqual(rig.file("collections/Consoles/menu_supported.txt"), "SNES\nNES\nMain\n", "moved there too");
        rig.screen("menu-entry", { { "id", "Consoles" }, { "entry", "Main" } });
        check(rig.run("removeEntry").ok, "removed");
        checkEqual(rig.file("collections/Consoles/menu_supported.txt"), "SNES\nNES\n", "removed there too");
    });

    run("menus: a merged collection's sources", [] {
        Rig rig(makeInstall("menus-sources"));
        rig.screen("menu-sources", { { "id", "Favs" } });
        Forms::Rows sources = rig.rows("sources");
        check(sources.total == 2 && findRow(sources, "Arcade") && findRow(sources, "Arcade")->detail == "all games (2)",
              "sources with their counts");
        auto addable = rig.model->options("menu:addableSources");
        check(hasOption(addable, "SNES") && !hasOption(addable, "NES") && !hasOption(addable, "Main"),
              "systems not yet sources");
        rig.set("addSource", "SNES");
        Forms::Model::Result added = rig.run("addSource");
        check(added.ok && rig.exists("collections/Favs/SNES.sub") && rig.file("collections/Favs/SNES.sub").empty(),
              "an empty .sub: all its games");

        rig.screen("menu-source", { { "id", "Favs" }, { "source", "NES" } });
        checkEqual(rig.get("sourceShows"), "1 game listed, 1 found", "what it shows");
        Forms::Model::Result removed = rig.run("removeSource");
        check(removed.ok && removed.back && !rig.exists("collections/Favs/NES.sub"), "removed: " + removed.message);
        check(rig.exists("manage/backups/collections/Favs/NES.sub"), "kept in the backups");

        rig.screen("menu-sources", { { "id", "Main" } });
        check(!rig.problem("addSource").empty(), "a menu isn't a merged collection");
    });

    run("menus: new menus and merged collections", [] {
        Rig rig(makeInstall("menus-new"));
        rig.screen("menu-new");
        checkEqual(rig.get("newParent"), "Main", "the top menu by default");
        check(!rig.problem("create").empty(), "a name is needed");
        rig.set("newName", "Retro");
        Forms::Model::Result made = rig.run("create");
        check(made.ok && made.open == "menu" && made.replace && made.params["id"] == "Retro", "made, in place of the add form: " + made.message);
        check(rig.exists("collections/Retro/menu.txt") && rig.file("collections/Retro/menu.txt").empty(), "an empty menu.txt");
        check(contains(rig.file("collections/Retro/info.conf"), "title = Retro"), "an info.conf");
        check(rig.exists("collections/Retro/settings.conf"), "a settings.conf, so RetroFE reads its settings");
        check(contains(rig.file("collections/Main/menu.txt"), "nes\r\nRetro\r\n"), "listed in Main");
        rig.screen("menu", { { "id", "Retro" } });
        checkEqual(rig.get("kind"), "menu", "it's a menu");

        rig.screen("menu-new", { { "id", "Consoles" } });
        checkEqual(rig.get("newParent"), "Consoles", "made from a menu's Actions: in that menu");
        rig.set("newKind", "merged");
        rig.set("newName", "Best");
        rig.set("newSource", "SNES");
        made = rig.run("create");
        check(made.ok && rig.exists("collections/Best/SNES.sub") && !rig.exists("collections/Best/menu.txt"), "merged: " + made.message);
        check(contains(rig.file("collections/Consoles/menu.txt"), "Best"), "listed in Consoles");
        rig.screen("menu", { { "id", "Best" } });
        checkEqual(rig.get("sources"), "SNES (all games)", "showing all of SNES");

        rig.screen("menu-new");
        rig.set("newKind", "menu");
        rig.set("newName", "arcade");
        Forms::Model::Result clash = rig.run("create");
        check(!clash.ok && contains(clash.message, "already"), "names are unique, ignoring case: " + clash.message);
        rig.set("newName", "_hidden");
        check(!rig.run("create").ok, "folder names RetroFE skips are refused");
        rig.set("newName", "Home");
        check(!rig.run("create").ok, "reserved names are refused");

        Library::Editor& editor = *rig.editor;
        auto index = rig.indexer->index();
        check(!editor.createMenu(*index, "Inside", "Arcade").ok, "a collection of games can't get menu entries");
    });

    run("menus: a top menu that doesn't exist yet", [] {
        std::string root = makeInstall("menus-newtop");
        writeFile(root + "/settings.conf", "firstCollection = Start\n");
        Rig rig(root);
        rig.screen("menus");
        check(contains(rig.get("browseNote"), "doesn't exist yet"), "says so: " + rig.get("browseNote"));
        rig.screen("menu-new");
        checkEqual(rig.get("newParent"), "Start", "offered as the parent");
        rig.set("newName", "Retro");
        check(rig.run("create").ok && contains(rig.file("collections/Start/menu.txt"), "Retro"), "the top menu is made with it");
    });

    run("menus: delete moves entries up and keeps the files", [] {
        std::string root = makeInstall("menus-delete");
        writeFile(root + "/collections/Consoles/system_artwork/logo.png", "logo");
        writeFile(root + "/collections/Consoles/info.conf", "title = Consoles\n");
        Rig rig(root);
        rig.screen("menu-actions", { { "id", "Consoles" } });
        check(rig.problem("delete").empty(), "a menu can be deleted: " + rig.problem("delete"));
        check(contains(rig.get("deleteNote"), "move up into Main"), "says where its entries go: " + rig.get("deleteNote"));
        Forms::Model::Result deleted = rig.run("delete");
        check(deleted.ok && deleted.back, "deleted: " + deleted.message);
        check(!rig.exists("collections/Consoles"), "the folder is gone");
        check(rig.exists("manage/backups/collections/Consoles/system_artwork/logo.png")
              && rig.exists("manage/backups/collections/Consoles/menu.txt"), "its files are in the backups");
        checkEqual(rig.file("collections/Main/menu.txt"), "SNES\r\n# comment\r\nArcade\r\nFavs\r\nGhost\r\nnes\r\n",
                   "SNES takes its place; NES (listed as nes) and Main (the menu itself) aren't added twice");

        rig.screen("menu-actions", { { "id", "Main" } });
        check(contains(rig.problem("delete"), "Browse opens"), "the top menu can't go");
        rig.screen("menu-actions", { { "id", "Arcade" } });
        check(contains(rig.problem("delete"), "Systems"), "a system can't go from here");

        writeFile(root + "/collections/Spare/notes.doc", "mine");
        rig.indexer->changed();
        rig.indexer->wait(10000);
        rig.screen("menu-actions", { { "id", "Spare" } });
        check(contains(rig.problem("delete"), "notes.doc"), "a folder with other files stays: " + rig.problem("delete"));

        rig.screen("menu-actions", { { "id", "Favs" } });
        check(contains(rig.get("deleteNote"), "stay in their systems"), "a merged collection's games stay");
        check(rig.run("delete").ok && !rig.exists("collections/Favs") && !contains(rig.file("collections/Main/menu.txt"), "Favs"),
              "a merged collection goes, and out of Main");
        rig.screen("menu", { { "id", "Favs" } });
        check(contains(rig.get("missing"), "was deleted"), "the details screen left behind says so");
    });

    run("menus: delete with several parents, one a menu folder, one listing it twice", [] {
        std::string root = makeInstall("menus-delete-parents");
        writeFile(root + "/collections/Main/menu.txt", "Consoles\r\n# comment\r\nArcade\r\nconsoles\r\n");
        writeFile(root + "/collections/Folders/menu/Consoles.txt", "");
        Rig rig(root);
        rig.screen("menu", { { "id", "Consoles" } });
        checkEqual(rig.get("parents"), "Folders, Main", "two parents");
        rig.screen("menu-actions", { { "id", "Consoles" } });
        Forms::Model::Result deleted = rig.run("delete");
        check(deleted.ok, "deleted: " + deleted.message);
        checkEqual(rig.file("collections/Main/menu.txt"), "NES\r\nSNES\r\n# comment\r\nArcade\r\n",
                   "its entries take its place in Main; every line naming it goes, whatever the spelling");
        check(rig.exists("collections/Folders/menu/SNES.txt") && rig.exists("collections/Folders/menu/Main.txt")
              && !rig.exists("collections/Folders/menu/Consoles.txt") && !rig.exists("collections/Folders/menu.txt"),
              "the menu folder gets menu/<entry>.txt files and loses Consoles.txt, without a menu.txt");
    });

    run("menus: a menu.xml parent is never turned into a menu.txt", [] {
        std::string root = makeInstall("menus-xml");
        writeFile(root + "/collections/Retro/settings.conf", "list.menuSort = yes\n");
        writeFile(root + "/collections/Retro/menu.xml", "<menu><item collection=\"Spare\"/></menu>");
        Rig rig(root);
        rig.screen("menu-actions", { { "id", "Spare" } });
        check(contains(rig.problem("delete"), "menu.xml"), "deleting what a menu.xml lists is refused: " + rig.problem("delete"));
        checkEqual(rig.get("canHaveChildren"), "yes", "Spare (menu.txt) can have children");
        rig.screen("menu-actions", { { "id", "Retro" } });
        checkEqual(rig.get("canHaveChildren"), "no", "an XML menu can't get new ones here");
        rig.screen("menu-new", { { "id", "Retro" } });
        rig.set("newName", "Inside");
        check(!rig.problem("create").empty() && !hasOption(rig.model->options("menu:parentMenus"), "Retro"),
              "create refuses rather than silently using another menu: " + rig.problem("create"));
        rig.screen("menu-entries", { { "id", "Retro" } });
        check(!rig.problem("addEntry").empty(), "no entries added to it");

        auto index = rig.indexer->index();
        check(!rig.editor->addMenuChild(*index, "Retro", "Arcade").ok, "the Editor refuses too");
        std::string error;
        check(!Library::MenuFiles::add(root, "Retro", "Arcade", -1, nullptr, error) && !rig.exists("collections/Retro/menu.txt"),
              "and MenuFiles never writes a menu.txt beside a lone menu.xml: " + error);
    });

    run("menus: a merged collection's last source removed, then a new one, then delete", [] {
        std::string root = makeInstall("menus-last-source");
        writeFile(root + "/collections/Blank/info.conf", "title = Blank\n");
        Rig rig(root);
        for (const char* source : { "Arcade", "NES" }) {
            rig.screen("menu-source", { { "id", "Favs" }, { "source", source } });
            check(rig.run("removeSource").ok, std::string("removed ") + source);
        }
        rig.screen("menu", { { "id", "Favs" } });
        checkEqual(rig.get("kind"), "empty collection", "nothing left");
        check(rig.get("showSources") == "yes" && rig.get("showEntries") == "yes", "it can become either");
        rig.screen("menu-sources", { { "id", "Favs" } });
        check(rig.problem("addSource").empty(), "sources can be added again: " + rig.problem("addSource"));
        rig.set("addSource", "SNES");
        check(rig.run("addSource").ok && rig.exists("collections/Favs/SNES.sub"), "SNES added back");
        rig.screen("menu-actions", { { "id", "Favs" } });
        check(rig.problem("delete").empty() && rig.run("delete").ok && !rig.exists("collections/Favs"), "and it can be deleted");

        rig.screen("menus");
        check(findRow(rig.rows("unlisted"), "Blank") != nullptr, "a collection with nothing yet is listed apart");
        rig.screen("menu-actions", { { "id", "Blank" } });
        check(rig.problem("delete").empty(), "and can be deleted: " + rig.problem("delete"));
        checkEqual(rig.get("canHaveChildren"), "yes", "or get entries");
    });

    run("menus: making the top menu by its own name", [] {
        std::string root = makeInstall("menus-top-self");
        writeFile(root + "/settings.conf", "firstCollection = Start\n");
        Rig rig(root);
        rig.screen("menu-new");
        rig.set("newName", "Start");
        Forms::Model::Result made = rig.run("create");
        check(made.ok && contains(made.message, "Browse opens it"), "made: " + made.message);
        check(rig.exists("collections/Start/menu.txt") && rig.file("collections/Start/menu.txt").empty(),
              "an empty menu, not listing itself");
    });

    run("menus: a failed re-read is shown and stops changes", [] {
        Library::Indexer::Status status;
        status.state = Library::Indexer::State::Failed;
        status.error = "disk gone";
        std::string text = Manager::MenuProvider::statusText(status, true);
        check(contains(text, "Couldn't re-read") && contains(text, "disk gone"), "says so: " + text);
        check(!Manager::MenuProvider::writeProblemFor(status, true).empty(), "writes wait");
        check(contains(Manager::MenuProvider::statusText(status, false), "Couldn't read"), "first read failed");
        status.state = Library::Indexer::State::Building;
        checkEqual(Manager::MenuProvider::statusText(status, true), "Updating...", "building");
        check(!Manager::MenuProvider::writeProblemFor(status, true).empty(), "writes wait while building");
        status.state = Library::Indexer::State::Ready;
        check(Manager::MenuProvider::statusText(status, true).empty() && Manager::MenuProvider::writeProblemFor(status, true).empty(),
              "nothing to say when ready");

        Rig rig(makeInstall("menus-retry"));
        rig.screen("menus");
        checkEqual(rig.get("isFailed"), "no", "not failed");
        check(!rig.problem("retry").empty(), "retry only when it failed");
    });

    run("menus: favorites with entries stop a delete, lastplayed doesn't", [] {
        std::string root = makeInstall("menus-favorites");
        writeFile(root + "/collections/Spare/playlists/favorites.txt", "_Arcade:a\n");
        writeFile(root + "/collections/Spare/playlists/lastplayed.txt", "_Arcade:a\n");
        Rig rig(root);
        rig.screen("menu-actions", { { "id", "Spare" } });
        check(contains(rig.problem("delete"), "favorites.txt"), "favorites someone chose stay: " + rig.problem("delete"));
        fs::remove(root + "/collections/Spare/playlists/favorites.txt");
        rig.indexer->changed();
        rig.indexer->wait(10000);
        rig.screen("menu-actions", { { "id", "Spare" } });
        check(rig.problem("delete").empty(), "lastplayed history doesn't: " + rig.problem("delete"));
        check(rig.run("delete").ok && rig.exists("manage/backups/collections/Spare/playlists/lastplayed.txt"), "kept in the backups");
    });

#ifdef _WIN32
    run("menus: a create that can't be listed is undone, so it can be tried again", [] {
        std::string root = makeInstall("menus-create-rollback");
        std::string menuTxt = root + "/collections/Main/menu.txt";
        Rig rig(root);
        fs::permissions(menuTxt, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove);
        rig.screen("menu-new");
        rig.set("newName", "Retro");
        Forms::Model::Result failed = rig.run("create");
        check(!failed.ok && contains(failed.message, "Nothing was made") && !rig.exists("collections/Retro"),
              "undone: " + failed.message);
        fs::permissions(menuTxt, fs::perms::owner_write, fs::perm_options::add);
        rig.screen("menu-new");
        rig.set("newName", "Retro");
        Forms::Model::Result made = rig.run("create");
        check(made.ok && contains(rig.file("collections/Main/menu.txt"), "Retro"), "a retry works: " + made.message);
    });
#endif

    run("menus: names Windows can't use are refused", [] {
        for (const char* bad : { "con", "Aux.v2", "lpt1", "Retro.", "Retro " }) {
            check(!Library::MenuFiles::checkChildName(bad).empty(), std::string("refused: ") + bad);
        }
        check(Library::MenuFiles::checkChildName("Console").empty() && Library::MenuFiles::checkChildName("Con Games").empty()
              && Library::MenuFiles::checkChildName("Retro.Picks").empty(), "names that only start like one are fine");
    });

    run("menus: a .sub spelt differently from its system is renamed, keeping its lines", [] {
        std::string root = makeInstall("menus-sub-case");
        writeFile(root + "/collections/Favs/snes.sub", "f\n");
        Rig rig(root);
        rig.screen("menu-source", { { "id", "Favs" }, { "source", "snes" } });
        checkEqual(rig.get("sourceMisspelt"), "yes", "flagged");
        Forms::Model::Result fixed = rig.run("respellSource");
        check(fixed.ok && fixed.back, "renamed: " + fixed.message);
        bool exact = false;
        for (const auto& entry : fs::directory_iterator(root + "/collections/Favs")) {
            exact |= entry.path().filename().string() == "SNES.sub";
        }
        check(exact && rig.file("collections/Favs/SNES.sub") == "f\n", "SNES.sub with the same lines");
        rig.screen("menu", { { "id", "Favs" } });
        check(contains(rig.get("sources"), "SNES (1 game listed)"), "now found: " + rig.get("sources"));
    });

    run("menus: an entry listed twice", [] {
        std::string root = makeInstall("menus-duplicate");
        writeFile(root + "/collections/Main/menu.txt", "Arcade\nConsoles\nArcade\n");
        Rig rig(root);
        rig.screen("menu-entries", { { "id", "Main" } });
        Forms::Rows entries = rig.rows("entries");
        check(entries.total == 3 && contains(entries.items[2].detail, "listed 2 times")
              && entries.items[2].params.at("position") == "2", "flagged, and each row is its own line");
        rig.screen("menu-entry", { { "id", "Main" }, { "entry", "Arcade" }, { "position", "2" } });
        checkEqual(rig.get("entryPosition"), "3 of 3", "the row's own line");
        check(contains(rig.problem("moveUp"), "more than once"), "can't be moved");
        Forms::Model::Result removed = rig.run("removeEntry");
        check(removed.ok && contains(removed.message, "2 times"), "removing says every line went: " + removed.message);
        checkEqual(rig.file("collections/Main/menu.txt"), "Consoles\n", "both lines went");
    });

    run("menus: layoutFromAnotherCollection decides the page", [] {
        std::string root = makeInstall("menus-page");
        writeFile(root + "/settings.conf", "firstCollection = Main\nlayout = Neat\n");
        writeFile(root + "/layouts/Neat/layout.xml", "<layout width=\"1920\" height=\"1080\"/>");
        writeFile(root + "/layouts/Neat/collections/_menu/layout/layout.xml", "<layout/>");
        writeFile(root + "/collections/Consoles/settings.conf", "layoutFromAnotherCollection = Arcade\n");
        Rig rig(root);
        rig.screen("menu", { { "id", "Main" } });
        check(contains(rig.get("page"), "menu page"), "Main uses the menu page: " + rig.get("page"));
        rig.screen("menu", { { "id", "Consoles" } });
        check(contains(rig.get("page"), "Arcade's page"), "Consoles borrows Arcade's: " + rig.get("page"));
    });

    run("menus: info.conf is edited with Save and Reset", [] {
        Rig rig(makeInstall("menus-info"));
        rig.screen("menu-info", { { "id", "Favs" } });
        checkEqual(rig.get("infoTitle"), "Favourites", "current title");
        rig.set("infoTitle", "Faves");
        rig.set("infoYear", "1999");
        checkEqual(rig.get("isDirty"), "yes", "dirty");
        check(!rig.model->leaving(rig.model->screen()).empty(), "leaving asks first");
        Forms::Model::Result saved = rig.run("save");
        check(saved.ok, "saved: " + saved.message);
        std::string info = rig.file("collections/Favs/info.conf");
        check(contains(info, "title = Faves") && contains(info, "year = 1999"), "written: " + info);
        checkEqual(rig.get("isDirty"), "no", "clean after saving");

        rig.set("infoTitle", "");
        check(rig.run("save").ok && !contains(rig.file("collections/Favs/info.conf"), "title"), "a blank title is taken out");
        rig.set("infoYear", "2001");
        check(rig.run("reset").ok && rig.get("infoYear") == "1999", "Undo puts the saved value back");
    });

    run("menus: artwork is replaced, cleared and put back in system_artwork", [] {
        std::string root = makeInstall("menus-media");
        writeFile(root + "/collections/Main/medium_artwork/marquee/Favs.png", "menu's own");
        writeFile(root + "/incoming/new.PNG", "new logo");
        Rig rig(root);
        rig.screen("menu-media", { { "id", "Favs" } });
        Forms::Rows slots = rig.rows("media");
        const Forms::Row* logo = findRow(slots, "logo");
        check(logo && logo->detail == "logo.png" && contains(logo->image, "logo.png"), "the logo slot");
        check(findRow(slots, "marquee") && contains(findRow(slots, "marquee")->detail, "menu's own"), "the menu's own art wins");
        check(findRow(slots, "story") && findRow(slots, "story")->detail == "story.txt", "a text slot");

        rig.screen("menu-medium", { { "id", "Favs" }, { "type", "marquee" } });
        check(contains(rig.get("mediumWhere"), "In Main"), "says the menu's own is shown");
        check(!rig.problem("clearMedium").empty(), "nothing of its own to clear");

        rig.screen("menu-medium", { { "id", "Favs" }, { "type", "story" } });
        checkEqual(rig.get("mediumText"), "All the best ones.", "text preview");

        rig.screen("menu-medium", { { "id", "Favs" }, { "type", "logo" } });
        check(!rig.problem("replaceMedium").empty(), "a file is needed");
        rig.set("newFile", root + "/incoming/new.PNG");
        Forms::Model::Result replaced = rig.run("replaceMedium");
        check(replaced.ok, "replaced: " + replaced.message);
        checkEqual(rig.file("collections/Favs/system_artwork/logo.png"), "new logo", "copied in, extension lower-cased");
        checkEqual(rig.get("canRestore"), "yes", "the original can be put back");
        check(rig.run("restoreMedium").ok, "put back");
        checkEqual(rig.file("collections/Favs/system_artwork/logo.png"), "old logo", "the original is back");
        check(rig.run("clearMedium").ok && !rig.exists("collections/Favs/system_artwork/logo.png"), "cleared");
        check(rig.exists("manage/backups/collections/Favs/system_artwork/logo.png"), "kept in the backups");
    });
}

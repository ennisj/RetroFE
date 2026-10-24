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

// RetroArch settings (Library/RetroArch, Manager/Games/RetroArchProvider):
// detecting RetroArch launchers and their core's settings folder, the curated
// rows, the files written (only changed keys, the one-file rule for options
// and controls, removal when equal to what's shadowed), backups, and the
// game and system screens.

#include "TestSupport.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Library/Backups.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Files.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Library/RetroArch.h"
#include "../Source/Manager/Games/RetroArchProvider.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Utility/Utils.h"
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
namespace RA = Library::RetroArch;
using namespace Test;

namespace
{
    std::string os() { return Utils::getOSType(); }

    // No platform folders: only what the fixture has counts
    RA::Environment noEnvironment()
    {
        RA::Environment env;
        env.os = os();
        return env;
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    bool exists(const std::string& path)
    {
        std::error_code ec;
        return fs::exists(path, ec);
    }

    const char* const retroarchCfg =
        "# RetroArch's own settings\n"
        "rgui_config_directory = \":/config\"\n"
        "input_remapping_directory = \":/config/remaps\"\n"
        "libretro_directory = \":/cores\"\n"
        "libretro_info_path = \":/info\"\n"
        "aspect_ratio_index = \"22\"\n"
        "video_smooth = \"false\"\n"
        "video_shader_enable = \"false\"\n"
        "input_overlay_enable = \"true\"\n"
        "video_scale_integer = \"false\"\n"
        "run_ahead_enabled = \"false\"\n"
        "run_ahead_frames = \"1\"\n"
        "audio_volume = \"0.000000\"\n"
        "savestate_auto_load = \"false\"\n"
        "savestate_auto_save = \"false\"\n";

    // A fake RetroArch (emu/RetroArch) and launchers for every case:
    //   genesis, genesis2   RetroArch with Genesis Plus GX (MegaDrive, Genesis)
    //   mystery             a core the table doesn't know, with an info file (Mystery)
    //   unknown             a core with no info file (Unknown)
    //   notepad             not RetroArch (Notes)
    //   gone                RetroArch's program missing (Gone)
    std::string makeInstall(const std::string& name)
    {
        std::string root = makeRoot(name);
        std::string ra = root + "/emu/RetroArch/";
        writeFile(ra + "retroarch.exe", "x");
        writeFile(ra + "retroarch.cfg", retroarchCfg);
        writeFile(ra + "cores/genesis_plus_gx_libretro.dll", "x");
        writeFile(ra + "cores/mystery_libretro.dll", "x");
        writeFile(ra + "info/mystery_libretro.info", "display_name = \"Mystery\"\ncorename = \"Mystery Core\"\n");
        writeFile(ra + "cores/unknown_libretro.dll", "x");

        std::string launchers = root + "/launchers." + os() + "/";
        writeFile(launchers + "genesis.conf",
                  "executable = %RETROFE_PATH%/emu/RetroArch/retroarch.exe\narguments = -L \"cores/genesis_plus_gx_libretro.dll\" \"%ITEM_FILEPATH%\"\n");
        writeFile(launchers + "genesis2.conf",
                  "executable = emu/RetroArch/retroarch.exe\narguments = --libretro=cores/genesis_plus_gx_libretro.dll \"%ITEM_FILEPATH%\"\n");
        writeFile(launchers + "mystery.conf", "executable = emu/RetroArch/retroarch.exe\narguments = -L cores/mystery_libretro.dll \"%ITEM_FILEPATH%\"\n");
        writeFile(launchers + "unknown.conf", "executable = emu/RetroArch/retroarch.exe\narguments = -L cores/unknown_libretro.dll \"%ITEM_FILEPATH%\"\n");
        writeFile(launchers + "notepad.conf", "executable = notepad.exe\narguments = \"%ITEM_FILEPATH%\"\n");
        writeFile(launchers + "gone.conf", "executable = emu/Missing/retroarch.exe\narguments = -L cores/fceumm_libretro.dll \"%ITEM_FILEPATH%\"\n");

        auto system = [&](const std::string& c, const std::string& launcher, const std::string& ext, const std::string& game) {
            writeFile(root + "/collections/" + c + "/settings.conf", "list.extensions = " + ext + "\nlauncher = " + launcher + "\n");
            writeFile(root + "/collections/" + c + "/roms/" + game + "." + ext, "x");
        };
        system("MegaDrive", "genesis", "md", "sonic");
        writeFile(root + "/collections/MegaDrive/roms/streets.md", "x");
        system("Genesis", "genesis2", "gen", "columns");
        system("Mystery", "mystery", "bin", "puzzle");
        system("Unknown", "unknown", "bin", "thing");
        system("Notes", "notepad", "txt", "readme");
        system("Gone", "gone", "nes", "mario");
        return root;
    }

    Library::LauncherDef launcherDef(const std::string& root, const std::string& name)
    {
        // A launcher file's "key = value" lines, values taken whole as RetroFE does
        Library::LauncherDef def;
        def.id = name;
        std::string text = readFile(root + "/launchers." + os() + "/" + name + ".conf");
        auto value = [&](const std::string& key) {
            size_t at = text.find(key + " = ");
            return at == std::string::npos ? std::string() : text.substr(at + key.size() + 3, text.find('\n', at) - at - key.size() - 3);
        };
        def.executable = value("executable");
        def.arguments = value("arguments");
        return def;
    }

    RA::Setup setupOf(const std::string& root, const std::string& launcher)
    {
        return RA::detect(root, launcherDef(root, launcher), noEnvironment());
    }

    Library::GameRecord gameRecord(const std::string& root, const std::string& collection, const std::string& file)
    {
        Library::GameRecord g;
        g.name = fs::path(file).stem().string();
        g.directory = root + "/collections/" + collection + "/roms";
        g.file = g.directory + "/" + file;
        return g;
    }

    struct Fixture
    {
        explicit Fixture(const std::string& root)
            : indexer(options(root))
            , editor(root)
        {
            Configuration::absolutePath = root;
            config = std::make_unique<Configuration>();
            model = std::make_unique<ManagerModel>(*config);
            Manager::Services services{ *config };
            services.library = &indexer;
            services.editor = &editor;
            model->addProvider(std::make_unique<Manager::RetroArchProvider>(services, noEnvironment()));
            indexer.rebuild();
            indexer.wait(10000);
        }

        static Library::ScanOptions options(const std::string& root)
        {
            Library::ScanOptions scan;
            scan.root = root;
            return scan;
        }

        Library::Indexer indexer;
        Library::Editor editor;
        std::unique_ptr<Configuration> config;
        std::unique_ptr<ManagerModel> model;
    };
}

static Test::Suite retroarch("retroarch", [] {
    run("retroarch: arguments, core stems and the core table", [] {
        auto args = RA::splitArguments("-L \"cores/a b_libretro.dll\"  \"%ITEM_FILEPATH%\" -c x.cfg");
        check(args.size() == 5 && args[1] == "cores/a b_libretro.dll" && args[2] == "%ITEM_FILEPATH%", "quotes group words");
        checkEqual(RA::coreStem("cores\\genesis_plus_gx_libretro.dll"), "genesis_plus_gx", "a Windows core");
        checkEqual(RA::coreStem("/usr/lib/libretro/snes9x_libretro.so"), "snes9x", "a Linux core");
        checkEqual(RA::coreStem("mgba_libretro_android.so"), "mgba", "an Android core");
        checkEqual(RA::libraryForStem("genesis_plus_gx"), "Genesis Plus GX", "the table's name");
        checkEqual(RA::libraryForStem("cap32"), "cap32", "not the info file's Caprice32");
        checkEqual(RA::libraryForStem("genesisplusgx"), "Genesis Plus GX", "OnesaUCE's stem");
        checkEqual(RA::libraryForStem("nosuchcore"), "", "unknown");

        Library::LauncherDef l;
        l.executable = "C:/Games/RetroArch/retroarch.exe";
        check(RA::runsRetroArch(l), "retroarch.exe runs RetroArch");
        l.executable = "launch.bat";
        l.arguments = "-L cores/x_libretro.dll \"%ITEM_FILEPATH%\"";
        check(RA::runsRetroArch(l), "-L loads a core");
        l.arguments = "retroarch fceumm_libretro.so \"%ITEM_FILEPATH%\"";
        check(RA::runsRetroArch(l), "a script handed a *_libretro core");
        l.executable = "flatpak";
        l.arguments = "run org.libretro.RetroArch \"%ITEM_FILEPATH%\"";
        check(RA::runsRetroArch(l), "the RetroArch flatpak");
        l.executable = "notepad.exe";
        l.arguments = "\"%ITEM_FILEPATH%\"";
        check(!RA::runsRetroArch(l), "notepad doesn't");
    });

    run("retroarch: config files are read first-wins and edited in place", [] {
        auto map = RA::parseConfig("# c\r\nvideo_smooth = \"true\"\r\nvideo_smooth = \"false\"\r\naudio_volume = -3.0 # trailing\r\nempty =\r\n");
        checkEqual(map["video_smooth"], "true", "the first line for a key wins, as in RetroArch");
        checkEqual(map["audio_volume"], "-3.0", "unquoted values end at a space");
        std::string text = "# keep me\r\nvideo_smooth = \"true\"\r\nother = \"1\"\r\nvideo_smooth = \"x\"\r\n";
        std::string edited = RA::editConfig(text, { { "video_smooth", std::nullopt }, { "aspect_ratio_index", std::string("0") } });
        checkEqual(edited, "# keep me\r\nother = \"1\"\r\naspect_ratio_index = \"0\"\r\n",
                   "every line of a removed key goes, comments and line endings stay, new keys go at the end");
        checkEqual(RA::editConfig("", { { "a", std::string("1") } }), "a = \"1\"\n", "a new file is written with \\n, as RetroArch does");
        checkEqual(RA::editConfig("a = \"1\"", { { "a", std::string("2") } }), "a = \"2\"", "a missing final line break stays missing");
        check(RA::sameValue("0.000000", "0") && RA::sameValue("True", "true") && !RA::sameValue("1", ""), "values compare as RetroArch reads them");
    });

    run("retroarch: detection finds the core, its settings folder and says why not", [] {
        std::string root = makeInstall("ra-detect");
        RA::Setup s = setupOf(root, "genesis");
        check(s.ready(), "genesis is ready: " + s.why);
        checkEqual(s.library, "Genesis Plus GX", "library name");
        check(s.libraryCertain && s.librarySource == "Manage's list of cores", "from the table");
        checkEqual(s.configDir, Library::normalPath(root + "/emu/RetroArch/config"), "\":\" is RetroArch's own folder");
        checkEqual(s.remapDir, Library::normalPath(root + "/emu/RetroArch/config/remaps"), "remaps folder");
        checkEqual(s.coreFile, Library::normalPath(root + "/emu/RetroArch/cores/genesis_plus_gx_libretro.dll"),
                   "the core is found from the working folder (RetroArch's)");
        check(setupOf(root, "genesis2").coreKey() == s.coreKey(), "--libretro= gives the same core");

        RA::Setup notepad = setupOf(root, "notepad");
        check(notepad.state == RA::State::NotRetroArch && contains(notepad.why, "outside RetroArch"), notepad.why);
        RA::Setup gone = setupOf(root, "gone");
        check(gone.state == RA::State::NotFound && contains(gone.why, "RetroArch wasn't found"), gone.why);
        RA::Setup unknown = setupOf(root, "unknown");
        check(unknown.state == RA::State::CoreUnknown && contains(unknown.why, "unknown") && contains(unknown.why, "isn't known"), unknown.why);

        RA::Setup mystery = setupOf(root, "mystery");
        check(mystery.ready() && mystery.library == "Mystery Core" && !mystery.libraryCertain, "the info file's corename, flagged");
        check(!mystery.notes.empty() && contains(mystery.notes[0], "isn't confirmed"), "and the screen says so");
        fs::create_directories(root + "/emu/RetroArch/config/Mystery Core");
        check(setupOf(root, "mystery").libraryCertain, "RetroArch's own folder of that name confirms it");

        fs::remove(root + "/emu/RetroArch/retroarch.cfg");
        RA::Setup noCfg = setupOf(root, "genesis");
        check(noCfg.state == RA::State::NotFound && contains(noCfg.why, "retroarch.cfg"), noCfg.why);
        RA::Environment env = noEnvironment();
        env.os = "windows";
        env.appData = root + "/appdata";
        writeFile(root + "/appdata/RetroArch/retroarch.cfg", "rgui_config_directory = \"default\"\n");
        RA::Setup roaming = RA::detect(root, launcherDef(root, "genesis"), env);
        check(roaming.ready() && contains(roaming.configFile, "appdata"), "%APPDATA%/RetroArch/retroarch.cfg when there's none beside it");
        checkEqual(roaming.configDir, Library::normalPath(root + "/emu/RetroArch/config"), "\"default\" is RetroArch's folder's config on Windows");
    });

    run("retroarch: a game's settings hold only the changed keys and go when back to default", [] {
        std::string root = makeInstall("ra-settings");
        Library::Backups backups(root);
        RA::Target t = RA::gameTarget(setupOf(root, "genesis"), gameRecord(root, "MegaDrive", "sonic.md"));
        checkEqual(t.rom, "sonic", "named after the file without its extension");
        std::string cfg = root + "/emu/RetroArch/config/Genesis Plus GX/sonic.cfg";
        checkEqual(RA::files(t, RA::Tile::Settings).own, Library::normalPath(cfg), "the game's file");

        RA::TileState before = RA::read(t, RA::Tile::Settings, backups);
        check(before.rows.size() == 8, "eight RetroArch rows");
        checkEqual(before.rows[0].choices[0], "Default (Core provided)", "Default names what retroarch.cfg gives");
        check(before.rows[0].current == 0, "no file: every row is Default");

        Library::EditResult r = RA::apply(backups, t, RA::Tile::Settings, { { "aspect", 2 }, { "runahead", 2 } });
        check(r.ok, r.message);
        checkEqual(readFile(cfg), "aspect_ratio_index = \"0\"\nrun_ahead_enabled = \"true\"\n",
                   "only the changed keys (run_ahead_frames = 1 is retroarch.cfg's already)");
        check(backups.hasOriginal(cfg), "an absent marker before the first change");
        RA::TileState after = RA::read(t, RA::Tile::Settings, backups);
        check(after.rows[0].current == 2 && after.rows[0].own && after.ownExists, "4:3, from its own file");
        check(after.canRestore, "Reset to original is offered");

        r = RA::apply(backups, t, RA::Tile::Settings, { { "smooth", 2 } });
        check(r.ok && !contains(readFile(cfg), "video_smooth"), "a value equal to what's shadowed isn't kept: " + readFile(cfg));

        r = RA::apply(backups, t, RA::Tile::Settings, { { "aspect", 0 }, { "runahead", 1 } });
        check(r.ok && !exists(cfg), "back to what retroarch.cfg says: the file goes (" + r.message + ")");

        r = RA::apply(backups, t, RA::Tile::Settings, { { "nosuchrow", 1 } });
        check(!r.ok, "only the curated rows");
        r = RA::apply(backups, t, RA::Tile::Settings, { { "aspect", 9 } });
        check(!r.ok, "only the listed choices");
    });

    run("retroarch: a game's options start as a copy of the core's (one file wins)", [] {
        std::string root = makeInstall("ra-options");
        Library::Backups backups(root);
        std::string folder = root + "/emu/RetroArch/config/Genesis Plus GX/";
        writeFile(folder + "Genesis Plus GX.opt",
                  "genesis_plus_gx_region_detect = \"pal\"\ngenesis_plus_gx_no_sprite_limit = \"disabled\"\ngenesis_plus_gx_bram = \"per bios\"\n");
        RA::Target t = RA::gameTarget(setupOf(root, "genesis"), gameRecord(root, "MegaDrive", "sonic.md"));
        RA::TileState state = RA::read(t, RA::Tile::CoreOptions, backups);
        check(state.rows.size() == 4, "Genesis Plus GX's four curated options");
        const RA::Row* region = nullptr;
        for (const RA::Row& row : state.rows) if (row.id == "genesis_plus_gx_region_detect") region = &row;
        check(region && region->choices[0] == "Default (Europe)" && region->current == 0, "Default is the core file's value");

        Library::EditResult r = RA::apply(backups, t, RA::Tile::CoreOptions, { { "genesis_plus_gx_no_sprite_limit", 2 } });
        check(r.ok, r.message);
        std::string own = readFile(folder + "sonic.opt");
        check(contains(own, "genesis_plus_gx_bram = \"per bios\"") && contains(own, "genesis_plus_gx_region_detect = \"pal\"")
                  && contains(own, "genesis_plus_gx_no_sprite_limit = \"enabled\""),
              "a copy of the core's file with the change: " + own);

        r = RA::apply(backups, t, RA::Tile::CoreOptions, { { "genesis_plus_gx_no_sprite_limit", 0 } });
        check(r.ok && !exists(folder + "sonic.opt"), "equal to the core's again: the game's file goes");

        // The core-wide file itself: Default takes the key out (the core's own default)
        RA::Target core = RA::coreTarget(setupOf(root, "genesis"));
        r = RA::apply(backups, core, RA::Tile::CoreOptions, { { "genesis_plus_gx_region_detect", 0 } });
        check(r.ok && !contains(readFile(folder + "Genesis Plus GX.opt"), "region_detect"), "the core's key goes");
        check(contains(readFile(folder + "Genesis Plus GX.opt"), "genesis_plus_gx_bram"), "the rest stays");
    });

    run("retroarch: controls, using the system's settings and resetting to the original", [] {
        std::string root = makeInstall("ra-controls");
        Library::Backups backups(root);
        RA::Target t = RA::gameTarget(setupOf(root, "genesis"), gameRecord(root, "MegaDrive", "sonic.md"));
        std::string rmp = root + "/emu/RetroArch/config/remaps/Genesis Plus GX/sonic.rmp";
        RA::TileState state = RA::read(t, RA::Tile::Controls, backups);
        check(state.rows.size() == 11 && state.rows[1].id == "btn_a", "the stick and ten buttons");
        checkEqual(state.rows[1].choices[0], "Default (A)", "a button sends itself by default");
        Library::EditResult r = RA::apply(backups, t, RA::Tile::Controls, { { "btn_a", 2 }, { "dpad", 1 } });
        check(r.ok && readFile(rmp) == "input_player1_analog_dpad_mode = \"1\"\ninput_player1_btn_a = \"0\"\n", "B's id, and the stick: " + readFile(rmp));

        // A game file that was there before Manage
        std::string cfg = root + "/emu/RetroArch/config/Genesis Plus GX/streets.cfg";
        writeFile(cfg, "video_smooth = \"true\"\n");
        RA::Target streets = RA::gameTarget(setupOf(root, "genesis"), gameRecord(root, "MegaDrive", "streets.md"));
        r = RA::useCoreSettings(backups, streets, RA::Tile::Settings);
        check(r.ok && !exists(cfg) && backups.hasOriginal(cfg), "its own file goes, kept in the backups: " + r.message);
        r = RA::restoreOriginal(backups, streets, RA::Tile::Settings);
        check(r.ok && readFile(cfg) == "video_smooth = \"true\"\n", "Reset to original puts it back");
        check(!RA::read(streets, RA::Tile::Settings, backups).canRestore, "and then there's nothing to reset");
        r = RA::useCoreSettings(backups, RA::coreTarget(streets.setup), RA::Tile::Settings);
        check(!r.ok, "a core has no system settings to fall back to");

        // RetroArch outside RetroFE's folder: its originals are kept under manage/backups/_external
        std::string elsewhere = makeRoot("ra-controls-elsewhere");
        writeFile(elsewhere + "/retroarch.exe", "x");
        writeFile(elsewhere + "/retroarch.cfg", retroarchCfg);
        writeFile(elsewhere + "/cores/snes9x_libretro.dll", "x");
        Library::LauncherDef outside;
        outside.id = "outside";
        outside.executable = elsewhere + "/retroarch.exe";
        outside.arguments = "-L cores/snes9x_libretro.dll \"%ITEM_FILEPATH%\"";
        RA::Target snes = RA::gameTarget(RA::detect(root, outside, noEnvironment()), gameRecord(root, "SNES", "zelda.sfc"));
        r = RA::apply(backups, snes, RA::Tile::Settings, { { "volume", 1 } });
        std::string own = RA::files(snes, RA::Tile::Settings).own;
        check(r.ok && contains(readFile(own), "audio_volume = \"-6.000000\""), r.message);
        check(contains(backups.backupPath(own), "_external") && backups.hasOriginal(own), "kept as an external original");
    });

    run("retroarch: RetroArch set not to read these files is said", [] {
        std::string root = makeInstall("ra-disabled");
        writeFile(root + "/emu/RetroArch/retroarch.cfg", std::string(retroarchCfg) + "auto_overrides_enable = \"false\"\nauto_remaps_enable = \"false\"\n");
        Library::Backups backups(root);
        RA::Target t = RA::gameTarget(setupOf(root, "genesis"), gameRecord(root, "MegaDrive", "sonic.md"));
        check(contains(RA::read(t, RA::Tile::Settings, backups).disabledNote, "auto_overrides_enable"), "overrides off");
        check(contains(RA::read(t, RA::Tile::Controls, backups).disabledNote, "auto_remaps_enable"), "remaps off");
        check(RA::read(t, RA::Tile::CoreOptions, backups).disabledNote.empty(), "game options are on");
    });

    run("retroarch: the game screens", [] {
        std::string root = makeInstall("ra-game-screens");
        Fixture f(root);
        f.model->setScreen("game", { { "collection", "MegaDrive" }, { "id", "sonic" } });
        checkEqual(f.model->get("retroarch:runs"), "yes", "the link shows for a RetroArch game");
        checkEqual(f.model->get("retroarch:status"), "Genesis Plus GX", "its status names the core");
        f.model->setScreen("game", { { "collection", "Notes" }, { "id", "readme" } });
        checkEqual(f.model->get("retroarch:runs"), "no", "hidden for a game outside RetroArch");
        f.model->setScreen("game", { { "collection", "Unknown" }, { "id", "thing" } });
        checkEqual(f.model->get("retroarch:status"), "Core not known", "says why");

        Forms::Params sonic = { { "collection", "MegaDrive" }, { "id", "sonic" } };
        f.model->setScreen("game-retroarch", sonic);
        checkEqual(f.model->get("retroarch:core"), "Genesis Plus GX (genesis_plus_gx_libretro.dll)", "core");
        auto choices = f.model->options("retroarch:choices:aspect");
        check(choices.size() == 5 && choices[0].second == "Default (Core provided)" && choices[2].second == "4:3", "Default plus four choices");
        checkEqual(f.model->get("retroarch:row:aspect"), "0", "Default");
        check(!f.model->actionProblem("retroarch.save").empty(), "nothing to save yet");
        f.model->set("retroarch:row:aspect", "2");
        checkEqual(f.model->get("retroarch:isDirty"), "yes", "unsaved");
        check(!f.model->leaving({ "game-retroarch", sonic }).empty(), "leaving warns about unsaved changes");
        auto result = f.model->runAction("retroarch.save");
        check(result.ok && contains(result.message, "sonic.cfg"), result.message);
        checkEqual(f.model->get("retroarch:isDirty"), "no", "saved");
        checkEqual(f.model->get("retroarch:canUseCore"), "yes", "Use the system's settings is offered");
        result = f.model->runAction("retroarch.useCore");
        check(result.ok && !exists(root + "/emu/RetroArch/config/Genesis Plus GX/sonic.cfg"), result.message);

        f.model->setScreen("game-core-options", sonic);
        Forms::Rows rows = f.model->rows("retroarch:coreOptions", 50);
        check(rows.total == 4 && rows.items[0].params.at("collection") == "MegaDrive", "options rows open the option with the game");
        Forms::Params option = sonic;
        option["key"] = "genesis_plus_gx_overclock";
        f.model->setScreen("game-core-option", option);
        checkEqual(f.model->get("retroarch:optionTitle"), "CPU speed (less slowdown)", "the option's label");
        f.model->set("retroarch:opt", "3");
        result = f.model->runAction("retroarch.save");
        check(result.ok && result.back, "saving an option goes back to the list");
        check(contains(readFile(root + "/emu/RetroArch/config/Genesis Plus GX/sonic.opt"), "genesis_plus_gx_overclock = \"150\""), "written");
        f.model->setScreen("game-retroarch", sonic);
        checkEqual(f.model->get("retroarch:optionsStatus"), "1 changed for this game", "the link counts what differs from the system's");
        checkEqual(f.model->get("retroarch:controlsStatus"), "The system's", "no remap file of its own");

        f.model->setScreen("game-retroarch", { { "collection", "Unknown" }, { "id", "thing" } });
        check(contains(f.model->get("retroarch:why"), "isn't known"), "the screen says why it's empty");
        checkEqual(f.model->get("retroarch:isReady"), "no", "and shows no rows");
    });

    run("retroarch: the system screens edit the core-wide files and say what they reach", [] {
        std::string root = makeInstall("ra-system-screens");
        Fixture f(root);
        f.model->setScreen("system", { { "id", "MegaDrive" } });
        checkEqual(f.model->get("retroarch:runs"), "yes", "the system link shows");
        Forms::Params md = { { "id", "MegaDrive" } };
        f.model->setScreen("system-retroarch", md);
        std::string reaches = f.model->get("retroarch:reaches");
        check(contains(reaches, "2 systems") && contains(reaches, "Genesis") && contains(reaches, "MegaDrive"), reaches);
        check(contains(f.model->get("retroarch:file"), "Genesis Plus GX.cfg"), f.model->get("retroarch:file"));
        checkEqual(f.model->get("retroarch:canUseCore"), "no", "no system settings to fall back to");
        f.model->set("retroarch:row:integer", "1");
        auto result = f.model->runAction("retroarch.save");
        std::string coreCfg = root + "/emu/RetroArch/config/Genesis Plus GX/Genesis Plus GX.cfg";
        check(result.ok && readFile(coreCfg) == "video_scale_integer = \"true\"\n", result.message);
        checkEqual(f.model->get("retroarch:canRestore"), "yes", "Reset to original");
        result = f.model->runAction("retroarch.restore");
        check(result.ok && !exists(coreCfg), "the core had no file: reset takes it away again");

        f.model->setScreen("system-controls", md);
        f.model->set("retroarch:row:btn_start", "11");
        result = f.model->runAction("retroarch.save");
        check(result.ok && contains(readFile(root + "/emu/RetroArch/config/remaps/Genesis Plus GX/Genesis Plus GX.rmp"),
                                    "input_player1_btn_start = \"-1\""), "Start sends nothing, core-wide");

        f.model->setScreen("system", { { "id", "Gone" } });
        checkEqual(f.model->get("retroarch:status"), "RetroArch not found", "a missing RetroArch is said");
    });
});

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

// Fork compatibility with the OneSauce themes and settings (docs/ONESAUCE-COMPAT.md):
// layout.lua pages, transform="...", %RETROFE_PATH% and baseLayoutPath in settings

#include "TestSupport.h"
#include "../Source/Catalog/Installer.h"
#include "../Source/Catalog/Package.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Graphics/LayoutCompat.h"
#include "../Source/Library/Index.h"
#include "../Source/Library/Properties.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Themes/ThemeProvider.h"
#include "../Source/ThemeEditor/LayoutCheck.h"
#include "../Source/ThemeEditor/LayoutSession.h"
#include "../Source/Utility/Utils.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Test;
namespace fs = std::filesystem;

namespace
{
    std::string normal(const std::string& p) { return fs::path(p).lexically_normal().string(); }
    std::string at(const std::string& root, const std::string& rel) { return normal((fs::path(root) / rel).string()); }

    bool closeTo(float a, float b) { return std::fabs(a - b) < 0.01f; }

    void luaTests()
    {
        run("compat: layout.lua that builds its own XML", [] {
            std::string theme = makeRoot("compat-lua-root");
            writeFile(at(theme, "layout.lua"),
                "local _x = false\n\n-- builds the main page\nfunction build(pageBuilder)\n"
                "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/collections/main.xml\")\nend\n\n"
                "function preUpdate(page)\n  if page then _x = true end\nend\n");
            std::string why;
            checkEqual(LayoutCompat::luaBuildTarget(at(theme, "layout.lua"), theme, &why), at(theme, "collections/main.xml"),
                       "layoutPath is the lua file's folder; hooks after build() don't matter");
        });

        run("compat: a collection's layout.lua points up the tree", [] {
            std::string theme = makeRoot("compat-lua-collection");
            std::string lua = at(theme, "collections/NES/layout/layout.lua");
            writeFile(lua, "function build(pb)\n  pb:buildFromXmlFile(pb.layoutPath .. '/../../single_system.xml')\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(lua, theme), at(theme, "collections/single_system.xml"),
                       "any parameter name, single quotes, .. resolved");
        });

        run("compat: layout.lua through a module's build function", [] {
            std::string theme = makeRoot("compat-lua-module");
            writeFile(at(theme, "lua/luna.lua"),
                "local M = {}\n--[[ helpers\n  end ]]\nfunction M.build(pageBuilder, xmlFile)\n"
                "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/\" .. xmlFile)\nend\n"
                "function M.postUpdate(page)\n  if page then return end\nend\nreturn M\n");
            std::string lua = at(theme, "collections/Atari 2600/layout/layout.lua");
            writeFile(lua, "local luna = require(\"luna\")\n\nfunction build(pageBuilder)\n"
                           "  luna.build(pageBuilder, \"../../non_arcade_games.xml\")\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(lua, theme), at(theme, "collections/non_arcade_games.xml"),
                       "the module's prefix and the argument are joined");
            writeFile(at(theme, "layout.lua"), "local luna = require(\"luna\")\nfunction build(pageBuilder)\n"
                                               "  luna.build(pageBuilder, \"collections/arcade_systems.xml\")\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(at(theme, "layout.lua"), theme), at(theme, "collections/arcade_systems.xml"),
                       "from the theme's root");
        });

        run("compat: layout.lua that does more than build one file isn't guessed at", [] {
            std::string theme = makeRoot("compat-lua-refused");
            std::string why;
            writeFile(at(theme, "a.lua"), "function build(pageBuilder)\n  if x then\n"
                                         "    pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/a.xml\")\n  end\nend\n");
            check(LayoutCompat::luaBuildTarget(at(theme, "a.lua"), theme, &why).empty() && !why.empty(), "a condition");
            writeFile(at(theme, "b.lua"), "function build(pageBuilder)\n"
                                         "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/a.xml\")\n"
                                         "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/b.xml\")\nend\n");
            check(LayoutCompat::luaBuildTarget(at(theme, "b.lua"), theme).empty(), "two builds");
            writeFile(at(theme, "c.lua"), "local m = require(\"missing\")\nfunction build(pageBuilder)\n"
                                         "  m.build(pageBuilder, \"a.xml\")\nend\n");
            check(LayoutCompat::luaBuildTarget(at(theme, "c.lua"), theme, &why).empty(), "a module that isn't there");
            writeFile(at(theme, "d.lua"), "function preUpdate(page)\nend\n");
            check(LayoutCompat::luaBuildTarget(at(theme, "d.lua"), theme).empty(), "no build()");
            writeFile(at(theme, "e.lua"), "-- function build(pageBuilder)\n"
                                         "--   pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/a.xml\")\n-- end\n");
            check(LayoutCompat::luaBuildTarget(at(theme, "e.lua"), theme).empty(), "a commented-out build()");
            check(LayoutCompat::luaBuildTarget(at(theme, "none.lua"), theme).empty(), "no file");
        });
    }

    void transformTests()
    {
        run("compat: transform maps the rectangle's corners onto the quad", [] {
            LayoutCompat::QuadTransform q;
            check(LayoutCompat::parseTransform("(0, 0), (400, 300); (1357, 322), (1700, 308), (1346, 586), (1686, 604)", q),
                  "the common form parses");
            float x = 0, y = 0;
            q.apply(0, 0, x, y);
            check(closeTo(x, 1357) && closeTo(y, 322), "top-left");
            q.apply(400, 0, x, y);
            check(closeTo(x, 1700) && closeTo(y, 308), "top-right");
            q.apply(0, 300, x, y);
            check(closeTo(x, 1346) && closeTo(y, 586), "bottom-left");
            q.apply(400, 300, x, y);
            check(closeTo(x, 1686) && closeTo(y, 604), "bottom-right");

            // A projective map sends the rectangle's centre to where the quad's diagonals cross
            const double x1 = 1357, y1 = 322, x2 = 1686, y2 = 604, x3 = 1700, y3 = 308, x4 = 1346, y4 = 586;
            const double d = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
            const double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / d;
            q.apply(200, 150, x, y);
            check(closeTo(x, static_cast<float>(x1 + t * (x2 - x1))) && closeTo(y, static_cast<float>(y1 + t * (y2 - y1))),
                  "the centre lands on the diagonals' crossing (perspective, not bilinear)");
        });

        run("compat: transform's other spellings", [] {
            LayoutCompat::QuadTransform q;
            check(LayoutCompat::parseTransform("(0, 0), (525, 525); (1251, 256), (1727, 170), (1258, 693), (1762, 679); 50", q),
                  "a trailing number is ignored");
            check(LayoutCompat::parseTransform("(1340, 440, 400, 200), (410, 745), (685, 628), (602, 888), (883, 747), 50", q),
                  "x, y, w, h in one group");
            float x = 0, y = 0;
            q.apply(1740, 640, x, y);
            check(closeTo(x, 883) && closeTo(y, 747), "the rectangle starts at x, y");
            check(LayoutCompat::parseTransform("(0, 0), (525, 525); (1251, 256), (1727, 170), (1258, 693), (1762, 679)>     ", q),
                  "junk after the corners");
            check(LayoutCompat::parseTransform("(0, 0), (10, 10); (0, 0), (20, 0), (0, 20), (20, 20)", q), "a square");
            q.apply(5, 5, x, y);
            check(closeTo(x, 10) && closeTo(y, 10), "an affine quad scales");
        });

        run("compat: transforms that aren't read", [] {
            LayoutCompat::QuadTransform q;
            std::string why;
            check(!LayoutCompat::parseTransform("quad (209, 365, 0, 0), (546, 273, 1, 0), (316, 654, 0, 1), (646, 532, 1, 1), 50", q, &why) &&
                  !why.empty(), "the quad (x, y, u, v) form");
            check(!LayoutCompat::parseTransform("(0, 0), (400, 300); (1, 2), (3, 4)", q), "too few corners");
            check(!LayoutCompat::parseTransform("(0, 0), (0, 300); (1, 2), (3, 4), (5, 6), (7, 8)", q), "no width");
            check(!LayoutCompat::parseTransform("", q), "empty");
        });
    }

    void settingsTests()
    {
        run("compat: %RETROFE_PATH% in settings is the root", [] {
            const std::string saved = Configuration::absolutePath;
            std::string root = makeRoot("compat-settings");
            Configuration::absolutePath = root;
            Configuration config;
            config.importString("", "userPath = %RETROFE_PATH%/../base_assets/layouts\nplain = abc\n");
            std::string value;
            check(config.getPropertyAbsolutePath("userPath", value), "read");
            checkEqual(normal(value), at(root, "../base_assets/layouts"), "expanded in a path");
            config.getProperty("userPath", value);
            checkEqual(value, "%RETROFE_PATH%/../base_assets/layouts", "a plain read stays as written (launchers expand it themselves)");
            config.getProperty("plain", value);
            checkEqual(value, "abc", "other values are as they are");
            Configuration::absolutePath = saved;
        });

        run("compat: a later settings file's base path is the one used", [] {
            Configuration config;
            config.importString("", "baseItemPath = /first\ncollections.NES.list.path = %BASE_ITEM_PATH%/NES/roms\n");
            std::string value;
            config.getProperty("collections.NES.list.path", value);
            checkEqual(value, "/first/NES/roms", "from settings.conf");
            config.importString("", "baseItemPath = /second\n");  // settings1.conf
            config.getProperty("collections.NES.list.path", value);
            checkEqual(value, "/second/NES/roms", "settings1.conf's override, not the first one read");
        });

        run("compat: baseLayoutPath moves the layouts folder", [] {
            const std::string saved = Configuration::absolutePath;
            std::string top = makeRoot("compat-baselayout");
            std::string root = at(top, "appdata/retrofe");
            fs::create_directories(root);
            fs::create_directories(at(top, "base_assets/layouts/Renee"));
            Configuration::absolutePath = root;

            Configuration none;
            none.applyBaseLayoutPath();
            checkEqual(Configuration::layoutsPath(), Utils::combinePath(root, "layouts"), "by default <root>/layouts");

            Configuration config;
            config.importString("", "baseLayoutPath = %RETROFE_PATH%/../../base_assets/layouts\n");
            config.applyBaseLayoutPath();
            checkEqual(normal(Configuration::layoutsPath()), at(top, "base_assets/layouts"), "the setting, expanded and resolved");

            Configuration missing;
            missing.importString("", "baseLayoutPath = ../nowhere\n");
            missing.applyBaseLayoutPath();
            checkEqual(Configuration::layoutsPath(), Utils::combinePath(root, "layouts"), "a folder that isn't there is ignored");

            Configuration::absolutePath = saved;
            none.applyBaseLayoutPath();
        });

        run("compat: Settings' Layout list offers only folders RetroFE can draw, in Manage's order", [] {
            const std::string saved = Configuration::absolutePath;
            std::string top = makeRoot("compat-layout-options");
            std::string root = at(top, "appdata/retrofe");
            for (const char* page : { "layout.xml", "splash.xml" }) {
                writeFile(at(top, std::string("base_assets/layouts/Renee/") + page), "<layout/>");
                writeFile(at(top, std::string("base_assets/layouts/amiga/") + page), "<layout/>");
            }
            writeFile(at(top, "base_assets/layouts/Aeon Nox/layout.lua"), "function build(pb)\nend\n");
            writeFile(at(top, "base_assets/layouts/Aeon Nox/splash.xml"), "<layout/>");
            writeFile(at(top, "base_assets/layouts/default/README.md"), "Put live wallpaper videos here.\n");   // OneSauce's
            writeFile(at(top, "base_assets/layouts/No splash/layout.xml"), "<layout/>");   // RetroFE would stop at start-up
            fs::create_directories(at(top, "base_assets/layouts/Half made"));
            writeFile(at(root, "settings.conf"), "layout = Half made\nbaseLayoutPath = %RETROFE_PATH%/../../base_assets/layouts\n");
            Configuration::absolutePath = root;
            {
                Configuration config;
                config.importString("", "baseLayoutPath = %RETROFE_PATH%/../../base_assets/layouts\n");
                config.applyBaseLayoutPath();
                ManagerModel model(config);
                std::string names;
                for (const auto& option : model.options("layouts")) names += (names.empty() ? "" : ", ") + option.second;
                checkEqual(names, std::string("Built-in, Aeon Nox, amiga, Half made, Renee"),
                           "a layout.lua counts; no splash page or no main page is left out unless it's the one set; case ignored");
            }
            Configuration::absolutePath = saved;
            Configuration none;
            none.applyBaseLayoutPath();
        });

        run("compat: the library expands %RETROFE_PATH% too", [] {
            std::string top = makeRoot("compat-library");
            std::string root = at(top, "appdata/retrofe");
            fs::create_directories(root);
            Library::Properties props(root);
            props.importText("", "baseMediaPath = %RETROFE_PATH%/../../base_assets/collections\n", "");
            checkEqual(normal(props.mediaPath("NES", "logo", false)), at(top, "base_assets/collections/NES/medium_artwork/logo"),
                       "media under the expanded base path");
        });
    }

    void luaEdgeTests()
    {
        run("compat: layout.lua paths with or without a leading slash", [] {
            std::string theme = makeRoot("compat-lua-slash");
            writeFile(at(theme, "a.lua"), "function build(pb)\n  pb:buildFromXmlFile(pb.layoutPath .. \"collections/main.xml\")\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(at(theme, "a.lua"), theme), at(theme, "collections/main.xml"), "no slash");
            writeFile(at(theme, "lua/m.lua"), "local M = {}\nfunction M.build(pb, f)\n  pb:buildFromXmlFile(pb.layoutPath .. f)\nend\nreturn M\n");
            writeFile(at(theme, "b.lua"), "local m = require('m')\nfunction build(pb)\n  m.build(pb, \"collections/x.xml\")\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(at(theme, "b.lua"), theme), at(theme, "collections/x.xml"), "a module with no prefix");
        });

        run("compat: keywords inside strings and long comments don't confuse the lua reader", [] {
            std::string theme = makeRoot("compat-lua-strings");
            writeFile(at(theme, "a.lua"), "function build(pageBuilder)\n"
                                         "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/collections/end.xml\")\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(at(theme, "a.lua"), theme), at(theme, "collections/end.xml"), "\"end\" in the path");
            writeFile(at(theme, "b.lua"), "function build(pageBuilder)\n"
                                         "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. '/do.xml')\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(at(theme, "b.lua"), theme), at(theme, "do.xml"), "\"do\" in the path");
            writeFile(at(theme, "c.lua"), "--[==[ function build(pageBuilder)\n  if x then end\n]] still a comment ]==]\n"
                                         "local note = [[ function build(x) end ]]\n"
                                         "function build(pageBuilder)\n"
                                         "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/real.xml\")\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(at(theme, "c.lua"), theme), at(theme, "real.xml"), "long comments and long strings skipped");
            writeFile(at(theme, "d.lua"), "function build(pageBuilder)\n  -- if this were code it would be refused\n"
                                         "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/a.xml\") -- end\nend\n");
            checkEqual(LayoutCompat::luaBuildTarget(at(theme, "d.lua"), theme), at(theme, "a.xml"), "line comments in the body");
        });
    }

    void transformEdgeTests()
    {
        run("compat: transform numbers that can't be read are refused, never thrown", [] {
            LayoutCompat::QuadTransform q;
            std::string why;
            const std::string huge(400, '9');
            check(!LayoutCompat::parseTransform("(" + huge + ", 0), (100, 100); (0, 0), (100, 0), (0, 100), (100, 100)", q, &why) && !why.empty(),
                  "a number too big for a double");
            check(!LayoutCompat::parseTransform("(0, 0), (inf, 100); (0, 0), (100, 0), (0, 100), (100, 100)", q), "inf");
            check(!LayoutCompat::parseTransform("(0, 0), (nan, 100); (0, 0), (100, 0), (0, 100), (100, 100)", q), "nan");
            check(!LayoutCompat::parseTransform("(0, 0), (100, 100); (0, 0), (100, 0), (0, 100), (100, 2e9)", q), "far out of range");
            check(LayoutCompat::parseTransform("(0, 0), (1e3, 1.0E3); (0, 0), (1000, 0), (0, 1000), (1000, 1000)", q), "exponents are one number");
            float x = 0, y = 0;
            check(q.apply(500, 500, x, y) && closeTo(x, 500) && closeTo(y, 500), "and read right");
        });

        run("compat: transform corners must make a convex quad in order", [] {
            LayoutCompat::QuadTransform q;
            std::string why;
            check(!LayoutCompat::parseTransform("(0, 0), (100, 100); (0, 0), (100, 0), (100, 100), (0, 100)", q, &why) && !why.empty(),
                  "bottom corners swapped (a bow tie)");
            check(!LayoutCompat::parseTransform("(0, 0), (100, 100); (0, 0), (100, 0), (0, 100), (30, 30)", q), "concave");
            check(!LayoutCompat::parseTransform("(0, 0), (100, 100); (0, 0), (100, 0), (200, 0), (300, 0)", q), "all in a line");
            check(LayoutCompat::parseTransform("(0, 0), (100, 100); (100, 0), (0, 0), (100, 100), (0, 100)", q), "a mirror image is fine");
        });

        run("compat: points far outside the rectangle aren't drawn or are clamped", [] {
            LayoutCompat::QuadTransform q;
            // the right edge is shorter: going right the picture shrinks towards x = 125,
            // going left it grows without bound and then folds back (the horizon)
            check(LayoutCompat::parseTransform("(0, 0), (100, 100); (0, 0), (100, 40), (0, 100), (100, 60)", q), "parsed");
            float x = 0, y = 0;
            check(q.apply(150, 50, x, y) && x > 100 && x < 125, "a bit outside still maps");
            check(q.apply(100000, 50, x, y) && x < 125, "far right approaches the vanishing point");
            check(!q.apply(-100000, 50, x, y), "past the horizon isn't drawn");
            bool clamped = true;
            bool hidden = false;
            for (float px = -1.0f; px > -200.0f; px -= 0.01f) {
                if (q.apply(px, 0, x, y)) clamped = clamped && std::fabs(x) <= LayoutCompat::maxCoordinate && std::fabs(y) <= LayoutCompat::maxCoordinate;
                else hidden = true;
            }
            check(clamped && hidden, "near the horizon points are clamped, past it hidden");
        });
    }

    void pathTests()
    {
        run("compat: layout_preferred and scrollingText find their files", [] {
            std::string root = makeRoot("compat-paths");
            std::string layout = at(root, "layouts/T/collections/Cave/system_artwork");
            std::string normalFolder = at(root, "media/Cave/system_artwork");
            writeFile(at(layout, "cabinet.png"), "png");
            writeFile(at(normalFolder, "marquee.png"), "png");
            const std::vector<std::string> ext = { "png", "jpg" };
            checkEqual(LayoutCompat::preferredFolder(layout, normalFolder, "cabinet", ext), layout, "the layout's copy");
            checkEqual(LayoutCompat::preferredFolder(layout, normalFolder, "marquee", ext), normalFolder, "else the usual folder");

            std::string theme = at(root, "base_assets/layouts/Cafe80s");
            std::string page = at(theme, "collections/02 ARCADE ALL/layout");
            fs::create_directories(page);
            writeFile(at(root, "base_assets/collections/_common/text/menu.txt"), "text");
            checkEqual(LayoutCompat::themeFile(page, theme, "../../collections/_common/text/menu.txt"),
                       at(root, "base_assets/collections/_common/text/menu.txt"), "relative to the theme when not to the page");
            writeFile(at(page, "own.txt"), "text");
            checkEqual(LayoutCompat::themeFile(page, theme, "own.txt"), at(page, "own.txt"), "the page's folder first");
            checkEqual(LayoutCompat::themeFile(page, theme, "none.txt"), "", "missing");
        });
    }

    // --theme-check on OneSauce's themes: names ONESAUCE-COMPAT.md ignores are set apart
    void themeCheckTests()
    {
        run("compat: the layout check sets OneSauce's ignored names apart, keeping real problems", [] {
            // Bits of Cafe80s, Aeon Nox and Renee pages, plus two real problems
            const std::string xml =
                "<layout width=\"1920\" height=\"1080\" loadfontSize=\"64\">"
                "<reloadableImage type=\"logo\" Height=\"540\" maxwidth=\"300\" keepOldWhileLoading=\"true\">"
                "<onEmpty><set duration=\"0\"><animate type=\"alpha\" to=\"0\"/></set></onEmpty>"
                "<onMenuScroll><stop/></onMenuScroll>"
                "</reloadableImage>"
                "<scrollingText src=\"text/menu.txt\" pauseTime=\"1\"/>"
                "<reloadableScrollingText type=\"title\" uppercase=\"1\"/>"
                "<video src=\"v.mp4\" hwdec=\"yes\"/>"
                "<menu mode=\"system\" algorithm=\"easeincircular\"><itemDefaults/></menu>"
                "<Reloadableimage type=\"logo\"/>"
                "<image src=\"a.png\" colour=\"red\"/>"          // real: a name nobody documented
                "<reloadablePanningImage type=\"artwork_front\"/>"  // real: draws differently (nothing)
                "</layout>";
            std::vector<std::string> ignored;
            const std::vector<std::string> problems = ThemeEditor::checkLayoutXml(xml, &ignored);
            std::string all;
            for (const std::string& p : problems) all += p + "; ";
            check(problems.size() == 2, "two real problems: " + all);
            check(all.find("colour") != std::string::npos && all.find("reloadablePanningImage") != std::string::npos, "the real ones: " + all);
            for (const char* name : { "loadfontSize", "Height", "maxwidth", "keepOldWhileLoading", "<onEmpty>", "<stop>", "pauseTime",
                                      "uppercase", "hwdec", "algorithm", "mode=\"system\" on <menu>", "<Reloadableimage>" }) {
                check(std::find(ignored.begin(), ignored.end(), name) != ignored.end(), std::string("set apart: ") + name);
            }
            check(ignored.size() == 12, "each once: " + std::to_string(ignored.size()));
            check(ThemeEditor::checkLayoutXml(xml).size() == problems.size() + ignored.size(),
                  "without the list (the theme editor), every one is still a problem");
            // Scoped to the elements OneSauce has them on: elsewhere they're problems
            ignored.clear();
            check(ThemeEditor::checkLayoutXml("<layout width=\"1\" height=\"1\"><text value=\"a\" Height=\"5\"/>"
                                              "<reloadableVideo imageType=\"logo\" static=\"1\"/></layout>", &ignored).size() == 2 &&
                  ignored.empty(), "Height on text, static on reloadableVideo: still problems");
            // Media modes stay strict: mode="system" is only OneSauce's on a menu
            ignored.clear();
            check(ThemeEditor::checkLayoutXml("<layout width=\"1\" height=\"1\"><reloadableImage type=\"logo\" mode=\"weird\"/></layout>", &ignored).size() == 1 &&
                  ignored.empty(), "an unknown mode is still a problem");
        });
    }

    // OneSauce's media folders: system_artwork/video/<any name>.mp4 for system_artwork/video.mp4
    void mediaFolderTests()
    {
        run("compat: media folders list their files in natural order", [] {
            check(LayoutCompat::naturalLess("ARCADES 2.mp4", "ARCADES 10.mp4"), "2 before 10");
            check(!LayoutCompat::naturalLess("ARCADES 10.mp4", "ARCADES 2.mp4"), "not 10 before 2");
            check(LayoutCompat::naturalLess("arcade3.mp4", "Arcade Classics.mp4") == LayoutCompat::naturalLess("arcade3.mp4", "arcade classics.mp4"),
                  "case ignored");
            check(LayoutCompat::naturalLess("Racing-1.mp4", "Racing-2.mp4"), "dash-numbered");
            check(LayoutCompat::naturalLess("v02.mp4", "v3.mp4"), "leading zeros are numbers too");
            check(!LayoutCompat::naturalLess("same.mp4", "same.mp4"), "irreflexive");

            // A strict weak ordering (std::sort needs one): leading zeros, case,
            // digits against punctuation and letters, prefixes
            const std::vector<std::string> names = {
                "", "a", "A", "a1", "A1", "a01", "a001", "a1b", "a01b", "a1B", "a2", "a10", "a9", "a-1", "a.1", "a_1",
                "a 1", "a:1", "a1-", "a1.", "1", "01", "10", "1a", "a1a1", "a1a01", "a01a1", "~", "Z9", "z10",
            };
            bool irreflexive = true, asymmetric = true, transitive = true, equivalence = true;
            auto less = [](const std::string& x, const std::string& y) { return LayoutCompat::naturalLess(x, y); };
            for (const auto& x : names) {
                irreflexive = irreflexive && !less(x, x);
                for (const auto& y : names) {
                    asymmetric = asymmetric && !(less(x, y) && less(y, x));
                    for (const auto& z : names) {
                        if (less(x, y) && less(y, z) && !less(x, z)) {
                            transitive = false;
                        }
                        // incomparability is transitive too
                        auto same = [&](const std::string& p, const std::string& q) { return !less(p, q) && !less(q, p); };
                        if (same(x, y) && same(y, z) && !same(x, z)) {
                            equivalence = false;
                        }
                    }
                }
            }
            check(irreflexive && asymmetric && transitive && equivalence, "strict weak ordering over the sample");
            check(LayoutCompat::naturalLess("a01", "a1") != LayoutCompat::naturalLess("a1", "a01"), "a01 and a1 are ordered, one way");
            check(LayoutCompat::naturalLess("a-1", "a1") && LayoutCompat::naturalLess("a1", "aa"), "punctuation, digits, letters");

            std::string root = makeRoot("compat-media-folder");
            std::string folder = at(root, "collections/1 ARCADES/system_artwork/video");
            for (const char* name : { "ARCADES 10.mp4", "ARCADES 2.mp4", "ARCADES 1.MP4", "notes.txt", "cover.png" }) {
                writeFile(at(folder, name), "x");
            }
            fs::create_directories(at(folder, "sub.mp4"));   // a folder, not a file
            const std::vector<std::string> video = { "mp4", "avi", "mkv" };
            const std::vector<std::string> files = LayoutCompat::mediaFolderFiles(folder, video);
            check(files.size() == 3, "only the videos: " + std::to_string(files.size()));
            if (files.size() == 3) {
                checkEqual(fs::path(files[0]).filename().string(), "ARCADES 1.MP4", "first (any case of the extension)");
                checkEqual(fs::path(files[1]).filename().string(), "ARCADES 2.mp4", "second");
                checkEqual(fs::path(files[2]).filename().string(), "ARCADES 10.mp4", "tenth last");
            }
            check(LayoutCompat::mediaFolderFiles(at(root, "nothing"), video).empty(), "no folder: nothing");
            check(LayoutCompat::mediaFolderFiles(at(folder, "ARCADES 2.mp4"), video).empty(), "a file isn't a folder");
        });

        run("compat: which file of a media folder shows", [] {
            const std::vector<std::string> files = { "a/1.mp4", "a/2.mp4", "a/3.mp4" };
            check(LayoutCompat::pickMediaFile(files, "", false, 0) == 0, "nothing showing: random 0");
            check(LayoutCompat::pickMediaFile(files, "", false, 7) == 1, "nothing showing: random 7 -> 1");
            check(LayoutCompat::pickMediaFile(files, "b/other.mp4", false, 5) == 2, "another folder's file showing: random");
            check(LayoutCompat::pickMediaFile(files, "a/2.mp4", false, 0) == 1, "the one showing stays");
            check(LayoutCompat::pickMediaFile(files, "a/2.mp4", true, 0) == 2, "another: the next");
            check(LayoutCompat::pickMediaFile(files, "a/3.mp4", true, 0) == 0, "another: wraps");
            check(LayoutCompat::pickMediaFile({ "a/1.mp4" }, "a/1.mp4", true, 9) == 0, "one file: always it");
        });

        run("compat: the media lookup takes a file first, else a media folder", [] {
            const std::string saved = Configuration::absolutePath;
            std::string root = makeRoot("compat-media-lookup");
            Configuration::absolutePath = root;
            const std::vector<std::string> video = { "mp4", "avi" };
            // A collection with one video file, and one with a folder of them (K:'s 1 CONSOLES and 1 ARCADES)
            writeFile(at(root, "media/1 CONSOLES/system_artwork/video.mp4"), "x");
            writeFile(at(root, "media/1 CONSOLES/system_artwork/video/extra.mp4"), "x");   // the file wins, as upstream
            writeFile(at(root, "media/1 ARCADES/system_artwork/video/ARCADES 1.mp4"), "x");
            writeFile(at(root, "media/1 ARCADES/system_artwork/video/ARCADES 2.mp4"), "x");
            std::string file;
            check(LayoutCompat::findMedia(at(root, "media/1 CONSOLES/system_artwork/video"), video, "", false, 1, file) &&
                  fs::path(file).filename() == "video.mp4", "a single file is used as before: " + file);
            check(LayoutCompat::findMedia(at(root, "media/1 ARCADES/system_artwork/video"), video, "", false, 1, file) &&
                  fs::path(file).filename() == "ARCADES 2.mp4", "a folder: random 1 picks the second: " + file);
            const std::string showing = file;
            check(LayoutCompat::findMedia(at(root, "media/1 ARCADES/system_artwork/video"), video, showing, false, 0, file) && file == showing,
                  "kept while showing");
            check(LayoutCompat::findMedia(at(root, "media/1 ARCADES/system_artwork/video"), video, showing, true, 0, file) &&
                  fs::path(file).filename() == "ARCADES 1.mp4", "another: the next, wrapping: " + file);
            check(!LayoutCompat::findMedia(at(root, "media/1 ARCADES/system_artwork/logo"), video, "", false, 0, file), "nothing there");
            // Relative to RetroFE's folder, as findMatchingFile takes it
            check(LayoutCompat::findMedia("media/1 ARCADES/system_artwork/video", video, "", false, 0, file) &&
                  fs::path(file).filename() == "ARCADES 1.mp4", "relative prefix: " + file);
            // layout_preferred counts a folder in the layout as the layout's copy
            writeFile(at(root, "layouts/Cafe80s/collections/1 ARCADES/medium_artwork/video/default/ARCADES 1.mp4"), "x");
            checkEqual(LayoutCompat::preferredFolder(at(root, "layouts/Cafe80s/collections/1 ARCADES/medium_artwork/video"), "normal",
                                                     "default", video),
                       at(root, "layouts/Cafe80s/collections/1 ARCADES/medium_artwork/video"), "layout_preferred sees the folder");
            Configuration::absolutePath = saved;
        });

        run("compat: media folders only at the media steps, and a file still wins", [] {
            std::string root = makeRoot("compat-media-steps");
            static const std::vector<std::string> video = { "mp4", "avi" };
            static const std::vector<std::string> images = { "png", "jpg" };          // ReloadableMedia's check
            static const std::vector<std::string> builderImages = { "gif", "webp", "png", "jpg" };   // what the image builder takes
            // A collection's media folder: a folder of videos for the game
            writeFile(at(root, "media/PC/medium_artwork/video/Half-Life/intro 1.mp4"), "x");
            writeFile(at(root, "media/PC/medium_artwork/video/Half-Life/intro 2.mp4"), "x");
            // (every file first: folder listings are cached for the run, as RetroFE's are)
            writeFile(at(root, "roms/Half-Life/video/cutscene.mp4"), "x");
            writeFile(at(root, "media/PC/medium_artwork/video/Doom.mp4"), "x");
            writeFile(at(root, "media/PC/medium_artwork/video/Doom/other.mp4"), "x");
            writeFile(at(root, "media/PC/medium_artwork/logo/Doom.webp"), "x");
            writeFile(at(root, "media/PC/medium_artwork/logo/Doom/a.png"), "x");
            LayoutCompat::MediaLookup m = LayoutCompat::lookupMediaStep(at(root, "media/PC/medium_artwork/video"), "Half-Life", false,
                                                                        video, video, "", false, 0);
            check(m.fromFolder && fs::path(m.file).filename() == "intro 1.mp4", "a media step uses the folder: " + m.file);
            // The game's own folder (list.romHierarchy): its video\ is the game's cutscenes, not art
            m = LayoutCompat::lookupMediaStep(at(root, "roms/Half-Life"), "video", true, video, video, "", false, 0);
            check(!m.fromFolder && m.file.empty(), "the ROM folder step doesn't: " + m.file);
            m = LayoutCompat::lookupMediaStep(at(root, "roms/Half-Life"), "video", false, video, video, "", false, 0);
            check(m.fromFolder, "(the same folder at a media step would)");
            // A file the step's own list finds: as upstream
            m = LayoutCompat::lookupMediaStep(at(root, "media/PC/medium_artwork/video"), "Doom", false, video, video, "", false, 0);
            check(!m.fromFolder && fs::path(m.file).filename() == "Doom.mp4", "the file wins: " + m.file);
            // A .webp only the image builder takes still wins over a folder
            m = LayoutCompat::lookupMediaStep(at(root, "media/PC/medium_artwork/logo"), "Doom", false, images, builderImages, "", false, 0);
            check(!m.fromFolder && m.file.empty(), "a webp file: left to the builder as upstream");
            // Nothing at all, including under a folder that doesn't exist
            m = LayoutCompat::lookupMediaStep(at(root, "media/PC/medium_artwork/logo"), "Quake", false, images, builderImages, "", false, 0);
            check(!m.fromFolder && m.file.empty(), "nothing there");
            m = LayoutCompat::lookupMediaStep(at(root, "media/Missing/medium_artwork/logo"), "Quake", false, images, builderImages, "", false, 0);
            check(!m.fromFolder && m.file.empty(), "no such parent folder");
            std::string file;
            check(!LayoutCompat::findMediaFolderFile(at(root, "media/Missing/x/y"), video, "", false, 0, file), "folder lookup under a missing parent");
        });
    }

    // A OneSauce-shaped install: root appdata/retrofe, layouts in base_assets/layouts
    std::string splitInstall(const std::string& name, std::string& layouts)
    {
        std::string top = makeRoot(name);
        std::string root = at(top, "appdata/retrofe");
        layouts = at(top, "base_assets/layouts");
        writeFile(at(root, "settings.conf"), "layout = Lua\nbaseLayoutPath = %RETROFE_PATH%/../../base_assets/layouts\n");
        writeFile(at(layouts, "Lua/layout.lua"), "function build(pageBuilder)\n"
                                                "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/collections/main.xml\")\nend\n");
        writeFile(at(layouts, "Lua/collections/main.xml"), "<layout width=\"1280\" height=\"720\"><menu/></layout>");
        writeFile(at(layouts, "Lua/collections/NES/layout/layout.lua"), "function build(pageBuilder)\n"
                  "  pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. \"/../../main.xml\")\nend\n");
        writeFile(at(layouts, "Lua/splash.xml"), "<layout width=\"1280\" height=\"720\"/>");
        writeFile(at(layouts, "Hand/layout.xml"), "<layout width=\"640\" height=\"480\"/>");
        return root;
    }

    void layoutsFolderTests()
    {
        run("compat: the Themes list reads the layouts folder and lua-only themes", [] {
            std::string layouts;
            std::string root = splitInstall("compat-themes-list", layouts);
            std::vector<Themes::ThemeFacts> themes = Themes::scanThemes(root, "Lua", layouts);
            check(themes.size() == 2, "both themes, from base_assets/layouts");
            const Themes::ThemeFacts* lua = nullptr;
            for (const auto& t : themes) if (t.folder == "Lua") lua = &t;
            check(lua && lua->hasLayout && lua->width == 1280 && lua->height == 720, "the main page a layout.lua builds counts");
            check(lua && lua->collectionPages == std::vector<std::string>({ "NES" }), "and a collection's");
            check(Themes::scanThemes(root, "Lua").empty(), "<root>/layouts has none (the old split)");
            checkEqual(Themes::layoutsFolder(root, ""), (fs::path(root) / "layouts").string(), "default folder");
        });

        run("compat: the library reads layouts where RetroFE does", [] {
            std::string layouts;
            std::string root = splitInstall("compat-library-layouts", layouts);
            Library::ScanOptions options;
            options.root = root;
            options.games = false;
            auto index = Library::buildIndex(options);
            check(index && index->layouts.size() == 2, "baseLayoutPath from the settings");
            bool luaMain = false;
            for (const auto& l : index->layouts) luaMain = luaMain || (l.name == "Lua" && l.hasLayout && l.width == 1280);
            check(luaMain, "a layout.lua's main page counts");
            options.layoutsPath = at(root, "nowhere");
            auto none = Library::buildIndex(options);
            check(none && none->layouts.empty(), "the folder it's given wins");
        });

        run("compat: the layout editor opens pages in the layouts folder", [] {
            std::string layouts;
            std::string root = splitInstall("compat-layout-session", layouts);
            ThemeEditor::LayoutSession session;
            std::string error;
            check(session.open(root, "Hand", "layout.xml", error, layouts), "opened: " + error);
            checkEqual(normal(session.file()), at(layouts, "Hand/layout.xml"), "from base_assets/layouts");
            ThemeEditor::LayoutSession old;
            check(!old.open(root, "Hand", "layout.xml", error), "not under <root>/layouts");
        });

        run("compat: catalog layouts go to the layouts folder, but never one holding the root", [] {
            std::string layouts;
            std::string root = splitInstall("compat-catalog", layouts);
            writeFile(at(root, "dl/download.tmp"), "<layout/>");
            auto package = [](const std::string& id) {
                Catalog::Package p;
                p.id = id;
                p.name = id;
                p.kind = "layout";
                p.version = "1";
                Catalog::PackageFile file;
                file.url = "https://h.org/files/extra.xml";
                file.target = "layouts";
                file.path = "Blue";
                p.files.push_back(file);
                return p;
            };
            Catalog::Targets targets;
            targets.root = root;
            targets.collections = at(root, "collections");
            targets.items = targets.collections;
            targets.media = targets.collections;
            targets.meta = at(root, "meta");
            targets.layouts = layouts;
            std::string error;
            Catalog::Installer installer(at(root, "catalog"), targets);
            check(installer.install(package("blue"), { at(root, "dl/download.tmp") }, error), "installed: " + error);
            check(fs::exists(at(layouts, "Blue/extra.xml")), "into base_assets/layouts");

            targets.layouts = at(root, "..");   // a layouts folder that holds the RetroFE folder
            Catalog::Installer unsafe(at(root, "catalog2"), targets);
            check(!unsafe.install(package("red"), { at(root, "dl/download.tmp") }, error) && error.find("holds") != std::string::npos,
                  "refused: " + error);
        });
    }
}

static Test::Suite compatSuite("compat", [] {
    luaTests();
    luaEdgeTests();
    transformTests();
    transformEdgeTests();
    pathTests();
    mediaFolderTests();
    themeCheckTests();
    settingsTests();
    layoutsFolderTests();
});

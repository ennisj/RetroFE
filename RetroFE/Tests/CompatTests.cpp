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
#include "../Source/Database/Configuration.h"
#include "../Source/Graphics/LayoutCompat.h"
#include "../Source/Library/Properties.h"
#include "../Source/Utility/Utils.h"
#include <cmath>
#include <filesystem>
#include <string>

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
}

static Test::Suite compatSuite("compat", [] {
    luaTests();
    transformTests();
    settingsTests();
});

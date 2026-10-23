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

// Which of a layout's files draws a page (Graphics/LayoutPages), and the built-in default layout

#include "TestSupport.h"
#include "../Source/Builtin/Builtin.h"
#include "../Source/Graphics/LayoutPages.h"
#include "../Source/Utility/Utils.h"
#include <filesystem>
#include <string>

using namespace Test;

namespace
{
    std::string path(const std::string& root, const std::string& a, const std::string& b = "", const std::string& c = "")
    {
        std::string p = Utils::combinePath(root, a);
        if (!b.empty()) p = Utils::combinePath(p, b);
        if (!c.empty()) p = Utils::combinePath(p, c);
        return p;
    }

    void touch(const std::string& file) { writeFile(file, "<layout/>"); }

    void runLayoutPagesTests()
    {
        run("built-in settings use Aeon Nox", [] {
            std::string settings = Builtin::settings;
            check(settings.find("\nlayout = Aeon Nox\n") != std::string::npos, "layout = Aeon Nox");
        });

        run("a collection's own pages come first, then _menu's for menus", [] {
            std::string layout = makeRoot("layoutpages-collections");
            std::filesystem::create_directories(path(layout, "collections", "_menu", "layout"));
            std::filesystem::create_directories(path(layout, "collections", "Home", "layout"));

            checkEqual(LayoutPages::collectionFolder(layout, "Home", true), path(layout, "collections", "Home", "layout"), "own pages, menu");
            checkEqual(LayoutPages::collectionFolder(layout, "Home", false), path(layout, "collections", "Home", "layout"), "own pages, games");
            checkEqual(LayoutPages::collectionFolder(layout, "Consoles", true), path(layout, "collections", "_menu", "layout"), "a menu uses _menu");
            checkEqual(LayoutPages::collectionFolder(layout, "Arcade", false), layout, "games use the layout's pages");

            std::string plain = makeRoot("layoutpages-nomenu");
            checkEqual(LayoutPages::collectionFolder(plain, "Main", true), plain, "no _menu: the layout's pages");
        });

        run("form pages follow the page collections are drawn with", [] {
            std::string forms = makeRoot("layoutpages-forms");
            checkEqual(LayoutPages::formFile(forms, "settings", "layout"), "", "nothing styled");

            touch(path(forms, "layout.xml"));
            checkEqual(LayoutPages::formFile(forms, "settings", "layout 4x3"), path(forms, "layout.xml"), "shared page when there's no 4x3 one");

            touch(path(forms, "package", "layout.xml"));
            checkEqual(LayoutPages::formFile(forms, "package", "layout"), path(forms, "package", "layout.xml"), "screen's own page");
            checkEqual(LayoutPages::formFile(forms, "settings", "layout"), path(forms, "layout.xml"), "other screens share");

            touch(path(forms, "layout 4x3.xml"));
            checkEqual(LayoutPages::formFile(forms, "settings", "layout 4x3"), path(forms, "layout 4x3.xml"), "shared 4x3 page");
            checkEqual(LayoutPages::formFile(forms, "package", "layout 4x3"), path(forms, "layout 4x3.xml"), "the right shape before the screen's own 16:9 page");

            touch(path(forms, "package", "layout 4x3.xml"));
            checkEqual(LayoutPages::formFile(forms, "package", "layout 4x3"), path(forms, "package", "layout 4x3.xml"), "screen's own 4x3 page");
            checkEqual(LayoutPages::formFile(forms, "package", ""), path(forms, "package", "layout.xml"), "no page name means layout");
        });

        run("the keyboard page follows it too", [] {
            std::string forms = makeRoot("layoutpages-keyboard");
            checkEqual(LayoutPages::keyboardFile(forms, "layout 4x3"), "", "nothing styled");
            touch(path(forms, "keyboard", "layout.xml"));
            checkEqual(LayoutPages::keyboardFile(forms, "layout 4x3"), path(forms, "keyboard", "layout.xml"), "falls back to layout.xml");
            touch(path(forms, "keyboard", "layout 4x3.xml"));
            checkEqual(LayoutPages::keyboardFile(forms, "layout 4x3"), path(forms, "keyboard", "layout 4x3.xml"), "4x3 page");
            checkEqual(LayoutPages::keyboardFile(forms, "layout"), path(forms, "keyboard", "layout.xml"), "16:9 page");
        });
    }

    Test::Suite layoutPages("layout pages", runLayoutPagesTests);
}

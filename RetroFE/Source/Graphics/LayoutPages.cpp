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
#include "LayoutPages.h"
#include "../Utility/Utils.h"
#include <filesystem>
#include <initializer_list>

namespace fs = std::filesystem;

namespace
{
    std::string firstFile(std::initializer_list<std::string> candidates)
    {
        std::error_code ec;
        for (const std::string& candidate : candidates) {
            if (fs::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
        return "";
    }
}

namespace LayoutPages
{

std::string collectionFolder(const std::string& layoutFolder, const std::string& collection, bool isMenu)
{
    std::error_code ec;
    std::string own = Utils::combinePath(layoutFolder, "collections", collection, "layout");
    if (!collection.empty() && fs::is_directory(own, ec)) {
        return own;
    }
    std::string menu = Utils::combinePath(layoutFolder, "collections", "_menu", "layout");
    if (isMenu && fs::is_directory(menu, ec)) {
        return menu;
    }
    return layoutFolder;
}

std::string formFile(const std::string& formsFolder, const std::string& screen, const std::string& page)
{
    std::string name = (page.empty() ? std::string("layout") : page) + ".xml";
    return firstFile({
        Utils::combinePath(formsFolder, screen, name),
        Utils::combinePath(formsFolder, name),
        Utils::combinePath(formsFolder, screen, "layout.xml"),
        Utils::combinePath(formsFolder, "layout.xml"),
    });
}

std::string keyboardFile(const std::string& formsFolder, const std::string& page)
{
    std::string name = (page.empty() ? std::string("layout") : page) + ".xml";
    return firstFile({
        Utils::combinePath(formsFolder, "keyboard", name),
        Utils::combinePath(formsFolder, "keyboard", "layout.xml"),
    });
}

}

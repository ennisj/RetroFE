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
#pragma once

#include <string>

// Which of a layout's files draws a page. No SDL here, so retrofe_tests can check it.
namespace LayoutPages
{
    // The folder a collection's pages come from, in the layout's folder:
    // collections/<collection>/layout when the layout has it, else
    // collections/_menu/layout for a collection of collections (isMenu), else
    // the layout's folder itself. A video collection (isVideos: info.conf
    // "type = videos") uses collections/_videos/layout the same way, before _menu.
    std::string collectionFolder(const std::string& layoutFolder, const std::string& collection, bool isMenu,
                                 bool isVideos = false);

    // The page a video collection's videos play on ("player" for the page
    // "layout", "player 4x3" for "layout 4x3"): collections/<collection>/layout/
    // then collections/_videos/layout/, each <player page>.xml then player.xml.
    // "" when the layout has none (RetroFE then uses its built-in player page).
    std::string playerFile(const std::string& layoutFolder, const std::string& collection, const std::string& page);

    // The page for a management screen, in the layout's forms folder, for the
    // page RetroFE draws collections with (e.g. "layout", or "layout 4x3"):
    // forms/<screen>/<page>.xml, forms/<page>.xml, forms/<screen>/layout.xml,
    // forms/layout.xml. "" when there's none.
    std::string formFile(const std::string& formsFolder, const std::string& screen, const std::string& page);

    // The on-screen keyboard's page: forms/keyboard/<page>.xml, then forms/keyboard/layout.xml
    std::string keyboardFile(const std::string& formsFolder, const std::string& page);
}

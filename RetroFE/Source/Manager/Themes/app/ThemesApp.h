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

class Configuration;

// The Themes section's parts that only RetroFE builds (they need the
// compiled-in font, SDL or pages)
namespace Themes
{
    // The font compiled into RetroFE (Roboto), which editor-made themes bundle
    std::string bundledFont();

    // retrofe --theme-check <layout> [report file]: loads the theme, checks
    // every page (generated from the project for an editor-made theme, as
    // written for a hand-made one) with LayoutCheck, builds the main page with
    // PageBuilder in a hidden window, and prints what's wrong. Returns the
    // exit status: 0 nothing found, 1 problems found, 2 couldn't check.
    // Call after the configuration is imported.
    int runThemeCheck(Configuration& config, const std::string& layout, const std::string& reportFile);
}

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
#include "ThemesApp.h"
#include "../ThemeProvider.h"
#include "../../../Builtin/Builtin.h"
#include "../../../Database/Configuration.h"

std::string Themes::bundledFont()
{
    return std::string(reinterpret_cast<const char*>(retrofe_builtin_font), retrofe_builtin_font_size);
}

std::unique_ptr<Manager::Provider> Themes::makeProvider(const Manager::Services& services)
{
    Paths paths;
    paths.root = Configuration::absolutePath;
    paths.layouts = Configuration::layoutsPath();
    paths.fontBytes = [] { return bundledFont(); };
    return std::make_unique<ThemeProvider>(services, paths);
}

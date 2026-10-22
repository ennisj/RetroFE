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

#include <cstddef>
#include <string>

// Font compiled into the executable (see CMake/EmbedFile.cmake)
extern const unsigned char retrofe_builtin_font[];
extern const std::size_t retrofe_builtin_font_size;

// Defaults compiled into RetroFE so it can start from a folder that only
// contains the executable. Anything found on disk always takes precedence.
namespace Builtin
{
    // Used in place of settings.conf when it doesn't exist
    extern const char* const settings;

    // Used in place of controls.conf when it doesn't exist
    extern const char* const controls;

    // Layout XML used when the configured layout isn't installed.
    // Returns nullptr for pages the built-in layout doesn't provide.
    const char* layoutXml(const std::string& page);
}

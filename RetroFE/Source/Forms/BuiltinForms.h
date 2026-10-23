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

#include <vector>

// Built-in forms are kept next to the code they belong to. Each Manage
// section has a table in Manager/<Section>/<Section>Forms.cpp; Forms::builtinForm
// looks through all of them (BuiltinForms.cpp).
namespace Forms
{
    struct BuiltinXml
    {
        const char* id;
        const char* xml;
    };
    using BuiltinTable = std::vector<BuiltinXml>;

    BuiltinTable gamesForms();       // Manager/Games/GamesForms.cpp
    BuiltinTable menusForms();       // Manager/Menus/MenusForms.cpp
    BuiltinTable systemsForms();     // Manager/Systems/SystemsForms.cpp
    BuiltinTable videosForms();      // Manager/Videos/VideosForms.cpp
    BuiltinTable emulatorsForms();   // Manager/Emulators/EmulatorsForms.cpp
    BuiltinTable themesForms();      // Manager/Themes/ThemesForms.cpp
}

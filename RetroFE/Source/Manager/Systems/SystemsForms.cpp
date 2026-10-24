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
#include "../../Forms/BuiltinForms.h"
#include "../Games/RetroArchForms.h"
#include "SystemForms.h"

// The Systems screens, opened from Manage: "systems", "system" and the
// "system-..." screens below it, served by SystemProvider (prefix "system").
// Their templates are shared with Videos (SystemForms.cpp). The system's
// RetroArch screens ("system-retroarch"...) are the core-wide ones, served by
// RetroArchProvider (Manager/Games/RetroArchForms.cpp).
Forms::BuiltinTable Forms::systemsForms()
{
    BuiltinTable table = Manager::systemForms(false);
    const BuiltinTable& retroArch = Manager::retroArchForms(true);
    table.insert(table.end(), retroArch.begin(), retroArch.end());
    return table;
}

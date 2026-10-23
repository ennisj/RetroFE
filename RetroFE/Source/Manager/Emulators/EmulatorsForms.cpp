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

// The Emulators screens, opened from Manage. A placeholder for now.
namespace
{

const char* const emulatorsForm = R"(
<form id="emulators" title="Emulators">
  <note value="Coming soon: set up the emulators that run your games and choose which systems use them."/>
</form>
)";

}

Forms::BuiltinTable Forms::emulatorsForms()
{
    return {
        { "emulators", emulatorsForm },
    };
}

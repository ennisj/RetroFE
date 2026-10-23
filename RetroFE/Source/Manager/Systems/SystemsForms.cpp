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

// The Systems screens, opened from Manage. For now: the systems there are
// (collection folders) and adding one (collection.create, see ManagerModel.h).
namespace
{

const char* const systemsForm = R"(
<form id="systems" title="Systems">
  <note value="Coming soon: change a system's games folder, emulator and artwork."/>
  <list label="Systems" bind="info:collections" empty="No systems yet. Add your first one below."/>
  <section label="Add a system">
    <note value="Point RetroFE at a folder of games and the emulator that runs them. It reloads with the new system under Browse."/>
    <text label="Name" bind="draft:name" hint="e.g. Arcade"/>
    <folder label="Games folder" bind="draft:romPath" hint="Not chosen yet" status="info:romCount"/>
    <text label="File extensions" bind="draft:extensions" hint="e.g. zip,7z" default="zip"/>
    <file label="Emulator" bind="draft:executable" hint="Not chosen yet" filter="Programs" pattern="*.exe;*.bat;*.cmd"/>
    <text label="Arguments" bind="draft:arguments" default="&quot;%ITEM_FILEPATH%&quot;" help="%ITEM_FILEPATH% is replaced with the game's file"/>
    <button label="Add system" action="collection.create"/>
  </section>
</form>
)";

}

Forms::BuiltinTable Forms::systemsForms()
{
    return {
        { "systems", systemsForm },
    };
}

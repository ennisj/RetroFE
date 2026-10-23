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

// The Emulators screens, opened from Manage. RetroFE has no emulator entity:
// these are its launchers (a program plus arguments). Served by
// EmulatorProvider (prefix "emulator").
namespace
{

// Every launcher, searchable, then the names asked for but not defined
const char* const emulatorsForm = R"(
<form id="emulators" title="Emulators">
  <note bind="emulator:status"/>
  <text label="Search" bind="emulator:query" live="yes" hint="Launcher, program or system"/>
  <results bind="emulator:search" open="emulator" empty="No launchers match."/>
  <section label="Used but not defined" visibleWhen="emulator:hasUnresolved">
    <note bind="emulator:unresolvedNote"/>
    <results bind="emulator:unresolved" open="emulator-new" param="name"/>
  </section>
  <link label="Add a launcher" open="emulator-new"/>
</form>
)";

// One launcher (param:id, param:scope): facts, who uses it, its file, actions
const char* const emulatorForm = R"(
<form id="emulator" title="Launcher" bind="emulator:title">
  <note bind="emulator:problem"/>
  <value label="Name" bind="emulator:name"/>
  <value label="Defined in" bind="emulator:where"/>
  <value label="Program" bind="emulator:executable"/>
  <value label="Program found" bind="emulator:executableStatus"/>
  <value label="Arguments" bind="emulator:arguments"/>
  <value label="Working folder" bind="emulator:workingFolder"/>
  <value label="Restarts RetroFE" bind="emulator:reboot"/>
  <link label="Used by" open="emulator-users" status="emulator:usersStatus"/>
  <link label="The file" open="emulator-file" status="emulator:fileName"/>
  <section label="Actions">
    <note value="A launcher's program and arguments can't be changed here, because systems and games rely on them. Duplicate it instead: change the copy before you create it, then give systems or games the copy."/>
    <button label="Duplicate and edit" action="emulator.duplicate"/>
    <button label="Delete launcher" action="emulator.delete" confirm="Delete this launcher? Its program stays on the drive."/>
    <note bind="emulator:deleteNote"/>
  </section>
</form>
)";

// The launcher's file, verbatim
const char* const emulatorFileForm = R"(
<form id="emulator-file" title="The file" bind="emulator:title">
  <note bind="emulator:file"/>
  <note value="As it is on disk, comments included. It isn't changed here."/>
  <section label="Contents">
    <note bind="emulator:fileText"/>
  </section>
</form>
)";

// Systems and games using the launcher; each opens its own screen
const char* const emulatorUsersForm = R"(
<form id="emulator-users" title="Used by" bind="emulator:title">
  <note bind="emulator:usersSummary"/>
  <section label="Systems">
    <results bind="emulator:systems" open="system" empty="No system uses this launcher."/>
  </section>
  <section label="Games that pick it">
    <results bind="emulator:games" open="game" empty="No game picks this launcher."/>
  </section>
</form>
)";

// A new launcher: param:name prefills the name, param:from copies a launcher
const char* const emulatorNewForm = R"(
<form id="emulator-new" title="Add a launcher">
  <note bind="emulator:newIntro"/>
  <text label="Name" bind="emulator:newName" hint="e.g. RetroArch SNES"
        help="Systems and games ask for it by this name, spelt exactly."/>
  <file label="Program" bind="emulator:newExecutable" filter="Programs" pattern="*.exe;*.bat;*.cmd" hint="Not chosen yet"/>
  <choice label="Arguments" bind="emulator:newPreset" options="emulator:presets"/>
  <file label="Core" bind="emulator:newCore" filter="RetroArch cores" pattern="*.dll;*.so;*.dylib" hint="Not chosen yet"
        visibleWhen="emulator:needsCore"/>
  <text label="Custom arguments" bind="emulator:newArguments" hint="e.g. &quot;%ITEM_FILEPATH%&quot;" visibleWhen="emulator:isCustom"
        help="%ITEM_FILEPATH% is the game's file, %ITEM_NAME% its name, %ITEM_DIRECTORY% its folder."/>
  <folder label="Working folder" bind="emulator:newWorkingFolder" hint="The program's folder"/>
  <value label="Runs" bind="emulator:newCommand"/>
  <button label="Create launcher" action="emulator.create"/>
</form>
)";

}

Forms::BuiltinTable Forms::emulatorsForms()
{
    return {
        { "emulators", emulatorsForm },
        { "emulator", emulatorForm },
        { "emulator-users", emulatorUsersForm },
        { "emulator-file", emulatorFileForm },
        { "emulator-new", emulatorNewForm },
    };
}

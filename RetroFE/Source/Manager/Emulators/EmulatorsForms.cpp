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

// The Emulators screens, opened from Manage. RetroFE has no emulator entity:
// these are its launchers (a program plus arguments). Served by
// EmulatorProvider (prefix "emulator").
namespace
{

// Add first, as on Systems and Themes; every launcher, searchable; then the
// names asked for but not defined: inline when there are a few, else a link
// with their count to a screen of their own
const char* const emulatorsForm = R"(
<form id="emulators" title="Emulators">
  <note bind="emulator:status"/>
  <link label="Add a launcher" open="emulator-new"/>
  <text label="Search" bind="emulator:query" live="yes" hint="Launcher, program or system"/>
  <results bind="emulator:search" open="emulator" empty="No launchers match."/>
  <link label="Used but not defined" open="emulators-unresolved" status="emulator:unresolvedCount" visibleWhen="emulator:hasManyUnresolved"/>
  <section label="Used but not defined" visibleWhen="emulator:hasFewUnresolved">
    <note bind="emulator:unresolvedNote"/>
    <results bind="emulator:unresolved" open="emulator-new" param="name"/>
  </section>
</form>
)";

// Every name used but not defined; each opens Add a launcher with that name
const char* const emulatorsUnresolvedForm = R"(
<form id="emulators-unresolved" title="Used but not defined">
  <note bind="emulator:status"/>
  <note bind="emulator:unresolvedNote"/>
  <results bind="emulator:unresolved" open="emulator-new" param="name" limit="500" empty="Every launcher asked for is defined."/>
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
  <link label="Edit" open="emulator-edit"/>
  <link label="RetroArch" open="emulator-retroarch" status="retroarch:status" visibleWhen="retroarch:runs"/>
  <link label="The file" open="emulator-file" status="emulator:fileName"/>
  <section label="Actions">
    <button label="Rename" action="emulator.startRename" visibleWhen="emulator:isGlobal"/>
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
  <note bind="emulator:newIntro" panel="yes"/>
  <text label="Name" bind="emulator:newName" hint="e.g. RetroArch SNES"
        help="Systems and games ask for it by this name, spelt exactly."/>
  <file label="Program" bind="emulator:newExecutable" hint="Not chosen yet"/>
  <choice label="Arguments" bind="emulator:newPreset" options="emulator:presets"/>
  <file label="Core" bind="emulator:newCore" filter="RetroArch cores" pattern="*.dll;*.so;*.dylib" hint="Not chosen yet"
        visibleWhen="emulator:needsCore"/>
  <text label="Custom arguments" bind="emulator:newArguments" hint="e.g. &quot;%ITEM_FILEPATH%&quot;" visibleWhen="emulator:isCustom"
        help="%ITEM_FILEPATH% is the game's file, %ITEM_NAME% its name, %ITEM_DIRECTORY% its folder (ending in a separator: in quotes, write &quot;%ITEM_DIRECTORY%.&quot;)."/>
  <folder label="Working folder" bind="emulator:newWorkingFolder" hint="The program's folder"/>
  <toggle label="Restarts RetroFE" bind="emulator:newReboot" help="RetroFE restarts once the program exits, e.g. after one that switches the theme."/>
  <value label="Runs" bind="emulator:newCommand"/>
  <button label="Create launcher" action="emulator.create" primary="yes"/>
</form>
)";

// An existing launcher, changed in place (param:id, param:scope)
const char* const emulatorEditForm = R"(
<form id="emulator-edit" title="Edit launcher" bind="emulator:title">
  <note bind="emulator:problem"/>
  <note bind="emulator:editIntro" panel="yes"/>
  <section label="Launcher" visibleWhen="emulator:editIsReady">
    <file label="Program" bind="emulator:editExecutable" hint="Not chosen yet"/>
    <choice label="Arguments" bind="emulator:editPreset" options="emulator:presets"/>
    <file label="Core" bind="emulator:editCore" filter="RetroArch cores" pattern="*.dll;*.so;*.dylib" hint="Not chosen yet"
          visibleWhen="emulator:editNeedsCore"/>
    <text label="Custom arguments" bind="emulator:editArguments" hint="None" visibleWhen="emulator:editIsCustom"
          help="%ITEM_FILEPATH% is the game's file, %ITEM_NAME% its name, %ITEM_DIRECTORY% its folder (ending in a separator: in quotes, write &quot;%ITEM_DIRECTORY%.&quot;), %ITEM_FILENAME% its file name, %RETROFE_PATH% RetroFE's folder."/>
    <folder label="Working folder" bind="emulator:editWorkingFolder" hint="The program's folder" visibleWhen="emulator:editIsGlobal"/>
    <toggle label="Restarts RetroFE" bind="emulator:editReboot" visibleWhen="emulator:editIsGlobal"
            help="RetroFE restarts once the program exits, e.g. after one that switches the theme."/>
    <value label="Runs" bind="emulator:editCommand"/>
    <value label="Saved in" bind="emulator:editFile"/>
  </section>
  <actions>
    <button label="Save" action="emulator.save" primary="yes"/>
    <button label="Undo changes" action="emulator.resetEdit" visibleWhen="emulator:editIsDirty"/>
  </actions>
  <actions>
    <button label="Put back the original" action="emulator.restore" visibleWhen="emulator:editHasOriginal"
            confirm="Put back the launcher file as it was before Manage changed it?"/>
  </actions>
</form>
)";

// A global launcher's new name, with what changes (param:id)
const char* const emulatorRenameForm = R"(
<form id="emulator-rename" title="Rename launcher" bind="emulator:title">
  <note bind="emulator:problem"/>
  <note bind="emulator:renameIntro" panel="yes"/>
  <text label="New name" bind="emulator:renameTo" hint="Not typed yet"
        help="Systems and games ask for it by this name, spelt exactly."/>
  <note bind="emulator:renameProblem"/>
  <section label="What changes" visibleWhen="emulator:canRename">
    <note bind="emulator:renameChangesText"/>
  </section>
  <button label="Rename" action="emulator.rename"
          confirm="Rename it and change these files? The originals are kept in manage/backups."/>
</form>
)";

}

Forms::BuiltinTable Forms::emulatorsForms()
{
    BuiltinTable table = {
        { "emulators", emulatorsForm },
        { "emulators-unresolved", emulatorsUnresolvedForm },
        { "emulator", emulatorForm },
        { "emulator-users", emulatorUsersForm },
        { "emulator-file", emulatorFileForm },
        { "emulator-new", emulatorNewForm },
        { "emulator-edit", emulatorEditForm },
        { "emulator-rename", emulatorRenameForm },
    };
    // The launcher's core-wide RetroArch screens (Games/RetroArchForms.cpp, RetroArchProvider)
    const BuiltinTable& retroArch = Manager::retroArchLauncherForms();
    table.insert(table.end(), retroArch.begin(), retroArch.end());
    return table;
}

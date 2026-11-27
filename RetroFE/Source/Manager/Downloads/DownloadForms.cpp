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

// The emulator download screens, opened from Add a launcher and Add a
// system. Served by DownloadProvider (prefix "download"). There's no trust
// setting: the confirmation of each Install and Update, which names where
// the download comes from, is the user's say.
namespace
{

// The built-in emulators. Opening it lists them at once and checks their
// releases in the background; Check again asks again.
const char* const downloadEmulatorsForm = R"(
<form id="download-emulators" title="Download an emulator">
  <note bind="download:unsupported"/>
  <note value="Downloads come from each emulator's own GitHub releases, or libretro's server for RetroArch. RetroFE checks each against its published checksum, but not what it contains."
        visibleWhen="download:supported"/>
  <note bind="download:checkStatus" visibleWhen="download:supported"/>
  <list label="Warnings" bind="download:warnings" visibleWhen="download:hasWarnings"/>
  <results bind="download:emulators" open="download-emulator" visibleWhen="download:supported"/>
  <link label="RetroArch cores" open="download-cores" visibleWhen="download:retroArchInstalled"/>
  <actions>
    <button label="Check again" action="download.check" visibleWhen="download:supported"/>
  </actions>
</form>
)";

// One emulator (param:id, e.g. emulator-duckstation)
const char* const downloadEmulatorForm = R"(
<form id="download-emulator" title="Emulator" bind="download:name">
  <note bind="download:unsupported"/>
  <note bind="download:description" visibleWhen="download:supported"/>
  <value label="Installed" bind="download:installed" visibleWhen="download:supported"/>
  <value label="Available" bind="download:available" visibleWhen="download:supported"/>
  <value label="Download size" bind="download:size" visibleWhen="download:supported"/>
  <value label="Comes from" bind="download:source" visibleWhen="download:supported"/>
  <value label="Launcher" bind="download:launcher" visibleWhen="download:supported"/>
  <note bind="download:checksumNote" visibleWhen="download:supported"/>
  <note bind="download:progress"/>
  <note value="Leaving this screen stops the download." visibleWhen="download:installingHere"/>
  <note bind="download:message"/>
  <link label="Cores" open="download-cores" visibleWhen="download:isRetroArchInstalled"/>
  <actions>
    <button label="Install" action="download.install" visibleWhen="download:canInstall"
            confirm="Download {bind:download:name} from {bind:download:source}? {bind:download:checks}"/>
    <button label="Update" action="download.update" visibleWhen="download:canUpdate"
            confirm="Download {bind:download:name} from {bind:download:source}? {bind:download:checks}"/>
    <button label="Remove" action="download.remove" visibleWhen="download:isInstalled"
            confirm="Remove {bind:download:name}? Its settings and saves stay in emulators\{bind:download:folder}."/>
    <button label="Stop" action="download.stop" visibleWhen="download:runningHere"/>
  </actions>
</form>
)";

// RetroArch's cores from libretro's nightly builds, installed ones first
const char* const downloadCoresForm = R"(
<form id="download-cores" title="RetroArch cores">
  <note bind="download:unsupported"/>
  <note value="Install RetroArch first: cores run in it." visibleWhen="download:needsRetroArch"/>
  <results bind="download:retroArchLink" open="download-emulator" visibleWhen="download:needsRetroArch"/>
  <note bind="download:coresStatus" visibleWhen="download:hasRetroArch"/>
  <list label="Warnings" bind="download:coreWarnings" visibleWhen="download:hasCoreWarnings"/>
  <choice label="System" bind="download:coreSystem" options="download:coreSystems" visibleWhen="download:hasRetroArch"/>
  <results bind="download:cores" open="download-core" param="core" limit="1000" visibleWhen="download:coresReady"
           empty="No cores listed."/>
  <actions>
    <button label="Check again" action="download.checkCores" visibleWhen="download:hasRetroArch"/>
  </actions>
</form>
)";

// One core (param:core, e.g. genesis_plus_gx)
const char* const downloadCoreForm = R"(
<form id="download-core" title="Core" bind="download:name">
  <note bind="download:unsupported"/>
  <note bind="download:description" visibleWhen="download:supported"/>
  <value label="System" bind="download:system" visibleWhen="download:supported"/>
  <value label="Installed" bind="download:installed" visibleWhen="download:supported"/>
  <value label="Available" bind="download:available" visibleWhen="download:supported"/>
  <value label="Comes from" bind="download:source" visibleWhen="download:supported"/>
  <value label="Launcher" bind="download:launcher" visibleWhen="download:supported"/>
  <note bind="download:progress"/>
  <note value="Leaving this screen stops the download." visibleWhen="download:installingHere"/>
  <note bind="download:message"/>
  <actions>
    <button label="Install" action="download.install" visibleWhen="download:canInstall"
            confirm="Download {bind:download:name} from {bind:download:source}? {bind:download:checks}"/>
    <button label="Update" action="download.update" visibleWhen="download:canUpdate"
            confirm="Download {bind:download:name} from {bind:download:source}? {bind:download:checks}"/>
    <button label="Remove" action="download.remove" visibleWhen="download:isInstalled"
            confirm="Remove {bind:download:name} and its launcher? RetroArch's settings and saves stay."/>
    <button label="Stop" action="download.stop" visibleWhen="download:runningHere"/>
  </actions>
</form>
)";

}

Forms::BuiltinTable Forms::downloadsForms()
{
    return {
        { "download-emulators", downloadEmulatorsForm },
        { "download-emulator", downloadEmulatorForm },
        { "download-cores", downloadCoresForm },
        { "download-core", downloadCoreForm },
    };
}

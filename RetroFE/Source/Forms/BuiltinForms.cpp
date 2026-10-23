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
#include "Form.h"

// The forms RetroFE ships with. Layouts can replace any of them with
// layouts/<layout>/forms/<id>.xml using the same elements and bindings.
//
// Bindings:
//   settings:<key>   a global setting; edits are kept until settings.save.
//                    default="" is what RetroFE uses when no file sets it.
//   draft:<key>      form input that isn't saved anywhere by itself.
//                    default="" is its initial value.
//   info:<key>       computed, read-only values
//   package:<key>    the package chosen in the Catalog list (see ManagerModel.h)
namespace
{

const char* const settingsForm = R"(
<form id="settings" title="Settings">
  <section label="Display">
    <toggle label="Fullscreen" bind="settings:fullscreen"/>
    <text label="Width" bind="settings:horizontal" hint="pixels, or stretch" enabledWhen="!settings:fullscreen"/>
    <text label="Height" bind="settings:vertical" hint="pixels, or stretch" enabledWhen="!settings:fullscreen"/>
    <toggle label="Hide mouse" bind="settings:hideMouse" default="no"/>
    <note value="Display changes take effect the next time RetroFE starts."/>
  </section>
  <section label="Menus">
    <choice label="Layout" bind="settings:layout" options="layouts"/>
    <toggle label="Start on Home" bind="settings:homeMenu" default="no"/>
    <choice label="Browse opens" bind="settings:firstCollection" options="collections" default="Main"/>
  </section>
  <section label="Video">
    <toggle label="Play videos" bind="settings:videoEnable" default="yes"/>
    <number label="Attract mode after" bind="settings:attractModeTime" min="0" max="3600" default="0" help="Seconds without input. 0 turns attract mode off."/>
  </section>
  <actions>
    <button label="Save" action="settings.save"/>
  </actions>
</form>
)";

const char* const manageForm = R"(
<form id="manage" title="Manage">
  <list label="Collections" bind="info:collections"
        empty="Welcome! Nothing is set up yet. Add your first collection below: point RetroFE at a folder of games and the emulator that runs them, and it will reload with the collection under Browse. Or install one from the Catalog: add a source under Sources."/>
  <section label="Add a collection">
    <text label="Name" bind="draft:name" hint="e.g. Arcade"/>
    <folder label="Games folder" bind="draft:romPath" hint="Not chosen yet" status="info:romCount"/>
    <text label="File extensions" bind="draft:extensions" hint="e.g. zip,7z" default="zip"/>
    <file label="Emulator" bind="draft:executable" hint="Not chosen yet" filter="Programs" pattern="*.exe;*.bat;*.cmd"/>
    <text label="Arguments" bind="draft:arguments" default="&quot;%ITEM_FILEPATH%&quot;" help="%ITEM_FILEPATH% is replaced with the game's file"/>
    <button label="Create collection" action="collection.create"/>
  </section>
  <section label="Catalog">
    <list label="Installed" bind="info:installed" empty="Nothing has been installed from the Catalog yet."/>
    <button label="Sources" action="open.sources"/>
    <note bind="info:removalStatus"/>
  </section>
  <section label="Remove a package" visibleWhen="info:hasInstalled">
    <choice label="Package" bind="draft:removePackage" options="installed"/>
    <note value="Puts back files it replaced, and keeps files you've changed since."/>
    <button label="Remove package" action="package.removeInstalled"/>
  </section>
</form>
)";

// Where the Catalog gets packages from
const char* const sourcesForm = R"(
<form id="sources" title="Catalog sources">
  <note bind="info:catalogStatus"/>
  <list label="Sources" bind="info:sources"
        empty="The Catalog lists packages from sources you add here: an index file on the web or on disk, or a folder of package archives, e.g. on a USB stick or a NAS."/>
  <list label="Problems" bind="info:catalogWarnings"/>
  <section label="Add a source">
    <text label="Name" bind="draft:sourceName" hint="e.g. community"/>
    <choice label="Type" bind="draft:sourceType" options="sourceTypes" default="index"/>
    <text label="Address" bind="draft:sourceUrl" hint="https://..." visibleWhen="!info:sourceIsFolder"/>
    <folder label="Folder" bind="draft:sourcePath" hint="Not chosen yet" visibleWhen="info:sourceIsFolder"/>
    <toggle label="I trust this source" bind="draft:sourceTrusted" default="no"
            help="RetroFE doesn't check what sources offer. Only add sources you trust."/>
    <button label="Add source" action="source.add"/>
  </section>
  <section label="Remove a source" visibleWhen="info:hasSources">
    <choice label="Source" bind="draft:removeSource" options="sources"/>
    <button label="Remove source" action="source.remove"/>
  </section>
  <actions>
    <button label="Check for updates" action="catalog.refresh"/>
  </actions>
</form>
)";

// One package from the Catalog; package: is the package chosen in the Catalog list
const char* const packageForm = R"(
<form id="package" title="Package" bind="package:name">
  <note bind="package:summary"/>
  <note bind="package:description"/>
  <note bind="package:installedStatus"/>
  <note bind="package:requires"/>
  <note bind="package:checksum"/>
  <note bind="package:progress"/>
  <actions>
    <button label="Install" action="package.install" visibleWhen="package:canInstall"/>
    <button label="Update" action="package.install" visibleWhen="package:canUpdate"/>
    <button label="Remove" action="package.remove" visibleWhen="package:canRemove"/>
    <button label="Stop" action="package.cancel" visibleWhen="package:isBusy"/>
  </actions>
</form>
)";

}

namespace Forms
{

const char* builtinForm(const std::string& id)
{
    if (id == "settings") return settingsForm;
    if (id == "manage") return manageForm;
    if (id == "sources") return sourcesForm;
    if (id == "package") return packageForm;
    return nullptr;
}

}

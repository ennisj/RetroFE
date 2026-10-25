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
#include "BuiltinForms.h"

// The forms RetroFE ships with. Layouts can replace any of them with
// layouts/<layout>/forms/<id>.xml using the same elements and bindings.
// The Manage sections (games, menus, systems...) keep theirs in
// Manager/<Section>/<Section>Forms.cpp.
//
// Bindings:
//   settings:<key>   a global setting; edits are kept until settings.save.
//                    default="" is what RetroFE uses when no file sets it.
//   draft:<key>      form input that isn't saved anywhere by itself.
//                    default="" is its initial value.
//   info:<key>       computed, read-only values
//   param:<name>     the screen's parameters, e.g. param:id
//   package:<key>    the package the screen is about, param:id (see ManagerModel.h)
//   <prefix>:<key>   a provider's values, e.g. game:title (see Manager/Provider.h)
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
  <link label="Controls" open="controls"/>
  <actions>
    <button label="Save" action="settings.save"/>
  </actions>
</form>
)";

// The hub: one link per section, then the Catalog
const char* const manageForm = R"(
<form id="manage" title="Manage">
  <note visibleWhen="!info:hasCollections"
        value="Welcome! Nothing is set up yet. Open Systems to add your first one: point RetroFE at a folder of games and the emulator that runs them, and it will reload with it under Browse. Or install one from the Catalog: add a source under Sources."/>
  <link label="Games" open="games"/>
  <link label="Menus" open="menus"/>
  <link label="Systems" open="systems"/>
  <link label="Videos" open="videos"/>
  <link label="Emulators" open="emulators"/>
  <link label="Themes" open="themes"/>
  <section label="Catalog">
    <list label="Installed" bind="info:installed" empty="Nothing has been installed from the Catalog yet."/>
    <link label="Sources" open="sources"/>
    <note bind="info:removalStatus"/>
  </section>
  <section label="Remove a package" visibleWhen="info:hasInstalled">
    <choice label="Package" bind="draft:removePackage" options="installed"/>
    <note value="Puts back files it replaced, and keeps files you've changed since."/>
    <button label="Remove package" action="package.removeInstalled" confirm="Remove this package?"/>
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
  <section label="RetroFE updates" visibleWhen="!info:hasRetroFESource">
    <note bind="info:retrofeSourceNote"/>
    <toggle label="I trust this source" bind="draft:retrofeTrusted" default="no"
            help="RetroFE doesn't check what sources offer. Only add sources you trust."/>
    <button label="Add RetroFE from GitHub" action="source.addRetroFE"/>
  </section>
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
    <button label="Remove source" action="source.remove" confirm="Remove this source?"/>
  </section>
  <actions>
    <button label="Check for updates" action="catalog.refresh"/>
  </actions>
</form>
)";

// One package from the Catalog; package: is the package param:id names
const char* const packageForm = R"(
<form id="package" title="Package" bind="package:name">
  <note bind="package:summary"/>
  <note bind="package:notice"/>
  <note bind="package:description"/>
  <note bind="package:installedStatus"/>
  <note bind="package:requires"/>
  <note bind="package:checksum"/>
  <note bind="package:progress"/>
  <actions>
    <button label="Install" action="package.install" visibleWhen="package:canInstall"/>
    <button label="Update" action="package.install" visibleWhen="package:canUpdate"/>
    <button label="Remove" action="package.remove" visibleWhen="package:canRemove" confirm="Remove this package?"/>
    <button label="Stop" action="package.cancel" visibleWhen="package:isBusy"/>
  </actions>
</form>
)";

// Every built-in form: these, then each Manage section's
Forms::BuiltinTable allForms()
{
    Forms::BuiltinTable forms = {
        { "settings", settingsForm },
        { "manage", manageForm },
        { "sources", sourcesForm },
        { "package", packageForm },
    };
    for (auto section : { Forms::gamesForms, Forms::menusForms, Forms::systemsForms,
                          Forms::videosForms, Forms::emulatorsForms, Forms::themesForms,
                          Forms::controlsForms, Forms::browserForms }) {
        Forms::BuiltinTable more = section();
        forms.insert(forms.end(), more.begin(), more.end());
    }
    return forms;
}

}

namespace Forms
{

const char* builtinForm(const std::string& id)
{
    static const BuiltinTable forms = allForms();
    for (const BuiltinXml& form : forms) {
        if (id == form.id) {
            return form.xml;
        }
    }
    return nullptr;
}

std::vector<std::string> builtinFormIds()
{
    std::vector<std::string> ids;
    for (const BuiltinXml& form : allForms()) {
        ids.push_back(form.id);
    }
    return ids;
}

}

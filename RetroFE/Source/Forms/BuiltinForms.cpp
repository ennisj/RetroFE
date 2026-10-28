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

// Controls first: on a cabinet it's the setting that matters most.
// Screen size sets horizontal and vertical together; Custom shows them.
const char* const settingsForm = R"(
<form id="settings" title="Settings">
  <link label="Controls" open="controls"/>
  <section label="Display">
    <toggle label="Fullscreen" bind="settings:fullscreen"/>
    <choice label="Screen size" bind="settings:screenSize" options="screenSizes" enabledWhen="!settings:fullscreen"/>
    <text label="Width" bind="settings:horizontal" hint="pixels, or stretch" enabledWhen="!settings:fullscreen" visibleWhen="info:customScreenSize"/>
    <text label="Height" bind="settings:vertical" hint="pixels, or stretch" enabledWhen="!settings:fullscreen" visibleWhen="info:customScreenSize"/>
    <toggle label="Hide mouse" bind="settings:hideMouse" default="no"/>
    <note value="Display changes take effect the next time RetroFE starts."/>
  </section>
  <section label="Menus">
    <choice label="Theme" bind="settings:layout" options="layouts"/>
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

// The hub: on a bare install the way to the first system, then applying
// what's waiting, one link per section, and the Catalog. Each package
// installed from the Catalog opens its own screen, which can remove it.
const char* const manageForm = R"(
<form id="manage" title="Manage">
  <note visibleWhen="!info:hasCollections"
        value="Welcome! Nothing is set up yet. Add your first system: point RetroFE at a folder of games and the emulator that runs them. Or install one from the Catalog: add a source under Sources."/>
  <link label="Add your first system" open="system-add" visibleWhen="!info:hasCollections"/>
  <button label="Apply changes now" action="changes.apply" visibleWhen="info:hasPendingChanges"/>
  <link label="Games" open="games"/>
  <link label="Menus" open="menus"/>
  <link label="Systems" open="systems"/>
  <link label="Videos" open="videos"/>
  <link label="Emulators" open="emulators"/>
  <link label="Themes" open="themes"/>
  <section label="Catalog">
    <link label="Sources" open="sources"/>
  </section>
  <results label="Installed from the Catalog" bind="info:installed" open="package"
           empty="Nothing has been installed from the Catalog yet."/>
</form>
)";

// Where the Catalog gets packages from. Each source opens its own screen.
const char* const sourcesForm = R"(
<form id="sources" title="Catalog sources">
  <note bind="info:catalogStatus"/>
  <button label="Open the Catalog" action="catalog.open" visibleWhen="info:canOpenCatalog"/>
  <results label="Sources" bind="info:sources" open="source"
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
  <actions>
    <button label="Check for updates" action="catalog.refresh"/>
  </actions>
</form>
)";

// One source (param:id, its name): what it offers, how its last reading went
const char* const sourceForm = R"(
<form id="source" title="Source" bind="source:name">
  <value label="Type" bind="source:type"/>
  <value label="Address" bind="source:location"/>
  <note bind="source:status"/>
  <list label="Problems" bind="source:problems"/>
  <results label="Packages it offers" bind="source:packages" open="package"
           empty="None: it offers no packages, or it hasn't been read yet."/>
  <actions>
    <button label="Check for updates" action="catalog.refresh"/>
    <button label="Remove this source" action="source.removeThis"
            confirm="Remove this source? Packages installed from it stay installed."/>
  </actions>
</form>
)";

// One package from the Catalog; package: is the package param:id names.
// Back is always there, so an install leaves focus on it rather than on
// Remove, which takes Install's place. param:listed: opened from a list in
// Manage (installed packages, a source's), not from the Catalog page.
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
    <button label="Show it in Browse" action="package.showInBrowse" visibleWhen="package:canShow"/>
    <button label="Back to the Catalog" action="back" visibleWhen="!param:listed"/>
    <button label="Back" action="back" visibleWhen="param:listed"/>
    <button label="Stop" action="package.cancel" visibleWhen="package:isBusy"/>
    <button label="Remove" action="package.remove" visibleWhen="package:canRemove" confirm="Remove this package? It puts back files it replaced, and keeps files you've changed since."/>
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
        { "source", sourceForm },
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

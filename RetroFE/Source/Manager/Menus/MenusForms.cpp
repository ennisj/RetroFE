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

// The Menus screens, opened from Manage: the menus you browse, from the top
// menu (Browse) down, and the merged collections in them. Served by
// MenuProvider (prefix "menu"); see MenuProvider.h for each screen's parameters.
namespace
{

const char* const menusForm = R"form(
<form id="menus" title="Menus">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <note bind="menu:browseNote"/>
  <link label="Change what Browse opens" open="settings" visibleWhen="menu:isReady"/>
  <text label="Search" bind="menu:query" live="yes" hint="Name of a menu, merged collection or system"/>
  <results label="From Browse down" bind="menu:tree" open="menu" empty="No menus yet."/>
  <results label="Not in any menu under Browse" bind="menu:unlisted" open="menu" visibleWhen="menu:hasUnlisted"/>
  <link label="New menu or merged collection" open="menu-new" visibleWhen="menu:isReady"/>
</form>
)form";

const char* const menuForm = R"form(
<form id="menu" title="Menu" bind="menu:title">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <note bind="menu:missing"/>
  <button label="Fix it in the menu that lists it" action="menu.openVia" visibleWhen="menu:hasVia"/>
  <image bind="menu:logo" rows="3"/>
  <note bind="menu:browse"/>
  <value label="Folder" bind="menu:name"/>
  <value label="Kind" bind="menu:kind"/>
  <value label="Listed in" bind="menu:parents"/>
  <value label="Shows games of" bind="menu:sources"/>
  <value label="Games" bind="menu:gameCount"/>
  <value label="Drawn with" bind="menu:page"/>
  <note bind="menu:problems"/>
  <link label="Entries" open="menu-entries" status="menu:entriesStatus" visibleWhen="menu:showEntries"/>
  <link label="Sources" open="menu-sources" status="menu:sourcesStatus" visibleWhen="menu:showSources"/>
  <link label="Info and artwork" open="menu-info" status="menu:infoStatus" visibleWhen="menu:isFound"/>
  <link label="Actions" open="menu-actions" visibleWhen="menu:isFound"/>
  <button label="Open in Systems" action="menu.openSystem" visibleWhen="menu:isSystem"/>
</form>
)form";

const char* const menuEntriesForm = R"form(
<form id="menu-entries" title="Entries" bind="menu:title">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <note bind="menu:entriesNote"/>
  <results label="Entries, in order" bind="menu:entries" open="menu-entry" param="entry" empty="No entries yet. Add one below."/>
  <section label="Add an entry">
    <choice label="Collection" bind="menu:addEntry" options="menu:addable"/>
    <button label="Add to the end" action="menu.addEntry"/>
  </section>
</form>
)form";

const char* const menuEntryForm = R"form(
<form id="menu-entry" title="Entry" bind="menu:entryName">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <note bind="menu:entryProblem"/>
  <value label="Position" bind="menu:entryPosition"/>
  <value label="Opens" bind="menu:entryOpens"/>
  <actions>
    <button label="Move up" action="menu.moveUp"/>
    <button label="Move down" action="menu.moveDown"/>
  </actions>
  <button label="Spell it as the folder is" action="menu.respell" visibleWhen="menu:entryMisspelt"/>
  <button label="Open this collection" action="menu.openEntry" visibleWhen="menu:entryExists"/>
  <button label="Remove from this menu" action="menu.removeEntry" confirm="Remove this entry? The collection itself stays."/>
</form>
)form";

const char* const menuSourcesForm = R"form(
<form id="menu-sources" title="Sources" bind="menu:title">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <note value="A merged collection shows games of other systems, one .sub file per system. An empty .sub shows all of that system's games."/>
  <results label="Sources" bind="menu:sources" open="menu-source" param="source" empty="No sources: it shows no games."/>
  <section label="Add a source">
    <choice label="System" bind="menu:addSource" options="menu:addableSources"/>
    <button label="Add, showing all its games" action="menu.addSource"/>
  </section>
  <note value="To show only some of a system's games, take games out in Games: each game's Playlists and collections screen."/>
</form>
)form";

const char* const menuSourceForm = R"form(
<form id="menu-source" title="Source" bind="menu:sourceName">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <note bind="menu:sourceProblem"/>
  <value label="Shows" bind="menu:sourceShows"/>
  <value label="File" bind="menu:sourceFile"/>
  <button label="Spell it as the system is" action="menu.respellSource" visibleWhen="menu:sourceMisspelt"/>
  <button label="Remove this source" action="menu.removeSource" confirm="Remove this source? Its games stay in their system."/>
</form>
)form";

const char* const menuInfoForm = R"form(
<form id="menu-info" title="Info and artwork" bind="menu:title">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <section label="Info (info.conf)">
    <text label="Title" bind="menu:infoTitle" hint="The folder name"/>
    <text label="Manufacturer" bind="menu:infoManufacturer"/>
    <text label="Year" bind="menu:infoYear"/>
    <text label="Genre" bind="menu:infoGenre"/>
  </section>
  <actions>
    <button label="Save" action="menu.save"/>
    <button label="Undo changes" action="menu.reset" visibleWhen="menu:isDirty"/>
  </actions>
  <link label="Artwork" open="menu-media"/>
</form>
)form";

const char* const menuMediaForm = R"form(
<form id="menu-media" title="Artwork" bind="menu:title">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <note value="The collection's own artwork, in system_artwork. Menus show it for this entry unless they have their own."/>
  <results bind="menu:media" open="menu-medium" param="type" empty="No artwork slots."/>
</form>
)form";

const char* const menuMediumForm = R"form(
<form id="menu-medium" title="Artwork" bind="menu:mediumTitle">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <image bind="menu:mediumImage" rows="5"/>
  <value label="File" bind="menu:mediumFile"/>
  <note bind="menu:mediumText"/>
  <note bind="menu:mediumFallback"/>
  <note bind="menu:mediumWhere"/>
  <value label="New files go to" bind="menu:mediumFolder"/>
  <note bind="menu:mediumFolderNote"/>
  <results label="In the folder" bind="menu:folderFiles" open="menu-medium-file" param="file" empty="No files of this kind." visibleWhen="menu:hasMediaFolder"/>
  <section label="Replace">
    <file label="Picture" bind="menu:newFile" hint="Not chosen yet" filter="Pictures" pattern="*.png;*.jpg;*.jpeg;*.gif;*.webp" visibleWhen="menu:isImageSlot"/>
    <file label="Video" bind="menu:newFile" hint="Not chosen yet" filter="Videos" pattern="*.mp4;*.avi;*.mkv;*.mp3;*.wav;*.flac" visibleWhen="menu:isVideoSlot"/>
    <file label="Text" bind="menu:newFile" hint="Not chosen yet" filter="Text" pattern="*.txt" visibleWhen="menu:isTextSlot"/>
    <button label="Use this file" action="menu.replaceMedium"/>
    <button label="Add it to the folder" action="menu.addToFolder" visibleWhen="menu:hasMediaFolder"/>
  </section>
  <button label="Clear" action="menu.clearMedium" visibleWhen="menu:canClear" confirm="Clear it? The file is kept in manage/backups."/>
  <button label="Clear the folder" action="menu.clearFolder" visibleWhen="menu:hasMediaFolder" confirm="Clear the whole folder? Its files are kept in manage/backups."/>
  <button label="Put back the original" action="menu.restoreMedium" visibleWhen="menu:canRestore"/>
</form>
)form";

const char* const menuMediumFileForm = R"form(
<form id="menu-medium-file" title="Artwork" bind="menu:mediumTitle">
  <note bind="menu:status"/>
  <image bind="menu:folderFileImage" rows="5"/>
  <value label="File" bind="menu:folderFileName"/>
  <value label="In the folder" bind="menu:folderFileFolder"/>
  <note bind="menu:folderFileNote"/>
  <button label="Keep only this file" action="menu.keepFolderFile" confirm="Make this the only file? It's copied beside the folder, which wins; the folder's files are kept in manage/backups."/>
  <button label="Take it out of the folder" action="menu.removeFolderFile" confirm="Take this file out of the folder? It's kept in manage/backups."/>
</form>
)form";

const char* const menuActionsForm = R"form(
<form id="menu-actions" title="Actions" bind="menu:title">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <link label="New menu or merged collection in this one" open="menu-new" visibleWhen="menu:canHaveChildren"/>
  <section label="Delete">
    <note bind="menu:deleteNote"/>
    <button label="Delete" action="menu.delete" confirm="Delete it? Select again to confirm."/>
  </section>
</form>
)form";

const char* const menuNewForm = R"form(
<form id="menu-new" title="New menu or merged collection">
  <note bind="menu:status"/>
  <button label="Read the collections again" action="menu.retry" visibleWhen="menu:isFailed"/>
  <choice label="Kind" bind="menu:newKind" options="menu:newKinds"/>
  <text label="Name" bind="menu:newName" hint="e.g. Consoles" help="The collection folder's name, and its title to start with."/>
  <choice label="In menu" bind="menu:newParent" options="menu:parentMenus"/>
  <note bind="menu:newParentProblem"/>
  <choice label="Shows games of" bind="menu:newSource" options="menu:systems" visibleWhen="menu:isNewMerged" help="All of that system's games; add more sources afterwards."/>
  <button label="Create" action="menu.create"/>
</form>
)form";

}

Forms::BuiltinTable Forms::menusForms()
{
    return {
        { "menus", menusForm },
        { "menu", menuForm },
        { "menu-entries", menuEntriesForm },
        { "menu-entry", menuEntryForm },
        { "menu-sources", menuSourcesForm },
        { "menu-source", menuSourceForm },
        { "menu-info", menuInfoForm },
        { "menu-media", menuMediaForm },
        { "menu-medium", menuMediumForm },
        { "menu-medium-file", menuMediumFileForm },
        { "menu-actions", menuActionsForm },
        { "menu-new", menuNewForm },
    };
}

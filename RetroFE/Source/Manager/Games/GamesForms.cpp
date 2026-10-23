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

// The Games screens, opened from Manage; bound to the game: provider
// (GameProvider.h). Games are found by collection (their own system) + id.
namespace
{

// Every installed game; opened with param collection, it starts filtered to it
const char* const gamesForm = R"(
<form id="games" title="Games">
  <note bind="game:status"/>
  <text label="Search" bind="game:query" live="yes" hint="Name or title"/>
  <choice label="In" bind="game:filter" options="game:filters"
          help="A system, a merged collection, or a menu: a menu covers everything below it."/>
  <toggle label="Only games without metadata" bind="game:onlyNoMetadata" visibleWhen="!game:onlyHidden"/>
  <toggle label="Only hidden games" bind="game:onlyHidden"
          help="Games whose system's exclude list keeps RetroFE from showing them. Open one to show it again."/>
  <results bind="game:search" open="game" limit="100" empty="No games match."/>
</form>
)";

// One game: facts, then a link per thing that can be changed
const char* const gameForm = R"(
<form id="game" title="Game" bind="game:title">
  <note bind="game:notFound"/>
  <note bind="game:hiddenNote"/>
  <image bind="game:image" rows="4"/>
  <value label="System" bind="game:system"/>
  <value label="File" bind="game:file"/>
  <value label="Size" bind="game:size"/>
  <value label="Year" bind="game:year"/>
  <value label="Manufacturer" bind="game:manufacturer"/>
  <value label="Genre" bind="game:genre"/>
  <value label="Players" bind="game:players"/>
  <value label="Played" bind="game:played"/>
  <value label="Time played" bind="game:timePlayed"/>
  <value label="Favourite" bind="game:favourite"/>
  <value label="Also shown in" bind="game:shownIn"/>
  <link label="Metadata" open="game-metadata" status="game:metadataStatus" visibleWhen="game:isFound"/>
  <link label="Launcher" open="game-launcher" status="game:launcherStatus" visibleWhen="game:isFound"/>
  <link label="Media" open="game-media" status="game:mediaStatus" visibleWhen="game:isFound"/>
  <link label="Playlists and collections" open="game-playlists" status="game:playlistsStatus" visibleWhen="game:isFound"/>
  <link label="Description" open="game-description" status="game:descriptionStatus" visibleWhen="game:isFound"/>
  <actions>
    <button label="Start this game" action="game.launch" visibleWhen="game:isFound"/>
    <button label="Hide this game" action="game.hide" visibleWhen="game:isFound"
            confirm="Hide it? Its system's exclude list will name it, so RetroFE stops showing it."/>
    <button label="Show this game again" action="game.unhide" visibleWhen="game:isHidden"/>
  </actions>
  <value label="Folder" bind="game:folder"/>
</form>
)";

const char* const metadataForm = R"(
<form id="game-metadata" title="Metadata" bind="game:screenTitle">
  <note bind="game:notFound"/>
  <section label="Metadata" visibleWhen="game:isFound">
    <text label="Title" bind="game:meta:description" status="game:metaStatus:description"/>
    <text label="Year" bind="game:meta:year" hint="e.g. 1981" status="game:metaStatus:year"/>
    <text label="Manufacturer" bind="game:meta:manufacturer" status="game:metaStatus:manufacturer"/>
    <text label="Developer" bind="game:meta:developer" status="game:metaStatus:developer"/>
    <text label="Genre" bind="game:meta:genre" status="game:metaStatus:genre"/>
    <text label="Players" bind="game:meta:players" hint="e.g. 2" status="game:metaStatus:players"/>
    <text label="Rating" bind="game:meta:rating" status="game:metaStatus:rating"/>
    <text label="Score" bind="game:meta:score" status="game:metaStatus:score"/>
    <text label="Clone of" bind="game:meta:cloneof" hint="the parent game's name" status="game:metaStatus:cloneof"/>
  </section>
  <actions>
    <button label="Save" action="game.saveMetadata"/>
    <button label="Reset" action="game.resetMetadata" visibleWhen="game:isMetadataDirty"/>
  </actions>
  <section label="Changed here" visibleWhen="game:hasOverrides">
    <button label="Put back the shipped title" action="game.unset.description" visibleWhen="game:isChanged:description"/>
    <button label="Put back the shipped year" action="game.unset.year" visibleWhen="game:isChanged:year"/>
    <button label="Put back the shipped manufacturer" action="game.unset.manufacturer" visibleWhen="game:isChanged:manufacturer"/>
    <button label="Put back the shipped developer" action="game.unset.developer" visibleWhen="game:isChanged:developer"/>
    <button label="Put back the shipped genre" action="game.unset.genre" visibleWhen="game:isChanged:genre"/>
    <button label="Put back the shipped players" action="game.unset.players" visibleWhen="game:isChanged:players"/>
    <button label="Put back the shipped rating" action="game.unset.rating" visibleWhen="game:isChanged:rating"/>
    <button label="Put back the shipped score" action="game.unset.score" visibleWhen="game:isChanged:score"/>
    <button label="Put back the shipped clone of" action="game.unset.cloneof" visibleWhen="game:isChanged:cloneof"/>
  </section>
  <note bind="game:metadataNote"/>
</form>
)";

const char* const launcherForm = R"(
<form id="game-launcher" title="Launcher" bind="game:screenTitle">
  <note bind="game:notFound"/>
  <value label="The system uses" bind="game:systemLauncher"/>
  <value label="This game starts with" bind="game:gameLauncher"/>
  <value label="Command" bind="game:launcherCommand"/>
  <note bind="game:launcherProblem"/>
  <note bind="game:unresolvedNote"/>
  <choice label="Launcher" bind="game:launcher" options="game:launchers" visibleWhen="game:isFound"
          help="Another launcher for this game only. It's kept in the system's launchers folder; the system's launcher removes it."/>
  <actions>
    <button label="Save" action="game.saveLauncher"/>
    <button label="Reset" action="game.resetLauncher" visibleWhen="game:isLauncherDirty"/>
    <button label="Use the system's launcher" action="game.useSystemLauncher" visibleWhen="game:hasLauncherOverride"/>
  </actions>
</form>
)";

const char* const mediaForm = R"(
<form id="game-media" title="Media" bind="game:screenTitle">
  <note bind="game:notFound"/>
  <note bind="game:mediaNote"/>
  <results bind="game:media" open="game-media-slot" param="type" empty="No media folders."/>
</form>
)";

// One media slot (param type): what shows, where its own file goes, and changing it
const char* const mediaSlotForm = R"(
<form id="game-media-slot" title="Media" bind="game:screenTitle">
  <image bind="game:slot:image" rows="7"/>
  <value label="Status" bind="game:slot:status"/>
  <value label="Shows" bind="game:slot:file"/>
  <value label="Its own file" bind="game:slot:place"/>
  <note bind="game:slot:text"/>
  <file label="Replace from a file" bind="game:slot:newFile" hint="Not chosen yet" visibleWhen="game:slot:isImage"
        filter="Images" pattern="*.png;*.jpg;*.jpeg;*.gif;*.webp"/>
  <file label="Replace from a file" bind="game:slot:newFile" hint="Not chosen yet" visibleWhen="game:slot:isVideo"
        filter="Videos" pattern="*.mp4;*.avi;*.mkv;*.mp3;*.wav;*.flac"/>
  <file label="Replace from a file" bind="game:slot:newFile" hint="Not chosen yet" visibleWhen="game:slot:isText"
        filter="Text" pattern="*.txt"/>
  <actions>
    <button label="Use this file" action="game.replaceMedia"/>
    <button label="Clear" action="game.clearMedia" confirm="Clear it? The file is kept in manage/backups."/>
    <button label="Put back the original" action="game.restoreMedia" visibleWhen="game:slot:hasOriginal"
            confirm="Put back the file it had before Manage changed it?"/>
  </actions>
</form>
)";

const char* const playlistsForm = R"(
<form id="game-playlists" title="Playlists and collections" bind="game:screenTitle">
  <note bind="game:notFound"/>
  <section label="Favourite" visibleWhen="game:isFound">
    <choice label="On the page of" bind="game:favouritePage" options="game:favouritePages" visibleWhen="game:hasSeveralPages"/>
    <value label="Favourite" bind="game:favouriteText"/>
    <button label="Add to favourites" action="game.addFavourite" visibleWhen="!game:isFavourite" enabledWhen="game:canFavourite"/>
    <button label="Remove from favourites" action="game.removeFavourite" visibleWhen="game:isFavourite" enabledWhen="game:canFavourite"/>
    <note bind="game:favouriteNote"/>
  </section>
  <section label="Playlists" visibleWhen="game:isFound">
    <list label="In" bind="game:playlistsListing" empty="It isn't in any playlist."/>
    <choice label="Playlist" bind="game:playlist" options="game:playlists" visibleWhen="game:hasPlaylists"/>
    <note bind="game:playlistStatus"/>
    <button label="Add to this playlist" action="game.addToPlaylist" visibleWhen="!game:isInPlaylist"/>
    <button label="Remove from this playlist" action="game.removeFromPlaylist" visibleWhen="game:isInPlaylist"
            confirm="Remove it from this playlist?"/>
  </section>
  <section label="Merged collections" visibleWhen="game:isFound">
    <value label="Comes from" bind="game:from"/>
    <list label="Shown in" bind="game:shownInList" empty="No merged collection shows it."/>
    <choice label="Collection" bind="game:merged" options="game:mergedCollections" visibleWhen="game:hasMerged"/>
    <note bind="game:mergedStatus"/>
    <button label="Show it in this collection" action="game.addToMerged" visibleWhen="!game:isInMerged"/>
    <button label="Take it out of this collection" action="game.removeFromMerged" visibleWhen="game:isInMerged"
            confirm="Take it out of this collection? Its file stays where it is."/>
  </section>
</form>
)";

const char* const descriptionForm = R"(
<form id="game-description" title="Description" bind="game:screenTitle">
  <note bind="game:notFound"/>
  <note bind="game:story:status"/>
  <text label="Story" bind="game:story:text" multiline="yes" rows="8" visibleWhen="game:story:canEdit"
        hint="No story text yet"/>
  <actions>
    <button label="Save" action="game.saveStory"/>
    <button label="Reset" action="game.resetStory" visibleWhen="game:isStoryDirty"/>
  </actions>
  <actions>
    <button label="Replace from a file" action="game.replaceStory"/>
    <button label="Clear" action="game.clearStory" visibleWhen="game:story:canClear"
            confirm="Clear the story text? The file is kept in manage/backups."/>
    <button label="Put back the original" action="game.restoreStory" visibleWhen="game:story:hasOriginal"
            confirm="Put back the text it had before Manage changed it?"/>
  </actions>
</form>
)";

}

Forms::BuiltinTable Forms::gamesForms()
{
    return {
        { "games", gamesForm },
        { "game", gameForm },
        { "game-metadata", metadataForm },
        { "game-launcher", launcherForm },
        { "game-media", mediaForm },
        { "game-media-slot", mediaSlotForm },
        { "game-playlists", playlistsForm },
        { "game-description", descriptionForm },
    };
}

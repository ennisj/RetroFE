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
#include "SystemForms.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

// The Systems and Videos screens. Each template is expanded twice: {p} is the
// provider prefix and form id stem ("system" / "video"), and the other
// {words} are the wording for systems or for video collections.
namespace
{

const char* const listForm = R"(
<form id="{p}s" title="{List}">
  <note bind="{p}:status"/>
  <note value="{intro}" panel="yes"/>
  <link label="Add a {noun}" open="{p}-add"/>
  <text label="Search" bind="{p}:query" live="yes" hint="Name or title"/>
  <choice label="In menu" bind="{p}:menu" options="{p}:menus"/>
  <results bind="{p}:search" open="{p}" empty="{empty}"/>
</form>
)";

const char* const detailsForm = R"(
<form id="{p}" title="{Noun}" bind="{p}:title">
  <note bind="{p}:status"/>
  {addGamesLink}<image bind="{p}:image" rows="4"/>
  <value label="Folder" bind="{p}:name"/>
  <value label="In menus" bind="{p}:menus"/>
  <value label="{Items} folder" bind="{p}:gamesFolder"/>
  <value label="Extensions" bind="{p}:extensionsText"/>
  {playsValue}<results bind="{p}:games" open="games" param="collection"/>
  <link label="Settings" open="{p}-settings" status="{p}:settingsStatus"/>
  <link label="{launcherLink}" open="{p}-launcher" status="{p}:launcherStatus"/>
  {raLink}<link label="Info" open="{p}-info" status="{p}:infoStatus"/>
  <link label="Media" open="{p}-media" status="{p}:mediaStatus"/>
  <link label="Playlists" open="{p}-playlists" status="{p}:playlistsStatus"/>
  <link label="Check this {noun}" open="{p}-check" status="{p}:checkStatus"/>
  <section label="Actions" visibleWhen="{p}:canDelete">
    <button label="Delete this {noun}" action="{p}.delete"
            confirm="Delete this {noun}? Its {items} stay on the drive; its settings, art and playlists are kept in manage/backups."/>
  </section>
</form>
)";

const char* const settingsForm = R"(
<form id="{p}-settings" title="Settings" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <value label="{Items} folder" bind="{p}:gamesFolder" help="The folder RetroFE reads."/>
  <text label="Extensions" bind="{p}:extensions" hint="e.g. {exts}" help="Comma separated, without dots. Case matters: RetroFE only lists files whose extension matches exactly."/>
  <toggle label="Sort by title" bind="{p}:menuSort"/>
  <toggle label="Show missing {items}" bind="{p}:includeMissing" help="Lists the {items} named in include.txt even when their file isn't there."/>
  <choice label="Metadata" bind="{p}:metadataType" options="{p}:metadataTypes" help="The meta/hyperlist file titles and years come from."/>
  <value label="Saved in" bind="{p}:settingsFile"/>
  <actions>
    <button label="Save" action="{p}.saveSettings" primary="yes"/>
    <button label="Undo changes" action="{p}.resetSettings" visibleWhen="{p}:settingsDirty"/>
  </actions>
</form>
)";

const char* const launcherForm = R"(
<form id="{p}-launcher" title="{launcherLink}" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  {playChoice}<value label="In effect" bind="{p}:launcherInEffect" visibleWhen="{p}:usesLauncher"/>
  <note bind="{p}:launcherFileNote"/>
  <choice label="{defaultLauncherLabel}" bind="{p}:launcher" options="{p}:launchers" visibleWhen="{p}:usesLauncher"/>
  <value label="Alternatives" bind="{p}:alternatives" visibleWhen="{p}:usesLauncher" help="RetroFE Plus lists these after the default; this RetroFE starts the default and keeps the list as it is."/>
  <note bind="{p}:overridesNote"/>
  <note bind="{p}:launcherProblem"/>
  <value label="Saved in" bind="{p}:launcherFile"/>
  <actions>
    <button label="Save" action="{p}.saveLauncher" primary="yes"/>
    <button label="Undo changes" action="{p}.resetLauncher" visibleWhen="{p}:launcherDirty"/>
  </actions>
</form>
)";

const char* const infoForm = R"(
<form id="{p}-info" title="Info" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <text label="Title" bind="{p}:infoTitle" hint="e.g. {nameHint}"/>
  <text label="Manufacturer" bind="{p}:manufacturer"/>
  <text label="Year" bind="{p}:year"/>
  <text label="Genre" bind="{p}:genre"/>
  <toggle label="This is a video collection" bind="{p}:isVideo" help="Writes type = videos: it's then listed under Videos instead of Systems."/>
  <value label="Saved in" bind="{p}:infoFile" help="Leave a field empty to take it out."/>
  <actions>
    <button label="Save" action="{p}.saveInfo" primary="yes"/>
    <button label="Undo changes" action="{p}.resetInfo" visibleWhen="{p}:infoDirty"/>
  </actions>
</form>
)";

const char* const mediaForm = R"(
<form id="{p}-media" title="Media" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <note value="The {noun}'s own art, from its system_artwork folder. Choose one to replace or clear it." panel="yes"/>
  <results bind="{p}:media" open="{p}-medium" param="slot" empty="Nothing to show yet."/>
</form>
)";

const char* const mediumForm = R"(
<form id="{p}-medium" title="Media" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <image bind="{p}:mediumImage" rows="6"/>
  <value label="Shows" bind="{p}:mediumFile"/>
  <value label="From" bind="{p}:mediumWhere"/>
  <value label="Saved in" bind="{p}:mediumFolder"/>
  <note bind="{p}:mediumFolderNote"/>
  <results label="In the folder" bind="{p}:folderFiles" open="{p}-medium-file" param="file" empty="No files of this kind." visibleWhen="{p}:hasMediaFolder"/>
  <file label="Replace with" bind="{p}:mediaFile" start="{p}:mediumFolder" hint="Not chosen yet" filter="Images" pattern="*.png;*.jpg;*.jpeg;*.gif;*.webp" visibleWhen="{p}:isImageSlot"/>
  <file label="Replace with" bind="{p}:mediaFile" start="{p}:mediumFolder" hint="Not chosen yet" filter="Videos" pattern="*.mp4;*.mkv;*.avi;*.mp3;*.wav;*.flac" visibleWhen="{p}:isVideoSlot"/>
  <file label="Replace with" bind="{p}:mediaFile" start="{p}:mediumFolder" hint="Not chosen yet" filter="Text" pattern="*.txt" visibleWhen="{p}:isTextSlot"/>
  <!-- The buttons stay put (greyed out with a reason), so focus never lands on the file field above by itself -->
  <actions>
    <button label="Use this file" action="{p}.replaceMedia" primary="yes"/>
    <button label="Add it to the folder" action="{p}.addToFolder" visibleWhen="{p}:hasMediaFolder"/>
    <button label="Clear" action="{p}.clearMedia" confirm="Clear this art? The file is kept in manage/backups."/>
    <button label="Clear the folder" action="{p}.clearFolder" visibleWhen="{p}:hasMediaFolder" confirm="Clear the whole folder? Its files are kept in manage/backups."/>
    <button label="Put back the original" action="{p}.restoreMedia"/>
  </actions>
</form>
)";

const char* const mediumFileForm = R"(
<form id="{p}-medium-file" title="Media" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <image bind="{p}:folderFileImage" rows="6"/>
  <value label="File" bind="{p}:folderFileName"/>
  <value label="In the folder" bind="{p}:folderFileFolder"/>
  <note bind="{p}:folderFileNote"/>
  <actions>
    <button label="Keep only this file" action="{p}.keepFolderFile" confirm="Make this the only file? It's copied beside the folder, which wins; the folder's files are kept in manage/backups."/>
    <button label="Take it out of the folder" action="{p}.removeFolderFile" confirm="Take this file out of the folder? It's kept in manage/backups."/>
  </actions>
</form>
)";

const char* const playlistsForm = R"(
<form id="{p}-playlists" title="Playlists" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <results bind="{p}:playlists" open="{p}-playlist" param="playlist" empty="No playlists yet."/>
  <section label="New playlist">
    <text label="Name" bind="{p}:newPlaylist" hint="e.g. Party {items}"/>
    <button label="Create playlist" action="{p}.createPlaylist" primary="yes"/>
  </section>
</form>
)";

const char* const playlistForm = R"(
<form id="{p}-playlist" title="Playlist" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <note bind="{p}:playlistNote"/>
  <button label="Show it in RetroFE" action="{p}.showPlaylist" visibleWhen="{p}:canShowPlaylist"/>
  <results label="In this playlist" bind="{p}:entries" open="{p}-playlist-entry" param="game" empty="It's empty."/>
  <section label="Add {items}" visibleWhen="{p}:canEditPlaylist">
    <text label="Search" bind="{p}:playlistQuery" live="yes" hint="Name or title"/>
    <results bind="{p}:candidates" open="{p}-playlist-entry" param="game" limit="30" empty="No {items} match."/>
  </section>
  <section label="Rename or delete" visibleWhen="{p}:canEditPlaylist">
    <text label="Name" bind="{p}:playlistName"/>
    <button label="Rename" action="{p}.renamePlaylist"/>
    <button label="Delete playlist" action="{p}.deletePlaylist" confirm="Delete this playlist? Its {items} stay; the list is kept in manage/backups."/>
  </section>
</form>
)";

const char* const entryForm = R"(
<form id="{p}-playlist-entry" title="Playlist" bind="{p}:entryTitle">
  <note bind="{p}:status"/>
  <value label="{Item}" bind="{p}:entryName"/>
  <value label="From" bind="{p}:entrySystem"/>
  <value label="Playlist" bind="{p}:entryPlaylist"/>
  <note bind="{p}:entryNote"/>
  <actions>
    <button label="Add to the playlist" action="{p}.addEntry" primary="yes" visibleWhen="!{p}:isInPlaylist"/>
    <button label="Take out of the playlist" action="{p}.removeEntry" visibleWhen="{p}:isInPlaylist" confirm="Take it out of the playlist?"/>
  </actions>
</form>
)";

// Problems (counted, something to fix) apart from notes (how this fork reads a setting)
const char* const checkForm = R"(
<form id="{p}-check" title="Check" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <note bind="{p}:checkSummary"/>
  <section label="Problems" visibleWhen="{p}:hasProblems">
    <note bind="{p}:finding0"/>
    <note bind="{p}:finding1"/>
    <note bind="{p}:finding2"/>
    <note bind="{p}:finding3"/>
    <note bind="{p}:finding4"/>
    <note bind="{p}:finding5"/>
    <note bind="{p}:finding6"/>
    <note bind="{p}:finding7"/>
    <note bind="{p}:finding8"/>
    <note bind="{p}:finding9"/>
    <note bind="{p}:finding10"/>
    <note bind="{p}:finding11"/>
  </section>
  <section label="Notes" visibleWhen="{p}:hasNotes">
    <note bind="{p}:note0"/>
    <note bind="{p}:note1"/>
    <note bind="{p}:note2"/>
    <note bind="{p}:note3"/>
    <note bind="{p}:note4"/>
    <note bind="{p}:note5"/>
  </section>
</form>
)";

const char* const addForm = R"(
<form id="{p}-add" title="Add a {noun}">
  <note bind="{p}:status"/>
  <note value="{addIntro}" panel="yes"/>
  <text label="Name" bind="{p}:addName" hint="e.g. {nameHint}"/>
  <choice label="List it in" bind="{p}:addMenu" options="{p}:addMenus"/>
  <folder label="{Items} folder" bind="{p}:addFolder" matching="{p}:addExtensions" hint="Not chosen yet" status="{p}:addFolderCount"/>
  <text label="Extensions" bind="{p}:addExtensions" default="{exts}" hint="e.g. {exts}"/>
  {addPlayChoice}<choice label="{launcherLabel}" bind="{p}:addLauncher" options="{p}:addLaunchers" visibleWhen="{p}:addUsesLauncher"/>
  <file label="{programLabel}" bind="{p}:addProgram" hint="Not chosen yet" filter="Programs" pattern="*.exe;*.bat;*.cmd" visibleWhen="{p}:addIsNewLauncher" help="{programHelp}"/>
  <text label="Arguments" bind="{p}:addArguments" default="&quot;%ITEM_FILEPATH%&quot;" visibleWhen="{p}:addIsNewLauncher" help="%ITEM_FILEPATH% is replaced with the {item}'s file"/>
  <button label="Add {noun}" action="{p}.create" primary="yes"/>
</form>
)";

const char* const systemAddForm = R"(
<form id="system-add" title="Add a system">
  <note bind="system:status"/>
  <note value="Choose the launcher that runs your games, then name the system. RetroFE shows it after it reloads, when you close Manage." panel="yes"/>
  <choice label="Launcher" bind="system:addLauncher" options="system:addLaunchers" enabledWhen="system:addHasLaunchers"/>
  <link label="Download an emulator" open="emulator-downloads"/>
  <note bind="system:addLauncherNote"/>
  <file label="Program" bind="system:addProgram" hint="Not chosen yet" filter="Programs" pattern="*.exe;*.bat;*.cmd" visibleWhen="system:addIsNewLauncher"/>
  <text label="Arguments" bind="system:addArguments" default="&quot;%ITEM_FILEPATH%&quot;" visibleWhen="system:addIsNewLauncher" help="%ITEM_FILEPATH% is replaced with the game's file"/>
  <text label="Name" bind="system:addName" hint="e.g. Arcade"/>
  <choice label="List it in" bind="system:addMenu" options="system:addMenus"/>
  <folder label="Games folder" bind="system:addFolder" matching="system:addExtensions" hint="Default folder" status="system:addFolderCount"/>
  <text label="Extensions" bind="system:addExtensions" hint="e.g. zip,7z"/>
  <button label="Add system" action="system.create" primary="yes"/>
</form>
)";

const char* const addGamesForm = R"(
<form id="{p}-add-games" title="Add games" bind="{p}:screenTitle">
  <note bind="{p}:status"/>
  <folder label="From" bind="{p}:importFrom" status="{p}:importCount" hint="Choose a folder" enabledWhen="!{p}:importRunning"/>
  <toggle label="Include subfolders" bind="{p}:importSubfolders" enabledWhen="!{p}:importRunning"/>
  <value label="Into" bind="{p}:importInto"/>
  <note bind="{p}:importLayout"/>
  <note bind="{p}:importMessage"/>
  <actions>
    <button label="Copy games" action="{p}.copyGames" primary="yes"/>
    <button label="Use this folder instead" action="{p}.useGamesFolder" confirm="Read games from this folder instead? No files are copied. The previous settings are kept in manage/backups."/>
    <button label="Stop" action="{p}.stopImport" visibleWhen="{p}:importRunning"/>
  </actions>
</form>
)";

const char* const templates[][2] = {
    { "s", listForm },
    { "", detailsForm },
    { "-settings", settingsForm },
    { "-add-games", addGamesForm },
    { "-launcher", launcherForm },
    { "-info", infoForm },
    { "-media", mediaForm },
    { "-medium", mediumForm },
    { "-medium-file", mediumFileForm },
    { "-playlists", playlistsForm },
    { "-playlist", playlistForm },
    { "-playlist-entry", entryForm },
    { "-check", checkForm },
    { "-add", addForm },
};

using Words = std::vector<std::pair<std::string, std::string>>;

Words systemWords()
{
    return {
        { "{p}", "system" }, { "{List}", "Systems" },
        { "{noun}", "system" }, { "{Noun}", "System" },
        { "{item}", "game" }, { "{Item}", "Game" }, { "{items}", "games" }, { "{Items}", "Games" },
        { "{exts}", "zip" }, { "{nameHint}", "Arcade" },
        { "{intro}", "" },
        { "{empty}", "No systems match. Add one with Add a system." },
        { "{addIntro}", "Point RetroFE at a folder of games and the emulator that runs them. It shows the new system after it reloads, when you close Manage." },
        { "{launcherLabel}", "Launcher" }, { "{programLabel}", "Emulator" },
        { "{programHelp}", "The program that runs the games, e.g. an emulator's .exe." },
        { "{launcherLink}", "Launcher" }, { "{defaultLauncherLabel}", "Default launcher" },
        { "{addGamesLink}", "<link label=\"Add games\" open=\"system-add-games\"/>" },
        { "{playsValue}", "" }, { "{playChoice}", "" }, { "{addPlayChoice}", "" },
        // The core-wide RetroArch screens (Manager/Games/RetroArchForms.cpp)
        { "{raLink}", "<link label=\"RetroArch\" open=\"system-retroarch\" status=\"retroarch:status\" visibleWhen=\"retroarch:runs\"/>\n  " },
    };
}

Words videoWords()
{
    return {
        { "{p}", "video" }, { "{List}", "Videos" },
        { "{noun}", "video collection" }, { "{Noun}", "Video collection" },
        { "{item}", "video" }, { "{Item}", "Video" }, { "{items}", "videos" }, { "{Items}", "Videos" },
        { "{exts}", "mp4,mkv,avi" }, { "{nameHint}", "Music videos" },
        { "{intro}", "A video collection is a folder of videos, marked with type = videos in its info.conf. RetroFE plays them itself, or hands them to a video player such as VLC." },
        { "{empty}", "No video collections yet. Add one with Add a video collection." },
        { "{addIntro}", "Point RetroFE at a folder of videos. RetroFE plays them itself, or a video player you choose does. It shows the new collection after it reloads, when you close Manage." },
        { "{launcherLabel}", "Video player" }, { "{programLabel}", "Player program" },
        { "{programHelp}", "A video player such as VLC (vlc.exe) or mpv (mpv.exe). It becomes the launcher VideoPlayer." },
        { "{launcherLink}", "Playback" }, { "{defaultLauncherLabel}", "Video player" },
        { "{addGamesLink}", "" },
        { "{playsValue}", "<value label=\"Plays\" bind=\"video:playsText\"/>\n  " },
        { "{playChoice}", "<choice label=\"Plays\" bind=\"video:play\" options=\"video:plays\" help=\"In RetroFE: full screen on its player page. With an external player: its launcher starts, e.g. VLC. Kept in its info.conf as play = here or play = launcher.\"/>\n  " },
        { "{addPlayChoice}", "<choice label=\"Plays\" bind=\"video:addPlay\" options=\"video:plays\" help=\"In RetroFE needs no player program (one can be added later in Playback). With an external player, choose it below.\"/>\n  " },
        { "{raLink}", "" },
    };
}

std::string expand(std::string text, const Words& words)
{
    for (const auto& [from, to] : words) {
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
            text.replace(at, from.size(), to);
        }
    }
    return text;
}

struct Table
{
    std::vector<std::string> ids;
    std::vector<std::string> xml;
    Forms::BuiltinTable table;

    explicit Table(const Words& words)
    {
        std::string base = words.front().second;
        for (const auto& entry : templates) {
            if (base == "video" && std::string(entry[0]) == "-add-games") continue;
            ids.push_back(base + entry[0]);
            const char* source = base == "system" && std::string(entry[0]) == "-add" ? systemAddForm : entry[1];
            xml.push_back(expand(source, words));
        }
        for (size_t i = 0; i < ids.size(); i++) {
            table.push_back({ ids[i].c_str(), xml[i].c_str() });
        }
    }
};

}

const Forms::BuiltinTable& Manager::systemForms(bool videos)
{
    static const Table systems(systemWords());
    static const Table videoCollections(videoWords());
    return videos ? videoCollections.table : systems.table;
}

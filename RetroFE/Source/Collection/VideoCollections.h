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
#pragma once

#include <string>
#include <vector>

class Configuration;

// Video collections (docs/DECISIONS.md M7): a collection whose info.conf says
// "type = videos". Its info.conf "play" says what selecting a video does:
//   play = here      RetroFE plays it full screen on its own player page
//   play = launcher  (or no play line) its launcher starts, e.g. VLC or mpv,
//                    as for any other collection
// No SDL here, so retrofe_tests checks it and Manage shares it.
namespace VideoCollections
{
    extern const char* const typeKey;        // "type"
    extern const char* const typeVideos;     // "videos"
    extern const char* const playKey;        // "play"
    extern const char* const playHere;       // "here"
    extern const char* const playLauncher;   // "launcher"

    // "videos", in any case and with spaces around it
    bool isVideoType(const std::string& type);

    // Whether a collection with this info.conf type and play plays in RetroFE.
    // Only "here" (any case) does: collections from before the setting keep
    // their launcher.
    bool playsHere(const std::string& type, const std::string& play);

    // A play value Manage may write: "here" or "launcher"
    bool isPlayValue(const std::string& play);

    // The file types RetroFE's video component opens (VideoBuilder), lower case
    const std::vector<std::string>& playableExtensions();

    // Whether RetroFE can play files with this extension ("MP4" too: Windows
    // ignores the case; elsewhere VideoBuilder knows the lower and upper case)
    bool isPlayableExtension(const std::string& extension);

    // The same, from RetroFE's configuration: collections.<name>.type and .play
    bool isVideoCollection(Configuration& config, const std::string& collection);
    bool playsHere(Configuration& config, const std::string& collection);

    // A video collection's own files are its "video" media, so a page's
    // reloadableVideo shows the selected video itself: sets
    // collections.<name>.media.video to its (first) list.path, unless
    // media.video is set or its usual video folder exists. Returns whether it
    // set it.
    bool useItemsAsVideoMedia(Configuration& config, const std::string& collection);
}

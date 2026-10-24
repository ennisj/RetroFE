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

#include <cstddef>
#include <functional>
#include <string>

class CollectionInfo;
class Configuration;
class FontCache;
class Item;
class Page;

// Plays a video collection's videos in RetroFE, for collections whose
// info.conf says "play = here" (Collection/VideoCollections.h), in place of
// starting their launcher. It's a page of its own, drawn instead of the
// collection's page: the layout's player page (LayoutPages::playerFile) or
// the built-in one, given the list the collection's page shows (its current
// playlist), so the page's reloadableVideo, title and time texts follow the
// video playing. The collection's page is freed while it plays and selects
// the last video played when it closes.
class VideoPlayback
{
public:
    VideoPlayback(Configuration& config, FontCache* fonts);
    ~VideoPlayback();
    VideoPlayback(const VideoPlayback&) = delete;
    VideoPlayback& operator=(const VideoPlayback&) = delete;

    // Starts playing the list page's selected video. False (and nothing
    // changed) when the page has no video selected or no player page builds.
    bool open(Page& listPage, const std::string& layoutName, const std::string& layoutPage);
    bool isOpen() const { return page_ != nullptr; }

    // Back to the list page, on the video played last; backToList = false
    // only frees the player page (RetroFE is closing)
    void close(bool backToList = true);

    // Called once for each video that starts playing (play count, last played)
    void setOnPlay(std::function<void(Item*)> onPlay) { onPlay_ = std::move(onPlay); }

    // How long a video may take to start before it's skipped
    static constexpr float startTimeout = 10.0f;

    enum class Command { TogglePause, Next, Previous, SeekForward, SeekBack, JumpForward, JumpBack };
    void command(Command command);

    // Per frame. When a video ends the next one starts; after the last one it closes.
    void update(float dt);
    void draw();

private:
    void select(size_t index);

    Configuration& config_;
    FontCache* fonts_;
    Page* page_ = nullptr;
    Page* listPage_ = nullptr;
    CollectionInfo* list_ = nullptr;   // the videos, borrowed from the list page's collection
    size_t index_ = 0;
    bool seenPlaying_ = false;
    int updatesSinceSelect_ = 0;
    float waited_ = 0;   // seconds since it was selected, until it plays
    std::function<void(Item*)> onPlay_;
};

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
#include "VideoPlayback.h"
#include "LayoutPages.h"
#include "Page.h"
#include "PageBuilder.h"
#include "../Builtin/Builtin.h"
#include "../Collection/CollectionInfo.h"
#include "../Collection/Item.h"
#include "../Database/Configuration.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <vector>

namespace
{
    // The items the page lists now: its playlist, else the collection's items
    const std::vector<Item*>& shownItems(Page& page)
    {
        CollectionInfo* collection = page.getCollection();
        auto playlist = collection->playlists.find(page.getPlaylistName());
        if (playlist != collection->playlists.end() && playlist->second) {
            return *playlist->second;
        }
        return collection->items;
    }

    // Takes back the borrowed items before the page deletes the collection
    void freePage(Page* page, CollectionInfo* list)
    {
        if (list) {
            list->playlists.clear();
            list->items.clear();
        }
        if (page) {
            page->deInitialize();
            delete page;
        }
    }
}

VideoPlayback::VideoPlayback(Configuration& config, FontCache* fonts)
    : config_(config)
    , fonts_(fonts)
{
}

VideoPlayback::~VideoPlayback()
{
    if (page_) {
        freePage(page_, list_);
    }
}

bool VideoPlayback::open(Page& listPage, const std::string& layoutName, const std::string& layoutPage)
{
    close();
    CollectionInfo* source = listPage.getCollection();
    Item* selected = listPage.getSelectedItem();
    if (!source || !selected || !selected->leaf) {
        return false;
    }

    // The videos as the page lists them; entries that open other collections are left out
    std::vector<Item*> videos;
    size_t index = 0;
    for (Item* item : shownItems(listPage)) {
        if (item && item->leaf) {
            if (item == selected) {
                index = videos.size();
            }
            videos.push_back(item);
        }
    }
    if (videos.empty()) {
        return false;
    }

    // The layout's player page, else the built-in one (also when the layout's has no <menu>)
    std::string folder = Utils::combinePath(Configuration::layoutsPath(), layoutName);
    std::string file = layoutName.empty() ? "" : LayoutPages::playerFile(folder, source->name, layoutPage);
    Page* page = nullptr;
    CollectionInfo* list = nullptr;
    std::string used;   // the layout file the page came from, "" for the built-in one
    for (int attempt = file.empty() ? 1 : 0; attempt < 2 && !page; attempt++) {
        PageBuilder builder(layoutName, "layout", config_, fonts_);
        if (attempt == 0) {
            builder.setLayoutFile(file);
        }
        else {
            builder.setLayoutXml(Builtin::layoutXml("player"), folder);
        }
        page = builder.buildPage();
        if (!page) {
            continue;
        }
        used = attempt == 0 ? file : "";
        // The page owns this collection (and deletes it); the items stay the list page's
        list = new CollectionInfo(config_, source->name, source->listpath, "", source->metadataType, "");
        list->items = videos;
        list->playlists["all"] = &list->items;
        page->pushCollection(list);
        // A layout's page needs a <menu> to follow the videos and a jukebox
        // reloadableVideo to pause them and say when one ends
        std::string missing = page->getCollectionSize() == 0 ? "no <menu>, so it can't follow the videos"
                            : !page->isJukebox() ? "no reloadableVideo with jukebox=\"true\", so it can't pause or play the next video"
                            : "";
        if (!missing.empty()) {
            LOG_ERROR("VideoPlayback", "The player page " + (attempt == 0 ? file : std::string("(built-in)")) + " has "
                + missing + (attempt == 0 ? "; using the built-in one" : ""));
            freePage(page, list);
            page = nullptr;
            list = nullptr;
        }
    }
    if (!page) {
        LOG_ERROR("VideoPlayback", "Couldn't build a page to play " + source->name + "'s videos");
        return false;
    }

    page_ = page;
    list_ = list;
    listPage_ = &listPage;
    LOG_INFO("VideoPlayback", "Playing " + source->name + "/" + list->items[index]->name + " in RetroFE"
        + (used.empty() ? " (built-in player page)" : " (" + used + ")"));

    // The list page stops its own videos while this one plays
    listPage.freeGraphicsMemory();
    page_->allocateGraphicsMemory();
    select(index);
    page_->start();
    return true;
}

void VideoPlayback::close(bool backToList)
{
    if (!page_) {
        return;
    }
    Item* last = list_->items[index_];
    page_->stop();
    freePage(page_, list_);
    page_ = nullptr;
    list_ = nullptr;

    // Back on the list, on the video played last
    Page* listPage = listPage_;
    listPage_ = nullptr;
    if (!backToList || !listPage) {
        return;
    }
    listPage->allocateGraphicsMemory();
    const std::vector<Item*>& shown = shownItems(*listPage);
    for (size_t i = 0; i < shown.size(); i++) {
        if (shown[i] == last) {
            listPage->setScrollOffsetIndex(i);
            break;
        }
    }
    listPage->setSelectedItem();
    listPage->onNewItemSelected();
    listPage->reallocateMenuSpritePoints(false);
}

void VideoPlayback::select(size_t index)
{
    index_ = index;
    seenPlaying_ = false;
    updatesSinceSelect_ = 0;
    waited_ = 0;
    LOG_INFO("VideoPlayback", "Now playing " + list_->items[index]->name);
    page_->setScrollOffsetIndex(index);
    page_->highlightLoadArt();   // the page's reloadables load the new video
    page_->reallocateMenuSpritePoints();
    page_->highlightEnter();
}

void VideoPlayback::command(Command command)
{
    if (!page_) {
        return;
    }
    size_t count = list_->items.size();
    LOG_DEBUG("VideoPlayback", "Command " + std::to_string(static_cast<int>(command)) + " on " + list_->items[index_]->name);
    switch (command) {
    case Command::TogglePause:
        page_->pause();
        break;
    case Command::Next:
        select((index_ + 1) % count);
        break;
    case Command::Previous:
        select((index_ + count - 1) % count);
        break;
    case Command::SeekForward:
        page_->skipForwardp();
        break;
    case Command::SeekBack:
        page_->skipBackwardp();
        break;
    case Command::JumpForward:
        page_->skipForward();
        break;
    case Command::JumpBack:
        page_->skipBackward();
        break;
    }
}

void VideoPlayback::update(float dt)
{
    if (!page_) {
        return;
    }
    page_->update(dt);

    // A video that has played to its end: the next one, or back to the list
    // after the last. The video selected last only counts once the page has
    // swapped it in (the one before may still report itself playing).
    if (updatesSinceSelect_ < 3) {
        updatesSinceSelect_++;
        return;
    }
    // One that never starts (a file the player can't open) is skipped after a while
    bool stalled = false;
    if (page_->isJukeboxPlaying()) {
        if (!seenPlaying_ && onPlay_) {
            onPlay_(list_->items[index_]);
        }
        seenPlaying_ = true;
    }
    else if (!seenPlaying_) {
        waited_ += dt;
        stalled = waited_ > startTimeout;
        if (stalled) {
            LOG_ERROR("VideoPlayback", list_->items[index_]->name + " didn't start playing within "
                + std::to_string(static_cast<int>(startTimeout)) + " seconds; skipping it");
        }
    }
    if ((!page_->isJukeboxPlaying() && seenPlaying_) || stalled) {
        if (!stalled) {
            LOG_INFO("VideoPlayback", list_->items[index_]->name + " has ended");
        }
        if (index_ + 1 < list_->items.size()) {
            select(index_ + 1);
        }
        else {
            close();
        }
    }
}

void VideoPlayback::draw()
{
    if (page_) {
        page_->draw();
    }
}

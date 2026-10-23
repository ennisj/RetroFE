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
    std::string folder = Utils::combinePath(Configuration::absolutePath, "layouts", layoutName);
    std::string file = layoutName.empty() ? "" : LayoutPages::playerFile(folder, source->name, layoutPage);
    Page* page = nullptr;
    CollectionInfo* list = nullptr;
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
        // The page owns this collection (and deletes it); the items stay the list page's
        list = new CollectionInfo(config_, source->name, source->listpath, "", source->metadataType, "");
        list->items = videos;
        list->playlists["all"] = &list->items;
        page->pushCollection(list);
        if (page->getCollectionSize() == 0) {
            LOG_ERROR("VideoPlayback", "The player page " + file + " has no <menu>, so it can't follow the videos; using the built-in one");
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
        + (file.empty() ? " (built-in player page)" : " (" + file + ")"));

    // The list page stops its own videos while this one plays
    listPage.freeGraphicsMemory();
    page_->allocateGraphicsMemory();
    select(index);
    page_->start();
    return true;
}

void VideoPlayback::close()
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
    if (page_->isJukeboxPlaying()) {
        seenPlaying_ = true;
    }
    else if (seenPlaying_) {
        LOG_INFO("VideoPlayback", list_->items[index_]->name + " has ended");
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

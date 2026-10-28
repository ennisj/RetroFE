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
#include "GameProvider.h"
#include "../Emulators/Launchers.h"
#include "../Systems/SystemProvider.h"
#include "../../Library/Edits.h"
#include "../../Library/Files.h"
#include "../../Library/Indexer.h"
#include "../../Library/LineList.h"
#include "../../Library/MetadataOverrides.h"
#include "../../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using Library::GameRecord;
using Library::Index;
using Library::MediaKind;
using Library::MediaSlot;

// Bindings (game:<key>):
//   status                    while the library is being read: how far it got
//   query, filter (options filters), onlyNoMetadata, onlyHidden    the search; rows search
//   filterSummary, filterCount    the Filters link's status ("All games"), and how many match
//   title, screenTitle        the game's title; "<title> - Metadata" etc. for sub-screens
//   isFound, notFound         whether param collection + id is a game RetroFE lists
//   isHidden, hiddenNote      a game its system's exclude list hides (not isFound)
//   isKnown                   isFound or isHidden: metadata, media and story work for both
//   system, file, folder, size, year, manufacturer, developer, genre, players,
//   favourite, from, shownIn, image, played, timePlayed       details
//   metadataStatus, launcherStatus, mediaStatus, playlistsStatus, descriptionStatus   link statuses
//   meta:<element>, metaStatus:<element>, isChanged:<element>, isMetadataDirty, hasOverrides,
//   metadataFile, metadataNote; metaPick:<element> (options metaChoices:<element>) and
//   metaTyping:<element> for genre, players and rating (a choice, or "Other..." to type)
//   launcher (options launchers), systemLauncher, gameLauncher, launcherCommand,
//   launcherProblem, rows addLaunchers, launcherFile, isLauncherDirty, hasLauncherOverride
//   rows media, mediaShown, mediaMissing; mediaNote; slot:title, slot:image, slot:file, slot:status, slot:place,
//   slot:text, slot:isImage, slot:isVideo, slot:isText, slot:canClear, slot:hasOriginal, slot:newFile
//   favouritePage (options favouritePages), hasSeveralPages, favouriteText, isFavourite,
//   favouriteToggle (setting it adds or removes the favourite), favouriteStatus,
//   canFavourite, favouriteNote, list playlistsListing, playlist (options playlists),
//   playlistStatus, isInPlaylist, canAddToPlaylist, hasPlaylists, rows newPlaylist,
//   list shownInList, merged (options mergedCollections), mergedStatus, isInMerged, hasMerged
//   story:text (multi-line), story:status, story:file, story:canEdit, isStoryDirty, story:canClear, story:hasOriginal
// Actions (game.<name>):
//   launch                    closes Manage and starts the game (Services::requestLaunch)
//   hide, unhide              the system's exclude list
//   saveMetadata, resetMetadata, unset.<element>
//   saveLauncher, resetLauncher, useSystemLauncher
//   replaceMedia, clearMedia, restoreMedia
//   addFavourite, removeFavourite, addToPlaylist, removeFromPlaylist, addToMerged, removeFromMerged
//   saveStory, resetStory, clearStory, restoreStory, replaceStory
namespace
{
    const char* const unsavedWarning = "You have unsaved changes. Save them, or leave again to discard them.";
    const char* const goneMessage = "This game isn't in the library any more.";
    const char* const lookingMessage = "Still looking for its files...";

    // The metadata fields the Metadata screen edits (hyperlist elements)
    const std::vector<std::string>& editedFields()
    {
        static const std::vector<std::string> fields = {
            "description", "year", "manufacturer", "developer", "genre", "players", "rating", "score", "cloneof"
        };
        return fields;
    }

    std::string currentValue(const GameRecord& game, const std::string& element)
    {
        const Library::Metadata& m = game.meta;
        if (element == "description") return game.title;
        if (element == "year") return m.year;
        if (element == "manufacturer") return m.manufacturer;
        if (element == "developer") return m.developer;
        if (element == "genre") return m.genre;
        if (element == "players") return m.players;
        if (element == "rating") return m.rating;
        if (element == "score") return m.score;
        if (element == "cloneof") return m.cloneOf;
        return "";
    }

    std::string describeSize(uintmax_t bytes)
    {
        const char* units[] = { "bytes", "KB", "MB", "GB", "TB" };
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024 && unit < 4) {
            value /= 1024;
            unit++;
        }
        char text[64];
        if (unit == 0) {
            std::snprintf(text, sizeof(text), "%llu bytes", static_cast<unsigned long long>(bytes));
        }
        else {
            std::snprintf(text, sizeof(text), value < 10 ? "%.1f %s" : "%.0f %s", value, units[unit]);
        }
        return text;
    }

    std::string join(const std::vector<std::string>& items, const std::string& separator = ", ")
    {
        std::string text;
        for (const std::string& item : items) {
            text += (text.empty() ? "" : separator) + item;
        }
        return text;
    }

    bool startsWith(const std::string& text, const std::string& prefix)
    {
        return text.compare(0, prefix.size(), prefix) == 0;
    }

    std::string yesNo(bool value)
    {
        return value ? "yes" : "no";
    }

    bool isGameForm(const std::string& form)
    {
        return form == "game" || startsWith(form, "game-");
    }

    std::string fileName(const std::string& path)
    {
        return Library::narrow(fs::path(path).filename());
    }

    std::string safeBasename(std::string name)
    {
        Utils::replaceSlashesWithUnderscores(name);
        return name;
    }

    // Text for a note: no carriage returns, and not too long to read on a screen
    std::string preview(std::string text)
    {
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        const size_t most = 600;
        return text.size() > most ? text.substr(0, most) + "..." : text;
    }

    // The "Other..." option of a metadata choice: the field is typed instead
    const char* const otherChoice = "\x1fother";

    // The metadata fields offered as choices of the values the system has
    bool isChoiceField(const std::string& field)
    {
        return field == "genre" || field == "players" || field == "rating";
    }

    // Numbers by value ("2" before "10"), anything else alphabetically, ignoring case
    bool naturalLess(const std::string& a, const std::string& b)
    {
        char* endA = nullptr;
        char* endB = nullptr;
        long numberA = std::strtol(a.c_str(), &endA, 10);
        long numberB = std::strtol(b.c_str(), &endB, 10);
        bool isNumberA = endA != a.c_str() && *endA == '\0';
        bool isNumberB = endB != b.c_str() && *endB == '\0';
        if (isNumberA && isNumberB && numberA != numberB) return numberA < numberB;
        if (isNumberA != isNumberB) return isNumberA;
        std::string lowerA = Library::toLower(a);
        std::string lowerB = Library::toLower(b);
        return lowerA != lowerB ? lowerA < lowerB : a < b;
    }

    // A path as Saved in shows it: from RetroFE's folder when it's inside it
    std::string shownPath(const std::string& root, const std::string& path)
    {
        if (root.empty() || path.empty()) return path;
        fs::path relative = fs::path(path).lexically_normal().lexically_relative(fs::path(root).lexically_normal());
        if (relative.empty() || *relative.begin() == "..") return path;
        std::string text = Library::narrow(relative);
        std::replace(text.begin(), text.end(), '\\', '/');
        return text;
    }

    template <typename T>
    bool isReady(const std::future<T>& future)
    {
        return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    // Holds one index snapshot for the length of a call (see GameProvider::pinned_)
    class Pin
    {
    public:
        Pin(Manager::GameProvider::IndexPtr& slot, Manager::GameProvider::IndexPtr value)
            : slot_(slot), owner_(!slot)
        {
            if (owner_) slot_ = std::move(value);
        }
        ~Pin()
        {
            if (owner_) slot_.reset();
        }
    private:
        Manager::GameProvider::IndexPtr& slot_;
        bool owner_;
    };
}

namespace Manager
{

GameProvider::GameProvider(const Services& services)
    : services_(services)
{
}

GameProvider::~GameProvider()
{
    // A running lookup finishes first (std::async's future waits); nothing else to do
}

// --- Plumbing ---------------------------------------------------------------------------

GameProvider::IndexPtr GameProvider::index() const
{
    if (pinned_) return pinned_;
    if (indexSource_) return indexSource_();
    return services_.library ? services_.library->index() : nullptr;
}

std::string GameProvider::gameKey(const Screen& screen)
{
    return screen.param("collection") + "/" + screen.param("id");
}

const GameRecord* GameProvider::findGame(const Index& index, const Screen& screen, int* position) const
{
    std::string collection = screen.param("collection");
    std::string id = screen.param("id");
    if (collection.empty() || id.empty()) {
        return nullptr;
    }
    int found = index.findGame(collection, id);
    if (found < 0) {
        return nullptr;
    }
    if (position) {
        *position = found;
    }
    return &index.games[static_cast<size_t>(found)];
}

const GameRecord* GameProvider::findRecord(const Index& index, const Screen& screen) const
{
    if (const GameRecord* game = findGame(index, screen)) {
        return game;
    }
    int hidden = index.findHiddenGame(screen.param("collection"), screen.param("id"));
    return hidden < 0 ? nullptr : &index.hiddenGames[static_cast<size_t>(hidden)];
}

Forms::Params GameProvider::saveView() const
{
    return {
        { "query", query_ },
        { "filter", filter_ },
        { "filterParam", filterParam_ },
        { "onlyNoMetadata", (onlyNoMetadata_ ? "yes" : "no") },
        { "onlyHidden", (onlyHidden_ ? "yes" : "no") },
        { "playlist", playlist_ },
        { "merged", merged_ },
        { "favouritePage", favouritePage_ }
    };
}

void GameProvider::restoreView(const Forms::Params& state)
{
    if (auto found = state.find("query"); found != state.end()) {
        query_ = found->second;
    }
    if (auto found = state.find("filter"); found != state.end()) {
        filter_ = found->second;
    }
    if (auto found = state.find("filterParam"); found != state.end()) {
        filterParam_ = found->second;
    }
    if (auto found = state.find("onlyNoMetadata"); found != state.end()) {
        onlyNoMetadata_ = Forms::isTruthy(found->second);
    }
    if (auto found = state.find("onlyHidden"); found != state.end()) {
        onlyHidden_ = Forms::isTruthy(found->second);
    }
    if (auto found = state.find("playlist"); found != state.end()) {
        playlist_ = found->second;
    }
    if (auto found = state.find("merged"); found != state.end()) {
        merged_ = found->second;
    }
    if (auto found = state.find("favouritePage"); found != state.end()) {
        favouritePage_ = found->second;
    }
}

void GameProvider::refresh()
{
    // The manager is opening: files may have changed while it was closed, and
    // the search starts afresh
    query_.clear();
    onlyNoMetadata_ = false;
    onlyHidden_ = false;
    hiddenIndex_.reset();
    hiddenSearchKey_ = "\x01";
    hiddenMatches_.clear();
    launchCheckKey_.clear();   // programs may have been installed meanwhile
    filterParam_ = "\x01";
    filtersIndex_.reset();
    thumbnails_.clear();
    wantedThumbnails_.clear();
    detailsKey_.clear();
    globalFavouritesKey_.clear();
    media_ = MediaCache();
    mediaEpoch_++;
    newFile_.clear();
    metadata_.load("", {});
    pendingMetadata_.clear();
    shippedCache_.clear();
    choicesIndex_.reset();
    choices_.clear();
    typing_.clear();
    favouriteStatus_.clear();
    launcher_.load("", {});
    story_.load("", {});
    generation_++;
}

void GameProvider::enter(const Screen& screen)
{
    current_ = screen;
    if (screen.form == "games") {
        // Opened for one system or menu (e.g. from Systems): start filtered to it.
        // Back to the same screen keeps whatever filter the user chose since.
        std::string collection = screen.param("collection");
        if (collection != filterParam_) {
            filterParam_ = collection;
            filter_ = collection;
        }
    }
    priorityType_ = screen.form == "game-media-slot" || screen.form == "game-media-folder-file" ? screen.param("type")
                  : screen.form == "game-description" ? "story" : "";
    if (screen.form == "game-media-slot") {
        newFile_.clear();
    }
    if (screen.form == "game-metadata" && !metadata_.isDirty()) {
        typing_.clear();   // the choices show the values again
    }
    if (screen.form == "game-playlists") {
        favouriteStatus_.clear();
    }
}

std::string GameProvider::leaving(const Screen& screen)
{
    if ((screen.form == "game-metadata" && metadata_.isDirty())
        || (screen.form == "game-launcher" && launcher_.isDirty())
        || (screen.form == "game-description" && story_.isDirty())) {
        return unsavedWarning;
    }
    return "";
}

void GameProvider::discard(const Screen& screen)
{
    if (screen.form == "game-metadata") {
        metadata_.reset();
        typing_.clear();
    }
    if (screen.form == "game-launcher") launcher_.reset();
    if (screen.form == "game-description") story_.reset();
}

void GameProvider::changed(const std::string& what)
{
    if (services_.library) {
        services_.library->changed(what);
    }
    generation_++;
}

void GameProvider::update(float dt)
{
    (void)dt;
    if (services_.library) {
        unsigned built = services_.library->status().generation;
        if (built != indexGeneration_) {
            indexGeneration_ = built;
            generation_++;
        }
    }
    if (collectJobs(false)) {
        generation_++;
    }
    IndexPtr idx = index();
    if (!idx) {
        return;
    }
    if (isGameForm(current_.form) && findRecord(*idx, current_)) {
        prepareMedia(*idx, current_);
        startMediaJob();
    }
    startThumbnailJob();
}

void GameProvider::finishPendingWork()
{
    collectJobs(true);
    IndexPtr idx = index();
    if (!idx) {
        return;
    }
    if (isGameForm(current_.form) && findRecord(*idx, current_)) {
        prepareMedia(*idx, current_);
        while (media_.slots.size() < media_.types.size()) {
            startMediaJob();
            if (!mediaJob_.result.valid()) {
                break;
            }
            collectJobs(true);
        }
    }
    while (!wantedThumbnails_.empty()) {
        startThumbnailJob();
        if (!thumbnailJob_.result.valid()) {
            break;
        }
        collectJobs(true);
    }
    generation_++;
}

// --- Workers ------------------------------------------------------------------------------

void GameProvider::startMediaJob()
{
    if (mediaJob_.result.valid() || media_.key.empty()) {
        return;
    }
    IndexPtr idx = index();
    if (!idx) {
        return;
    }
    // A few slots per lookup, the ones a screen shows first
    std::vector<std::string> order;
    for (const std::string& type : { priorityType_, std::string("logo"), std::string("artwork_front"), std::string("story") }) {
        if (!type.empty() && std::find(media_.types.begin(), media_.types.end(), type) != media_.types.end()) {
            order.push_back(type);
        }
    }
    order.insert(order.end(), media_.types.begin(), media_.types.end());
    std::vector<std::string> batch;
    for (const std::string& type : order) {
        if (!media_.slots.count(type) && std::find(batch.begin(), batch.end(), type) == batch.end()) {
            batch.push_back(type);
        }
        if (batch.size() >= 4) {
            break;
        }
    }
    if (batch.empty()) {
        return;
    }
    size_t slash = media_.key.find('/');
    std::string collection = media_.key.substr(0, slash);
    std::string name = media_.key.substr(slash + 1);
    mediaJob_.key = media_.key;
    mediaJob_.epoch = mediaEpoch_;
    mediaJob_.result = std::async(std::launch::async, [idx, collection, name, batch] {
        // File checks only, on a snapshot that never changes
        std::map<std::string, MediaSlot> slots;
        int game = idx->findGame(collection, name);
        int hidden = game < 0 ? idx->findHiddenGame(collection, name) : -1;
        for (const std::string& type : batch) {
            if (game >= 0) {
                slots[type] = Library::gameMedia(*idx, game, type, Library::defaultKind(type));
            }
            else if (hidden >= 0) {
                // A hidden game keeps its media, found as RetroFE would if it listed it
                slots[type] = Library::gameMedia(*idx, idx->hiddenGames[static_cast<size_t>(hidden)], type,
                                                 Library::defaultKind(type));
            }
            else {
                MediaSlot empty;
                empty.type = type;
                empty.kind = Library::defaultKind(type);
                slots[type] = empty;
            }
        }
        return slots;
    });
}

void GameProvider::startThumbnailJob()
{
    if (thumbnailJob_.result.valid() || wantedThumbnails_.empty()) {
        return;
    }
    IndexPtr idx = index();
    if (!idx) {
        return;
    }
    std::vector<std::pair<std::string, std::string>> batch;
    while (!wantedThumbnails_.empty() && batch.size() < 24) {
        auto wanted = wantedThumbnails_.front();
        wantedThumbnails_.erase(wantedThumbnails_.begin());
        std::string key = wanted.first + "/" + wanted.second;
        if (!thumbnails_.count(key) && thumbnailsAsked_.insert(key).second) {
            batch.push_back(wanted);
        }
    }
    if (batch.empty()) {
        return;
    }
    thumbnailJob_.result = std::async(std::launch::async, [idx, batch] {
        std::vector<std::pair<std::string, std::string>> found;
        for (const auto& [collection, name] : batch) {
            found.emplace_back(collection + "/" + name, thumbnail(*idx, collection, name));
        }
        return found;
    });
}

bool GameProvider::collectJobs(bool wait)
{
    bool any = false;
    if (mediaJob_.result.valid() && (wait || isReady(mediaJob_.result))) {
        std::map<std::string, MediaSlot> slots = mediaJob_.result.get();
        // Dropped when the user moved to another game, or a slot was changed meanwhile
        if (mediaJob_.key == media_.key && mediaJob_.epoch == mediaEpoch_) {
            for (auto& [type, found] : slots) {
                media_.slots.emplace(type, std::move(found));
            }
            any = true;
        }
    }
    if (thumbnailJob_.result.valid() && (wait || isReady(thumbnailJob_.result))) {
        for (auto& [key, file] : thumbnailJob_.result.get()) {
            thumbnails_[key] = file;
            thumbnailsAsked_.erase(key);
        }
        any = true;
    }
    return any;
}

// --- get / set --------------------------------------------------------------------------

std::string GameProvider::get(const std::string& key, const Screen& screen)
{
    if (key == "status") return searchStatus();
    if (key == "query") return query_;
    if (key == "filter") return filter_;
    if (key == "onlyNoMetadata") return yesNo(onlyNoMetadata_);
    if (key == "onlyHidden") return yesNo(onlyHidden_);
    if (key == "filterSummary") return filterSummary(false);
    if (key == "filterCount") return filterSummary(true);
    if (key == "isHidden" || key == "hiddenNote") return hiddenValue(key, screen);

    if (startsWith(key, "meta:") || startsWith(key, "metaStatus:") || startsWith(key, "isChanged:")
        || startsWith(key, "metaPick:") || startsWith(key, "metaTyping:")
        || key == "isMetadataDirty" || key == "metadataNote" || key == "metadataFile" || key == "hasOverrides") {
        return metadataValue(key, screen);
    }
    if (key == "launcher" || key == "systemLauncher" || key == "gameLauncher" || key == "launcherCommand"
        || key == "launcherProblem" || key == "launcherFile" || key == "isLauncherDirty" || key == "hasLauncherOverride") {
        return launcherValue(key, screen);
    }
    if (startsWith(key, "slot:") || startsWith(key, "folderFile:") || key == "mediaNote") {
        return slotValue(key, screen);
    }
    if (startsWith(key, "story:") || key == "isStoryDirty") {
        return storyValue(key, screen);
    }
    if (key == "favouritePage" || key == "hasSeveralPages" || key == "favouriteText" || key == "isFavourite"
        || key == "favouriteToggle" || key == "canFavourite" || key == "favouriteNote" || key == "playlist"
        || key == "playlistStatus" || key == "isInPlaylist" || key == "canAddToPlaylist" || key == "hasPlaylists"
        || key == "merged" || key == "mergedStatus" || key == "isInMerged" || key == "hasMerged") {
        return playlistValue(key, screen);
    }
    if (key == "favouriteStatus") {
        return favouriteStatusKey_ == gameKey(screen) ? favouriteStatus_ : "";
    }
    return detail(key, screen);
}

Forms::Severity GameProvider::severity(const std::string& key, const Screen& screen)
{
    using Forms::Severity;
    if (key == "launcherStatus" || key == "file") {
        IndexPtr idx = index();
        const auto* game = idx ? findGame(*idx, screen) : nullptr;
        if (!game) return Severity::None;
        if (key == "file") return game->missing || game->file.empty() ? Severity::Error : Severity::None;
        return idx->resolveLauncher(game->collection, game->name).launcher < 0 ? Severity::Warning : Severity::None;
    }
    if (key == "launcherProblem" || key == "unresolvedNote" || key == "notFound") {
        return get(key, screen).empty() ? Severity::None : Severity::Warning;
    }
    if (key == "slot:status") {
        const MediaSlot* s = slot(screen, screen.param("type"));
        return s && !s->own && s->file.empty() ? Severity::Warning : Severity::None;
    }
    return Severity::None;
}

void GameProvider::set(const std::string& key, const std::string& value, const Screen& screen)
{
    if (key == "query") {
        query_ = value;
    }
    else if (key == "filter") {
        filter_ = value;
    }
    else if (key == "onlyNoMetadata") {
        onlyNoMetadata_ = Forms::isTruthy(value);
    }
    else if (key == "onlyHidden") {
        onlyHidden_ = Forms::isTruthy(value);
    }
    else if (startsWith(key, "metaPick:")) {
        // A value the system's games have, or "Other...", which shows a text field instead
        std::string field = key.substr(9);
        if (value == otherChoice) {
            typing_.insert(field);
        }
        else {
            typing_.erase(field);
            set("meta:" + field, value, screen);
        }
    }
    else if (key == "favouriteToggle") {
        // Adds or removes the favourite straight away, saying so under the toggle
        bool wanted = Forms::isTruthy(value);
        if (wanted != (playlistValue("isFavourite", screen) == "yes")) {
            Forms::Model::Result result = runAction(wanted ? "addFavourite" : "removeFavourite", screen);
            favouriteStatusKey_ = gameKey(screen);
            favouriteStatus_ = result.message + (result.ok && !result.info ? " RetroFE shows it after it reloads, when you close Manage." : "");
        }
    }
    else if (startsWith(key, "meta:")) {
        IndexPtr idx = index();
        if (idx) {
            loadMetadata(*idx, screen);
        }
        if (metadata_.id() == gameKey(screen)) {
            metadata_.set(key.substr(5), value);
        }
        else {
            // Kept until the game's metadata can be loaded
            pendingMetadata_[key.substr(5)] = value;
        }
    }
    else if (key == "launcher") {
        if (IndexPtr idx = index()) {
            loadLauncher(*idx, screen);
            launcher_.set("launcher", value);
        }
    }
    else if (key == "slot:newFile") {
        newFile_ = value;
    }
    else if (key == "story:text") {
        if (loadStory(screen)) {
            story_.set("text", value);
        }
    }
    else if (key == "playlist") {
        playlist_ = value;
    }
    else if (key == "merged") {
        merged_ = value;
    }
    else if (key == "favouritePage") {
        favouritePage_ = value;
    }
}

std::vector<std::string> GameProvider::list(const std::string& key, const Screen& screen)
{
    std::vector<std::string> values;
    IndexPtr idx = index();
    int g = -1;
    const GameRecord* game = idx ? findGame(*idx, screen, &g) : nullptr;
    if (!game) {
        return values;
    }
    if (key == "playlistsListing") {
        for (const Index::PlaylistRef& ref : idx->playlistsListing(g)) {
            const std::string& c = idx->collections[static_cast<size_t>(ref.collection)].name;
            std::string name = Library::iequals(ref.playlist, "favorites") ? "Favourites" : ref.playlist;
            values.push_back(name + " (" + c + ")");
        }
    }
    else if (key == "shownInList") {
        for (int m : idx->mergedCollectionsShowing(g)) {
            values.push_back(idx->collections[static_cast<size_t>(m)].name);
        }
    }
    return values;
}

// --- Search -----------------------------------------------------------------------------

std::string GameProvider::searchStatus() const
{
    if (index()) {
        return "";
    }
    if (!services_.library) {
        return "The library isn't available.";
    }
    Library::Indexer::Status status = services_.library->status();
    if (status.state == Library::Indexer::State::Failed) {
        return "Your games couldn't be read: " + status.error;
    }
    int percent = static_cast<int>(status.progress * 100);
    return "Reading your games" + (status.phase.empty() ? std::string() : ": " + status.phase)
        + " (" + std::to_string(percent) + "%)...";
}

std::string GameProvider::filterSummary(bool withCount)
{
    // "All games", "Arcade", "Arcade, without metadata", "Hidden games in Arcade"
    std::string text;
    if (onlyHidden_) {
        text = filter_.empty() ? "Hidden games" : "Hidden games in " + filter_;
    }
    else {
        text = filter_.empty() ? "All games" : filter_;
        if (onlyNoMetadata_) {
            text += ", without metadata";
        }
    }
    if (!withCount) {
        return text;
    }
    if (!index()) {
        return "";
    }
    // How many the search shows with these filters (and the search text, if any)
    size_t total = onlyHidden_ ? hiddenRows(1).total : searchRows(1).total;
    std::string digits = std::to_string(total);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) {
        digits.insert(static_cast<size_t>(at), ",");
    }
    return (total == 1 ? std::string("1 game") : digits + " games")
        + (Utils::trimEnds(query_).empty() ? "" : " matching \"" + Utils::trimEnds(query_) + "\"");
}

std::string GameProvider::thumbnail(const Index& index, const std::string& collection, const std::string& name)
{
    // Only the game's own logo or front art, by its name: a few file checks,
    // not the whole fallback chain (which would show "default" art on every row)
    for (const char* type : { "logo", "artwork_front" }) {
        std::string file;
        if (Library::findMediaFile(index.properties.mediaPath(collection, type, false), safeBasename(name),
                                   MediaKind::Image, file)) {
            return file;
        }
    }
    return "";
}

Forms::Rows GameProvider::searchRows(size_t limit)
{
    Forms::Rows rows;
    IndexPtr idx = index();
    if (!idx) {
        return rows;
    }
    Library::GameQuery query;
    query.text = Utils::trimEnds(query_);
    query.collection = filter_;
    query.onlyWithoutMetadata = onlyNoMetadata_;
    query.limit = limit;
    const Library::Results<int>& results = search_.games(idx, query);
    rows.total = results.total;
    wantedThumbnails_.clear();
    for (int g : results.rows) {
        const GameRecord& game = idx->games[static_cast<size_t>(g)];
        const std::string& system = idx->collections[static_cast<size_t>(game.collection)].name;
        Forms::Row row;
        row.id = game.name;
        row.label = game.title.empty() ? game.name : game.title;
        row.detail = system + "  -  " + (game.missing || game.file.empty() ? "file missing" : fileName(game.file));
        row.params["collection"] = system;
        std::string key = system + "/" + game.name;
        auto thumb = thumbnails_.find(key);
        if (thumb != thumbnails_.end()) {
            row.image = thumb->second;
        }
        else if (!thumbnailsAsked_.count(key)) {
            wantedThumbnails_.emplace_back(system, game.name);   // the worker looks them up, top rows first
        }
        rows.items.push_back(std::move(row));
    }
    return rows;
}

// --- Hidden games -------------------------------------------------------------------------

const GameProvider::HiddenGame* GameProvider::findHidden(const IndexPtr& idx, const Screen& screen) const
{
    int h = idx ? idx->findHiddenGame(screen.param("collection"), screen.param("id")) : -1;
    return h < 0 ? nullptr : &idx->hiddenGames[static_cast<size_t>(h)];
}

std::vector<char> GameProvider::systemsUnder(const Index& idx, const std::string& filter) const
{
    // The systems whose games a filter covers: the system itself, a merged
    // collection's sources, everything below a menu
    std::vector<char> under(idx.collections.size(), filter.empty() ? 1 : 0);
    int top = filter.empty() ? -1 : idx.findCollection(filter);
    if (top < 0) {
        return under;
    }
    under[static_cast<size_t>(top)] = 1;
    for (const Library::MenuRow& row : idx.menuTree(idx.collections[static_cast<size_t>(top)].name)) {
        if (row.collection >= 0) {
            under[static_cast<size_t>(row.collection)] = 1;
        }
    }
    for (size_t c = 0; c < idx.collections.size(); c++) {
        if (under[c]) {
            for (const Library::SubSource& sub : idx.collections[c].subs) {
                if (sub.collection >= 0) {
                    under[static_cast<size_t>(sub.collection)] = 1;
                }
            }
        }
    }
    return under;
}

Forms::Rows GameProvider::hiddenRows(size_t limit)
{
    Forms::Rows rows;
    IndexPtr idx = index();
    if (!idx) {
        return rows;
    }
    if (idx != hiddenIndex_) {
        hiddenIndex_ = idx;
        hiddenSearchKey_ = "\x01";
    }
    std::string text = Library::toLower(Utils::trimEnds(query_));
    std::string key = text + '\n' + filter_;
    if (key != hiddenSearchKey_) {
        // Once per query, filter and index; by name or title, as the search does
        hiddenSearchKey_ = key;
        hiddenMatches_.clear();
        std::vector<char> under = systemsUnder(*idx, filter_);
        for (const HiddenGame& game : idx->hiddenGames) {
            if (under[static_cast<size_t>(game.collection)] && Library::icontains(game.searchText, text)) {
                hiddenMatches_.push_back(&game);
            }
        }
    }
    rows.total = hiddenMatches_.size();
    wantedThumbnails_.clear();
    for (const HiddenGame* game : hiddenMatches_) {
        if (rows.items.size() >= limit) {
            break;
        }
        const std::string& system = idx->collections[static_cast<size_t>(game->collection)].name;
        Forms::Row row;
        row.id = game->name;
        row.label = game->title.empty() ? game->name : game->title;
        row.detail = system + "  -  hidden" + (game->file.empty() ? "" : ": " + fileName(game->file));
        row.params["collection"] = system;
        std::string thumbKey = system + "/" + game->name;
        auto thumb = thumbnails_.find(thumbKey);
        if (thumb != thumbnails_.end()) {
            row.image = thumb->second;
        }
        else if (!thumbnailsAsked_.count(thumbKey)) {
            wantedThumbnails_.emplace_back(system, game->name);
        }
        rows.items.push_back(std::move(row));
    }
    return rows;
}

std::string GameProvider::hiddenValue(const std::string& key, const Screen& screen)
{
    IndexPtr idx = index();
    const HiddenGame* hidden = idx && !findGame(*idx, screen) ? findHidden(idx, screen) : nullptr;
    if (key == "isHidden") return yesNo(hidden != nullptr);
    if (!hidden) return "";
    // The list's path from the root when it's inside it (list.excludeFile may point anywhere)
    const Library::CollectionRecord& record = idx->collections[static_cast<size_t>(hidden->collection)];
    std::string list = Library::narrow(fs::path(record.excludeFile).lexically_normal());   // one kind of slash
    fs::path relative = fs::path(record.excludeFile).lexically_relative(fs::path(idx->options.root));
    if (!relative.empty() && *relative.begin() != "..") {
        list = Library::narrow(relative);
        std::replace(list.begin(), list.end(), '\\', '/');
    }
    return "Hidden from RetroFE: " + list + " names it. Show this game again lists it.";
}

Forms::Model::Result GameProvider::hideAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    std::string collection = screen.param("collection");
    std::string name = screen.param("id");
    if (const GameRecord* game = findGame(*idx, screen)) {
        collection = idx->collections[static_cast<size_t>(game->collection)].name;
    }
    else if (const HiddenGame* hidden = findHidden(idx, screen)) {
        collection = idx->collections[static_cast<size_t>(hidden->collection)].name;
    }
    Library::EditResult edit = services_.editor->setGameHidden(*idx, collection, name, action == "hide");
    result.ok = edit.ok;
    result.message = edit.ok ? edit.message + (action == "hide" ? " Find it again with \"Only hidden games\" on Games." : "")
                                 + " RetroFE shows the change after it reloads, when you close Manage."
                             : edit.message;
    if (edit.ok && !edit.files.empty()) {
        changed("game hidden");
    }
    return result;
}

Forms::Rows GameProvider::rows(const std::string& key, size_t limit, const Screen& screen)
{
    if (key == "search") {
        return onlyHidden_ ? hiddenRows(limit) : searchRows(limit);
    }
    if (key == "media" || key == "mediaShown" || key == "mediaMissing") {
        return mediaRows(screen, key == "mediaShown" ? 1 : key == "mediaMissing" ? 2 : 0, limit);
    }
    Forms::Rows rows;
    IndexPtr idx = index();
    if (key == "addLaunchers") {
        // A link per launcher name this game or its system asks for that isn't defined
        const GameRecord* game = idx ? findGame(*idx, screen) : nullptr;
        if (!game) {
            return rows;
        }
        Library::LauncherResolution system = idx->resolveLauncher(game->collection);
        Library::LauncherResolution now = idx->resolveLauncher(game->collection, game->name);
        if (now.launcher >= 0) return rows;
        std::vector<std::string> asked = { now.overrideName, system.configured };
        asked.insert(asked.end(), system.alternatives.begin(), system.alternatives.end());
        for (const std::string& name : asked) {
            bool unresolved = std::find(idx->unresolvedLaunchers.begin(), idx->unresolvedLaunchers.end(), name)
                              != idx->unresolvedLaunchers.end();
            bool listed = std::any_of(rows.items.begin(), rows.items.end(), [&](const Forms::Row& r) { return r.id == name; });
            if (name.empty() || !unresolved || listed) {
                continue;
            }
            Forms::Row row;
            row.id = name;
            row.label = "Add a launcher called " + name;
            row.link = true;
            rows.items.push_back(row);
        }
        rows.total = rows.items.size();
        return rows;
    }
    if (key == "newPlaylist" || key == "newVideoPlaylist") {
        // Playlists are made on the system's Playlists screen
        const GameRecord* game = idx ? findGame(*idx, screen) : nullptr;
        if (!game) {
            return rows;
        }
        const std::string& own = idx->collections[static_cast<size_t>(game->collection)].name;
        Forms::Row row;
        row.id = own;
        if (isVideoCollection(idx->collections[static_cast<size_t>(game->collection)]) != (key == "newVideoPlaylist")) return rows;
        row.label = "New playlist...";
        row.detail = "on " + own + "'s page";
        row.link = true;
        rows.items.push_back(row);
        rows.total = 1;
        return rows;
    }
    if (key == "slot:folderFiles") {
        // The files of the slot's own media folder, each opening its screen
        const MediaSlot* s = slot(screen, screen.param("type"));
        if (!s) {
            return rows;
        }
        rows.total = s->folderFiles.size();
        for (const std::string& file : s->folderFiles) {
            if (rows.items.size() >= limit) {
                break;
            }
            Forms::Row row;
            row.id = fileName(file);
            row.label = row.id;
            row.detail = s->fileBesideFolder() ? "not shown" : "";
            row.image = s->kind == MediaKind::Image ? file : "";
            row.params = { { "collection", screen.param("collection") }, { "id", screen.param("id") }, { "type", s->type } };
            rows.items.push_back(std::move(row));
        }
        return rows;
    }
    return rows;
}

Forms::Rows GameProvider::mediaRows(const Screen& screen, int which, size_t limit)
{
    // Every slot (which 0), the ones that show something (1), or the missing
    // ones (2), with what shows in words; pictures as thumbnails. A slot not
    // looked up yet counts as missing until it is.
    Forms::Rows rows;
    IndexPtr idx = index();
    if (!idx || !findRecord(*idx, screen)) {
        return rows;
    }
    prepareMedia(*idx, screen);
    for (const std::string& type : media_.types) {
        auto found = media_.slots.find(type);
        bool shows = found != media_.slots.end() && !found->second.file.empty();
        if ((which == 1 && !shows) || (which == 2 && shows)) {
            continue;
        }
        rows.total++;
        if (rows.items.size() >= limit) {
            continue;
        }
        Forms::Row row;
        row.id = type;
        row.label = mediaLabel(type);
        row.params["collection"] = screen.param("collection");
        row.params["id"] = screen.param("id");
        if (found == media_.slots.end()) {
            row.detail = "Checking...";
        }
        else {
            const MediaSlot& slot = found->second;
            row.detail = slot.own ? (slot.fromFolder() ? "Its own folder (" + std::to_string(slot.shownFolderFiles) + ")" : "Its own file")
                       : !slot.file.empty() ? "Fallback: " + slot.fallback : "Missing";
            row.severity = !slot.own && slot.file.empty() ? Forms::Severity::Warning : Forms::Severity::None;
            if (slot.kind == MediaKind::Image) {
                row.image = slot.file;
            }
        }
        rows.items.push_back(std::move(row));
    }
    return rows;
}

std::vector<std::pair<std::string, std::string>> GameProvider::options(const std::string& key, const Screen& screen)
{
    std::vector<std::pair<std::string, std::string>> result;
    if (startsWith(key, "metaChoices:")) {
        return metadataChoices(key.substr(12), screen);
    }
    IndexPtr idx = index();
    if (key == "filters") {
        if (idx && idx != filtersIndex_) {
            // Menus in the order Browse shows them (a menu covers everything
            // below it), then systems and merged collections no menu lists
            filtersIndex_ = idx;
            filters_.clear();
            std::vector<char> seen(idx->collections.size(), 0);
            for (const Library::MenuRow& row : idx->menuTree()) {
                if (row.collection < 0 || row.cycle || seen[static_cast<size_t>(row.collection)]) {
                    continue;
                }
                seen[static_cast<size_t>(row.collection)] = 1;
                const Library::CollectionRecord& record = idx->collections[static_cast<size_t>(row.collection)];
                if (record.reserved) {
                    continue;
                }
                filters_.emplace_back(record.name, record.name + (record.is(Library::KindMenu) ? " (menu)" : ""));
            }
            for (size_t c = 0; c < idx->collections.size(); c++) {
                const Library::CollectionRecord& record = idx->collections[c];
                if (!seen[c] && !record.reserved && record.kinds != 0) {
                    filters_.emplace_back(record.name, record.name + (record.is(Library::KindMenu) ? " (menu)" : ""));
                }
            }
        }
        result.emplace_back("", "All games");
        result.insert(result.end(), filters_.begin(), filters_.end());
        bool listed = filter_.empty() || std::any_of(result.begin(), result.end(), [&](const auto& option) {
            return option.first == filter_;
        });
        if (!listed) {
            result.emplace_back(filter_, filter_ + (idx ? " (not found)" : ""));
        }
        return result;
    }

    int g = -1;
    const GameRecord* game = idx ? findGame(*idx, screen, &g) : nullptr;
    if (!game) {
        return result;
    }
    const Library::CollectionRecord& own = idx->collections[static_cast<size_t>(game->collection)];
    if (key == "launchers") {
        // In the order a launch looks names up: this system's local launchers
        // hide global ones of the same name, so each name is offered once
        Library::LauncherResolution system = idx->resolveLauncher(game->collection);
        result.emplace_back("", "Use the system's launcher" + (system.name.empty() ? std::string() : " (" + system.name + ")"));
        auto offered = [&](const std::string& value) {
            return std::any_of(result.begin(), result.end(), [&](const auto& option) { return option.first == value; });
        };
        auto globalNamed = [&](const std::string& name) {
            return std::any_of(idx->launchers.begin(), idx->launchers.end(), [&](const Library::LauncherDef& def) {
                return def.scope == Library::LauncherScope::Global && def.id == name;
            });
        };
        for (const Library::LauncherDef& def : idx->launchers) {
            if (def.scope == Library::LauncherScope::Local && def.collection == own.name && !offered(def.name)) {
                // A game's override names a local launcher by its own name
                result.emplace_back(def.name, def.name + " (" + own.name + " only"
                    + (globalNamed(def.name) ? "; it hides the global " + def.name : "") + ")");
            }
        }
        for (const Library::LauncherDef& def : idx->launchers) {
            if (def.scope == Library::LauncherScope::Collection && def.collection == own.name && !offered(def.id)) {
                result.emplace_back(def.id, def.id + " (the system's launcher file)");
            }
        }
        for (const Library::LauncherDef& def : idx->launchers) {
            if (def.scope == Library::LauncherScope::Global && !offered(def.id)) {
                result.emplace_back(def.id, def.id);
            }
        }
        for (auto& option : result) {
            if (!option.first.empty() && option.first == system.configured) {
                option.second += " - the system's launcher: same as using the system's";
            }
        }
        loadLauncher(*idx, screen);
        std::string picked = launcher_.get("launcher");
        if (!offered(picked)) {
            result.emplace_back(picked, picked + " (not installed)");
        }
    }
    else if (key == "favouritePages") {
        for (int page : pages(*idx, g)) {
            const std::string& name = idx->collections[static_cast<size_t>(page)].name;
            result.emplace_back(name, name + (page == game->collection ? " (its own system)" : "")
                + (isFavourite(*idx, g, page) ? " - a favourite" : ""));
        }
    }
    else if (key == "playlists") {
        // Playlists of the pages that show the game: its own system and the merged collections showing it
        for (int page : pages(*idx, g)) {
            const Library::CollectionRecord& record = idx->collections[static_cast<size_t>(page)];
            for (const Library::PlaylistInfo& playlist : record.playlists) {
                if (!playlist.reserved) {
                    result.emplace_back(record.name + "/" + playlist.name, playlist.name + " (" + record.name + ")");
                }
            }
        }
    }
    else if (key == "mergedCollections") {
        const std::vector<int>& showing = idx->mergedCollectionsShowing(g);
        for (size_t c = 0; c < idx->collections.size(); c++) {
            const Library::CollectionRecord& record = idx->collections[c];
            if (!record.is(Library::KindMerged) || record.reserved || static_cast<int>(c) == game->collection) {
                continue;
            }
            bool shows = std::find(showing.begin(), showing.end(), static_cast<int>(c)) != showing.end();
            result.emplace_back(record.name, record.name + (shows ? " (shows it)" : ""));
        }
    }
    return result;
}

// --- Favourites ---------------------------------------------------------------------------

bool GameProvider::globalFavourites(const Index& index) const
{
    bool global = false;
    return index.properties.getBool("globalFavLast", global) && global;
}

std::vector<int> GameProvider::pages(const Index& index, int game) const
{
    std::vector<int> result = { index.games[static_cast<size_t>(game)].collection };
    for (int m : index.mergedCollectionsShowing(game)) {
        result.push_back(m);
    }
    return result;
}

bool GameProvider::isFavourite(const Index& index, int game, int page) const
{
    // RetroFE keeps favourites per page: collections/<page>/playlists/favorites.txt
    for (const Index::PlaylistRef& ref : index.playlistsListing(game)) {
        if (ref.collection == page && Library::iequals(ref.playlist, "favorites")) {
            return true;
        }
    }
    return false;
}

bool GameProvider::inGlobalFavourites(const Index& index, const GameRecord& game)
{
    // With globalFavLast, RetroFE writes every favourite to the Favorites
    // collection's list as _<system>:<game>. Read once per game.
    const std::string& own = index.collections[static_cast<size_t>(game.collection)].name;
    std::string key = own + "/" + game.name;
    if (globalFavouritesKey_ != key) {
        globalFavouritesKey_ = key;
        inGlobalFavourites_ = false;
        Library::LineList list(Utils::combinePath(index.options.root, "collections", "Favorites", "playlists", "favorites.txt"));
        if (list.load()) {
            for (const std::string& entry : list.entries()) {
                Library::LineList::Qualified q = Library::LineList::parseQualified(entry, "Favorites");
                inGlobalFavourites_ |= q.collection == own && q.name == game.name;
            }
        }
    }
    return inGlobalFavourites_;
}

std::vector<std::string> GameProvider::favouritePages(const Index& index, int game)
{
    std::vector<std::string> names;
    for (int page : pages(index, game)) {
        if (isFavourite(index, game, page)) {
            names.push_back(index.collections[static_cast<size_t>(page)].name);
        }
    }
    return names;
}

int GameProvider::chosenPage(const Index& index, int game) const
{
    for (int page : pages(index, game)) {
        if (index.collections[static_cast<size_t>(page)].name == favouritePage_) {
            return page;
        }
    }
    return index.games[static_cast<size_t>(game)].collection;
}

// --- Details ----------------------------------------------------------------------------

std::string GameProvider::playlistsSummary(const Index& index, int game)
{
    std::vector<std::string> parts;
    const GameRecord& record = index.games[static_cast<size_t>(game)];
    bool favourite = globalFavourites(index) ? inGlobalFavourites(index, record) : !favouritePages(index, game).empty();
    if (favourite) {
        parts.push_back("Favourite");
    }
    size_t playlists = 0;
    for (const Index::PlaylistRef& ref : index.playlistsListing(game)) {
        playlists += Library::iequals(ref.playlist, "favorites") ? 0 : 1;
    }
    if (playlists > 0) {
        parts.push_back(std::to_string(playlists) + (playlists == 1 ? " playlist" : " playlists"));
    }
    size_t merged = index.mergedCollectionsShowing(game).size();
    if (merged > 0) {
        parts.push_back("in " + std::to_string(merged) + (merged == 1 ? " collection" : " collections"));
    }
    return parts.empty() ? "None" : join(parts);
}

std::string GameProvider::mediaSummary(const Screen& screen)
{
    IndexPtr idx = index();
    if (!idx || !findRecord(*idx, screen)) {
        return "";
    }
    prepareMedia(*idx, screen);
    if (media_.slots.size() < media_.types.size()) {
        return "Checking...";
    }
    size_t own = 0;
    for (const auto& [type, slot] : media_.slots) {
        own += slot.own ? 1 : 0;
    }
    return std::to_string(own) + " of " + std::to_string(media_.types.size());
}

std::string GameProvider::detail(const std::string& key, const Screen& screen)
{
    IndexPtr idx = index();
    int g = -1;
    const GameRecord* game = idx ? findGame(*idx, screen, &g) : nullptr;
    // A hidden game: what RetroFE would list if its exclude list didn't name it
    const GameRecord* record = game ? game : idx ? findRecord(*idx, screen) : nullptr;

    if (key == "isFound") return yesNo(game != nullptr);
    if (key == "isKnown") return yesNo(record != nullptr);
    if (key == "notFound") {
        if (!idx) return searchStatus();
        if (record) return "";
        return "This game isn't in the library any more: it may have been removed or renamed. Go back and search again.";
    }
    if (key == "title" || key == "screenTitle") {
        // A hidden game has its title too (the library reads their metadata)
        const GameRecord* named = record;
        std::string title = named ? (named->title.empty() ? named->name : named->title) : screen.param("id");
        if (key == "title" || screen.form == "game") return title;
        static const std::map<std::string, std::string> sections = {
            { "game-metadata", "Metadata" }, { "game-launcher", "Launcher" }, { "game-media", "Media" },
            { "game-playlists", "Playlists and collections" }, { "game-description", "Description" },
        };
        auto section = sections.find(screen.form);
        if (screen.form == "game-media-slot") return title + " - " + mediaLabel(screen.param("type"));
        if (screen.form == "game-media-folder-file") return title + " - " + mediaLabel(screen.param("type")) + " folder";
        return section == sections.end() ? title : title + " - " + section->second;
    }
    if (!record) {
        return "";
    }
    if (!game) {
        // A hidden game: no file to measure, plays, favourites or launcher, as RetroFE doesn't list it
        if (key == "file") return fileName(record->file);
        if (key == "size" || key == "played" || key == "timePlayed" || key == "favourite" || key == "shownIn"
            || key == "launcherStatus" || key == "playlistsStatus") return "";
    }
    const Library::CollectionRecord& own = idx->collections[static_cast<size_t>(record->collection)];
    std::string key2 = gameKey(screen);
    if (detailsKey_ != key2) {
        // Once per game: its file size, play counts and metadata changes (files read)
        detailsKey_ = key2;
        std::error_code ec;
        uintmax_t bytes = record->file.empty() ? 0 : fs::file_size(record->file, ec);
        size_ = record->file.empty() || ec ? "" : describeSize(bytes);
        PlayStats stats = readPlayStats(idx->options.root, own.name, record->name);
        played_ = describePlays(stats);
        if (stats.count <= 0) {
            std::string why = playsNotCounted(idx->properties.getOr("lastPlayedSkipCollection"),
                                              idx->properties.getOr("lastplayedSize", "10"), own.name);
            if (!why.empty()) played_ = why;
        }
        timePlayed_ = stats.seconds >= 1 ? describeTime(stats.seconds) : "";
        overrides_ = services_.editor ? services_.editor->metadataOverrides(*idx, own.name, record->name)
                                      : std::map<std::string, std::string>();
    }

    if (key == "system") return own.title != own.name ? own.name + " (" + own.title + ")" : own.name;
    if (key == "from") return own.name;
    if (key == "folder") return record->directory;
    if (key == "year") return record->meta.year;
    if (key == "manufacturer") return record->meta.manufacturer;
    if (key == "developer") return record->meta.developer;
    if (key == "genre") return record->meta.genre;
    if (key == "players") return record->meta.players;
    if (key == "image") {
        // The logo, else the front art: the game's own or a fallback by its
        // title or clone, but not the collection's "default" picture. Shown
        // once the worker has looked them up.
        for (const char* type : { "logo", "artwork_front" }) {
            const MediaSlot* found = slot(screen, type);
            if (found && !found->file.empty() && found->fallback.find("default") == std::string::npos) {
                return found->file;
            }
        }
        return "";
    }
    if (key == "metadataStatus") {
        if (!overrides_.empty()) return "Changed here";
        return record->hasMetadata ? "From " + own.metadataType : "None";
    }
    if (key == "mediaStatus") return mediaSummary(screen);
    if (key == "descriptionStatus") {
        const MediaSlot* story = slot(screen, "story");
        if (!story) return "Checking...";
        if (story->file.empty()) return "None";
        return story->own ? "Yes" : "From " + story->fallback;
    }
    if (!game) {
        return "";
    }

    if (key == "file") return game->missing || game->file.empty() ? "Missing: listed, but there's no file" : fileName(game->file);
    if (key == "size") return size_;
    if (key == "played") return played_;
    if (key == "timePlayed") return timePlayed_;
    if (key == "favourite") {
        if (globalFavourites(*idx)) return inGlobalFavourites(*idx, *game) ? "Yes (in the Favorites collection)" : "";
        std::vector<std::string> names = favouritePages(*idx, g);
        if (names.size() == 1 && names[0] == own.name) return "Yes";
        return names.empty() ? "" : "In " + join(names);
    }
    if (key == "shownIn") {
        std::vector<std::string> names;
        for (int m : idx->mergedCollectionsShowing(g)) {
            names.push_back(idx->collections[static_cast<size_t>(m)].name);
        }
        return join(names);
    }
    if (key == "launcherStatus") {
        Library::LauncherResolution r = idx->resolveLauncher(game->collection, game->name);
        if (r.launcher < 0) return "Won't start";
        return r.name + (r.via == Library::LauncherVia::GameOverride ? " (its own)" : "");
    }
    if (key == "playlistsStatus") return playlistsSummary(*idx, g);
    return "";
}

// --- Media ------------------------------------------------------------------------------

std::vector<std::string> GameProvider::orderedMediaTypes(const std::vector<std::string>& types)
{
    static const std::vector<std::string> first = {
        "logo", "artwork_front", "marquee", "screenshot", "screentitle", "bezel", "video", "story",
    };
    std::vector<std::string> ordered;
    for (const std::string& type : first) {
        if (std::find(types.begin(), types.end(), type) != types.end()) {
            ordered.push_back(type);
        }
    }
    for (const std::string& type : types) {
        if (std::find(ordered.begin(), ordered.end(), type) == ordered.end()) {
            ordered.push_back(type);
        }
    }
    return ordered;
}

std::string GameProvider::mediaLabel(const std::string& type)
{
    static const std::map<std::string, std::string> labels = {
        { "logo", "Logo" }, { "artwork_front", "Front art" }, { "artwork_back", "Back art" },
        { "artwork_3d", "3D box" }, { "artwork_front_s", "Front art (small)" }, { "marquee", "Marquee" },
        { "led_marquee", "LED marquee" }, { "screenshot", "Screenshot" }, { "screentitle", "Title screen" },
        { "bezel", "Bezel" }, { "video", "Video" }, { "videoPlayer", "Video (player)" }, { "videoSD", "Video (SD)" },
        { "story", "Story text" }, { "medium_front", "Medium (front)" }, { "medium_back", "Medium (back)" },
        { "cabinet", "Cabinet" }, { "fanart", "Fan art" }, { "neon", "Neon" },
    };
    auto found = labels.find(type);
    return found != labels.end() ? found->second : type;
}

void GameProvider::prepareMedia(const Index& index, const Screen& screen)
{
    std::string key = gameKey(screen);
    if (media_.key == key) {
        return;
    }
    media_ = MediaCache();
    mediaEpoch_++;
    const GameRecord* game = findRecord(index, screen);
    if (!game) {
        return;
    }
    media_.key = key;
    // Lists the media folder's sub-folders: once per game
    media_.types = orderedMediaTypes(Library::mediaTypes(index, game->collection));
}

const MediaSlot* GameProvider::slot(const Screen& screen, const std::string& type)
{
    IndexPtr idx = index();
    if (!idx || type.empty() || !findRecord(*idx, screen)) {
        return nullptr;
    }
    prepareMedia(*idx, screen);
    auto found = media_.slots.find(type);
    if (found == media_.slots.end()) {
        if (std::find(media_.types.begin(), media_.types.end(), type) == media_.types.end()) {
            media_.types.push_back(type);   // a type the folders don't have yet (e.g. a first story)
        }
        return nullptr;   // update() has the worker look it up
    }
    return &found->second;
}

void GameProvider::forgetSlot(const std::string& type)
{
    media_.slots.erase(type);
    media_.originals.erase(type);
    mediaEpoch_++;   // a lookup running now may have read the old file
    generation_++;
}

std::string GameProvider::slotStatus(const MediaSlot& slot) const
{
    if (slot.own && slot.fromFolder()) return "Its own folder: " + Library::mediaShownText(slot);
    if (slot.own) return slot.fileBesideFolder() ? "Its own file (it wins over the folder beside it)" : "Its own file";
    if (!slot.file.empty()) return "A fallback from the " + slot.fallback + (slot.fromFolder() ? ": " + Library::mediaShownText(slot) : "");
    return "Missing";
}

const std::vector<std::string>& GameProvider::originals(const MediaSlot& slot)
{
    // Asked every frame (hasOriginal, the Put back button's problem): worked
    // out once per looked-up slot; forgetSlot() and a new game drop it
    auto cached = media_.originals.find(slot.type);
    if (cached != media_.originals.end()) {
        return cached->second;
    }
    std::vector<std::string>& files = media_.originals[slot.type];
    if (!services_.editor || slot.directory.empty() || slot.basename.empty()) {
        return files;
    }
    // The slot's own files that Manage changed and that differ now: each extension it could have
    for (const std::string& extension : Library::acceptedExtensions(slot.kind)) {
        std::string file = Utils::combinePath(slot.directory, slot.basename + "." + extension);
        if (services_.editor->differsFromOriginal(file)) {
            files.push_back(file);
        }
    }
    // and the files of its media folder Manage changed
    for (const std::string& file : services_.editor->mediaFolderOriginals(slot)) {
        files.push_back(file);
    }
    return files;
}

std::string GameProvider::slotValue(const std::string& key, const Screen& screen)
{
    if (key == "mediaNote") {
        IndexPtr idx = index();
        if (!idx) return searchStatus();
        return "What RetroFE shows for each kind of media on the game's own page. A fallback is another file "
               "it shows instead (by title, parent or the default). Choose one to replace it.";
    }
    if (key == "slot:newFile") return newFile_;
    if (key == "slot:title") return mediaLabel(screen.param("type"));
    const MediaSlot* s = slot(screen, screen.param("type"));
    if (!s) {
        if (key == "slot:status") {
            IndexPtr idx = index();
            return idx && findRecord(*idx, screen) ? lookingMessage : goneMessage;
        }
        return startsWith(key, "slot:is") || startsWith(key, "slot:can") || startsWith(key, "slot:has") ? "no" : "";
    }
    if (startsWith(key, "folderFile:")) {
        std::string name = screen.param("file");
        std::string file = s->folder.empty() ? "" : Utils::combinePath(s->folder, name);
        bool there = std::find(s->folderFiles.begin(), s->folderFiles.end(), file) != s->folderFiles.end();
        if (key == "folderFile:name") return name;
        if (key == "folderFile:folder") return s->folder;
        if (key == "folderFile:image") return there && s->kind == MediaKind::Image ? file : "";
        if (key == "folderFile:note") {
            if (!there) return "This file isn't in the folder any more.";
            if (s->fileBesideFolder()) return "A file beside the folder (" + fileName(s->file) + ") wins, so RetroFE doesn't show this one.";
            return "One of the folder's " + std::to_string(s->folderFiles.size()) + " files: RetroFE shows one of them at random. "
                   "Keep only this file puts it beside the folder as " + s->basename + " and empties the folder.";
        }
        return "";
    }
    if (key == "slot:image") return s->kind == MediaKind::Image ? s->file : "";
    if (key == "slot:file") return s->fromFolder() ? Library::mediaShownText(*s) : s->file;
    if (key == "slot:status") return slotStatus(*s);
    if (key == "slot:place") return Utils::combinePath(s->directory, s->basename) + ".*";
    if (key == "slot:folderNote") {
        std::string note = Library::mediaFolderNote(*s);
        if (!note.empty() && Library::iequals(s->basename, "default")) {
            note += " A game named default shares this folder with every game that has no " + s->type + " of its own.";
        }
        return note;
    }
    if (key == "slot:hasFolder") return yesNo(!s->folder.empty());
    if (key == "slot:isImage") return yesNo(s->kind == MediaKind::Image);
    if (key == "slot:isVideo") return yesNo(s->kind == MediaKind::Video);
    if (key == "slot:isText") return yesNo(s->kind == MediaKind::Text);
    if (key == "slot:canClear") return yesNo(s->own && !s->fromFolder());
    if (key == "slot:hasOriginal") return yesNo(!originals(*s).empty());
    if (key == "slot:text") {
        std::string content;
        if (s->kind != MediaKind::Text || s->file.empty() || !Library::readFile(s->file, content)) return "";
        return preview(content);
    }
    return "";
}

Forms::Model::Result GameProvider::mediaAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    std::string type = screen.param("type");
    const MediaSlot* s = slot(screen, type);
    if (!s) {
        result.message = goneMessage;
        return result;
    }
    MediaSlot copy = *s;
    Library::EditResult edit;
    if (action == "replaceMedia") {
        edit = services_.editor->replaceMedia(copy, Utils::trimEnds(newFile_));
    }
    else if (action == "clearMedia") {
        edit = services_.editor->clearMedia(copy);
    }
    else if (action == "addToFolder") {
        edit = services_.editor->addMediaFolderFile(copy, Utils::trimEnds(newFile_));
    }
    else if (action == "clearFolder") {
        edit = services_.editor->clearMediaFolder(copy);
    }
    else if (action == "removeFolderFile") {
        edit = services_.editor->removeMediaFolderFile(copy, screen.param("file"));
    }
    else if (action == "keepFolderFile") {
        edit = services_.editor->mediaFolderToFile(copy, screen.param("file"));
    }
    else {
        std::vector<std::string> files = originals(copy);
        std::string label = mediaLabel(type);
        if (!label.empty() && label != type) {
            label[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(label[0])));
        }
        edit.ok = !files.empty();
        edit.message = edit.ok ? "Put back the original " + label + "." : "There's no original kept for this slot.";
        for (const std::string& file : files) {
            Library::EditResult restored = services_.editor->restoreOriginal(file);
            if (!restored.ok) {
                edit = restored;
                break;
            }
        }
    }
    result.ok = edit.ok;
    result.message = edit.message;
    // A failure part way can still have changed files (a folder made one
    // file, then not emptied): look the slot up again whenever any were touched
    if (edit.ok || !edit.files.empty()) {
        // Back to the list: pictures are cached by path, and the slot's file
        // has the same path as before, so a fresh screen shows the new one.
        // A folder's changes stay on the slot's screen (adding several files),
        // or go back to it from one of its files.
        result.back = edit.ok && action != "addToFolder" && action != "clearFolder";
        if (edit.ok) {
            newFile_.clear();
        }
        forgetSlot(type);
        thumbnails_.erase(screen.param("collection") + "/" + screen.param("id"));
        if (type == "story") {
            story_.load("", {});
        }
        changed("game media");
    }
    return result;
}

// --- Metadata ---------------------------------------------------------------------------

void GameProvider::loadMetadata(const Index& index, const Screen& screen)
{
    std::string key = gameKey(screen);
    if (metadata_.id() == key) {
        return;   // keeps unsaved edits when coming back to the same game
    }
    const GameRecord* game = findRecord(index, screen);
    if (!game) {
        return;
    }
    const Library::CollectionRecord& own = index.collections[static_cast<size_t>(game->collection)];
    metadataSaved_.clear();
    for (const std::string& field : editedFields()) {
        metadataSaved_[field] = currentValue(*game, field);
    }
    metadata_.load(key, metadataSaved_);
    // Anything typed before the library was read
    for (const auto& [field, value] : pendingMetadata_) {
        metadata_.set(field, value);
    }
    pendingMetadata_.clear();
    metadataType_ = own.metadataType;
    overrides_ = services_.editor ? services_.editor->metadataOverrides(index, own.name, game->name)
                                  : std::map<std::string, std::string>();
    detailsKey_ = "";   // the details screen re-reads them too
}

const std::map<std::string, std::string>& GameProvider::shipped(const Index& index, const Screen& screen, bool& known)
{
    // Parses the shipped hyperlist files (MAME.xml is several MB), so only for
    // a game with changes, once per game
    static const std::map<std::string, std::string> none;
    known = false;
    const GameRecord* game = findRecord(index, screen);
    if (!game) {
        return none;
    }
    const std::string& type = index.collections[static_cast<size_t>(game->collection)].metadataType;
    std::string key = type + "\n" + game->name;
    auto found = shippedCache_.find(key);
    if (found == shippedCache_.end()) {
        Shipped entry;
        Library::MetadataOverrides::Shipped outcome =
            Library::MetadataOverrides::shipped(index.options.root, type, game->name, entry.values);
        entry.known = outcome != Library::MetadataOverrides::Shipped::Unknown;
        if (outcome == Library::MetadataOverrides::Shipped::NotFound) {
            entry.values.clear();
        }
        found = shippedCache_.emplace(key, std::move(entry)).first;
    }
    known = found->second.known;
    return found->second.values;
}

std::string GameProvider::metadataValue(const std::string& key, const Screen& screen)
{
    IndexPtr idx = index();
    const GameRecord* game = idx ? findRecord(*idx, screen) : nullptr;
    if (!game) {
        return key == "isMetadataDirty" || key == "hasOverrides" || startsWith(key, "isChanged:")
            || startsWith(key, "metaTyping:") ? "no" : "";
    }
    loadMetadata(*idx, screen);
    if (key == "isMetadataDirty") return yesNo(metadata_.isDirty());
    if (key == "hasOverrides") return yesNo(!overrides_.empty());
    if (key == "metadataFile") return "manage/metadata/" + metadataType_ + ".xml";
    if (key == "metadataNote") {
        std::string note = "Content updates don't undo these changes. RetroFE shows them after it reloads, when you close Manage.";
        return game->hasMetadata ? note : "This game has no metadata yet; saving a field adds it. " + note;
    }
    if (startsWith(key, "metaPick:")) {
        std::string field = key.substr(9);
        return typing_.count(field) ? otherChoice : metadata_.get(field);
    }
    if (startsWith(key, "metaTyping:")) return yesNo(typing_.count(key.substr(11)) > 0);
    if (startsWith(key, "meta:")) return metadata_.get(key.substr(5));
    if (startsWith(key, "isChanged:")) return yesNo(overrides_.count(key.substr(10)) > 0);
    if (startsWith(key, "metaStatus:")) {
        std::string field = key.substr(11);
        if (metadata_.changes().count(field)) return "Not saved yet.";
        if (!overrides_.count(field)) return "";
        bool known = false;
        const std::map<std::string, std::string>& values = shipped(*idx, screen, known);
        if (!known) return "Changed here.";
        auto value = values.find(field);
        std::string text = value == values.end() ? "" : value->second;
        return "Changed here. Shipped: " + (text.empty() ? std::string("nothing") : text);
    }
    return "";
}

std::vector<std::pair<std::string, std::string>> GameProvider::metadataChoices(const std::string& field, const Screen& screen)
{
    std::vector<std::pair<std::string, std::string>> result;
    IndexPtr idx = index();
    const GameRecord* game = idx ? findRecord(*idx, screen) : nullptr;
    if (!game || !isChoiceField(field)) {
        return result;
    }
    loadMetadata(*idx, screen);
    if (idx != choicesIndex_) {
        choicesIndex_ = idx;
        choices_.clear();
    }
    // The values the system's games (hidden ones too) have, once per index
    std::string cacheKey = std::to_string(game->collection) + "\n" + field;
    auto cached = choices_.find(cacheKey);
    if (cached == choices_.end()) {
        std::set<std::string> seen;
        auto add = [&](const GameRecord& other) {
            if (other.collection == game->collection) {
                std::string value = Utils::trimEnds(currentValue(other, field));
                if (!value.empty()) seen.insert(value);
            }
        };
        const Library::CollectionRecord& own = idx->collections[static_cast<size_t>(game->collection)];
        for (size_t g = own.gameBegin; g < own.gameBegin + own.gameCount && g < idx->games.size(); g++) {
            add(idx->games[g]);
        }
        for (const GameRecord& hidden : idx->hiddenGames) {
            add(hidden);
        }
        std::vector<std::string> values(seen.begin(), seen.end());
        std::sort(values.begin(), values.end(), naturalLess);
        cached = choices_.emplace(cacheKey, std::move(values)).first;
    }
    result.emplace_back("", "None");
    result.emplace_back(otherChoice, "Other...");
    for (const std::string& value : cached->second) {
        result.emplace_back(value, value);
    }
    // A value typed (or saved) that no game of the system has yet
    std::string current = metadata_.get(field);
    bool listed = current.empty() || std::find(cached->second.begin(), cached->second.end(), current) != cached->second.end();
    if (!listed) {
        result.emplace_back(current, current);
    }
    return result;
}

Forms::Model::Result GameProvider::metadataAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    const GameRecord* game = idx ? findRecord(*idx, screen) : nullptr;
    if (!game) {
        result.message = goneMessage;
        return result;
    }
    const std::string own = idx->collections[static_cast<size_t>(game->collection)].name;
    const std::string name = game->name;
    loadMetadata(*idx, screen);

    if (action == "resetMetadata") {
        metadata_.reset();
        typing_.clear();
        result.ok = true;
        result.info = true;
        return result;
    }

    std::map<std::string, std::string> saved = metadataSaved_;
    std::map<std::string, std::string> pending = metadata_.changes();
    std::vector<std::string> done;
    std::string failure;
    std::string lastMessage;

    if (startsWith(action, "unset.")) {
        std::string field = action.substr(6);
        bool known = false;
        std::map<std::string, std::string> values = shipped(*idx, screen, known);
        Library::EditResult edit = services_.editor->clearMetadata(*idx, own, name, field);
        if (!edit.ok) {
            result.message = edit.message;
            return result;
        }
        // The field shows the shipped value again; other unsaved edits stay
        std::string back = values.count(field) ? values[field] : known ? "" : saved[field];
        if (field == "description" && back.empty()) {
            back = name;
        }
        saved[field] = back;
        pending.erase(field);
        lastMessage = edit.message;
        done.push_back(field);
    }
    else {
        for (const auto& [field, raw] : pending) {
            std::string value = Utils::trimEnds(raw);
            // Typing the shipped value back drops the change rather than storing a copy of it
            bool isShipped = false;
            if (overrides_.count(field)) {
                bool known = false;
                const std::map<std::string, std::string>& values = shipped(*idx, screen, known);
                auto shippedValue = values.find(field);
                isShipped = known && shippedValue != values.end() && shippedValue->second == value;
            }
            Library::EditResult edit = isShipped ? services_.editor->clearMetadata(*idx, own, name, field)
                                                 : services_.editor->setMetadata(*idx, own, name, field, value);
            if (!edit.ok) {
                failure = edit.message;
                break;
            }
            lastMessage = edit.message;
            saved[field] = value;
            done.push_back(field);
        }
        for (const std::string& field : done) {
            pending.erase(field);
        }
    }

    // Saved fields become the saved values; anything not saved stays an edit
    metadataSaved_ = saved;
    metadata_.load(gameKey(screen), saved);
    for (const auto& [field, value] : pending) {
        metadata_.set(field, value);
    }
    if (!done.empty()) {
        overrides_ = services_.editor->metadataOverrides(*idx, own, name);
        detailsKey_ = "";
        if (std::find(done.begin(), done.end(), "cloneof") != done.end()
            || std::find(done.begin(), done.end(), "description") != done.end()) {
            media_ = MediaCache();   // media fall back by title and clone
            mediaEpoch_++;
        }
        changed("game metadata");
    }
    if (!failure.empty()) {
        result.message = done.empty() ? failure : "Saved " + std::to_string(done.size()) + " of "
            + std::to_string(done.size() + pending.size()) + " changes. " + failure;
        return result;
    }
    result.ok = true;
    result.message = done.size() == 1 ? lastMessage : "Saved " + std::to_string(done.size()) + " changes.";
    for (const std::string& field : done) {
        typing_.erase(field);   // the choice shows the saved value
    }
    return result;
}

// --- Launcher ---------------------------------------------------------------------------

void GameProvider::loadLauncher(const Index& index, const Screen& screen)
{
    std::string key = gameKey(screen);
    if (launcher_.id() == key) {
        return;
    }
    const GameRecord* game = findGame(index, screen);
    if (!game) {
        return;
    }
    Library::LauncherResolution r = index.resolveLauncher(game->collection, game->name);
    launcher_.load(key, { { "launcher", r.overrideName } });
}

std::string GameProvider::launcherValue(const std::string& key, const Screen& screen)
{
    IndexPtr idx = index();
    const GameRecord* game = idx ? findGame(*idx, screen) : nullptr;
    if (!game) {
        return startsWith(key, "is") || startsWith(key, "has") ? "no" : "";
    }
    loadLauncher(*idx, screen);
    Library::LauncherResolution system = idx->resolveLauncher(game->collection);
    Library::LauncherResolution now = idx->resolveLauncher(game->collection, game->name);

    if (key == "launcher") return launcher_.get("launcher");
    if (key == "isLauncherDirty") return yesNo(launcher_.isDirty());
    if (key == "hasLauncherOverride") return yesNo(!now.overrideFile.empty());
    if (key == "systemLauncher") {
        if (system.name.empty()) return "None set";
        std::string where = system.via == Library::LauncherVia::CollectionFile ? " (the system's launcher file)" : "";
        std::string alternatives = system.alternatives.empty() ? "" : "; also listed: " + join(system.alternatives);
        return system.name + where + alternatives;
    }
    if (key == "gameLauncher") {
        if (now.name.empty()) return "None";
        return now.name + (now.via == Library::LauncherVia::GameOverride ? " (this game's own choice)" : " (the system's)");
    }
    if (key == "launcherCommand") {
        if (now.launcher < 0) return "";
        const Library::LauncherDef& def = idx->launchers[static_cast<size_t>(now.launcher)];
        return def.executable + (def.arguments.empty() ? "" : " " + def.arguments);
    }
    if (key == "launcherProblem") {
        // One line: why it won't start (the Add a launcher links follow it)
        if (now.launcher < 0) {
            return "It won't start. " + (now.problem.empty() ? std::string("No launcher is set for it or its system.") : now.problem);
        }
        return now.problem;
    }
    if (key == "launcherFile") {
        // The game's own launcher file: where Save writes the choice
        if (!now.overrideFile.empty()) return shownPath(idx->options.root, now.overrideFile);
        const Library::CollectionRecord& own = idx->collections[static_cast<size_t>(game->collection)];
        return shownPath(idx->options.root, Utils::combinePath(own.folder, "launchers", game->name + ".conf"));
    }
    return "";
}

Forms::Model::Result GameProvider::launcherAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    const GameRecord* game = idx ? findGame(*idx, screen) : nullptr;
    if (!game) {
        result.message = goneMessage;
        return result;
    }
    std::string own = idx->collections[static_cast<size_t>(game->collection)].name;
    loadLauncher(*idx, screen);
    if (action == "resetLauncher") {
        launcher_.reset();
        result.ok = true;
        result.info = true;
        return result;
    }
    std::string pick = action == "useSystemLauncher" ? "" : launcher_.get("launcher");
    Library::EditResult edit = services_.editor->setGameLauncher(*idx, own, game->name, pick);
    result.ok = edit.ok;
    result.message = edit.message;
    if (edit.ok) {
        // Picking the system's own launcher removes the file, so it reads as "the system's"
        Library::LauncherResolution system = idx->resolveLauncher(game->collection);
        launcher_.load(gameKey(screen), { { "launcher", pick == system.configured ? "" : pick } });
        changed("game launcher");
    }
    return result;
}

// --- Playlists and merged collections ---------------------------------------------------

std::string GameProvider::playlistValue(const std::string& key, const Screen& screen)
{
    IndexPtr idx = index();
    int g = -1;
    const GameRecord* game = idx ? findGame(*idx, screen, &g) : nullptr;
    if (!game) {
        return key == "playlist" || key == "merged" || key == "playlistStatus" || key == "mergedStatus"
            || key == "favouriteNote" || key == "favouritePage" || key == "favouriteText" ? "" : "no";
    }
    const std::string& own = idx->collections[static_cast<size_t>(game->collection)].name;
    if (favouritesIndex_ != idx) {
        favouritesIndex_ = idx;
        pendingFavourites_.clear();
    }
    bool global = globalFavourites(*idx);
    int page = chosenPage(*idx, g);
    const std::string& pageName = idx->collections[static_cast<size_t>(page)].name;
    if (key == "favouritePage") return pageName;
    if (key == "hasSeveralPages") return yesNo(!global && pages(*idx, g).size() > 1);
    if (key == "isFavourite" || key == "favouriteToggle") {
        auto pending = pendingFavourites_.find(gameKey(screen) + "\n" + pageName);
        if (pending != pendingFavourites_.end()) return yesNo(pending->second);
        return yesNo(global ? inGlobalFavourites(*idx, *game) : isFavourite(*idx, g, page));
    }
    if (key == "canFavourite") return yesNo(!global);
    if (key == "favouriteText") {
        if (global) return inGlobalFavourites(*idx, *game) ? "Yes, in the Favorites collection" : "No";
        return isFavourite(*idx, g, page) ? "Yes" : "No";
    }
    if (key == "favouriteNote") {
        if (global) {
            return "RetroFE keeps every favourite in the Favorites collection (globalFavLast), which Manage doesn't "
                   "change yet. Add or remove favourites while playing.";
        }
        return "Each page RetroFE shows the game on keeps its own favourites (collections/" + pageName
            + "/playlists/favorites.txt). RetroFE also changes these lists when you add a favourite while playing.";
    }

    if (key == "playlist" || key == "playlistStatus" || key == "isInPlaylist" || key == "hasPlaylists"
        || key == "canAddToPlaylist") {
        std::vector<std::pair<std::string, std::string>> choices = options("playlists", screen);
        if (key == "hasPlaylists") return yesNo(!choices.empty());
        if (key == "canAddToPlaylist") {
            return yesNo(!choices.empty() && playlistValue("isInPlaylist", screen) != "yes");
        }
        bool valid = std::any_of(choices.begin(), choices.end(), [&](const auto& c) { return c.first == playlist_; });
        std::string chosen = valid ? playlist_ : choices.empty() ? "" : choices.front().first;
        if (key == "playlist") return chosen;
        size_t slash = chosen.find('/');
        bool in = false;
        if (slash != std::string::npos) {
            int c = idx->findCollection(chosen.substr(0, slash));
            std::string name = chosen.substr(slash + 1);
            for (const Index::PlaylistRef& ref : idx->playlistsListing(g)) {
                in |= ref.collection == c && ref.playlist == name;
            }
        }
        if (key == "isInPlaylist") return yesNo(in);
        if (chosen.empty()) return "";
        return in ? "It's in this playlist." : "It isn't in this playlist.";
    }

    std::vector<std::pair<std::string, std::string>> choices = options("mergedCollections", screen);
    if (key == "hasMerged") return yesNo(!choices.empty());
    bool valid = std::any_of(choices.begin(), choices.end(), [&](const auto& c) { return c.first == merged_; });
    std::string chosen = valid ? merged_ : choices.empty() ? "" : choices.front().first;
    if (key == "merged") return chosen;
    int m = idx->findCollection(chosen);
    const std::vector<int>& showing = idx->mergedCollectionsShowing(g);
    bool in = m >= 0 && std::find(showing.begin(), showing.end(), m) != showing.end();
    if (key == "isInMerged") return yesNo(in);
    if (chosen.empty() || m < 0) return "";
    if (!in) return chosen + " doesn't show it.";
    for (const Library::SubSource& sub : idx->collections[static_cast<size_t>(m)].subs) {
        if (sub.collection == game->collection && sub.entries.empty()) {
            return chosen + " shows it, with every other game of " + own + " (its " + sub.source + ".sub is empty).";
        }
    }
    return chosen + " shows it (listed in its " + own + ".sub).";
}

Forms::Model::Result GameProvider::playlistAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    int g = -1;
    const GameRecord* game = idx ? findGame(*idx, screen, &g) : nullptr;
    if (!game) {
        result.message = goneMessage;
        return result;
    }
    std::string own = idx->collections[static_cast<size_t>(game->collection)].name;
    std::string name = game->name;
    Library::EditResult edit;
    if (action == "addFavourite" || action == "removeFavourite") {
        // The page's own favourites, as RetroFE's CollectionInfo::saveFavorites writes them
        std::string page = idx->collections[static_cast<size_t>(chosenPage(*idx, g))].name;
        bool adding = action == "addFavourite";
        edit = adding ? services_.editor->addToPlaylist(*idx, page, "favorites", own, name)
                      : services_.editor->removeFromPlaylist(*idx, page, "favorites", own, name);
        if (edit.ok) {
            if (favouritesIndex_ != idx) {
                favouritesIndex_ = idx;
                pendingFavourites_.clear();
            }
            pendingFavourites_[gameKey(screen) + "\n" + page] = adding;
        }
        // Only when something changed: "... is already in favorites." is kept as it is
        if (edit.ok && !edit.files.empty()) {
            edit.message = (adding ? "Added " + name + " to the favourites of " : "Removed " + name + " from the favourites of ")
                + page + ".";
        }
    }
    else if (action == "addToPlaylist" || action == "removeFromPlaylist") {
        std::string chosen = playlistValue("playlist", screen);
        size_t slash = chosen.find('/');
        if (slash == std::string::npos) {
            result.message = "There are no playlists to choose from.";
            return result;
        }
        std::string page = chosen.substr(0, slash);
        std::string playlist = chosen.substr(slash + 1);
        edit = action == "addToPlaylist" ? services_.editor->addToPlaylist(*idx, page, playlist, own, name)
                                         : services_.editor->removeFromPlaylist(*idx, page, playlist, own, name);
    }
    else {
        std::string merged = playlistValue("merged", screen);
        edit = action == "addToMerged" ? services_.editor->addToSub(*idx, merged, own, name)
                                       : services_.editor->removeFromSub(*idx, merged, own, name);
    }
    result.ok = edit.ok;
    result.message = edit.message;
    result.info = edit.ok && edit.files.empty();   // "... is already in ..."
    if (edit.ok && !edit.files.empty()) {
        changed("game playlists");
    }
    return result;
}

// --- Story text -------------------------------------------------------------------------

std::string GameProvider::storyToText(const std::string& story)
{
    // Every line, blank ones too, without its line ending
    Library::Lines lines = Library::Lines::parse(story);
    std::vector<std::string> parts;
    for (const Library::Line& line : lines.lines) {
        parts.push_back(line.text);
    }
    return join(parts, "\n");
}

std::string GameProvider::textToStory(const std::string& text, const std::string& original)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t newline = text.find('\n', start);
        parts.push_back(text.substr(start, newline == std::string::npos ? std::string::npos : newline - start));
        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1;
    }

    // Each line keeps the ending the original's line in its place had; new
    // lines get the file's own; the final line break is kept as it was
    Library::Lines lines = Library::Lines::parse(original);
    std::string newline = original.empty() ? Library::defaultNewline() : lines.newline();
    bool finalBreak = original.empty() || !lines.lines.back().eol.empty();
    std::string story;
    for (size_t i = 0; i < parts.size(); i++) {
        std::string eol = i < lines.lines.size() ? lines.lines[i].eol : newline;
        if (eol.empty()) {
            eol = newline;
        }
        if (i + 1 == parts.size() && !finalBreak) {
            eol.clear();
        }
        story += parts[i] + eol;
    }
    return story;
}

bool GameProvider::loadStory(const Screen& screen)
{
    std::string key = gameKey(screen);
    if (story_.id() == key) {
        return true;
    }
    const MediaSlot* s = slot(screen, "story");
    if (!s) {
        return false;   // not looked up yet
    }
    storyOriginal_.clear();
    if (!s->file.empty()) {
        Library::readFile(s->file, storyOriginal_);
    }
    story_.load(key, { { "text", storyToText(storyOriginal_) } });
    return true;
}

// --- Play counts --------------------------------------------------------------------------

GameProvider::PlayStats GameProvider::readPlayStats(const std::string& root, const std::string& system, const std::string& game)
{
    // As CollectionInfoBuilder::ImportPlayCount / ImportTimeSpent read them:
    // '#' starts a comment, '\r' is dropped, the key ends at the first ';'
    PlayStats stats;
    std::string key = "_" + system + ":" + game;
    auto eachLine = [](const std::string& file, const std::function<void(const std::string&, const std::string&)>& use) {
        std::string content;
        if (!Library::readFile(file, content)) {
            return;
        }
        for (const Library::Line& line : Library::Lines::parse(content).lines) {
            std::string text = line.text.substr(0, line.text.find('#'));
            text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
            size_t separator = text.find(';');
            if (separator != std::string::npos) {
                use(text.substr(0, separator), text.substr(separator + 1));
            }
        }
    };
    bool exact = false;
    eachLine(Utils::combinePath(root, "collections", "playCount.txt"), [&](const std::string& name, const std::string& rest) {
        size_t separator = rest.find(';');
        bool qualified = name == key;
        if (separator == std::string::npos || (!qualified && (exact || name != game))) {
            return;   // the "_<system>:<game>" line wins over a plain "<game>" one
        }
        stats.count = std::max(0, Utils::convertInt(rest.substr(0, separator)));
        stats.lastPlayed = Utils::trimEnds(rest.substr(separator + 1));
        exact |= qualified;
    });
    eachLine(Utils::combinePath(root, "collections", "timeSpent.txt"), [&](const std::string& name, const std::string& rest) {
        if (name == key) {
            char* end = nullptr;
            double seconds = std::strtod(rest.c_str(), &end);
            stats.seconds = end != rest.c_str() && seconds > 0 ? seconds : 0;
        }
    });
    return stats;
}

std::string GameProvider::playsNotCounted(const std::string& skipCollections, const std::string& lastPlayedSize,
                                          const std::string& system)
{
    // RetroFE (as upstream) counts a play only when lastPlayedSkipCollection is
    // set and doesn't name the game's system, and lastplayedSize isn't 0
    std::string skip = Utils::trimEnds(skipCollections);
    if (skip.empty()) {
        return "Not counted (set lastPlayedSkipCollection to count plays)";
    }
    size_t start = 0;
    while (start <= skip.size()) {
        size_t comma = skip.find(',', start);
        std::string name = Utils::trimEnds(skip.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (name == system) {
            return "Not counted (lastPlayedSkipCollection skips " + system + ")";
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    std::string size = Utils::trimEnds(lastPlayedSize);
    if (!size.empty() && std::atoi(size.c_str()) <= 0) {
        return "Not counted (lastplayedSize is 0)";
    }
    return "";
}

std::string GameProvider::describePlays(const PlayStats& stats)
{
    if (stats.count <= 0) {
        return "Never";
    }
    std::string text = stats.count == 1 ? "Once" : std::to_string(stats.count) + " times";
    char* end = nullptr;
    long long epoch = std::strtoll(stats.lastPlayed.c_str(), &end, 10);
    if (end != stats.lastPlayed.c_str() && epoch > 0) {
        std::time_t time = static_cast<std::time_t>(epoch);
        std::tm local{};
#ifdef _WIN32
        bool converted = localtime_s(&local, &time) == 0;
#else
        bool converted = localtime_r(&time, &local) != nullptr;
#endif
        char date[32];
        if (converted && std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &local) > 0) {
            text += (stats.count == 1 ? ", on " : ", last on ") + std::string(date);
        }
    }
    return text;
}

std::string GameProvider::describeTime(double seconds)
{
    long long total = static_cast<long long>(seconds + 0.5);
    if (total < 60) {
        return std::to_string(total) + " s";
    }
    long long minutes = total / 60;
    if (minutes < 60) {
        return std::to_string(minutes) + " min";
    }
    return std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " min";
}

std::string GameProvider::storyValue(const std::string& key, const Screen& screen)
{
    const MediaSlot* s = slot(screen, "story");
    if (!s) {
        if (key == "story:status") {
            IndexPtr idx = index();
            return idx && findRecord(*idx, screen) ? lookingMessage : "";
        }
        return startsWith(key, "is") || startsWith(key, "story:can") || startsWith(key, "story:has") ? "no" : "";
    }
    loadStory(screen);
    if (key == "story:text") return story_.get("text");
    if (key == "isStoryDirty") return yesNo(story_.isDirty());
    if (key == "story:canEdit") return "yes";   // once its file has been looked up
    if (key == "story:canClear") return yesNo(s->own);
    if (key == "story:hasOriginal") return yesNo(!originals(*s).empty());
    if (key == "story:status") {
        if (s->own) return "";
        if (!s->file.empty()) return "Shows a fallback from the " + s->fallback + " (" + s->file
            + "). Saving gives the game its own text.";
        return "No story text yet.";
    }
    if (key == "story:file") {
        // Where Save writes: its own file, or the one it would have
        if (s->own) return s->file;
        return s->directory.empty() ? "" : Utils::combinePath(s->directory, s->basename) + ".txt";
    }
    return "";
}

Forms::Model::Result GameProvider::storyAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    if (action == "replaceStory") {
        result.ok = true;
        result.open = "game-media-slot";
        result.params["type"] = "story";
        result.keepParams = true;
        return result;
    }
    if (action == "resetStory") {
        story_.reset();
        result.ok = true;
        result.info = true;
        return result;
    }
    const MediaSlot* s = slot(screen, "story");
    if (!s || !loadStory(screen)) {
        result.message = s ? lookingMessage : goneMessage;
        return result;
    }
    MediaSlot copy = *s;
    Library::EditResult edit;
    if (action == "saveStory") {
        edit = services_.editor->writeMediaText(copy, textToStory(story_.get("text"), storyOriginal_));
    }
    else if (action == "clearStory") {
        edit = services_.editor->clearMedia(copy);
    }
    else {
        std::vector<std::string> files = originals(copy);
        edit.ok = !files.empty();
        edit.message = edit.ok ? "Put back the original story text." : "There's no original kept for the story.";
        for (const std::string& file : files) {
            Library::EditResult restored = services_.editor->restoreOriginal(file);
            if (!restored.ok) {
                edit = restored;
                break;
            }
        }
    }
    result.ok = edit.ok;
    result.message = edit.message;
    if (edit.ok) {
        // Read again from disk: the story text is cheap to look up now, and the
        // screen should show what was written
        forgetSlot("story");
        story_.load("", {});
        IndexPtr idx = index();
        if (const GameRecord* record = idx ? findRecord(*idx, screen) : nullptr) {
            media_.slots["story"] = Library::gameMedia(*idx, *record, "story", MediaKind::Text);
            loadStory(screen);
        }
        changed("game story");
    }
    return result;
}

// --- Actions ----------------------------------------------------------------------------

std::string GameProvider::problem(const std::string& action, const Screen& screen)
{
    static const std::vector<std::string> known = {
        "saveMetadata", "resetMetadata", "saveLauncher", "resetLauncher", "useSystemLauncher",
        "replaceMedia", "clearMedia", "restoreMedia", "addToFolder", "clearFolder", "removeFolderFile", "keepFolderFile",
        "addFavourite", "removeFavourite", "addToPlaylist",
        "removeFromPlaylist", "addToMerged", "removeFromMerged", "saveStory", "resetStory", "clearStory",
        "restoreStory", "replaceStory", "launch", "hide", "unhide",
    };
    bool isUnset = startsWith(action, "unset.");
    if (!isUnset && std::find(known.begin(), known.end(), action) == known.end()) {
        return Provider::actionProblem(action, screen);
    }
    IndexPtr idx = index();
    if (!idx) return "Your games are still being read.";
    if (action == "launch" && !services_.requestLaunch) return "Games can't be started from here.";
    if (action != "launch" && !services_.editor) return "Changes can't be made here.";
    int g = -1;
    const GameRecord* game = findGame(*idx, screen, &g);
    if (action == "unhide") {
        if (game) return "It isn't hidden.";
        return findHidden(idx, screen) ? "" : goneMessage;
    }
    // A hidden game's metadata, media and story can be changed; the rest needs RetroFE to list it
    static const std::vector<std::string> hiddenToo = {
        "saveMetadata", "resetMetadata", "replaceMedia", "clearMedia", "restoreMedia", "addToFolder", "clearFolder",
        "removeFolderFile", "keepFolderFile", "saveStory", "resetStory", "clearStory", "restoreStory", "replaceStory",
    };
    bool forHidden = isUnset || std::find(hiddenToo.begin(), hiddenToo.end(), action) != hiddenToo.end();
    if (!game && !(forHidden && findRecord(*idx, screen))) {
        if (action == "hide" && findHidden(idx, screen)) return "It's hidden already.";
        return findHidden(idx, screen) ? "It's hidden: show this game again first." : goneMessage;
    }

    if (action == "hide") {
        return "";   // listed now, so its exclude list doesn't name it (the editor checks the file)
    }
    if (action == "launch") {
        if (game->missing || game->file.empty()) return "Its file is missing, so it can't start.";
        Library::LauncherResolution r = idx->resolveLauncher(game->collection, game->name);
        if (r.launcher < 0) {
            return "It won't start: " + (r.problem.empty() ? std::string("no launcher is set for it or its system.") : r.problem);
        }
        // Its program, checked on disk once per game and snapshot (a missing
        // one would leave RetroFE waiting for a window that never comes)
        std::string checkKey = gameKey(screen) + '\n' + std::to_string(reinterpret_cast<uintptr_t>(idx.get()));
        if (checkKey != launchCheckKey_) {
            launchCheckKey_ = checkKey;
            const Library::LauncherDef& def = idx->launchers[static_cast<size_t>(r.launcher)];
            Emulators::ExecutableCheck check = Emulators::checkExecutable(*idx, def.executable);
            launchCheck_ = check.state == Emulators::ExecutableCheck::State::Missing
                ? "It won't start: " + r.name + "'s program isn't there (" + (check.path.empty() ? def.executable : check.path) + ")."
                : "";
        }
        if (!launchCheck_.empty()) return launchCheck_;
        return services_.launchProblem ? services_.launchProblem() : "";
    }

    if (isUnset) {
        std::string field = action.substr(6);
        if (std::find(editedFields().begin(), editedFields().end(), field) == editedFields().end()) {
            return Provider::actionProblem(action, screen);
        }
        loadMetadata(*idx, screen);
        return overrides_.count(field) ? "" : "It hasn't been changed here.";
    }
    if (action == "saveMetadata" || action == "resetMetadata") {
        loadMetadata(*idx, screen);
        return metadata_.isDirty() ? "" : "No changes to save.";
    }
    if (action == "saveLauncher" || action == "resetLauncher") {
        loadLauncher(*idx, screen);
        return launcher_.isDirty() ? "" : "No changes to save.";
    }
    if (action == "useSystemLauncher") {
        return idx->resolveLauncher(game->collection, game->name).overrideFile.empty()
            ? "It already uses the system's launcher." : "";
    }
    if (action == "replaceMedia" || action == "clearMedia" || action == "restoreMedia") {
        const MediaSlot* s = slot(screen, screen.param("type"));
        if (!s) return lookingMessage;
        if (action == "replaceMedia") return Utils::trimEnds(newFile_).empty() ? "Choose a file first." : "";
        if (action == "clearMedia") {
            if (s->own && s->fromFolder()) return "It's a folder of files, not one file: take files out of it, or clear the folder.";
            if (s->own) return "";
            return s->file.empty() ? "There's nothing to clear." : "This shows a fallback from the " + s->fallback
                + "; there's no file of the game's own to clear.";
        }
        return originals(*s).empty() ? "There's no original kept for this slot." : "";
    }
    if (action == "addToFolder" || action == "clearFolder" || action == "removeFolderFile" || action == "keepFolderFile") {
        const MediaSlot* s = slot(screen, screen.param("type"));
        if (!s) return lookingMessage;
        if (s->folder.empty()) return "It has no folder of files.";
        if (action == "addToFolder") return Utils::trimEnds(newFile_).empty() ? "Choose a file first." : "";
        // A game named "default": its folder is the collection's default art,
        // which every game without its own shows; it isn't emptied from here
        if ((action == "clearFolder" || action == "keepFolderFile") && Library::iequals(s->basename, "default")) {
            return "This folder is the collection's default " + s->type + ", which every game without its own shows; "
                   "change it file by file, or in the folder itself.";
        }
        if (action == "clearFolder") return "";
        std::string file = Utils::combinePath(s->folder, screen.param("file"));
        return std::find(s->folderFiles.begin(), s->folderFiles.end(), file) == s->folderFiles.end()
            ? "This file isn't in the folder any more." : "";
    }
    if (action == "addFavourite" || action == "removeFavourite") {
        if (globalFavourites(*idx)) {
            return "Favourites are kept in the Favorites collection (globalFavLast), which Manage doesn't change yet.";
        }
        bool favourite = isFavourite(*idx, g, chosenPage(*idx, g));
        if (action == "addFavourite") return favourite ? "It's a favourite there already." : "";
        return favourite ? "" : "It isn't a favourite there.";
    }
    if (action == "addToPlaylist" || action == "removeFromPlaylist") {
        if (playlistValue("playlist", screen).empty()) return "There are no playlists to choose from.";
        bool in = playlistValue("isInPlaylist", screen) == "yes";
        if (action == "addToPlaylist") return in ? "It's in this playlist already." : "";
        return in ? "" : "It isn't in this playlist.";
    }
    if (action == "addToMerged" || action == "removeFromMerged") {
        std::string merged = playlistValue("merged", screen);
        if (merged.empty()) return "There are no merged collections.";
        bool in = playlistValue("isInMerged", screen) == "yes";
        if (action == "addToMerged") return in ? "It's shown there already." : "";
        if (!in) return "It isn't shown there.";
        // An empty .sub shows every game of the system: one can't be taken out
        int m = idx->findCollection(merged);
        for (const Library::SubSource& sub : idx->collections[static_cast<size_t>(m)].subs) {
            if (sub.collection == game->collection && sub.entries.empty()) {
                return merged + " shows every game of " + idx->collections[static_cast<size_t>(game->collection)].name
                    + " (its " + sub.source + ".sub is empty), so one can't be taken out here.";
            }
        }
        return "";
    }
    if (action == "saveStory" || action == "resetStory") {
        if (!loadStory(screen)) return lookingMessage;
        if (!story_.isDirty()) return "No changes to save.";
        if (action == "saveStory" && story_.get("text").find_first_not_of(" \t\r\n") == std::string::npos) {
            return "The story is empty. To remove it, use Clear.";
        }
        return "";
    }
    if (action == "clearStory" || action == "restoreStory") {
        const MediaSlot* s = slot(screen, "story");
        if (!s) return lookingMessage;
        if (action == "clearStory") {
            if (s->own) return "";
            return s->file.empty() ? "There's no story text to clear." : "This shows a fallback from the " + s->fallback
                + "; there's no text of the game's own to clear.";
        }
        return originals(*s).empty() ? "There's no original kept for the story." : "";
    }
    return "";
}

std::string GameProvider::actionProblem(const std::string& action, const Screen& screen)
{
    Pin pin(pinned_, index());
    return problem(action, screen);
}

Forms::Model::Result GameProvider::runAction(const std::string& action, const Screen& screen)
{
    // One snapshot from the check to the write: a new index published
    // meanwhile (the game renamed or removed) can't pull the game away
    Pin pin(pinned_, index());
    Forms::Model::Result result;
    result.message = problem(action, screen);
    if (!result.message.empty()) {
        return result;
    }
    if (action == "saveMetadata" || action == "resetMetadata" || startsWith(action, "unset.")) {
        return metadataAction(action, screen);
    }
    if (action == "saveLauncher" || action == "resetLauncher" || action == "useSystemLauncher") {
        return launcherAction(action, screen);
    }
    if (action == "replaceMedia" || action == "clearMedia" || action == "restoreMedia" || action == "addToFolder"
        || action == "clearFolder" || action == "removeFolderFile" || action == "keepFolderFile") {
        return mediaAction(action, screen);
    }
    if (action == "saveStory" || action == "resetStory" || action == "clearStory" || action == "restoreStory"
        || action == "replaceStory") {
        return storyAction(action, screen);
    }
    if (action == "hide" || action == "unhide") {
        return hideAction(action, screen);
    }
    if (action == "launch") {
        // RetroFE starts it once Manage has closed, as from its own system's page
        IndexPtr idx = index();
        const GameRecord* game = findGame(*idx, screen);
        const std::string& own = idx->collections[static_cast<size_t>(game->collection)].name;
        services_.requestLaunch(own, game->name);
        result.ok = true;
        result.message = "Starting " + (game->title.empty() ? game->name : game->title) + "...";
        return result;
    }
    return playlistAction(action, screen);
}

}

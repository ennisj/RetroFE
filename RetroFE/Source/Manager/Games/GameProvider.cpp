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
#include "../../Library/Edits.h"
#include "../../Library/Files.h"
#include "../../Library/Indexer.h"
#include "../../Library/MetadataOverrides.h"
#include "../../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using Library::GameRecord;
using Library::Index;
using Library::MediaKind;
using Library::MediaSlot;

// Bindings (game:<key>):
//   status                    while the library is being read: how far it got
//   query, filter (options filters), onlyNoMetadata    the search; rows search
//   title, screenTitle        the game's title; "<title> - Metadata" etc. for sub-screens
//   isFound, notFound         whether param collection + id is a game in the library
//   system, file, folder, size, year, manufacturer, developer, genre, players,
//   favourite, from, shownIn, image                           details
//   metadataStatus, launcherStatus, mediaStatus, playlistsStatus, descriptionStatus   link statuses
//   meta:<element>, metaStatus:<element>, isChanged:<element>, isMetadataDirty, metadataNote
//   launcher (options launchers), systemLauncher, gameLauncher, launcherCommand,
//   launcherProblem, unresolvedNote, isLauncherDirty, hasLauncherOverride
//   rows media; mediaNote; slot:title, slot:image, slot:file, slot:status, slot:place,
//   slot:text, slot:isImage, slot:isVideo, slot:isText, slot:canClear, slot:hasOriginal, slot:newFile
//   isFavourite, canFavourite, favouriteNote, list playlistsListing, playlist (options playlists),
//   playlistStatus, isInPlaylist, hasPlaylists, list shownInList, merged (options mergedCollections),
//   mergedStatus, isInMerged, hasMerged
//   story:text, story:status, story:preview, isStoryDirty, story:canClear, story:hasOriginal
// Actions (game.<name>):
//   saveMetadata, resetMetadata, unset.<element>
//   saveLauncher, resetLauncher, useSystemLauncher
//   replaceMedia, clearMedia, restoreMedia
//   addFavourite, removeFavourite, addToPlaylist, removeFromPlaylist, addToMerged, removeFromMerged
//   saveStory, resetStory, clearStory, restoreStory, replaceStory
namespace
{
    const char* const unsavedWarning = "You have unsaved changes. Save them, or leave again to discard them.";

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
}

namespace Manager
{

GameProvider::GameProvider(const Services& services)
    : services_(services)
{
}

// --- Plumbing ---------------------------------------------------------------------------

GameProvider::IndexPtr GameProvider::index() const
{
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

void GameProvider::refresh()
{
    // The manager is opening: files may have changed while it was closed
    filterParam_ = "\x01";
    filtersIndex_.reset();
    thumbnails_.clear();
    wantedThumbnails_.clear();
    detailsKey_.clear();
    media_ = MediaCache();
    newFile_.clear();
    metadata_.load("", {});
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
    if (screen.form == "game-media-slot") {
        newFile_.clear();
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
    if (screen.form == "game-metadata") metadata_.reset();
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
    IndexPtr idx = index();
    if (!idx) {
        return;
    }
    // A few file checks per frame: thumbnails for the search results, and the
    // slots of the game being looked at
    auto start = std::chrono::steady_clock::now();
    auto spent = [&] { return std::chrono::steady_clock::now() - start > std::chrono::milliseconds(4); };
    bool found = false;
    while (!wantedThumbnails_.empty() && !spent()) {
        auto [collection, name] = wantedThumbnails_.back();
        wantedThumbnails_.pop_back();
        std::string key = collection + "/" + name;
        if (!thumbnails_.count(key)) {
            thumbnails_[key] = thumbnail(*idx, collection, name);
            found = true;
        }
    }
    if (isGameForm(current_.form) && media_.key == gameKey(current_)) {
        for (const std::string& type : media_.types) {
            if (spent()) {
                break;
            }
            if (!media_.slots.count(type)) {
                slot(current_, type);
                found = true;
            }
        }
    }
    if (found) {
        generation_++;
    }
}

void GameProvider::finishPendingWork()
{
    IndexPtr idx = index();
    if (!idx) {
        return;
    }
    while (!wantedThumbnails_.empty()) {
        auto [collection, name] = wantedThumbnails_.back();
        wantedThumbnails_.pop_back();
        thumbnails_[collection + "/" + name] = thumbnail(*idx, collection, name);
    }
    if (isGameForm(current_.form)) {
        prepareMedia(*idx, current_);
        for (const std::string& type : std::vector<std::string>(media_.types)) {
            slot(current_, type);
        }
    }
}

// --- get / set --------------------------------------------------------------------------

std::string GameProvider::get(const std::string& key, const Screen& screen)
{
    if (key == "status") return searchStatus();
    if (key == "query") return query_;
    if (key == "filter") return filter_;
    if (key == "onlyNoMetadata") return yesNo(onlyNoMetadata_);

    if (startsWith(key, "meta:") || startsWith(key, "metaStatus:") || startsWith(key, "isChanged:")
        || key == "isMetadataDirty" || key == "metadataNote" || key == "hasOverrides") {
        return metadataValue(key, screen);
    }
    if (key == "launcher" || key == "systemLauncher" || key == "gameLauncher" || key == "launcherCommand"
        || key == "launcherProblem" || key == "unresolvedNote" || key == "isLauncherDirty" || key == "hasLauncherOverride") {
        return launcherValue(key, screen);
    }
    if (startsWith(key, "slot:") || key == "mediaNote") {
        return slotValue(key, screen);
    }
    if (startsWith(key, "story:") || key == "isStoryDirty") {
        return storyValue(key, screen);
    }
    if (key == "isFavourite" || key == "canFavourite" || key == "favouriteNote" || key == "playlist"
        || key == "playlistStatus" || key == "isInPlaylist" || key == "hasPlaylists" || key == "merged"
        || key == "mergedStatus" || key == "isInMerged" || key == "hasMerged") {
        return playlistValue(key, screen);
    }
    return detail(key, screen);
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
    else if (startsWith(key, "meta:")) {
        if (IndexPtr idx = index()) {
            loadMetadata(*idx, screen);
            metadata_.set(key.substr(5), value);
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
        loadStory(screen);
        story_.set("text", value);
    }
    else if (key == "playlist") {
        playlist_ = value;
    }
    else if (key == "merged") {
        merged_ = value;
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
    if (!services_.library) {
        return "The library isn't available.";
    }
    Library::Indexer::Status status = services_.library->status();
    if (index()) {
        return "";
    }
    if (status.state == Library::Indexer::State::Failed) {
        return "Your games couldn't be read: " + status.error;
    }
    int percent = static_cast<int>(status.progress * 100);
    return "Reading your games" + (status.phase.empty() ? std::string() : ": " + status.phase)
        + " (" + std::to_string(percent) + "%)...";
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
        else {
            wantedThumbnails_.emplace_back(system, game.name);
        }
        rows.items.push_back(std::move(row));
    }
    // update() takes them from the back: the top rows first
    std::reverse(wantedThumbnails_.begin(), wantedThumbnails_.end());
    return rows;
}

Forms::Rows GameProvider::rows(const std::string& key, size_t limit, const Screen& screen)
{
    if (key == "search") {
        return searchRows(limit);
    }
    Forms::Rows rows;
    if (key != "media") {
        return rows;
    }
    IndexPtr idx = index();
    if (!idx || !findGame(*idx, screen)) {
        return rows;
    }
    prepareMedia(*idx, screen);
    rows.total = media_.types.size();
    for (const std::string& type : media_.types) {
        if (rows.items.size() >= limit) {
            break;
        }
        Forms::Row row;
        row.id = type;
        row.label = mediaLabel(type);
        row.params["collection"] = screen.param("collection");
        row.params["id"] = screen.param("id");
        auto found = media_.slots.find(type);
        if (found == media_.slots.end()) {
            row.detail = "Checking...";
        }
        else {
            const MediaSlot& slot = found->second;
            row.detail = slot.own ? "Own file" : !slot.file.empty() ? "Fallback: " + slot.fallback : "Missing";
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
        Library::LauncherResolution system = idx->resolveLauncher(game->collection);
        result.emplace_back("", "Use the system's launcher" + (system.name.empty() ? std::string() : " (" + system.name + ")"));
        for (const Library::LauncherDef& def : idx->launchers) {
            if (def.scope == Library::LauncherScope::Global) {
                result.emplace_back(def.id, def.id);
            }
            else if (def.scope == Library::LauncherScope::Local && def.collection == own.name) {
                // A game's override names a local launcher by its own name
                result.emplace_back(def.name, def.name + " (" + own.name + " only)");
            }
            else if (def.scope == Library::LauncherScope::Collection && def.collection == own.name) {
                result.emplace_back(def.id, def.id + " (the system's launcher file)");
            }
        }
        if (IndexPtr i = idx) {
            loadLauncher(*i, screen);
        }
        std::string picked = launcher_.get("launcher");
        bool listed = std::any_of(result.begin(), result.end(), [&](const auto& option) { return option.first == picked; });
        if (!listed) {
            result.emplace_back(picked, picked + " (not installed)");
        }
    }
    else if (key == "playlists") {
        // Playlists of the pages that show the game: its own system and the merged collections showing it
        std::vector<int> pages = { game->collection };
        for (int m : idx->mergedCollectionsShowing(g)) {
            pages.push_back(m);
        }
        for (int page : pages) {
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

// --- Details ----------------------------------------------------------------------------

bool GameProvider::isFavourite(const Index& index, int game) const
{
    const GameRecord& record = index.games[static_cast<size_t>(game)];
    for (const Index::PlaylistRef& ref : index.playlistsListing(game)) {
        if (ref.collection == record.collection && Library::iequals(ref.playlist, "favorites")) {
            return true;
        }
    }
    return false;
}

std::string GameProvider::favouritesProblem(const Index& index, const GameRecord& game) const
{
    (void)game;
    bool global = false;
    if (index.properties.getBool("globalFavLast", global) && global) {
        return "Favourites are kept in the Favorites collection (globalFavLast), which Manage doesn't change yet.";
    }
    return "";
}

std::string GameProvider::playlistsSummary(const Index& index, int game) const
{
    std::vector<std::string> parts;
    if (isFavourite(index, game)) {
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
    if (!idx || !findGame(*idx, screen)) {
        return "";
    }
    prepareMedia(*idx, screen);
    size_t own = 0;
    for (const auto& [type, slot] : media_.slots) {
        own += slot.own ? 1 : 0;
    }
    if (media_.slots.size() < media_.types.size()) {
        return "Checking...";
    }
    return std::to_string(own) + " of " + std::to_string(media_.types.size());
}

std::string GameProvider::detail(const std::string& key, const Screen& screen)
{
    IndexPtr idx = index();
    int g = -1;
    const GameRecord* game = idx ? findGame(*idx, screen, &g) : nullptr;

    if (key == "isFound") return yesNo(game != nullptr);
    if (key == "notFound") {
        if (!idx) return searchStatus();
        return game ? "" : "This game isn't in the library any more: it may have been removed or renamed. Go back and search again.";
    }
    if (key == "title" || key == "screenTitle") {
        std::string title = game ? (game->title.empty() ? game->name : game->title) : screen.param("id");
        if (key == "title" || screen.form == "game") return title;
        static const std::map<std::string, std::string> sections = {
            { "game-metadata", "Metadata" }, { "game-launcher", "Launcher" }, { "game-media", "Media" },
            { "game-playlists", "Playlists and collections" }, { "game-description", "Description" },
        };
        auto section = sections.find(screen.form);
        if (screen.form == "game-media-slot") return title + " - " + mediaLabel(screen.param("type"));
        return section == sections.end() ? title : title + " - " + section->second;
    }
    if (!game) {
        return "";
    }
    const Library::CollectionRecord& own = idx->collections[static_cast<size_t>(game->collection)];
    std::string key2 = gameKey(screen);
    if (detailsKey_ != key2) {
        // Once per game: its file size and its metadata changes (files read)
        detailsKey_ = key2;
        std::error_code ec;
        uintmax_t bytes = game->file.empty() ? 0 : fs::file_size(game->file, ec);
        size_ = game->file.empty() || ec ? "" : describeSize(bytes);
        overrides_ = services_.editor ? services_.editor->metadataOverrides(*idx, own.name, game->name)
                                      : std::map<std::string, std::string>();
    }

    if (key == "system") return own.title != own.name ? own.name + " (" + own.title + ")" : own.name;
    if (key == "from") return own.name;
    if (key == "file") return game->missing || game->file.empty() ? "Missing: listed, but there's no file" : fileName(game->file);
    if (key == "folder") return game->directory;
    if (key == "size") return size_;
    if (key == "year") return game->meta.year;
    if (key == "manufacturer") return game->meta.manufacturer;
    if (key == "developer") return game->meta.developer;
    if (key == "genre") return game->meta.genre;
    if (key == "players") return game->meta.players;
    if (key == "favourite") return isFavourite(*idx, g) ? "Yes" : "";
    if (key == "favouriteText") return isFavourite(*idx, g) ? "Yes" : "No";
    if (key == "shownIn") {
        std::vector<std::string> names;
        for (int m : idx->mergedCollectionsShowing(g)) {
            names.push_back(idx->collections[static_cast<size_t>(m)].name);
        }
        return join(names);
    }
    if (key == "image") {
        // The logo, else the front art: the game's own or a fallback by its
        // title or clone, but not the collection's "default" picture
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
        return game->hasMetadata ? "From " + own.metadataType : "None";
    }
    if (key == "launcherStatus") {
        Library::LauncherResolution r = idx->resolveLauncher(game->collection, game->name);
        if (r.launcher < 0) return "Won't start";
        return r.name + (r.via == Library::LauncherVia::GameOverride ? " (its own)" : "");
    }
    if (key == "mediaStatus") return mediaSummary(screen);
    if (key == "playlistsStatus") return playlistsSummary(*idx, g);
    if (key == "descriptionStatus") {
        const MediaSlot* story = slot(screen, "story");
        if (!story || story->file.empty()) return "None";
        return story->own ? "Yes" : "From " + story->fallback;
    }
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
    const GameRecord* game = findGame(index, screen);
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
    int g = -1;
    if (!idx || type.empty() || !findGame(*idx, screen, &g)) {
        return nullptr;
    }
    prepareMedia(*idx, screen);
    auto found = media_.slots.find(type);
    if (found == media_.slots.end()) {
        // File checks: once per slot, then kept until the slot is changed here
        found = media_.slots.emplace(type, Library::gameMedia(*idx, g, type, Library::defaultKind(type))).first;
    }
    return &found->second;
}

void GameProvider::forgetSlot(const std::string& type)
{
    media_.slots.erase(type);
    generation_++;
}

std::string GameProvider::slotStatus(const MediaSlot& slot) const
{
    if (slot.own) return "Its own file";
    if (!slot.file.empty()) return "A fallback from the " + slot.fallback;
    return "Missing";
}

std::vector<std::string> GameProvider::originals(const MediaSlot& slot) const
{
    // The slot's own files that Manage changed: each extension it could have
    std::vector<std::string> files;
    if (!services_.editor || slot.directory.empty() || slot.basename.empty()) {
        return files;
    }
    for (const std::string& extension : Library::acceptedExtensions(slot.kind)) {
        std::string file = Utils::combinePath(slot.directory, slot.basename + "." + extension);
        if (services_.editor->backups().hasOriginal(file)) {
            files.push_back(file);
        }
    }
    return files;
}

std::string GameProvider::slotValue(const std::string& key, const Screen& screen)
{
    if (key == "mediaNote") {
        IndexPtr idx = index();
        if (!idx) return searchStatus();
        return "Each kind of media, as RetroFE finds it for this game. A fallback is another file RetroFE shows "
               "instead (by the game's title, its parent's name, or the collection's default); replacing puts a "
               "file of the game's own in its place.";
    }
    if (key == "slot:newFile") return newFile_;
    const MediaSlot* s = slot(screen, screen.param("type"));
    if (key == "slot:title") return mediaLabel(screen.param("type"));
    if (!s) return key == "slot:status" ? "This game isn't in the library any more." : "";
    if (key == "slot:image") return s->kind == MediaKind::Image ? s->file : "";
    if (key == "slot:file") return s->file;
    if (key == "slot:status") return slotStatus(*s);
    if (key == "slot:place") return Utils::combinePath(s->directory, s->basename) + ".*";
    if (key == "slot:isImage") return yesNo(s->kind == MediaKind::Image);
    if (key == "slot:isVideo") return yesNo(s->kind == MediaKind::Video);
    if (key == "slot:isText") return yesNo(s->kind == MediaKind::Text);
    if (key == "slot:canClear") return yesNo(s->own);
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
        result.message = "This game isn't in the library any more.";
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
    else {
        std::vector<std::string> files = originals(copy);
        edit.ok = !files.empty();
        std::string label = mediaLabel(type);
        if (!label.empty() && label != type) {
            label[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(label[0])));
        }
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
    if (edit.ok) {
        // Back to the list: pictures are cached by path, and the slot's file
        // has the same path as before, so a fresh screen shows the new one
        result.back = true;
        newFile_.clear();
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
    const GameRecord* game = findGame(index, screen);
    if (!game) {
        return;
    }
    const Library::CollectionRecord& own = index.collections[static_cast<size_t>(game->collection)];
    metadataSaved_.clear();
    for (const std::string& field : editedFields()) {
        metadataSaved_[field] = currentValue(*game, field);
    }
    metadata_.load(key, metadataSaved_);
    metadataType_ = own.metadataType;
    overrides_ = services_.editor ? services_.editor->metadataOverrides(index, own.name, game->name)
                                  : std::map<std::string, std::string>();
    detailsKey_ = "";   // the details screen re-reads them too
    // Parses the shipped hyperlist files for this game: once per screen
    shipped_.clear();
    Library::MetadataOverrides::Shipped found =
        Library::MetadataOverrides::shipped(index.options.root, own.metadataType, game->name, shipped_);
    shippedKnown_ = found != Library::MetadataOverrides::Shipped::Unknown;
    if (found == Library::MetadataOverrides::Shipped::NotFound) {
        shipped_.clear();
    }
}

std::string GameProvider::metadataValue(const std::string& key, const Screen& screen)
{
    IndexPtr idx = index();
    const GameRecord* game = idx ? findGame(*idx, screen) : nullptr;
    if (!game) {
        return key == "isMetadataDirty" || key == "hasOverrides" || startsWith(key, "isChanged:") ? "no" : "";
    }
    loadMetadata(*idx, screen);
    if (key == "isMetadataDirty") return yesNo(metadata_.isDirty());
    if (key == "hasOverrides") return yesNo(!overrides_.empty());
    if (key == "metadataNote") {
        std::string note = "Changes are kept in manage/metadata/" + metadataType_
                         + ".xml, so content updates don't undo them. RetroFE shows them after it reloads.";
        return game->hasMetadata ? note : "This game has no metadata yet; saving a field adds it. " + note;
    }
    if (startsWith(key, "meta:")) return metadata_.get(key.substr(5));
    if (startsWith(key, "isChanged:")) return yesNo(overrides_.count(key.substr(10)) > 0);
    if (startsWith(key, "metaStatus:")) {
        std::string field = key.substr(11);
        if (metadata_.changes().count(field)) return "Not saved yet.";
        if (!overrides_.count(field)) return "";
        if (!shippedKnown_) return "Changed here.";
        auto shipped = shipped_.find(field);
        std::string value = shipped == shipped_.end() ? "" : shipped->second;
        return "Changed here. Shipped: " + (value.empty() ? std::string("nothing") : value);
    }
    return "";
}

Forms::Model::Result GameProvider::metadataAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    const GameRecord* game = findGame(*idx, screen);
    const std::string own = idx->collections[static_cast<size_t>(game->collection)].name;
    const std::string name = game->name;
    loadMetadata(*idx, screen);

    if (action == "resetMetadata") {
        metadata_.reset();
        result.ok = true;
        return result;
    }

    std::map<std::string, std::string> saved = metadataSaved_;
    std::map<std::string, std::string> pending = metadata_.changes();
    std::vector<std::string> done;
    std::string failure;
    std::string lastMessage;

    if (startsWith(action, "unset.")) {
        std::string field = action.substr(6);
        Library::EditResult edit = services_.editor->clearMetadata(*idx, own, name, field);
        if (!edit.ok) {
            result.message = edit.message;
            return result;
        }
        // The field shows the shipped value again; other unsaved edits stay
        std::string back = shipped_.count(field) ? shipped_[field] : "";
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
            auto shipped = shipped_.find(field);
            bool isShipped = shippedKnown_ && overrides_.count(field) && shipped != shipped_.end() && shipped->second == value;
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
    if (key == "launcherProblem") return now.problem;
    if (key == "unresolvedNote") {
        // Names this game or its system asks for that no launcher file defines
        std::vector<std::string> asked = { system.configured, now.overrideName };
        asked.insert(asked.end(), system.alternatives.begin(), system.alternatives.end());
        std::vector<std::string> missing;
        for (const std::string& name : asked) {
            bool unresolved = std::find(idx->unresolvedLaunchers.begin(), idx->unresolvedLaunchers.end(), name)
                              != idx->unresolvedLaunchers.end();
            if (!name.empty() && unresolved && std::find(missing.begin(), missing.end(), name) == missing.end()) {
                missing.push_back(name);
            }
        }
        if (missing.empty()) return "";
        return "Not installed: " + join(missing) + ". There's no launcher file with "
            + (missing.size() == 1 ? "that name" : "those names") + "; pick an installed launcher.";
    }
    return "";
}

Forms::Model::Result GameProvider::launcherAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    const GameRecord* game = findGame(*idx, screen);
    std::string own = idx->collections[static_cast<size_t>(game->collection)].name;
    loadLauncher(*idx, screen);
    if (action == "resetLauncher") {
        launcher_.reset();
        result.ok = true;
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
            || key == "favouriteNote" ? "" : "no";
    }
    if (key == "isFavourite") return yesNo(isFavourite(*idx, g));
    if (key == "canFavourite") return yesNo(favouritesProblem(*idx, *game).empty());
    if (key == "favouriteNote") {
        std::string problem = favouritesProblem(*idx, *game);
        if (!problem.empty()) return problem;
        return "Favourites of " + idx->collections[static_cast<size_t>(game->collection)].name
            + " (playlists/favorites.txt). RetroFE also changes this list when you add a favourite while playing.";
    }

    if (key == "playlist" || key == "playlistStatus" || key == "isInPlaylist" || key == "hasPlaylists") {
        std::vector<std::pair<std::string, std::string>> choices = options("playlists", screen);
        if (key == "hasPlaylists") return yesNo(!choices.empty());
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
    const std::string& own = idx->collections[static_cast<size_t>(game->collection)].name;
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
    const GameRecord* game = findGame(*idx, screen);
    std::string own = idx->collections[static_cast<size_t>(game->collection)].name;
    std::string name = game->name;
    Library::EditResult edit;
    if (action == "addFavourite") {
        edit = services_.editor->addToPlaylist(*idx, own, "favorites", own, name);
        if (edit.ok) edit.message = "Added " + name + " to the favourites of " + own + ".";
    }
    else if (action == "removeFavourite") {
        edit = services_.editor->removeFromPlaylist(*idx, own, "favorites", own, name);
        if (edit.ok) edit.message = "Removed " + name + " from the favourites of " + own + ".";
    }
    else if (action == "addToPlaylist" || action == "removeFromPlaylist") {
        std::string chosen = playlistValue("playlist", screen);
        size_t slash = chosen.find('/');
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
    if (edit.ok && !edit.files.empty()) {
        changed("game playlists");
    }
    return result;
}

// --- Story text -------------------------------------------------------------------------

std::string GameProvider::storyToLine(const std::string& text)
{
    // One line per paragraph, joined with " | "; a final line break isn't shown
    Library::Lines lines = Library::Lines::parse(text);
    std::vector<std::string> parts;
    for (const Library::Line& line : lines.lines) {
        parts.push_back(line.text);
    }
    while (!parts.empty() && parts.back().empty() && lines.lines.size() > 1) {
        parts.pop_back();
        lines.lines.pop_back();
    }
    return join(parts, " | ");
}

std::string GameProvider::lineToStory(const std::string& line, const std::string& original)
{
    // Back to lines with the original file's line endings ("\r\n" if it used them)
    Library::Lines lines = Library::Lines::parse(original);
    std::string newline = original.empty() ? Library::defaultNewline() : lines.newline();
    bool finalBreak = original.empty() || (!lines.lines.empty() && !lines.lines.back().eol.empty())
                      || (lines.lines.size() > 1 && lines.lines.back().text.empty());
    std::string text;
    size_t start = 0;
    while (true) {
        size_t bar = line.find('|', start);
        std::string part = Utils::trimEnds(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        text += part;
        if (bar == std::string::npos) {
            break;
        }
        text += newline;
        start = bar + 1;
    }
    return finalBreak ? text + newline : text;
}

void GameProvider::loadStory(const Screen& screen)
{
    std::string key = gameKey(screen);
    if (story_.id() == key) {
        return;
    }
    const MediaSlot* s = slot(screen, "story");
    if (!s) {
        return;
    }
    storyOriginal_.clear();
    if (!s->file.empty()) {
        Library::readFile(s->file, storyOriginal_);
    }
    story_.load(key, { { "text", storyToLine(storyOriginal_) } });
}

std::string GameProvider::storyValue(const std::string& key, const Screen& screen)
{
    const MediaSlot* s = slot(screen, "story");
    if (!s) {
        return startsWith(key, "is") || startsWith(key, "story:can") || startsWith(key, "story:has") ? "no" : "";
    }
    loadStory(screen);
    if (key == "story:text") return story_.get("text");
    if (key == "isStoryDirty") return yesNo(story_.isDirty());
    if (key == "story:canClear") return yesNo(s->own);
    if (key == "story:hasOriginal") return yesNo(!originals(*s).empty());
    if (key == "story:status") {
        if (s->own) return "Its own text: " + s->file;
        if (!s->file.empty()) return "A fallback from the " + s->fallback + ": " + s->file
            + ". Saving writes the game's own text instead.";
        return "None yet. Saving writes " + Utils::combinePath(s->directory, s->basename) + ".txt.";
    }
    if (key == "story:preview") {
        return preview(storyOriginal_);
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
        return result;
    }
    loadStory(screen);
    MediaSlot copy = *slot(screen, "story");
    Library::EditResult edit;
    if (action == "saveStory") {
        edit = services_.editor->writeMediaText(copy, lineToStory(story_.get("text"), storyOriginal_));
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
        forgetSlot("story");
        story_.load("", {});
        loadStory(screen);
        changed("game story");
    }
    return result;
}

// --- Actions ----------------------------------------------------------------------------

std::string GameProvider::problem(const std::string& action, const Screen& screen)
{
    static const std::vector<std::string> known = {
        "saveMetadata", "resetMetadata", "saveLauncher", "resetLauncher", "useSystemLauncher",
        "replaceMedia", "clearMedia", "restoreMedia", "addFavourite", "removeFavourite", "addToPlaylist",
        "removeFromPlaylist", "addToMerged", "removeFromMerged", "saveStory", "resetStory", "clearStory",
        "restoreStory", "replaceStory",
    };
    bool isUnset = startsWith(action, "unset.");
    if (!isUnset && std::find(known.begin(), known.end(), action) == known.end()) {
        return Provider::actionProblem(action, screen);
    }
    IndexPtr idx = index();
    if (!idx) return "Your games are still being read.";
    if (!services_.editor) return "Changes can't be made here.";
    int g = -1;
    const GameRecord* game = findGame(*idx, screen, &g);
    if (!game) return "This game isn't in the library any more.";

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
        if (!s) return "Unknown media slot.";
        if (action == "replaceMedia") return Utils::trimEnds(newFile_).empty() ? "Choose a file first." : "";
        if (action == "clearMedia") {
            if (s->own) return "";
            return s->file.empty() ? "There's nothing to clear." : "This shows a fallback from the " + s->fallback
                + "; there's no file of the game's own to clear.";
        }
        return originals(*s).empty() ? "There's no original kept for this slot." : "";
    }
    if (action == "addFavourite" || action == "removeFavourite") {
        std::string why = favouritesProblem(*idx, *game);
        if (!why.empty()) return why;
        bool favourite = isFavourite(*idx, g);
        if (action == "addFavourite") return favourite ? "It's a favourite already." : "";
        return favourite ? "" : "It isn't a favourite.";
    }
    if (action == "addToPlaylist" || action == "removeFromPlaylist") {
        if (playlistValue("playlist", screen).empty()) return "There are no playlists to choose from.";
        bool in = playlistValue("isInPlaylist", screen) == "yes";
        if (action == "addToPlaylist") return in ? "It's in this playlist already." : "";
        return in ? "" : "It isn't in this playlist.";
    }
    if (action == "addToMerged" || action == "removeFromMerged") {
        if (playlistValue("merged", screen).empty()) return "There are no merged collections.";
        bool in = playlistValue("isInMerged", screen) == "yes";
        if (action == "addToMerged") return in ? "It's shown there already." : "";
        return in ? "" : "It isn't shown there.";
    }
    if (action == "saveStory" || action == "resetStory") {
        loadStory(screen);
        return story_.isDirty() ? "" : "No changes to save.";
    }
    if (action == "clearStory" || action == "restoreStory") {
        const MediaSlot* s = slot(screen, "story");
        if (action == "clearStory") {
            if (!s || s->own) return "";
            return s->file.empty() ? "There's no story text to clear." : "This shows a fallback from the " + s->fallback
                + "; there's no text of the game's own to clear.";
        }
        return !s || originals(*s).empty() ? "There's no original kept for the story." : "";
    }
    return "";
}

std::string GameProvider::actionProblem(const std::string& action, const Screen& screen)
{
    return problem(action, screen);
}

Forms::Model::Result GameProvider::runAction(const std::string& action, const Screen& screen)
{
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
    if (action == "replaceMedia" || action == "clearMedia" || action == "restoreMedia") {
        return mediaAction(action, screen);
    }
    if (action == "saveStory" || action == "resetStory" || action == "clearStory" || action == "restoreStory"
        || action == "replaceStory") {
        return storyAction(action, screen);
    }
    return playlistAction(action, screen);
}

}

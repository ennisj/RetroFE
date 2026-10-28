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

#include "../EditBuffer.h"
#include "../Provider.h"
#include "../../Library/Index.h"
#include "../../Library/Media.h"
#include "../../Library/Search.h"
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The Games screens (GamesForms.cpp): find any installed game, and look at or
// change one game's metadata, launcher, media, playlists and story text.
//
// Screens and their parameters:
//   games                      search; param collection = the initial filter
//   games-filters              the search's filters (the Filters link on games)
//   game                       collection + id: the game (id) of its own system (collection),
//                              or a hidden game of that name (metadata, media and story only)
//   game-metadata, game-launcher, game-media, game-playlists, game-description
//                              the same parameters
//   game-media-slot            collection + id + type: one media slot (logo, video, story...)
//
// Bindings (game:<key>), actions (game.<name>) and option sources are listed
// in GameProvider.cpp. Everything reads the shared library index and writes
// through the shared Library::Editor. Media file checks (slots, search
// thumbnails) run on a worker and arrive through update().
namespace Manager
{
    class GameProvider : public Provider
    {
    public:
        using IndexPtr = std::shared_ptr<const Library::Index>;

        explicit GameProvider(const Services& services);
        ~GameProvider() override;

        std::string prefix() const override { return "game"; }

        void refresh() override;
        void enter(const Screen& screen) override;
        std::string leaving(const Screen& screen) override;
        void discard(const Screen& screen) override;
        void update(float dt) override;
        unsigned generation() const override { return generation_; }

        std::string get(const std::string& key, const Screen& screen) override;
        // Warnings: a launcher that won't start, a missing file or media slot
        Forms::Severity severity(const std::string& key, const Screen& screen) override;
        void set(const std::string& key, const std::string& value, const Screen& screen) override;
        std::vector<std::string> list(const std::string& key, const Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Screen& screen) override;
        std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Screen& screen) override;
        std::string actionProblem(const std::string& action, const Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Screen& screen) override;

        // Media slot types in the order the Media screen lists them
        static std::vector<std::string> orderedMediaTypes(const std::vector<std::string>& types);
        // "Front art" for artwork_front, ...
        static std::string mediaLabel(const std::string& type);

        // The story text as the multi-line field shows it (lines joined with
        // "\n") and back. Lines keep the ending the original's line in their
        // place had (new ones get the file's own) and the final line break is
        // kept as it was, so lines that weren't changed keep their exact bytes.
        static std::string storyToText(const std::string& story);
        static std::string textToStory(const std::string& text, const std::string& original);

        // collections/playCount.txt ("<key>;<count>;<epoch>") and timeSpent.txt
        // ("<key>;<seconds>"), key "_<system>:<game>" (playCount.txt also has
        // plain "<game>" lines from older versions), read as RetroFE does
        struct PlayStats
        {
            int count = 0;
            std::string lastPlayed;    // seconds since 1970, as written
            double seconds = 0;
        };
        static PlayStats readPlayStats(const std::string& root, const std::string& system, const std::string& game);
        static std::string describePlays(const PlayStats& stats);   // "3 times, last on 2026-10-01 14:03"
        // Why RetroFE doesn't count a system's plays, from lastPlayedSkipCollection
        // and lastplayedSize as RetroFE reads them; "" when it does. Shown in
        // place of "Never" for a game with no plays.
        static std::string playsNotCounted(const std::string& skipCollections, const std::string& lastPlayedSize,
                                           const std::string& system);
        static std::string describeTime(double seconds);             // "1 h 12 min"

        // Tests: waits for the media worker and does whatever it still has to do
        void finishPendingWork();
        // Tests: where the index comes from (default: the shared library's)
        void setIndexSource(std::function<IndexPtr()> source) { indexSource_ = std::move(source); }

    private:
        IndexPtr index() const;
        static std::string gameKey(const Screen& screen);
        // The game a screen is about (its own collection + name), or nullptr
        const Library::GameRecord* findGame(const Library::Index& index, const Screen& screen, int* position = nullptr) const;
        // That game, or the hidden game of that name: what the screens that
        // work for hidden games too (metadata, media, story) are about
        const Library::GameRecord* findRecord(const Library::Index& index, const Screen& screen) const;

        // --- hidden games: files of a system its exclude list keeps RetroFE from
        // listing (Library::Index::hiddenGames, with their metadata)
        using HiddenGame = Library::GameRecord;
        const HiddenGame* findHidden(const IndexPtr& index, const Screen& screen) const;
        std::vector<char> systemsUnder(const Library::Index& index, const std::string& filter) const;
        Forms::Rows hiddenRows(size_t limit);
        std::string hiddenValue(const std::string& key, const Screen& screen);
        Forms::Model::Result hideAction(const std::string& action, const Screen& screen);

        // --- search
        std::string searchStatus() const;
        std::string filterSummary(bool withCount);
        Forms::Rows searchRows(size_t limit);
        static std::string thumbnail(const Library::Index& index, const std::string& collection, const std::string& name);

        // --- details
        std::string detail(const std::string& key, const Screen& screen);
        std::string mediaSummary(const Screen& screen);
        std::string playlistsSummary(const Library::Index& index, int game);

        // --- favourites: RetroFE keeps them per page (the collection a game is shown in)
        bool globalFavourites(const Library::Index& index) const;
        std::vector<int> pages(const Library::Index& index, int game) const;      // own system, then merged collections showing it
        bool isFavourite(const Library::Index& index, int game, int page) const;
        bool inGlobalFavourites(const Library::Index& index, const Library::GameRecord& game);
        std::vector<std::string> favouritePages(const Library::Index& index, int game);
        int chosenPage(const Library::Index& index, int game) const;

        // --- media: slots are looked up on a worker, once per game
        struct MediaCache
        {
            std::string key;                                   // collection/id
            std::vector<std::string> types;
            std::map<std::string, Library::MediaSlot> slots;   // by type, once looked up
            // What Put back would restore, by type: worked out once per slot, not per frame
            std::map<std::string, std::vector<std::string>> originals;
        };
        struct MediaJob
        {
            std::string key;
            unsigned epoch = 0;
            std::future<std::map<std::string, Library::MediaSlot>> result;
        };
        struct ThumbnailJob
        {
            std::future<std::vector<std::pair<std::string, std::string>>> result;   // key -> file
        };
        void prepareMedia(const Library::Index& index, const Screen& screen);
        // The slot when it has been looked up, else nullptr (the worker is asked for it)
        const Library::MediaSlot* slot(const Screen& screen, const std::string& type);
        void forgetSlot(const std::string& type);
        void startMediaJob();
        void startThumbnailJob();
        bool collectJobs(bool wait);
        std::string slotStatus(const Library::MediaSlot& slot) const;
        Forms::Rows mediaRows(const Screen& screen, int which, size_t limit);   // which: 0 every slot, 1 shown, 2 missing
        std::string slotValue(const std::string& key, const Screen& screen);
        // The slot's files Manage changed (its own file's, its media folder's), cached
        const std::vector<std::string>& originals(const Library::MediaSlot& slot);
        Forms::Model::Result mediaAction(const std::string& action, const Screen& screen);

        // --- metadata
        void loadMetadata(const Library::Index& index, const Screen& screen);
        const std::map<std::string, std::string>& shipped(const Library::Index& index, const Screen& screen, bool& known);
        std::string metadataValue(const std::string& key, const Screen& screen);
        Forms::Model::Result metadataAction(const std::string& action, const Screen& screen);
        // Genre, players and rating: the values the system's games have, then "Other..."
        std::vector<std::pair<std::string, std::string>> metadataChoices(const std::string& field, const Screen& screen);

        // --- launcher
        void loadLauncher(const Library::Index& index, const Screen& screen);
        std::string launcherValue(const std::string& key, const Screen& screen);
        Forms::Model::Result launcherAction(const std::string& action, const Screen& screen);

        // --- playlists and merged collections
        std::string playlistValue(const std::string& key, const Screen& screen);
        Forms::Model::Result playlistAction(const std::string& action, const Screen& screen);

        // --- story text
        bool loadStory(const Screen& screen);
        std::string storyValue(const std::string& key, const Screen& screen);
        Forms::Model::Result storyAction(const std::string& action, const Screen& screen);

        void changed(const std::string& what);
        std::string problem(const std::string& action, const Screen& screen);

        Services services_;
        std::function<IndexPtr()> indexSource_;
        // While an action is checked and run, every lookup uses this one
        // snapshot, so a new index published meanwhile can't pull the game away
        IndexPtr pinned_;
        IndexPtr favouritesIndex_;
        std::map<std::string, bool> pendingFavourites_;
        Screen current_;
        unsigned generation_ = 0;
        unsigned indexGeneration_ = 0;

        // Search
        std::string query_;
        std::string filter_;
        std::string filterParam_ = "\x01";   // the param:collection the filter was last set from
        bool onlyNoMetadata_ = false;
        bool onlyHidden_ = false;
        IndexPtr hiddenIndex_;                             // keeps hiddenMatches_'s games alive
        std::string hiddenSearchKey_ = "\x01";             // query + filter the rows below are for
        std::vector<const HiddenGame*> hiddenMatches_;
        Library::Search search_;
        IndexPtr filtersIndex_;
        std::vector<std::pair<std::string, std::string>> filters_;
        std::map<std::string, std::string> thumbnails_;    // collection/name -> file ("" = none)
        std::vector<std::pair<std::string, std::string>> wantedThumbnails_;   // (collection, name) still to look up
        std::set<std::string> thumbnailsAsked_;            // being looked up now

        // Details
        std::string detailsKey_;
        std::string size_;
        std::string played_;                               // play count and last played
        std::string timePlayed_;
        std::map<std::string, std::string> overrides_;     // this game's metadata changes (element -> value)
        std::string globalFavouritesKey_;
        bool inGlobalFavourites_ = false;
        MediaCache media_;
        std::string priorityType_;                         // looked up first (the slot screen's)
        std::string newFile_;                              // the slot screen's "Replace from a file"

        // Workers (at most one of each at a time)
        MediaJob mediaJob_;
        ThumbnailJob thumbnailJob_;
        unsigned mediaEpoch_ = 0;                          // bumped when slots are forgotten: older lookups are dropped

        // Metadata
        EditBuffer metadata_;
        std::map<std::string, std::string> metadataSaved_;
        std::map<std::string, std::string> pendingMetadata_;   // typed before the library was read
        std::string metadataType_;
        // Shipped values, read from the hyperlist files only when a field has
        // been changed here, and kept per metadata type and game
        struct Shipped
        {
            bool known = false;
            std::map<std::string, std::string> values;
        };
        std::map<std::string, Shipped> shippedCache_;      // "<type>\n<name>"
        // The values a system's games have for a field, by "<collection>\n<field>", for one index
        IndexPtr choicesIndex_;
        std::map<std::string, std::vector<std::string>> choices_;
        std::set<std::string> typing_;                     // fields whose choice is on "Other...": typed instead

        // Launcher
        EditBuffer launcher_;
        std::string launchCheckKey_;                       // game + snapshot the program was checked for
        std::string launchCheck_;                          // why it won't start, or ""

        // Playlists
        std::string playlist_;                             // "<collection>/<playlist>"
        std::string merged_;                               // a merged collection
        std::string favouritePage_;                        // the page whose favourites are shown
        std::string favouriteStatus_;                      // what the Favourite toggle last did, for this game
        std::string favouriteStatusKey_;

        // Story
        EditBuffer story_;
        std::string storyOriginal_;                        // the text loaded, for its bytes and line endings
    };
}

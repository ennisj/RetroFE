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
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// The Games screens (GamesForms.cpp): find any installed game, and look at or
// change one game's metadata, launcher, media, playlists and story text.
//
// Screens and their parameters:
//   games                      search; param collection = the initial filter
//   game                       collection + id: the game (id) of its own system (collection)
//   game-metadata, game-launcher, game-media, game-playlists, game-description
//                              the same parameters
//   game-media-slot            collection + id + type: one media slot (logo, video, story...)
//
// Bindings (game:<key>), actions (game.<name>) and option sources are listed
// in GameProvider.cpp. Everything reads the shared library index and writes
// through the shared Library::Editor.
namespace Manager
{
    class GameProvider : public Provider
    {
    public:
        explicit GameProvider(const Services& services);

        std::string prefix() const override { return "game"; }

        void refresh() override;
        void enter(const Screen& screen) override;
        std::string leaving(const Screen& screen) override;
        void discard(const Screen& screen) override;
        void update(float dt) override;
        unsigned generation() const override { return generation_; }

        std::string get(const std::string& key, const Screen& screen) override;
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

        // The story text as a one-line field shows it (lines joined with " | ") and back
        static std::string storyToLine(const std::string& text);
        static std::string lineToStory(const std::string& line, const std::string& original);

        // Does the media work update() would do, all at once (tests)
        void finishPendingWork();

    private:
        using IndexPtr = std::shared_ptr<const Library::Index>;

        IndexPtr index() const;
        static std::string gameKey(const Screen& screen);
        // The game a screen is about (its own collection + name), or nullptr
        const Library::GameRecord* findGame(const Library::Index& index, const Screen& screen, int* position = nullptr) const;

        // --- search
        std::string searchStatus() const;
        Forms::Rows searchRows(size_t limit);
        static std::string thumbnail(const Library::Index& index, const std::string& collection, const std::string& name);

        // --- details
        std::string detail(const std::string& key, const Screen& screen);
        std::string mediaSummary(const Screen& screen);
        std::string playlistsSummary(const Library::Index& index, int game) const;
        std::string favouritesProblem(const Library::Index& index, const Library::GameRecord& game) const;
        bool isFavourite(const Library::Index& index, int game) const;

        // --- media: slots are looked up once per game (file checks), a few per frame
        struct MediaCache
        {
            std::string key;                                   // collection/id
            std::vector<std::string> types;
            std::map<std::string, Library::MediaSlot> slots;   // by type, once looked up
        };
        void prepareMedia(const Library::Index& index, const Screen& screen);
        const Library::MediaSlot* slot(const Screen& screen, const std::string& type);
        void forgetSlot(const std::string& type);
        std::string slotStatus(const Library::MediaSlot& slot) const;
        std::string slotValue(const std::string& key, const Screen& screen);
        std::vector<std::string> originals(const Library::MediaSlot& slot) const;
        Forms::Model::Result mediaAction(const std::string& action, const Screen& screen);

        // --- metadata
        void loadMetadata(const Library::Index& index, const Screen& screen);
        std::string metadataValue(const std::string& key, const Screen& screen);
        Forms::Model::Result metadataAction(const std::string& action, const Screen& screen);

        // --- launcher
        void loadLauncher(const Library::Index& index, const Screen& screen);
        std::string launcherValue(const std::string& key, const Screen& screen);
        Forms::Model::Result launcherAction(const std::string& action, const Screen& screen);

        // --- playlists and merged collections
        std::string playlistValue(const std::string& key, const Screen& screen);
        Forms::Model::Result playlistAction(const std::string& action, const Screen& screen);

        // --- story text
        void loadStory(const Screen& screen);
        std::string storyValue(const std::string& key, const Screen& screen);
        Forms::Model::Result storyAction(const std::string& action, const Screen& screen);

        void changed(const std::string& what);
        std::string problem(const std::string& action, const Screen& screen);

        Services services_;
        Screen current_;
        unsigned generation_ = 0;
        unsigned indexGeneration_ = 0;

        // Search
        std::string query_;
        std::string filter_;
        std::string filterParam_ = "\x01";   // the param:collection the filter was last set from
        bool onlyNoMetadata_ = false;
        Library::Search search_;
        IndexPtr filtersIndex_;
        std::vector<std::pair<std::string, std::string>> filters_;
        std::map<std::string, std::string> thumbnails_;    // collection/name -> file ("" = none)
        std::vector<std::pair<std::string, std::string>> wantedThumbnails_;   // (collection, name) still to look up

        // Details
        std::string detailsKey_;
        std::string size_;
        std::map<std::string, std::string> overrides_;     // this game's metadata changes (element -> value)
        MediaCache media_;
        std::string newFile_;                              // the slot screen's "Replace from a file"

        // Metadata
        EditBuffer metadata_;
        std::map<std::string, std::string> metadataSaved_;
        std::map<std::string, std::string> shipped_;       // element -> shipped value
        bool shippedKnown_ = false;
        std::string metadataType_;

        // Launcher
        EditBuffer launcher_;

        // Playlists
        std::string playlist_;                             // "<collection>/<playlist>"
        std::string merged_;                               // a merged collection

        // Story
        EditBuffer story_;
        std::string storyOriginal_;                        // the text loaded, for its line endings
    };
}

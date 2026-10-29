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
#include <string>
#include <vector>

// The Systems and Videos screens (docs/design/manage-entities.md). One class
// serves both: registered once with prefix "system" (collections with games
// of their own) and once with prefix "video" (the same, for collections whose
// info.conf says "type = videos", decision M7). Forms are "<p>s", "<p>",
// "<p>-settings", "<p>-launcher", "<p>-info", "<p>-media", "<p>-medium",
// "<p>-playlists", "<p>-playlist", "<p>-playlist-entry", "<p>-check" and
// "<p>-add" (SystemForms.h). Screens about one system get param id (its
// folder name); playlist screens add playlist and game (and source for a
// game of another collection); the medium screen adds slot.
namespace Manager
{
    // What "Check this system" finds: plain sentences, most important first,
    // and a one-line summary. Warn-only; nothing is changed. findings are the
    // problems (counted); notes say how this fork reads a setting, with
    // nothing to fix (e.g. a OneSauce key it honours), and aren't counted.
    struct SystemCheck
    {
        std::vector<std::string> findings;
        std::vector<std::string> notes;
        std::string summary;
    };
    SystemCheck checkSystem(const Library::Index& index, int collection, bool videos);

    // "zip, .7z ,ZIP;zip" -> "zip,7z,ZIP": split on commas, semicolons and
    // spaces, dots dropped, duplicates (same case) dropped, case kept because
    // RetroFE compares it
    std::string normaliseExtensions(const std::string& text);

    // A system made here: Setup's or Manage's header in its settings.conf, and
    // nothing in its folder but settings, lists and art
    bool madeHere(const Library::CollectionRecord& record);

    // A collection Videos lists: info.conf "type = videos"
    bool isVideoCollection(const Library::CollectionRecord& record);

    // A video collection RetroFE plays itself: info.conf "play = here"
    // (VideoCollections::playsHere); otherwise its launcher plays its videos
    bool playsHere(const Library::CollectionRecord& record);

    // Its extensions RetroFE's own player can't open (VideoCollections::playableExtensions)
    std::vector<std::string> unplayableExtensions(const Library::CollectionRecord& record);

    // The launcher a new video collection gets by default
    extern const char* const videoPlayerLauncher;   // "VideoPlayer"

    class SystemProvider : public Provider
    {
    public:
        SystemProvider(const Services& services, bool videos);

        std::string prefix() const override { return base_; }
        Forms::Params saveView() const override;
        void restoreView(const Forms::Params& state) override;
        void refresh() override;
        void enter(const Screen& screen) override;
        std::string leaving(const Screen& screen) override;
        void discard(const Screen& screen) override;
        unsigned generation() const override;

        std::string get(const std::string& key, const Screen& screen) override;
        // Warnings: a launcher that isn't installed, a games folder with nothing in it
        Forms::Severity severity(const std::string& key, const Screen& screen) override;
        void set(const std::string& key, const std::string& value, const Screen& screen) override;
        void setDefault(const std::string& key, const std::string& value, const Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Screen& screen) override;
        std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Screen& screen) override;
        std::string actionProblem(const std::string& action, const Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Screen& screen) override;

    private:
        using IndexPtr = std::shared_ptr<const Library::Index>;

        // An edit buffer and the snapshot it was loaded from
        struct Buffer
        {
            EditBuffer edits;
            const Library::Index* from = nullptr;
        };

        IndexPtr index() const;
        std::string root() const;
        const Library::CollectionRecord* record(const Library::Index& index, const Screen& screen, int* position = nullptr) const;
        bool isForm(const Screen& screen, const char* suffix) const { return screen.form == base_ + suffix; }
        bool isMine(const Screen& screen) const;
        bool belongs(const Library::CollectionRecord& record) const;
        std::string status(const Screen& screen) const;
        std::string screenTitle(const Screen& screen) const;
        void changed(const std::string& what);

        // Loads buffers (on entering a screen, and when a new snapshot arrives while unchanged)
        void syncBuffers(const Screen& screen);
        std::map<std::string, std::string> readSettings(const Library::CollectionRecord& record);
        std::map<std::string, std::string> readLauncher(const Library::CollectionRecord& record);
        std::map<std::string, std::string> readInfo(const Library::CollectionRecord& record) const;
        Buffer* bufferFor(const Screen& screen);

        // Per-screen caches, rebuilt when the snapshot or the record changes
        void ensureDetails(const Screen& screen);
        void ensureMedia(const Screen& screen, bool force = false);
        void ensurePlaylist(const Screen& screen, bool force = false);
        std::string detailsValue(const std::string& key, const Screen& screen);
        std::string launcherText(const Library::Index& index, const Library::CollectionRecord& record) const;
        const Library::MediaSlot* slot(const Screen& screen);

        Forms::Rows listRows(size_t limit);
        Forms::Rows playlistRows(const Screen& screen);
        Forms::Rows entryRows(const Screen& screen);
        Forms::Rows candidateRows(const Screen& screen, size_t limit);
        std::string playlistFile(const Library::CollectionRecord& record, const std::string& name) const;
        // Why RetroFE won't show a playlist (cyclePlaylist, empty) and how it sorts it, or ""
        std::string playlistShowNote(const Library::Index& index, const Library::CollectionRecord& record,
                                     const std::string& playlist, size_t entries) const;
        static bool canAppendCycle(const Library::Index& index, const Library::CollectionRecord& record);
        bool inPlaylist(const Screen& screen);

        // Adding a system
        // The menu new systems go in (MenuFiles::menuForNewCollections reads up
        // to 17 conf files), worked out once per snapshot
        const std::string& defaultMenu(const Library::Index& index);
        static bool canListIn(const Library::CollectionRecord& menu);
        std::string addProblem();
        Forms::Model::Result create();
        std::string addFolderCount();
        std::string defaultGamesFolder();
        void suggestLauncher();
        void syncAddLaunchers();
        bool addIsNewLauncher() const;
        // A system, or a video collection added to play with an external player
        bool addUsesLauncher() const;

        std::string deleteProblem(const Screen& screen);
        Forms::Model::Result remove(const Screen& screen);

        Forms::Model::Result saveSettings(const Screen& screen);
        Forms::Model::Result saveLauncher(const Screen& screen);
        Forms::Model::Result saveInfo(const Screen& screen);
        Forms::Model::Result mediaAction(const std::string& action, const Screen& screen);
        Forms::Model::Result playlistAction(const std::string& action, const Screen& screen);

        Services services_;
        bool videos_;
        std::string base_;    // "system" / "video"
        std::string noun_;    // "system" / "video collection"
        std::string item_;    // "game" / "video"
        std::string items_;   // "games" / "videos"
        unsigned changes_ = 0;

        // systems list
        std::string query_;
        std::string menu_;
        Library::Search search_;
        IndexPtr listIndex_;
        std::string listKey_;
        std::vector<Forms::Row> listRows_;

        // details
        IndexPtr detailsIndex_;
        std::string detailsId_;
        std::string image_;
        SystemCheck check_;
        std::string launcherSource_;   // the file "launcher =" comes from
        std::string deleteProblem_;    // why it can't be deleted, or ""
        size_t ownArt_ = 0;            // art slots with a file of their own

        // buffers
        Buffer settings_;
        Buffer launcher_;
        Buffer info_;
        std::map<std::string, std::string> booleanStyle_;   // key -> "true" when the files write true/false
        std::vector<std::string> metadataTypes_;

        // media
        IndexPtr mediaIndex_;
        std::string mediaId_;
        std::vector<Library::MediaSlot> slots_;
        std::map<std::string, std::vector<std::string>> originals_;   // slot type -> files "Put back" restores
        std::string mediaFile_;

        // playlists
        std::string newPlaylist_;
        std::string playlistsKey_;         // id + generation the playlists list was read for
        std::vector<Forms::Row> playlistsRows_;
        std::string playlistKey_;          // id + playlist the entries were read for
        std::string playlistPath_;
        std::vector<std::string> entries_;
        Buffer playlistName_;
        std::string playlistQuery_;
        Library::Search candidates_;

        // adding
        IndexPtr menuIndex_;
        std::string defaultMenu_;
        std::map<std::string, std::string> add_;
        std::map<std::string, bool> addTyped_;
        IndexPtr addLauncherIndex_;
        std::vector<std::string> addLauncherIds_;
        IndexPtr suggestionIndex_;
        std::string suggestedLauncher_;
        std::string countedKey_;
        std::string countText_;
    };
}

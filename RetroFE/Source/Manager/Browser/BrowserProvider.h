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

#include "PathBrowser.h"
#include "../Provider.h"
#include "../Navigator.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <vector>

// The file and folder browser behind every <folder> and <file> field: Select
// on one opens the file-browser form (BrowserForms.cpp), driven by the
// controller. Its first screen lists starting places; each folder is a screen
// of its own over the one it was opened from, so Back goes back up and
// cancels from the first. "Use this folder" (a folder field) or a file (a file
// field) gives the field its new value, unsaved, as typing it would
// (Result::pick). The field's attributes say where to start (start=), which
// files to show (pattern=) and what to count in folders (matching=).
//
// Bindings (prefix browse): title, for, where, summary, note, emptyNote,
// query (a search within a large folder), isTop, isFolder, canUseFolder,
// canSearch, hasSystemDialog; rows places and entries. Actions: use,
// enter:<folder>, pick:<file>, up, system (the Windows dialog, offered only
// once a mouse has been used).
namespace Browser
{
    constexpr const char* formId = "file-browser";

    // Screens deeper than this replace each other rather than pile up on
    // the manager's stack (which keeps 32)
    constexpr int maxLevels = 16;

    // What a field asks for when Select is pressed on it
    struct Request
    {
        std::string bind;
        bool folder = false;
        std::string label;
        std::string filterName;
        std::string pattern;
        std::string value;      // the field's value now
        std::string start;      // the value of its start= key
        std::string matching;   // the value of its matching= key
    };

    // The parameters of the browser's first screen (the starting places)
    Forms::Params screenParams(const Request& request);

    // When the browse opens in a folder (openingFolder: the value's, else the
    // field's start), the parameters of that folder's screen, shown over the
    // first one. False when it starts at the places.
    bool openingParams(const Forms::Params& first, const std::filesystem::path& root, Forms::Params& folder);

    void replaceFolder(Navigator& nav, Forms::Params params);

    class BrowserProvider : public Manager::Provider
    {
    public:
        ~BrowserProvider() override { if (pending_) pending_->cancelled = true; }
        explicit BrowserProvider(const Manager::Services& services);

        std::string prefix() const override { return "browse"; }
        void refresh() override;
        void enter(const Manager::Screen& screen) override;
        void update(float dt) override;
        unsigned generation() const override { return generation_; }
        std::string get(const std::string& key, const Manager::Screen& screen) override;
        void set(const std::string& key, const std::string& value, const Manager::Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Manager::Screen& screen) override;
        std::string actionProblem(const std::string& action, const Manager::Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Manager::Screen& screen) override;

        // For tests: RetroFE's folder (default Configuration::absolutePath),
        // and where drives and the user's folders come from
        void setRoot(const std::string& root) { root_ = root; }
        void setPlaceSources(std::function<std::vector<Drive>()> drives, std::function<std::vector<Place>()> folders)
        {
            drives_ = std::move(drives);
            folders_ = std::move(folders);
        }
        // Counts every folder waiting to be counted now (tests)
        void countAll();

        // Folders picked from in this run of RetroFE, newest first
        static std::vector<std::string>& recent();

    private:
        std::filesystem::path root() const;
        bool isFolderKind(const Manager::Screen& screen) const { return screen.param("kind") == "folder"; }
        std::vector<std::string> shownExtensions(const Manager::Screen& screen) const;    // files listed (file fields)
        std::vector<std::string> countedExtensions(const Manager::Screen& screen) const;  // files counted in folders
        Forms::Model::Result chosen(const std::filesystem::path& path, const Manager::Screen& screen);
        struct Pending
        {
            std::mutex mutex;
            std::condition_variable ready;
            Listing listing;
            std::vector<Place> places;
            std::map<std::string, Count> counts;
            std::atomic<bool> cancelled{false};
            bool listed = false;
            bool done = false;
        };
        std::shared_ptr<Pending> pending_;
        const std::vector<size_t>& filtered();
        std::vector<size_t> filtered_;
        bool filterDirty_ = true;

        Manager::Services services_;
        std::string root_;
        std::function<std::vector<Drive>()> drives_;
        std::function<std::vector<Place>()> folders_;

        // The folder shown, listed when its screen is entered
        std::string listedKey_ = "\x01";
        std::filesystem::path dir_;
        Listing listing_;
        std::string query_;
        std::vector<Place> places_;

        // What the first folders hold, published together after the worker finishes
        std::map<std::string, Count> counts_;
        std::string countExtensions_;   // extensions used for folder counts
        unsigned generation_ = 0;
    };
}

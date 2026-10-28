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
#include "BrowserProvider.h"
#include "../../Database/Configuration.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

namespace
{
    // A large folder shows this many rows, and a search to find the rest
    constexpr size_t shownRows = 200;

    // Folders counted per screen; past that they show no count
    constexpr size_t maxCounted = 50;

    std::string yesNo(bool value) { return value ? "yes" : "no"; }

    bool startsWith(const std::string& text, const char* start, std::string& rest)
    {
        size_t length = std::char_traits<char>::length(start);
        if (text.compare(0, length, start) != 0) {
            return false;
        }
        rest = text.substr(length);
        return true;
    }

    std::string joined(const std::vector<std::string>& items)
    {
        std::string text;
        for (const std::string& item : items) {
            text += (text.empty() ? "" : ",") + item;
        }
        return text;
    }

    std::string countKey(const std::string& extensions, const fs::path& path)
    {
        return extensions + "|" + path.string();
    }

    int levelOf(const Manager::Screen& screen)
    {
        try {
            return std::stoi(screen.param("level"));
        }
        catch (...) {
            return 0;
        }
    }

    char separator()
    {
        return static_cast<char>(fs::path::preferred_separator);
    }
}

namespace Browser
{

void replaceFolder(Navigator& nav, Forms::Params params)
{
    const auto& frames = nav.frames();
    if (frames.size() > 1) {
        const auto& below = frames[frames.size() - 2];
        auto dir = below.params.find("dir");
        if (below.form == formId && dir != below.params.end()
            && insideRoot(fs::path(params["dir"]), fs::path(dir->second))
            && insideRoot(fs::path(dir->second), fs::path(params["dir"]))) {
            nav.back();
            return;
        }
    }
    // Level counts stack frames, including when deep folders replace a frame.
    if (nav.current()) {
        auto level = nav.current()->params.find("level");
        if (level != nav.current()->params.end()) params["level"] = level->second;
    }
    nav.replace(formId, params);
}

Forms::Params screenParams(const Request& request)
{
    Forms::Params params;
    params["for"] = request.bind;
    params["kind"] = request.folder ? "folder" : "file";
    params["label"] = request.label;
    params["filter"] = request.filterName;
    params["pattern"] = request.pattern;
    params["value"] = request.value;
    params["start"] = request.start;
    params["matching"] = request.matching;
    params["level"] = "0";
    return params;
}

bool openingParams(const Forms::Params& first, const fs::path& root, Forms::Params& folder)
{
    auto param = [&](const char* name) {
        auto found = first.find(name);
        return found != first.end() ? found->second : std::string();
    };
    fs::path open = openingFolder(param("value"), param("start"), param("kind") == "folder", root);
    if (open.empty()) {
        return false;
    }
    folder = first;
    folder["dir"] = open.string();
    folder["level"] = "1";
    return true;
}

BrowserProvider::BrowserProvider(const Manager::Services& services)
    : services_(services)
    , drives_(listDrives)
    , folders_(userFolders)
{
}

std::vector<std::string>& BrowserProvider::recent()
{
    static std::vector<std::string> folders;
    return folders;
}

fs::path BrowserProvider::root() const
{
    return fs::path(root_.empty() ? Configuration::absolutePath : root_);
}

std::vector<std::string> BrowserProvider::shownExtensions(const Manager::Screen& screen) const
{
    return isFolderKind(screen) ? std::vector<std::string>{} : parseExtensions(screen.param("pattern"));
}

std::vector<std::string> BrowserProvider::countedExtensions(const Manager::Screen& screen) const
{
    return parseExtensions(isFolderKind(screen) ? screen.param("matching") : screen.param("pattern"));
}

void BrowserProvider::refresh()
{
    // Files may have changed while the manager was closed
    if (pending_) pending_->cancelled = true;
    pending_.reset();
    counts_.clear();
    listedKey_ = "\x01";
}

void BrowserProvider::enter(const Manager::Screen& screen)
{
    if (screen.form != formId) {
        if (pending_) pending_->cancelled = true;
        listedKey_ = "\x01";
        return;
    }
    std::string dir = screen.param("dir");
    std::vector<std::string> counted = countedExtensions(screen);
    std::string key = dir + "\n" + screen.param("kind") + "\n" + screen.param("pattern") + "\n" + joined(counted);
    if (key == listedKey_ && !dir.empty()) {
        return;   // back from a file dialog, say: the same folder
    }
    listedKey_ = key;
    query_.clear();
    listing_ = Listing{};
    places_.clear();
    countExtensions_ = joined(counted);
    dir_ = dir.empty() ? fs::path() : fs::path(dir);
    filterDirty_ = true;
    auto pending = std::make_shared<Pending>();
    if (pending_) pending_->cancelled = true;
    pending_ = pending;
    PlacesInput input;
    input.root = root();
    input.value = screen.param("value");
    input.start = screen.param("start");
    input.folderField = isFolderKind(screen);
    input.recent = recent();
    auto drives = drives_;
    auto folders = folders_;
#ifdef _WIN32
    if (dir_.empty()) {
        if (drives) input.drives = drives();
        PlacesInput letters;
        letters.drives = input.drives;
        places_ = startingPlaces(letters);
        drives = {};
    }
#endif
    auto path = dir_;
    auto extensions = countExtensions_;
    std::thread([pending, input, drives, folders, path, extensions, counted]() mutable {
        try {
            if (path.empty()) {
                if (folders) input.userFolders = folders();
                if (drives) input.drives = drives();
                auto places = startingPlaces(input);
                {
                    std::lock_guard<std::mutex> lock(pending->mutex);
                    pending->places = std::move(places);
                    pending->listed = true;
                }
                // Each drive answers independently; none holds up another or shutdown.
                for (auto drive : input.drives) {
                    if (pending->cancelled) break;
                    requestDriveDetails(pending, drive);
                }
            }
            else {
                auto listing = listFolder(path, counted, !input.folderField);
                std::vector<fs::path> countPaths;
                for (const auto& entry : listing.entries) {
                    if (!entry.folder || countPaths.size() >= maxCounted) break;
                    countPaths.push_back(path / entry.name);
                }
                {
                    std::lock_guard<std::mutex> lock(pending->mutex);
                    pending->listing = std::move(listing);
                    pending->listed = true;
                }
                std::map<std::string, Count> counts;
                for (const auto& folder : countPaths) {
                    if (pending->cancelled) break;
                    counts[countKey(extensions, folder)] = countFolder(folder, counted);
                }
                {
                    std::lock_guard<std::mutex> lock(pending->mutex);
                    pending->counts = std::move(counts);
                }
            }
        }
        catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(pending->mutex);
            pending->listing.ok = false;
            pending->listing.error = error.what();
            pending->listed = true;
        }
        {
            std::lock_guard<std::mutex> lock(pending->mutex);
            pending->done = true;
        }
        pending->ready.notify_all();
    }).detach();
}

void BrowserProvider::requestDriveDetails(const std::shared_ptr<Pending>& pending, Drive drive)
{
    struct Entry
    {
        DriveFetch fetch;
        Drive drive;
        bool answered = true;
        std::vector<std::weak_ptr<Pending>> waiting;
    };
    struct Cache
    {
        std::mutex mutex;
        std::map<std::string, Entry> entries;
    };
    // Process lifetime: detached workers may still use it during static destruction.
    static auto cache = new Cache;
    auto shared = cache;
    std::lock_guard<std::mutex> lock(shared->mutex);
    auto& entry = shared->entries[drive.path];
    auto now = std::chrono::steady_clock::now();
    if (entry.fetch.fresh(now)) {
        std::lock_guard<std::mutex> pendingLock(pending->mutex);
        applyDriveDetails(pending->places, entry.drive, entry.answered);
        pending->listed = true;
        return;
    }
    entry.waiting.erase(std::remove_if(entry.waiting.begin(), entry.waiting.end(),
        [](const std::weak_ptr<Pending>& waiter) {
            auto pending = waiter.lock();
            return !pending || pending->cancelled;
        }), entry.waiting.end());
    entry.waiting.push_back(pending);
    if (!entry.fetch.begin(now)) return;
    try {
        std::thread([shared, drive]() mutable {
            bool answered = true;
            try {
                answered = driveDetails(drive);
            }
            catch (const std::exception&) {
                // An unexpected failure leaves the letter available.
            }
            std::lock_guard<std::mutex> lock(shared->mutex);
            auto& entry = shared->entries[drive.path];
            entry.drive = drive;
            entry.answered = answered;
            entry.fetch.fetched = std::chrono::steady_clock::now();
            entry.fetch.valid = true;
            entry.fetch.inFlight = false;
            for (const auto& waiter : entry.waiting) {
                auto pending = waiter.lock();
                if (!pending || pending->cancelled) continue;
                std::lock_guard<std::mutex> pendingLock(pending->mutex);
                applyDriveDetails(pending->places, drive, answered);
                pending->listed = true;
            }
            entry.waiting.clear();
        }).detach();
    }
    catch (const std::exception&) {
        entry.fetch.inFlight = false;
        entry.waiting.clear();
    }
}

void BrowserProvider::update(float)
{
    if (!pending_) return;
    std::lock_guard<std::mutex> lock(pending_->mutex);
    bool changed = false;
    if (pending_->listed) {
        if (dir_.empty()) places_ = pending_->places;
        else {
            listing_ = std::move(pending_->listing);
            filterDirty_ = true;
        }
        pending_->listed = false;
        changed = true;
    }
    if (!pending_->counts.empty()) {
        counts_.insert(pending_->counts.begin(), pending_->counts.end());
        pending_->counts.clear();
        changed = true;
    }
    if (changed) ++generation_;
}

void BrowserProvider::countAll()
{
    // Tests may wait for the batch; the UI only polls it in update().
    if (pending_) {
        std::unique_lock<std::mutex> lock(pending_->mutex);
        pending_->ready.wait(lock, [&] { return pending_->done; });
    }
    update(0);
}

const std::vector<size_t>& BrowserProvider::filtered()
{
    if (filterDirty_) {
        filtered_ = filterEntries(listing_.entries, query_);
        filterDirty_ = false;
    }
    return filtered_;
}

std::string BrowserProvider::get(const std::string& key, const Manager::Screen& screen)
{
    if (screen.form != formId) {
        return "";
    }
    const bool folderKind = isFolderKind(screen);
    const bool atPlaces = screen.param("dir").empty();
    const bool listed = !atPlaces && listing_.ok;
    if (key == "title") {
        return folderKind ? "Choose a folder" : "Choose a file";
    }
    if (key == "for") {
        return screen.param("label");
    }
    if (key == "where") {
        return atPlaces ? "" : displayPath(dir_, root(), isRelativeStyle(screen.param("value")));
    }
    if (key == "summary") {
        if (atPlaces) {
            return "";
        }
        if (!listing_.ok) {
            return listing_.error.empty() ? "Loading..." : "Can't be read: " + listing_.error;
        }
        Count count;
        count.ok = true;
        count.files = listing_.files;
        count.matching = listing_.matching;
        count.folders = listing_.folders;
        return countText(count, countedExtensions(screen));
    }
    if (key == "note") {
        if (atPlaces) {
            return "Start from one of these places.";
        }
        if (folderKind) {
            return "Open the folder you want, then choose Use this folder.";
        }
        std::vector<std::string> shown = shownExtensions(screen);
        if (shown.empty()) {
            return "Showing folders and files. Choose a file to use it.";
        }
        std::string kind = screen.param("filter").empty() ? "files" : screen.param("filter");
        return "Showing folders and " + kind + " (" + extensionList(shown) + "). Choose one to use it.";
    }
    if (key == "emptyNote") {
        if (!listed) {
            return "";
        }
        if (!filtered().empty()) {
            return "";
        }
        if (!query_.empty()) {
            return "Nothing here matches the search.";
        }
        return folderKind ? "There are no folders in here." : "There are no folders or matching files in here.";
    }
    if (key == "query") return query_;
    if (key == "isTop") return yesNo(atPlaces);
    if (key == "isFolder") return yesNo(listed);
    if (key == "canUseFolder") return yesNo(folderKind && listed);
    if (key == "canSearch") return yesNo(listed && (listing_.entries.size() > shownRows || !query_.empty()));
    if (key == "hasSystemDialog") {
        return yesNo(services_.pickWithSystem && services_.mousePresent && services_.mousePresent());
    }
    return "";
}

void BrowserProvider::set(const std::string& key, const std::string& value, const Manager::Screen&)
{
    if (key == "query") {
        if (query_ != value) filterDirty_ = true;
        query_ = value;
    }
}

Forms::Rows BrowserProvider::rows(const std::string& key, size_t limit, const Manager::Screen& screen)
{
    Forms::Rows rows;
    if (screen.form != formId) {
        return rows;
    }
    if (key == "places") {
        rows.total = places_.size();
        for (const Place& place : places_) {
            if (rows.items.size() >= limit) {
                break;
            }
            Forms::Row row;
            row.id = place.path.string();
            row.label = place.label;
            row.detail = place.detail;
            row.action = "browse.enter:" + place.path.string();
            rows.items.push_back(row);
        }
        return rows;
    }
    if (key != "entries" || dir_.empty() || !listing_.ok) {
        return rows;
    }
    // Up a folder, unless this is the top of a drive (Back goes to the places)
    fs::path parent = parentFolder(dir_);
    bool hasParent = !parent.empty();
    const auto& shown = filtered();
    rows.total = shown.size() + (hasParent ? 1 : 0);
    if (hasParent && limit > 0) {
        Forms::Row up;
        up.id = "..";
        up.label = "..";
        up.detail = "Up to " + shortPath(displayPath(parent, root(), isRelativeStyle(screen.param("value"))), 48);
        up.action = "browse.up";
        rows.items.push_back(up);
    }
    for (size_t index : shown) {
        if (rows.items.size() >= limit) {
            break;
        }
        const Entry& entry = listing_.entries[index];
        fs::path path = dir_ / entry.name;
        Forms::Row row;
        row.id = entry.name;
        if (entry.folder) {
            row.label = entry.name + separator();
            auto count = counts_.find(countKey(countExtensions_, path));
            row.detail = count != counts_.end() ? countText(count->second, parseExtensions(countExtensions_)) : "";
            row.action = "browse.enter:" + path.string();
        }
        else {
            row.label = entry.name;
            row.detail = fileDetail(entry);
            if (isPicture(entry.name) && entry.size <= 8 * 1024 * 1024) {
                row.image = path.string();
            }
            row.action = "browse.pick:" + path.string();
        }
        rows.items.push_back(row);
    }
    return rows;
}

std::string BrowserProvider::actionProblem(const std::string& action, const Manager::Screen& screen)
{
    std::string rest;
    if (action == "use") {
        return isFolderKind(screen) && !screen.param("dir").empty() && listing_.ok ? "" : "Open a folder first.";
    }
    if (action == "up" || startsWith(action, "enter:", rest) || startsWith(action, "pick:", rest)) {
        return "";
    }
    if (action == "system") {
        return services_.pickWithSystem && services_.mousePresent && services_.mousePresent() ? "" : "There's no system file dialog here.";
    }
    return Provider::actionProblem(action, screen);
}

Forms::Model::Result BrowserProvider::chosen(const fs::path& path, const Manager::Screen& screen)
{
    Forms::Model::Result result;
    result.ok = true;
    result.info = true;
    result.pickFor = screen.param("for");
    result.pick = storedValue(path, root(), screen.param("value"));
    result.message = "Chose " + displayPath(path, root(), isRelativeStyle(screen.param("value"))) + ".";
    remember(recent(), isFolderKind(screen) ? path : path.parent_path());
    return result;
}

Forms::Model::Result BrowserProvider::runAction(const std::string& action, const Manager::Screen& screen)
{
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) {
        return result;
    }
    result.ok = true;
    std::string rest;
    if (action == "use") {
        return chosen(dir_, screen);
    }
    if (startsWith(action, "pick:", rest)) {
        return chosen(fs::path(rest), screen);
    }
    if (startsWith(action, "enter:", rest)) {
        int level = levelOf(screen) + 1;
        result.open = formId;
        result.keepParams = true;
        result.params["dir"] = rest;
        result.params["level"] = std::to_string(std::min(level, maxLevels));
        result.replace = level > maxLevels;
        return result;
    }
    if (action == "up") {
        // In place of this folder's screen, so going up doesn't pile screens up
        auto parent = parentFolder(fs::path(screen.param("dir")));
        if (parent.empty()) return result;
        result.open = formId;
        result.keepParams = true;
        result.params["dir"] = parent.string();
        result.params["level"] = std::to_string(std::max(1, levelOf(screen) - 1));
        result.replace = true;
        return result;
    }
    if (action == "system") {
        Manager::Services::SystemPick pick;
        pick.folder = isFolderKind(screen);
        pick.title = screen.param("label");
        pick.filterName = screen.param("filter");
        pick.pattern = screen.param("pattern");
        pick.path = !dir_.empty() ? dir_.string() : resolve(screen.param("value"), root()).string();
        Manager::Screen from = screen;
        pick.chosen = [this, from](const std::string& path) { return chosen(fs::path(path), from); };
        services_.pickWithSystem(pick);
        return result;
    }
    return result;
}

}

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

namespace fs = std::filesystem;

namespace
{
    // A large folder shows this many rows, and a search to find the rest
    constexpr size_t shownRows = 200;

    // Folders counted per screen; past that they show no count
    constexpr size_t maxCounted = 400;

    // Time spent counting per frame
    constexpr auto countBudget = std::chrono::milliseconds(6);

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
    counts_.clear();
    toCount_.clear();
    listedKey_ = "\x01";
}

void BrowserProvider::enter(const Manager::Screen& screen)
{
    if (screen.form != formId) {
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
    toCount_.clear();
    countExtensions_ = joined(counted);
    dir_ = dir.empty() ? fs::path() : fs::path(dir);
    if (dir_.empty()) {
        // The places, drives included: a USB stick plugged in since shows up
        PlacesInput input;
        input.root = root();
        input.value = screen.param("value");
        input.start = screen.param("start");
        input.folderField = isFolderKind(screen);
        input.recent = recent();
        if (folders_) input.userFolders = folders_();
        if (drives_) input.drives = drives_();
        places_ = startingPlaces(input);
        return;
    }
    // A folder field lists no files, but counts the ones matching its extensions
    listing_ = listFolder(dir_, counted, !isFolderKind(screen));
    for (const Entry& entry : listing_.entries) {
        if (!entry.folder || toCount_.size() >= maxCounted) {
            break;
        }
        fs::path path = dir_ / entry.name;
        if (!counts_.count(countKey(countExtensions_, path))) {
            toCount_.push_back(path);
        }
    }
    std::reverse(toCount_.begin(), toCount_.end());   // popped from the back: top of the list first
}

bool BrowserProvider::countOne()
{
    if (toCount_.empty()) {
        return false;
    }
    fs::path path = toCount_.back();
    toCount_.pop_back();
    counts_[countKey(countExtensions_, path)] = countFolder(path, parseExtensions(countExtensions_));
    return true;
}

void BrowserProvider::update(float)
{
    if (toCount_.empty()) {
        return;
    }
    auto started = std::chrono::steady_clock::now();
    bool counted = false;
    while (std::chrono::steady_clock::now() - started < countBudget && countOne()) {
        counted = true;
    }
    if (counted) {
        generation_++;
    }
}

void BrowserProvider::countAll()
{
    while (countOne()) {
    }
    generation_++;
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
            return "Can't be read: " + listing_.error;
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
        if (!filterEntries(listing_.entries, query_).empty()) {
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
    fs::path parent = dir_.parent_path();
    bool hasParent = !parent.empty() && parent != dir_ && dir_.has_relative_path();
    std::vector<size_t> shown = filterEntries(listing_.entries, query_);
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
            if (isPicture(entry.name)) {
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
        return services_.pickWithSystem ? "" : "There's no system file dialog here.";
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
    std::error_code ec;
    remember(recent(), fs::is_directory(path, ec) ? path : path.parent_path());
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
        result.open = formId;
        result.keepParams = true;
        result.params["dir"] = dir_.parent_path().string();
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

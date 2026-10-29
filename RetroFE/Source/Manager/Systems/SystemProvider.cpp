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
#include "SystemProvider.h"
#include "../../Catalog/Emulators.h"
#include "../../Library/RetroArch.h"
#include "../Emulators/Launchers.h"
#include "../../Collection/VideoCollections.h"
#include "../../Library/Backups.h"
#include "../../Library/ConfFile.h"
#include "../../Library/Edits.h"
#include "../../Library/Files.h"
#include "../../Library/Indexer.h"
#include "../../Library/LineList.h"
#include "../../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

using Library::CollectionRecord;
using Library::Index;
using Library::iequals;

namespace Manager
{

const char* const videoPlayerLauncher = "VideoPlayer";

namespace
{
    const char* const createdHeader = "# Created by RetroFE Manage";
    const size_t maxFindings = 12;

    // The art a system's page usually shows, in the order the details screen
    // looks for a picture; other files in system_artwork are offered too
    const std::vector<std::string>& artTypes()
    {
        static const std::vector<std::string> types = { "logo", "marquee", "device", "fanart", "video", "story" };
        return types;
    }

    std::string count(size_t n, const std::string& one, const std::string& many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    std::string yesNo(bool value)
    {
        return value ? "yes" : "no";
    }

    std::string capitalised(std::string text)
    {
        if (!text.empty()) {
            text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        }
        return text;
    }

    std::string join(const std::vector<std::string>& parts, const std::string& separator)
    {
        std::string text;
        for (const std::string& part : parts) {
            text += (text.empty() ? "" : separator) + part;
        }
        return text;
    }

    std::string fileName(const std::string& path)
    {
        return Library::narrow(fs::path(path).filename());
    }

    bool sameFileContent(const std::string& a, const std::string& b)
    {
        std::error_code ec;
        auto size = fs::file_size(a, ec);
        if (ec || size != fs::file_size(b, ec) || ec) {
            return false;
        }
        if (size > 64u * 1024 * 1024) {
            return true;   // a big video of the same size: close enough for "is it changed?"
        }
        std::string left;
        std::string right;
        return Library::readFile(a, left) && Library::readFile(b, right) && left == right;
    }

    // What a new video player launcher's arguments are, by the program's name
    std::string playerArguments(const std::string& program)
    {
        std::string name = Library::toLower(fileName(program));
        if (name.find("vlc") != std::string::npos) {
            return "--fullscreen --play-and-exit \"%ITEM_FILEPATH%\"";
        }
        if (name.find("mpv") != std::string::npos) {
            return "--fs \"%ITEM_FILEPATH%\"";
        }
        return "\"%ITEM_FILEPATH%\"";
    }

    bool validExtension(const std::string& extension)
    {
        return !extension.empty() && std::all_of(extension.begin(), extension.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        });
    }

    // Writes a new file (or replaces one), keeping the original first
    bool writeNew(Library::Backups& backups, const std::string& path, const std::string& content, std::string& error)
    {
        return backups.beforeChange(path, error) && Library::writeFileAtomic(path, content, error);
    }

    bool hasPercent(const std::string& path)
    {
        return path.find('%') != std::string::npos;
    }

    // Moves a folder: one rename when it can, else file by file. On failure
    // whatever moved is moved back, so it's all in one place or the other.
    bool moveTree(const std::string& from, const std::string& to, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(fs::path(to).parent_path(), ec);
        fs::rename(from, to, ec);
        if (!ec) {
            return true;
        }
        std::vector<fs::path> files;
        for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                files.push_back(it->path());
            }
        }
        if (ec) {
            error = ec.message();
            return false;
        }
        std::vector<std::pair<fs::path, fs::path>> moved;
        for (const fs::path& file : files) {
            fs::path target = fs::path(to) / file.lexically_relative(from);
            fs::create_directories(target.parent_path(), ec);
            fs::rename(file, target, ec);
            if (ec) {
                std::string copyError;
                if (Library::copyFileAtomic(Library::narrow(file), Library::narrow(target), copyError) && fs::remove(file, ec)) {
                    ec.clear();
                }
            }
            if (ec) {
                error = "can't move " + Library::narrow(file) + ": " + ec.message();
                for (auto back = moved.rbegin(); back != moved.rend(); ++back) {
                    std::error_code ignored;
                    fs::rename(back->second, back->first, ignored);
                }
                return false;
            }
            moved.emplace_back(file, target);
        }
        fs::remove_all(from, ec);   // only folders are left
        return true;
    }

    // Moves a deleted collection's folder to manage/backups/deleted/<name> <time>
    bool moveAside(Library::Backups& backups, const std::string& folder, const std::string& name, std::string& kept,
                   std::string& error)
    {
        std::time_t now = std::time(nullptr);
        char stamp[32] = {};
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H%M%S", &local);
        std::error_code ec;
        fs::path base = fs::path(backups.folder()) / "deleted";
        fs::path target = base / (name + " " + stamp);
        for (int n = 2; fs::exists(target, ec); n++) {
            target = base / (name + " " + stamp + " (" + std::to_string(n) + ")");
        }
        kept = Library::narrow(target);
        return moveTree(folder, kept, error);
    }

    // Whether RetroFE lists the playlist: cyclePlaylist names it exactly, or is empty
    bool shownByCycle(const CollectionRecord& record, const std::string& playlist)
    {
        return record.cyclePlaylist.empty()
            || std::find(record.cyclePlaylist.begin(), record.cyclePlaylist.end(), playlist) != record.cyclePlaylist.end();
    }

    // A playlist named like a sort field (Item::validSortType) is sorted by it
    bool isSortName(const std::string& name)
    {
        static const std::vector<std::string> names = {
            "year", "manufacturer", "developer", "genre", "numberplayers", "numberbuttons", "ctrltype",
            "joyways", "rating", "score", "lastplayed", "playcount",
        };
        return std::find(names.begin(), names.end(), Library::toLower(name)) != names.end();
    }

    // The first file in a collection folder that isn't settings, lists or art
    // (its relative path), or "": what deleting the folder could lose
    std::string strayFile(const std::string& folder)
    {
        std::error_code ec;
        fs::path base(folder);
        for (auto it = fs::recursive_directory_iterator(base, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) {
                continue;
            }
            fs::path relative = it->path().lexically_relative(base);
            std::string top = Library::toLower(Library::narrow(*relative.begin()));
            std::string extension = Library::toLower(Library::narrow(it->path().extension()));
            bool nested = std::distance(relative.begin(), relative.end()) > 1;
            bool art = nested && (top == "system_artwork" || top == "medium_artwork");
            bool lists = nested && top == "playlists" && extension == ".txt";
            bool launchers = nested && top.compare(0, 9, "launchers") == 0 && extension == ".conf";
            bool menu = nested && top == "menu" && extension == ".txt";
            bool config = !nested && (extension == ".conf" || extension == ".txt" || extension == ".sub" || extension == ".xml");
            if (!art && !lists && !launchers && !menu && !config) {
                return Library::narrow(relative.generic_string());
            }
        }
        return "";
    }
}

std::string normaliseExtensions(const std::string& text)
{
    std::vector<std::string> result;
    std::string part;
    auto flush = [&] {
        while (!part.empty() && part[0] == '.') {
            part.erase(0, 1);
        }
        if (!part.empty() && std::find(result.begin(), result.end(), part) == result.end()) {
            result.push_back(part);
        }
        part.clear();
    };
    for (char c : text) {
        if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) {
            flush();
        }
        else {
            part += c;
        }
    }
    flush();
    return join(result, ",");
}

bool madeHere(const CollectionRecord& record)
{
    std::string content;
    if (!Library::readFile(Utils::combinePath(record.folder, "settings.conf"), content)) {
        return false;
    }
    // Setup's "# Created by RetroFE setup" and this screen's own header, and
    // nothing in the folder but settings, lists and art (a header can be copied)
    return content.compare(0, 20, "# Created by RetroFE") == 0 && strayFile(record.folder).empty();
}

bool isVideoCollection(const CollectionRecord& record)
{
    auto type = record.info.find(VideoCollections::typeKey);
    return type != record.info.end() && VideoCollections::isVideoType(type->second);
}

bool playsHere(const CollectionRecord& record)
{
    auto type = record.info.find(VideoCollections::typeKey);
    auto play = record.info.find(VideoCollections::playKey);
    return type != record.info.end() && play != record.info.end() && VideoCollections::playsHere(type->second, play->second);
}

std::vector<std::string> unplayableExtensions(const CollectionRecord& record)
{
    std::vector<std::string> result;
    for (const std::string& extension : record.extensions) {
        if (!extension.empty() && !VideoCollections::isPlayableExtension(extension)) {
            result.push_back(extension);
        }
    }
    return result;
}

SystemCheck checkSystem(const Index& index, int collection, bool videos)
{
    SystemCheck check;
    if (collection < 0 || static_cast<size_t>(collection) >= index.collections.size()) {
        return check;
    }
    const CollectionRecord& r = index.collections[static_cast<size_t>(collection)];
    const std::string item = videos ? "video" : "game";
    const std::string items = videos ? "videos" : "games";
    auto add = [&](const std::string& finding) { check.findings.push_back(finding); };

    // Where it's listed
    bool isTop = iequals(r.name, index.topMenu);
    if (r.parents.empty() && !isTop) {
        add("No menu lists " + r.name + ", so it can't be reached from Browse. Add it to a menu in Menus.");
    }

    // Its files
    bool anyFolder = false;
    std::error_code ec;
    for (const std::string& folder : r.listPaths) {
        if (folder.empty()) {
            continue;
        }
        if (!fs::is_directory(folder, ec)) {
            add("The " + item + "s folder " + folder + " doesn't exist.");
        }
        else {
            anyFolder = true;
        }
    }
    bool hasExtensions = std::any_of(r.extensions.begin(), r.extensions.end(), [](const std::string& e) { return !e.empty(); });
    size_t files = static_cast<size_t>(collection) < index.candidates.size() ? index.candidates[static_cast<size_t>(collection)].size() : 0;
    if (!hasExtensions) {
        add("No file extensions are set (list.extensions), so RetroFE looks for no files.");
    }
    else if (anyFolder && files == 0) {
        std::vector<std::string> dotted;
        for (const std::string& extension : r.extensions) {
            dotted.push_back("." + extension);
        }
        add("No file in its " + items + " folder ends in " + join(dotted, ", ") + ".");
    }
    if (r.caseMismatchCount > 0) {
        std::string example = "\"" + (r.caseMismatches.empty() ? std::string() : r.caseMismatches[0]) + "\"";
        add((r.caseMismatchCount == 1 ? example + " ends"
                                      : std::to_string(r.caseMismatchCount) + " files such as " + example + " end")
            + " in a listed extension only if case is ignored. RetroFE compares the case, so it skips "
            + (r.caseMismatchCount == 1 ? "it" : "them") + ": add that spelling to the extensions, or rename the "
            + (r.caseMismatchCount == 1 ? "file." : "files."));
    }
    if (files > 0 && r.gameCount == 0) {
        add("Its include and exclude lists leave no " + items + ": " + count(files, "file is", "files are")
            + " found, but none is listed.");
    }

    // How they start: RetroFE plays them itself, so only their file types matter
    const Library::LauncherResolution& launcher = r.launcher;
    bool here = videos && playsHere(r);
    std::vector<std::string> unplayable = here ? unplayableExtensions(r) : std::vector<std::string>();
    if (here) {
        if (!unplayable.empty()) {
            std::vector<std::string> dotted;
            for (const std::string& extension : unplayable) {
                dotted.push_back("." + extension);
            }
            add("RetroFE plays " + join(VideoCollections::playableExtensions(), ", ") + " files itself; "
                + join(dotted, ", ") + " files don't play here. Play them with an external player (Playback), or convert them.");
        }
    }
    else if (launcher.via == Library::LauncherVia::None) {
        add("No launcher is set, so its " + items + " can't start.");
    }
    else if (launcher.launcher < 0) {
        add(launcher.problem.empty() ? "The launcher \"" + launcher.name + "\" isn't installed, so its " + items + " can't start."
                                     : launcher.problem);
    }
    else {
        const Library::LauncherDef& def = index.launchers[static_cast<size_t>(launcher.launcher)];
        if (Utils::trimEnds(def.executable).empty()) {
            add("The launcher " + def.id + " names no program (executable), so its " + items + " can't start.");
        }
        else if (!hasPercent(def.executable) && fs::path(def.executable).is_absolute() && !fs::exists(def.executable, ec)) {
            add("The launcher " + def.id + " starts " + def.executable + ", which isn't there.");
        }
        else if (!launcher.problem.empty() && hasExtensions) {
            add(launcher.problem);   // (no extensions is said above)
        }
    }
    size_t broken = 0;
    for (const auto& [game, name] : r.launcherOverrides) {
        if (name && index.resolveLauncher(collection, game).launcher < 0) {
            broken++;
        }
    }
    if (broken > 0 && !here) {
        add(count(broken, item + " has a launcher", items + " have launchers")
            + (broken == 1 ? " of its own that isn't installed" : " of their own that aren't installed")
            + " (collections/" + r.name + "/launchers).");
    }

    // Titles and years (videos seldom have hyperlist metadata, so it's only checked for games)
    if (r.gameCount > 0 && !videos) {
        size_t withMetadata = 0;
        for (size_t g = r.gameBegin; g < r.gameBegin + r.gameCount && g < index.games.size(); g++) {
            withMetadata += index.games[g].hasMetadata ? 1 : 0;
        }
        if (withMetadata == 0) {
            // What RetroFE imports: meta/hyperlist/<type>.xml, meta/mamelist/<type>*.xml,
            // and meta/emuarc/*.dat (matched by the name in each file's header)
            const std::string& type = r.metadataType;
            std::string meta = Utils::combinePath(index.options.root, "meta");
            std::vector<std::string> found;
            if (fs::exists(Utils::combinePath(meta, "hyperlist", type + ".xml"), ec)) {
                found.push_back("meta/hyperlist/" + type + ".xml");
            }
            bool emuarc = false;
            for (const char* folder : { "mamelist", "emuarc" }) {
                for (const auto& entry : fs::directory_iterator(Utils::combinePath(meta, folder), ec)) {
                    std::string name;
                    if (!entry.is_regular_file(ec) || !Library::narrowName(entry.path().filename(), name)) {
                        continue;
                    }
                    if (std::string(folder) == "mamelist" && name.size() > 4 && iequals(name.substr(name.size() - 4), ".xml")
                        && Library::toLower(name).compare(0, type.size(), Library::toLower(type)) == 0) {
                        found.push_back("meta/mamelist/" + name);
                    }
                    emuarc |= std::string(folder) == "emuarc" && name.size() > 4 && iequals(name.substr(name.size() - 4), ".dat");
                }
            }
            if (found.empty() && !emuarc) {
                add("There's no metadata for " + type + " (meta/hyperlist/" + type + ".xml, meta/mamelist/" + type
                    + "*.xml or a meta/emuarc list), so its " + items + " show their file names instead of titles.");
            }
            else if (found.empty()) {
                add("None of its " + items + " has metadata. There's no meta/hyperlist/" + type + ".xml or meta/mamelist/"
                    + type + "*.xml; meta/emuarc has lists, but RetroFE uses one only if its header names " + type + ".");
            }
            else {
                add("None of its " + items + " has metadata, though " + join(found, ", ") + (found.size() == 1 ? " exists" : " exist")
                    + ": it may be unreadable, name other " + items + ", or not be imported yet.");
            }
        }
    }
    for (const std::string& problem : index.problems) {
        if (Library::toLower(problem).find("meta") != std::string::npos) {
            add("Metadata couldn't be read: " + problem);
        }
    }

    // Playlists RetroFE won't show because cyclePlaylist leaves them out
    for (const Library::PlaylistInfo& playlist : r.playlists) {
        if (!playlist.reserved && !shownByCycle(r, playlist.name)) {
            add("The playlist " + playlist.name + " isn't in cyclePlaylist (" + join(r.cyclePlaylist, ", ")
                + "), so RetroFE doesn't show it.");
        }
    }

    // What the library noticed (fork-only keys, spellings), except what's said
    // above. Keys this fork honours are notes, not problems: nothing to fix.
    for (const std::string& note : r.notes) {
        if (note.find("only ignoring case") != std::string::npos) {
            continue;
        }
        bool informational = note.find("this fork honours") != std::string::npos
            || note.find("this fork uses") != std::string::npos || note.find("keeps it in step") != std::string::npos;
        if (informational) {
            check.notes.push_back(note);
        }
        else {
            add(note);
        }
    }

    std::string where;
    if (!r.parents.empty()) {
        std::vector<std::string> names;
        for (int parent : r.parents) {
            names.push_back(index.collections[static_cast<size_t>(parent)].name);
        }
        where = "listed under " + join(names, ", ");
    }
    else {
        where = isTop ? "the top menu" : "in no menu";
    }
    check.summary = r.name + ": "
        + (check.findings.empty() ? "no problems" : count(check.findings.size(), "problem", "problems"))
        + ". " + count(r.gameCount, item, items) + ", " + where + ".";
    return check;
}

// --- The provider -------------------------------------------------------------------

SystemProvider::SystemProvider(const Services& services, bool videos)
    : services_(services)
    , videos_(videos)
    , base_(videos ? "video" : "system")
    , noun_(videos ? "video collection" : "system")
    , item_(videos ? "video" : "game")
    , items_(videos ? "videos" : "games")
{
}

SystemProvider::IndexPtr SystemProvider::index() const
{
    return services_.library ? services_.library->index() : nullptr;
}

std::string SystemProvider::root() const
{
    return services_.library ? services_.library->options().root : "";
}

const CollectionRecord* SystemProvider::record(const Index& index, const Screen& screen, int* position) const
{
    int c = index.findCollection(screen.param("id"));
    if (position) {
        *position = c;
    }
    return c < 0 ? nullptr : &index.collections[static_cast<size_t>(c)];
}

bool SystemProvider::isMine(const Screen& screen) const
{
    return screen.form == base_ || screen.form == base_ + "s" || screen.form.compare(0, base_.size() + 1, base_ + "-") == 0;
}

bool SystemProvider::belongs(const CollectionRecord& record) const
{
    return record.is(Library::KindSystem) && isVideoCollection(record) == videos_;
}

void SystemProvider::changed(const std::string& what)
{
    changes_++;
    if (services_.library) {
        services_.library->changed(what);
    }
    else {
        services_.requestReload();
    }
}

unsigned SystemProvider::generation() const
{
    return (services_.library ? services_.library->status().generation : 0) + changes_;
}

Forms::Params SystemProvider::saveView() const
{
    return {
        { "query", query_ },
        { "menu", menu_ },
        { "playlistQuery", playlistQuery_ }
    };
}

void SystemProvider::restoreView(const Forms::Params& state)
{
    if (auto found = state.find("query"); found != state.end()) {
        query_ = found->second;
    }
    if (auto found = state.find("menu"); found != state.end()) {
        menu_ = found->second;
    }
    if (auto found = state.find("playlistQuery"); found != state.end()) {
        playlistQuery_ = found->second;
    }
}

void SystemProvider::refresh()
{
    listIndex_.reset();
    detailsIndex_.reset();
    mediaIndex_.reset();
    playlistKey_.clear();
    playlistsKey_.clear();
    menuIndex_.reset();
    suggestionIndex_.reset();
    settings_.from = launcher_.from = info_.from = playlistName_.from = nullptr;
}

void SystemProvider::enter(const Screen& screen)
{
    if (!isMine(screen)) {
        return;
    }
    if (isForm(screen, "-check") || screen.form == base_) {
        detailsIndex_.reset();   // checked afresh on every visit
    }
    if (isForm(screen, "-media") || isForm(screen, "-medium") || isForm(screen, "-medium-file")) {
        ensureMedia(screen, true);
    }
    if (isForm(screen, "-medium")) {
        mediaFile_.clear();
    }
    if (isForm(screen, "-playlist") || isForm(screen, "-playlist-entry")) {
        ensurePlaylist(screen, true);
    }
    if (isForm(screen, "-playlist")) {
        playlistName_.from = nullptr;   // the name it has now
    }
    if (isForm(screen, "-add-games") && importSystem_ != screen.param("id")) {
        import_.stop(); updateImport();
        import_.reset();
        importFolder_.clear(); importSubfolders_ = true;
        importSystem_ = screen.param("id");
    }
    if (!videos_ && isForm(screen, "-add")) syncAddLaunchers();
    syncBuffers(screen);
}

SystemProvider::Buffer* SystemProvider::bufferFor(const Screen& screen)
{
    if (isForm(screen, "-settings")) return &settings_;
    if (isForm(screen, "-launcher")) return &launcher_;
    if (isForm(screen, "-info")) return &info_;
    if (isForm(screen, "-playlist")) return &playlistName_;
    return nullptr;
}

void SystemProvider::syncBuffers(const Screen& screen)
{
    Buffer* buffer = bufferFor(screen);
    IndexPtr idx = index();
    if (!buffer || !idx) {
        return;
    }
    const CollectionRecord* r = record(*idx, screen);
    if (!r) {
        return;
    }
    bool isPlaylist = buffer == &playlistName_;
    std::string id = r->name + (isPlaylist ? "\n" + screen.param("playlist") : "");
    if (buffer->edits.id() == id && (buffer->edits.isDirty() || buffer->from == idx.get())) {
        return;
    }
    buffer->from = idx.get();
    if (buffer == &settings_) buffer->edits.load(id, readSettings(*r));
    else if (buffer == &launcher_) buffer->edits.load(id, readLauncher(*r));
    else if (buffer == &info_) buffer->edits.load(id, readInfo(*r));
    else buffer->edits.load(id, { { "playlistName", screen.param("playlist") } });
}

std::map<std::string, std::string> SystemProvider::readSettings(const CollectionRecord& record)
{
    Library::ConfLayers layers(record.folder, Library::ConfLayers::collectionFileNames());
    layers.load();
    for (const char* key : { "list.menuSort", "list.includeMissingItems" }) {
        std::string raw = Library::toLower(layers.get(key));
        booleanStyle_[key] = raw == "true" || raw == "false" ? "true" : "";
    }

    // The metadata files there are, plus what it uses now
    metadataTypes_.clear();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(Utils::combinePath(root(), "meta", "hyperlist"), ec)) {
        std::string name;
        if (entry.is_regular_file(ec) && Library::narrowName(entry.path().filename(), name) && name.size() > 4
            && iequals(name.substr(name.size() - 4), ".xml")) {
            metadataTypes_.push_back(name.substr(0, name.size() - 4));
        }
    }
    for (const std::string& name : { record.metadataType, record.name }) {
        if (std::find(metadataTypes_.begin(), metadataTypes_.end(), name) == metadataTypes_.end()) {
            metadataTypes_.push_back(name);
        }
    }
    std::sort(metadataTypes_.begin(), metadataTypes_.end(), [](const std::string& a, const std::string& b) {
        return Library::toLower(a) < Library::toLower(b);
    });

    return {
        { "extensions", Utils::trimEnds(layers.get("list.extensions")) },
        { "menuSort", yesNo(record.menuSort) },
        { "includeMissing", yesNo(record.includeMissingItems.value_or(false)) },
        { "metadataType", record.metadataType },
    };
}

std::map<std::string, std::string> SystemProvider::readLauncher(const CollectionRecord& record)
{
    Library::ConfLayers layers(record.folder, Library::ConfLayers::collectionFileNames());
    layers.load();
    launcherSource_ = layers.source("launcher");
    if (videos_) {
        return { { "launcher", record.launcher.configured },
                 { "play", playsHere(record) ? VideoCollections::playHere : VideoCollections::playLauncher } };
    }
    return { { "launcher", record.launcher.configured } };
}

std::map<std::string, std::string> SystemProvider::readInfo(const CollectionRecord& record) const
{
    auto value = [&](const char* key) {
        auto found = record.info.find(key);
        return found == record.info.end() ? std::string() : found->second;
    };
    return {
        { "infoTitle", value("title") },
        { "manufacturer", value("manufacturer") },
        { "year", value("year") },
        { "genre", value("genre") },
        { "isVideo", yesNo(isVideoCollection(record)) },
    };
}

std::string SystemProvider::leaving(const Screen& screen)
{
    if (isForm(screen, "-add-games")) { import_.stop(); updateImport(); }
    Buffer* buffer = bufferFor(screen);
    if (buffer && buffer != &playlistName_ && buffer->edits.isDirty()) {
        return "You have unsaved changes. Save them, or leave again to discard them.";
    }
    return "";
}

void SystemProvider::discard(const Screen& screen)
{
    if (Buffer* buffer = bufferFor(screen)) {
        buffer->edits.reset();
    }
}

std::string SystemProvider::status(const Screen& screen) const
{
    if (!services_.library) {
        return "Manage's library isn't available.";
    }
    IndexPtr idx = index();
    if (!idx) {
        Library::Indexer::Status st = services_.library->status();
        if (st.state == Library::Indexer::State::Failed) {
            return "Couldn't read your collections: " + st.error;
        }
        return "Reading your collections... " + std::to_string(static_cast<int>(st.progress * 100)) + "%";
    }
    if (screen.form != base_ + "s" && !isForm(screen, "-add") && !record(*idx, screen)) {
        return "There's no " + noun_ + " called \"" + screen.param("id") + "\" yet. If you've just added it, it appears in a moment.";
    }
    return "";
}

std::string SystemProvider::screenTitle(const Screen& screen) const
{
    IndexPtr idx = index();
    const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
    std::string name = r ? r->name : screen.param("id");
    if (isForm(screen, "-settings")) return name + ": settings";
    if (isForm(screen, "-launcher")) return name + (videos_ ? ": playback" : ": launcher");
    if (isForm(screen, "-info")) return name + ": info";
    if (isForm(screen, "-media")) return name + ": media";
    if (isForm(screen, "-medium")) return name + ": " + screen.param("slot");
    if (isForm(screen, "-medium-file")) return name + ": " + screen.param("slot") + " folder";
    if (isForm(screen, "-playlists")) return name + ": playlists";
    if (isForm(screen, "-playlist")) return name + ": " + screen.param("playlist");
    if (isForm(screen, "-check")) return "Check " + name;
    return name;
}

// --- Details --------------------------------------------------------------------------

void SystemProvider::ensureDetails(const Screen& screen)
{
    IndexPtr idx = index();
    std::string id = screen.param("id");
    if (!idx || (idx == detailsIndex_ && id == detailsId_)) {
        return;
    }
    detailsIndex_ = idx;
    detailsId_ = id;
    image_.clear();
    check_ = SystemCheck();
    deleteProblem_.clear();
    launcherSource_.clear();
    int c;
    const CollectionRecord* r = record(*idx, screen, &c);
    if (!r) {
        return;
    }
    for (const char* type : { "logo", "marquee", "device", "fanart" }) {
        Library::MediaSlot slot = Library::collectionMedia(*idx, c, type, Library::MediaKind::Image);
        if (!slot.file.empty()) {
            image_ = slot.file;
            break;
        }
    }
    check_ = checkSystem(*idx, c, videos_);
    readLauncher(*r);
    ensureMedia(screen);

    // Deleting: only a plain system, empty or made here, whose files aren't inside it
    const std::string& name = r->name;
    if (r->reserved) {
        deleteProblem_ = "RetroFE gives " + name + " a meaning of its own, so it can't be deleted here.";
    }
    else if (iequals(name, idx->topMenu)) {
        deleteProblem_ = name + " is the top menu, so it can't be deleted.";
    }
    else if (r->is(Library::KindMenu)) {
        deleteProblem_ = name + " lists other collections too, so it's a menu as well; it can't be deleted here.";
    }
    else if (r->is(Library::KindMerged)) {
        deleteProblem_ = name + " also shows other collections' " + items_ + " (.sub files), so it can't be deleted here.";
    }
    for (int parent : r->parents) {
        // Manage edits menu.txt and menu/ folders; a menu.xml or a menu made from
        // the collection launchers would keep listing it
        const CollectionRecord& menu = idx->collections[static_cast<size_t>(parent)];
        if (deleteProblem_.empty()
            && (menu.menuSource == Library::MenuSource::MenuXml || menu.menuSource == Library::MenuSource::CollectionLaunchers)) {
            deleteProblem_ = menu.name + " lists it in "
                + std::string(menu.menuSource == Library::MenuSource::MenuXml ? "its menu.xml" : "collectionLaunchers")
                + ", which Manage doesn't change; take it out there first.";
        }
    }
    std::error_code ec;
    for (const std::string& folder : r->listPaths) {
        if (deleteProblem_.empty() && !folder.empty() && Library::isInside(folder, r->folder) && fs::exists(folder, ec)) {
            deleteProblem_ = "Its " + items_ + " folder (" + folder + ") is inside its own folder, so deleting it would take that folder too.";
        }
    }
    if (deleteProblem_.empty()) {
        std::string stray = strayFile(r->folder);
        if (!stray.empty()) {
            deleteProblem_ = "Its folder holds more than settings and art (such as " + stray + "); move those out first.";
        }
    }
    if (deleteProblem_.empty() && r->gameCount > 0 && !madeHere(*r)) {
        deleteProblem_ = "It has " + count(r->gameCount, item_, items_) + ". Only an empty " + noun_ + ", or one added here, can be deleted.";
    }
    for (const CollectionRecord& other : idx->collections) {
        for (const Library::SubSource& sub : other.subs) {
            if (deleteProblem_.empty() && sub.collection == c) {
                deleteProblem_ = other.name + " shows its " + items_ + " (" + sub.source + ".sub), so it can't be deleted.";
            }
        }
    }
}

std::string SystemProvider::launcherText(const Index& index, const CollectionRecord& record) const
{
    const Library::LauncherResolution& l = record.launcher;
    std::string text;
    switch (l.via) {
    case Library::LauncherVia::None:
        return "None";
    case Library::LauncherVia::CollectionFile: {
        std::string file = l.launcher >= 0 ? fileName(index.launchers[static_cast<size_t>(l.launcher)].file) : "launcher.conf";
        text = "Its own launcher file (" + file + ")";
        break;
    }
    default:
        text = l.name + (launcherSource_.empty() ? "" : " (set in " + launcherSource_ + ")");
        break;
    }
    if (l.launcher < 0) {
        text += ", which isn't installed";
    }
    return text;
}

std::string SystemProvider::detailsValue(const std::string& key, const Screen& screen)
{
    ensureDetails(screen);
    IndexPtr idx = index();
    const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
    if (!r) {
        return key == "canDelete" ? "no" : "";
    }
    if (key == "title") return r->title;
    if (key == "name") return r->title != r->name ? r->name : "";
    if (key == "settingsFile" || key == "infoFile" || key == "launcherFile") {
        // Saved in: the file Save writes (the launcher setting stays in the file that has it)
        std::string name = key == "infoFile" ? "info.conf"
                         : key == "launcherFile" && !launcherSource_.empty() ? launcherSource_ : "settings.conf";
        fs::path file = fs::path(Utils::combinePath(r->folder, name)).lexically_normal();
        fs::path relative = file.lexically_relative(fs::path(root()).lexically_normal());
        std::string text = relative.empty() || *relative.begin() == ".." ? Library::narrow(file) : Library::narrow(fs::path(relative.generic_wstring()));
        return text;
    }
    if (key == "menus") {
        std::vector<std::string> names;
        for (int parent : r->parents) {
            names.push_back(idx->collections[static_cast<size_t>(parent)].name);
        }
        if (names.empty()) {
            return iequals(r->name, idx->topMenu) ? "It's the top menu" : "None";
        }
        return join(names, ", ");
    }
    if (key == "gamesFolder") {
        std::vector<std::string> folders;
        for (const std::string& folder : r->listPaths) {
            if (!folder.empty()) folders.push_back(folder);
        }
        return folders.empty() ? "Not set" : join(folders, "; ");
    }
    if (key == "extensionsText") return r->extensions.empty() ? "None" : join(r->extensions, ", ");
    if (key == "gameCount") return count(r->gameCount, item_, items_);
    if (key == "launcherInEffect") return launcherText(*idx, *r);
    if (key == "playsText") {
        if (!playsHere(*r)) {
            return "With an external player (its launcher)";
        }
        std::vector<std::string> unplayable = unplayableExtensions(*r);
        return unplayable.empty() ? "In RetroFE" : "In RetroFE, except its ." + join(unplayable, ", .") + " files";
    }
    if (key == "settingsStatus") return r->extensions.empty() ? "No extensions" : join(r->extensions, ", ");
    if (key == "launcherStatus") {
        if (videos_ && playsHere(*r)) {
            return "In RetroFE";
        }
        // The only Launcher row on the details screen, so it carries the warning
        const Library::LauncherResolution& l = r->launcher;
        if (l.via == Library::LauncherVia::None) return "None: its " + items_ + " can't start";
        std::string name = l.via == Library::LauncherVia::CollectionFile ? "Its own file" : l.name;
        return l.launcher < 0 ? name + ", which isn't installed" : name;
    }
    if (key == "infoStatus") {
        std::vector<std::string> parts;
        for (const char* field : { "manufacturer", "year", "genre" }) {
            auto found = r->info.find(field);
            if (found != r->info.end() && !Utils::trimEnds(found->second).empty()) {
                parts.push_back(found->second);
            }
        }
        return parts.empty() ? (r->title != r->name ? r->title : "Not set") : join(parts, ", ");
    }
    if (key == "mediaStatus") return std::to_string(ownArt_) + " of " + std::to_string(slots_.size());
    if (key == "playlistsStatus") return count(r->playlists.size(), "playlist", "playlists");
    if (key == "checkStatus") {
        return check_.findings.empty() ? "No problems" : count(check_.findings.size(), "problem", "problems");
    }
    if (key == "hasProblems") return yesNo(!check_.findings.empty());
    if (key == "hasNotes") return yesNo(!check_.notes.empty());
    if (key.compare(0, 4, "note") == 0 && key.size() > 4 && std::isdigit(static_cast<unsigned char>(key[4]))) {
        size_t n = static_cast<size_t>(Utils::convertInt(key.substr(4)));
        return n < check_.notes.size() ? "- " + check_.notes[n] : "";
    }
    if (key == "canDelete") return yesNo(deleteProblem_.empty());
    if (key == "image") return image_;
    if (key == "checkSummary") return check_.summary;
    if (key.compare(0, 7, "finding") == 0) {
        size_t n = static_cast<size_t>(Utils::convertInt(key.substr(7)));
        if (n + 1 == maxFindings && check_.findings.size() > maxFindings) {
            return "... and " + count(check_.findings.size() - maxFindings + 1, "more problem", "more problems") + ".";
        }
        return n < check_.findings.size() ? "- " + check_.findings[n] : "";
    }
    // Launcher screen (a video collection RetroFE plays has no launcher notes)
    if ((key == "launcherFileNote" || key == "overridesNote" || key == "launcherProblem") && videos_ && playsHere(*r)) {
        return "";
    }
    if (key == "launcherFileNote") {
        if (r->launcher.via == Library::LauncherVia::CollectionFile) {
            std::string file = r->launcher.launcher >= 0 ? idx->launchers[static_cast<size_t>(r->launcher.launcher)].file : "";
            return "Its own launcher file " + file + " decides how its " + items_
                 + " start. The default below applies only if that file goes.";
        }
        return "";
    }
    if (key == "alternatives") return join(r->launcher.alternatives, ", ");
    if (key == "overridesNote") {
        size_t own = 0;
        for (const auto& [game, name] : r->launcherOverrides) {
            own += name ? 1 : 0;
        }
        return own == 0 ? "" : count(own, item_ + " has a launcher of its own; change it", items_ + " have launchers of their own; change those")
                                   + " in Games.";
    }
    if (key == "launcherProblem") return r->launcher.problem;
    return "";
}

// --- Media ------------------------------------------------------------------------------

void SystemProvider::ensureMedia(const Screen& screen, bool force)
{
    IndexPtr idx = index();
    std::string id = screen.param("id");
    if (!idx || (!force && idx == mediaIndex_ && id == mediaId_)) {
        return;
    }
    mediaIndex_ = idx;
    mediaId_ = id;
    slots_.clear();
    originals_.clear();
    ownArt_ = 0;
    int c;
    if (!record(*idx, screen, &c)) {
        return;
    }
    std::vector<std::string> types = artTypes();
    Library::MediaSlot first = Library::collectionMedia(*idx, c, types.front(), Library::defaultKind(types.front()));
    // Other art already in system_artwork
    std::error_code ec;
    std::set<std::string> extra;
    for (const auto& entry : fs::directory_iterator(first.directory, ec)) {
        std::string name;
        if (!Library::narrowName(entry.path().filename(), name)) {
            continue;
        }
        if (entry.is_directory(ec)) {
            // A media folder (fork compat) holding art of the kind its name suggests
            bool known = std::any_of(types.begin(), types.end(), [&](const std::string& t) { return iequals(t, name); });
            if (!known && !Library::mediaFolderFiles(first.directory, name, Library::defaultKind(name)).empty()) {
                extra.insert(name);
            }
            continue;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        size_t dot = name.find_last_of('.');
        std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
        std::string extension = dot == std::string::npos ? "" : Library::toLower(name.substr(dot + 1));
        bool known = std::any_of(types.begin(), types.end(), [&](const std::string& t) { return iequals(t, stem); });
        bool media = false;
        for (Library::MediaKind kind : { Library::MediaKind::Image, Library::MediaKind::Video, Library::MediaKind::Text }) {
            const auto& accepted = Library::acceptedExtensions(kind);
            media |= std::find(accepted.begin(), accepted.end(), extension) != accepted.end();
        }
        if (!known && media && !stem.empty()) {
            extra.insert(stem);
        }
    }
    types.insert(types.end(), extra.begin(), extra.end());

    Library::Backups* backups = services_.editor ? &services_.editor->backups() : nullptr;
    for (const std::string& type : types) {
        Library::MediaSlot slot = Library::collectionMedia(*idx, c, type, Library::defaultKind(type));
        ownArt_ += slot.own ? 1 : 0;

        // Files a "Put back the original" would restore: originals kept for this slot
        // (or "absent" markers for files Manage made), where something differs now
        if (backups && !slot.directory.empty()) {
            std::string probe = backups->backupPath(Utils::combinePath(slot.directory, slot.basename + ".x"));
            const auto& accepted = Library::acceptedExtensions(slot.kind);
            for (const auto& entry : fs::directory_iterator(fs::path(probe).parent_path(), ec)) {
                std::string name;
                if (!entry.is_regular_file(ec) || !Library::narrowName(entry.path().filename(), name)) {
                    continue;
                }
                bool absent = name.size() > 7 && name.compare(name.size() - 7, 7, ".absent") == 0;
                std::string original = absent ? name.substr(0, name.size() - 7) : name;
                size_t dot = original.find_last_of('.');
                if (dot == std::string::npos || !iequals(original.substr(0, dot), slot.basename)
                    || std::find(accepted.begin(), accepted.end(), Library::toLower(original.substr(dot + 1))) == accepted.end()) {
                    continue;
                }
                std::string live = Utils::combinePath(slot.directory, original);
                bool differs = absent ? fs::exists(live, ec) : !fs::exists(live, ec) || !sameFileContent(live, Library::narrow(entry.path()));
                if (differs) {
                    originals_[type].push_back(live);
                }
            }
            // and its media folder's files (fork compat)
            for (const std::string& live : services_.editor->mediaFolderOriginals(slot)) {
                originals_[type].push_back(live);
            }
        }
        slots_.push_back(std::move(slot));
    }
}

const Library::MediaSlot* SystemProvider::slot(const Screen& screen)
{
    ensureMedia(screen);
    std::string type = screen.param("slot");
    for (const Library::MediaSlot& s : slots_) {
        if (s.type == type) {
            return &s;
        }
    }
    return nullptr;
}

// --- Playlists --------------------------------------------------------------------------

std::string SystemProvider::playlistFile(const CollectionRecord& record, const std::string& name) const
{
    std::error_code ec;
    std::string folder = Utils::combinePath(record.folder, "playlists");
    std::string found;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        std::string file;
        if (!entry.is_regular_file(ec) || !Library::narrowName(entry.path().filename(), file) || file.size() <= 4
            || file.compare(file.size() - 4, 4, ".txt") != 0) {
            continue;
        }
        std::string stem = file.substr(0, file.size() - 4);
        if (stem == name) {
            return Utils::combinePath(folder, file);
        }
        if (found.empty() && iequals(stem, name)) {
            found = Utils::combinePath(folder, file);
        }
    }
    return found;
}

void SystemProvider::ensurePlaylist(const Screen& screen, bool force)
{
    std::string key = screen.param("id") + "\n" + screen.param("playlist");
    if (!force && key == playlistKey_) {
        return;
    }
    playlistKey_ = key;
    entries_.clear();
    playlistPath_.clear();
    IndexPtr idx = index();
    const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
    if (!r) {
        playlistKey_.clear();   // try again once the index is there
        return;
    }
    playlistPath_ = playlistFile(*r, screen.param("playlist"));
    if (!playlistPath_.empty()) {
        Library::LineList list(playlistPath_);
        list.load();
        entries_ = list.entries();
    }
}

bool SystemProvider::inPlaylist(const Screen& screen)
{
    ensurePlaylist(screen);
    std::string own = screen.param("id");
    IndexPtr idx = index();
    const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
    if (r) {
        own = r->name;
    }
    std::string source = screen.param("source").empty() ? own : screen.param("source");
    for (const std::string& entry : entries_) {
        Library::LineList::Qualified q = Library::LineList::parseQualified(entry, own);
        if (q.collection == source && q.name == screen.param("game")) {
            return true;
        }
    }
    return false;
}

Forms::Rows SystemProvider::playlistRows(const Screen& screen)
{
    Forms::Rows rows;
    IndexPtr idx = index();
    const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
    if (!r) {
        return rows;
    }
    // The folder as it is now (a playlist made or renamed a moment ago isn't in
    // the index yet), read again when something changes
    std::string key = r->name + "\n" + std::to_string(generation());
    if (key != playlistsKey_) {
        playlistsKey_ = key;
        playlistsRows_.clear();
        std::error_code ec;
        std::vector<std::pair<std::string, std::string>> files;   // stem, path
        for (const auto& entry : fs::directory_iterator(Utils::combinePath(r->folder, "playlists"), ec)) {
            std::string file;
            if (entry.is_regular_file(ec) && Library::narrowName(entry.path().filename(), file) && file.size() > 4
                && file.compare(file.size() - 4, 4, ".txt") == 0) {
                files.emplace_back(file.substr(0, file.size() - 4), Library::narrow(entry.path()));
            }
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return Library::toLower(a.first) < Library::toLower(b.first); });
        for (const auto& [name, path] : files) {
            Library::LineList list(path);
            list.load();
            size_t entries = list.size();
            Forms::Row row;
            row.id = name;
            row.label = name;
            row.detail = count(entries, "entry", "entries");
            if (Library::Editor::isReservedPlaylist(name)) {
                row.detail = "Kept by RetroFE, " + row.detail;
            }
            else if (!shownByCycle(*r, name)) {
                row.detail += ", hidden by cyclePlaylist";
            }
            else if (entries == 0) {
                row.detail = "Empty, so not shown";
            }
            row.params = { { "id", r->name } };
            playlistsRows_.push_back(std::move(row));
        }
    }
    rows.items = playlistsRows_;
    rows.total = rows.items.size();
    return rows;
}

std::string SystemProvider::playlistShowNote(const Index& index, const CollectionRecord& record, const std::string& playlist,
                                             size_t entries) const
{
    if (Library::Editor::isReservedPlaylist(playlist)) {
        return "";
    }
    std::string note;
    if (!shownByCycle(record, playlist)) {
        note = "It isn't in cyclePlaylist (" + join(record.cyclePlaylist, ", ") + "), so RetroFE doesn't show it. "
             + (canAppendCycle(index, record) ? "Show it in RetroFE adds it there."
                                              : "Add it to cyclePlaylist in RetroFE's settings.conf to show it.");
    }
    else if (entries == 0) {
        note = "RetroFE shows a playlist once it has " + items_ + " in it.";
    }
    if (isSortName(playlist)) {
        note += std::string(note.empty() ? "" : " ") + "RetroFE sorts a playlist called " + playlist + " by " + playlist + ".";
    }
    return note;
}

bool SystemProvider::canAppendCycle(const Index& index, const CollectionRecord& record)
{
    // The collection's own cyclePlaylist can be added to; it's ignored for
    // firstCollection while the global one is set
    bool global = !index.properties.getOr("cyclePlaylist").empty() && index.properties.getOr("firstCollection") == record.name;
    return !global && record.name != "Favorites";
}

Forms::Rows SystemProvider::entryRows(const Screen& screen)
{
    ensurePlaylist(screen);
    Forms::Rows rows;
    IndexPtr idx = index();
    const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
    if (!r) {
        return rows;
    }
    for (const std::string& entry : entries_) {
        Library::LineList::Qualified q = Library::LineList::parseQualified(entry, r->name);
        Forms::Row row;
        row.id = q.name;
        row.params = { { "id", r->name }, { "playlist", screen.param("playlist") } };
        if (q.collection != r->name) {
            row.params["source"] = q.collection;
        }
        if (q.name == "*") {
            row.label = "Every " + item_ + " of " + q.collection;
        }
        else {
            int game = idx->findGame(q.collection, q.name);
            row.label = game >= 0 ? idx->games[static_cast<size_t>(game)].title : q.name;
            row.detail = q.collection != r->name ? q.collection : game < 0 ? "Not found" : row.label != q.name ? q.name : "";
        }
        rows.items.push_back(std::move(row));
    }
    rows.total = rows.items.size();
    return rows;
}

Forms::Rows SystemProvider::candidateRows(const Screen& screen, size_t limit)
{
    ensurePlaylist(screen);
    Forms::Rows rows;
    IndexPtr idx = index();
    const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
    if (!r) {
        return rows;
    }
    Library::GameQuery query;
    query.text = playlistQuery_;
    query.collection = r->name;
    query.limit = limit;
    const Library::Results<int>& results = candidates_.games(idx, query);
    std::set<std::string> listed;
    for (const std::string& entry : entries_) {
        Library::LineList::Qualified q = Library::LineList::parseQualified(entry, r->name);
        if (q.collection == r->name) {
            listed.insert(q.name);
        }
    }
    for (int g : results.rows) {
        const Library::GameRecord& game = idx->games[static_cast<size_t>(g)];
        if (game.collection < 0) {
            continue;
        }
        const std::string& source = idx->collections[static_cast<size_t>(game.collection)].name;
        Forms::Row row;
        row.id = game.name;
        row.label = game.title;
        row.detail = source == r->name && listed.count(game.name) ? "In the playlist" : game.title != game.name ? game.name : "";
        row.params = { { "id", r->name }, { "playlist", screen.param("playlist") } };
        if (source != r->name) {
            row.params["source"] = source;
        }
        rows.items.push_back(std::move(row));
    }
    rows.total = results.total;
    return rows;
}

// --- The systems list -----------------------------------------------------------------------

Forms::Rows SystemProvider::listRows(size_t limit)
{
    Forms::Rows rows;
    IndexPtr idx = index();
    if (!idx) {
        return rows;
    }
    std::string key = query_ + "\n" + menu_;
    if (idx != listIndex_ || key != listKey_) {
        listIndex_ = idx;
        listKey_ = key;
        listRows_.clear();
        Library::CollectionQuery query;
        query.text = query_;
        query.kinds = Library::KindSystem;
        query.under = menu_;
        query.limit = idx->collections.size() + 1;
        for (int c : search_.collections(idx, query).rows) {
            const CollectionRecord& r = idx->collections[static_cast<size_t>(c)];
            if (!belongs(r)) {
                continue;
            }
            Forms::Row row;
            row.id = r.name;
            row.label = r.title != r.name ? r.name + " (" + r.title + ")" : r.name;
            std::string launcher = videos_ && playsHere(r) ? "plays in RetroFE"
                                 : r.launcher.via == Library::LauncherVia::None ? "no launcher"
                                 : r.launcher.via == Library::LauncherVia::CollectionFile ? "own launcher" : r.launcher.name;
            row.detail = count(r.gameCount, item_, items_) + "  -  " + launcher;
            row.severity = !(videos_ && playsHere(r)) && r.launcher.launcher < 0 ? Forms::Severity::Warning : Forms::Severity::None;
            listRows_.push_back(std::move(row));
        }
    }
    rows.total = listRows_.size();
    rows.items.assign(listRows_.begin(), listRows_.begin() + static_cast<std::ptrdiff_t>(std::min(limit, listRows_.size())));
    return rows;
}

// --- Bindings ------------------------------------------------------------------------------

std::string SystemProvider::get(const std::string& key, const Screen& screen)
{
    if (key == "importFrom") return importFolder_;
    if (key == "importSubfolders") return yesNo(importSubfolders_);
    if (key == "importRunning") return yesNo(import_.status().running);
    if (key == "importCount") {
        auto state = import_.status();
        if (importFolder_.empty()) return "";
        if (state.running && !state.copying) return "Counting games...";
        return count(state.found, "game found", "games found");
    }
    if (key == "importMessage") return import_.status().message;
    if (key == "importInto") return importRequest(screen).destinationLabel;
    if (key == "importLayout") return importRequest(screen).hierarchy
        ? "Subfolders are kept. Leaving this screen stops copying and keeps finished files."
        : "Games are put in one folder because this system does not read subfolders. Companion paths are kept. Leaving this screen stops copying and keeps finished files.";
    if (key == "status") return status(screen);
    if (key == "screenTitle") return screenTitle(screen);
    if (key == "query") return query_;
    if (key == "menu") return menu_;

    // Edit buffers
    static const std::set<std::string> settingKeys = { "extensions", "menuSort", "includeMissing", "metadataType" };
    static const std::set<std::string> infoKeys = { "infoTitle", "manufacturer", "year", "genre", "isVideo" };
    if (settingKeys.count(key) || key == "settingsDirty") {
        syncBuffers(screen);
        return key == "settingsDirty" ? yesNo(settings_.edits.isDirty()) : settings_.edits.get(key);
    }
    if (key == "launcher" || key == "launcherDirty" || key == "play") {
        syncBuffers(screen);
        return key == "launcherDirty" ? yesNo(launcher_.edits.isDirty()) : launcher_.edits.get(key);
    }
    if (key == "usesLauncher") {
        // Systems always do; a video collection unless it plays in RetroFE (as
        // chosen on the Playback screen, before it's saved)
        if (!videos_) return "yes";
        if (isForm(screen, "-launcher")) {
            syncBuffers(screen);
            return yesNo(!iequals(launcher_.edits.get("play"), VideoCollections::playHere));
        }
        IndexPtr idx = index();
        const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
        return yesNo(!r || !playsHere(*r));
    }
    if (key == "addPlay") {
        auto found = add_.find(key);
        return found != add_.end() ? found->second : std::string(VideoCollections::playHere);
    }
    if (key == "addUsesLauncher") return yesNo(addUsesLauncher());
    if (infoKeys.count(key) || key == "infoDirty") {
        syncBuffers(screen);
        return key == "infoDirty" ? yesNo(info_.edits.isDirty()) : info_.edits.get(key);
    }

    // Media
    if (key == "mediaFile") return mediaFile_;
    if (key.compare(0, 6, "medium") == 0 || key == "isImageSlot" || key == "isVideoSlot" || key == "isTextSlot"
        || key == "hasOwnMedia" || key == "canRestoreMedia" || key == "hasMediaFolder" || key.compare(0, 10, "folderFile") == 0) {
        const Library::MediaSlot* s = slot(screen);
        if (!s) {
            return key.compare(0, 2, "is") == 0 || key.compare(0, 3, "has") == 0 || key.compare(0, 3, "can") == 0 ? "no" : "";
        }
        if (key.compare(0, 10, "folderFile") == 0) {
            // One file of the slot's media folder (param file)
            std::string name = screen.param("file");
            std::string file = s->folder.empty() ? "" : Utils::combinePath(s->folder, name);
            bool there = std::find(s->folderFiles.begin(), s->folderFiles.end(), file) != s->folderFiles.end();
            if (key == "folderFileName") return name;
            if (key == "folderFileFolder") return s->folder;
            if (key == "folderFileImage") return there && s->kind == Library::MediaKind::Image ? file : "";
            if (key == "folderFileNote") {
                if (!there) return "This file isn't in the folder any more.";
                if (s->fileBesideFolder()) return "A file beside the folder (" + fileName(s->file) + ") wins, so RetroFE doesn't show this one.";
                return "One of the folder's " + std::to_string(s->folderFiles.size()) + " files: RetroFE shows one of them at random"
                    + (s->kind == Library::MediaKind::Video ? " (a video: it plays on the page, not here)." : ".");
            }
            return "";
        }
        if (key == "mediumImage") return s->kind == Library::MediaKind::Image ? s->file : "";
        if (key == "mediumFile") {
            if (s->file.empty()) return "Nothing";
            std::string shown = s->fromFolder() ? Library::mediaShownText(*s) : fileName(s->file);
            return shown + (s->kind == Library::MediaKind::Video ? " (a video: it plays on the page, not here)" : "");
        }
        if (key == "mediumWhere") {
            if (s->file.empty()) return "";
            if (s->own) return s->fromFolder() ? "Its own folder, " + s->folder : "Its own file";
            return "Not its own: it's the " + s->fallback + " (" + (s->fromFolder() ? fs::path(s->file).parent_path().string() : s->file) + ")";
        }
        if (key == "mediumFolderNote") return Library::mediaFolderNote(*s);
        if (key == "hasMediaFolder") return yesNo(!s->folder.empty());
        if (key == "mediumFolder") return s->directory;
        if (key == "isImageSlot") return yesNo(s->kind == Library::MediaKind::Image);
        if (key == "isVideoSlot") return yesNo(s->kind == Library::MediaKind::Video);
        if (key == "isTextSlot") return yesNo(s->kind == Library::MediaKind::Text);
        if (key == "hasOwnMedia") return yesNo(s->own);   // a folder of its own counts
        if (key == "canRestoreMedia") return yesNo(originals_.count(s->type) > 0);
        return "";
    }

    // Playlists
    if (key == "newPlaylist") return newPlaylist_;
    if (key == "playlistQuery") return playlistQuery_;
    if (key == "playlistName") {
        syncBuffers(screen);
        return playlistName_.edits.get("playlistName");
    }
    if (key == "canEditPlaylist") {
        return yesNo(!Library::Editor::isReservedPlaylist(screen.param("playlist")));
    }
    if (key == "playlistNote") {
        if (Library::Editor::isReservedPlaylist(screen.param("playlist"))) {
            return "RetroFE keeps this list itself, so it's shown here but not changed.";
        }
        ensurePlaylist(screen);
        if (playlistPath_.empty()) {
            return "";
        }
        IndexPtr idx = index();
        const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
        std::string show = r ? playlistShowNote(*idx, *r, screen.param("playlist"), entries_.size()) : "";
        return count(entries_.size(), "entry", "entries") + ", in " + playlistPath_ + "." + (show.empty() ? "" : " " + show);
    }
    if (key == "canShowPlaylist") {
        IndexPtr idx = index();
        const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
        std::string playlist = screen.param("playlist");
        return yesNo(r && !Library::Editor::isReservedPlaylist(playlist) && !shownByCycle(*r, playlist) && canAppendCycle(*idx, *r));
    }
    if (key == "isInPlaylist") return yesNo(inPlaylist(screen));
    if (key == "entryTitle" || key == "entryName" || key == "entrySystem" || key == "entryPlaylist" || key == "entryNote") {
        IndexPtr idx = index();
        std::string source = screen.param("source").empty() ? screen.param("id") : screen.param("source");
        if (key == "entryTitle") {
            int game = idx ? idx->findGame(source, screen.param("game")) : -1;
            return game >= 0 ? idx->games[static_cast<size_t>(game)].title : screen.param("game");
        }
        if (key == "entryName") return screen.param("game");
        if (key == "entrySystem") return screen.param("source");
        if (key == "entryPlaylist") return screen.param("playlist");
        if (Library::Editor::isReservedPlaylist(screen.param("playlist"))) {
            return "RetroFE keeps this list itself, so it isn't changed here.";
        }
        return screen.param("game") == "*" ? "This line lists every " + item_ + " of " + source + "." : "";
    }

    if (key == "hasFirstEmptySystem" || key == "hasSystems") {
        IndexPtr idx = index();
        const CollectionRecord* only = nullptr;
        if (idx) for (const auto& r : idx->collections) {
            if (r.reserved || !r.is(Library::KindSystem) || isVideoCollection(r)) continue;
            if (only) return key == "hasSystems" ? "yes" : "no";
            only = &r;
        }
        return yesNo(only && (key == "hasSystems" || only->gameCount == 0));
    }

    // Adding
    if (key == "addMenu") {
        auto found = add_.find(key);
        if (found != add_.end()) return found->second;
        IndexPtr idx = index();
        return idx ? defaultMenu(*idx) : "";
    }
    if (!videos_ && key.compare(0, 3, "add") == 0) syncAddLaunchers();
    if (key == "addHasLaunchers") return yesNo(!addLauncherIds_.empty());
    if (key == "addLauncher") {
        auto found = add_.find(key);
        if (found != add_.end()) return found->second;
        IndexPtr idx = index();
        if (videos_) return idx && idx->findLauncher(videoPlayerLauncher) >= 0 ? videoPlayerLauncher : "";
        if (idx) for (const auto& def : idx->launchers) {
            if (def.scope == Library::LauncherScope::Global) return def.id;
        }
        return "";
    }
    if (!videos_ && (key == "addName" || key == "addExtensions")) suggestLauncher();
    if (key == "addLauncherNote") {
        IndexPtr idx = index();
        bool any = idx && std::any_of(idx->launchers.begin(), idx->launchers.end(), [](const auto& def) {
            return def.scope == Library::LauncherScope::Global;
        });
        return any ? "" : "A system needs a launcher first.";
    }
    if (key == "addIsNewLauncher") return yesNo(addIsNewLauncher());
    if (key == "addFolderCount") return addFolderCount();
    if (key.compare(0, 3, "add") == 0) {
        auto found = add_.find(key);
        return found != add_.end() ? found->second : "";
    }

    // Everything else is about the record: details, check, launcher notes
    return detailsValue(key, screen);
}

void SystemProvider::set(const std::string& key, const std::string& value, const Screen& screen)
{
    if (key == "importFrom" || key == "importSubfolders") {
        updateImport();
        if (import_.status().running && import_.status().copying) return;
        if (key == "importFrom") importFolder_ = value;
        else importSubfolders_ = iequals(value, "yes") || iequals(value, "true");
        import_.start(importRequest(screen), false);
        return;
    }
    if (key == "query") { query_ = value; return; }
    if (key == "menu") { menu_ = value; return; }
    if (key == "extensions" || key == "menuSort" || key == "includeMissing" || key == "metadataType") {
        syncBuffers(screen);
        settings_.edits.set(key, value);
        return;
    }
    if (key == "launcher" || (key == "play" && videos_)) {
        syncBuffers(screen);
        launcher_.edits.set(key, value);
        return;
    }
    if (key == "infoTitle" || key == "manufacturer" || key == "year" || key == "genre" || key == "isVideo") {
        syncBuffers(screen);
        info_.edits.set(key, value);
        return;
    }
    if (key == "mediaFile") { mediaFile_ = Utils::trimEnds(value); return; }
    if (key == "newPlaylist") { newPlaylist_ = value; return; }
    if (key == "playlistQuery") { playlistQuery_ = value; return; }
    if (key == "playlistName") {
        syncBuffers(screen);
        playlistName_.edits.set(key, value);
        return;
    }
    if (key.compare(0, 3, "add") == 0) {
        if (key == "addProgram" && videos_) {
            // A video player's usual arguments, unless the user typed their own
            std::string arguments = add_["addArguments"];
            if (arguments.empty() || arguments == "\"%ITEM_FILEPATH%\"" || arguments == playerArguments(add_["addProgram"])) {
                add_["addArguments"] = playerArguments(value);
            }
        }
        add_[key] = value;
        if (!videos_ && (key == "addName" || key == "addExtensions")) addTyped_[key] = true;
        if (!videos_ && key == "addLauncher") suggestLauncher();
    }
}

void SystemProvider::setDefault(const std::string& key, const std::string& value, const Screen&)
{
    if (key.compare(0, 3, "add") == 0 && add_.find(key) == add_.end()) {
        add_[key] = value;
    }
}

Forms::Rows SystemProvider::rows(const std::string& key, size_t limit, const Screen& screen)
{
    if (key == "firstGames") {
        Forms::Rows rows;
        IndexPtr idx = index();
        if (get("hasFirstEmptySystem", screen) == "yes" && idx) for (const auto& r : idx->collections) {
            if (r.reserved || !r.is(Library::KindSystem) || isVideoCollection(r)) continue;
            Forms::Row row;
            row.id = r.name;
            row.label = "Add games to " + r.name;
            rows.items.push_back(row);
            rows.total = 1;
        }
        return rows;
    }
    if (key == "search") return listRows(limit);
    if (key == "playlists") return playlistRows(screen);
    if (key == "entries") return entryRows(screen);
    if (key == "candidates") return candidateRows(screen, limit);
    Forms::Rows rows;
    IndexPtr idx = index();
    const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
    if (!r) {
        return rows;
    }
    if (key == "games") {
        // One row that opens the Games screen for this system (param collection)
        Forms::Row row;
        row.id = r->name;
        row.label = capitalised(items_);
        row.detail = count(r->gameCount, item_, items_);
        row.link = true;   // drawn like the links below it: it opens a screen
        rows.items.push_back(std::move(row));
    }
    else if (key == "folderFiles") {
        // The files of the slot's own media folder, each opening its screen
        const Library::MediaSlot* s = slot(screen);
        for (size_t i = 0; s && i < s->folderFiles.size() && rows.items.size() < limit; i++) {
            Forms::Row row;
            row.id = fileName(s->folderFiles[i]);
            row.label = row.id;
            row.detail = s->fileBesideFolder() ? "not shown" : "";
            row.image = s->kind == Library::MediaKind::Image ? s->folderFiles[i] : "";
            row.params = { { "id", r->name }, { "slot", s->type } };
            rows.items.push_back(std::move(row));
        }
        rows.total = s ? s->folderFiles.size() : 0;
        return rows;
    }
    else if (key == "media") {
        ensureMedia(screen);
        for (const Library::MediaSlot& s : slots_) {
            Forms::Row row;
            row.id = s.type;
            row.label = capitalised(s.type);
            row.detail = s.file.empty() ? "None" : s.own ? Library::mediaShownText(s) : "From the " + s.fallback;
            row.image = s.kind == Library::MediaKind::Image ? s.file : "";
            row.params = { { "id", r->name } };
            rows.items.push_back(std::move(row));
        }
    }
    rows.total = rows.items.size();
    return rows;
}

std::vector<std::pair<std::string, std::string>> SystemProvider::options(const std::string& key, const Screen& screen)
{
    std::vector<std::pair<std::string, std::string>> result;
    if (key == "plays") {
        result.emplace_back(VideoCollections::playHere, "In RetroFE");
        result.emplace_back(VideoCollections::playLauncher, "With an external player");
        return result;
    }
    IndexPtr idx = index();
    if (!idx) {
        return result;
    }
    if (key == "menus" || key == "addMenus") {
        std::string top = defaultMenu(*idx);
        int topIndex = idx->findCollection(top);
        if (key == "menus") {
            result.emplace_back("", "Every menu");
        }
        else if (topIndex < 0 || canListIn(idx->collections[static_cast<size_t>(topIndex)])) {
            result.emplace_back(top, top);   // may not exist yet: adding makes it
        }
        for (const CollectionRecord& r : idx->collections) {
            if (key == "menus" ? r.is(Library::KindMenu) && !r.reserved : canListIn(r) && !iequals(r.name, top)) {
                result.emplace_back(r.name, r.name);
            }
        }
    }
    else if (key == "metadataTypes") {
        syncBuffers(screen);
        for (const std::string& type : metadataTypes_) {
            result.emplace_back(type, type);
        }
    }
    else if (key == "launchers") {
        const CollectionRecord* r = record(*idx, screen);
        for (const Library::LauncherDef& def : idx->launchers) {
            if (def.scope == Library::LauncherScope::Global) {
                result.emplace_back(def.id, def.id);
            }
            else if (def.scope == Library::LauncherScope::Local && r && def.collection == r->name) {
                // As a collection's default, RetroFE looks a local launcher up as <collection>.<name>
                result.emplace_back(def.id, def.id + " (this " + noun_ + "'s own)");
            }
        }
        std::string current = launcher_.edits.get("launcher");
        if (!current.empty() && std::none_of(result.begin(), result.end(), [&](const auto& o) { return o.first == current; })) {
            result.emplace_back(current, current + " (not installed)");
        }
        if (current.empty()) {
            result.insert(result.begin(), { "", "None" });
        }
    }
    else if (key == "addLaunchers") {
        if (videos_) result.emplace_back("", "A new video player...");
        for (const Library::LauncherDef& def : idx->launchers) {
            if (def.scope == Library::LauncherScope::Global) {
                result.emplace_back(def.id, def.id);
            }
        }
    }
    if (key == "addLaunchers" && !videos_) {
        result.emplace_back("", "A program on the drive...");
    }
    return result;
}

// --- Actions ---------------------------------------------------------------------------------

std::string SystemProvider::actionProblem(const std::string& action, const Screen& screen)
{
    IndexPtr idx = index();
    if (!idx || !services_.editor) {
        return idx ? "Changes can't be saved here." : "Still reading your collections...";
    }
    if (action == "stopImport") return import_.status().running ? "" : "Nothing is running.";
    if (action == "copyGames" || action == "useGamesFolder") {
        const auto* r = record(*idx, screen);
        if (!r || !belongs(*r)) return "Choose a system first.";
        if (r->extensions.empty()) return "Set this system's extensions in Settings first.";
        if (import_.status().running) return "Wait for the folder count, or stop the current job.";
        if (importFolder_.empty()) return "Choose the folder your games are in.";
        if (action == "useGamesFolder") return Library::Editor::checkSetting(*idx, screen.param("id"), "list.path",
            Emulators::portablePath(root(), importFolder_, idx->options.os));
        return "";
    }
    if (action == "create") {
        return addProblem();
    }
    const CollectionRecord* r = record(*idx, screen);
    if (!r) {
        return "There's no " + noun_ + " called \"" + screen.param("id") + "\".";
    }
    if (action == "delete") {
        ensureDetails(screen);
        return deleteProblem_;
    }
    if (action == "saveSettings" || action == "resetSettings") {
        syncBuffers(screen);
        if (!settings_.edits.isDirty()) return "No changes to save.";
        if (action == "resetSettings") return "";
        for (const auto& [k, v] : settings_.edits.changes()) {
            std::string fileKey = k == "extensions" ? "list.extensions" : k == "menuSort" ? "list.menuSort"
                                : k == "includeMissing" ? "list.includeMissingItems" : "metadata.type";
            std::string value = k == "extensions" ? normaliseExtensions(v) : v;
            if (k == "extensions" && value.empty()) return "List at least one extension, e.g. " + std::string(videos_ ? "mp4" : "zip") + ".";
            std::string problem = Library::Editor::checkSetting(*idx, r->name, fileKey, value);
            if (!problem.empty()) return problem;
        }
        return "";
    }
    if (action == "saveLauncher" || action == "resetLauncher") {
        syncBuffers(screen);
        if (!launcher_.edits.isDirty()) return "No changes to save.";
        if (action == "resetLauncher") return "";
        std::map<std::string, std::string> changes = launcher_.edits.changes();
        std::string launcher = Utils::trimEnds(launcher_.edits.get("launcher"));
        bool launcherChanged = changes.count("launcher") > 0;
        if (videos_ && iequals(launcher_.edits.get("play"), VideoCollections::playHere)) {
            // RetroFE plays them: a player is optional, kept for later
            return launcherChanged && !launcher.empty() ? Library::Editor::checkSetting(*idx, r->name, "launcher", launcher) : "";
        }
        if (videos_ && !launcherChanged) {
            // Switching to the external player: the one it names must be there
            if (r->launcher.launcher >= 0) return "";
            return launcher.empty() ? "Choose the video player that plays them first."
                                    : "The video player " + launcher + " isn't installed. Choose another one first.";
        }
        if (videos_ && launcher.empty()) return "Choose the video player that plays them.";
        return Library::Editor::checkSetting(*idx, r->name, "launcher", launcher);
    }
    if (action == "saveInfo" || action == "resetInfo") {
        syncBuffers(screen);
        if (!info_.edits.isDirty()) return "No changes to save.";
        if (action == "resetInfo") return "";
        for (const auto& [k, v] : info_.edits.changes()) {
            if (Utils::trimEnds(v).empty() || k == "isVideo") continue;
            std::string problem = Library::Editor::checkSetting(*idx, r->name, k == "infoTitle" ? "title" : k, v);
            if (!problem.empty()) return problem;
        }
        return "";
    }
    if (action == "replaceMedia" || action == "clearMedia" || action == "restoreMedia") {
        const Library::MediaSlot* s = slot(screen);
        if (!s) return "Choose the art to change.";
        if (action == "replaceMedia") {
            std::error_code ec;
            if (mediaFile_.empty()) return "Choose a file first.";
            if (!fs::is_regular_file(mediaFile_, ec)) return "That file isn't there.";
            return "";
        }
        if (action == "clearMedia") {
            if (s->own && s->fromFolder()) return "It's a folder of files, not one file: take files out of it, or clear the folder.";
            return s->own ? "" : "It has no file of its own to clear.";
        }
        return originals_.count(s->type) ? "" : "Nothing was changed here.";
    }
    if (action == "addToFolder" || action == "clearFolder" || action == "removeFolderFile" || action == "keepFolderFile") {
        const Library::MediaSlot* s = slot(screen);
        if (!s) return "Choose the art to change.";
        if (s->folder.empty()) return "It has no folder of files.";
        if (action == "addToFolder") {
            std::error_code ec;
            if (mediaFile_.empty()) return "Choose a file first.";
            return fs::is_regular_file(mediaFile_, ec) ? "" : "That file isn't there.";
        }
        if (action == "clearFolder") return "";
        std::string file = Utils::combinePath(s->folder, screen.param("file"));
        return std::find(s->folderFiles.begin(), s->folderFiles.end(), file) == s->folderFiles.end()
            ? "This file isn't in the folder any more." : "";
    }
    if (action == "createPlaylist") {
        std::string name = Utils::trimEnds(newPlaylist_);
        if (name.empty()) return "Type a name for the playlist.";
        if (Library::Editor::isReservedPlaylist(name)) return "\"" + name + "\" is a list RetroFE keeps itself. Pick another name.";
        if (!playlistFile(*r, name).empty()) return r->name + " already has a playlist called \"" + name + "\".";
        return "";
    }
    if (action == "renamePlaylist" || action == "deletePlaylist" || action == "addEntry" || action == "removeEntry") {
        std::string playlist = screen.param("playlist");
        if (Library::Editor::isReservedPlaylist(playlist)) return "RetroFE keeps this list itself.";
        if (action == "renamePlaylist") {
            syncBuffers(screen);
            std::string name = Utils::trimEnds(playlistName_.edits.get("playlistName"));
            if (name.empty() || name == playlist) return "Type a new name first.";
            return "";
        }
        if (action == "addEntry") return inPlaylist(screen) ? "It's in the playlist already." : "";
        if (action == "removeEntry") return inPlaylist(screen) ? "" : "It isn't in the playlist.";
        return "";
    }
    if (action == "showPlaylist") {
        std::string playlist = screen.param("playlist");
        if (Library::Editor::isReservedPlaylist(playlist)) return "RetroFE keeps this list itself.";
        if (shownByCycle(*r, playlist)) return "RetroFE shows it already.";
        return canAppendCycle(*idx, *r) ? "" : "RetroFE's own cyclePlaylist (settings.conf) decides for " + r->name + ".";
    }
    return Provider::actionProblem(action, screen);
}

Forms::Model::Result SystemProvider::runAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) {
        return result;
    }
    if (action == "stopImport") { import_.stop(); updateImport(); result.ok = true; return result; }
    if (action == "copyGames") {
        updateImport();
        importSystem_ = screen.param("id");
        import_.start(importRequest(screen), true);
        result.ok = true;
        return result;
    }
    if (action == "useGamesFolder") {
        auto idx = index();
        auto edit = services_.editor->setCollectionSetting(*idx, screen.param("id"), "list.path",
            Emulators::portablePath(root(), importFolder_, idx->options.os));
        result.ok = edit.ok;
        result.message = edit.message;
        if (edit.ok) changed("games folder of " + screen.param("id"));
        return result;
    }
    if (action == "create") return create();
    if (action == "delete") return remove(screen);
    if (action == "saveSettings") return saveSettings(screen);
    if (action == "saveLauncher") return saveLauncher(screen);
    if (action == "saveInfo") return saveInfo(screen);
    if (action == "resetSettings" || action == "resetLauncher" || action == "resetInfo") {
        bufferFor(screen)->edits.reset();
        result.ok = true;
        result.info = true;
        return result;
    }
    if (action == "replaceMedia" || action == "clearMedia" || action == "restoreMedia" || action == "addToFolder"
        || action == "clearFolder" || action == "removeFolderFile" || action == "keepFolderFile") {
        return mediaAction(action, screen);
    }
    return playlistAction(action, screen);
}

Forms::Model::Result SystemProvider::saveSettings(const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    std::string name = record(*idx, screen)->name;
    bool wrote = false;
    for (const auto& [k, v] : settings_.edits.changes()) {
        std::string fileKey = k == "extensions" ? "list.extensions" : k == "menuSort" ? "list.menuSort"
                            : k == "includeMissing" ? "list.includeMissingItems" : "metadata.type";
        std::string value = v;
        if (k == "extensions") {
            value = normaliseExtensions(v);
        }
        else if (k == "menuSort" || k == "includeMissing") {
            bool on = Forms::isTruthy(v);
            value = booleanStyle_[fileKey] == "true" ? (on ? "true" : "false") : yesNo(on);
        }
        Library::EditResult edit = services_.editor->setCollectionSetting(*idx, name, fileKey, value);
        if (!edit.ok) {
            if (wrote) changed("settings of " + name);
            result.message = edit.message;
            return result;
        }
        wrote = true;
        if (k == "extensions") {
            settings_.edits.set("extensions", value);   // show it as written
        }
    }
    settings_.edits.commit();
    changed("settings of " + name);
    result.ok = true;
    result.message = "Saved. RetroFE uses the new settings after it reloads, when you close Manage.";
    return result;
}

Forms::Model::Result SystemProvider::saveLauncher(const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    std::string name = record(*idx, screen)->name;
    std::string launcher = Utils::trimEnds(launcher_.edits.get("launcher"));
    std::map<std::string, std::string> changes = launcher_.edits.changes();
    Library::EditResult edit{ true, "", {} };
    if (changes.count("launcher")) {
        edit = services_.editor->setCollectionSetting(*idx, name, "launcher", launcher);
        result.message = edit.message;
        if (!edit.ok) {
            return result;
        }
    }
    std::string note = edit.message.find("Note:") != std::string::npos ? edit.message.substr(edit.message.find(" Note:")) : "";
    if (!videos_) {
        launcher_.edits.commit();
        changed("launcher of " + name);
        result.ok = true;
        result.message = name + "'s " + items_ + " now start with " + launcher + ", unless one has its own launcher." + note;
        return result;
    }

    // A video collection: where it plays goes in its info.conf
    bool here = iequals(launcher_.edits.get("play"), VideoCollections::playHere);
    if (changes.count("play")) {
        Library::EditResult play = services_.editor->setCollectionSetting(*idx, name, VideoCollections::playKey,
                                                                          here ? VideoCollections::playHere : VideoCollections::playLauncher);
        if (!play.ok) {
            if (changes.count("launcher")) changed("launcher of " + name);
            result.message = play.message;
            return result;
        }
    }
    launcher_.edits.commit();
    changed("playback of " + name);
    result.ok = true;
    if (here) {
        result.message = "RetroFE plays " + name + "'s videos itself after it reloads, when you close Manage."
                       + std::string(launcher.empty() ? "" : " " + launcher + " is kept for when you choose the external player again.");
    }
    else {
        std::string player = launcher.empty() ? record(*idx, screen)->launcher.name : launcher;
        result.message = name + "'s videos play with " + player + " after RetroFE reloads, when you close Manage." + note;
    }
    return result;
}

Forms::Model::Result SystemProvider::saveInfo(const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    std::string name = record(*idx, screen)->name;
    bool wrote = false;
    bool movedList = false;
    for (const auto& [k, v] : info_.edits.changes()) {
        std::string infoKey = k == "infoTitle" ? "title" : k == "isVideo" ? "type" : k;
        std::string value = k == "isVideo" ? (Forms::isTruthy(v) ? "videos" : "") : v;
        movedList |= k == "isVideo";
        Library::EditResult edit = Utils::trimEnds(value).empty()
            ? services_.editor->removeCollectionSetting(*idx, name, infoKey)
            : services_.editor->setCollectionSetting(*idx, name, infoKey, value);
        if (!edit.ok) {
            if (wrote) changed("info of " + name);
            result.message = edit.message;
            return result;
        }
        wrote = true;
    }
    info_.edits.commit();
    changed("info of " + name);
    result.ok = true;
    result.message = "Saved in info.conf. RetroFE shows it after it reloads, when you close Manage.";
    if (movedList) {
        result.message += Forms::isTruthy(info_.edits.get("isVideo")) ? " It's listed under Videos now." : " It's listed under Systems now.";
    }
    return result;
}

Forms::Model::Result SystemProvider::mediaAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    const Library::MediaSlot* s = slot(screen);
    Library::MediaSlot target = *s;
    Library::EditResult edit;
    if (action == "replaceMedia") {
        edit = services_.editor->replaceMedia(target, mediaFile_);
    }
    else if (action == "clearMedia") {
        edit = services_.editor->clearMedia(target);
    }
    else if (action == "addToFolder") {
        edit = services_.editor->addMediaFolderFile(target, mediaFile_);
    }
    else if (action == "clearFolder") {
        edit = services_.editor->clearMediaFolder(target);
    }
    else if (action == "removeFolderFile") {
        edit = services_.editor->removeMediaFolderFile(target, screen.param("file"));
    }
    else if (action == "keepFolderFile") {
        edit = services_.editor->mediaFolderToFile(target, screen.param("file"));
    }
    else {
        edit.ok = true;
        for (const std::string& file : originals_[target.type]) {
            Library::EditResult one = services_.editor->restoreOriginal(file);
            if (!one.ok) {
                edit = one;
                break;
            }
        }
        if (edit.ok) {
            edit.message = "Put back the " + target.type + " it had before.";
        }
    }
    result.message = edit.message;
    // A failure part way can still have changed files: read the art again whenever any were touched
    if (!edit.ok && edit.files.empty()) {
        return result;
    }
    if (edit.ok) {
        mediaFile_.clear();
    }
    changed(target.type + " art of " + screen.param("id"));
    ensureMedia(screen, true);
    detailsIndex_.reset();
    if (!edit.ok) {
        return result;
    }
    result.ok = true;
    // From one of a folder's files, back to the art's screen
    result.back = action == "removeFolderFile" || action == "keepFolderFile";
    return result;
}

Forms::Model::Result SystemProvider::playlistAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    std::string name = record(*idx, screen)->name;
    std::string playlist = screen.param("playlist");
    std::string source = screen.param("source").empty() ? name : screen.param("source");
    Library::EditResult edit;
    if (action == "createPlaylist") {
        std::string newName = Utils::trimEnds(newPlaylist_);
        edit = services_.editor->createPlaylist(*idx, name, newName);
        if (edit.ok) {
            newPlaylist_.clear();
            result.open = base_ + "-playlist";
            result.params = { { "id", name }, { "playlist", newName } };
        }
    }
    else if (action == "renamePlaylist") {
        edit = services_.editor->renamePlaylist(*idx, name, playlist, Utils::trimEnds(playlistName_.edits.get("playlistName")));
        if (edit.ok) {
            playlistName_.edits.commit();
            result.back = true;
        }
    }
    else if (action == "deletePlaylist") {
        edit = services_.editor->deletePlaylist(*idx, name, playlist);
        result.back = edit.ok;
    }
    else if (action == "addEntry") {
        edit = services_.editor->addToPlaylist(*idx, name, playlist, source, screen.param("game"));
        result.back = edit.ok;
    }
    else if (action == "removeEntry") {
        edit = services_.editor->removeFromPlaylist(*idx, name, playlist, source, screen.param("game"));
        result.back = edit.ok;
    }
    else if (action == "showPlaylist") {
        // The list as the files say now (the index may not have caught up with an earlier add)
        const CollectionRecord& r = *record(*idx, screen);
        Library::ConfLayers layers(r.folder, Library::ConfLayers::collectionFileNames());
        layers.load();
        std::vector<std::string> cycle;
        if (layers.has("cyclePlaylist")) {
            Utils::listToVector(layers.get("cyclePlaylist"), cycle, ',');
        }
        else {
            cycle = r.cyclePlaylist;   // the global list, kept for the others
        }
        if (std::find(cycle.begin(), cycle.end(), playlist) == cycle.end()) {
            cycle.push_back(playlist);
            edit = services_.editor->setCollectionSetting(*idx, name, "cyclePlaylist", join(cycle, ","));
            if (edit.ok) {
                edit.message = "Added " + playlist + " to " + name + "'s cyclePlaylist; RetroFE shows it after it reloads, when you close Manage"
                             + (entries_.empty() ? ", once it has " + items_ + " in it." : ".");
            }
        }
        else {
            edit = Library::EditResult{ true, playlist + " is in cyclePlaylist already.", {} };
        }
    }
    else {
        return Provider::runAction(action, screen);
    }
    result.message = edit.message;
    if (edit.ok && (action == "createPlaylist" || action == "renamePlaylist")) {
        std::string shown = action == "createPlaylist" ? Utils::trimEnds(newPlaylist_.empty() ? result.params["playlist"] : newPlaylist_)
                                                       : Utils::trimEnds(playlistName_.edits.get("playlistName"));
        std::string note = playlistShowNote(*idx, *record(*idx, screen), shown, action == "createPlaylist" ? 0 : entries_.size());
        result.message += note.empty() ? "" : " " + note;
    }
    result.ok = edit.ok;
    result.info = edit.ok && edit.files.empty();   // e.g. "... is in cyclePlaylist already."
    if (edit.ok && !edit.files.empty()) {   // nothing written: nothing to reload
        changed("playlist " + playlist + " of " + name);
        playlistKey_.clear();   // read the file again
    }
    return result;
}

// --- Adding and deleting ---------------------------------------------------------------------

const std::string& SystemProvider::defaultMenu(const Index& index)
{
    if (menuIndex_.get() != &index) {
        IndexPtr current = this->index();
        menuIndex_ = current.get() == &index ? current : nullptr;
        defaultMenu_ = Library::MenuFiles::menuForNewCollections(root(), index.topMenu);
    }
    return defaultMenu_;
}

bool SystemProvider::canListIn(const CollectionRecord& menu)
{
    // Manage writes menu.txt or menu/<child>.txt; a menu.txt beside a menu.xml
    // would replace it, and a menu made from collectionLaunchers has no file
    return menu.is(Library::KindMenu) && !menu.reserved && menu.menuSource != Library::MenuSource::MenuXml
        && menu.menuSource != Library::MenuSource::CollectionLaunchers;
}

bool SystemProvider::addUsesLauncher() const
{
    if (!videos_) {
        return true;
    }
    auto found = add_.find("addPlay");
    return found != add_.end() && !iequals(Utils::trimEnds(found->second), VideoCollections::playHere);
}

bool SystemProvider::addIsNewLauncher() const
{
    if (!addUsesLauncher()) {
        return false;
    }
    auto found = add_.find("addLauncher");
    if (found != add_.end()) {
        return found->second.empty();
    }
    IndexPtr idx = index();
    if (!videos_) return !idx || std::none_of(idx->launchers.begin(), idx->launchers.end(), [](const auto& def) {
        return def.scope == Library::LauncherScope::Global;
    });
    return !(idx && idx->findLauncher(videoPlayerLauncher) >= 0);
}

Forms::Severity SystemProvider::severity(const std::string& key, const Screen& screen)
{
    using Forms::Severity;
    if (key == "launcherInEffect" || key == "launcherStatus") {
        // A launcher that isn't installed: its games can't start
        IndexPtr idx = index();
        const CollectionRecord* r = idx ? record(*idx, screen) : nullptr;
        if (!r || (videos_ && playsHere(*r))) return Severity::None;
        return r->launcher.launcher < 0 ? Severity::Warning : Severity::None;
    }
    if (key == "addFolderCount") {
        std::string text = addFolderCount();
        return text == "Folder not found" ? Severity::Error
             : text.rfind("No matching files", 0) == 0 ? Severity::Warning
             : Severity::None;
    }
    return Severity::None;
}

std::string SystemProvider::defaultGamesFolder()
{
    IndexPtr idx = index();
    return idx ? idx->properties.collectionPath(Utils::trimEnds(add_["addName"])) : "";
}

void SystemProvider::syncAddLaunchers()
{
    IndexPtr idx = index();
    if (!idx || addLauncherIndex_ == idx) return;
    std::vector<std::string> ids, added;
    for (const auto& def : idx->launchers) {
        if (def.scope != Library::LauncherScope::Global) continue;
        ids.push_back(def.id);
        if (std::find(addLauncherIds_.begin(), addLauncherIds_.end(), def.id) == addLauncherIds_.end()) {
            added.push_back(def.id);
        }
    }
    if (addLauncherIndex_ && added.size() == 1) add_["addLauncher"] = added.front();
    addLauncherIds_ = std::move(ids);
    addLauncherIndex_ = idx;
}

void SystemProvider::suggestLauncher()
{
    std::string launcher = get("addLauncher", Screen{});
    IndexPtr idx = index();
    if (suggestionIndex_ == idx && suggestedLauncher_ == launcher) return;
    suggestionIndex_ = idx;
    suggestedLauncher_ = launcher;
    std::string name, extensions;
    for (const auto& entry : Catalog::emulators()) {
        if (!entry.launcher.empty() && entry.launcher == launcher) {
            name = entry.system;
            extensions = entry.extensions;
        }
    }
    std::string catalog;
    if (!idx || !idx->properties.getAbsolutePath("catalogPath", catalog) || catalog.empty()) {
        catalog = Utils::combinePath(root(), "catalog");
    }
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(fs::path(catalog) / "installed", ec)) {
        Catalog::Manifest manifest;
        std::string error;
        if (!Catalog::readManifest(file.path().string(), manifest, error) || manifest.kind != "core"
            || !Catalog::isCoreId(manifest.id)) continue;
        std::string coreLauncher = manifest.launcherName.empty() ? Catalog::coreLauncherName(manifest.name) : manifest.launcherName;
        if (coreLauncher != launcher) continue;
        auto info = Library::RetroArch::readConfigFile(Utils::combinePath(root(), "emulators/RetroArch/info",
                                                       manifest.id.substr(5) + "_libretro.info"));
        name = info["systemname"];
        extensions = info["supported_extensions"];
        std::replace(extensions.begin(), extensions.end(), '|', ',');
        break;
    }
    if (!addTyped_["addName"]) add_["addName"] = name;
    if (!addTyped_["addExtensions"]) add_["addExtensions"] = extensions;
}

std::string SystemProvider::addFolderCount()
{
    std::string folder = Utils::trimEnds(add_["addFolder"]);
    if (folder.empty()) {
        return videos_ ? "" : defaultGamesFolder() + ", made when you add it";
    }
    std::string extensions = normaliseExtensions(add_["addExtensions"]);
    std::string key = folder + "\n" + extensions;
    if (key == countedKey_) {
        return countText_;
    }
    countedKey_ = key;
    countText_.clear();
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        countText_ = "Folder not found";
        return countText_;
    }
    std::vector<std::string> wanted;
    Utils::listToVector(extensions, wanted, ',');
    size_t exact = 0;
    size_t caseOnly = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        std::string name;
        if (!entry.is_regular_file(ec) || !Library::narrowName(entry.path().filename(), name)) {
            continue;
        }
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) {
            continue;
        }
        std::string extension = name.substr(dot + 1);
        if (std::find(wanted.begin(), wanted.end(), extension) != wanted.end()) {
            exact++;
        }
        else if (std::any_of(wanted.begin(), wanted.end(), [&](const std::string& w) { return iequals(w, extension); })) {
            caseOnly++;
        }
    }
    countText_ = exact == 0 ? "No matching files" : count(exact, "matching file", "matching files");
    if (caseOnly > 0) {
        countText_ += " (" + count(caseOnly, "more differs", "more differ") + " only in the case of its extension; RetroFE skips those)";
    }
    return countText_;
}

std::string SystemProvider::addProblem()
{
    IndexPtr idx = index();
    std::string name = Utils::trimEnds(add_["addName"]);
    if (name.empty()) {
        return "Give the " + noun_ + " a name.";
    }
    std::string problem = Library::MenuFiles::checkChildName(name);
    if (!problem.empty()) {
        return "That name won't work: " + problem;
    }
    if (Library::isReservedCollection(name)) {
        return "\"" + name + "\" is a name RetroFE uses itself. Pick another one.";
    }
    std::error_code ec;
    int existing = idx->findCollection(name);
    if (existing >= 0 || fs::exists(Utils::combinePath(root(), "collections", name), ec)) {
        return "There's already a collection called \"" + (existing >= 0 ? idx->collections[static_cast<size_t>(existing)].name : name) + "\".";
    }
    std::string menu = get("addMenu", Screen{});
    if (menu.empty()) {
        return "Choose the menu to list it in.";
    }
    int menuIndex = idx->findCollection(menu);
    if (menuIndex >= 0) {
        const CollectionRecord& listing = idx->collections[static_cast<size_t>(menuIndex)];
        if (!canListIn(listing) && !(menu == defaultMenu(*idx) && !listing.is(Library::KindMenu))) {
            return listing.menuSource == Library::MenuSource::MenuXml || listing.menuSource == Library::MenuSource::CollectionLaunchers
                ? menu + "'s list comes from " + (listing.menuSource == Library::MenuSource::MenuXml ? "menu.xml" : "collectionLaunchers")
                      + ", which Manage doesn't change. Choose another menu."
                : menu + " isn't a menu.";
        }
    }
    else if (menu != defaultMenu(*idx)) {
        return menu + " isn't a menu.";
    }
    std::string folder = Utils::trimEnds(add_["addFolder"]);
    if ((videos_ && folder.empty()) || (!folder.empty() && !fs::is_directory(folder, ec))) {
        return "Choose the folder your " + items_ + " are in.";
    }
    std::string extensions = normaliseExtensions(add_["addExtensions"]);
    if (extensions.empty()) {
        return std::string("List the file extensions to look for, e.g. ") + (videos_ ? "mp4,mkv,avi." : "zip,7z.");
    }
    std::vector<std::string> parts;
    Utils::listToVector(extensions, parts, ',');
    for (const std::string& part : parts) {
        if (!validExtension(part)) {
            return "\"" + part + "\" isn't a file extension.";
        }
    }
    if (!addUsesLauncher()) {
        // RetroFE plays them: only the files it can open
        std::vector<std::string> unplayable;
        for (const std::string& part : parts) {
            if (!VideoCollections::isPlayableExtension(part)) {
                unplayable.push_back(part);
            }
        }
        if (!unplayable.empty()) {
            return "RetroFE plays " + join(VideoCollections::playableExtensions(), ", ") + " files itself, not "
                 + join(unplayable, ", ") + ". Leave " + (unplayable.size() == 1 ? "it" : "them")
                 + " out, or choose With an external player.";
        }
    }
    else if (addIsNewLauncher()) {
        std::string program = Utils::trimEnds(add_["addProgram"]);
        if (program.empty() || !fs::is_regular_file(program, ec)) {
            return videos_ ? "Choose the video player program, e.g. vlc.exe or mpv.exe."
                           : "Choose the emulator program to launch the games with.";
        }
        if (program.find('#') != std::string::npos || add_["addArguments"].find('#') != std::string::npos) {
            return "Paths and arguments can't contain #.";
        }
    }
    else if (idx->findLauncher(get("addLauncher", Screen{})) < 0) {
        return "There's no launcher called \"" + get("addLauncher", Screen{}) + "\".";
    }
    if (folder.find('#') != std::string::npos) {
        return "Paths can't contain #.";
    }
    return "";
}

Forms::Model::Result SystemProvider::create()
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    Library::Backups& backups = services_.editor->backups();
    const std::string nl = Library::defaultNewline();
    const std::string os = Utils::getOSType();
    std::string name = Utils::trimEnds(add_["addName"]);
    std::string menu = get("addMenu", Screen{});
    std::string folder = Utils::trimEnds(add_["addFolder"]);
    std::string extensions = normaliseExtensions(add_["addExtensions"]);
    std::string program = Utils::trimEnds(add_["addProgram"]);
    std::string arguments = Utils::trimEnds(add_["addArguments"]);
    if (arguments.empty()) {
        arguments = videos_ ? playerArguments(program) : "\"%ITEM_FILEPATH%\"";
    }
    std::string collection = Utils::combinePath(root(), "collections", name);
    std::string error;

    std::string defaultFolder = defaultGamesFolder();
    if (!videos_ && folder.empty()) folder = defaultFolder;
    if (!videos_) {
        std::error_code ec;
        fs::create_directories(folder, ec);
        if (ec) { result.message = "Could not make the games folder: " + ec.message(); return result; }
        program = Emulators::portablePath(root(), program, os);
    }
    std::string settings = std::string(createdHeader) + nl
        + (videos_ || fs::path(folder).lexically_normal() != fs::path(defaultFolder).lexically_normal() ? "list.path = " + folder + nl : "")
        + "list.extensions = " + extensions + nl
        + "list.includeMissingItems = false" + nl
        + "list.menuSort = yes" + nl;
    std::string launcherNote;
    std::string launcherConf = std::string(createdHeader) + nl + "executable = " + program + nl + "arguments = " + arguments + nl;
    std::string newLauncher;   // a new global launcher file, to take back if adding fails
    std::string ownLauncher;   // or the collection's own launcher file
    bool here = !addUsesLauncher();
    if (here) {
        launcherNote = "RetroFE plays its videos itself; Playback can add an external player.";
    }
    else if (!addIsNewLauncher()) {
        std::string launcher = get("addLauncher", Screen{});
        settings += "launcher = " + launcher + nl;
        launcherNote = "It starts its " + items_ + " with " + launcher + ".";
    }
    else if (videos_) {
        // A global launcher any video collection can use: VideoPlayer, or VideoPlayer2... if that's taken
        std::string launcher = videoPlayerLauncher;
        std::error_code ec;
        for (int n = 2; idx->findLauncher(launcher) >= 0
             || fs::exists(Utils::combinePath(root(), "launchers." + os, launcher + ".conf"), ec); n++) {
            launcher = std::string(videoPlayerLauncher) + std::to_string(n);
        }
        newLauncher = Utils::combinePath(root(), "launchers." + os, launcher + ".conf");
        settings += "launcher = " + launcher + nl;
        launcherNote = "It plays its videos with the new launcher " + launcher + " (" + fileName(program) + ").";
    }
    else {
        // As Setup does: the collection's own launcher file
        ownLauncher = Utils::combinePath(collection, "launcher." + os + ".conf");
        launcherNote = "It starts its games with " + fileName(program) + ".";
    }
    bool wroteLauncher = false;
    if (!(newLauncher.empty() || (wroteLauncher = writeNew(backups, newLauncher, launcherConf, error)))
        || !(ownLauncher.empty() || writeNew(backups, ownLauncher, launcherConf, error))
        || !writeNew(backups, Utils::combinePath(collection, "settings.conf"), settings, error)
        || (videos_ && !writeNew(backups, Utils::combinePath(collection, "info.conf"),
                                 std::string("type = videos") + nl + "play = " + (here ? VideoCollections::playHere : VideoCollections::playLauncher) + nl, error))
        || !Library::MenuFiles::add(root(), menu, name, -1, &backups, error)) {
        // Nothing half made is left behind: the new folder (and a new player
        // launcher) go to the backups, so RetroFE never sees a system no menu lists
        std::string ignored;
        std::string kept;
        std::error_code ec;
        if (fs::exists(collection, ec)) {
            moveAside(backups, collection, name, kept, ignored);
        }
        if (wroteLauncher) {
            backups.removeKeepingOriginal(newLauncher, ignored);
        }
        result.message = (error.empty() ? "Could not add " + name + "." : error) + " Nothing was added.";
        return result;
    }
    changed("systems: " + name);
    addTyped_.clear();
    suggestionIndex_.reset();
    for (const char* key : { "addName", "addFolder", "addProgram" }) {
        add_.erase(key);
    }
    countedKey_ = "\x01";
    result.ok = true;
    result.open = base_;
    result.replace = true;   // Back from the new system goes to the list, not the filled-in add form
    result.params = { { "id", name } };
    result.message = "Added " + name + " to " + menu + ". " + launcherNote
                   + " RetroFE shows it after it reloads, when you close Manage.";
    return result;
}

Forms::Model::Result SystemProvider::remove(const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    const CollectionRecord& r = *record(*idx, screen);
    std::string name = r.name;
    // The folder goes first, in one rename when it can
    std::string error;
    std::string kept;
    if (!moveAside(services_.editor->backups(), r.folder, name, kept, error)) {
        result.message = "Couldn't move " + name + "'s folder into manage/backups (" + error + "). Nothing was changed.";
        return result;
    }
    // Then its menu lines; if one can't go, everything is put back as it was
    std::vector<std::pair<std::string, int>> unlisted;
    for (int parent : r.parents) {
        const CollectionRecord& menu = idx->collections[static_cast<size_t>(parent)];
        int position = -1;
        for (size_t i = 0; i < menu.children.size(); i++) {
            if (position < 0 && menu.children[i].collection == static_cast<int>(idx->findCollection(name))) {
                position = static_cast<int>(i);
            }
        }
        Library::EditResult edit = services_.editor->removeMenuChild(*idx, menu.name, name);
        if (!edit.ok) {
            std::string ignored;
            bool back = moveTree(kept, r.folder, ignored);
            for (const auto& [listed, at] : unlisted) {
                services_.editor->addMenuChild(*idx, listed, name, at);
            }
            changed("systems: " + name);
            result.message = edit.message + (back ? " Nothing was changed." : " Its folder is in " + kept + ".");
            return result;
        }
        unlisted.emplace_back(menu.name, position);
    }
    changed("systems: " + name);

    std::vector<std::string> folders;
    for (const std::string& folder : r.listPaths) {
        if (!folder.empty()) folders.push_back(folder);
    }
    result.ok = true;
    result.back = true;
    result.message = "Deleted " + name + "."
        + (folders.empty() ? "" : " Its " + items_ + " folder " + join(folders, "; ") + " isn't touched.")
        + " Its settings, art and playlists are kept in " + kept + "."
        + (r.launcher.via == Library::LauncherVia::Setting && r.launcher.launcher >= 0 ? " The launcher " + r.launcher.name + " stays installed." : "");
    return result;
}

GameImport::Request SystemProvider::importRequest(const Screen& screen) const
{
    GameImport::Request request;
    request.from = importFolder_;
    request.subfolders = importSubfolders_;
    auto idx = index();
    const auto* r = idx ? record(*idx, screen) : nullptr;
    if (r) {
        request.into = r->listPaths.empty() ? idx->properties.collectionPath(r->name) : r->listPaths.front();
        fs::path relative = request.into.lexically_normal().lexically_relative(fs::path(root()).lexically_normal());
        bool inside = !relative.empty();
        for (const auto& part : relative) if (part == "..") inside = false;
        request.destinationLabel = (inside ? relative : request.into).make_preferred().string();
        request.extensions = r->extensions;
        request.hierarchy = r->romHierarchy;
    }
    return request;
}

void SystemProvider::updateImport()
{
    auto state = import_.status();
    if (state.generation == importObserved_) return;
    importObserved_ = state.generation;
    changes_++;
    if (!state.running && state.changed) {
        changed("games of " + importSystem_);
    }
}

}

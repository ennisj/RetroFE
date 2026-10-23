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
#include "../../Library/Backups.h"
#include "../../Library/ConfFile.h"
#include "../../Library/Edits.h"
#include "../../Library/Files.h"
#include "../../Library/Indexer.h"
#include "../../Library/LineList.h"
#include "../../Utility/Utils.h"
#include <algorithm>
#include <cctype>
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
    // Setup's "# Created by RetroFE setup" and this screen's own header
    return content.compare(0, 20, "# Created by RetroFE") == 0;
}

bool isVideoCollection(const CollectionRecord& record)
{
    auto type = record.info.find("type");
    return type != record.info.end() && iequals(Utils::trimEnds(type->second), "videos");
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

    // How they start
    const Library::LauncherResolution& launcher = r.launcher;
    if (launcher.via == Library::LauncherVia::None) {
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
        else if (!launcher.problem.empty()) {
            add(launcher.problem);
        }
    }
    size_t broken = 0;
    for (const auto& [game, name] : r.launcherOverrides) {
        if (name && index.resolveLauncher(collection, game).launcher < 0) {
            broken++;
        }
    }
    if (broken > 0) {
        add(count(broken, item + " has a launcher", items + " have launchers") + " of its own that isn't installed (collections/"
            + r.name + "/launchers).");
    }

    // Titles and years (videos seldom have hyperlist metadata, so it's only checked for games)
    if (r.gameCount > 0 && !videos) {
        size_t withMetadata = 0;
        for (size_t g = r.gameBegin; g < r.gameBegin + r.gameCount && g < index.games.size(); g++) {
            withMetadata += index.games[g].hasMetadata ? 1 : 0;
        }
        if (withMetadata == 0) {
            std::string file = Utils::combinePath(index.options.root, "meta", "hyperlist", r.metadataType + ".xml");
            if (!fs::exists(file, ec)) {
                add("There's no metadata file meta/hyperlist/" + r.metadataType + ".xml, so its " + items
                    + " show their file names instead of titles.");
            }
            else {
                add("None of its " + items + " has metadata, though meta/hyperlist/" + r.metadataType
                    + ".xml exists: it may be unreadable, name other " + items + ", or not be imported yet.");
            }
        }
    }
    for (const std::string& problem : index.problems) {
        if (Library::toLower(problem).find("meta") != std::string::npos) {
            add("Metadata couldn't be read: " + problem);
        }
    }

    // What the library noticed (fork-only keys, spellings), except what's said above
    for (const std::string& note : r.notes) {
        if (note.find("only ignoring case") == std::string::npos) {
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
        + (check.findings.empty() ? "nothing to report" : count(check.findings.size(), "thing", "things") + " to look at")
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

void SystemProvider::refresh()
{
    listIndex_.reset();
    detailsIndex_.reset();
    mediaIndex_.reset();
    playlistKey_.clear();
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
    if (isForm(screen, "-media") || isForm(screen, "-medium")) {
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
    };
}

std::string SystemProvider::leaving(const Screen& screen)
{
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
    if (isForm(screen, "-launcher")) return name + ": launcher";
    if (isForm(screen, "-info")) return name + ": info";
    if (isForm(screen, "-media")) return name + ": media";
    if (isForm(screen, "-medium")) return name + ": " + screen.param("slot");
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
    else if (r->gameCount > 0 && !madeHere(*r)) {
        deleteProblem_ = "It has " + count(r->gameCount, item_, items_) + ". Only an empty " + noun_ + ", or one added here, can be deleted.";
    }
    for (const CollectionRecord& other : idx->collections) {
        for (const Library::SubSource& sub : other.subs) {
            if (deleteProblem_.empty() && sub.collection == c) {
                deleteProblem_ = other.name + " shows its " + items_ + " (" + sub.source + ".sub), so it can't be deleted.";
            }
        }
    }
    if (deleteProblem_.empty() && static_cast<size_t>(c) < idx->candidates.size()) {
        for (const Index::Candidate& candidate : idx->candidates[static_cast<size_t>(c)]) {
            if (Library::isInside(candidate.file, r->folder)) {
                deleteProblem_ = "Its " + items_ + " are inside its own folder, so deleting it would delete them.";
                break;
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
    if (key == "settingsStatus") return r->extensions.empty() ? "No extensions" : join(r->extensions, ", ");
    if (key == "launcherStatus") {
        return r->launcher.via == Library::LauncherVia::None ? "None"
             : r->launcher.via == Library::LauncherVia::CollectionFile ? "Its own file" : r->launcher.name;
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
        return check_.findings.empty() ? "Nothing to report" : count(check_.findings.size(), "thing", "things") + " to look at";
    }
    if (key == "canDelete") return yesNo(deleteProblem_.empty());
    if (key == "image") return image_;
    if (key == "checkSummary") return check_.summary;
    if (key.compare(0, 7, "finding") == 0) {
        size_t n = static_cast<size_t>(Utils::convertInt(key.substr(7)));
        if (n + 1 == maxFindings && check_.findings.size() > maxFindings) {
            return "... and " + count(check_.findings.size() - maxFindings + 1, "more thing", "more things") + ".";
        }
        return n < check_.findings.size() ? "- " + check_.findings[n] : "";
    }
    // Launcher screen
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
        return own == 0 ? "" : count(own, item_ + " has a launcher", items_ + " have launchers") + " of its own; change those in Games.";
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
        if (!entry.is_regular_file(ec) || !Library::narrowName(entry.path().filename(), name)) {
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
    for (const Library::PlaylistInfo& playlist : r->playlists) {
        Forms::Row row;
        row.id = playlist.name;
        row.label = playlist.name;
        row.detail = count(playlist.entries.size(), "entry", "entries");
        if (Library::Editor::isReservedPlaylist(playlist.name)) {
            row.detail = "Kept by RetroFE, " + row.detail;
        }
        row.params = { { "id", r->name } };
        rows.items.push_back(std::move(row));
    }
    rows.total = rows.items.size();
    return rows;
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
            std::string launcher = r.launcher.via == Library::LauncherVia::None ? "no launcher"
                                 : r.launcher.via == Library::LauncherVia::CollectionFile ? "own launcher" : r.launcher.name;
            row.detail = count(r.gameCount, item_, items_) + "  -  " + launcher;
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
    if (key == "status") return status(screen);
    if (key == "screenTitle") return screenTitle(screen);
    if (key == "query") return query_;
    if (key == "menu") return menu_;

    // Edit buffers
    static const std::set<std::string> settingKeys = { "extensions", "menuSort", "includeMissing", "metadataType" };
    static const std::set<std::string> infoKeys = { "infoTitle", "manufacturer", "year", "genre" };
    if (settingKeys.count(key) || key == "settingsDirty") {
        syncBuffers(screen);
        return key == "settingsDirty" ? yesNo(settings_.edits.isDirty()) : settings_.edits.get(key);
    }
    if (key == "launcher" || key == "launcherDirty") {
        syncBuffers(screen);
        return key == "launcherDirty" ? yesNo(launcher_.edits.isDirty()) : launcher_.edits.get(key);
    }
    if (infoKeys.count(key) || key == "infoDirty") {
        syncBuffers(screen);
        return key == "infoDirty" ? yesNo(info_.edits.isDirty()) : info_.edits.get(key);
    }

    // Media
    if (key == "mediaFile") return mediaFile_;
    if (key.compare(0, 6, "medium") == 0 || key == "isImageSlot" || key == "isVideoSlot" || key == "isTextSlot"
        || key == "hasOwnMedia" || key == "canRestoreMedia") {
        const Library::MediaSlot* s = slot(screen);
        if (!s) {
            return key.compare(0, 2, "is") == 0 || key.compare(0, 3, "has") == 0 || key.compare(0, 3, "can") == 0 ? "no" : "";
        }
        if (key == "mediumImage") return s->kind == Library::MediaKind::Image ? s->file : "";
        if (key == "mediumFile") {
            if (s->file.empty()) return "Nothing";
            return fileName(s->file) + (s->kind == Library::MediaKind::Video ? " (a video: it plays on the page, not here)" : "");
        }
        if (key == "mediumWhere") {
            if (s->file.empty()) return "";
            return s->own ? "Its own file" : "Not its own: it's the " + s->fallback + " (" + s->file + ")";
        }
        if (key == "mediumFolder") return s->directory;
        if (key == "isImageSlot") return yesNo(s->kind == Library::MediaKind::Image);
        if (key == "isVideoSlot") return yesNo(s->kind == Library::MediaKind::Video);
        if (key == "isTextSlot") return yesNo(s->kind == Library::MediaKind::Text);
        if (key == "hasOwnMedia") return yesNo(s->own);
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
        return playlistPath_.empty() ? "" : count(entries_.size(), "entry", "entries") + ", in " + playlistPath_;
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

    // Adding
    if (key == "addMenu") {
        auto found = add_.find(key);
        if (found != add_.end()) return found->second;
        IndexPtr idx = index();
        return idx ? Library::MenuFiles::menuForNewCollections(root(), idx->topMenu) : "";
    }
    if (key == "addLauncher") {
        auto found = add_.find(key);
        if (found != add_.end()) return found->second;
        IndexPtr idx = index();
        return videos_ && idx && idx->findLauncher(videoPlayerLauncher) >= 0 ? videoPlayerLauncher : "";
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
    if (key == "query") { query_ = value; return; }
    if (key == "menu") { menu_ = value; return; }
    if (key == "extensions" || key == "menuSort" || key == "includeMissing" || key == "metadataType") {
        syncBuffers(screen);
        settings_.edits.set(key, value);
        return;
    }
    if (key == "launcher") {
        syncBuffers(screen);
        launcher_.edits.set(key, value);
        return;
    }
    if (key == "infoTitle" || key == "manufacturer" || key == "year" || key == "genre") {
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
        rows.items.push_back(std::move(row));
    }
    else if (key == "media") {
        ensureMedia(screen);
        for (const Library::MediaSlot& s : slots_) {
            Forms::Row row;
            row.id = s.type;
            row.label = capitalised(s.type);
            row.detail = s.file.empty() ? "None" : s.own ? fileName(s.file) : "From the " + s.fallback;
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
    IndexPtr idx = index();
    if (!idx) {
        return result;
    }
    if (key == "menus" || key == "addMenus") {
        std::string top = Library::MenuFiles::menuForNewCollections(root(), idx->topMenu);
        if (key == "menus") {
            result.emplace_back("", "Every menu");
        }
        else {
            result.emplace_back(top, top);
        }
        for (const CollectionRecord& r : idx->collections) {
            if (r.is(Library::KindMenu) && !r.reserved && (key == "menus" || !iequals(r.name, top))) {
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
                result.emplace_back(def.name, def.name + " (this " + noun_ + "'s own)");
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
        result.emplace_back("", videos_ ? "A new video player..." : "A new emulator program...");
        for (const Library::LauncherDef& def : idx->launchers) {
            if (def.scope == Library::LauncherScope::Global) {
                result.emplace_back(def.id, def.id);
            }
        }
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
        return action == "resetLauncher" ? "" : Library::Editor::checkSetting(*idx, r->name, "launcher", launcher_.edits.get("launcher"));
    }
    if (action == "saveInfo" || action == "resetInfo") {
        syncBuffers(screen);
        if (!info_.edits.isDirty()) return "No changes to save.";
        if (action == "resetInfo") return "";
        for (const auto& [k, v] : info_.edits.changes()) {
            if (Utils::trimEnds(v).empty()) continue;
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
        if (action == "clearMedia") return s->own ? "" : "It has no file of its own to clear.";
        return originals_.count(s->type) ? "" : "Nothing was changed here.";
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
    return Provider::actionProblem(action, screen);
}

Forms::Model::Result SystemProvider::runAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) {
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
        return result;
    }
    if (action == "replaceMedia" || action == "clearMedia" || action == "restoreMedia") {
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
    std::string launcher = launcher_.edits.get("launcher");
    Library::EditResult edit = services_.editor->setCollectionSetting(*idx, name, "launcher", launcher);
    result.message = edit.message;
    if (!edit.ok) {
        return result;
    }
    launcher_.edits.commit();
    changed("launcher of " + name);
    result.ok = true;
    result.message = name + "'s " + items_ + " now start with " + launcher + ", unless one has its own launcher."
                   + (edit.message.find("Note:") != std::string::npos ? edit.message.substr(edit.message.find(" Note:")) : "");
    return result;
}

Forms::Model::Result SystemProvider::saveInfo(const Screen& screen)
{
    Forms::Model::Result result;
    IndexPtr idx = index();
    std::string name = record(*idx, screen)->name;
    bool wrote = false;
    for (const auto& [k, v] : info_.edits.changes()) {
        std::string infoKey = k == "infoTitle" ? "title" : k;
        Library::EditResult edit = Utils::trimEnds(v).empty()
            ? services_.editor->removeCollectionSetting(*idx, name, infoKey)
            : services_.editor->setCollectionSetting(*idx, name, infoKey, v);
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
    if (!edit.ok) {
        return result;
    }
    mediaFile_.clear();
    changed(target.type + " art of " + screen.param("id"));
    ensureMedia(screen, true);
    detailsIndex_.reset();
    result.ok = true;
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
    else {
        return Provider::runAction(action, screen);
    }
    result.message = edit.message;
    result.ok = edit.ok;
    if (edit.ok) {
        changed("playlist " + playlist + " of " + name);
        playlistKey_.clear();   // read the file again
    }
    return result;
}

// --- Adding and deleting ---------------------------------------------------------------------

bool SystemProvider::addIsNewLauncher() const
{
    auto found = add_.find("addLauncher");
    if (found != add_.end()) {
        return found->second.empty();
    }
    IndexPtr idx = index();
    return !(videos_ && idx && idx->findLauncher(videoPlayerLauncher) >= 0);
}

std::string SystemProvider::addFolderCount()
{
    std::string folder = Utils::trimEnds(add_["addFolder"]);
    std::string extensions = normaliseExtensions(add_["addExtensions"]);
    std::string key = folder + "\n" + extensions;
    if (key == countedKey_) {
        return countText_;
    }
    countedKey_ = key;
    countText_.clear();
    if (folder.empty()) {
        return countText_;
    }
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
    bool isMenu = menuIndex >= 0 && idx->collections[static_cast<size_t>(menuIndex)].is(Library::KindMenu);
    if (!isMenu && menu != Library::MenuFiles::menuForNewCollections(root(), idx->topMenu)) {
        return menu + " isn't a menu.";
    }
    std::string folder = Utils::trimEnds(add_["addFolder"]);
    if (folder.empty() || !fs::is_directory(folder, ec)) {
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
    if (addIsNewLauncher()) {
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

    std::string settings = std::string(createdHeader) + nl
        + "list.path = " + folder + nl
        + "list.extensions = " + extensions + nl
        + "list.includeMissingItems = false" + nl
        + "list.menuSort = yes" + nl;
    std::string launcherNote;
    if (!addIsNewLauncher()) {
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
        std::string file = Utils::combinePath(root(), "launchers." + os, launcher + ".conf");
        if (!writeNew(backups, file, std::string(createdHeader) + nl + "executable = " + program + nl + "arguments = " + arguments + nl, error)) {
            result.message = error;
            return result;
        }
        settings += "launcher = " + launcher + nl;
        launcherNote = "It plays its videos with the new launcher " + launcher + " (" + fileName(program) + ").";
    }
    else {
        // As Setup does: the collection's own launcher file
        std::string file = Utils::combinePath(collection, "launcher." + os + ".conf");
        if (!writeNew(backups, file, std::string(createdHeader) + nl + "executable = " + program + nl + "arguments = " + arguments + nl, error)) {
            result.message = error;
            return result;
        }
        launcherNote = "It starts its games with " + fileName(program) + ".";
    }
    if (!writeNew(backups, Utils::combinePath(collection, "settings.conf"), settings, error)
        || (videos_ && !writeNew(backups, Utils::combinePath(collection, "info.conf"), "type = videos" + nl, error))
        || !Library::MenuFiles::add(root(), menu, name, -1, &backups, error)) {
        result.message = error.empty() ? "Could not add " + name + "." : error;
        changed("added " + name + " (incomplete)");
        return result;
    }
    changed("added " + name);
    for (const char* key : { "addName", "addFolder", "addProgram" }) {
        add_.erase(key);
    }
    countedKey_ = "\x01";
    result.ok = true;
    result.open = base_;
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
    for (int parent : r.parents) {
        Library::EditResult edit = services_.editor->removeMenuChild(*idx, idx->collections[static_cast<size_t>(parent)].name, name);
        if (!edit.ok) {
            result.message = edit.message;
            return result;
        }
    }
    // Every file goes to the backups (moved, so art isn't copied)
    std::error_code ec;
    std::vector<std::string> files;
    for (auto it = fs::recursive_directory_iterator(r.folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            files.push_back(Library::narrow(it->path()));
        }
    }
    std::string error;
    for (const std::string& file : files) {
        if (!services_.editor->backups().removeKeepingOriginal(file, error)) {
            changed("deleted " + name + " (incomplete)");
            result.message = error;
            return result;
        }
    }
    bool emptied = true;
    for (auto it = fs::recursive_directory_iterator(r.folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        emptied &= !it->is_regular_file(ec);
    }
    if (emptied) {
        fs::remove_all(r.folder, ec);
    }
    changed("deleted " + name);

    std::vector<std::string> folders;
    for (const std::string& folder : r.listPaths) {
        if (!folder.empty()) folders.push_back(folder);
    }
    result.ok = true;
    result.back = true;
    result.message = "Deleted " + name + "."
        + (folders.empty() ? "" : " Its " + items_ + " in " + join(folders, "; ") + " stay where they are.")
        + " Its settings, art and playlists are kept in manage/backups."
        + (r.launcher.via == Library::LauncherVia::Setting && r.launcher.launcher >= 0 ? " The launcher " + r.launcher.name + " stays installed." : "");
    return result;
}

}

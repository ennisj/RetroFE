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
#include "Index.h"
#include "ConfFile.h"
#include "Files.h"
#include "LineList.h"
#include "../Graphics/LayoutCompat.h"
#include "../Utility/Utils.h"
#include <rapidxml.hpp>
#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <set>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Library
{

const std::vector<std::string>& imageExtensions()
{
    // ImageBuilder's order
    static const std::vector<std::string> extensions = { "gif", "webp", "png", "jpg", "jpeg" };
    return extensions;
}

const std::vector<std::string>& videoExtensions()
{
    // VideoBuilder's order (it plays audio files in a video slot too)
    static const std::vector<std::string> extensions = { "mp4", "avi", "mkv", "mp3", "wav", "flac" };
    return extensions;
}

bool isReservedCollection(const std::string& name)
{
    return name.empty() || name[0] == '_' || iequals(name, "Home") || iequals(name, "Catalog") || iequals(name, "Favorites");
}

bool isReservedPlaylistName(const std::string& name)
{
    return iequals(name, "all") || iequals(name, "favorites") || iequals(name, "lastplayed");
}

namespace
{
    // CollectionInfo::extensionList: split on ',', trim each (empty tokens kept)
    std::vector<std::string> splitExtensions(const std::string& text)
    {
        std::vector<std::string> result;
        std::istringstream stream(text);
        std::string token;
        while (std::getline(stream, token, ',')) {
            result.push_back(Utils::trimEnds(token));
        }
        return result;
    }

    std::vector<std::string> readEntries(const std::string& path)
    {
        LineList list(path);
        list.load();
        return list.entries();
    }

    bool fileExists(const std::string& path)
    {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    }

    bool folderExists(const std::string& path)
    {
        std::error_code ec;
        return fs::is_directory(path, ec);
    }

    // File names in a folder ending in suffix (case-sensitive, as RetroFE compares), with the suffix removed
    std::vector<std::pair<std::string, std::string>> filesEnding(const std::string& folder, const std::string& suffix)
    {
        std::vector<std::pair<std::string, std::string>> result;
        std::error_code ec;
        if (!folderExists(folder)) {
            return result;
        }
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            std::string name;
            if (!entry.is_regular_file(ec) || !narrowName(entry.path().filename(), name)) {
                continue;
            }
            if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                result.emplace_back(name.substr(0, name.size() - suffix.size()), Utils::combinePath(folder, name));
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    bool isVideoExtension(const std::string& extension)
    {
        static const std::set<std::string> video = { "mp4", "avi", "mkv", "mov", "webm", "m4v", "mpg", "mpeg", "wmv", "flv" };
        std::string lower = toLower(extension);
        if (!lower.empty() && lower[0] == '.') {
            lower.erase(0, 1);
        }
        return video.count(lower) > 0;
    }

    // <layout width="1920" height="1080" ...> from the start of a layout file
    void readLayoutSize(const std::string& file, int& width, int& height)
    {
        std::string content;
        if (!readFile(file, content)) {
            return;
        }
        size_t start = content.find("<layout");
        if (start == std::string::npos) {
            return;
        }
        size_t end = content.find('>', start);
        std::string tag = content.substr(start, end == std::string::npos ? std::string::npos : end - start);
        auto attribute = [&](const std::string& name) {
            for (size_t at = tag.find(name); at != std::string::npos; at = tag.find(name, at + 1)) {
                // Whole attribute name only (not "loadFontwidth")
                if (at > 0 && !std::isspace(static_cast<unsigned char>(tag[at - 1]))) {
                    continue;
                }
                size_t equals = tag.find_first_not_of(" \t\r\n", at + name.size());
                if (equals == std::string::npos || tag[equals] != '=') {
                    continue;
                }
                size_t quote = tag.find_first_of("\"'", equals);
                if (quote == std::string::npos) {
                    return 0;
                }
                return Utils::convertInt(tag.substr(quote + 1, tag.find(tag[quote], quote + 1) - quote - 1));
            }
            return 0;
        };
        width = attribute("width");
        height = attribute("height");
    }

    // RetroFE's own text-mode getline: the first line of a file, without its line ending
    // Only Windows' text mode turns "\r\n" into "\n"; elsewhere a '\r' stays
    // on the line (Launcher::run reads override files this way)
    bool firstLine(const std::string& path, const std::string& os, std::string& line)
    {
        std::string content;
        if (!readFile(path, content) || content.empty()) {
            return false;
        }
        size_t end = content.find('\n');
        line = content.substr(0, end);
        if (os == "windows" && end != std::string::npos && !line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return true;
    }

    class Builder
    {
    public:
        Builder(const ScanOptions& options, const BuildCallbacks& callbacks)
            : options_(options)
            , callbacks_(callbacks)
        {
        }

        std::unique_ptr<Index> build();

    private:
        bool cancelled() const { return callbacks_.cancel && callbacks_.cancel->load(); }
        void progress(float value, const std::string& phase)
        {
            if (callbacks_.progress) {
                callbacks_.progress(value, phase);
            }
        }

        void readCollections();
        void readCollection(CollectionRecord& record);
        void linkCollections();
        void readLaunchers();
        bool readGames();
        void scanFolder(int collection, const std::string& folder, bool recurse, std::unordered_set<std::string>& seen, int depth);
        void readMetadata();
        void resolveLaunchers();
        void readLayouts();

        ScanOptions options_;
        BuildCallbacks callbacks_;
        std::unique_ptr<Index> index_;
        std::string collectionsFolder_;
    };

    std::unique_ptr<Index> Builder::build()
    {
        auto started = std::chrono::steady_clock::now();
        index_ = std::make_unique<Index>();
        Index& index = *index_;
        if (options_.os.empty()) {
            options_.os = Utils::getOSType();
        }
        index.options = options_;
        collectionsFolder_ = Utils::combinePath(options_.root, "collections");

        progress(0.0f, "Reading settings");
        index.properties = Properties(options_.root);
        if (!index.properties.load(options_.os, callbacks_.cancel)) {
            return nullptr;
        }
        if (!options_.baseItemPath.empty()) {
            index.properties.set("baseItemPath", options_.baseItemPath);
        }
        if (!options_.baseMediaPath.empty()) {
            index.properties.set("baseMediaPath", options_.baseMediaPath);
        }
        if (!options_.firstCollection.empty()) {
            index.properties.set("firstCollection", options_.firstCollection);
        }
        index.topMenu = index.properties.getOr("firstCollection", "Main");
        if (index.topMenu.empty()) {
            index.topMenu = "Main";
        }
        index.activeLayout = index.properties.getOr("layout", "");

        progress(0.05f, "Reading collections");
        readCollections();
        if (cancelled()) {
            return nullptr;
        }
        linkCollections();

        progress(0.15f, "Reading launchers");
        readLaunchers();

        if (options_.games) {
            if (!readGames()) {
                return nullptr;
            }
            if (options_.metadata) {
                progress(0.85f, "Reading metadata");
                readMetadata();
            }
        }
        if (cancelled()) {
            return nullptr;
        }

        // Each collection's games sorted by title, then indexed
        progress(0.93f, "Sorting");
        auto sortGames = [&index](std::vector<GameRecord>& list, std::map<std::pair<int, std::string>, int>& byName, bool own) {
            std::vector<GameRecord> sorted;
            sorted.reserve(list.size());
            std::vector<std::vector<GameRecord>> byCollection(index.collections.size());
            for (GameRecord& game : list) {
                byCollection[static_cast<size_t>(game.collection)].push_back(std::move(game));
            }
            for (size_t c = 0; c < byCollection.size(); c++) {
                auto& games = byCollection[c];
                std::vector<std::pair<std::string, size_t>> order;
                for (size_t i = 0; i < games.size(); i++) {
                    games[i].searchText = toLower(games[i].name) + "\n" + toLower(games[i].title);
                    order.emplace_back(toLower(games[i].title) + '\x01' + games[i].name, i);
                }
                std::sort(order.begin(), order.end());
                if (own) {
                    index.collections[c].gameBegin = sorted.size();
                    index.collections[c].gameCount = games.size();
                }
                for (const auto& [key, i] : order) {
                    byName[{ static_cast<int>(c), games[i].name }] = static_cast<int>(sorted.size());
                    sorted.push_back(std::move(games[i]));
                }
            }
            list = std::move(sorted);
        };
        sortGames(index.games, index.gameByName, true);
        sortGames(index.hiddenGames, index.hiddenByName, false);

        progress(0.94f, "Linking collections");
        index.precompute();

        progress(0.95f, "Working out launchers");
        resolveLaunchers();
        progress(0.97f, "Reading layouts");
        readLayouts();

        index.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        progress(1.0f, "Ready");
        return std::move(index_);
    }

    void Builder::readCollections()
    {
        Index& index = *index_;
        std::error_code ec;
        if (!folderExists(collectionsFolder_)) {
            return;
        }
        for (const auto& entry : fs::directory_iterator(collectionsFolder_, ec)) {
            std::string name;
            if (!entry.is_directory(ec)) {
                continue;
            }
            if (!narrowName(entry.path().filename(), name)) {
                index.problems.push_back("A collection folder's name can't be read in this code page; RetroFE skips it too.");
                continue;
            }
            if (name.empty() || name[0] == '_' || name == "." || name == "..") {
                continue;
            }
            CollectionRecord record;
            record.name = name;
            record.folder = Utils::combinePath(collectionsFolder_, name);
            index.collections.push_back(std::move(record));
        }
        std::sort(index.collections.begin(), index.collections.end(), [](const CollectionRecord& a, const CollectionRecord& b) {
            std::string al = toLower(a.name);
            std::string bl = toLower(b.name);
            return al != bl ? al < bl : a.name < b.name;
        });
        for (size_t i = 0; i < index.collections.size(); i++) {
            index.collectionByName[index.collections[i].name] = static_cast<int>(i);
            index.collectionByLower.emplace(toLower(index.collections[i].name), static_cast<int>(i));
        }
        index.candidates.resize(index.collections.size());

        for (size_t i = 0; i < index.collections.size() && !cancelled(); i++) {
            readCollection(index.collections[i]);
            progress(0.05f + 0.1f * static_cast<float>(i + 1) / static_cast<float>(index.collections.size()), "Reading collections");
        }
    }

    void Builder::readCollection(CollectionRecord& record)
    {
        const Properties& props = index_->properties;
        const std::string prefix = "collections." + record.name + ".";
        record.reserved = isReservedCollection(record.name);

        for (const std::string& name : ConfLayers::collectionFileNames()) {
            if (fileExists(Utils::combinePath(record.folder, name))) {
                record.settingsFiles.push_back(name);
            }
        }
        ConfFile info(Utils::combinePath(record.folder, "info.conf"));
        info.load();
        for (const auto& [key, value] : info.entries()) {
            record.info[key] = Utils::replace(value, "%ITEM_COLLECTION_NAME%", record.name);
        }
        record.title = record.info.count("title") && !record.info["title"].empty() ? record.info["title"] : record.name;

        // Menu: MenuParser's order of preference
        bool fromLaunchers = false;
        props.getBool(prefix + "menuFromCollectionLaunchers", fromLaunchers);
        std::string menuTxt = Utils::combinePath(record.folder, "menu.txt");
        std::string menuFolder = Utils::combinePath(record.folder, "menu");
        std::string menuXml = Utils::combinePath(record.folder, "menu.xml");
        if (fromLaunchers) {
            record.menuSource = MenuSource::CollectionLaunchers;
            std::istringstream csv(props.getOr("collectionLaunchers"));
            for (std::string part; std::getline(csv, part, ',');) {
                if (!part.empty()) {
                    record.children.push_back(MenuEntry{ part });
                }
            }
        }
        else if (fileExists(menuTxt)) {
            record.menuSource = MenuSource::MenuTxt;
            for (const std::string& entry : readEntries(menuTxt)) {
                record.children.push_back(MenuEntry{ entry });
            }
        }
        else if (folderExists(menuFolder)) {
            record.menuSource = MenuSource::MenuFolder;
            auto files = filesEnding(menuFolder, ".txt");
            std::vector<std::string> names;
            for (const auto& [stem, path] : files) {
                // basename is up to the last '.', so "a.b.txt" lists "a.b"
                names.push_back(stem);
            }
            std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return toLower(a) < toLower(b); });
            for (const std::string& name : names) {
                record.children.push_back(MenuEntry{ name });
            }
        }
        else if (fileExists(menuXml)) {
            record.menuSource = MenuSource::MenuXml;
            std::string content;
            if (readFile(menuXml, content)) {
                std::vector<char> buffer(content.begin(), content.end());
                buffer.push_back('\0');
                rapidxml::xml_document<> doc;
                try {
                    doc.parse<0>(buffer.data());
                    if (rapidxml::xml_node<>* menu = doc.first_node("menu")) {
                        for (rapidxml::xml_node<>* item = menu->first_node("item"); item; item = item->next_sibling()) {
                            if (rapidxml::xml_attribute<>* attribute = item->first_attribute("collection")) {
                                record.children.push_back(MenuEntry{ attribute->value() });
                            }
                        }
                    }
                }
                catch (const rapidxml::parse_error&) {
                    record.notes.push_back("menu.xml isn't valid XML, so RetroFE can't read this menu.");
                }
            }
        }
        if (record.menuSource != MenuSource::None) {
            record.kinds |= KindMenu;
        }
        record.hasMenuSupported = fileExists(Utils::combinePath(record.folder, "menu_supported.txt"));
        if (record.hasMenuSupported) {
            record.notes.push_back("menu_supported.txt is RetroFE Plus's list of this menu; Manage keeps it in step with menu.txt.");
        }

        // Merged: <source>.sub files
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(record.folder, ec)) {
            std::string file;
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".sub" || !narrowName(entry.path(), file)) {
                continue;
            }
            SubSource sub;
            narrowName(entry.path().stem(), sub.source);
            sub.file = file;
            sub.entries = readEntries(file);
            record.subs.push_back(std::move(sub));
        }
        std::sort(record.subs.begin(), record.subs.end(), [](const SubSource& a, const SubSource& b) { return a.source < b.source; });
        if (!record.subs.empty()) {
            record.kinds |= KindMerged;
        }

        // Playlists
        for (const auto& [stem, path] : filesEnding(Utils::combinePath(record.folder, "playlists"), ".txt")) {
            PlaylistInfo playlist;
            playlist.name = stem;
            playlist.file = path;
            playlist.entries = readEntries(path);
            playlist.reserved = isReservedPlaylistName(stem);
            record.playlists.push_back(std::move(playlist));
        }
        // Which of them RetroFE shows (CollectionInfoBuilder::loadPlaylistItems)
        {
            std::string cycle = props.getOr("cyclePlaylist");
            if ((cycle.empty() || props.getOr("firstCollection") != record.name) && props.exists(prefix + "cyclePlaylist")) {
                cycle = props.getOr(prefix + "cyclePlaylist");
                record.cyclePlaylistOwn = true;
            }
            if (record.name == "Favorites") {
                cycle = "favorites";
                record.cyclePlaylistOwn = false;
            }
            Utils::listToVector(cycle, record.cyclePlaylist, ',');
        }

        // What CollectionInfoBuilder::buildCollection reads
        record.listPath = props.collectionPath(record.name);
        {
            std::string path = record.listPath;
            do {
                size_t semicolon = path.find(';');
                if (semicolon != std::string::npos) {
                    record.listPaths.push_back(path.substr(0, semicolon));
                    path = path.substr(semicolon + 1);
                }
                else {
                    record.listPaths.push_back(path);
                    path.clear();
                }
            } while (!path.empty());
        }
        record.extensions = splitExtensions(props.getOr(prefix + "list.extensions"));
        record.metadataType = props.getOr(prefix + "metadata.type", record.name);
        bool flag = false;
        if (props.getBool(prefix + "list.includeMissingItems", flag)) {
            record.includeMissingItems = flag;
        }
        props.getBool(prefix + "list.romHierarchy", record.romHierarchy);
        props.getBool(prefix + "list.emuarc", record.emuarc);
        if (record.emuarc) {
            record.romHierarchy = true;
        }
        props.getBool(prefix + "list.menuSort", record.menuSort);
        record.layout = props.getOr(prefix + "layout");
        // Fork-compat: list.includeFile / list.excludeFile (OneSauce) name the lists
        // instead, as in CollectionInfoBuilder::ImportDirectory
        record.includeFile = Utils::combinePath(record.folder, "include.txt");
        record.excludeFile = Utils::combinePath(record.folder, "exclude.txt");
        props.getAbsolutePath(prefix + "list.includeFile", record.includeFile);
        props.getAbsolutePath(prefix + "list.excludeFile", record.excludeFile);
        record.includeList = readEntries(record.includeFile);
        record.excludeList = readEntries(record.excludeFile);
        record.videos = !record.extensions.empty()
            && std::all_of(record.extensions.begin(), record.extensions.end(), isVideoExtension);

        bool hasOwnFolder = false;
        for (const std::string& folder : record.listPaths) {
            hasOwnFolder |= !folder.empty() && folderExists(folder);
        }
        bool hasExtensions = std::any_of(record.extensions.begin(), record.extensions.end(),
            [](const std::string& e) { return !e.empty(); });
        if (hasExtensions || props.exists(prefix + "list.path") || (hasOwnFolder && !record.is(KindMenu) && !record.is(KindMerged))) {
            record.kinds |= KindSystem;
        }

        // Keys this RetroFE doesn't read but OneSauce data uses
        if (props.exists(prefix + "media.basePath")) {
            record.notes.push_back("media.basePath (a OneSauce key) sets where this collection's media are; this fork honours it.");
        }
        if (props.exists(prefix + "list.includeFile") || props.exists(prefix + "list.excludeFile")) {
            record.notes.push_back("list.includeFile / list.excludeFile (OneSauce keys) name its include and exclude lists; this fork honours them.");
        }
        if (props.getOr(prefix + "launcher").find(',') != std::string::npos) {
            record.notes.push_back("\"launcher =\" lists several launchers (OneSauce); this fork uses the first and shows the rest as alternatives.");
        }

        // Per-game launcher overrides
        for (const auto& [stem, path] : filesEnding(Utils::combinePath(record.folder, "launchers"), ".conf")) {
            std::string line;
            record.launcherOverrides[stem] = firstLine(path, options_.os, line) ? std::optional<std::string>(line) : std::nullopt;
        }
    }

    void Builder::linkCollections()
    {
        Index& index = *index_;
        auto resolve = [&](const std::string& name, int& collection, bool& exact) {
            collection = index.findCollection(name);
            exact = collection >= 0 && index.collections[static_cast<size_t>(collection)].name == name;
        };
        for (size_t i = 0; i < index.collections.size(); i++) {
            CollectionRecord& record = index.collections[i];
            for (MenuEntry& child : record.children) {
                resolve(child.entry, child.collection, child.exactCase);
                if (child.collection >= 0) {
                    auto& parents = index.collections[static_cast<size_t>(child.collection)].parents;
                    if (std::find(parents.begin(), parents.end(), static_cast<int>(i)) == parents.end()) {
                        parents.push_back(static_cast<int>(i));
                    }
                }
                else {
                    record.notes.push_back("The menu lists \"" + child.entry + "\", but there's no such collection.");
                }
                if (child.collection >= 0 && !child.exactCase) {
                    record.notes.push_back("The menu lists \"" + child.entry + "\" but the folder is spelt \""
                        + index.collections[static_cast<size_t>(child.collection)].name + "\"; RetroFE won't find its settings.");
                }
            }
            for (SubSource& sub : record.subs) {
                resolve(sub.source, sub.collection, sub.exactCase);
                if (sub.collection < 0) {
                    record.notes.push_back(sub.source + ".sub names a collection that doesn't exist.");
                }
                else if (!sub.exactCase) {
                    record.notes.push_back(sub.source + ".sub is spelt differently from the folder \""
                        + index.collections[static_cast<size_t>(sub.collection)].name + "\", so RetroFE finds none of its games.");
                }
            }
        }
    }

    void Builder::readLaunchers()
    {
        Index& index = *index_;
        const Properties& props = index.properties;
        auto add = [&](LauncherDef def, const std::string& keyPrefix) {
            def.executable = props.getOr(keyPrefix + ".executable");
            def.arguments = props.getOr(keyPrefix + ".arguments");
            def.currentDirectory = props.getOr(keyPrefix + ".currentDirectory");
            props.getBool(keyPrefix + ".reboot", def.reboot);
            std::string scope = def.scope == LauncherScope::Global ? "g:" : def.scope == LauncherScope::Collection ? "c:" : "l:";
            index.launcherById[scope + def.id] = static_cast<int>(index.launchers.size());
            index.launchers.push_back(std::move(def));
        };

        for (const auto& [stem, path] : filesEnding(Utils::combinePath(options_.root, "launchers." + options_.os), ".conf")) {
            LauncherDef def;
            def.id = stem;
            def.name = stem;
            def.scope = LauncherScope::Global;
            def.file = path;
            add(std::move(def), "launchers." + stem);
        }
        for (const CollectionRecord& record : index.collections) {
            std::string osFile = Utils::combinePath(record.folder, "launcher." + options_.os + ".conf");
            std::string defaultFile = Utils::combinePath(record.folder, "launcher.conf");
            std::string file = fileExists(osFile) ? osFile : fileExists(defaultFile) ? defaultFile : "";
            if (!file.empty()) {
                LauncherDef def;
                def.id = record.name;
                def.name = record.name;
                def.scope = LauncherScope::Collection;
                def.collection = record.name;
                def.file = file;
                add(std::move(def), "collectionLaunchers." + record.name);
            }
            std::string localFolder = Utils::combinePath(record.folder, "launchers." + options_.os + ".local");
            if (!folderExists(localFolder)) {
                localFolder = Utils::combinePath(record.folder, "launchers.local");
            }
            for (const auto& [stem, path] : filesEnding(localFolder, ".conf")) {
                LauncherDef def;
                def.id = record.name + "." + stem;
                def.name = stem;
                def.scope = LauncherScope::Local;
                def.collection = record.name;
                def.file = path;
                add(std::move(def), "localLaunchers." + record.name + "." + stem);
            }
        }
    }

    void Builder::scanFolder(int collection, const std::string& folder, bool recurse, std::unordered_set<std::string>& seen, int depth)
    {
        Index& index = *index_;
        CollectionRecord& record = index.collections[static_cast<size_t>(collection)];
        std::error_code ec;
        if (folder.empty() || !folderExists(folder) || depth > 64) {
            return;
        }
        std::vector<std::string> extensions = record.extensions;
        size_t counter = 0;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            if ((++counter & 1023) == 0 && cancelled()) {
                return;
            }
            std::string file;
            if (!narrowName(it->path().filename(), file)) {
                continue;
            }
            if (recurse && it->is_directory(ec)) {
                scanFolder(collection, Utils::combinePath(folder, file), recurse, seen, depth + 1);
                continue;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            size_t dot = file.find_last_of('.');
            std::string basename = dot == std::string::npos ? file : file.substr(0, dot);
            bool matched = false;
            for (const std::string& extension : extensions) {
                std::string comparator = "." + extension;
                if (file.size() >= comparator.size() && file.compare(file.size() - comparator.size(), comparator.size(), comparator) == 0) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                // RetroFE compares extensions case-sensitively: say so when only the case differs
                std::string lowerFile = toLower(file);
                for (const std::string& extension : extensions) {
                    std::string comparator = toLower("." + extension);
                    if (!extension.empty() && lowerFile.size() >= comparator.size()
                        && lowerFile.compare(lowerFile.size() - comparator.size(), comparator.size(), comparator) == 0) {
                        record.caseMismatchCount++;
                        if (record.caseMismatches.size() < 5) {
                            record.caseMismatches.push_back(file);
                        }
                        break;
                    }
                }
                continue;
            }
            // emuarc: the item is named after its folder, but include/exclude match
            // the file's basename, so every file stays a candidate; items are
            // made unique by name when the filters are applied
            std::string name = record.emuarc ? Utils::getFileName(folder) : basename;
            if (!seen.insert(record.emuarc ? folder + '\n' + basename : basename).second) {
                continue;
            }
            index.candidates[static_cast<size_t>(collection)].push_back(
                Index::Candidate{ name, basename, Utils::combinePath(folder, file), folder });
        }
    }

    bool Builder::readGames()
    {
        Index& index = *index_;
        size_t systems = 0;
        for (const CollectionRecord& record : index.collections) {
            systems += record.is(KindSystem) ? 1 : 0;
        }
        size_t done = 0;
        for (size_t c = 0; c < index.collections.size(); c++) {
            if (cancelled()) {
                return false;
            }
            CollectionRecord& record = index.collections[c];
            if (!record.is(KindSystem)) {
                continue;
            }
            progress(0.2f + 0.65f * static_cast<float>(done) / static_cast<float>(std::max<size_t>(systems, 1)), "Listing " + record.name);
            done++;

            std::unordered_set<std::string> seen;
            for (const std::string& folder : record.listPaths) {
                scanFolder(static_cast<int>(c), folder, record.romHierarchy, seen, 0);
            }
            if (record.caseMismatchCount) {
                record.notes.push_back(std::to_string(record.caseMismatchCount) + " file(s) such as \"" + record.caseMismatches[0]
                    + "\" match list.extensions only ignoring case; RetroFE compares the case too, so it doesn't list them.");
            }

            // ImportDirectory for the collection's own page (no .sub filter)
            std::set<std::string> include(record.includeList.begin(), record.includeList.end());
            std::set<std::string> exclude(record.excludeList.begin(), record.excludeList.end());
            bool showMissing = record.includeMissingItems.value_or(false);
            std::set<std::string> listed;
            std::map<std::string, const Index::Candidate*> byName;
            for (const Index::Candidate& candidate : index.candidates[c]) {
                byName.emplace(candidate.name, &candidate);
            }
            auto makeGame = [&](const std::string& name, const Index::Candidate* candidate) {
                GameRecord game;
                game.collection = static_cast<int>(c);
                game.name = name;
                game.title = name;
                if (candidate) {
                    game.file = candidate->file;
                    game.directory = candidate->directory;
                }
                game.missing = candidate == nullptr;
                return game;
            };
            auto addGame = [&](const std::string& name, const Index::Candidate* candidate) {
                index.games.push_back(makeGame(name, candidate));
                listed.insert(name);
            };
            // What the exclude list hides, for Manage (Index::hiddenGames)
            std::vector<std::pair<std::string, const Index::Candidate*>> hidden;
            if (showMissing) {
                for (const std::string& name : record.includeList) {
                    auto it = byName.find(name);
                    if (exclude.count(name)) {
                        hidden.emplace_back(name, it == byName.end() ? nullptr : it->second);
                    }
                    else if (!listed.count(name)) {
                        addGame(name, it == byName.end() ? nullptr : it->second);
                    }
                }
            }
            if (!showMissing || include.empty()) {
                for (const Index::Candidate& candidate : index.candidates[c]) {
                    if (!include.empty() && !include.count(candidate.basename)) {
                        continue;
                    }
                    if (exclude.count(candidate.basename)) {
                        hidden.emplace_back(candidate.name, &candidate);
                    }
                    else if (!listed.count(candidate.name)) {
                        addGame(candidate.name, &candidate);
                    }
                }
            }
            // Once per name, and not when another file of it is listed (a folder game)
            std::set<std::string> hiddenSeen;
            for (const auto& [name, candidate] : hidden) {
                if (!listed.count(name) && hiddenSeen.insert(name).second) {
                    index.hiddenGames.push_back(makeGame(name, candidate));
                }
            }
        }
        return true;
    }

    void Builder::readMetadata()
    {
        Index& index = *index_;
        std::string path = Utils::combinePath(options_.root, "meta.db");
        if (!fileExists(path)) {
            return;
        }
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            index.problems.push_back("Could not open meta.db: " + std::string(db ? sqlite3_errmsg(db) : "unknown error"));
            sqlite3_close(db);
            return;
        }
        sqlite3_busy_timeout(db, 2000);
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT name, title, year, manufacturer, developer, genre, cloneOf, players, ctrltype, buttons, joyways, rating, score "
                          "FROM Meta WHERE collectionName = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            index.problems.push_back("meta.db has no metadata table yet: " + std::string(sqlite3_errmsg(db)));
            sqlite3_close(db);
            return;
        }

        // Games by metadata type, so each type is read once
        std::map<std::string, std::vector<GameRecord*>> byType;
        for (std::vector<GameRecord>* list : { &index.games, &index.hiddenGames }) {
            for (GameRecord& game : *list) {
                byType[index.collections[static_cast<size_t>(game.collection)].metadataType].push_back(&game);
            }
        }
        size_t done = 0;
        for (auto& [type, games] : byType) {
            if (cancelled()) {
                break;
            }
            progress(0.85f + 0.08f * static_cast<float>(done++) / static_cast<float>(byType.size()), "Reading metadata for " + type);
            std::multimap<std::string, GameRecord*> byName;
            for (GameRecord* game : games) {
                byName.emplace(game->name, game);
            }
            // RetroFE may be writing meta.db (an import): a busy database is
            // retried from the start (rows are applied idempotently)
            int rc = SQLITE_OK;
            for (int attempt = 0; attempt < 5; attempt++) {
                sqlite3_reset(stmt);
                sqlite3_bind_text(stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT);
                size_t rows = 0;
                while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                    if ((++rows & 4095) == 0 && cancelled()) {
                        break;
                    }
                auto text = [&](int column) {
                    const unsigned char* value = sqlite3_column_text(stmt, column);
                    return value ? std::string(reinterpret_cast<const char*>(value)) : std::string();
                };
                // injectMetadata matches names exactly
                auto it = byName.find(text(0));
                if (it == byName.end()) {
                    continue;
                }
                // Several sources with the same names share a type: fill each
                auto range = byName.equal_range(text(0));
                for (auto match = range.first; match != range.second; ++match) {
                    GameRecord& game = *match->second;
                    Metadata& meta = game.meta;
                    meta.title = text(1);
                    meta.year = text(2);
                    meta.manufacturer = text(3);
                    meta.developer = text(4);
                    meta.genre = text(5);
                    meta.cloneOf = text(6);
                    meta.players = text(7);
                    meta.ctrlType = text(8);
                    meta.buttons = text(9);
                    meta.joyWays = text(10);
                    meta.rating = text(11);
                    meta.score = text(12);
                    game.title = meta.title;
                    game.hasMetadata = true;
                }
                }
                if ((rc != SQLITE_BUSY && rc != SQLITE_LOCKED) || cancelled()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (rc != SQLITE_DONE && rc != SQLITE_ROW && !cancelled()) {
                index.problems.push_back("Reading metadata for " + type + " from meta.db failed (" + sqlite3_errmsg(db)
                    + "); some titles may show as file names.");
            }
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db);
    }

    void Builder::resolveLaunchers()
    {
        Index& index = *index_;
        std::set<std::string> unresolved;
        for (size_t c = 0; c < index.collections.size(); c++) {
            CollectionRecord& record = index.collections[c];
            bool hasGames = record.is(KindSystem) || !record.launcherOverrides.empty();
            if (!hasGames) {
                continue;
            }
            record.launcher = index.resolveLauncher(static_cast<int>(c));
            if (record.is(KindSystem)) {
                index.launcherUses.push_back(LauncherUse{ static_cast<int>(c), "", record.launcher.via, record.launcher.launcher });
                if (record.launcher.launcher < 0 && !record.launcher.name.empty()) {
                    unresolved.insert(record.launcher.name);
                }
            }
            for (const auto& [game, line] : record.launcherOverrides) {
                if (!line) {
                    continue;
                }
                LauncherResolution resolution = index.resolveLauncher(static_cast<int>(c), game);
                index.launcherUses.push_back(LauncherUse{ static_cast<int>(c), game, resolution.via, resolution.launcher });
                if (resolution.launcher < 0 && !resolution.name.empty()) {
                    unresolved.insert(resolution.name);
                }
            }
        }
        index.unresolvedLaunchers.assign(unresolved.begin(), unresolved.end());
    }

    void Builder::readLayouts()
    {
        Index& index = *index_;
        // As RetroFE: the layouts folder it was given, else baseLayoutPath when that's a folder, else <root>/layouts
        std::string layoutsFolder = options_.layoutsPath;
        if (layoutsFolder.empty() && (!index.properties.getAbsolutePath("baseLayoutPath", layoutsFolder) || !folderExists(layoutsFolder))) {
            layoutsFolder = Utils::combinePath(options_.root, "layouts");
        }
        std::error_code ec;
        if (!folderExists(layoutsFolder)) {
            return;
        }
        for (const auto& entry : fs::directory_iterator(layoutsFolder, ec)) {
            LayoutRecord layout;
            if (!entry.is_directory(ec) || !narrowName(entry.path().filename(), layout.name)) {
                continue;
            }
            layout.folder = Utils::combinePath(layoutsFolder, layout.name);
            std::string mainPage = Utils::combinePath(layout.folder, "layout.xml");
            layout.hasLayout = fileExists(mainPage);
            if (!layout.hasLayout && fileExists(Utils::combinePath(layout.folder, "layout.lua"))) {
                // Fork compat: a layout.lua whose build() names the main page (Graphics/LayoutCompat.h)
                mainPage = LayoutCompat::luaBuildTarget(Utils::combinePath(layout.folder, "layout.lua"), layout.folder);
                layout.hasLayout = !mainPage.empty() && fileExists(mainPage);
            }
            layout.hasSplash = fileExists(Utils::combinePath(layout.folder, "splash.xml"));
            if (layout.hasLayout) {
                readLayoutSize(mainPage, layout.width, layout.height);
            }
            for (const auto& [stem, path] : filesEnding(layout.folder, ".xml")) {
                if (stem != "layout" && stem != "splash") {
                    layout.extraLayouts.push_back(stem + ".xml");
                }
            }
            std::string collections = Utils::combinePath(layout.folder, "collections");
            if (folderExists(collections)) {
                for (const auto& sub : fs::directory_iterator(collections, ec)) {
                    std::string name;
                    if (!sub.is_directory(ec) || !narrowName(sub.path().filename(), name)) {
                        continue;
                    }
                    std::string folder = Utils::combinePath(collections, name);
                    if (folderExists(Utils::combinePath(folder, "layout"))) {
                        layout.collectionLayouts.push_back(name);
                    }
                    if (folderExists(Utils::combinePath(folder, "medium_artwork")) || folderExists(Utils::combinePath(folder, "system_artwork"))) {
                        layout.mediaCollections.push_back(name);
                    }
                }
            }
            for (const auto& [stem, path] : filesEnding(Utils::combinePath(layout.folder, "forms"), ".xml")) {
                layout.forms.push_back(stem);
            }
            layout.active = layout.name == index.activeLayout;
            for (const CollectionRecord& record : index.collections) {
                if (record.layout == layout.name) {
                    layout.usedBy.push_back(record.name);
                }
            }
            index.layouts.push_back(std::move(layout));
        }
        std::sort(index.layouts.begin(), index.layouts.end(), [](const LayoutRecord& a, const LayoutRecord& b) {
            return toLower(a.name) < toLower(b.name);
        });
    }
}

std::unique_ptr<Index> buildIndex(const ScanOptions& options, const BuildCallbacks& callbacks)
{
    Builder builder(options, callbacks);
    return builder.build();
}

// --- Index queries -------------------------------------------------------------

int Index::findCollection(const std::string& name) const
{
    auto exact = collectionByName.find(name);
    if (exact != collectionByName.end()) {
        return exact->second;
    }
    auto lower = collectionByLower.find(toLower(name));
    return lower == collectionByLower.end() ? -1 : lower->second;
}

int Index::findGame(const std::string& collection, const std::string& name) const
{
    int c = findCollection(collection);
    if (c < 0) {
        return -1;
    }
    auto it = gameByName.find({ c, name });
    return it == gameByName.end() ? -1 : it->second;
}

int Index::findHiddenGame(const std::string& collection, const std::string& name) const
{
    int c = findCollection(collection);
    if (c < 0) {
        return -1;
    }
    auto it = hiddenByName.find({ c, name });
    return it == hiddenByName.end() ? -1 : it->second;
}

int Index::findLauncher(LauncherScope scope, const std::string& id) const
{
    std::string key = (scope == LauncherScope::Global ? "g:" : scope == LauncherScope::Collection ? "c:" : "l:") + id;
    auto it = launcherById.find(key);
    return it == launcherById.end() ? -1 : it->second;
}

int Index::findLauncher(const std::string& id) const
{
    // The order Launcher::launcherExecutable looks
    for (LauncherScope scope : { LauncherScope::Local, LauncherScope::Collection, LauncherScope::Global }) {
        int found = findLauncher(scope, id);
        if (found >= 0) {
            return found;
        }
    }
    return -1;
}

int Index::findLayout(const std::string& name) const
{
    for (size_t i = 0; i < layouts.size(); i++) {
        if (layouts[i].name == name) {
            return static_cast<int>(i);
        }
    }
    for (size_t i = 0; i < layouts.size(); i++) {
        if (iequals(layouts[i].name, name)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<ItemRef> Index::itemsFromSource(int source, int merged, const SubSource* sub) const
{
    // CollectionInfoBuilder::ImportDirectory for buildCollection(source, merged)
    const CollectionRecord& src = collections[static_cast<size_t>(source)];
    const CollectionRecord& page = collections[static_cast<size_t>(merged)];
    bool showMissing = page.includeMissingItems.value_or(false);
    if (src.includeMissingItems) {
        showMissing = *src.includeMissingItems;
    }
    std::vector<std::string> includeOrdered;
    std::set<std::string> include;
    auto addInclude = [&](const std::string& name) {
        if (include.insert(name).second) {
            includeOrdered.push_back(name);
        }
    };
    if (sub) {
        for (const std::string& name : sub->entries) {
            addInclude(name);
        }
    }
    for (const std::string& name : src.includeList) {
        addInclude(name);
    }
    std::set<std::string> exclude(src.excludeList.begin(), src.excludeList.end());

    std::vector<ItemRef> result;
    std::set<std::string> listed;
    auto add = [&](const std::string& name) {
        auto it = gameByName.find({ source, name });
        result.push_back(ItemRef{ it == gameByName.end() ? -1 : it->second, source, name });
        listed.insert(name);
    };
    if (showMissing) {
        for (const std::string& name : includeOrdered) {
            if (!exclude.count(name) && !listed.count(name)) {
                add(name);
            }
        }
    }
    if (!showMissing || include.empty()) {
        for (const Candidate& candidate : candidates[static_cast<size_t>(source)]) {
            if ((include.empty() || include.count(candidate.basename)) && !exclude.count(candidate.basename)
                && !listed.count(candidate.name)) {
                add(candidate.name);
            }
        }
    }
    return result;
}

void Index::precompute()
{
    // Snapshots never change, so the page lists, the merged collections
    // showing each game and the playlists listing it are worked out once
    items_.assign(collections.size(), {});
    mergedShowing_.assign(games.size(), {});
    playlistsListing_.assign(games.size(), {});
    for (size_t c = 0; c < collections.size(); c++) {
        const CollectionRecord& record = collections[c];
        std::vector<ItemRef>& items = items_[c];
        for (size_t i = record.gameBegin; i < record.gameBegin + record.gameCount; i++) {
            items.push_back(ItemRef{ static_cast<int>(i), static_cast<int>(c), games[i].name });
        }
        for (const SubSource& sub : record.subs) {
            // RetroFE builds the source by the .sub's spelling; settings are keyed by the folder's
            if (sub.collection < 0 || !sub.exactCase) {
                continue;
            }
            for (ItemRef& item : itemsFromSource(sub.collection, static_cast<int>(c), &sub)) {
                if (item.game >= 0) {
                    std::vector<int>& showing = mergedShowing_[static_cast<size_t>(item.game)];
                    if (showing.empty() || showing.back() != static_cast<int>(c)) {
                        showing.push_back(static_cast<int>(c));
                    }
                }
                items.push_back(std::move(item));
            }
        }
    }

    // A playlist of collection C lists a game only if C's page shows it
    // (CollectionInfoBuilder::loadPlaylistItems matches against info->items)
    for (size_t c = 0; c < collections.size(); c++) {
        const CollectionRecord& record = collections[c];
        if (record.playlists.empty()) {
            continue;
        }
        std::map<std::pair<std::string, std::string>, int> onPage;  // (collection, name) -> game
        for (const ItemRef& item : items_[c]) {
            if (item.game >= 0) {
                onPage.emplace(std::make_pair(collections[static_cast<size_t>(item.collection)].name, item.name), item.game);
            }
        }
        for (const PlaylistInfo& playlist : record.playlists) {
            std::set<int> listed;
            for (const std::string& entry : playlist.entries) {
                LineList::Qualified q = LineList::parseQualified(entry, record.name);
                if (q.name == "*") {
                    for (const auto& [key, game] : onPage) {
                        if (key.first == q.collection) {
                            listed.insert(game);
                        }
                    }
                    continue;
                }
                auto it = onPage.find({ q.collection, q.name });
                if (it != onPage.end()) {
                    listed.insert(it->second);
                }
            }
            for (int game : listed) {
                playlistsListing_[static_cast<size_t>(game)].push_back(PlaylistRef{ static_cast<int>(c), playlist.name });
            }
        }
    }
}

const std::vector<ItemRef>& Index::itemsOf(int collection) const
{
    static const std::vector<ItemRef> none;
    if (collection < 0 || static_cast<size_t>(collection) >= items_.size()) {
        return none;
    }
    return items_[static_cast<size_t>(collection)];
}

std::vector<MenuRow> Index::menuTree(const std::string& from) const
{
    std::vector<MenuRow> rows;
    int top = findCollection(from.empty() ? topMenu : from);
    if (top < 0) {
        return rows;
    }
    std::vector<int> path;
    std::function<void(int, const std::string&, int, int)> visit = [&](int collection, const std::string& entry, int depth, int parentRow) {
        MenuRow row;
        row.collection = collection;
        row.entry = entry;
        row.depth = depth;
        row.parentRow = parentRow;
        row.cycle = collection >= 0 && std::find(path.begin(), path.end(), collection) != path.end();
        rows.push_back(row);
        int rowIndex = static_cast<int>(rows.size()) - 1;
        if (collection < 0 || row.cycle || depth > 32) {
            return;
        }
        path.push_back(collection);
        for (const MenuEntry& child : collections[static_cast<size_t>(collection)].children) {
            visit(child.collection, child.entry, depth + 1, rowIndex);
        }
        path.pop_back();
    };
    visit(top, collections[static_cast<size_t>(top)].name, 0, -1);
    return rows;
}

std::vector<char> Index::gameMask(int collection) const
{
    std::vector<char> mask(games.size(), 0);
    if (collection < 0 || static_cast<size_t>(collection) >= collections.size()) {
        return mask;
    }
    std::vector<char> visited(collections.size(), 0);
    std::function<void(int)> visit = [&](int c) {
        if (visited[static_cast<size_t>(c)]) {
            return;
        }
        visited[static_cast<size_t>(c)] = 1;
        for (const ItemRef& item : itemsOf(c)) {
            if (item.game >= 0) {
                mask[static_cast<size_t>(item.game)] = 1;
            }
        }
        for (const MenuEntry& child : collections[static_cast<size_t>(c)].children) {
            if (child.collection >= 0) {
                visit(child.collection);
            }
        }
    };
    visit(collection);
    return mask;
}

LauncherResolution Index::resolveLauncher(int collection, const std::string& game) const
{
    // Launcher::run, step for step. For a game shown through a .sub, pass its
    // own (source) collection: that's the one RetroFE launches it with.
    LauncherResolution r;
    if (collection < 0 || static_cast<size_t>(collection) >= collections.size()) {
        r.problem = "Unknown collection.";
        return r;
    }
    const CollectionRecord& record = collections[static_cast<size_t>(collection)];
    const std::string& c = record.name;
    // Fork-compat: the first of "launcher = a, b, c"; the rest are alternatives
    r.configured = firstLauncher(properties.getOr("collections." + c + ".launcher", ""), &r.alternatives);
    r.name = r.configured;
    r.via = r.configured.empty() ? LauncherVia::None : LauncherVia::Setting;

    if (!game.empty()) {
        auto it = record.launcherOverrides.find(game);
#ifdef WIN32
        if (it == record.launcherOverrides.end()) {
            // The file system ignores case on Windows
            it = std::find_if(record.launcherOverrides.begin(), record.launcherOverrides.end(),
                [&](const auto& entry) { return iequals(entry.first, game); });
        }
#endif
        if (it != record.launcherOverrides.end() && it->second) {
            r.overrideName = *it->second;
            r.overrideFile = Utils::combinePath(record.folder, "launchers", it->first + ".conf");
            bool local = properties.prefixExists("localLaunchers." + c + "." + r.overrideName);
            r.name = local ? c + "." + r.overrideName : r.overrideName;
            r.via = LauncherVia::GameOverride;
        }
    }
    if (r.name == r.configured && properties.prefixExists("collectionLaunchers." + c)) {
        r.name = c;
        r.via = LauncherVia::CollectionFile;
    }

    if (properties.exists("localLaunchers." + r.name + ".executable")) {
        r.launcher = findLauncher(LauncherScope::Local, r.name);
    }
    else if (properties.exists("collectionLaunchers." + r.name + ".executable")) {
        r.launcher = findLauncher(LauncherScope::Collection, r.name);
    }
    else if (properties.exists("launchers." + r.name + ".executable")) {
        r.launcher = findLauncher(LauncherScope::Global, r.name);
    }

    if (r.launcher < 0) {
        if (r.name.empty()) {
            r.problem = "No launcher is set, so its games can't be started.";
        }
        else if (!r.overrideName.empty() && r.overrideName.back() == '\r') {
            r.problem = r.overrideFile + " ends its first line with Windows line endings; on "
                + options.os + " RetroFE reads the launcher name with a stray carriage return and finds no launcher.";
        }
        else {
            r.problem = "There's no launcher called \"" + r.name + "\" (launchers." + options.os + "/" + r.name + ".conf).";
        }
    }
    else if (!properties.exists("collections." + c + ".list.extensions")) {
        r.problem = "list.extensions isn't set, so RetroFE can't find the game file to launch.";
    }
    return r;
}

std::vector<LauncherUse> Index::usesOf(int launcher) const
{
    std::vector<LauncherUse> result;
    for (const LauncherUse& use : launcherUses) {
        if (use.launcher == launcher) {
            result.push_back(use);
        }
    }
    return result;
}

const std::vector<int>& Index::mergedCollectionsShowing(int game) const
{
    static const std::vector<int> none;
    if (game < 0 || static_cast<size_t>(game) >= mergedShowing_.size()) {
        return none;
    }
    return mergedShowing_[static_cast<size_t>(game)];
}

const std::vector<Index::PlaylistRef>& Index::playlistsListing(int game) const
{
    static const std::vector<PlaylistRef> none;
    if (game < 0 || static_cast<size_t>(game) >= playlistsListing_.size()) {
        return none;
    }
    return playlistsListing_[static_cast<size_t>(game)];
}

}

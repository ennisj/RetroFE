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
#include "Edits.h"
#include "ConfFile.h"
#include "Files.h"
#include "LineList.h"
#include "MetadataOverrides.h"
#include "Properties.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{

namespace
{
    // A name used as a file name (a game, playlist, ...): no folders, nothing Windows refuses
    std::string checkFileName(const std::string& name, const std::string& what)
    {
        if (Utils::trimEnds(name).empty()) {
            return "The " + what + " name is empty.";
        }
        if (name.find_first_of("\\/:*?\"<>|#\r\n") != std::string::npos) {
            return "A " + what + " name can't contain \\ / : * ? \" < > | or #.";
        }
        if (name == "." || name == ".." || name[0] == '.') {
            return "A " + what + " name can't start with a dot.";
        }
        if (Utils::trimEnds(name) != name) {
            return "A " + what + " name can't start or end with a space.";
        }
        return "";
    }

    const CollectionRecord* findRecord(const Index& index, const std::string& name, int& position)
    {
        position = index.findCollection(name);
        return position < 0 ? nullptr : &index.collections[static_cast<size_t>(position)];
    }

    bool isBoolean(const std::string& value)
    {
        std::string lower = toLower(value);
        return lower == "yes" || lower == "no" || lower == "true" || lower == "false" || lower == "on" || lower == "off";
    }

    // The files in folder named stem.<ext> for any of extensions, ignoring case
    std::vector<std::string> slotFiles(const std::string& folder, const std::string& stem, const std::vector<std::string>& extensions)
    {
        std::vector<std::string> result;
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) {
            return result;
        }
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            std::string name;
            if (!entry.is_regular_file(ec) || !narrowName(entry.path().filename(), name)) {
                continue;
            }
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || !iequals(name.substr(0, dot), stem)) {
                continue;
            }
            std::string extension = toLower(name.substr(dot + 1));
            if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end()) {
                result.push_back(Utils::combinePath(folder, name));
            }
        }
        return result;
    }

    bool withMetaDb(const std::string& root, const std::function<bool(sqlite3*, std::string&)>& work, std::string& error)
    {
        std::string path = Utils::combinePath(root, "meta.db");
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            error = "there's no meta.db yet";
            return false;
        }
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
            error = db ? sqlite3_errmsg(db) : "could not open meta.db";
            sqlite3_close(db);
            return false;
        }
        sqlite3_busy_timeout(db, 2000);
        bool ok = work(db, error);
        sqlite3_close(db);
        return ok;
    }

    bool writeMetaDb(const std::string& root, const std::string& type, const std::string& game,
                     const std::map<std::string, std::string>& values, std::string& error)
    {
        return withMetaDb(root, [&](sqlite3* db, std::string& e) { return MetadataOverrides::apply(db, type, game, values, e); }, error);
    }
}

Editor::Editor(const std::string& root)
    : root_(root)
    , backups_(root)
{
}

// --- Games ---------------------------------------------------------------------

EditResult Editor::setGameLauncher(const Index& index, const std::string& collection, const std::string& game,
                                   const std::string& launcher)
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + collection + "\".");
    }
    std::string problem = checkFileName(game, "game");
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    if (index.findGame(record->name, game) < 0 && !record->launcherOverrides.count(game)) {
        return EditResult::fail(record->name + " has no game called \"" + game + "\".");
    }

    // An existing file keeps its spelling (the file system may ignore case)
    std::string file = Utils::combinePath(record->folder, "launchers", game + ".conf");
    for (const auto& [stem, line] : record->launcherOverrides) {
        if (iequals(stem, game)) {
            file = Utils::combinePath(record->folder, "launchers", stem + ".conf");
        }
    }

    std::string name = Utils::trimEnds(launcher);
    LauncherResolution system = index.resolveLauncher(c);
    EditResult result;
    if (name.empty() || name == system.configured) {
        // Launcher::run treats an override naming the default as no override
        std::string error;
        if (!backups_.removeKeepingOriginal(file, error)) {
            return EditResult::fail(error);
        }
        result.ok = true;
        result.files.push_back(file);
        result.message = game + " now starts with " + record->name + "'s launcher"
            + (system.name.empty() ? "." : " (" + system.name + ").");
        return result;
    }

    if (name.find_first_of("#\r\n") != std::string::npos) {
        return EditResult::fail("A launcher name can't contain # or a line break.");
    }
    const Properties& props = index.properties;
    bool known = props.prefixExists("localLaunchers." + record->name + "." + name)
        || props.exists("localLaunchers." + name + ".executable")
        || props.exists("collectionLaunchers." + name + ".executable")
        || props.exists("launchers." + name + ".executable");
    if (!known) {
        return EditResult::fail("There's no launcher called \"" + name + "\".");
    }

    // Only the first line counts; keep anything after it
    std::string content;
    Lines lines;
    if (readFile(file, content)) {
        lines = Lines::parse(content);
    }
    // Always "\n" after the name: RetroFE reads only this line, and on Linux
    // and macOS its text-mode getline keeps a '\r', which would become part
    // of the launcher name
    if (lines.lines.empty()) {
        lines.lines.push_back(Line{ name, "\n" });
    }
    else {
        lines.lines[0].text = name;
        lines.lines[0].eol = "\n";
    }
    std::string error;
    if (!backups_.beforeChange(file, error) || !writeFileAtomic(file, lines.join(), error)) {
        return EditResult::fail(error);
    }
    result.ok = true;
    result.files.push_back(file);
    result.message = game + " now starts with " + name + ".";
    return result;
}

EditResult Editor::setMetadata(const Index& index, const std::string& collection, const std::string& game,
                               const std::string& field, const std::string& value)
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + collection + "\".");
    }
    const MetadataOverrides::Field* info = MetadataOverrides::findField(field);
    if (!info) {
        return EditResult::fail("\"" + field + "\" isn't a metadata field.");
    }
    std::string problem = MetadataOverrides::checkValue(info->element, value);
    if (game.empty()) {
        problem = "No game given.";
    }
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    std::string type = record->metadataType;
    std::string file = MetadataOverrides::fileFor(root_, type);
    MetadataOverrides::Overrides overrides;
    std::string error;
    if (!MetadataOverrides::read(file, overrides, error)) {
        return EditResult::fail("Your earlier metadata changes in " + file + " can't be read (" + error + "), so nothing was changed.");
    }
    overrides[game][info->element] = value;
    if (!MetadataOverrides::write(file, overrides, error)) {
        return EditResult::fail(error);
    }
    EditResult result;
    result.ok = true;
    result.files.push_back(file);
    if (writeMetaDb(root_, type, game, { { info->element, value } }, error)) {
        result.message = std::string(info->label) + " saved.";
    }
    else {
        requestMetadataImport();
        result.message = std::string(info->label) + " saved; RetroFE applies it the next time it imports metadata ("
            + error + ").";
    }
    return result;
}

EditResult Editor::clearMetadata(const Index& index, const std::string& collection, const std::string& game,
                                 const std::string& field)
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    const MetadataOverrides::Field* info = MetadataOverrides::findField(field);
    if (!record || !info) {
        return EditResult::fail("Unknown collection or field.");
    }
    std::string type = record->metadataType;
    std::string file = MetadataOverrides::fileFor(root_, type);
    MetadataOverrides::Overrides overrides;
    std::string error;
    if (!MetadataOverrides::read(file, overrides, error)) {
        return EditResult::fail(error);
    }
    auto it = overrides.find(game);
    if (it == overrides.end() || !it->second.erase(info->element)) {
        return EditResult{ true, std::string(info->label) + " wasn't changed here.", {} };
    }
    if (it->second.empty()) {
        overrides.erase(it);
    }
    if (!MetadataOverrides::write(file, overrides, error)) {
        return EditResult::fail(error);
    }

    // meta.db gets back what RetroFE's own import would give it
    EditResult result{ true, std::string(info->label) + " is back to the shipped value.", { file } };
    std::map<std::string, std::string> shipped;
    std::string problem;
    switch (MetadataOverrides::shipped(root_, type, game, shipped, &problem)) {
    case MetadataOverrides::Shipped::Found:
        if (!writeMetaDb(root_, type, game, { { info->element, shipped[info->element] } }, error)) {
            requestMetadataImport();
            result.message += " RetroFE shows it after its next metadata import (" + error + ").";
        }
        break;
    case MetadataOverrides::Shipped::NotFound: {
        // No shipped file lists the game, so its row exists only because of the
        // user's changes: drop it with the last of them, or reset just this field
        bool lastChange = overrides.find(game) == overrides.end();
        std::string fallback = std::string(info->element) == "description" ? game : "";
        bool ok = lastChange ? withMetaDb(root_, [&](sqlite3* db, std::string& e) { return MetadataOverrides::removeRow(db, type, game, e); }, error)
                             : writeMetaDb(root_, type, game, { { info->element, fallback } }, error);
        if (!ok) {
            requestMetadataImport();
            result.message += " RetroFE shows it after its next metadata import (" + error + ").";
        }
        break;
    }
    case MetadataOverrides::Shipped::Unknown:
        // A shipped file can't be read, so the shipped value isn't known: leave
        // meta.db alone and have RetroFE re-import it on its next start
        requestMetadataImport();
        result.message += " RetroFE re-imports its metadata on its next start (" + problem + ").";
        break;
    }
    return result;
}

void Editor::requestMetadataImport()
{
    // MetadataDatabase::needsRefresh re-imports when anything in the override
    // folder is newer than meta.db; make sure this marker is, whatever the file
    // system's time resolution (FAT keeps 2 s)
    std::error_code ec;
    std::string marker = Utils::combinePath(MetadataOverrides::folder(root_), ".reimport");
    std::string error;
    if (!writeFileAtomic(marker, "Asks RetroFE to re-import its metadata (written by Manage).\n", error)) {
        LOG_ERROR("Library", error);
        return;
    }
    std::string db = Utils::combinePath(root_, "meta.db");
    fs::file_time_type dbTime = fs::last_write_time(db, ec);
    if (!ec && fs::last_write_time(marker, ec) <= dbTime + std::chrono::seconds(2)) {
        fs::last_write_time(marker, dbTime + std::chrono::seconds(3), ec);
    }
}

std::map<std::string, std::string> Editor::metadataOverrides(const Index& index, const std::string& collection,
                                                             const std::string& game) const
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    if (!record) {
        return {};
    }
    MetadataOverrides::Overrides overrides;
    std::string error;
    MetadataOverrides::read(MetadataOverrides::fileFor(root_, record->metadataType), overrides, error);
    auto it = overrides.find(game);
    return it == overrides.end() ? std::map<std::string, std::string>() : it->second;
}

// --- Collection settings ----------------------------------------------------------

const std::vector<std::string>& Editor::editableSettings()
{
    static const std::vector<std::string> keys = {
        "list.extensions", "list.menuSort", "list.includeMissingItems", "list.sortType", "launcher", "metadata.type", "layout",
        "cyclePlaylist",
    };
    return keys;
}

const std::vector<std::string>& Editor::editableInfo()
{
    // type: "videos" marks a video collection (this fork's Manage, DECISIONS M7); RetroFE ignores it
    static const std::vector<std::string> keys = { "title", "manufacturer", "developer", "year", "genre", "type" };
    return keys;
}

std::string Editor::checkSetting(const Index& index, const std::string& collection, const std::string& key,
                                 const std::string& value)
{
    const auto& settings = editableSettings();
    const auto& info = editableInfo();
    bool isSetting = std::find(settings.begin(), settings.end(), key) != settings.end();
    bool isInfo = std::find(info.begin(), info.end(), key) != info.end();
    if (!isSetting && !isInfo) {
        return "\"" + key + "\" can't be changed here.";
    }
    std::string problem = ConfFile::checkValue(value);
    if (!problem.empty()) {
        return problem;
    }
    std::string v = Utils::trimEnds(value);
    if (key == "list.extensions") {
        std::vector<std::string> parts;
        Utils::listToVector(v, parts, ',');
        if (parts.empty()) {
            return "List at least one extension, e.g. zip,7z.";
        }
        for (const std::string& part : parts) {
            if (part[0] == '.') {
                return "Leave out the dots: zip, not .zip.";
            }
            for (unsigned char ch : part) {
                if (!std::isalnum(ch) && ch != '_' && ch != '-') {
                    return "\"" + part + "\" isn't a file extension.";
                }
            }
        }
    }
    else if (key == "list.menuSort" || key == "list.includeMissingItems") {
        if (!isBoolean(v)) {
            return "Use yes or no.";
        }
    }
    else if (key == "list.sortType") {
        static const std::vector<std::string> sortTypes = {
            "year", "manufacturer", "developer", "genre", "numberPlayers", "numberButtons", "ctrlType",
            "joyWays", "rating", "score", "lastPlayed", "playCount",
        };
        if (std::none_of(sortTypes.begin(), sortTypes.end(), [&](const std::string& s) { return iequals(s, v); })) {
            return "Sort by one of: year, manufacturer, developer, genre, numberPlayers, numberButtons, ctrlType, joyWays, rating, score, lastPlayed, playCount.";
        }
    }
    else if (key == "launcher") {
        if (v.empty()) {
            return "Pick a launcher.";
        }
        // A collection's "launcher =" is looked up as written: RetroFE maps a bare
        // local launcher name to <collection>.<name> only for per-game overrides
        if (index.findLauncher(v) < 0) {
            if (index.properties.prefixExists("localLaunchers." + collection + "." + v)) {
                return "\"" + v + "\" is " + collection + "'s own launcher; as its default it's named " + collection + "." + v + ".";
            }
            return "There's no launcher called \"" + v + "\".";
        }
    }
    else if (key == "cyclePlaylist") {
        std::vector<std::string> names;
        Utils::listToVector(v, names, ',');
        for (const std::string& name : names) {
            std::string bad = checkFileName(name, "playlist");
            if (!bad.empty()) {
                return bad;
            }
        }
    }
    else if (key == "type") {
        if (!v.empty() && !iequals(v, "videos")) {
            return "The type is \"videos\" for a video collection, or empty.";
        }
    }
    else if (key == "metadata.type") {
        if (v.empty() || v.find_first_of("\\/:*?\"<>|.") != std::string::npos) {
            return "Use a metadata name such as MAME (the name of a meta/hyperlist file, without .xml).";
        }
    }
    else if (key == "layout") {
        if (index.findLayout(v) < 0 || index.layouts[static_cast<size_t>(index.findLayout(v))].name != v) {
            return "There's no layout called \"" + v + "\" in layouts/.";
        }
    }
    else if (isInfo) {
        if (v.size() > 200) {
            return "Keep it to 200 characters or fewer.";
        }
    }
    return "";
}

EditResult Editor::setCollectionSetting(const Index& index, const std::string& collection, const std::string& key,
                                        const std::string& value)
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + collection + "\".");
    }
    std::string problem = checkSetting(index, record->name, key, value);
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    const auto& info = editableInfo();
    bool isInfo = std::find(info.begin(), info.end(), key) != info.end();
    ConfLayers layers(record->folder, ConfLayers::collectionFileNames());
    size_t target = isInfo ? static_cast<size_t>(layers.indexOf("info.conf")) : 0;
    std::string written = Utils::trimEnds(value);
    if (key == "launcher") {
        // Fork-compat: in a "launcher = a, b, c" list the pick moves to the
        // front (becoming the default) and the alternatives are kept
        std::vector<std::string> alternatives;
        firstLauncher(layers.get("launcher"), &alternatives);
        std::string current = firstLauncher(layers.get("launcher"));
        if (!alternatives.empty()) {
            alternatives.insert(alternatives.begin(), current);
            alternatives.erase(std::remove(alternatives.begin(), alternatives.end(), written), alternatives.end());
            for (const std::string& name : alternatives) {
                written += ", " + name;
            }
        }
    }
    std::string error;
    if (!layers.set(key, written, error, target, "", &backups_)) {
        return EditResult::fail(error);
    }
    EditResult result{ true, "Saved " + key + " in " + layers.source(key) + ".",
                       { Utils::combinePath(record->folder, layers.source(key)) } };
    if (key == "launcher" && index.properties.prefixExists("collectionLaunchers." + record->name)) {
        result.message += " Note: " + record->name + "'s own launcher file still decides how its games start.";
    }
    return result;
}

EditResult Editor::removeCollectionSetting(const Index& index, const std::string& collection, const std::string& key)
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + collection + "\".");
    }
    const auto& settings = editableSettings();
    const auto& info = editableInfo();
    if (std::find(settings.begin(), settings.end(), key) == settings.end() && std::find(info.begin(), info.end(), key) == info.end()) {
        return EditResult::fail("\"" + key + "\" can't be changed here.");
    }
    ConfLayers layers(record->folder, ConfLayers::collectionFileNames());
    std::vector<std::string> files;
    for (size_t i = 0; i < layers.size(); i++) {
        if (layers.file(i).has(key)) {
            files.push_back(layers.file(i).path());
        }
    }
    std::string error;
    if (!layers.remove(key, error, &backups_)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, files.empty() ? key + " wasn't set." : "Removed " + key + ".", files };
}

// --- Menus ----------------------------------------------------------------------

namespace MenuFiles
{
    std::string topMenu(const std::string& root)
    {
        Properties props(root);
        props.loadGlobal();
        std::string top = props.getOr("firstCollection", "Main");
        return top.empty() ? "Main" : top;
    }

    std::string menuForNewCollections(const std::string& root, const std::string& firstCollection)
    {
        if (firstCollection.empty() || !checkChildName(firstCollection).empty()) {
            return "Main";
        }
        std::error_code ec;
        std::string folder = Utils::combinePath(root, "collections", firstCollection);
        if (!fs::is_directory(folder, ec) || fs::exists(Utils::combinePath(folder, "menu.txt"), ec)
            || fs::is_directory(Utils::combinePath(folder, "menu"), ec) || fs::exists(Utils::combinePath(folder, "menu.xml"), ec)) {
            return firstCollection;
        }
        ConfLayers settings(folder, ConfLayers::collectionFileNames());
        bool games = !Utils::trimEnds(settings.get("list.extensions")).empty() || settings.has("list.path")
            || fs::is_directory(Utils::combinePath(folder, "roms"), ec);
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            games |= entry.is_regular_file(ec) && entry.path().extension() == ".sub";
        }
        return games ? "Main" : firstCollection;
    }

    std::string checkChildName(const std::string& name)
    {
        if (Utils::trimEnds(name).empty()) {
            return "The name is empty.";
        }
        if (Utils::trimEnds(name) != name) {
            return "The name can't start or end with a space.";
        }
        if (name.find_first_of("\\/:*?\"<>|#\r\n") != std::string::npos) {
            return "The name can't contain \\ / : * ? \" < > | or #.";
        }
        if (name[0] == '_' || name[0] == '.') {
            return "The name can't start with _ or a dot.";
        }
        if (name.back() == '.') {
            return "The name can't end with a dot.";
        }
        // Windows keeps device names for itself, whatever follows the first dot (con.txt, aux.v2)
        static const std::vector<std::string> devices = { "con", "prn", "aux", "nul",
            "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
            "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
        std::string stem = toLower(Utils::trimEnds(name.substr(0, name.find('.'))));
        if (std::find(devices.begin(), devices.end(), stem) != devices.end()) {
            return "\"" + name + "\" starts with a name Windows keeps for itself (" + stem + ").";
        }
        return "";
    }

    namespace
    {
        // menu/<child>.txt, matched ignoring case; "" when there's none
        std::string folderEntry(const std::string& menuFolder, const std::string& child)
        {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(menuFolder, ec)) {
                std::string name;
                if (entry.is_regular_file(ec) && narrowName(entry.path().filename(), name) && name.size() > 4
                    && name.compare(name.size() - 4, 4, ".txt") == 0 && iequals(name.substr(0, name.size() - 4), child)) {
                    return Utils::combinePath(menuFolder, name);
                }
            }
            return "";
        }

        bool editList(const std::string& path, const std::function<bool(LineList&)>& edit, Backups* backups, std::string& error)
        {
            LineList list(path);
            if (!list.load(&error)) {
                return false;
            }
            if (!edit(list)) {
                return error.empty();
            }
            return list.save(error, backups);
        }

        bool removeFrom(LineList& list, const std::string& child)
        {
            return list.remove(child, LineList::Match::Trimmed) || list.remove(child, LineList::Match::IgnoreCase);
        }
    }

    bool add(const std::string& root, const std::string& menu, const std::string& child, int position,
             Backups* backups, std::string& error)
    {
        // The name becomes a line or, for a menu folder, a file name: no paths
        error = checkChildName(child);
        if (error.empty()) {
            error = checkChildName(menu);
        }
        if (!error.empty()) {
            return false;
        }
        std::error_code ec;
        std::string folder = Utils::combinePath(root, "collections", menu);
        bool newFolder = !fs::is_directory(folder, ec);
        fs::create_directories(folder, ec);
        if (newFolder) {
            // As Setup always did: a settings file so RetroFE doesn't report the menu as unreadable
            std::string settings = Utils::combinePath(folder, "settings.conf");
            if ((backups && !backups->beforeChange(settings, error))
                || !writeFileAtomic(settings, std::string("list.menuSort = yes") + defaultNewline(), error)) {
                return false;
            }
        }

        std::string menuTxt = Utils::combinePath(folder, "menu.txt");
        std::string menuFolder = Utils::combinePath(folder, "menu");
        if (!fs::exists(menuTxt, ec) && !fs::is_directory(menuFolder, ec)
            && fs::exists(Utils::combinePath(folder, "menu.xml"), ec)) {
            // A menu.txt would win over menu.xml and hide everything it lists
            error = menu + "'s menu is a menu.xml, which Manage doesn't change; add " + child + " there.";
            return false;
        }
        if (!fs::exists(menuTxt, ec) && fs::is_directory(menuFolder, ec)) {
            if (folderEntry(menuFolder, child).empty()) {
                std::string file = Utils::combinePath(menuFolder, child + ".txt");
                if ((backups && !backups->beforeChange(file, error)) || !writeFileAtomic(file, "", error)) {
                    return false;
                }
            }
        }
        else if (!editList(menuTxt, [&](LineList& list) {
                     return list.find(child, LineList::Match::IgnoreCase) < 0 && list.add(child, error, position);
                 }, backups, error)) {
            return false;
        }

        std::string supported = Utils::combinePath(folder, "menu_supported.txt");
        if (fs::exists(supported, ec)
            && !editList(supported, [&](LineList& list) {
                   return list.find(child, LineList::Match::IgnoreCase) < 0 && list.add(child, error, position);
               }, backups, error)) {
            return false;
        }
        LOG_INFO("Library", "Listed " + child + " in menu " + menu);
        return true;
    }

    bool remove(const std::string& root, const std::string& menu, const std::string& child, Backups* backups,
                std::string& error, bool* removed)
    {
        std::error_code ec;
        bool any = false;
        std::string folder = Utils::combinePath(root, "collections", menu);
        std::string menuTxt = Utils::combinePath(folder, "menu.txt");
        std::string menuFolder = Utils::combinePath(folder, "menu");
        if (fs::exists(menuTxt, ec)) {
            if (!editList(menuTxt, [&](LineList& list) { return any = removeFrom(list, child); }, backups, error)) {
                return false;
            }
        }
        else if (fs::is_directory(menuFolder, ec) && !checkChildName(child).empty()) {
            // Not a name a menu file can have, so there's no such file
        }
        else if (fs::is_directory(menuFolder, ec)) {
            std::string file = folderEntry(menuFolder, child);
            any = !file.empty();
            if (!file.empty()) {
                if (backups ? !backups->removeKeepingOriginal(file, error) : !fs::remove(file, ec)) {
                    if (error.empty()) {
                        error = "Could not delete " + file;
                    }
                    return false;
                }
            }
        }
        std::string supported = Utils::combinePath(folder, "menu_supported.txt");
        if (fs::exists(supported, ec)
            && !editList(supported, [&](LineList& list) { return removeFrom(list, child); }, backups, error)) {
            return false;
        }
        if (removed) {
            *removed = any;
        }
        return true;
    }

    bool move(const std::string& root, const std::string& menu, const std::string& child, int position,
              Backups* backups, std::string& error)
    {
        std::error_code ec;
        std::string folder = Utils::combinePath(root, "collections", menu);
        std::string menuTxt = Utils::combinePath(folder, "menu.txt");
        if (!fs::exists(menuTxt, ec)) {
            error = fs::is_directory(Utils::combinePath(folder, "menu"), ec)
                ? "This menu lists the files in its menu folder, which RetroFE sorts by name, so their order can't be changed."
                : "This menu has no menu.txt.";
            return false;
        }
        auto moveIn = [&](LineList& list) {
            int from = list.find(child, LineList::Match::Trimmed);
            if (from < 0) {
                from = list.find(child, LineList::Match::IgnoreCase);
            }
            if (from < 0) {
                error = child + " isn't listed in this menu.";
                return false;
            }
            int to = std::clamp(position, 0, static_cast<int>(list.size()) - 1);
            return list.move(from, to);
        };
        if (!editList(menuTxt, moveIn, backups, error)) {
            return false;
        }
        std::string supported = Utils::combinePath(folder, "menu_supported.txt");
        if (fs::exists(supported, ec)) {
            std::string ignored;
            editList(supported, [&](LineList& list) {
                bool moved = moveIn(list);
                error.clear();
                return moved;
            }, backups, ignored);
        }
        return true;
    }

    bool respell(const std::string& root, const std::string& menu, const std::string& entry,
                 const std::string& folderName, int position, Backups* backups, std::string& error)
    {
        error = checkChildName(folderName);
        if (!error.empty()) {
            return false;
        }
        bool removed = false;
        if (!remove(root, menu, entry, backups, error, &removed)) {
            return false;
        }
        if (!removed) {
            error = entry + " isn't listed in " + menu + ".";
            return false;
        }
        return add(root, menu, folderName, position, backups, error);
    }
}

namespace
{
    // A collection that may get menu entries: a menu already, or one with no
    // games of its own (LB9), or the top menu before its folder exists
    std::string checkParentMenu(const Index& index, const std::string& parent, std::string& spelling)
    {
        int p = index.findCollection(parent);
        if (p < 0) {
            if (iequals(parent, index.topMenu) && MenuFiles::checkChildName(parent).empty()) {
                spelling = index.topMenu;
                return "";
            }
            return "There's no menu called \"" + parent + "\".";
        }
        const CollectionRecord& record = index.collections[static_cast<size_t>(p)];
        spelling = record.name;
        if (iequals(record.name, "Home") || iequals(record.name, "Catalog")) {
            return "\"" + record.name + "\" is a menu RetroFE builds itself.";
        }
        if (record.menuSource == MenuSource::MenuXml || record.menuSource == MenuSource::CollectionLaunchers) {
            return record.name + "'s menu isn't a menu.txt or menu folder, so it can't be changed here.";
        }
        if (!record.is(KindMenu) && (record.is(KindSystem) || record.is(KindMerged))) {
            return record.name + " has games of its own, so it can't list other collections.";
        }
        return "";
    }
}

std::string Editor::checkMenuParent(const Index& index, const std::string& parent)
{
    std::string spelling;
    return checkParentMenu(index, parent, spelling);
}

namespace
{

    // A name for a new collection folder
    std::string checkNewCollectionName(const Index& index, const std::string& name)
    {
        std::string problem = MenuFiles::checkChildName(name);
        if (!problem.empty()) {
            return problem;
        }
        if (isReservedCollection(name)) {
            return "\"" + name + "\" is a name RetroFE keeps for its own menus.";
        }
        int existing = index.findCollection(name);
        std::error_code ec;
        if (existing >= 0 || fs::exists(Utils::combinePath(index.options.root, "collections", name), ec)) {
            return "There's already a collection called \""
                + (existing >= 0 ? index.collections[static_cast<size_t>(existing)].name : name) + "\".";
        }
        return "";
    }

    // A system whose games a merged collection can show
    std::string checkSource(const Index& index, const std::string& source, int& s)
    {
        s = index.findCollection(source);
        if (s < 0) {
            return "There's no system called \"" + source + "\".";
        }
        const CollectionRecord& record = index.collections[static_cast<size_t>(s)];
        if (!record.is(KindSystem)) {
            return record.name + " has no games of its own to show.";
        }
        return "";
    }
}

EditResult Editor::createMenu(const Index& index, const std::string& name, const std::string& parent)
{
    return createListed(index, name, parent, "");
}

EditResult Editor::createMerged(const Index& index, const std::string& name, const std::string& parent,
                                const std::string& source)
{
    if (source.empty()) {
        return EditResult::fail("Pick the system whose games it shows.");
    }
    return createListed(index, name, parent, source);
}

EditResult Editor::createListed(const Index& index, const std::string& name, const std::string& parent,
                                const std::string& source)
{
    bool merged = !source.empty();
    std::string problem = checkNewCollectionName(index, name);
    std::string parentName;
    if (problem.empty()) {
        problem = checkParentMenu(index, parent, parentName);
    }
    int s = -1;
    if (problem.empty() && merged) {
        problem = checkSource(index, source, s);
    }
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    std::string folder = Utils::combinePath(index.options.root, "collections", name);
    std::string newline = defaultNewline();
    std::vector<std::pair<std::string, std::string>> files = {
        { Utils::combinePath(folder, "settings.conf"), "list.menuSort = yes" + newline },
        { Utils::combinePath(folder, "info.conf"), "title = " + name + newline },
    };
    if (merged) {
        files.emplace_back(Utils::combinePath(folder, index.collections[static_cast<size_t>(s)].name + ".sub"), "");
    }
    else {
        files.emplace_back(Utils::combinePath(folder, "menu.txt"), "");
    }
    EditResult result;
    std::string error;
    // Undoes a half-made collection, so trying again isn't blocked by its folder
    auto rollBack = [&](const std::string& why) {
        std::string ignored;
        for (const std::string& path : result.files) {
            backups_.removeKeepingOriginal(path, ignored);
        }
        std::error_code ec;
        fs::remove(folder, ec);  // only when empty
        result.ok = false;
        result.message = why + " Nothing was made.";
        return result;
    };
    for (const auto& [path, content] : files) {
        if (!backups_.beforeChange(path, error) || !writeFileAtomic(path, content, error)) {
            return rollBack(error.empty() ? "Could not write " + path + "." : error);
        }
        result.files.push_back(path);
    }
    std::string madeWhat = merged
        ? "Made " + name + ", showing every game of " + index.collections[static_cast<size_t>(s)].name
        : "Made the menu " + name;
    // The top menu made by its own name: Browse opens it, nothing lists it
    if (iequals(parentName, name)) {
        result.ok = true;
        result.message = madeWhat + ". Browse opens it (it's firstCollection).";
        return result;
    }
    if (!MenuFiles::add(index.options.root, parentName, name, -1, &backups_, error)) {
        return rollBack("Couldn't list " + name + " in " + parentName + ": " + error);
    }
    result.ok = true;
    result.files.push_back(Utils::combinePath(index.options.root, "collections", parentName));
    result.message = madeWhat + ", in " + parentName + ".";
    return result;
}

EditResult Editor::addSubSource(const Index& index, const std::string& merged, const std::string& source)
{
    int m;
    const CollectionRecord* record = findRecord(index, merged, m);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + merged + "\".");
    }
    // A merged collection, or one with nothing yet (e.g. its last .sub was removed)
    if (record->is(KindSystem) || record->is(KindMenu) || record->reserved) {
        return EditResult::fail(record->name + " isn't a merged collection.");
    }
    int s;
    std::string problem = checkSource(index, source, s);
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    if (s == m) {
        return EditResult::fail("A collection can't take games from itself.");
    }
    const std::string& sourceName = index.collections[static_cast<size_t>(s)].name;
    for (const SubSource& sub : record->subs) {
        if (sub.collection == s || iequals(sub.source, sourceName)) {
            return EditResult{ true, record->name + " already shows games of " + sourceName + ".", {} };
        }
    }
    std::string file = Utils::combinePath(record->folder, sourceName + ".sub");
    std::string error;
    if (!backups_.beforeChange(file, error) || !writeFileAtomic(file, "", error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, record->name + " now shows every game of " + sourceName + ".", { file } };
}

EditResult Editor::removeSubSource(const Index& index, const std::string& merged, const std::string& source)
{
    int m;
    const CollectionRecord* record = findRecord(index, merged, m);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + merged + "\".");
    }
    const SubSource* sub = nullptr;
    for (const SubSource& candidate : record->subs) {
        if (candidate.source == source) {
            sub = &candidate;
        }
    }
    for (const SubSource& candidate : record->subs) {
        if (!sub && iequals(candidate.source, source)) {
            sub = &candidate;
        }
    }
    if (!sub) {
        return EditResult::fail(record->name + " has no " + source + ".sub.");
    }
    std::string error;
    if (!backups_.removeKeepingOriginal(sub->file, error)) {
        return EditResult::fail(error);
    }
    std::string message = record->name + " no longer shows games of " + sub->source + "; " + sub->source + " itself is unchanged.";
    if (record->subs.size() == 1) {
        message += " It has no sources left, so it shows no games.";
    }
    return EditResult{ true, message, { sub->file } };
}

EditResult Editor::respellSubSource(const Index& index, const std::string& merged, const std::string& source)
{
    int m;
    const CollectionRecord* record = findRecord(index, merged, m);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + merged + "\".");
    }
    const SubSource* sub = nullptr;
    for (const SubSource& candidate : record->subs) {
        if (candidate.source == source) {
            sub = &candidate;
        }
    }
    if (!sub || sub->collection < 0 || sub->exactCase) {
        return EditResult::fail(sub ? source + ".sub is spelt as its system already, or names no system." : record->name + " has no " + source + ".sub.");
    }
    const std::string& system = index.collections[static_cast<size_t>(sub->collection)].name;
    std::string target = Utils::combinePath(record->folder, system + ".sub");
    std::string content;
    if (!readFile(sub->file, content)) {
        return EditResult::fail("Could not read " + sub->file + ".");
    }
    // The old spelling goes to the backups; the lines come back under the system's spelling.
    // (On Windows both names are the same file, so this also works as a case-only rename.)
    std::string error;
    if (!backups_.removeKeepingOriginal(sub->file, error) || !backups_.beforeChange(target, error)
        || !writeFileAtomic(target, content, error)) {
        return EditResult{ false, error, { sub->file, target } };
    }
    return EditResult{ true, sub->source + ".sub is now " + system + ".sub, so RetroFE finds " + system + "'s games.",
                       { sub->file, target } };
}

std::string Editor::checkDeleteMenu(const Index& index, const std::string& name)
{
    int c = index.findCollection(name);
    if (c < 0) {
        return "There's no collection called \"" + name + "\".";
    }
    const CollectionRecord& record = index.collections[static_cast<size_t>(c)];
    if (record.reserved) {
        return "\"" + record.name + "\" is a name RetroFE keeps for its own menus.";
    }
    if (iequals(record.name, index.topMenu)) {
        return "Browse opens " + record.name + ". Choose another menu for Browse in Settings first.";
    }
    if (record.is(KindSystem)) {
        return record.name + " has games of its own; it can only be deleted from Systems.";
    }
    // Its entries would move up into the menus listing it, and Manage can't
    // change a menu.xml or a menu built from collectionLaunchers
    for (int p : record.parents) {
        const CollectionRecord& parent = index.collections[static_cast<size_t>(p)];
        if (parent.menuSource == MenuSource::MenuXml) {
            return parent.name + " lists it from menu.xml; take it out there first.";
        }
        if (parent.menuSource == MenuSource::CollectionLaunchers) {
            return parent.name + " lists it from the collectionLaunchers setting; take it out there first.";
        }
    }
    // Menus, merged collections, and ones with nothing yet (no games, entries or
    // sources) qualify; only what they're made of may be in the folder
    std::error_code ec;
    fs::path folder(record.folder);
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::string relative = narrow(fs::relative(it->path(), folder, ec));
        for (char& ch : relative) {
            if (ch == '\\') {
                ch = '/';
            }
        }
        std::string top = relative.substr(0, relative.find('/'));
        std::string lowerTop = toLower(top);
        bool directory = it->is_directory(ec);
        bool nested = relative.find('/') != std::string::npos;
        if (lowerTop == "system_artwork" || lowerTop == "medium_artwork") {
            continue;
        }
        if (lowerTop == "menu") {
            if (directory || (relative.size() > 4 && toLower(relative.substr(relative.size() - 4)) == ".txt" && relative.find('/', 5) == std::string::npos)) {
                continue;
            }
        }
        else if (lowerTop == "playlists") {
            std::string file = toLower(relative.substr(relative.find('/') + 1));
            if (directory && !nested) {
                continue;
            }
            // lastplayed is history RetroFE writes; favorites someone chose, so only an empty one goes
            if (file == "lastplayed.txt" || (file == "favorites.txt" && fs::file_size(it->path(), ec) == 0)) {
                continue;
            }
        }
        else if (!nested && !directory) {
            static const std::vector<std::string> allowed = { "settings.conf", "info.conf", "menu.txt", "menu_supported.txt", "menu.xml" };
            bool settingsN = lowerTop.rfind("settings", 0) == 0 && lowerTop.size() > 13 && lowerTop.compare(lowerTop.size() - 5, 5, ".conf") == 0;
            bool sub = lowerTop.size() > 4 && lowerTop.compare(lowerTop.size() - 4, 4, ".sub") == 0;
            if (sub || settingsN || std::find(allowed.begin(), allowed.end(), lowerTop) != allowed.end()) {
                continue;
            }
        }
        return record.name + " holds other files (" + relative
            + "), so it can only be deleted by hand. Manage deletes collections that hold only menus, sources, settings and artwork.";
    }
    if (ec) {
        return "Could not read " + record.folder + ": " + ec.message();
    }
    return "";
}

EditResult Editor::deleteMenu(const Index& index, const std::string& name)
{
    std::string problem = checkDeleteMenu(index, name);
    if (problem.empty() && !fs::is_directory(index.collections[static_cast<size_t>(index.findCollection(name))].folder)) {
        problem = "There's no folder for " + name + ".";
    }
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    int c = index.findCollection(name);
    const CollectionRecord& record = index.collections[static_cast<size_t>(c)];
    const std::string& root = index.options.root;
    EditResult result;
    std::string error;
    std::vector<std::string> moved;
    std::vector<std::string> skipped;

    // Files may already have changed when a later step fails: say which, so the index is rebuilt
    auto failed = [&](const std::string& message) { return EditResult{ false, message, result.files }; };

    // 1. Each menu listing it gets its entries in its place (before its first
    // line), then loses every line naming it, whatever their spelling
    for (int p : record.parents) {
        const CollectionRecord& parent = index.collections[static_cast<size_t>(p)];
        int position = -1;
        std::vector<std::string> spellings;
        for (size_t i = 0; i < parent.children.size(); i++) {
            if (parent.children[i].collection != c) {
                continue;
            }
            if (position < 0) {
                position = static_cast<int>(i);
            }
            if (std::find(spellings.begin(), spellings.end(), parent.children[i].entry) == spellings.end()) {
                spellings.push_back(parent.children[i].entry);
            }
        }
        result.files.push_back(parent.folder);
        // Inserted before it, so they take its place once it's removed
        int at = position;
        for (const MenuEntry& child : record.children) {
            if (child.collection < 0 || child.collection == p) {
                skipped.push_back(child.entry);
                continue;
            }
            const CollectionRecord& childRecord = index.collections[static_cast<size_t>(child.collection)];
            bool listed = std::any_of(parent.children.begin(), parent.children.end(),
                                      [&](const MenuEntry& e) { return e.collection == child.collection; });
            bool loop = false;
            for (const MenuRow& row : index.menuTree(childRecord.name)) {
                loop |= row.collection == p;
            }
            if (listed || loop) {
                if (std::find(skipped.begin(), skipped.end(), childRecord.name) == skipped.end()) {
                    skipped.push_back(childRecord.name);
                }
                continue;
            }
            if (!MenuFiles::add(root, parent.name, childRecord.name, at, &backups_, error)) {
                return failed("Could not move " + childRecord.name + " into " + parent.name + ": " + error);
            }
            if (at >= 0) {
                at++;
            }
            if (std::find(moved.begin(), moved.end(), childRecord.name) == moved.end()) {
                moved.push_back(childRecord.name);
            }
        }
        for (const std::string& spelling : spellings) {
            if (!MenuFiles::remove(root, parent.name, spelling, &backups_, error)) {
                return failed("Could not take " + record.name + " out of " + parent.name + ": " + error);
            }
        }
    }

    // 2. Its files go to the backups, then the empty folders
    std::error_code ec;
    std::vector<fs::path> folders;
    std::vector<std::string> files;
    for (auto it = fs::recursive_directory_iterator(record.folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec)) {
            folders.push_back(it->path());
        }
        else {
            files.push_back(narrow(it->path()));
        }
    }
    for (const std::string& file : files) {
        if (!backups_.removeKeepingOriginal(file, error)) {
            return failed("Could not move " + file + " to the backups: " + error);
        }
        result.files.push_back(file);
    }
    std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
    folders.push_back(fs::path(record.folder));
    for (const fs::path& folder : folders) {
        fs::remove(folder, ec);  // only removes empty folders
    }

    result.ok = true;
    result.message = "Deleted " + record.name + ".";
    if (!moved.empty()) {
        std::string list;
        for (const std::string& m : moved) {
            list += (list.empty() ? "" : ", ") + m;
        }
        std::string parents;
        for (int p : record.parents) {
            parents += (parents.empty() ? "" : ", ") + index.collections[static_cast<size_t>(p)].name;
        }
        result.message += " " + list + " moved up into " + parents + ".";
    }
    if (!record.children.empty() && record.parents.empty()) {
        result.message += " No menu listed it, so its entries aren't in any menu now; the collections themselves stay.";
    }
    else if (!skipped.empty()) {
        std::string list;
        for (const std::string& s : skipped) {
            list += (list.empty() ? "" : ", ") + s;
        }
        result.message += " Not moved (already there, a loop, or no such collection): " + list + ".";
    }
    if (record.is(KindMerged)) {
        result.message += " The games it showed stay in their systems.";
    }
    result.message += " Its files are kept in manage/backups.";
    return result;
}

EditResult Editor::addMenuChild(const Index& index, const std::string& menu, const std::string& child, int position)
{
    int m;
    int c;
    const CollectionRecord* menuRecord = findRecord(index, menu, m);
    const CollectionRecord* childRecord = findRecord(index, child, c);
    if (!menuRecord) {
        return EditResult::fail("There's no collection called \"" + menu + "\".");
    }
    if (!childRecord) {
        return EditResult::fail("There's no collection called \"" + child + "\".");
    }
    if (iequals(childRecord->name, "Home") || iequals(childRecord->name, "Catalog")) {
        return EditResult::fail("\"" + childRecord->name + "\" is a name RetroFE keeps for its own menus.");
    }
    if (m == c) {
        return EditResult::fail("A menu can't list itself.");
    }
    std::string parentProblem = checkMenuParent(index, menuRecord->name);
    if (!parentProblem.empty()) {
        return EditResult::fail(parentProblem);
    }
    for (const MenuEntry& entry : menuRecord->children) {
        if (entry.collection == c) {
            return EditResult{ true, childRecord->name + " is already in " + menuRecord->name + ".", {} };
        }
    }
    for (const MenuRow& row : index.menuTree(childRecord->name)) {
        if (row.collection == m) {
            return EditResult::fail(childRecord->name + " already leads to " + menuRecord->name
                + ", so listing it there would make a loop.");
        }
    }
    std::string error;
    if (!MenuFiles::add(index.options.root, menuRecord->name, childRecord->name, position, &backups_, error)) {
        return EditResult::fail(error);
    }
    std::string message = "Added " + childRecord->name + " to " + menuRecord->name + ".";
    if (position >= 0 && menuRecord->menuSource == MenuSource::MenuFolder) {
        message += " This menu lists its menu folder's files, which RetroFE sorts by name, so it goes in name order.";
    }
    return EditResult{ true, message, { menuRecord->folder } };
}

EditResult Editor::removeMenuChild(const Index& index, const std::string& menu, const std::string& child)
{
    int m;
    const CollectionRecord* menuRecord = findRecord(index, menu, m);
    if (!menuRecord) {
        return EditResult::fail("There's no collection called \"" + menu + "\".");
    }
    std::string error;
    bool removed = false;
    if (!MenuFiles::remove(index.options.root, menuRecord->name, child, &backups_, error, &removed)) {
        return EditResult::fail(error);
    }
    if (!removed) {
        return EditResult::fail(child + " isn't listed in " + menuRecord->name + ".");
    }
    return EditResult{ true, "Removed " + child + " from " + menuRecord->name + ". The collection itself is unchanged.",
                       { menuRecord->folder } };
}

EditResult Editor::moveMenuChild(const Index& index, const std::string& menu, const std::string& child, int position)
{
    int m;
    const CollectionRecord* menuRecord = findRecord(index, menu, m);
    if (!menuRecord) {
        return EditResult::fail("There's no collection called \"" + menu + "\".");
    }
    std::string error;
    if (!MenuFiles::move(index.options.root, menuRecord->name, child, position, &backups_, error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Moved " + child + ".", { Utils::combinePath(menuRecord->folder, "menu.txt") } };
}

// --- Playlists and merged collections -----------------------------------------------

namespace
{
    // playlists/<name>.txt as it is on disk now, matching the name ignoring
    // case (the exact spelling wins); "" when there's none
    std::string findPlaylistFile(const std::string& folder, const std::string& name)
    {
        std::error_code ec;
        std::string directory = Utils::combinePath(folder, "playlists");
        std::string found;
        for (const auto& entry : fs::directory_iterator(directory, ec)) {
            std::string file;
            if (!entry.is_regular_file(ec) || !narrowName(entry.path().filename(), file) || file.size() <= 4
                || file.compare(file.size() - 4, 4, ".txt") != 0) {
                continue;
            }
            std::string stem = file.substr(0, file.size() - 4);
            if (stem == name) {
                return Utils::combinePath(directory, file);
            }
            if (found.empty() && iequals(stem, name)) {
                found = Utils::combinePath(directory, file);
            }
        }
        return found;
    }
}

EditResult Editor::addToPlaylist(const Index& index, const std::string& collection, const std::string& playlist,
                                 const std::string& gameCollection, const std::string& game)
{
    int c;
    int s;
    const CollectionRecord* record = findRecord(index, collection, c);
    const CollectionRecord* source = findRecord(index, gameCollection, s);
    if (!record || !source) {
        return EditResult::fail("There's no collection called \"" + (record ? gameCollection : collection) + "\".");
    }
    std::string problem = checkFileName(playlist, "playlist");
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    if (iequals(playlist, "all") || iequals(playlist, "lastplayed")) {
        return EditResult::fail("\"" + playlist + "\" is a list RetroFE makes itself.");
    }
    bool shown = false;
    for (const ItemRef& item : index.itemsOf(c)) {
        shown |= item.collection == s && item.name == game;
    }
    if (!shown) {
        return EditResult::fail(game + " isn't shown in " + record->name + ", so a playlist there can't show it.");
    }

    std::string file = findPlaylistFile(record->folder, playlist);
    if (file.empty()) {
        file = Utils::combinePath(record->folder, "playlists", playlist + ".txt");
    }
    LineList list(file);
    std::string error;
    if (!list.load(&error)) {
        return EditResult::fail(error);
    }
    for (const std::string& entry : list.entries()) {
        LineList::Qualified q = LineList::parseQualified(entry, record->name);
        if (q.collection == source->name && (q.name == game || q.name == "*")) {
            return EditResult{ true, game + " is already in " + playlist + ".", {} };
        }
    }
    if (!list.add(LineList::qualify(source->name, game, record->name), error) || !list.save(error, &backups_)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Added " + game + " to " + playlist + ".", { file } };
}

EditResult Editor::removeFromPlaylist(const Index& index, const std::string& collection, const std::string& playlist,
                                      const std::string& gameCollection, const std::string& game)
{
    int c;
    int s;
    const CollectionRecord* record = findRecord(index, collection, c);
    const CollectionRecord* source = findRecord(index, gameCollection, s);
    if (!record || !source) {
        return EditResult::fail("Unknown collection.");
    }
    // The folder as it is now, so a playlist made since the index was built counts
    std::string file = findPlaylistFile(record->folder, playlist);
    if (file.empty()) {
        return EditResult::fail(record->name + " has no playlist called \"" + playlist + "\".");
    }
    LineList list(file);
    std::string error;
    if (!list.load(&error)) {
        return EditResult::fail(error);
    }
    bool removed = false;
    bool star = false;
    for (const std::string& entry : list.entries()) {
        LineList::Qualified q = LineList::parseQualified(entry, record->name);
        if (q.collection == source->name && q.name == game) {
            removed |= list.remove(entry);
        }
        star |= q.collection == source->name && q.name == "*";
    }
    if (!removed) {
        return EditResult::fail(star ? playlist + " lists every game of " + source->name + " with *, so one can't be taken out."
                                     : game + " isn't in " + playlist + ".");
    }
    if (!list.save(error, &backups_)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Removed " + game + " from " + playlist + ".", { file } };
}

bool Editor::isReservedPlaylist(const std::string& name)
{
    // all is the page itself; favorites and lastplayed are written by RetroFE
    // (CollectionInfoBuilder), so Manage leaves them alone. Index's
    // PlaylistInfo::reserved uses the same names.
    return isReservedPlaylistName(name);
}

EditResult Editor::createPlaylist(const Index& index, const std::string& collection, const std::string& playlist)
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + collection + "\".");
    }
    std::string problem = checkFileName(playlist, "playlist");
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    if (isReservedPlaylist(playlist)) {
        return EditResult::fail("\"" + playlist + "\" is a list RetroFE keeps itself. Pick another name.");
    }
    if (!findPlaylistFile(record->folder, playlist).empty()) {
        return EditResult::fail(record->name + " already has a playlist called \"" + playlist + "\".");
    }
    std::string file = Utils::combinePath(record->folder, "playlists", playlist + ".txt");
    std::string error;
    if (!backups_.beforeChange(file, error) || !writeFileAtomic(file, "", error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Created the playlist " + playlist + ".", { file } };
}

EditResult Editor::renamePlaylist(const Index& index, const std::string& collection, const std::string& playlist,
                                  const std::string& newName)
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + collection + "\".");
    }
    if (isReservedPlaylist(playlist)) {
        return EditResult::fail("\"" + playlist + "\" is a list RetroFE keeps itself, so it can't be renamed.");
    }
    std::string problem = checkFileName(newName, "playlist");
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    if (isReservedPlaylist(newName)) {
        return EditResult::fail("\"" + newName + "\" is a list RetroFE keeps itself. Pick another name.");
    }
    std::string from = findPlaylistFile(record->folder, playlist);
    if (from.empty()) {
        return EditResult::fail(record->name + " has no playlist called \"" + playlist + "\".");
    }
    std::string fromName = Utils::getFileName(from);
    fromName = fromName.substr(0, fromName.size() - 4);
    if (fromName == newName) {
        return EditResult::fail("It's called " + newName + " already.");
    }
    std::string to = Utils::combinePath(record->folder, "playlists", newName + ".txt");
    bool caseOnly = iequals(fromName, newName);
    if (!caseOnly && !findPlaylistFile(record->folder, newName).empty()) {
        return EditResult::fail(record->name + " already has a playlist called \"" + newName + "\".");
    }
    std::string error;
    std::error_code ec;
    if (caseOnly) {
        // The same file on a case-insensitive disk: rename it in two steps
        std::string step = from + ".rename";
        if (!backups_.beforeChange(from, error)) {
            return EditResult::fail(error);
        }
        fs::rename(from, step, ec);
        if (!ec) {
            fs::rename(step, to, ec);
        }
        if (ec) {
            return EditResult::fail("Could not rename " + from + ": " + ec.message());
        }
        return EditResult{ true, "Renamed " + fromName + " to " + newName + ".", { from, to } };
    }
    std::string content;
    if (!readFile(from, content)) {
        return EditResult::fail("Could not read " + from + ".");
    }
    if (!backups_.beforeChange(to, error) || !writeFileAtomic(to, content, error)
        || !backups_.removeKeepingOriginal(from, error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Renamed " + fromName + " to " + newName + ".", { from, to } };
}

EditResult Editor::deletePlaylist(const Index& index, const std::string& collection, const std::string& playlist)
{
    int c;
    const CollectionRecord* record = findRecord(index, collection, c);
    if (!record) {
        return EditResult::fail("There's no collection called \"" + collection + "\".");
    }
    if (isReservedPlaylist(playlist)) {
        return EditResult::fail("\"" + playlist + "\" is a list RetroFE keeps itself, so it can't be deleted.");
    }
    std::string file = findPlaylistFile(record->folder, playlist);
    if (file.empty()) {
        return EditResult::fail(record->name + " has no playlist called \"" + playlist + "\".");
    }
    std::string error;
    if (!backups_.removeKeepingOriginal(file, error)) {
        return EditResult::fail(error);
    }
    std::error_code ec;
    fs::remove(Utils::combinePath(record->folder, "playlists"), ec);   // only when it's empty now
    return EditResult{ true, "Deleted the playlist " + playlist + ". A copy is kept in manage/backups.", { file } };
}

EditResult Editor::addToSub(const Index& index, const std::string& merged, const std::string& sourceName, const std::string& game)
{
    int m;
    int s;
    const CollectionRecord* record = findRecord(index, merged, m);
    const CollectionRecord* source = findRecord(index, sourceName, s);
    if (!record || !source) {
        return EditResult::fail("There's no collection called \"" + (record ? sourceName : merged) + "\".");
    }
    if (m == s) {
        return EditResult::fail("A collection can't take games from itself.");
    }
    bool exists = false;
    for (const Index::Candidate& candidate : index.candidates[static_cast<size_t>(s)]) {
        exists |= candidate.name == game;
    }
    if (!exists) {
        return EditResult::fail(source->name + " has no game file called \"" + game + "\".");
    }
    std::string file = Utils::combinePath(record->folder, source->name + ".sub");
    for (const SubSource& sub : record->subs) {
        if (sub.collection == s) {
            if (!sub.exactCase) {
                return EditResult::fail(sub.source + ".sub is spelt differently from " + source->name
                    + "; rename it to " + source->name + ".sub first.");
            }
            if (sub.entries.empty()) {
                return EditResult{ true, record->name + " already shows every game of " + source->name + ".", {} };
            }
            file = sub.file;
        }
    }
    LineList list(file);
    std::string error;
    if (!list.load(&error)) {
        return EditResult::fail(error);
    }
    if (list.find(game) >= 0) {
        return EditResult{ true, game + " is already in " + record->name + ".", {} };
    }
    if (!list.add(game, error) || !list.save(error, &backups_)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Added " + game + " to " + record->name + ".", { file } };
}

EditResult Editor::removeFromSub(const Index& index, const std::string& merged, const std::string& sourceName, const std::string& game)
{
    int m;
    int s;
    const CollectionRecord* record = findRecord(index, merged, m);
    const CollectionRecord* source = findRecord(index, sourceName, s);
    if (!record || !source) {
        return EditResult::fail("Unknown collection.");
    }
    const SubSource* sub = nullptr;
    for (const SubSource& candidate : record->subs) {
        if (candidate.collection == s) {
            sub = &candidate;
        }
    }
    if (!sub) {
        return EditResult::fail(record->name + " shows no games of " + source->name + ".");
    }
    if (sub->entries.empty()) {
        return EditResult::fail(record->name + " shows every game of " + source->name
            + " (its " + sub->source + ".sub is empty); list the games it should show in it instead.");
    }
    LineList list(sub->file);
    std::string error;
    if (!list.load(&error)) {
        return EditResult::fail(error);
    }
    if (!list.remove(game)) {
        return EditResult::fail(game + " isn't listed in " + sub->source + ".sub.");
    }
    if (list.size() == 0) {
        // An empty .sub would mean every game: remove the file instead
        if (!backups_.removeKeepingOriginal(sub->file, error)) {
            return EditResult::fail(error);
        }
        return EditResult{ true, game + " was the last game of " + source->name + " in " + record->name
            + ", so " + sub->source + ".sub was removed.", { sub->file } };
    }
    if (!list.save(error, &backups_)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Removed " + game + " from " + record->name + ".", { sub->file } };
}

// --- Media --------------------------------------------------------------------------

EditResult Editor::replaceMedia(const MediaSlot& slot, const std::string& file)
{
    if (slot.directory.empty() || slot.basename.empty()) {
        return EditResult::fail("This slot has no folder to put a file in.");
    }
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return EditResult::fail("Can't read " + file + ".");
    }
    std::string extension = toLower(fs::path(file).extension().string());
    if (!extension.empty()) {
        extension.erase(0, 1);
    }
    const std::vector<std::string>& accepted = acceptedExtensions(slot.kind);
    if (std::find(accepted.begin(), accepted.end(), extension) == accepted.end()) {
        std::string list;
        for (const std::string& a : accepted) {
            list += (list.empty() ? "" : ", ") + a;
        }
        return EditResult::fail("Pick a " + list + " file.");
    }
    std::string target = Utils::combinePath(slot.directory, slot.basename + "." + extension);
    if (fs::exists(target, ec) && fs::equivalent(file, target, ec)) {
        return EditResult::fail("That's already this slot's file.");
    }

    // 1. Copy beside the destination, so a failed copy changes nothing
    fs::create_directories(slot.directory, ec);
    std::string temp = Utils::combinePath(slot.directory, "." + slot.basename + "." + extension + ".tmp");
    fs::remove(temp, ec);
    if (!fs::copy_file(file, temp, fs::copy_options::overwrite_existing, ec) || ec) {
        fs::remove(temp, ec);
        return EditResult::fail("Could not copy " + file + " into " + slot.directory + ".");
    }
    // 2. The slot's current files of this kind (any extension) go to the backups
    EditResult result;
    std::string error;
    for (const std::string& old : slotFiles(slot.directory, slot.basename, accepted)) {
        if (!backups_.removeKeepingOriginal(old, error)) {
            fs::remove(temp, ec);
            return EditResult::fail(error);
        }
        result.files.push_back(old);
    }
    // 3. A file that wasn't there before is recorded as new, so a reset removes it
    if (!backups_.beforeChange(target, error)) {
        fs::remove(temp, ec);
        return EditResult::fail(error);
    }
    if (!flushToDisk(temp) || !replaceFile(temp, target)) {
        fs::remove(temp, ec);
        return EditResult::fail("Could not put the new file in place: " + target);
    }
    result.ok = true;
    result.files.push_back(target);
    result.message = "The " + slot.type + " is now " + fs::path(file).filename().string() + ".";
    return result;
}

EditResult Editor::clearMedia(const MediaSlot& slot)
{
    if (!slot.own) {
        return EditResult::fail(slot.file.empty()
            ? "There's no " + slot.type + " to clear."
            : "This shows another folder's file (" + slot.fallback + "); there's no " + slot.type + " of its own to clear.");
    }
    EditResult result;
    std::string error;
    for (const std::string& file : slotFiles(slot.directory, slot.basename, acceptedExtensions(slot.kind))) {
        if (!backups_.removeKeepingOriginal(file, error)) {
            return EditResult::fail(error);
        }
        result.files.push_back(file);
    }
    result.ok = true;
    result.message = "Cleared the " + slot.type + ".";
    return result;
}

// --- Launchers ----------------------------------------------------------------------

namespace
{
    // Systems using a launcher (as their default or an alternative) and games picking it
    void launcherUsers(const Index& index, int launcher, size_t& systems, size_t& games)
    {
        std::vector<int> users;
        games = 0;
        for (const LauncherUse& use : index.launcherUses) {
            if (use.launcher != launcher) {
                continue;
            }
            if (use.game.empty()) {
                users.push_back(use.collection);
            }
            else {
                games++;
            }
        }
        for (size_t c = 0; c < index.collections.size(); c++) {
            for (const std::string& alternative : index.collections[c].launcher.alternatives) {
                if (index.findLauncher(alternative) == launcher) {
                    users.push_back(static_cast<int>(c));
                }
            }
        }
        std::sort(users.begin(), users.end());
        systems = static_cast<size_t>(std::unique(users.begin(), users.end()) - users.begin());
    }

    std::string count(size_t n, const std::string& one, const std::string& many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    // A value written into a new launcher file: one line, and no '#', which RetroFE reads as a comment
    std::string checkLauncherValue(const std::string& value, const std::string& what)
    {
        return value.find_first_of("#\r\n") == std::string::npos ? "" : "The " + what + " can't contain # or a line break.";
    }
}

std::string Editor::checkLauncherName(const std::string& name)
{
    std::string problem = checkFileName(name, "launcher");
    if (!problem.empty()) {
        return problem;
    }
    // '=' and ',' would break "launcher = a, b" lines, '%' looks like a variable
    if (name.find_first_of("=,;%") != std::string::npos) {
        return "A launcher name can't contain = , ; or %.";
    }
    if (name.size() > 64) {
        return "A launcher name can be 64 characters at most.";
    }
    if (name.back() == '.') {
        return "A launcher name can't end with a dot.";
    }
    // Plain ASCII: RetroFE reads launcher file names in the ANSI code page while
    // settings are UTF-8, so other names wouldn't match reliably
    for (unsigned char c : name) {
        if (c < 0x20 || c > 0x7e) {
            return "A launcher name can only use plain letters, digits, spaces and punctuation (no accents).";
        }
    }
    // Windows keeps device names for itself, whatever follows the first dot (con.txt, aux.v2)
    static const std::vector<std::string> reserved = { "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
    std::string stem = toLower(Utils::trimEnds(name.substr(0, name.find('.'))));
    if (std::find(reserved.begin(), reserved.end(), stem) != reserved.end()) {
        return "\"" + name + "\" starts with a name Windows keeps for itself (" + stem + ").";
    }
    return "";
}

std::string Editor::checkNewLauncherName(const Index& index, const std::string& name)
{
    std::string problem = checkLauncherName(name);
    if (!problem.empty()) {
        return problem;
    }
    // Launcher::launcherExecutable looks for localLaunchers.<name>, then
    // collectionLaunchers.<name>, then launchers.<name>: a global launcher
    // with another launcher's id would never run
    for (const LauncherDef& def : index.launchers) {
        if (!iequals(def.id, name)) {
            continue;
        }
        switch (def.scope) {
        case LauncherScope::Global:
            return "There's already a launcher called \"" + def.id + "\".";
        case LauncherScope::Collection:
            return def.collection + " has its own launcher file, which RetroFE looks up by the name \"" + def.id
                + "\" before global launchers, so a launcher with that name would never run.";
        case LauncherScope::Local:
            return "\"" + def.id + "\" is a local launcher of " + def.collection
                + ", which RetroFE looks up before global launchers, so a launcher with that name would never run.";
        }
    }
    size_t dot = name.find('.');
    if (dot != std::string::npos) {
        int collection = index.findCollection(name.substr(0, dot));
        if (collection >= 0) {
            const std::string& owner = index.collections[static_cast<size_t>(collection)].name;
            return "Names like \"" + owner + ".<name>\" belong to " + owner
                + "'s local launchers, which RetroFE looks up first. Choose a name without \"" + owner + ".\" in front.";
        }
    }
    return "";
}

EditResult Editor::addLauncher(const Index& index, const NewLauncher& launcher)
{
    const std::string& name = launcher.name;
    std::string problem = checkNewLauncherName(index, name);
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    std::string executable = Utils::trimEnds(launcher.executable);
    std::string arguments = Utils::trimEnds(launcher.arguments);
    std::string directory = Utils::trimEnds(launcher.currentDirectory);
    if (executable.empty()) {
        return EditResult::fail("Choose the program the launcher runs.");
    }
    for (const auto& [value, what] : { std::make_pair(executable, "program"), std::make_pair(arguments, "arguments"),
                                       std::make_pair(directory, "working folder") }) {
        problem = checkLauncherValue(value, what);
        if (!problem.empty()) {
            return EditResult::fail(problem);
        }
    }

    std::string os = index.options.os.empty() ? Utils::getOSType() : index.options.os;
    std::string folder = Utils::combinePath(root_, "launchers." + os);
    std::string file = Utils::combinePath(folder, name + ".conf");
    // The folder may have changed since the snapshot: refuse any spelling of the file
    std::error_code ec;
    if (fs::is_directory(folder, ec)) {
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            std::string existing;
            if (narrowName(entry.path().filename(), existing) && iequals(existing, name + ".conf")) {
                return EditResult::fail("There's already a launcher file called " + existing + ".");
            }
        }
    }

    std::string newline = defaultNewline();
    std::string content = "executable = " + executable + newline;
    if (!arguments.empty()) {
        content += "arguments = " + arguments + newline;
    }
    if (!directory.empty()) {
        content += "currentDirectory = " + directory + newline;
    }
    if (launcher.reboot) {
        content += "reboot = yes" + newline;
    }
    std::string error;
    if (!backups_.beforeChange(file, error) || !writeFileAtomic(file, content, error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Created the launcher " + name + ".", { file } };
}

EditResult Editor::removeLauncher(const Index& index, LauncherScope scope, const std::string& id)
{
    int launcher = index.findLauncher(scope, id);
    if (launcher < 0) {
        return EditResult::fail("There's no launcher called \"" + id + "\".");
    }
    const LauncherDef& def = index.launchers[static_cast<size_t>(launcher)];
    size_t systems = 0;
    size_t games = 0;
    launcherUsers(index, launcher, systems, games);
    if (systems > 0 || games > 0) {
        std::string users = systems > 0 && games > 0
            ? count(systems, "system", "systems") + " and " + count(games, "game", "games")
            : systems > 0 ? count(systems, "system", "systems") : count(games, "game", "games");
        return EditResult::fail(def.id + " is still used by " + users + ". Give them another launcher first.");
    }
    if (!isInside(def.file, root_)) {
        return EditResult::fail(def.file + " isn't in RetroFE's folder, so it's left alone.");
    }
    if (def.scope == LauncherScope::Collection) {
        // launcher.<os>.conf hides launcher.conf: deleting it would let that one take over under the same name
        std::string generic = Utils::combinePath(Utils::getDirectory(def.file), "launcher.conf");
        std::error_code ec;
        if (!iequals(normalPath(def.file), normalPath(generic)) && fs::is_regular_file(generic, ec)) {
            return EditResult::fail(def.collection + " also has launcher.conf, which RetroFE would use in its place, "
                "so deleting " + Utils::getFileName(def.file) + " wouldn't remove " + def.collection + "'s launcher.");
        }
    }
    std::string error;
    if (!backups_.removeKeepingOriginal(def.file, error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Deleted the launcher " + def.id + ". Its program stays on the drive.", { def.file } };
}

EditResult Editor::writeMediaText(const MediaSlot& slot, const std::string& text)
{
    if (slot.kind != MediaKind::Text) {
        return EditResult::fail("The " + slot.type + " isn't a text slot.");
    }
    if (slot.directory.empty() || slot.basename.empty()) {
        return EditResult::fail("This slot has no folder to put a file in.");
    }
    if (Utils::trimEnds(text).empty()) {
        return EditResult::fail("The text is empty; clear the " + slot.type + " instead.");
    }
    if (text.size() > 256 * 1024) {
        return EditResult::fail("The text is too long (at most 256 KB).");
    }
    // An existing own file keeps its spelling (the file system may ignore case)
    std::string target = Utils::combinePath(slot.directory, slot.basename + ".txt");
    std::vector<std::string> existing = slotFiles(slot.directory, slot.basename, acceptedExtensions(MediaKind::Text));
    if (!existing.empty()) {
        target = existing.front();
    }
    std::string error;
    if (!backups_.beforeChange(target, error) || !writeFileAtomic(target, text, error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Saved the " + slot.type + ".", { target } };
}

EditResult Editor::restoreOriginal(const std::string& file)
{
    std::string error;
    if (!backups_.restore(file, error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Put back the original.", { file } };
}

}

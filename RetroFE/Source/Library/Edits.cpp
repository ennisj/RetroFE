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
    };
    return keys;
}

const std::vector<std::string>& Editor::editableInfo()
{
    static const std::vector<std::string> keys = { "title", "manufacturer", "developer", "year", "genre" };
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
        if (index.findLauncher(v) < 0 && !index.properties.prefixExists("localLaunchers." + collection + "." + v)) {
            return "There's no launcher called \"" + v + "\".";
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
            if (!writeFileAtomic(settings, std::string("list.menuSort = yes") + defaultNewline(), error)) {
                return false;
            }
        }

        std::string menuTxt = Utils::combinePath(folder, "menu.txt");
        std::string menuFolder = Utils::combinePath(folder, "menu");
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

    std::string file = Utils::combinePath(record->folder, "playlists", playlist + ".txt");
    for (const PlaylistInfo& existing : record->playlists) {
        if (iequals(existing.name, playlist)) {
            file = existing.file;
        }
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
    const PlaylistInfo* info = nullptr;
    for (const PlaylistInfo& existing : record->playlists) {
        if (iequals(existing.name, playlist)) {
            info = &existing;
        }
    }
    if (!info) {
        return EditResult::fail(record->name + " has no playlist called \"" + playlist + "\".");
    }
    LineList list(info->file);
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
    return EditResult{ true, "Removed " + game + " from " + playlist + ".", { info->file } };
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
    static const std::vector<std::string> reserved = { "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
    if (std::find(reserved.begin(), reserved.end(), toLower(name)) != reserved.end()) {
        return "\"" + name + "\" is a name Windows keeps for itself.";
    }
    return "";
}

EditResult Editor::addLauncher(const Index& index, const NewLauncher& launcher)
{
    const std::string& name = launcher.name;
    std::string problem = checkLauncherName(name);
    if (!problem.empty()) {
        return EditResult::fail(problem);
    }
    for (const LauncherDef& def : index.launchers) {
        if (def.scope == LauncherScope::Global && iequals(def.name, name)) {
            return EditResult::fail("There's already a launcher called \"" + def.name + "\".");
        }
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
    std::string error;
    if (!backups_.removeKeepingOriginal(def.file, error)) {
        return EditResult::fail(error);
    }
    return EditResult{ true, "Deleted the launcher " + def.id + ". Its program stays on the drive.", { def.file } };
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

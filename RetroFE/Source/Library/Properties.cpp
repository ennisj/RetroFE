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
#include "Properties.h"
#include "Files.h"
#include "../Builtin/Builtin.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{

std::string firstLauncher(const std::string& value, std::vector<std::string>* rest)
{
    if (value.find(',') == std::string::npos) {
        return value;
    }
    std::vector<std::string> names;
    Utils::listToVector(value, names, ',');
    if (names.empty()) {
        return "";
    }
    if (rest) {
        rest->assign(names.begin() + 1, names.end());
    }
    return names.front();
}

Properties::Properties(std::string root)
    : root_(std::move(root))
{
    baseMediaPath_ = Utils::combinePath(root_, "collections");
    baseItemPath_ = baseMediaPath_;
}

void Properties::parseLine(const std::string& collection, std::string prefix, std::string line, const std::string& file)
{
    // Configuration::parseLine, step for step
    line = Utils::filterComments(line);
    if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos) {
        return;
    }
    size_t position = line.find('=');
    if (position == std::string::npos) {
        return;
    }
    if (!prefix.empty()) {
        prefix += ".";
    }
    std::string key = Utils::trimEnds(prefix + line.substr(0, position));
    std::string value = Utils::trimEnds(line.substr(position + 1));
    if (!collection.empty()) {
        value = Utils::replace(value, "%ITEM_COLLECTION_NAME%", collection);
    }
    values_[key] = Value{ value, file };
}

bool Properties::importFile(const std::string& collection, const std::string& prefix, const std::string& path)
{
    std::string content;
    if (!readFile(path, content)) {
        return false;
    }
    for (const Line& line : Lines::parse(content).lines) {
        parseLine(collection, prefix, line.text, path);
    }
    return true;
}

void Properties::importText(const std::string& prefix, const std::string& text, const std::string& file)
{
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        parseLine("", prefix, line, file);
    }
}

void Properties::loadGlobal()
{
    std::error_code ec;
    std::string settings = Utils::combinePath(root_, "settings.conf");
    if (!fs::exists(settings, ec)) {
        importText("", Builtin::settings, "");
    }
    else {
        importFile("", "", settings);
    }
    for (int i = 1; i < 16; i++) {
        std::string numbered = Utils::combinePath(root_, "settings" + std::to_string(i) + ".conf");
        if (fs::exists(numbered, ec)) {
            importFile("", "", numbered);
        }
    }
    std::string saved = Utils::combinePath(root_, "settings_saved.conf");
    if (fs::exists(saved, ec)) {
        importFile("", "", saved);
    }

    // Configuration::getProperty freezes these on its first call, which
    // happens after the global files are read
    baseMediaPath_ = Utils::combinePath(root_, "collections");
    baseItemPath_ = baseMediaPath_;
    getRaw("baseMediaPath", baseMediaPath_);
    getRaw("baseItemPath", baseItemPath_);
}

bool Properties::load(const std::string& os, const std::atomic<bool>* cancel)
{
    values_.clear();
    loadGlobal();

    std::error_code ec;
    fs::path launchersPath = Utils::combinePath(root_, "launchers." + os);
    if (fs::is_directory(launchersPath, ec)) {
        for (const auto& entry : fs::directory_iterator(launchersPath, ec)) {
            std::string file;
            if (entry.is_regular_file(ec) && entry.path().extension() == ".conf" && narrowName(entry.path(), file)) {
                std::string stem;
                narrowName(entry.path().stem(), stem);
                importFile("", "launchers." + stem, file);
            }
        }
    }

    fs::path collectionsPath = Utils::combinePath(root_, "collections");
    if (!fs::is_directory(collectionsPath, ec)) {
        return true;
    }
    for (const auto& entry : fs::directory_iterator(collectionsPath, ec)) {
        if (cancel && cancel->load()) {
            return false;
        }
        std::string collection;
        if (!narrowName(entry.path().filename(), collection)) {
            continue;
        }
        if (!entry.is_directory(ec) || collection.empty() || collection[0] == '_' || collection == "." || collection == "..") {
            continue;
        }
        std::string folder = Utils::combinePath(collectionsPath.string(), collection);
        std::string prefix = "collections." + collection;
        std::string settings = Utils::combinePath(folder, "settings.conf");
        if (fs::exists(settings, ec)) {
            importFile(collection, prefix, settings);
        }
        for (int i = 1; i < 16; i++) {
            std::string numbered = Utils::combinePath(folder, "settings" + std::to_string(i) + ".conf");
            if (fs::exists(numbered, ec)) {
                importFile(collection, prefix, numbered);
            }
        }
        std::string info = Utils::combinePath(folder, "info.conf");
        if (fs::exists(info, ec)) {
            importFile(collection, prefix, info);
        }

        std::string osLauncher = Utils::combinePath(folder, "launcher." + os + ".conf");
        std::string defaultLauncher = Utils::combinePath(folder, "launcher.conf");
        std::string launcherFile = fs::exists(osLauncher, ec) ? osLauncher : fs::exists(defaultLauncher, ec) ? defaultLauncher : "";
        if (!launcherFile.empty()) {
            importFile(collection, "collectionLaunchers." + collection, launcherFile);
            if (!exists(prefix + ".launcher")) {
                set(prefix + ".launcher", collection);
            }
        }

        fs::path localPath = Utils::combinePath(folder, "launchers." + os + ".local");
        fs::path defaultLocalPath = Utils::combinePath(folder, "launchers.local");
        if (!fs::is_directory(localPath, ec) && fs::is_directory(defaultLocalPath, ec)) {
            localPath = defaultLocalPath;
        }
        if (fs::is_directory(localPath, ec)) {
            for (const auto& launcherEntry : fs::directory_iterator(localPath, ec)) {
                std::string file;
                if (launcherEntry.is_regular_file(ec) && launcherEntry.path().extension() == ".conf"
                    && narrowName(launcherEntry.path(), file)) {
                    std::string stem;
                    narrowName(launcherEntry.path().stem(), stem);
                    importFile(collection, "localLaunchers." + collection + "." + stem, file);
                }
            }
        }

        if (!launcherFile.empty() && (collection.size() < 3 || collection.substr(collection.size() - 3) != "SUB")) {
            set("collectionLaunchers", getOr("collectionLaunchers") + collection + ",");
        }
    }
    return true;
}

bool Properties::getRaw(const std::string& key, std::string& value) const
{
    auto it = values_.find(key);
    if (it == values_.end()) {
        return false;
    }
    value = it->second.value;
    return true;
}

std::string Properties::expand(const std::string& value) const
{
    std::string result = value;
    if (result.find("%BASE_MEDIA_PATH%") != std::string::npos) {
        result = Utils::replace(result, "%BASE_MEDIA_PATH%", baseMediaPath_);
    }
    if (result.find("%BASE_ITEM_PATH%") != std::string::npos) {
        result = Utils::replace(result, "%BASE_ITEM_PATH%", baseItemPath_);
    }
    return result;
}

bool Properties::get(const std::string& key, std::string& value) const
{
    std::string raw;
    if (!getRaw(key, raw)) {
        return false;
    }
    value = expand(raw);
    return true;
}

std::string Properties::getOr(const std::string& key, const std::string& fallback) const
{
    std::string value;
    return get(key, value) ? value : fallback;
}

bool Properties::getBool(const std::string& key, bool& value) const
{
    std::string text;
    if (!get(key, text)) {
        return false;
    }
    text = toLower(text);
    value = text == "yes" || text == "true" || text == "on";
    return true;
}

std::string Properties::absolute(const std::string& path) const
{
    fs::path p(path);
    if (p.is_absolute()) {
        return path;
    }
    return narrow(fs::absolute(fs::path(root_) / p));
}

bool Properties::getAbsolutePath(const std::string& key, std::string& value) const
{
    if (!get(key, value)) {
        return false;
    }
    // Fork compat (OneSauce settings.conf: baseMediaPath = %RETROFE_PATH%/../..),
    // as in Configuration::getPropertyAbsolutePath. Only paths: get() stays raw.
    if (value.find("%RETROFE_PATH%") != std::string::npos) {
        value = Utils::replace(value, "%RETROFE_PATH%", root_);
    }
#ifndef WIN32
    std::replace(value.begin(), value.end(), '\\', '/');
#endif
    value = absolute(value);
    return true;
}

bool Properties::exists(const std::string& key) const
{
    return values_.count(key) > 0;
}

bool Properties::prefixExists(const std::string& prefix) const
{
    std::string search = prefix + ".";
    auto it = values_.lower_bound(search);
    return it != values_.end() && it->first.compare(0, search.size(), search) == 0;
}

std::vector<std::string> Properties::keysWithPrefix(const std::string& prefix) const
{
    std::vector<std::string> keys;
    std::string search = prefix + ".";
    for (auto it = values_.lower_bound(search); it != values_.end() && it->first.compare(0, search.size(), search) == 0; ++it) {
        keys.push_back(it->first);
    }
    return keys;
}

std::string Properties::sourceFile(const std::string& key) const
{
    auto it = values_.find(key);
    return it == values_.end() ? "" : it->second.file;
}

void Properties::set(const std::string& key, const std::string& value, const std::string& file)
{
    values_[key] = Value{ value, file };
    // Overriding a base path (ScanOptions) changes what %BASE_*% expands to
    if (key == "baseMediaPath") {
        baseMediaPath_ = value;
    }
    else if (key == "baseItemPath") {
        baseItemPath_ = value;
    }
}

std::string Properties::collectionPath(const std::string& collection) const
{
    std::string value;
    if (getAbsolutePath("collections." + collection + ".list.path", value)) {
        return value;
    }
    if (getAbsolutePath("baseItemPath", value)) {
        return Utils::combinePath(value, collection);
    }
    return Utils::combinePath(root_, "collections", collection, "roms");
}

std::string Properties::mediaPath(const std::string& collection, const std::string& type, bool system) const
{
    std::string value;
    std::string key = "collections." + collection + (system ? ".media.system_artwork" : ".media." + type);
    if (getAbsolutePath(key, value)) {
        return value;
    }
    // Fork-compat: OneSauce's media.basePath names the folder holding the
    // collection's medium_artwork and system_artwork
    if (getAbsolutePath("collections." + collection + ".media.basePath", value)) {
        return system ? Utils::combinePath(value, "system_artwork") : Utils::combinePath(value, "medium_artwork", type);
    }
    std::string baseMediaPath;
    if (!getAbsolutePath("baseMediaPath", baseMediaPath)) {
        baseMediaPath = Utils::combinePath(root_, "collections");
    }
    return system ? Utils::combinePath(baseMediaPath, collection, "system_artwork")
                  : Utils::combinePath(baseMediaPath, collection, "medium_artwork", type);
}

}

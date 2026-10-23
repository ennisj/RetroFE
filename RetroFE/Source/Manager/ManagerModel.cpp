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
#include "ManagerModel.h"
#include "Setup.h"
#include "../Builtin/Builtin.h"
#include "../Database/Configuration.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    // For the main screen RetroFE reads these keys, first one found wins
    const std::map<std::string, std::vector<std::string>> screenKeys = {
        { "fullscreen", { "fullscreen", "fullscreen0" } },
        { "horizontal", { "horizontal0", "horizontal" } },
        { "vertical",   { "vertical0", "vertical" } },
    };

    bool stripPrefix(const std::string& key, const std::string& prefix, std::string& rest)
    {
        if (key.compare(0, prefix.size(), prefix) != 0) {
            return false;
        }
        rest = key.substr(prefix.size());
        return true;
    }

    // What a new settings.conf starts with: the built-in defaults RetroFE was running on
    std::string initialSettings()
    {
        return std::string("# Created by RetroFE.\n"
                           "# Run \"retrofe -createconfig\" to get a reference of every setting.\n")
            + Builtin::settings;
    }

}

ManagerModel::ManagerModel(Configuration& config)
    : config_(config)
    , settings_(Configuration::absolutePath)
{
}

void ManagerModel::refresh()
{
    settings_.load();
    pendingSettings_.clear();
    collections_ = Setup::listCollections();
    countedPath_.clear();
    countedExtensions_.clear();
    romCountText_.clear();
}

std::string ManagerModel::settingKey(const std::string& key) const
{
    auto keys = screenKeys.find(key);
    if (keys != screenKeys.end()) {
        for (const std::string& candidate : keys->second) {
            if (settings_.has(candidate)) {
                return candidate;
            }
        }
    }
    return key;
}

std::string ManagerModel::settingValue(const std::string& key)
{
    std::string fileKey = settingKey(key);
    if (settings_.has(fileKey)) {
        return settings_.get(fileKey);
    }
    // Not in any file: what RetroFE is actually using, e.g. the built-in defaults
    std::string value;
    if (config_.getProperty(fileKey, value)) {
        return value;
    }
    auto fallback = settingDefaults_.find(key);
    return fallback != settingDefaults_.end() ? fallback->second : "";
}

void ManagerModel::setDefault(const std::string& key, const std::string& value)
{
    std::string name;
    if (stripPrefix(key, "settings:", name)) {
        settingDefaults_[name] = value;
    }
    else if (stripPrefix(key, "draft:", name) && drafts_[name].empty()) {
        drafts_[name] = value;
    }
}

std::string ManagerModel::get(const std::string& key)
{
    std::string name;
    if (stripPrefix(key, "settings:", name)) {
        auto pending = pendingSettings_.find(name);
        return pending != pendingSettings_.end() ? pending->second : settingValue(name);
    }
    if (stripPrefix(key, "draft:", name)) {
        return drafts_[name];
    }
    if (key == "info:romCount") {
        return romCount();
    }
    if (stripPrefix(key, "info:source:settings:", name)) {
        std::string source = settings_.source(settingKey(name));
        return source == "settings.conf" ? "" : source;
    }
    return "";
}

void ManagerModel::set(const std::string& key, const std::string& value)
{
    std::string name;
    if (stripPrefix(key, "settings:", name)) {
        std::string saved = settingValue(name);
        std::string newValue = value;

        // Keep the file's style for switches: true/false stays true/false
        std::string savedLower = Utils::toLower(saved);
        if ((value == "yes" || value == "no") && (savedLower == "true" || savedLower == "false")) {
            newValue = value == "yes" ? "true" : "false";
        }

        if (newValue == saved) {
            pendingSettings_.erase(name);
        }
        else {
            pendingSettings_[name] = newValue;
        }
    }
    else if (stripPrefix(key, "draft:", name)) {
        drafts_[name] = value;
    }
}

std::vector<std::string> ManagerModel::list(const std::string& key)
{
    if (key == "info:collections") {
        return collections_;
    }
    return {};
}

std::vector<std::pair<std::string, std::string>> ManagerModel::options(const std::string& source)
{
    std::vector<std::pair<std::string, std::string>> result;

    if (source == "layouts") {
        result.emplace_back("", "Built-in");
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(Utils::combinePath(Configuration::absolutePath, "layouts"), ec)) {
            if (entry.is_directory()) {
                names.push_back(entry.path().filename().string());
            }
        }
        std::sort(names.begin(), names.end());
        for (const std::string& name : names) {
            result.emplace_back(name, name);
        }
    }
    else if (source == "collections") {
        result.emplace_back("Main", "Main");
        for (const std::string& name : collections_) {
            result.emplace_back(name, name);
        }
    }
    return result;
}

std::string ManagerModel::actionProblem(const std::string& action)
{
    if (action == "settings.save") {
        return pendingSettings_.empty() ? "No changes to save." : "";
    }
    if (action == "collection.create") {
        return Setup::validate(Setup::NewCollection{ drafts_["name"], drafts_["romPath"], drafts_["extensions"],
                                                     drafts_["executable"], drafts_["arguments"] });
    }
    return "Unknown action " + action;
}

Forms::Model::Result ManagerModel::runAction(const std::string& action)
{
    if (action == "settings.save") {
        return saveSettings();
    }
    if (action == "collection.create") {
        return createCollection();
    }
    Result result;
    result.message = "Unknown action " + action;
    return result;
}

std::string ManagerModel::romCount()
{
    const std::string& path = drafts_["romPath"];
    const std::string& extensions = drafts_["extensions"];
    if (path != countedPath_ || extensions != countedExtensions_) {
        countedPath_ = path;
        countedExtensions_ = extensions;
        if (path.empty()) {
            romCountText_.clear();
        }
        else {
            int count = Setup::countRoms(path, extensions);
            romCountText_ = count < 0 ? "Folder not found"
                          : std::to_string(count) + (count == 1 ? " matching file" : " matching files");
        }
    }
    return romCountText_;
}

Forms::Model::Result ManagerModel::saveSettings()
{
    Result result;
    for (const auto& [name, value] : pendingSettings_) {
        std::string error;
        if (!settings_.set(settingKey(name), value, initialSettings(), error)) {
            result.message = error;
            return result;
        }
        LOG_NOTICE("Manager", "Set " + settingKey(name) + " = " + value);
    }
    pendingSettings_.clear();

    result.ok = true;
    result.reload = true;
    result.message = "Saved. Display changes apply the next time RetroFE starts.";
    return result;
}

Forms::Model::Result ManagerModel::createCollection()
{
    Result result;
    Setup::NewCollection collection{ drafts_["name"], drafts_["romPath"], drafts_["extensions"],
                                     drafts_["executable"], drafts_["arguments"] };

    // A bare install keeps its built-in defaults, Home menu included, from here on
    if (!settings_.ensureExists(initialSettings(), result.message)) {
        return result;
    }
    if (!Setup::createCollection(collection, result.message)) {
        return result;
    }

    LOG_NOTICE("Manager", "Created collection " + Utils::trimEnds(collection.name));
    drafts_["name"].clear();
    result.ok = true;
    result.reload = true;
    result.message = "Created " + Utils::trimEnds(collection.name) + ".";
    return result;
}

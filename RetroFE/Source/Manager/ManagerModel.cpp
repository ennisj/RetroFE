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
#include "../Catalog/Download.h"
#include "../Catalog/Service.h"
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


    // The source the Sources form describes
    Catalog::SourceConfig draftSource(std::map<std::string, std::string>& drafts)
    {
        Catalog::SourceConfig source;
        source.name = Utils::trimEnds(drafts["sourceName"]);
        source.type = drafts["sourceType"].empty() ? "index" : drafts["sourceType"];
        if (source.type == "folder") {
            source.path = Utils::trimEnds(drafts["sourcePath"]);
        }
        else {
            source.url = Utils::trimEnds(drafts["sourceUrl"]);
        }
        return source;
    }

}

// What a new settings.conf starts with: the built-in defaults RetroFE was running on
std::string ManagerModel::initialSettings()
{
    return std::string("# Created by RetroFE.\n"
                       "# Run \"retrofe -createconfig\" to get a reference of every setting.\n")
        + Builtin::settings;
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
        // A layout's form can't pre-fill a source or vouch for one: the user adds sources
        if (name == "sourceTrusted" || name == "sourceUrl" || name == "sourcePath") {
            return;
        }
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
        // Removing starts on the first source or package
        if (name == "removeSource" && drafts_[name].empty() && catalog_) {
            std::vector<Catalog::SourceConfig> sources = catalog_->sources();
            return sources.empty() ? "" : sources.front().name;
        }
        if (name == "removePackage" && drafts_[name].empty() && catalog_) {
            std::vector<Catalog::Manifest> installed = catalog_->installed();
            return installed.empty() ? "" : installed.front().id;
        }
        return drafts_[name];
    }
    if (key == "info:romCount") {
        return romCount();
    }
    if (stripPrefix(key, "info:source:settings:", name)) {
        std::string source = settings_.source(settingKey(name));
        return source == "settings.conf" ? "" : source;
    }
    if (stripPrefix(key, "package:", name)) {
        return packageValue(name);
    }
    if (key == "info:catalogStatus") {
        return catalogStatus();
    }
    if (key == "info:hasSources") {
        return catalog_ && !catalog_->sources().empty() ? "yes" : "no";
    }
    if (key == "info:hasInstalled") {
        return catalog_ && !catalog_->installed().empty() ? "yes" : "no";
    }
    if (key == "info:removalStatus") {
        bool ok = false;
        std::string message;
        if (!catalog_ || removedPackage_.empty()) return "";
        if (catalog_->isQueued(removedPackage_)) return "Removing...";
        return catalog_->outcome(removedPackage_, ok, message) ? message : "";
    }
    if (key == "info:sourceIsFolder") {
        return drafts_["sourceType"] == "folder" ? "yes" : "no";
    }
    return "";
}

std::string ManagerModel::packageValue(const std::string& key)
{
    Catalog::Entry entry;
    if (!catalog_ || !catalog_->entry(package_, entry)) {
        return key == "summary" ? "This package isn't offered by any source any more." : "";
    }
    const Catalog::Package& package = entry.package;
    bool installed = !entry.installedVersion.empty();
    bool busy = catalog_->isQueued(package.id);

    if (key == "name") return package.name;
    if (key == "description") return package.description;
    if (key == "canInstall") return !installed && !busy ? "yes" : "no";
    if (key == "canUpdate") return installed && entry.updateAvailable && !busy ? "yes" : "no";
    if (key == "canRemove") return installed && !busy ? "yes" : "no";
    if (key == "isBusy") return busy ? "yes" : "no";
    if (key == "summary") {
        std::string text = "Version " + package.version;
        if (package.totalSize() > 0) text += "  -  " + Catalog::describeSize(package.totalSize());
        if (!package.kind.empty()) {
            std::string kind = package.kind;
            kind[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(kind[0])));
            text += "  -  " + kind;
        }
        if (!package.source.empty()) text += "  -  from " + package.source;
        return text;
    }
    if (key == "installedStatus") {
        if (!installed) return "";
        if (!entry.installedSource.empty() && entry.installedSource != package.source) {
            return "Version " + entry.installedVersion + " is installed from " + entry.installedSource + ".";
        }
        if (entry.updateAvailable) return "Version " + entry.installedVersion + " is installed. " + package.version + " is available.";
        return entry.installedVersion == package.version ? "Installed." : "Version " + entry.installedVersion + " is installed.";
    }
    if (key == "requires") {
        std::string text;
        for (const Catalog::Requirement& requirement : package.requirements) {
            Catalog::Entry needed;
            std::string name = catalog_->entry(requirement.id, needed) ? needed.package.name : requirement.id;
            text += (text.empty() ? "Needs " : ", ") + name + (requirement.version.empty() ? "" : " " + requirement.version);
        }
        return text;
    }
    if (key == "checksum") {
        bool checked = std::any_of(package.files.begin(), package.files.end(),
                                   [](const Catalog::PackageFile& file) { return !file.sha256.empty() || !file.md5.empty(); });
        return checked ? "" : "The source gives no checksums, so this download can't be checked for damage.";
    }
    if (key == "progress") {
        Catalog::Service::Activity activity;
        if (catalog_->activity(activity) && activity.id == package.id) {
            if (activity.removing) return "Removing...";
            if (activity.total > 0) {
                int percent = static_cast<int>(activity.received * 100 / activity.total);
                if (activity.received >= activity.total) return "Installing...";
                return "Downloading: " + Catalog::describeSize(activity.received) + " of "
                     + Catalog::describeSize(activity.total) + " (" + std::to_string(percent) + "%)";
            }
            return "Downloading: " + Catalog::describeSize(activity.received);
        }
        if (busy) return "Waiting for other packages to finish...";
        bool ok = false;
        std::string message;
        return catalog_->outcome(package.id, ok, message) ? message : "";
    }
    return "";
}

std::string ManagerModel::catalogStatus()
{
    if (!catalog_) {
        return "The Catalog isn't available.";
    }
    Catalog::Service::Activity activity;
    if (catalog_->activity(activity)) {
        if (activity.removing) return "Removing " + activity.name + "...";
        std::string text = "Installing " + activity.name;
        if (activity.total > 0) {
            text += ": " + std::to_string(static_cast<int>(activity.received * 100 / activity.total)) + "%";
        }
        return text;
    }
    if (catalog_->refreshing()) {
        return "Reading sources...";
    }
    size_t sources = catalog_->sources().size();
    if (sources == 0) {
        return "";
    }
    size_t packages = catalog_->packageCount();
    return std::to_string(packages) + (packages == 1 ? " package from " : " packages from ")
         + std::to_string(sources) + (sources == 1 ? " source." : " sources.");
}

std::string ManagerModel::takeOpenRequest()
{
    std::string request = openRequest_;
    openRequest_.clear();
    return request;
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
    if (!catalog_) {
        return {};
    }
    std::vector<std::string> values;
    if (key == "info:sources") {
        for (const Catalog::SourceConfig& source : catalog_->sources()) {
            std::string location = source.type == "folder" ? source.path : source.url;
            values.push_back(source.name + ": " + location + (source.enabled ? "" : " (off)"));
        }
    }
    else if (key == "info:installed") {
        for (const Catalog::Manifest& manifest : catalog_->installed()) {
            values.push_back(manifest.name + " " + manifest.version);
        }
    }
    else if (key == "info:catalogWarnings") {
        values = catalog_->warnings();
    }
    return values;
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
    else if (source == "sourceTypes") {
        result.emplace_back("index", "Index file");
        result.emplace_back("folder", "Folder of packages");
    }
    else if (source == "sources" && catalog_) {
        for (const Catalog::SourceConfig& config : catalog_->sources()) {
            result.emplace_back(config.name, config.name);
        }
    }
    else if (source == "installed" && catalog_) {
        for (const Catalog::Manifest& manifest : catalog_->installed()) {
            result.emplace_back(manifest.id, manifest.name + " " + manifest.version);
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
    if (action == "open.sources") {
        return "";
    }
    return catalogProblem(action);
}

std::string ManagerModel::catalogProblem(const std::string& action)
{
    bool known = action == "package.install" || action == "package.remove" || action == "package.cancel"
              || action == "package.removeInstalled"
              || action == "source.add" || action == "source.remove" || action == "catalog.refresh";
    if (!known) {
        return "Unknown action " + action;
    }
    if (!catalog_) {
        return "The Catalog isn't available.";
    }

    if (action == "package.install" || action == "package.remove" || action == "package.cancel") {
        Catalog::Entry entry;
        if (!catalog_->entry(package_, entry)) {
            return "This package isn't offered by any source any more.";
        }
        bool busy = catalog_->isQueued(package_);
        bool installed = !entry.installedVersion.empty();
        if (action == "package.cancel") return busy ? "" : "Nothing to stop.";
        if (busy) return "Already working on it.";
        if (action == "package.remove") return installed ? "" : "It isn't installed.";
        return installed && !entry.updateAvailable ? "Installed, and up to date." : "";
    }
    if (action == "source.add") {
        Catalog::SourceConfig source = draftSource(drafts_);
        if (source.name.empty()) return "Give the source a name.";
        if (source.url.empty() && source.path.empty()) return source.type == "folder" ? "Choose the folder." : "Enter the address of the index file.";
        std::string problem = Catalog::validateSource(source, catalog_->sources());
        if (!problem.empty()) return problem;
        return Forms::isTruthy(drafts_["sourceTrusted"]) ? "" : "Confirm that you trust this source.";
    }
    if (action == "source.remove") {
        return get("draft:removeSource").empty() ? "Choose a source to remove." : "";
    }
    if (action == "package.removeInstalled") {
        std::string id = get("draft:removePackage");
        if (id.empty()) return "Choose a package to remove.";
        return catalog_->isQueued(id) ? "Already working on it." : "";
    }
    return catalog_->refreshing() ? "Already reading the sources." : "";
}

Forms::Model::Result ManagerModel::runCatalogAction(const std::string& action)
{
    Result result;
    result.message = catalogProblem(action);
    if (!result.message.empty()) {
        return result;
    }
    result.ok = true;
    if (action == "package.install") {
        catalog_->install(package_);
    }
    else if (action == "package.remove") {
        catalog_->remove(package_);
    }
    else if (action == "package.cancel") {
        catalog_->cancel(package_);
    }
    else if (action == "package.removeInstalled") {
        removedPackage_ = get("draft:removePackage");
        drafts_["removePackage"].clear();
        catalog_->remove(removedPackage_);
    }
    else if (action == "catalog.refresh") {
        catalog_->refresh();
    }
    else if (action == "source.add") {
        Catalog::SourceConfig source = draftSource(drafts_);
        result.ok = catalog_->addSource(source, result.message);
        if (result.ok) {
            result.message = "Added " + source.name + ".";
            drafts_["sourceName"].clear();
            drafts_["sourceUrl"].clear();
            drafts_["sourcePath"].clear();
            drafts_["sourceTrusted"] = "no";
        }
    }
    else if (action == "source.remove") {
        std::string name = get("draft:removeSource");
        result.ok = catalog_->removeSource(name, result.message);
        if (result.ok) {
            result.message = "Removed " + name + ".";
            drafts_["removeSource"].clear();
        }
    }
    return result;
}

Forms::Model::Result ManagerModel::runAction(const std::string& action)
{
    if (action == "settings.save") {
        return saveSettings();
    }
    if (action == "collection.create") {
        return createCollection();
    }
    if (action == "open.sources") {
        openRequest_ = "sources";
        Result result;
        result.ok = true;
        return result;
    }
    return runCatalogAction(action);
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

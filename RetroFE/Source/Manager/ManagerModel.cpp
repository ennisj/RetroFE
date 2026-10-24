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
#include "../Catalog/GitHub.h"
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

    // Binding and action prefixes the model serves itself
    const std::vector<std::string> builtinPrefixes = {
        "settings", "draft", "info", "param", "package", "collection", "source", "catalog", "open", "back"
    };


    // The source the Sources form describes
    Catalog::SourceConfig draftSource(std::map<std::string, std::string>& drafts)
    {
        Catalog::SourceConfig source;
        source.name = Utils::trimEnds(drafts["sourceName"]);
        source.type = drafts["sourceType"].empty() ? "index" : drafts["sourceType"];
        if (source.type == "folder") {
            source.path = Utils::trimEnds(drafts["sourcePath"]);
        }
        else if (source.type == "github") {
            // The Address field takes owner/name or the repository's github.com address
            std::string text = Utils::trimEnds(drafts["sourceUrl"]);
            std::string repo = Catalog::parseRepository(text);
            source.repo = repo.empty() ? text : repo;
        }
        else {
            source.url = Utils::trimEnds(drafts["sourceUrl"]);
        }
        return source;
    }

    // Installed packages that can be removed: everything but RetroFE itself
    std::vector<Catalog::Manifest> removable(Catalog::Service& catalog)
    {
        std::vector<Catalog::Manifest> result = catalog.installed();
        result.erase(std::remove_if(result.begin(), result.end(), [](const Catalog::Manifest& manifest) {
            return manifest.kind == "app" || manifest.id == Catalog::appPackageId;
        }), result.end());
        return result;
    }

    // The GitHub source for this fork of RetroFE, if one has been added
    const Catalog::SourceConfig* retrofeSource(const std::vector<Catalog::SourceConfig>& sources)
    {
        for (const Catalog::SourceConfig& source : sources) {
            if (source.type == "github" && Utils::toLower(source.repo) == Utils::toLower(Catalog::retrofeRepository)) {
                return &source;
            }
        }
        return nullptr;
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

ManagerModel::~ManagerModel() = default;

void ManagerModel::refresh()
{
    settings_.load();
    pendingSettings_.clear();
    collections_ = Setup::listCollections();
    countedPath_.clear();
    countedExtensions_.clear();
    romCountText_.clear();
    for (auto& [prefix, provider] : providers_) {
        provider->refresh();
    }
}

void ManagerModel::setScreen(const std::string& form, const Forms::Params& params)
{
    screen_.form = form;
    screen_.params = params;
    for (auto& [prefix, provider] : providers_) {
        provider->enter(screen_);
    }
}

bool ManagerModel::addProvider(std::unique_ptr<Manager::Provider> provider)
{
    if (!provider) {
        return false;
    }
    std::string prefix = provider->prefix();
    bool valid = !prefix.empty() && prefix.find_first_of(":. ") == std::string::npos;
    if (!valid || providers_.count(prefix)
        || std::find(builtinPrefixes.begin(), builtinPrefixes.end(), prefix) != builtinPrefixes.end()) {
        LOG_ERROR("Manager", "Can't add a provider for \"" + prefix + "\": the prefix is invalid, built in or taken");
        return false;
    }
    providers_[prefix] = std::move(provider);
    return true;
}

std::string ManagerModel::leaving(const Manager::Screen& screen)
{
    // Settings keeps its changes until Save: don't drop them without saying so
    if (screen.form == "settings" && !pendingSettings_.empty()) {
        return "Your changes aren't saved. Choose Save, or leave again to drop them.";
    }
    for (auto& [prefix, provider] : providers_) {
        std::string warning = provider->leaving(screen);
        if (!warning.empty()) {
            return warning;
        }
    }
    return "";
}

void ManagerModel::discard(const Manager::Screen& screen)
{
    if (screen.form == "settings") {
        pendingSettings_.clear();
    }
    for (auto& [prefix, provider] : providers_) {
        provider->discard(screen);
    }
}

void ManagerModel::update(float dt)
{
    for (auto& [prefix, provider] : providers_) {
        provider->update(dt);
    }
}

unsigned ManagerModel::generation()
{
    unsigned sum = 0;
    for (auto& [prefix, provider] : providers_) {
        sum += provider->generation();
    }
    return sum;
}

Manager::Provider* ManagerModel::provider(const std::string& key, char separator, std::string& rest) const
{
    if (providers_.empty()) {
        return nullptr;
    }
    size_t at = key.find(separator);
    if (at == std::string::npos) {
        return nullptr;
    }
    auto found = providers_.find(key.substr(0, at));
    if (found == providers_.end()) {
        return nullptr;
    }
    rest = key.substr(at + 1);
    return found->second.get();
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
    if (Manager::Provider* p = provider(key, ':', name)) {
        p->setDefault(name, value, screen_);
    }
    else if (stripPrefix(key, "settings:", name)) {
        settingDefaults_[name] = value;
    }
    else if (stripPrefix(key, "draft:", name) && drafts_[name].empty()) {
        // A layout's form can't pre-fill a source or vouch for one: the user adds sources
        if (name == "sourceTrusted" || name == "sourceUrl" || name == "sourcePath" || name == "retrofeTrusted") {
            return;
        }
        drafts_[name] = value;
    }
}

std::string ManagerModel::get(const std::string& key)
{
    std::string name;
    if (Manager::Provider* p = provider(key, ':', name)) {
        return p->get(name, screen_);
    }
    if (stripPrefix(key, "param:", name)) {
        return screen_.param(name);
    }
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
            std::vector<Catalog::Manifest> installed = removable(*catalog_);
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
        return catalog_ && !removable(*catalog_).empty() ? "yes" : "no";
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
    if (key == "info:hasCollections") {
        return collections_.empty() ? "no" : "yes";
    }
    if (key == "info:hasRetroFESource") {
        return catalog_ && retrofeSource(catalog_->sources()) ? "yes" : "no";
    }
    if (key == "info:retrofeSourceNote") {
        return std::string("New versions of RetroFE, from the releases of ") + Catalog::retrofeRepository
             + " on GitHub. Installing one updates RetroFE and restarts it.";
    }
    return "";
}

std::string ManagerModel::packageValue(const std::string& key)
{
    Catalog::Entry entry;
    if (!catalog_ || !catalog_->entry(screen_.param("id"), entry)) {
        return key == "summary" ? "This package isn't offered by any source any more." : "";
    }
    const Catalog::Package& package = entry.package;
    bool installed = !entry.installedVersion.empty();
    bool busy = catalog_->isQueued(package.id);
    bool isApp = package.kind == "app";

    if (key == "name") return package.name;
    if (key == "description") return package.description;
    if (key == "canInstall") return !installed && !busy ? "yes" : "no";
    if (key == "canUpdate") return installed && entry.updateAvailable && !busy ? "yes" : "no";
    if (key == "canRemove") return installed && !busy && !isApp ? "yes" : "no";
    if (key == "notice") {
        return isApp ? "Installing this updates RetroFE itself: it replaces RetroFE's program files, then RetroFE restarts. "
                       "The files it replaces are kept in the catalog's backups folder. The checksum only guards against a damaged "
                       "download; what you're trusting is the repository you added."
                     : "";
    }
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
        bool checked = std::any_of(package.files.begin(), package.files.end(), [](const Catalog::PackageFile& file) {
            return !file.sha256.empty() || !file.md5.empty() || !file.sha256Url.empty();
        });
        if (checked) return "";
        return isApp ? "This release has no checksum, so RetroFE won't install it."
                     : "The source gives no checksums, so this download can't be checked for damage.";
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

void ManagerModel::set(const std::string& key, const std::string& value)
{
    std::string name;
    if (Manager::Provider* p = provider(key, ':', name)) {
        p->set(name, value, screen_);
    }
    else if (stripPrefix(key, "settings:", name)) {
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
    std::string name;
    if (Manager::Provider* p = provider(key, ':', name)) {
        return p->list(name, screen_);
    }
    if (key == "info:collections") {
        return collections_;
    }
    if (!catalog_) {
        return {};
    }
    std::vector<std::string> values;
    if (key == "info:sources") {
        for (const Catalog::SourceConfig& source : catalog_->sources()) {
            std::string location = source.type == "folder" ? source.path
                                 : source.type == "github" ? "GitHub " + source.repo + (source.prerelease ? " (with pre-releases)" : "")
                                 : source.url;
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

Forms::Rows ManagerModel::rows(const std::string& key, size_t limit)
{
    std::string name;
    Forms::Rows rows;
    if (Manager::Provider* p = provider(key, ':', name)) {
        rows = p->rows(name, limit, screen_);
    }
    // Hold providers to the limit, so a form never draws thousands of rows
    if (rows.total < rows.items.size()) {
        rows.total = rows.items.size();
    }
    if (rows.items.size() > limit) {
        rows.items.resize(limit);
    }
    return rows;
}

std::vector<std::pair<std::string, std::string>> ManagerModel::options(const std::string& source)
{
    std::vector<std::pair<std::string, std::string>> result;
    std::string name;
    if (Manager::Provider* p = provider(source, ':', name)) {
        return p->options(name, screen_);
    }

    if (source == "layouts") {
        result.emplace_back("", "Built-in");
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(Configuration::layoutsPath(), ec)) {
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
        // The top menu first (listCollections leaves it out), then every other collection
        std::string top = Setup::topMenu();
        result.emplace_back(top, top);
        for (const std::string& name : collections_) {
            result.emplace_back(name, name);
        }
    }
    else if (source == "sourceTypes") {
        result.emplace_back("index", "Index file");
        result.emplace_back("folder", "Folder of packages");
        result.emplace_back("github", "GitHub releases (RetroFE itself)");
    }
    else if (source == "sources" && catalog_) {
        for (const Catalog::SourceConfig& config : catalog_->sources()) {
            result.emplace_back(config.name, config.name);
        }
    }
    else if (source == "installed" && catalog_) {
        for (const Catalog::Manifest& manifest : removable(*catalog_)) {
            result.emplace_back(manifest.id, manifest.name + " " + manifest.version);
        }
    }
    return result;
}

std::string ManagerModel::actionProblem(const std::string& action)
{
    std::string name;
    if (Manager::Provider* p = provider(action, '.', name)) {
        return p->actionProblem(name, screen_);
    }
    if (action == "settings.save") {
        return pendingSettings_.empty() ? "No changes to save." : "";
    }
    if (action == "collection.create") {
        return Setup::validate(Setup::NewCollection{ drafts_["name"], drafts_["romPath"], drafts_["extensions"],
                                                     drafts_["executable"], drafts_["arguments"] });
    }
    if (stripPrefix(action, "open.", name)) {
        return name.empty() ? "Unknown action " + action : "";
    }
    if (action == "back") {
        return "";
    }
    return catalogProblem(action);
}

std::string ManagerModel::catalogProblem(const std::string& action)
{
    bool known = action == "package.install" || action == "package.remove" || action == "package.cancel"
              || action == "package.removeInstalled"
              || action == "source.add" || action == "source.addRetroFE" || action == "source.remove" || action == "catalog.refresh";
    if (!known) {
        return "Unknown action " + action;
    }
    if (!catalog_) {
        return "The Catalog isn't available.";
    }

    if (action == "package.install" || action == "package.remove" || action == "package.cancel") {
        Catalog::Entry entry;
        if (!catalog_->entry(screen_.param("id"), entry)) {
            return "This package isn't offered by any source any more.";
        }
        bool busy = catalog_->isQueued(screen_.param("id"));
        bool installed = !entry.installedVersion.empty();
        if (action == "package.cancel") return busy ? "" : "Nothing to stop.";
        if (busy) return "Already working on it.";
        if (action == "package.remove" && entry.package.kind == "app") return "RetroFE can't remove itself.";
        if (action == "package.remove") return installed ? "" : "It isn't installed.";
        if (entry.package.kind == "app" && std::none_of(entry.package.files.begin(), entry.package.files.end(), [](const Catalog::PackageFile& file) {
                return !file.sha256.empty() || !file.sha256Url.empty();
            })) {
            return "This release has no checksum, so RetroFE won't install it.";
        }
        return installed && !entry.updateAvailable ? "Installed, and up to date." : "";
    }
    if (action == "source.add") {
        Catalog::SourceConfig source = draftSource(drafts_);
        if (source.name.empty()) return "Give the source a name.";
        if (source.type == "github" && source.repo.empty()) return "Enter the repository, e.g. " + std::string(Catalog::retrofeRepository) + ".";
        if (source.type != "github" && source.url.empty() && source.path.empty()) {
            return source.type == "folder" ? "Choose the folder." : "Enter the address of the index file.";
        }
        std::string problem = Catalog::validateSource(source, catalog_->sources());
        if (!problem.empty()) return problem;
        return Forms::isTruthy(drafts_["sourceTrusted"]) ? "" : "Confirm that you trust this source.";
    }
    if (action == "source.addRetroFE") {
        // sources() returns a copy: keep it alive while existing points into it
        std::vector<Catalog::SourceConfig> sources = catalog_->sources();
        if (const Catalog::SourceConfig* existing = retrofeSource(sources)) {
            return "RetroFE's releases are already a source: " + existing->name + ".";
        }
        return Forms::isTruthy(drafts_["retrofeTrusted"]) ? "" : "Confirm that you trust this source.";
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
        catalog_->install(screen_.param("id"));
    }
    else if (action == "package.remove") {
        catalog_->remove(screen_.param("id"));
    }
    else if (action == "package.cancel") {
        catalog_->cancel(screen_.param("id"));
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
    else if (action == "source.addRetroFE") {
        // Named "retrofe", or "retrofe-2"... if that name is taken
        Catalog::SourceConfig source;
        source.type = "github";
        source.repo = Catalog::retrofeRepository;
        source.app = true;   // it offers RetroFE itself
        std::vector<Catalog::SourceConfig> sources = catalog_->sources();
        for (int n = 1; source.name.empty() || !Catalog::validateSource(source, sources).empty(); n++) {
            source.name = n == 1 ? "retrofe" : "retrofe-" + std::to_string(n);
            if (n > 50) break;
        }
        result.ok = catalog_->addSource(source, result.message);
        if (result.ok) {
            result.message = "Added RetroFE's releases on GitHub as " + source.name + ".";
            drafts_["retrofeTrusted"] = "no";
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
    std::string name;
    if (Manager::Provider* p = provider(action, '.', name)) {
        return p->runAction(name, screen_);
    }
    if (action == "settings.save") {
        return saveSettings();
    }
    if (action == "collection.create") {
        return createCollection();
    }
    if (stripPrefix(action, "open.", name) || action == "back") {
        Result result;
        result.message = actionProblem(action);
        result.ok = result.message.empty();
        result.open = name;
        result.keepParams = true;   // like a link
        result.back = action == "back";
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

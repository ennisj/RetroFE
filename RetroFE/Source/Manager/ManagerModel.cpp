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

    // Binding and action prefixes the model serves itself
    const std::vector<std::string> builtinPrefixes = {
        "settings", "draft", "info", "param", "collection", "open", "back", "changes"
    };

    // Settings > Screen size: (value, label); "WxH" values set horizontal and vertical
    const std::vector<std::pair<std::string, std::string>> screenSizes = {
        { "stretch", "The screen's size" },
        { "3840x2160", "3840 x 2160" },
        { "2560x1440", "2560 x 1440" },
        { "1920x1080", "1920 x 1080" },
        { "1600x900", "1600 x 900" },
        { "1366x768", "1366 x 768" },
        { "1280x720", "1280 x 720" },
        { "1280x1024", "1280 x 1024 (5:4)" },
        { "1024x768", "1024 x 768 (4:3)" },
        { "800x600", "800 x 600 (4:3)" },
        { "custom", "Custom" },
    };
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
    customScreenSize_ = false;
    collections_ = Setup::listCollections();
    countedPath_.clear();
    countedExtensions_.clear();
    romCountText_.clear();
    for (auto& [prefix, provider] : providers_) {
        provider->refresh();
    }
}

ManagerModel::ViewState ManagerModel::saveView() const
{
    ViewState state;
    for (const auto& [prefix, provider] : providers_) {
        state[prefix] = provider->saveView();
    }
    return state;
}

void ManagerModel::restoreView(const ViewState& state)
{
    for (const auto& [prefix, values] : state) {
        auto found = providers_.find(prefix);
        if (found != providers_.end()) {
            found->second->restoreView(values);
        }
    }
}

void ManagerModel::setScreen(const std::string& form, const Forms::Params& params)
{
    screen_.form = form;
    screen_.params = params;
    // Systems added or deleted on the screen before count straight away (the
    // hub's welcome, the collections choice)
    collections_ = Setup::listCollections();
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
        customScreenSize_ = false;
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
    if (key == "settings:screenSize") {
        return screenSize();
    }
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
    if (key == "info:pendingChanges") {
        return pendingChanges().text();
    }
    if (key == "info:hasPendingChanges") {
        return pendingChanges().count() > 0 ? "yes" : "no";
    }
    if (key == "info:customScreenSize") {
        return screenSize() == "custom" ? "yes" : "no";
    }
    if (key == "info:hasLaunchers") {
        return get("emulator:hasLaunchers") == "yes" ? "yes" : "no";
    }
    if (key == "info:hasCollections") {
        return collections_.empty() ? "no" : "yes";
    }
    return "";
}

void ManagerModel::set(const std::string& key, const std::string& value)
{
    std::string name;
    if (Manager::Provider* p = provider(key, ':', name)) {
        p->set(name, value, screen_);
    }
    else if (key == "settings:screenSize") {
        setScreenSize(value);
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

std::string ManagerModel::screenSize()
{
    if (customScreenSize_) {
        return "custom";
    }
    std::string width = Utils::toLower(Utils::trimEnds(get("settings:horizontal")));
    std::string height = Utils::toLower(Utils::trimEnds(get("settings:vertical")));
    if (width == "stretch" && height == "stretch") {
        return "stretch";
    }
    std::string size = width + "x" + height;
    for (const auto& [value, label] : screenSizes) {
        if (value == size) {
            return value;
        }
    }
    return "custom";
}

void ManagerModel::setScreenSize(const std::string& value)
{
    if (value == "custom") {
        customScreenSize_ = true;   // Width and Height show, with what's set now
        return;
    }
    size_t x = value.find('x');
    std::string width = value == "stretch" ? "stretch" : x == std::string::npos ? "" : value.substr(0, x);
    std::string height = value == "stretch" ? "stretch" : x == std::string::npos ? "" : value.substr(x + 1);
    if (width.empty() || height.empty()) {
        return;
    }
    customScreenSize_ = false;
    set("settings:horizontal", width);
    set("settings:vertical", height);
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
    return {};
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
        // Only folders RetroFE can start with: a main page and a splash page (a .xml, or a
        // .lua: fork compat), as Manage's "Use this theme" asks; RetroFE stops at start-up
        // without a splash page. Plus the one set now, whatever it is. In name order,
        // ignoring case, as Manage's Themes list. (OneSauce's layouts folder also holds
        // "default", a folder of wallpapers.)
        const std::string current = get("settings:layout");
        auto hasPage = [](const fs::path& folder, const char* page) {
            std::error_code fileEc;
            return fs::is_regular_file(folder / (std::string(page) + ".xml"), fileEc)
                || fs::is_regular_file(folder / (std::string(page) + ".lua"), fileEc);
        };
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(Configuration::layoutsPath(), ec)) {
            std::error_code fileEc;
            if (!entry.is_directory(fileEc)) {
                continue;
            }
            std::string name = entry.path().filename().string();
            if (name == current || (hasPage(entry.path(), "layout") && hasPage(entry.path(), "splash"))) {
                names.push_back(name);
            }
        }
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
            std::string la = Utils::toLower(a), lb = Utils::toLower(b);
            return la != lb ? la < lb : a < b;
        });
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
    else if (source == "displays") {
        result = displays_;
    }
    else if (source == "screenSizes") {
        result = screenSizes;
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
    if (action == "changes.apply") {
        return pendingChanges().count() == 0 ? "Nothing is waiting to be applied." : "";
    }
    return "Unknown action " + action;
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
    if (action == "changes.apply") {
        // RetroFE reloads now, then Manage opens on this screen again
        Result result;
        result.message = actionProblem(action);
        if (!result.message.empty()) {
            return result;
        }
        result.ok = true;
        result.reload = true;
        result.open = screen_.form;
        result.params = screen_.params;
        result.message = Manager::PendingChanges::appliedText(pendingChanges().count());
        return result;
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
    Result result;
    result.message = "Unknown action " + action;
    return result;
}

Forms::Severity ManagerModel::severity(const std::string& key)
{
    std::string name;
    if (Manager::Provider* p = provider(key, ':', name)) {
        return p->severity(name, screen_);
    }
    if (key == "info:romCount") {
        std::string count = romCount();
        return count == "Folder not found" ? Forms::Severity::Error
             : count == "0 matching files" ? Forms::Severity::Warning
             : Forms::Severity::None;
    }
    return Forms::Severity::None;
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
    result.message = "Saved. RetroFE reloads now. Fullscreen, monitor and screen size apply the next time RetroFE starts.";
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

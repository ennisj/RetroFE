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
#pragma once

#include "PendingChanges.h"
#include "Provider.h"
#include "../Forms/Form.h"
#include "../Forms/SettingsFile.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Configuration;

// What the manager's forms are bound to.
//   settings:<key>         global settings; edits are pending until settings.save.
//                          A field's default is the value RetroFE uses when no file sets it.
//   draft:<key>            unsaved form input; a field's default is its initial value
//   param:<name>           the current screen's parameters
//   info:collections       the collection folders (list)
//   info:hasLaunchers      whether the Launchers screen lists any, for visibleWhen
//   info:hasCollections    whether there are any, for visibleWhen
//   info:romCount          matching files in draft:romPath
//   info:source:<binding>  the file a settings: value comes from, when it isn't settings.conf
//   info:pendingChanges    "2 changes apply when you close Manage", or "" (see
//                          PendingChanges.h); info:hasPendingChanges for visibleWhen
//   settings:screenSize    horizontal and vertical as one choice (options
//                          "screenSizes": "stretch", "1920x1080"..., "custom");
//                          info:customScreenSize while "custom" shows Width and Height
//   <prefix>:<key>         a registered provider's (see Provider.h)
// Actions: settings.save, collection.create, changes.apply (reloads now, then
// opens Manage again), open.<form> (opens that form), back, and
// <prefix>.<name> for providers.
class ManagerModel : public Forms::Model
{
public:
    explicit ManagerModel(Configuration& config);
    ~ManagerModel() override;

    // Re-reads settings and collections and drops unsaved setting edits
    void refresh();

    // What's waiting for a reload, for info:pendingChanges and changes.apply
    void setPendingChanges(std::function<Manager::PendingChanges()> pending) { pending_ = std::move(pending); }

    // The screen being shown; param: bindings and providers read its parameters
    void setScreen(const std::string& form, const Forms::Params& params);
    const Manager::Screen& screen() const { return screen_; }

    // Serves provider->prefix() bindings and actions from now on. False, and
    // not added, if the prefix is built in or taken.
    bool addProvider(std::unique_ptr<Manager::Provider> provider);

    using ViewState = std::map<std::string, Forms::Params>;
    ViewState saveView() const;
    void restoreView(const ViewState& state);

    // Leaving a screen: a provider's warning, or "" (see Provider::leaving)
    std::string leaving(const Manager::Screen& screen);
    // The user left anyway: providers drop their unsaved changes
    void discard(const Manager::Screen& screen);

    // Every frame while the manager is open
    void update(float dt);

    unsigned generation() override;

    // Writes settings.conf from the built-in defaults if there isn't one, so a
    // bare install that now has content keeps its settings (Home included)
    bool ensureSettingsFile(std::string& error) { return settings_.ensureExists(initialSettings(), error); }

    std::string get(const std::string& key) override;
    Forms::Severity severity(const std::string& key) override;
    void set(const std::string& key, const std::string& value) override;
    void setDefault(const std::string& key, const std::string& value) override;
    std::vector<std::string> list(const std::string& key) override;
    Forms::Rows rows(const std::string& key, size_t limit) override;
    void setDisplays(const std::vector<std::pair<std::string, std::string>>& displays) { displays_ = displays; }
    std::vector<std::pair<std::string, std::string>> options(const std::string& source) override;
    std::string actionProblem(const std::string& action) override;
    Result runAction(const std::string& action) override;

private:
    // The provider for "prefix<separator>rest", with rest set; nullptr if none
    Manager::Provider* provider(const std::string& key, char separator, std::string& rest) const;
    static std::string initialSettings();
    std::string settingKey(const std::string& key) const;
    std::string settingValue(const std::string& key);
    std::string romCount();
    Result saveSettings();
    Result createCollection();
    std::string screenSize();
    void setScreenSize(const std::string& value);
    Manager::PendingChanges pendingChanges() const { return pending_ ? pending_() : Manager::PendingChanges{}; }

    Configuration& config_;
    std::function<Manager::PendingChanges()> pending_;
    bool customScreenSize_ = false;   // "Custom" chosen for settings:screenSize
    Manager::Screen screen_;
    std::map<std::string, std::unique_ptr<Manager::Provider>> providers_;   // by prefix
    SettingsFile settings_;
    std::map<std::string, std::string> pendingSettings_;
    std::map<std::string, std::string> settingDefaults_;  // what RetroFE uses when nothing sets a key
    std::map<std::string, std::string> drafts_;
    std::vector<std::string> collections_;
    std::vector<std::pair<std::string, std::string>> displays_;

    std::string countedPath_;
    std::string countedExtensions_;
    std::string romCountText_;
};

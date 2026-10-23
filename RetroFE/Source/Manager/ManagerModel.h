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

#include "../Forms/Form.h"
#include "../Forms/SettingsFile.h"
#include <map>
#include <string>
#include <vector>

class Configuration;
namespace Catalog { class Service; }

// What the manager's forms are bound to.
//   settings:<key>         global settings; edits are pending until settings.save.
//                          A field's default is the value RetroFE uses when no file sets it.
//   draft:<key>            unsaved form input; a field's default is its initial value
//   info:collections       the collection folders (list)
//   info:romCount          matching files in draft:romPath
//   info:source:<binding>  the file a settings: value comes from, when it isn't settings.conf
//   info:sources, info:installed, info:catalogWarnings   Catalog lists
//   info:catalogStatus     what the Catalog is doing, or how many packages it has
//   info:hasSources, info:hasInstalled, info:sourceIsFolder   for visibleWhen
//                          (hasInstalled: packages that can be removed, so not RetroFE itself)
//   info:removalStatus     how the last package.removeInstalled went
//   package:<key>          the package chosen in the Catalog list: name, summary,
//                          description, installedStatus, requires, checksum, progress,
//                          notice (installing RetroFE itself restarts it),
//                          and canInstall, canUpdate, canRemove, isBusy for visibleWhen
//   draft:source*          the Sources form's input
//   info:hasRetroFESource, info:retrofeSourceNote, draft:retrofeTrusted
//                          adding this fork's GitHub releases in one step
// Actions: settings.save, collection.create, package.install, package.remove,
// package.cancel, package.removeInstalled (draft:removePackage), source.add,
// source.addRetroFE (draft:retrofeTrusted), source.remove, catalog.refresh, open.sources
class ManagerModel : public Forms::Model
{
public:
    explicit ManagerModel(Configuration& config);

    // Re-reads settings and collections and drops unsaved setting edits
    void refresh();

    // The Catalog the package:, source and catalog bindings use; may be null
    void setCatalog(Catalog::Service* catalog) { catalog_ = catalog; }
    void selectPackage(const std::string& id) { package_ = id; }

    // A screen an action asked to open ("sources"), once
    std::string takeOpenRequest();

    // Writes settings.conf from the built-in defaults if there isn't one, so a
    // bare install that now has content keeps its settings (Home included)
    bool ensureSettingsFile(std::string& error) { return settings_.ensureExists(initialSettings(), error); }

    std::string get(const std::string& key) override;
    void set(const std::string& key, const std::string& value) override;
    void setDefault(const std::string& key, const std::string& value) override;
    std::vector<std::string> list(const std::string& key) override;
    std::vector<std::pair<std::string, std::string>> options(const std::string& source) override;
    std::string actionProblem(const std::string& action) override;
    Result runAction(const std::string& action) override;

private:
    static std::string initialSettings();
    std::string settingKey(const std::string& key) const;
    std::string settingValue(const std::string& key);
    std::string romCount();
    Result saveSettings();
    Result createCollection();
    std::string packageValue(const std::string& key);
    std::string catalogStatus();
    std::string catalogProblem(const std::string& action);
    Result runCatalogAction(const std::string& action);

    Configuration& config_;
    Catalog::Service* catalog_ = nullptr;
    std::string package_;
    std::string removedPackage_;   // the last package.removeInstalled, for info:removalStatus
    std::string openRequest_;
    SettingsFile settings_;
    std::map<std::string, std::string> pendingSettings_;
    std::map<std::string, std::string> settingDefaults_;  // what RetroFE uses when nothing sets a key
    std::map<std::string, std::string> drafts_;
    std::vector<std::string> collections_;

    std::string countedPath_;
    std::string countedExtensions_;
    std::string romCountText_;
};

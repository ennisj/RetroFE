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
#include "RetroArchProvider.h"
#include "../../Library/Edits.h"
#include "../../Library/Indexer.h"
#include <filesystem>
#include <system_error>

namespace RA = Library::RetroArch;

namespace Manager
{

namespace
{
    const char* const unsavedWarning = "You have unsaved changes. Save them, or leave again to discard them.";

    bool startsWith(const std::string& s, const std::string& prefix)
    {
        return s.compare(0, prefix.size(), prefix) == 0;
    }

    std::string fileName(const std::string& path)
    {
        size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::string yesNo(bool value) { return value ? "yes" : "no"; }

    std::string listNames(const std::vector<std::string>& names, size_t most = 6)
    {
        std::string out;
        for (size_t i = 0; i < names.size() && i < most; i++) {
            out += (i ? ", " : "") + names[i];
        }
        if (names.size() > most) out += " and " + std::to_string(names.size() - most) + " more";
        return out;
    }
}

RetroArchProvider::RetroArchProvider(const Services& services, RA::Environment environment)
    : services_(services)
    , environment_(std::move(environment))
{
}

// --- Plumbing ---------------------------------------------------------------------------

RetroArchProvider::Kind RetroArchProvider::kindOf(const std::string& form)
{
    if (form == "game") return Kind::GameLink;
    if (form == "system") return Kind::SystemLink;
    if (form == "emulator") return Kind::LauncherLink;
    std::string rest;
    if (startsWith(form, "game-")) rest = form.substr(5);
    else if (startsWith(form, "system-")) rest = form.substr(7);
    else if (startsWith(form, "emulator-")) rest = form.substr(9);
    else return Kind::None;
    if (rest == "retroarch") return Kind::Hub;
    if (rest == "core-options") return Kind::CoreOptions;
    if (rest == "core-option") return Kind::CoreOption;
    if (rest == "controls") return Kind::Controls;
    return Kind::None;
}

bool RetroArchProvider::isGameForm(const std::string& form)
{
    return form == "game" || startsWith(form, "game-");
}

bool RetroArchProvider::isLauncherForm(const std::string& form)
{
    return form == "emulator" || startsWith(form, "emulator-");
}

bool RetroArchProvider::tileOf(Kind kind, Tile& tile)
{
    switch (kind) {
    case Kind::Hub: tile = Tile::Settings; return true;
    case Kind::CoreOptions:
    case Kind::CoreOption: tile = Tile::CoreOptions; return true;
    case Kind::Controls: tile = Tile::Controls; return true;
    default: return false;
    }
}

std::string RetroArchProvider::contextKey(const Screen& screen)
{
    if (isGameForm(screen.form)) return "game:" + screen.param("collection") + "/" + screen.param("id");
    if (isLauncherForm(screen.form)) return "launcher:" + screen.param("scope") + "/" + screen.param("id");
    return "system:" + screen.param("id");
}

RetroArchProvider::IndexPtr RetroArchProvider::index() const
{
    return services_.library ? services_.library->index() : nullptr;
}

Library::Backups& RetroArchProvider::backups()
{
    if (services_.editor) return services_.editor->backups();
    if (!ownBackups_) {
        ownBackups_ = std::make_unique<Library::Backups>(context_.index ? context_.index->options.root : std::string("."));
    }
    return *ownBackups_;
}

const RA::Setup& RetroArchProvider::setupFor(const Library::Index& idx, int launcher)
{
    if (setupsIndex_.get() != &idx) {
        setups_.clear();
        setupsIndex_ = context_.index;
    }
    auto found = setups_.find(launcher);
    if (found == setups_.end()) {
        found = setups_.emplace(launcher, RA::detect(idx.options.root, idx.launchers[static_cast<size_t>(launcher)], environment_)).first;
    }
    return found->second;
}

void RetroArchProvider::sync(const Screen& screen)
{
    if (kindOf(screen.form) == Kind::None) return;
    std::string key = contextKey(screen);
    IndexPtr idx = index();
    if (key != context_.key) {
        tiles_.clear();
        loadContext(screen, key, idx);
        generation_++;
    }
    else if (idx != context_.index) {
        loadContext(screen, key, idx);
        for (auto& [t, view] : tiles_) {
            if (view.loaded) loadTile(static_cast<Tile>(t), true);
        }
        generation_++;
    }
}

void RetroArchProvider::loadContext(const Screen& screen, const std::string& key, const IndexPtr& idx)
{
    context_ = Context();
    context_.key = key;
    context_.index = idx;
    context_.game = isGameForm(screen.form);
    if (!idx) {
        context_.why = "Reading the library...";
        return;
    }
    Library::LauncherResolution resolution;
    const Library::GameRecord* game = nullptr;
    if (isLauncherForm(screen.form)) {
        // A launcher of the Emulators section: its own core-wide files
        std::string scope = screen.param("scope");
        Library::LauncherScope parsed = scope == "collection" ? Library::LauncherScope::Collection
            : scope == "local" ? Library::LauncherScope::Local : Library::LauncherScope::Global;
        int launcher = idx->findLauncher(parsed, screen.param("id"));
        if (launcher < 0) {
            context_.why = "There's no launcher called " + screen.param("id") + ".";
            return;
        }
        context_.title = screen.param("id");
        context_.launcher = idx->launchers[static_cast<size_t>(launcher)].id;
        context_.found = true;
        context_.setup = setupFor(*idx, launcher);
        context_.target = RA::coreTarget(context_.setup);
        if (!context_.setup.ready()) context_.why = context_.setup.why;
        return;
    }
    if (context_.game) {
        int g = idx->findGame(screen.param("collection"), screen.param("id"));
        if (g < 0) {
            context_.why = "This game isn't in the library.";
            return;
        }
        game = &idx->games[static_cast<size_t>(g)];
        context_.title = game->title;
        resolution = idx->resolveLauncher(game->collection, game->name);
    }
    else {
        int c = idx->findCollection(screen.param("id"));
        if (c < 0) {
            context_.why = "There's no system called " + screen.param("id") + ".";
            return;
        }
        const Library::CollectionRecord& record = idx->collections[static_cast<size_t>(c)];
        context_.title = record.title.empty() ? record.name : record.title;
        resolution = idx->resolveLauncher(c);
    }
    context_.launcher = resolution.name;
    if (resolution.launcher < 0) {
        context_.why = resolution.problem.empty() ? "It has no launcher." : resolution.problem;
        return;
    }
    context_.found = true;
    context_.setup = setupFor(*idx, resolution.launcher);
    context_.target = game ? RA::gameTarget(context_.setup, *game) : RA::coreTarget(context_.setup);
    if (!context_.setup.ready()) context_.why = context_.setup.why;
    else if (!context_.target.problem.empty()) context_.why = context_.target.problem;
}

RetroArchProvider::TileView& RetroArchProvider::tile(Tile t)
{
    TileView& view = tiles_[static_cast<int>(t)];
    if (!view.loaded && context_.target.usable()) loadTile(t, true);
    return view;
}

void RetroArchProvider::loadTile(Tile t, bool keepEdits)
{
    TileView& view = tiles_[static_cast<int>(t)];
    view.state = RA::read(context_.target, t, backups());
    std::map<std::string, std::string> values;
    for (const RA::Row& row : view.state.rows) {
        values[row.id] = row.current < 0 ? "keep" : std::to_string(row.current);
    }
    std::string id = context_.key + "#" + std::to_string(static_cast<int>(t));
    if (keepEdits && view.edits.id() == id) {
        auto changes = view.edits.changes();
        view.edits.load(id, values);
        for (const auto& [k, v] : changes) view.edits.set(k, v);
    }
    else {
        view.edits.load(id, values);
    }
    view.loaded = true;
}

const RA::Row* RetroArchProvider::findRow(Tile t, const std::string& id)
{
    for (const RA::Row& row : tile(t).state.rows) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

std::map<std::string, int> RetroArchProvider::pendingChoices(Tile t)
{
    std::map<std::string, int> choices;
    for (const auto& [id, value] : tile(t).edits.changes()) {
        if (value == "keep" || value.empty()) continue;
        try {
            choices[id] = std::stoi(value);
        }
        catch (...) {
        }
    }
    return choices;
}

// --- Provider ---------------------------------------------------------------------------

void RetroArchProvider::refresh()
{
    // Files may have changed while the manager was closed (RetroArch itself writes them)
    context_ = Context();
    tiles_.clear();
    setups_.clear();
    setupsIndex_.reset();
    generation_++;
}

void RetroArchProvider::enter(const Screen& screen)
{
    Kind kind = kindOf(screen.form);
    if (kind == Kind::None) return;
    sync(screen);
    if (!context_.target.usable()) return;
    Tile t = Tile::Settings;
    if (tileOf(kind, t)) {
        loadTile(t, true);
        if (kind == Kind::Hub) {
            loadTile(Tile::CoreOptions, true);
            loadTile(Tile::Controls, true);
        }
    }
    else {
        // Back on the game or system: what the link says may have changed
        for (auto& [k, view] : tiles_) {
            if (view.loaded) loadTile(static_cast<Tile>(k), true);
        }
    }
    generation_++;
}

std::string RetroArchProvider::leaving(const Screen& screen)
{
    Tile t = Tile::Settings;
    if (!tileOf(kindOf(screen.form), t)) return "";
    auto found = tiles_.find(static_cast<int>(t));
    if (found == tiles_.end() || !found->second.loaded) return "";
    for (const auto& [id, value] : found->second.edits.changes()) {
        if (value != "keep") return unsavedWarning;
    }
    return "";
}

void RetroArchProvider::discard(const Screen& screen)
{
    Tile t = Tile::Settings;
    if (!tileOf(kindOf(screen.form), t)) return;
    auto found = tiles_.find(static_cast<int>(t));
    if (found != tiles_.end()) found->second.edits.reset();
}

std::string RetroArchProvider::get(const std::string& key, const Screen& screen)
{
    Kind kind = kindOf(screen.form);
    if (kind == Kind::None) return "";
    sync(screen);
    const RA::Setup& s = context_.setup;
    const bool ready = context_.target.usable();

    if (key == "runs") return yesNo(context_.found && s.state != RA::State::NotRetroArch);
    if (key == "status") {
        if (!context_.found) return "";
        switch (s.state) {
        case RA::State::NotRetroArch: return "Runs outside RetroArch";
        case RA::State::NotFound: return "RetroArch not found";
        case RA::State::CoreUnknown: return "Core not known";
        case RA::State::Ready: break;
        }
        if (!context_.target.problem.empty()) return s.library + ": not for this game";
        int own = 0;
        if (context_.game) {
            for (Tile t : { Tile::Settings, Tile::CoreOptions, Tile::Controls }) {
                if (tile(t).state.ownExists) own++;
            }
        }
        return s.library + (s.libraryCertain ? "" : " (not confirmed)") + (own ? ", its own settings" : "");
    }
    if (key == "screenTitle") return context_.title;
    if (key == "why") return ready ? "" : context_.why;
    if (key == "isReady") return yesNo(ready);
    if (key == "isGame") return yesNo(context_.game);
    if (key == "launcher") return context_.launcher;
    if (key == "core") {
        if (s.coreArgument.empty()) return "";
        std::string file = fileName(s.coreFile.empty() ? s.coreArgument : s.coreFile);
        return s.library.empty() ? file : s.library + " (" + file + ")";
    }
    if (key == "coreNote") {
        std::string out;
        for (const std::string& note : s.notes) out += (out.empty() ? "" : " ") + note;
        return out;
    }
    if (key == "reaches") {
        if (context_.game || !ready || !context_.index) return "";
        if (!context_.reachesDone) {
            context_.reachesDone = true;
            const Library::Index& idx = *context_.index;
            for (size_t c = 0; c < idx.collections.size(); c++) {
                if (!idx.collections[c].is(Library::KindSystem)) continue;
                Library::LauncherResolution r = idx.resolveLauncher(static_cast<int>(c));
                if (r.launcher < 0) continue;
                const RA::Setup& other = setupFor(idx, r.launcher);
                if (other.ready() && other.coreKey() == s.coreKey()) context_.reaches.push_back(idx.collections[c].name);
            }
        }
        size_t n = context_.reaches.size();
        return "Reaches " + std::to_string(n) + (n == 1 ? " system" : " systems") + " whose launcher runs " + s.library
            + (n ? ": " + listNames(context_.reaches) : std::string()) + ", and any game set to a launcher on this core. "
            "A game with a file of its own keeps it; for core options and controls the game's file replaces these entirely.";
    }

    Tile t = Tile::Settings;
    bool hasTile = tileOf(kind, t);
    if (key == "intro") {
        if (!ready || !hasTile) return "";
        std::string core = s.library;
        if (t == Tile::Settings) {
            return context_.game
                ? "Only what you change here is kept, in this game's own file. Everything else comes from the system's settings ("
                    + core + ".cfg) and retroarch.cfg. RetroArch reads it the next time the game starts."
                : "These are " + core + "'s own settings (" + core + ".cfg), on top of retroarch.cfg.";
        }
        if (t == Tile::CoreOptions) {
            if (s.globalCoreOptions && !context_.game) {
                return "RetroArch keeps every core's options in one file (global_core_options), so these are kept there.";
            }
            const std::string& below = tile(t).state.files.below;
            if (!context_.game) return core + "'s options, for every game on this core without an options file of its own.";
            return below.empty()
                ? "Only one options file applies to a game. The core has none yet, so the options this game doesn't set keep the core's defaults."
                : "Only one options file applies to a game: when this game gets its own, it starts as a copy of " + fileName(below)
                    + ", so everything else stays as it was.";
        }
        const std::string& below = tile(t).state.files.below;
        if (!context_.game) return "Player 1's buttons for every game on this core without a remap file of its own.";
        return below.empty()
            ? "Player 1's buttons for this game, kept in a remap file of its own."
            : "Player 1's buttons for this game. One remap file applies: this game's own starts as a copy of " + fileName(below) + ".";
    }
    if (key == "optionsStatus" || key == "controlsStatus") {
        if (!ready) return "";
        Tile which = key == "optionsStatus" ? Tile::CoreOptions : Tile::Controls;
        const TileView& view = tile(which);
        if (view.state.rows.empty()) return "None for this core yet";
        size_t changed = 0;
        for (const RA::Row& row : view.state.rows) {
            if (row.differs) changed++;
        }
        if (context_.game && !view.state.ownExists) return "The system's";
        if (!changed) return context_.game ? "Its own file, as the system's" : "Defaults";
        return std::to_string(changed) + (context_.game ? " changed for this game" : " changed");
    }
    if (!hasTile || !ready) {
        if (key == "isDirty" || key == "hasOwnFile" || key == "canUseCore" || key == "canRestore") return "no";
        return "";
    }
    TileView& view = tile(t);
    if (key == "file") {
        return view.state.files.own + (view.state.ownExists ? "" : " (not made yet)");
    }
    if (key == "disabledNote") return view.state.disabledNote;
    if (key == "isDirty") return yesNo(!pendingChoices(t).empty());
    if (key == "hasOwnFile") return yesNo(view.state.ownExists);
    if (key == "canUseCore") return yesNo(context_.game && view.state.ownExists);
    if (key == "canRestore") return yesNo(view.state.canRestore);
    if (startsWith(key, "row:")) return view.edits.get(key.substr(4));
    if (key == "opt") return view.edits.get(screen.param("key"));
    if (key == "optionTitle") {
        const RA::Row* row = findRow(t, screen.param("key"));
        return row ? row->label : context_.title;
    }
    if (key == "optionNote") {
        const RA::Row* row = findRow(t, screen.param("key"));
        if (!row) return "This core has no option called " + screen.param("key") + " in Manage's list.";
        return std::string("RetroArch's name for it: ") + row->id + ". "
            + (row->own ? "Set in " + fileName(view.state.files.own) + "." : "Not set in " + fileName(view.state.files.own) + ".");
    }
    return "";
}

void RetroArchProvider::set(const std::string& key, const std::string& value, const Screen& screen)
{
    Tile t = Tile::Settings;
    if (!tileOf(kindOf(screen.form), t)) return;
    sync(screen);
    if (!context_.target.usable()) return;
    if (startsWith(key, "row:")) {
        tile(t).edits.set(key.substr(4), value);
    }
    else if (key == "opt") {
        tile(t).edits.set(screen.param("key"), value);
    }
    generation_++;
}

std::vector<std::pair<std::string, std::string>> RetroArchProvider::options(const std::string& key, const Screen& screen)
{
    std::vector<std::pair<std::string, std::string>> out;
    Tile t = Tile::Settings;
    if (!tileOf(kindOf(screen.form), t)) return out;
    sync(screen);
    if (!context_.target.usable()) return out;
    std::string id;
    if (startsWith(key, "choices:")) id = key.substr(8);
    else if (key == "optChoices") id = screen.param("key");
    else return out;
    const RA::Row* row = findRow(t, id);
    if (!row) return out;
    if (row->current < 0) out.emplace_back("keep", row->currentText);
    for (size_t i = 0; i < row->choices.size(); i++) {
        out.emplace_back(std::to_string(i), row->choices[i]);
    }
    return out;
}

Forms::Rows RetroArchProvider::rows(const std::string& key, size_t limit, const Screen& screen)
{
    Forms::Rows out;
    if (key != "coreOptions" || kindOf(screen.form) != Kind::CoreOptions) return out;
    sync(screen);
    if (!context_.target.usable()) return out;
    TileView& view = tile(Tile::CoreOptions);
    out.total = view.state.rows.size();
    for (const RA::Row& row : view.state.rows) {
        if (out.items.size() >= limit) break;
        Forms::Row r;
        r.id = row.id;
        r.label = row.label;
        r.detail = row.currentText;
        std::string pending = view.edits.get(row.id);
        if (pending != "keep" && pending != std::to_string(row.current)) {
            try {
                size_t i = static_cast<size_t>(std::stoi(pending));
                if (i < row.choices.size()) r.detail = row.choices[i] + " (not saved)";
            }
            catch (...) {
            }
        }
        if (context_.game) {
            r.params = { { "collection", screen.param("collection") }, { "id", screen.param("id") } };
        }
        else {
            r.params = { { "id", screen.param("id") } };
            if (isLauncherForm(screen.form)) r.params["scope"] = screen.param("scope");
        }
        out.items.push_back(r);
    }
    return out;
}

std::string RetroArchProvider::actionProblem(const std::string& action, const Screen& screen)
{
    Tile t = Tile::Settings;
    Kind kind = kindOf(screen.form);
    if (!tileOf(kind, t)) return Provider::actionProblem(action, screen);
    sync(screen);
    if (!context_.target.usable()) return context_.why.empty() ? "RetroArch isn't set up for this." : context_.why;
    TileView& view = tile(t);
    if (action == "save") {
        if (pendingChoices(t).empty()) return "No changes to save.";
        return "";
    }
    if (action == "reset") return view.edits.isDirty() ? "" : "No changes.";
    if (action == "useCore") {
        if (!context_.game) return "These are the core's own settings already.";
        return view.state.ownExists ? "" : "This game has no file of its own for these.";
    }
    if (action == "restore") return view.state.canRestore ? "" : "Manage hasn't changed these.";
    return Provider::actionProblem(action, screen);
}

Forms::Model::Result RetroArchProvider::runAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) return result;
    Tile t = Tile::Settings;
    Kind kind = kindOf(screen.form);
    tileOf(kind, t);
    TileView& view = tile(t);
    if (action == "reset") {
        view.edits.reset();
        result.ok = true;
        result.info = true;
        generation_++;
        return result;
    }
    Library::EditResult edit;
    if (action == "save") edit = RA::apply(backups(), context_.target, t, pendingChoices(t));
    else if (action == "useCore") edit = RA::useCoreSettings(backups(), context_.target, t);
    else edit = RA::restoreOriginal(backups(), context_.target, t);
    // RetroArch reads these files each time it starts a game, so RetroFE has nothing to reload
    loadTile(t, !edit.ok);   // a failed write keeps the unsaved choices
    result.ok = edit.ok;
    result.message = edit.message;
    result.info = edit.ok && edit.files.empty();   // "No changes."
    result.back = edit.ok && action == "save" && kind == Kind::CoreOption;
    generation_++;
    return result;
}

}

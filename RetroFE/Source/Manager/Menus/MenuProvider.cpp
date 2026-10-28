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
#include "MenuProvider.h"
#include "../../Library/Files.h"
#include "../../Library/Indexer.h"
#include "../../Utility/Utils.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <system_error>

namespace fs = std::filesystem;
using namespace Library;

namespace
{
    const std::vector<std::string>& infoKeys()
    {
        static const std::vector<std::string> keys = { "title", "manufacturer", "year", "genre" };
        return keys;
    }

    // The info.conf key behind a field key ("infoTitle" -> "title"), or ""
    std::string infoKeyOf(const std::string& key)
    {
        if (key.size() <= 4 || key.compare(0, 4, "info") != 0) {
            return "";
        }
        std::string rest = key.substr(4);
        rest[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(rest[0])));
        return std::find(infoKeys().begin(), infoKeys().end(), rest) != infoKeys().end() ? rest : "";
    }

    // The artwork a collection usually has in system_artwork
    const std::vector<std::string>& standardSlots()
    {
        static const std::vector<std::string> slots = { "logo", "marquee", "device", "led_marquee", "video", "story" };
        return slots;
    }

    std::string slotLabel(const std::string& type)
    {
        if (type == "led_marquee") {
            return "LED marquee";
        }
        std::string label = type;
        std::replace(label.begin(), label.end(), '_', ' ');
        if (!label.empty()) {
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        }
        return label;
    }

    std::string count(size_t n, const std::string& one, const std::string& many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    std::string yesNo(bool value)
    {
        return value ? "yes" : "no";
    }

    std::string join(const std::vector<std::string>& parts, const std::string& separator)
    {
        std::string result;
        for (const std::string& part : parts) {
            result += (result.empty() ? "" : separator) + part;
        }
        return result;
    }

    std::string fileName(const std::string& path)
    {
        return path.empty() ? "" : fs::path(path).filename().string();
    }
}

namespace Manager
{

MenuProvider::MenuProvider(const Services& services)
    : services_(services)
{
}

void MenuProvider::forget()
{
    treeBuilt_ = false;
    optionsKey_.clear();
    parentsBuilt_ = systemsBuilt_ = false;
    mediaKey_.clear();
    entriesKey_.clear();
    deleteKey_.clear();
}

const std::string& MenuProvider::deleteProblem(const std::string& name)
{
    if (deleteKey_ != name) {
        deleteKey_ = name;
        deleteProblem_ = Editor::checkDeleteMenu(*index_, name);
    }
    return deleteProblem_;
}

void MenuProvider::refresh()
{
    index_.reset();
    forget();
    query_.clear();
    addEntry_.clear();
    addSource_.clear();
    newName_.clear();
    newParent_.clear();
    newSource_.clear();
    newFile_.clear();
    edits_.load("", {});
    deleted_.clear();
}

const Index* MenuProvider::index()
{
    if (!services_.library) {
        return nullptr;
    }
    std::shared_ptr<const Index> latest = services_.library->index();
    if (latest != index_) {
        index_ = latest;
        forget();
    }
    return index_.get();
}

bool MenuProvider::building() const
{
    return services_.library && services_.library->status().state == Indexer::State::Building;
}

unsigned MenuProvider::generation() const
{
    if (!services_.library) {
        return local_;
    }
    Indexer::Status status = services_.library->status();
    return status.generation * 2 + (status.state == Indexer::State::Building ? 1 : 0) + local_ * 7919;
}

int MenuProvider::collectionOf(const Screen& screen)
{
    const Index* idx = index();
    return idx ? idx->findCollection(screen.param("id")) : -1;
}

const CollectionRecord* MenuProvider::record(const Screen& screen)
{
    int c = collectionOf(screen);
    return c < 0 ? nullptr : &index_->collections[static_cast<size_t>(c)];
}

// --- Describing collections ---------------------------------------------------------

std::string MenuProvider::kindName(int collection)
{
    const CollectionRecord& r = index_->collections[static_cast<size_t>(collection)];
    std::vector<std::string> kinds;
    if (r.is(KindMenu)) {
        kinds.push_back("menu");
    }
    if (r.is(KindMerged)) {
        kinds.push_back("merged collection");
    }
    if (r.is(KindSystem)) {
        kinds.push_back(r.videos ? "videos" : "system");
    }
    return kinds.empty() ? "empty collection" : join(kinds, " + ");
}

std::string MenuProvider::summary(int collection)
{
    const CollectionRecord& r = index_->collections[static_cast<size_t>(collection)];
    std::string text = kindName(collection);
    if (r.is(KindMenu)) {
        text += ", " + count(r.children.size(), "entry", "entries");
    }
    if (r.is(KindMerged) || r.is(KindSystem)) {
        text += ", " + count(index_->itemsOf(collection).size(), "game", "games");
    }
    return text;
}

std::string MenuProvider::pageName(int collection)
{
    const CollectionRecord& r = index_->collections[static_cast<size_t>(collection)];
    const LayoutRecord* layout = nullptr;
    int l = index_->findLayout(r.layout.empty() ? index_->activeLayout : r.layout);
    if (l >= 0) {
        layout = &index_->layouts[static_cast<size_t>(l)];
    }
    if (!layout) {
        return "RetroFE's built-in page";
    }
    // layoutFromAnotherCollection wins over the collection's own page and the menu page
    std::string borrowed = index_->properties.getOr("collections." + r.name + ".layoutFromAnotherCollection");
    if (!borrowed.empty()) {
        return borrowed + "'s page in " + layout->name + " (layoutFromAnotherCollection)";
    }
    const auto& pages = layout->collectionLayouts;
    if (std::find(pages.begin(), pages.end(), r.name) != pages.end()) {
        return "Its own page in " + layout->name + " (collections/" + r.name + ")";
    }
    bool menuPage = r.is(KindMenu) && !r.children.empty() && index_->itemsOf(collection).empty();
    if (menuPage && std::find(pages.begin(), pages.end(), "_menu") != pages.end()) {
        return layout->name + "'s menu page (collections/_menu)";
    }
    if (r.is(KindMenu) && r.children.empty() && index_->itemsOf(collection).empty()) {
        return layout->name + "'s games page, until it has entries";
    }
    return layout->name + "'s games page";
}

std::string MenuProvider::entryProblem(int menu, size_t i, bool brief)
{
    const CollectionRecord& m = index_->collections[static_cast<size_t>(menu)];
    const MenuEntry& e = m.children[i];
    size_t times = static_cast<size_t>(std::count_if(m.children.begin(), m.children.end(),
                                                     [&](const MenuEntry& other) { return other.entry == e.entry; }));
    if (times > 1) {
        return brief ? "listed " + std::to_string(times) + " times"
                     : "Listed " + std::to_string(times) + " times: RetroFE shows it that often. Removing it takes out every line.";
    }
    if (e.collection < 0) {
        return brief ? "no such folder" : "Missing: there's no collections/" + e.entry + " folder.";
    }
    const CollectionRecord& child = index_->collections[static_cast<size_t>(e.collection)];
    if (!e.exactCase) {
        return brief ? "folder is spelt " + child.name
                     : "Spelt differently from the folder \"" + child.name + "\": RetroFE won't find its settings.";
    }
    if (e.collection == menu) {
        return brief ? "loop: lists itself" : "Loop: the menu lists itself.";
    }
    for (const MenuRow& row : index_->menuTree(child.name)) {
        if (row.collection == menu) {
            return brief ? "loop back to " + m.name : "Loop: it leads back to " + m.name + ".";
        }
    }
    return "";
}

const MenuEntry* MenuProvider::entry(const Screen& screen)
{
    int position = entryPosition(screen);
    const CollectionRecord* r = record(screen);
    return position < 0 ? nullptr : &r->children[static_cast<size_t>(position)];
}

int MenuProvider::entryPosition(const Screen& screen)
{
    const CollectionRecord* r = record(screen);
    if (!r) {
        return -1;
    }
    std::string wanted = screen.param("entry");
    // A row passes its position too, so a line listed twice opens the one chosen
    // (until a change moves it; then the first line of that name)
    std::string at = screen.param("position");
    if (!at.empty() && at.find_first_not_of("0123456789") == std::string::npos && at.size() < 6) {
        size_t i = static_cast<size_t>(std::stoul(at));
        if (i < r->children.size() && r->children[i].entry == wanted) {
            return static_cast<int>(i);
        }
    }
    for (size_t i = 0; i < r->children.size(); i++) {
        if (r->children[i].entry == wanted) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// --- The tree ------------------------------------------------------------------------

void MenuProvider::buildTree()
{
    if (treeBuilt_ || !index()) {
        return;
    }
    treeBuilt_ = true;
    tree_.clear();
    paths_.clear();
    unlisted_.clear();
    std::vector<MenuRow> rows = index_->menuTree();
    std::set<int> reached;
    for (size_t i = 0; i < rows.size(); i++) {
        const MenuRow& row = rows[i];
        Forms::Row out;
        std::string name = row.collection >= 0 ? index_->collections[static_cast<size_t>(row.collection)].name : row.entry;
        out.id = name;
        out.label = std::string(static_cast<size_t>(row.depth) * 6, ' ') + row.entry;
        std::string parent;
        bool exact = true;
        if (row.parentRow >= 0) {
            const MenuRow& up = rows[static_cast<size_t>(row.parentRow)];
            if (up.collection >= 0) {
                parent = index_->collections[static_cast<size_t>(up.collection)].name;
                for (const MenuEntry& e : index_->collections[static_cast<size_t>(up.collection)].children) {
                    if (e.entry == row.entry) {
                        exact = e.exactCase;
                    }
                }
            }
            out.params["via"] = parent;
        }
        // Details stay short: the screen has room for a few words on the right
        if (row.collection < 0) {
            out.detail = "no such folder";
        }
        else if (row.cycle) {
            out.detail = "loop: already above";
        }
        else {
            out.detail = summary(row.collection);
            if (!exact) {
                out.detail = "folder is " + name + "; " + out.detail;
            }
            if (row.depth == 0) {
                out.detail += "; Browse";
            }
        }
        if (row.collection >= 0) {
            reached.insert(row.collection);
        }
        std::string path = row.entry;
        for (int up = row.parentRow; up >= 0; up = rows[static_cast<size_t>(up)].parentRow) {
            path = rows[static_cast<size_t>(up)].entry + " > " + path;
        }
        paths_.push_back(path);
        tree_.push_back(std::move(out));
    }
    for (size_t c = 0; c < index_->collections.size(); c++) {
        const CollectionRecord& r = index_->collections[c];
        // Menus, merged collections, and ones with nothing yet (which can become either)
        if (r.reserved || reached.count(static_cast<int>(c)) || !(r.is(KindMenu) || r.is(KindMerged) || r.kinds == 0)) {
            continue;
        }
        Forms::Row out;
        out.id = r.name;
        out.label = r.name;
        out.detail = summary(static_cast<int>(c));
        if (!r.parents.empty()) {
            std::vector<std::string> parents;
            for (int p : r.parents) {
                parents.push_back(index_->collections[static_cast<size_t>(p)].name);
            }
            out.detail += ", in " + join(parents, ", ");
        }
        unlisted_.push_back(std::move(out));
    }
}

Forms::Rows MenuProvider::treeRows(size_t limit)
{
    buildTree();
    Forms::Rows result;
    std::string needle = toLower(Utils::trimEnds(query_));
    for (size_t i = 0; i < tree_.size(); i++) {
        if (!needle.empty() && toLower(paths_[i]).find(needle) == std::string::npos) {
            continue;
        }
        result.total++;
        if (result.items.size() < limit) {
            Forms::Row row = tree_[i];
            if (!needle.empty()) {
                row.label = paths_[i];
            }
            result.items.push_back(std::move(row));
        }
    }
    return result;
}

// --- Choices ---------------------------------------------------------------------------

std::string MenuProvider::choice(const std::string& value, const Options& options)
{
    for (const auto& option : options) {
        if (option.first == value) {
            return value;
        }
    }
    return options.empty() ? "" : options.front().first;
}

const MenuProvider::Options& MenuProvider::addableEntries(const Screen& screen)
{
    std::string key = "entries|" + screen.param("id");
    int m = collectionOf(screen);
    if (optionsKey_ == key) {
        return addable_;
    }
    optionsKey_ = key;
    addable_.clear();
    sources_.clear();
    if (m < 0) {
        return addable_;
    }
    const CollectionRecord& menu = index_->collections[static_cast<size_t>(m)];
    for (size_t c = 0; c < index_->collections.size(); c++) {
        const CollectionRecord& r = index_->collections[c];
        if (static_cast<int>(c) == m || iequals(r.name, "Home") || iequals(r.name, "Catalog")
            || std::any_of(menu.children.begin(), menu.children.end(), [&](const MenuEntry& e) { return e.collection == static_cast<int>(c); })) {
            continue;
        }
        bool loop = false;
        if (r.is(KindMenu)) {
            for (const MenuRow& row : index_->menuTree(r.name)) {
                loop |= row.collection == m;
            }
        }
        if (!loop) {
            addable_.emplace_back(r.name, r.name + " (" + kindName(static_cast<int>(c)) + ")");
        }
    }
    return addable_;
}

const MenuProvider::Options& MenuProvider::addableSources(const Screen& screen)
{
    std::string key = "sources|" + screen.param("id");
    int m = collectionOf(screen);
    if (optionsKey_ == key) {
        return sources_;
    }
    optionsKey_ = key;
    addable_.clear();
    sources_.clear();
    if (m < 0) {
        return sources_;
    }
    const CollectionRecord& merged = index_->collections[static_cast<size_t>(m)];
    for (size_t c = 0; c < index_->collections.size(); c++) {
        const CollectionRecord& r = index_->collections[c];
        if (static_cast<int>(c) == m || !r.is(KindSystem)
            || std::any_of(merged.subs.begin(), merged.subs.end(),
                           [&](const SubSource& s) { return s.collection == static_cast<int>(c) || iequals(s.source, r.name); })) {
            continue;
        }
        sources_.emplace_back(r.name, r.name + " (" + count(index_->itemsOf(static_cast<int>(c)).size(), "game", "games") + ")");
    }
    return sources_;
}

const MenuProvider::Options& MenuProvider::parentMenus()
{
    if (parentsBuilt_ || !index()) {
        return parents_;
    }
    parentsBuilt_ = true;
    parents_.clear();
    // The same rule the Editor applies (Editor::checkMenuParent), top menu first
    int top = index_->findCollection(index_->topMenu);
    std::string topName = top >= 0 ? index_->collections[static_cast<size_t>(top)].name : index_->topMenu;
    if (Editor::checkMenuParent(*index_, topName).empty()) {
        parents_.emplace_back(topName, topName + " (Browse)");
    }
    for (size_t c = 0; c < index_->collections.size(); c++) {
        const CollectionRecord& r = index_->collections[c];
        if (static_cast<int>(c) == top || r.reserved || !Editor::checkMenuParent(*index_, r.name).empty()) {
            continue;
        }
        parents_.emplace_back(r.name, r.name);
    }
    return parents_;
}

const MenuProvider::Options& MenuProvider::systems()
{
    if (systemsBuilt_ || !index()) {
        return systems_;
    }
    systemsBuilt_ = true;
    systems_.clear();
    for (size_t c = 0; c < index_->collections.size(); c++) {
        const CollectionRecord& r = index_->collections[c];
        if (r.is(KindSystem)) {
            systems_.emplace_back(r.name, r.name + " (" + count(index_->itemsOf(static_cast<int>(c)).size(), "game", "games") + ")");
        }
    }
    return systems_;
}

// --- Media -------------------------------------------------------------------------------

const std::vector<MenuProvider::MediaInfo>& MenuProvider::media(const Screen& screen)
{
    int c = collectionOf(screen);
    std::string key = c < 0 ? "" : index_->collections[static_cast<size_t>(c)].name;
    if (key == mediaKey_) {
        return media_;
    }
    mediaKey_ = key;
    media_.clear();
    if (c < 0) {
        return media_;
    }
    const CollectionRecord& r = index_->collections[static_cast<size_t>(c)];
    std::vector<std::string> types = standardSlots();
    // Other artwork already in system_artwork (e.g. a OneSauce "neon.png")
    std::string folder = index_->properties.mediaPath(r.name, "logo", true);
    std::error_code ec;
    if (fs::is_directory(folder, ec)) {
        for (const auto& item : fs::directory_iterator(folder, ec)) {
            std::string name;
            if (item.is_directory(ec) && narrowName(item.path().filename(), name) && !name.empty() && name[0] != '.') {
                // A media folder (fork compat) holding art of the kind its name suggests
                if (std::none_of(types.begin(), types.end(), [&](const std::string& t) { return iequals(t, name); })
                    && !mediaFolderFiles(folder, name, defaultKind(name)).empty()) {
                    types.push_back(name);
                }
                continue;
            }
            if (!item.is_regular_file(ec) || !narrowName(item.path().filename(), name) || name.find('.') == std::string::npos
                || name[0] == '.') {
                continue;
            }
            std::string stem = name.substr(0, name.find_last_of('.'));
            std::string extension = toLower(name.substr(name.find_last_of('.') + 1));
            bool media = false;
            for (MediaKind kind : { MediaKind::Image, MediaKind::Video, MediaKind::Text }) {
                const auto& accepted = acceptedExtensions(kind);
                media |= std::find(accepted.begin(), accepted.end(), extension) != accepted.end();
            }
            if (media && std::none_of(types.begin(), types.end(), [&](const std::string& t) { return iequals(t, stem); })) {
                types.push_back(stem);
            }
        }
    }
    for (const std::string& type : types) {
        MediaInfo info;
        MediaKind kind = defaultKind(type);
        info.slot = collectionMedia(*index_, c, type, kind);
        if (services_.editor) {
            for (const std::string& extension : acceptedExtensions(kind)) {
                info.canRestore |= services_.editor->backups().hasOriginal(
                    Utils::combinePath(info.slot.directory, info.slot.basename + "." + extension));
            }
            info.canRestore |= !services_.editor->mediaFolderOriginals(info.slot).empty();
        }
        if (!r.parents.empty()) {
            int p = r.parents.front();
            MediaSlot shown = menuEntryMedia(*index_, p, r.name, type, kind);
            if (shown.own && shown.file != info.slot.file) {
                info.shownInstead = "In " + index_->collections[static_cast<size_t>(p)].name + ", its own " + slotLabel(type)
                    + " for this entry is shown instead: " + shown.file;
            }
        }
        if (kind == MediaKind::Text && !info.slot.file.empty()) {
            std::string text;
            if (readFile(info.slot.file, text)) {
                info.text = text.size() > 400 ? text.substr(0, 400) + "..." : text;
            }
        }
        media_.push_back(std::move(info));
    }
    return media_;
}

const MenuProvider::MediaInfo* MenuProvider::medium(const Screen& screen)
{
    std::string type = screen.param("type");
    for (const MediaInfo& info : media(screen)) {
        if (info.slot.type == type) {
            return &info;
        }
    }
    return nullptr;
}

// --- Screens ---------------------------------------------------------------------------------

std::map<std::string, std::string> MenuProvider::readInfo(const CollectionRecord& r)
{
    std::map<std::string, std::string> values;
    for (const std::string& key : infoKeys()) {
        values[key] = index_->properties.getOr("collections." + r.name + "." + key);
    }
    return values;
}

void MenuProvider::enter(const Screen& screen)
{
    if (screen.form == "menu-info") {
        const CollectionRecord* r = record(screen);
        if (r && r->name != edits_.id()) {
            edits_.load(r->name, readInfo(*r));
        }
    }
    else if (screen.form == "menu-new") {
        newParent_.clear();
    }
    else if (screen.form == "menu-medium") {
        newFile_.clear();
    }
    // Artwork may have changed on disk while another screen was open
    if (screen.form == "menu" || screen.form == "menu-media" || screen.form == "menu-medium" || screen.form == "menu-medium-file") {
        mediaKey_.clear();
    }
}

std::string MenuProvider::leaving(const Screen& screen)
{
    return screen.form == "menu-info" && edits_.isDirty()
        ? "You have unsaved changes. Save them, or leave again to discard them." : "";
}

void MenuProvider::discard(const Screen& screen)
{
    if (screen.form == "menu-info") {
        edits_.reset();
    }
}

std::string MenuProvider::get(const std::string& key, const Screen& screen)
{
    const Index* idx = index();

    if (key == "status") {
        if (!services_.library) {
            return "The library isn't available.";
        }
        return statusText(services_.library->status(), idx != nullptr);
    }
    if (key == "isFailed") {
        return yesNo(services_.library && services_.library->status().state == Indexer::State::Failed);
    }
    if (key == "isReady") {
        return yesNo(idx != nullptr);
    }
    if (key == "query") {
        return query_;
    }
    if (!idx) {
        return "";
    }

    // menus
    if (key == "browseNote") {
        int top = idx->findCollection(idx->topMenu);
        if (top < 0) {
            return "Browse opens " + idx->topMenu + " (firstCollection), which doesn't exist yet. Make a menu called "
                + idx->topMenu + ", or choose another one for Browse in Settings.";
        }
        return "Browse opens " + idx->collections[static_cast<size_t>(top)].name
            + " (firstCollection in Settings). Everything below is reached from it.";
    }
    if (key == "hasUnlisted") {
        buildTree();
        return yesNo(!unlisted_.empty());
    }

    // menu-new
    if (key == "newKind") {
        return newKind_ == "merged" ? "merged" : "menu";
    }
    if (key == "isNewMerged") {
        return yesNo(newKind_ == "merged");
    }
    if (key == "newName") {
        return newName_;
    }
    if (key == "newParent") {
        std::string wanted = newParent_.empty() ? screen.param("id") : newParent_;
        const Options& parents = parentMenus();
        for (const auto& option : parents) {
            if (iequals(option.first, wanted)) {
                return option.first;
            }
        }
        return parents.empty() ? "" : parents.front().first;
    }
    if (key == "newParentProblem") {
        // Opened from a menu's Actions: it must be one that can list collections
        // (never fall back to another menu without saying so)
        std::string id = screen.param("id");
        if (!newParent_.empty() || id.empty()) {
            return parentMenus().empty() ? "There's no menu a new one could go in." : "";
        }
        std::string problem = Editor::checkMenuParent(*idx, id);
        return problem.empty() ? "" : problem + " Choose another menu above.";
    }
    if (key == "newSource") {
        return choice(newSource_, systems());
    }

    int c = collectionOf(screen);
    const CollectionRecord* r = c < 0 ? nullptr : &idx->collections[static_cast<size_t>(c)];

    if (key == "title") {
        return r ? r->title : screen.param("id");
    }
    if (key == "isFound") {
        return yesNo(r != nullptr);
    }
    if (key == "missing") {
        if (r || screen.param("id").empty()) {
            return "";
        }
        if (building()) {
            return "Updating...";
        }
        if (deleted_.count(screen.param("id"))) {
            return screen.param("id") + " was deleted. Back returns to the screen before it.";
        }
        std::string via = screen.param("via");
        return (via.empty() ? "There's" : via + " lists " + screen.param("id") + ", but there's")
            + " no collection folder called " + screen.param("id") + ".";
    }
    if (key == "hasVia") {
        return yesNo(!screen.param("via").empty() && idx->findCollection(screen.param("via")) >= 0
                     && idx->findCollection(screen.param("id")) < 0);
    }
    if (key == "via") {
        return screen.param("via");
    }
    if (!r) {
        return "";
    }
    if (key == "name") {
        return r->name;
    }
    if (key == "kind") {
        return kindName(c);
    }
    if (key == "isMenu") {
        return yesNo(r->is(KindMenu));
    }
    if (key == "isMerged") {
        return yesNo(r->is(KindMerged));
    }
    // Nothing yet (no games, entries or sources): it can become a menu or a merged collection
    if (key == "showEntries") {
        return yesNo(r->is(KindMenu) || r->kinds == 0);
    }
    if (key == "showSources") {
        return yesNo(r->is(KindMerged) || r->kinds == 0);
    }
    if (key == "isSystem") {
        return yesNo(r->is(KindSystem));
    }
    if (key == "browse") {
        return iequals(r->name, idx->topMenu) ? "Browse opens this menu (firstCollection in Settings)." : "";
    }
    if (key == "parents") {
        std::vector<std::string> names;
        for (int p : r->parents) {
            names.push_back(idx->collections[static_cast<size_t>(p)].name);
        }
        return names.empty() ? (iequals(r->name, idx->topMenu) ? "" : "No menu lists it") : join(names, ", ");
    }
    if (key == "entryCount") {
        return r->is(KindMenu) ? std::to_string(r->children.size()) : "";
    }
    if (key == "gameCount") {
        return r->is(KindMerged) || r->is(KindSystem) ? std::to_string(idx->itemsOf(c).size()) : "";
    }
    if (key == "sources") {
        std::vector<std::string> parts;
        for (const SubSource& sub : r->subs) {
            parts.push_back(sub.source + (sub.collection < 0 ? " (missing)"
                : sub.entries.empty() ? " (all games)" : " (" + count(sub.entries.size(), "game", "games") + " listed)"));
        }
        return join(parts, ", ");
    }
    if (key == "page") {
        return pageName(c);
    }
    if (key == "problems") {
        return join(r->notes, " ");
    }
    if (key == "logo") {
        for (const MediaInfo& info : media(screen)) {
            if (info.slot.type == "logo" && info.slot.kind == MediaKind::Image) {
                return info.slot.file;
            }
        }
        return "";
    }
    if (key == "entriesStatus") {
        return count(r->children.size(), "entry", "entries");
    }
    if (key == "sourcesStatus") {
        return count(r->subs.size(), "source", "sources");
    }
    if (key == "infoStatus") {
        size_t have = 0;
        const auto& slots = media(screen);
        for (const MediaInfo& info : slots) {
            have += info.slot.own ? 1 : 0;
        }
        return "Title: " + r->title + ", artwork " + std::to_string(have) + " of " + std::to_string(slots.size());
    }

    // menu-entries / menu-entry
    if (key == "entriesNote") {
        std::vector<std::string> notes;
        if (r->menuSource == MenuSource::MenuFolder) {
            notes.push_back("This menu lists the files in its menu folder, which RetroFE sorts by name: new entries go in name order and can't be moved.");
        }
        else if (r->menuSource == MenuSource::MenuXml) {
            notes.push_back("This menu comes from menu.xml, so it can't be changed here.");
        }
        else if (r->menuSource == MenuSource::CollectionLaunchers) {
            notes.push_back("This menu is built from the collectionLaunchers setting, so it can't be changed here.");
        }
        if (r->hasMenuSupported) {
            notes.push_back("menu_supported.txt (RetroFE Plus) is kept in step.");
        }
        if (!r->is(KindMenu) && r->is(KindSystem)) {
            notes.push_back(r->name + " has games of its own, so it doesn't list other collections.");
        }
        return join(notes, " ");
    }
    if (key == "addEntry") {
        return choice(addEntry_, addableEntries(screen));
    }
    if (key == "addSource") {
        return choice(addSource_, addableSources(screen));
    }
    if (key == "entryName" || key == "entryPosition" || key == "entryOpens" || key == "entryProblem"
        || key == "entryExists" || key == "entryMisspelt") {
        const MenuEntry* e = entry(screen);
        int position = entryPosition(screen);
        if (!e) {
            if (key == "entryProblem") {
                return building() ? "Updating..." : screen.param("entry") + " isn't listed in " + r->name + " any more.";
            }
            if (key == "entryName") {
                return screen.param("entry");
            }
            return key == "entryExists" || key == "entryMisspelt" ? "no" : "";
        }
        if (key == "entryName") {
            return e->entry;
        }
        if (key == "entryPosition") {
            return std::to_string(position + 1) + " of " + std::to_string(r->children.size());
        }
        if (key == "entryOpens") {
            return e->collection < 0 ? "" : idx->collections[static_cast<size_t>(e->collection)].name + " (" + summary(e->collection) + ")";
        }
        if (key == "entryProblem") {
            return entryProblem(c, static_cast<size_t>(position));
        }
        if (key == "entryExists") {
            return yesNo(e->collection >= 0);
        }
        return yesNo(e->collection >= 0 && !e->exactCase);
    }

    // menu-source
    if (key == "sourceName" || key == "sourceShows" || key == "sourceFile" || key == "sourceProblem" || key == "sourceMisspelt") {
        const SubSource* sub = nullptr;
        for (const SubSource& s : r->subs) {
            if (s.source == screen.param("source")) {
                sub = &s;
            }
        }
        if (!sub) {
            return key == "sourceProblem" ? (building() ? "Updating..." : r->name + " has no " + screen.param("source") + ".sub any more.")
                 : key == "sourceName" ? screen.param("source") : key == "sourceMisspelt" ? "no" : "";
        }
        if (key == "sourceMisspelt") {
            return yesNo(sub->collection >= 0 && !sub->exactCase);
        }
        if (key == "sourceName") {
            return sub->source;
        }
        if (key == "sourceFile") {
            return sub->file;
        }
        if (key == "sourceProblem") {
            if (sub->collection < 0) {
                return "There's no collection called " + sub->source + ", so this .sub shows nothing.";
            }
            if (!sub->exactCase) {
                return sub->source + ".sub is spelt differently from the folder "
                    + idx->collections[static_cast<size_t>(sub->collection)].name + ", so RetroFE finds none of its games.";
            }
            return "";
        }
        if (sub->collection < 0) {
            return "nothing";
        }
        size_t shown = 0;
        for (const ItemRef& item : idx->itemsOf(c)) {
            shown += item.collection == sub->collection ? 1 : 0;
        }
        return sub->entries.empty()
            ? "All games of " + sub->source + " (" + std::to_string(shown) + ")"
            : count(sub->entries.size(), "game", "games") + " listed, " + std::to_string(shown) + " found";
    }

    // menu-info
    if (key == "isDirty") {
        return yesNo(edits_.isDirty());
    }
    std::string info = infoKeyOf(key);
    if (!info.empty()) {
        return edits_.id() == r->name ? edits_.get(info) : readInfo(*r)[info];
    }

    // menu-medium
    if (key == "mediumTitle" || key == "mediumImage" || key == "mediumFile" || key == "mediumFolder" || key == "mediumWhere"
        || key == "mediumText" || key == "isImageSlot" || key == "isVideoSlot" || key == "isTextSlot" || key == "canClear"
        || key == "canRestore" || key == "mediumFallback" || key == "mediumFolderNote" || key == "hasMediaFolder"
        || key.rfind("folderFile", 0) == 0) {
        const MediaInfo* m = medium(screen);
        if (!m) {
            return key.rfind("is", 0) == 0 || key.rfind("can", 0) == 0 || key.rfind("has", 0) == 0 ? "no" : "";
        }
        const MediaSlot& slot = m->slot;
        if (key.rfind("folderFile", 0) == 0) {
            // One file of the slot's media folder (param file)
            std::string name = screen.param("file");
            std::string file = slot.folder.empty() ? "" : Utils::combinePath(slot.folder, name);
            bool there = std::find(slot.folderFiles.begin(), slot.folderFiles.end(), file) != slot.folderFiles.end();
            if (key == "folderFileName") return name;
            if (key == "folderFileFolder") return slot.folder;
            if (key == "folderFileImage") return there && slot.kind == MediaKind::Image ? file : "";
            if (key == "folderFileNote") {
                if (!there) return "This file isn't in the folder any more.";
                if (slot.fileBesideFolder()) return "A file beside the folder (" + fileName(slot.file) + ") wins, so RetroFE doesn't show this one.";
                return "One of the folder's " + std::to_string(slot.folderFiles.size()) + " files: RetroFE shows one of them at random.";
            }
            return "";
        }
        if (key == "mediumTitle") {
            return slotLabel(slot.type) + " of " + r->name;
        }
        if (key == "mediumImage") {
            return slot.kind == MediaKind::Image ? slot.file : "";
        }
        if (key == "mediumFile") {
            if (slot.file.empty()) return "None";
            if (!slot.fromFolder()) return slot.file;
            // (the folder's path is in the note below, or the fallback's here)
            std::string shown = mediaShownText(slot);
            shown[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shown[0])));
            return slot.own ? shown : shown + ", in " + fs::path(slot.file).parent_path().string();
        }
        if (key == "mediumFolderNote") {
            return mediaFolderNote(slot);
        }
        if (key == "hasMediaFolder") {
            return yesNo(!slot.folder.empty());
        }
        if (key == "mediumFolder") {
            return slot.directory.empty() ? "" : Utils::combinePath(slot.directory, slot.basename + ".*");
        }
        if (key == "mediumWhere") {
            return m->shownInstead;
        }
        if (key == "mediumFallback") {
            return !slot.own && !slot.file.empty() ? "This shows " + slot.fallback + "'s file, not one of its own." : "";
        }
        if (key == "mediumText") {
            return m->text;
        }
        if (key == "isImageSlot") {
            return yesNo(slot.kind == MediaKind::Image);
        }
        if (key == "isVideoSlot") {
            return yesNo(slot.kind == MediaKind::Video);
        }
        if (key == "isTextSlot") {
            return yesNo(slot.kind == MediaKind::Text);
        }
        if (key == "canClear") {
            return yesNo(slot.own && !slot.fromFolder());
        }
        return yesNo(m->canRestore);
    }
    if (key == "newFile") {
        return newFile_;
    }

    // menu-actions
    if (key == "deleteNote") {
        const std::string& problem = deleteProblem(r->name);
        if (!problem.empty()) {
            return "Can't delete: " + problem;
        }
        std::vector<std::string> parents;
        for (int p : r->parents) {
            parents.push_back(idx->collections[static_cast<size_t>(p)].name);
        }
        std::string note = "Deleting " + r->name + " ";
        note += parents.empty() ? "affects no menu (none lists it)" : "takes it out of " + join(parents, ", ");
        if (!r->children.empty()) {
            note += parents.empty()
                ? ". Its " + count(r->children.size(), "entry", "entries") + " won't be in any menu, but the collections stay"
                : ", and its " + count(r->children.size(), "entry", "entries") + (r->children.size() == 1 ? " moves" : " move")
                  + " up into " + join(parents, ", ") + " in its place";
        }
        if (r->is(KindMerged)) {
            note += ". The games it shows stay in their systems";
        }
        return note + ". Its files are kept in manage/backups.";
    }
    if (key == "canHaveChildren") {
        return yesNo(Editor::checkMenuParent(*idx, r->name).empty());
    }
    return "";
}

Forms::Severity MenuProvider::severity(const std::string& key, const Screen& screen)
{
    if (key == "entryProblem") {
        std::string problem = get(key, screen);
        return problem.empty() || problem == "Updating..." ? Forms::Severity::None : Forms::Severity::Warning;
    }
    return Forms::Severity::None;
}

void MenuProvider::set(const std::string& key, const std::string& value, const Screen& screen)
{
    (void)screen;
    if (key == "query") {
        query_ = value;
    }
    else if (key == "addEntry") {
        addEntry_ = value;
    }
    else if (key == "addSource") {
        addSource_ = value;
    }
    else if (key == "newKind") {
        newKind_ = value;
    }
    else if (key == "newName") {
        newName_ = Utils::trimEnds(value);
    }
    else if (key == "newParent") {
        newParent_ = value;
    }
    else if (key == "newSource") {
        newSource_ = value;
    }
    else if (key == "newFile") {
        newFile_ = Utils::trimEnds(value);
    }
    else {
        std::string info = infoKeyOf(key);
        if (!info.empty()) {
            edits_.set(info, Utils::trimEnds(value));
        }
    }
}

Forms::Rows MenuProvider::rows(const std::string& key, size_t limit, const Screen& screen)
{
    Forms::Rows result;
    if (!index()) {
        return result;
    }
    auto take = [&](const Forms::Row& row) {
        result.total++;
        if (result.items.size() < limit) {
            result.items.push_back(row);
        }
    };
    if (key == "tree") {
        return treeRows(limit);
    }
    if (key == "unlisted") {
        buildTree();
        std::string needle = toLower(Utils::trimEnds(query_));
        for (const Forms::Row& row : unlisted_) {
            if (needle.empty() || toLower(row.id).find(needle) != std::string::npos) {
                take(row);
            }
        }
        return result;
    }

    int c = collectionOf(screen);
    if (c < 0) {
        return result;
    }
    const CollectionRecord& r = index_->collections[static_cast<size_t>(c)];
    if (key == "entries") {
        if (entriesKey_ != r.name) {
            entriesKey_ = r.name;
            entryRows_.clear();
            for (size_t i = 0; i < r.children.size(); i++) {
                const MenuEntry& e = r.children[i];
                Forms::Row row;
                row.id = e.entry;
                row.label = std::to_string(i + 1) + ". " + e.entry;
                std::string problem = entryProblem(c, i, true);
                row.detail = problem.empty() ? summary(e.collection) : problem;
                row.severity = problem.empty() ? Forms::Severity::None : Forms::Severity::Warning;
                row.params["id"] = r.name;
                row.params["position"] = std::to_string(i);
                entryRows_.push_back(std::move(row));
            }
        }
        for (const Forms::Row& row : entryRows_) {
            take(row);
        }
    }
    else if (key == "sources") {
        for (const SubSource& sub : r.subs) {
            Forms::Row row;
            row.id = sub.source;
            row.label = sub.source;
            if (sub.collection < 0) {
                row.detail = "missing: no such system";
            }
            else if (!sub.exactCase) {
                row.detail = "spelt differently from " + index_->collections[static_cast<size_t>(sub.collection)].name;
            }
            else {
                row.detail = sub.entries.empty() ? "all games (" + std::to_string(index_->itemsOf(sub.collection).size()) + ")"
                                                 : count(sub.entries.size(), "game", "games") + " listed";
            }
            row.params["id"] = r.name;
            take(row);
        }
    }
    else if (key == "media") {
        for (const MediaInfo& info : media(screen)) {
            Forms::Row row;
            row.id = info.slot.type;
            row.label = slotLabel(info.slot.type);
            row.detail = info.slot.own ? mediaShownText(info.slot)
                       : info.slot.file.empty() ? "none" : "from " + info.slot.fallback;
            if (!info.shownInstead.empty()) {
                row.detail += " (the menu's own is shown)";
            }
            if (info.slot.kind == MediaKind::Image) {
                row.image = info.slot.file;
            }
            row.params["id"] = r.name;
            take(row);
        }
    }
    else if (key == "folderFiles") {
        // The files of the slot's own media folder, each opening its screen
        const MediaInfo* m = medium(screen);
        for (size_t i = 0; m && i < m->slot.folderFiles.size(); i++) {
            Forms::Row row;
            row.id = fileName(m->slot.folderFiles[i]);
            row.label = row.id;
            row.detail = m->slot.fileBesideFolder() ? "not shown" : "";
            row.image = m->slot.kind == MediaKind::Image ? m->slot.folderFiles[i] : "";
            row.params = { { "id", r.name }, { "type", m->slot.type } };
            take(row);
        }
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> MenuProvider::options(const std::string& key, const Screen& screen)
{
    if (!index()) {
        return {};
    }
    if (key == "addable") {
        return addableEntries(screen);
    }
    if (key == "addableSources") {
        return addableSources(screen);
    }
    if (key == "parentMenus") {
        return parentMenus();
    }
    if (key == "systems") {
        return systems();
    }
    if (key == "newKinds") {
        return { { "menu", "Menu" }, { "merged", "Merged collection" } };
    }
    return {};
}

// --- Actions --------------------------------------------------------------------------------

std::string MenuProvider::statusText(const Indexer::Status& status, bool hasIndex)
{
    if (status.state == Indexer::State::Failed) {
        return (hasIndex ? "Couldn't re-read your collections: " : "Couldn't read your collections: ") + status.error
            + (hasIndex ? " What's shown may be out of date, so changes wait until they're read again." : "");
    }
    if (!hasIndex) {
        return "Reading your collections... " + std::to_string(static_cast<int>(std::lround(status.progress * 100))) + "%";
    }
    return status.state == Indexer::State::Building ? "Updating..." : "";
}

std::string MenuProvider::writeProblemFor(const Indexer::Status& status, bool hasIndex)
{
    if (!hasIndex) {
        return status.state == Indexer::State::Failed ? "Your collections couldn't be read." : "Still reading your collections.";
    }
    if (status.state == Indexer::State::Failed) {
        // The snapshot is older than the files: writing against it could undo changes
        return "Your collections couldn't be read again after the last change; read them again first.";
    }
    if (status.state == Indexer::State::Building) {
        return "Updating after the last change; one moment.";
    }
    return "";
}

std::string MenuProvider::writeProblem()
{
    if (!services_.library || !services_.editor) {
        return "Changes can't be made here.";
    }
    return writeProblemFor(services_.library->status(), index() != nullptr);
}

std::string MenuProvider::actionProblem(const std::string& action, const Screen& screen)
{
    static const std::vector<std::string> known = {
        "addEntry", "moveUp", "moveDown", "removeEntry", "respell", "openEntry", "addSource", "removeSource",
        "save", "reset", "replaceMedium", "clearMedium", "restoreMedium", "create", "delete", "openSystem", "openVia",
        "respellSource", "retry", "addToFolder", "clearFolder", "removeFolderFile", "keepFolderFile",
    };
    if (std::find(known.begin(), known.end(), action) == known.end()) {
        return Provider::actionProblem(action, screen);
    }
    if (action == "retry") {
        if (!services_.library) {
            return "The library isn't available.";
        }
        return services_.library->status().state == Indexer::State::Failed ? "" : "Your collections were read fine.";
    }
    if (action == "reset") {
        return edits_.isDirty() ? "" : "No changes to undo.";
    }
    if (action == "openVia") {
        return get("hasVia", screen) == "yes" ? "" : "No menu to go back to.";
    }
    if (action == "create") {
        std::string problem = writeProblem();
        if (!problem.empty()) {
            return problem;
        }
        if (newName_.empty()) {
            return "Give it a name.";
        }
        if (newKind_ == "merged" && systems().empty()) {
            return "There are no systems whose games it could show.";
        }
        return get("newParentProblem", screen);
    }
    const CollectionRecord* r = record(screen);
    if (!r) {
        return building() ? "Updating..." : "There's no collection called \"" + screen.param("id") + "\".";
    }
    if (action == "openSystem") {
        return r->is(KindSystem) ? "" : r->name + " has no games of its own.";
    }
    if (action == "openEntry") {
        const MenuEntry* e = entry(screen);
        return e && e->collection >= 0 ? "" : "There's no collection to open.";
    }
    if (action == "save") {
        if (!edits_.isDirty()) {
            return "No changes to save.";
        }
        return writeProblem();
    }
    std::string problem = writeProblem();
    if (!problem.empty()) {
        return problem;
    }
    if (action == "addEntry") {
        std::string parentProblem = Editor::checkMenuParent(*index_, r->name);
        if (!parentProblem.empty()) {
            return parentProblem;
        }
        return addableEntries(screen).empty() ? "Every collection that could go here is listed already." : "";
    }
    if (action == "moveUp" || action == "moveDown" || action == "removeEntry" || action == "respell") {
        int position = entryPosition(screen);
        if (position < 0) {
            return screen.param("entry") + " isn't listed in " + r->name + ".";
        }
        if (r->menuSource == MenuSource::MenuXml || r->menuSource == MenuSource::CollectionLaunchers) {
            return "This menu can't be changed here.";
        }
        if (action == "respell") {
            const MenuEntry* e = entry(screen);
            return e->collection >= 0 && !e->exactCase ? "" : "It's spelt as the folder is already.";
        }
        if (action == "removeEntry") {
            return "";
        }
        if (r->menuSource != MenuSource::MenuTxt) {
            return "RetroFE sorts this menu's entries by name, so they can't be moved.";
        }
        const std::string& text = r->children[static_cast<size_t>(position)].entry;
        if (std::count_if(r->children.begin(), r->children.end(), [&](const MenuEntry& e) { return e.entry == text; }) > 1) {
            return "It's listed more than once; remove it and add it again first.";
        }
        if (action == "moveUp" && position == 0) {
            return "It's first already.";
        }
        if (action == "moveDown" && position + 1 >= static_cast<int>(r->children.size())) {
            return "It's last already.";
        }
        return "";
    }
    if (action == "addSource") {
        // A merged collection, or one with nothing yet (e.g. after removing its last source)
        if (r->is(KindSystem) || r->is(KindMenu) || r->reserved) {
            return r->name + " isn't a merged collection.";
        }
        return addableSources(screen).empty() ? "Every system is a source already." : "";
    }
    if (action == "respellSource") {
        return get("sourceMisspelt", screen) == "yes" ? "" : "It's spelt as its system is already.";
    }
    if (action == "removeSource") {
        for (const SubSource& sub : r->subs) {
            if (sub.source == screen.param("source")) {
                return "";
            }
        }
        return r->name + " has no " + screen.param("source") + ".sub.";
    }
    if (action == "replaceMedium" || action == "clearMedium" || action == "restoreMedium") {
        const MediaInfo* m = medium(screen);
        if (!m) {
            return "There's no such artwork slot.";
        }
        if (action == "replaceMedium") {
            return newMediumFile(screen).empty() ? "Choose a file first." : "";
        }
        if (action == "clearMedium") {
            if (m->slot.own && m->slot.fromFolder()) {
                return "It's a folder of files, not one file: take files out of it, or clear the folder.";
            }
            return m->slot.own ? "" : m->slot.file.empty() ? "There's nothing to clear." : "It shows " + m->slot.fallback + "'s file; there's none of its own to clear.";
        }
        return m->canRestore ? "" : "Nothing here was changed from Manage.";
    }
    if (action == "addToFolder" || action == "clearFolder" || action == "removeFolderFile" || action == "keepFolderFile") {
        const MediaInfo* m = medium(screen);
        if (!m) {
            return "There's no such artwork slot.";
        }
        if (m->slot.folder.empty()) {
            return "It has no folder of files.";
        }
        if (action == "addToFolder") {
            return newMediumFile(screen).empty() ? "Choose a file first." : "";
        }
        if (action == "clearFolder") {
            return "";
        }
        std::string file = Utils::combinePath(m->slot.folder, screen.param("file"));
        return std::find(m->slot.folderFiles.begin(), m->slot.folderFiles.end(), file) == m->slot.folderFiles.end()
            ? "This file isn't in the folder any more." : "";
    }
    if (action == "delete") {
        return deleteProblem(r->name);
    }
    return "";
}

std::string MenuProvider::newMediumFile(const Screen& screen)
{
    (void)screen;
    return newFile_;
}

Forms::Model::Result MenuProvider::finish(const EditResult& edit, const std::string& what)
{
    Forms::Model::Result result;
    result.ok = edit.ok;
    result.message = edit.message;
    result.info = edit.ok && edit.files.empty();   // nothing written: "... is already in ..."
    // A failure part way can still have changed files: re-read whenever any were touched
    if (!edit.files.empty()) {
        local_++;
        forget();
        if (services_.library) {
            services_.library->changed(what);
        }
    }
    return result;
}

Forms::Model::Result MenuProvider::runAction(const std::string& action, const Screen& screen)
{
    Forms::Model::Result result;
    result.message = actionProblem(action, screen);
    if (!result.message.empty()) {
        return result;
    }
    if (action == "retry") {
        services_.library->rebuild();
        local_++;
        result.ok = true;
        result.message = "Reading your collections again.";
        return result;
    }
    if (!index_ || !services_.editor) {
        result.message = "Changes can't be made here.";
        return result;
    }
    const Index& idx = *index_;
    Editor& editor = *services_.editor;
    std::string name = screen.param("id");

    if (action == "openSystem") {
        result.ok = true;
        result.open = "system";
        result.params["id"] = record(screen)->name;
        return result;
    }
    if (action == "openVia") {
        result.ok = true;
        result.open = "menu-entries";
        result.params["id"] = screen.param("via");
        return result;
    }
    if (action == "openEntry") {
        result.ok = true;
        result.open = "menu";
        result.params["id"] = idx.collections[static_cast<size_t>(entry(screen)->collection)].name;
        result.params["via"] = record(screen)->name;
        return result;
    }
    if (action == "reset") {
        edits_.reset();
        result.ok = true;
        result.info = true;
        return result;
    }
    if (action == "create") {
        EditResult edit = newKind_ == "merged"
            ? editor.createMerged(idx, newName_, get("newParent", screen), choice(newSource_, systems()))
            : editor.createMenu(idx, newName_, get("newParent", screen));
        result = finish(edit, "menus");
        if (result.ok) {
            result.open = "menu";
            result.replace = true;   // Back from the new menu skips the add form
            result.params["id"] = newName_;
            newName_.clear();
        }
        return result;
    }
    if (action == "addEntry") {
        std::string child = choice(addEntry_, addableEntries(screen));
        result = finish(editor.addMenuChild(idx, name, child), "menus");
        if (result.ok) {
            addEntry_.clear();
        }
        return result;
    }
    if (action == "moveUp" || action == "moveDown") {
        int position = entryPosition(screen);
        bool up = action == "moveUp";
        result = finish(editor.moveMenuChild(idx, name, screen.param("entry"), position + (up ? -1 : 1)), "menus");
        if (result.ok) {
            result.message = "Moved " + screen.param("entry") + (up ? " up" : " down") + " to " + std::to_string(position + (up ? 0 : 2))
                + " of " + std::to_string(record(screen)->children.size()) + ".";
        }
        return result;
    }
    if (action == "removeEntry") {
        const std::string text = screen.param("entry");
        const auto& children = record(screen)->children;
        auto times = std::count_if(children.begin(), children.end(), [&](const MenuEntry& e) { return e.entry == text; });
        result = finish(editor.removeMenuChild(idx, name, text), "menus");
        if (result.ok && times > 1) {
            result.message += " It was listed " + std::to_string(times) + " times; every line went.";
        }
        result.back = result.ok;
        return result;
    }
    if (action == "respellSource") {
        result = finish(editor.respellSubSource(idx, name, screen.param("source")), "menus");
        result.back = result.ok;
        return result;
    }
    if (action == "respell") {
        const MenuEntry* e = entry(screen);
        const std::string& folderName = idx.collections[static_cast<size_t>(e->collection)].name;
        std::string error;
        EditResult edit;
        edit.ok = MenuFiles::respell(idx.options.root, record(screen)->name, e->entry, folderName, entryPosition(screen),
                                     &editor.backups(), error);
        edit.message = edit.ok ? "The entry now reads " + folderName + ", as the folder is spelt." : error;
        edit.files.push_back(record(screen)->folder);  // a failed add can follow a done remove
        result = finish(edit, "menus");
        result.back = result.ok;
        return result;
    }
    if (action == "addSource") {
        std::string source = choice(addSource_, addableSources(screen));
        result = finish(editor.addSubSource(idx, name, source), "menus");
        if (result.ok) {
            addSource_.clear();
        }
        return result;
    }
    if (action == "removeSource") {
        result = finish(editor.removeSubSource(idx, name, screen.param("source")), "menus");
        result.back = result.ok;
        return result;
    }
    if (action == "save") {
        const CollectionRecord* r = record(screen);
        std::map<std::string, std::string> changes = edits_.changes();
        for (const auto& [key, value] : changes) {
            if (!value.empty()) {
                std::string problem = Editor::checkSetting(idx, r->name, key, value);
                if (!problem.empty()) {
                    result.message = slotLabel(key) + ": " + problem;
                    return result;
                }
            }
        }
        const std::string savedMessage = "Saved. RetroFE shows it after it reloads, when you leave Manage.";
        EditResult all{ true, savedMessage, {} };
        for (const auto& [key, value] : changes) {
            EditResult edit = value.empty() ? editor.removeCollectionSetting(idx, r->name, key)
                                            : editor.setCollectionSetting(idx, r->name, key, value);
            if (!edit.ok) {
                all.ok = !all.files.empty();
                all.message = (all.files.empty() ? "" : "Saved some changes, but not all. ") + edit.message;
                break;
            }
            all.files.insert(all.files.end(), edit.files.begin(), edit.files.end());
        }
        if (all.ok) {
            edits_.commit();
        }
        result = finish(all, "menus");
        result.ok = all.ok && all.message == savedMessage;
        return result;
    }
    if (action == "replaceMedium") {
        result = finish(editor.replaceMedia(medium(screen)->slot, newMediumFile(screen)), "menu artwork");
        if (result.ok) {
            newFile_.clear();
        }
        return result;
    }
    if (action == "clearMedium") {
        return finish(editor.clearMedia(medium(screen)->slot), "menu artwork");
    }
    if (action == "restoreMedium") {
        MediaSlot slot = medium(screen)->slot;
        EditResult all{ true, "Put back the original " + toLower(slotLabel(slot.type)) + ".", {} };
        for (const std::string& extension : acceptedExtensions(slot.kind)) {
            std::string file = Utils::combinePath(slot.directory, slot.basename + "." + extension);
            if (!editor.backups().hasOriginal(file)) {
                continue;
            }
            EditResult edit = editor.restoreOriginal(file);
            if (!edit.ok) {
                all = edit;
                break;
            }
            all.files.push_back(file);
        }
        // and its media folder's files (fork compat)
        for (const std::string& file : all.ok ? editor.mediaFolderOriginals(slot) : std::vector<std::string>()) {
            EditResult edit = editor.restoreOriginal(file);
            if (!edit.ok) {
                all = edit;
                break;
            }
            all.files.push_back(file);
        }
        return finish(all, "menu artwork");
    }
    if (action == "addToFolder" || action == "clearFolder" || action == "removeFolderFile" || action == "keepFolderFile") {
        MediaSlot slot = medium(screen)->slot;
        EditResult edit = action == "addToFolder" ? editor.addMediaFolderFile(slot, newMediumFile(screen))
                        : action == "clearFolder" ? editor.clearMediaFolder(slot)
                        : action == "removeFolderFile" ? editor.removeMediaFolderFile(slot, screen.param("file"))
                        : editor.mediaFolderToFile(slot, screen.param("file"));
        result = finish(edit, "menu artwork");
        // From one of the folder's files, back to the slot's screen
        result.back = result.ok && (action == "removeFolderFile" || action == "keepFolderFile");
        return result;
    }
    if (action == "delete") {
        std::string folder = record(screen)->name;
        result = finish(editor.deleteMenu(idx, name), "menus");
        result.back = result.ok;
        if (result.ok) {
            deleted_.insert(folder);
        }
        return result;
    }
    return result;
}

}

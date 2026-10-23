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
#include "LayoutEditorScreen.h"
#include "../ThemeProvider.h"
#include "../../FileDialog.h"
#include "../../Home.h"
#include "../../Setup.h"
#include "../../../Collection/CollectionInfo.h"
#include "../../../Collection/Item.h"
#include "../../../Database/Configuration.h"
#include "../../../Database/GlobalOpts.h"
#include "../../../Graphics/Component/Component.h"
#include "../../../Graphics/Page.h"
#include "../../../ThemeEditor/LayoutCheck.h"
#include "../../../ThemeEditor/LayoutPalette.h"
#include "../../../ThemeEditor/ThemeStore.h"
#include "../../../SDL.h"
#include "../../../Utility/Log.h"
#include "../../../Utility/Utils.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

using namespace ThemeEditor;
namespace fs = std::filesystem;

namespace
{
    const float REBUILD_DELAY = 0.25f;
    const size_t PREVIEW_ITEMS = 60;

    // The attributes most elements use, in the order the panel shows them
    const char* const COMMON[] = { "x", "y", "width", "height", "xOrigin", "yOrigin", "xOffset", "yOffset",
                                   "alpha", "layer", "font", "fontSize", "fontColor", "src" };

    void handlePoint(const DragRect& r, int handle, float& x, float& y)
    {
        const float x1 = static_cast<float>(r.x), y1 = static_cast<float>(r.y);
        const float x2 = static_cast<float>(r.x + r.w), y2 = static_cast<float>(r.y + r.h);
        const float hx[8] = { x1, (x1 + x2) * 0.5f, x2, x1, x2, x1, (x1 + x2) * 0.5f, x2 };
        const float hy[8] = { y1, y1, y1, (y1 + y2) * 0.5f, (y1 + y2) * 0.5f, y2, y2, y2 };
        x = hx[handle];
        y = hy[handle];
    }

    // A colour per kind of element
    ImU32 colourFor(const std::string& name, int alpha)
    {
        unsigned int v = 0x9AA0A6;
        if (name == "menu" || name == "item" || name == "itemDefaults") v = 0xF0A030;
        else if (name == "image" || name == "reloadableImage") v = 0x45A8E0;
        else if (name == "video" || name == "reloadableVideo" || name == "reloadableAudio") v = 0xB070E0;
        else if (name == "text" || name == "statusText" || name == "reloadableText" || name == "reloadableScrollingText") v = 0x60C060;
        else if (name == "form" || name == "keyboard" || name == "container") v = 0xE0E060;
        return IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, alpha);
    }

    std::string number(float v)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", std::round(v * 100.0f) / 100.0f);
        return buffer;
    }

    // Saved, unsaved, nothing: what the leave dialog is for
    const char* const UNSAVED = "Unsaved changes##layout";
    const char* const RESTORE = "Put back the original##layout";
    const char* const DELETE_ELEMENT = "Delete element##layout";
    const char* const PALETTE = "Add element##layout";
    // Files bigger than this are announced before they're copied (the copy blocks a frame)
    const uintmax_t LARGE_COPY = 16u * 1024u * 1024u;

    // The file kind an element's src is
    std::string srcKind(const std::string& element)
    {
        if (element == "video") return "video";
        if (element == "sound") return "sound";
        return "image";
    }
}

LayoutEditorScreen::LayoutEditorScreen(const ThemeEditorScreen::Context& context, const std::string& folder, const std::string& page)
    : ctx_(context)
    , folder_(folder)
{
    collections_ = Setup::listCollections();
    std::error_code ec;
    if (fs::is_directory(fs::path(Utils::combinePath(ctx_.root, "collections", "Main")), ec)) {
        collections_.insert(collections_.begin(), "Main");
    }
    previewName_ = ctx_.currentCollection ? ctx_.currentCollection() : "";
    if (previewName_.empty() || previewName_ == Home::collectionName || previewName_ == Home::catalogCollectionName) {
        previewName_ = "Main";
        if (ctx_.config) ctx_.config->getProperty(OPTION_FIRSTCOLLECTION, previewName_);
    }
    if (ctx_.config) ctx_.config->getProperty(OPTION_LAYOUT, originalLayout_);
    lastBrowseDir_ = fs::path(Utils::combinePath(ctx_.root, "layouts", folder_));

    pages_ =LayoutSession::themePages(Utils::combinePath(ctx_.root, "layouts", folder_));
    std::string first = page;
    if (first.empty()) first = pages_.empty() ? std::string("layout.xml") : pages_.front();
    openPage(first);

    Themes::takeEditorScript(ctx_.config, script_, scriptPath_);
}

LayoutEditorScreen::~LayoutEditorScreen()
{
    shutdown();
}

void LayoutEditorScreen::switchPage(const std::string& page)
{
    // The old page's components (and the boxes traced from them) go before the new page is read
    destroyPreview();
    extra_.clear();
    previewDirty_ = true;
    builtText_.clear();
    openPage(page);
}

bool LayoutEditorScreen::openPage(const std::string& page)
{
    LayoutSession next;
    std::string error;
    if (!next.open(ctx_.root, folder_, page, error)) {
        LOG_ERROR("ThemeEditor", "Can't open layouts/" + folder_ + "/" + page + ": " + error);
        if (!session_.isOpen()) openError_ = error;
        else setStatus(error, true);
        return false;
    }
    bool snap = session_.snap;
    session_ = std::move(next);
    session_.snap = snap;
    session_.setCanvasSize(SDL::getWindowWidth(0), SDL::getWindowHeight(0));
    openIssues_ = checkLayoutXml(session_.text());
    hasOriginal_ = session_.hasOriginal();
    seenRevision_ = session_.revision();
    previewDirty_ = true;
    builtText_.clear();
    status_.clear();
    statusIsError_ = false;
    LOG_INFO("ThemeEditor", "Editing layouts/" + folder_ + "/" + session_.page() + " in place");

    // A collection's own page previews with that collection
    const std::string& p = session_.page();
    if (p.rfind("collections/", 0) == 0) {
        std::string collection = p.substr(12, p.find('/', 12) - 12);
        if (std::find(collections_.begin(), collections_.end(), collection) != collections_.end()) previewName_ = collection;
    }
    return true;
}

void LayoutEditorScreen::retire()
{
    if (layoutOverridden_ && ctx_.config) {
        ctx_.config->setProperty(OPTION_LAYOUT, originalLayout_);
        layoutOverridden_ = false;
    }
    if (navFlagsSaved_ && ImGui::GetCurrentContext()) {
        if (savedNavKeyboard_) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        navFlagsSaved_ = false;
    }
    retired_ = true;
}

void LayoutEditorScreen::shutdown()
{
    retire();
    destroyPreview();
    if (target_) {
        SDL_LockMutex(SDL::getMutex());
        SDL_DestroyTexture(target_);
        SDL_UnlockMutex(SDL::getMutex());
        target_ = nullptr;
    }
    master_.reset();
}

// ---- update: everything that builds or frees -------------------------------

void LayoutEditorScreen::update(float dt)
{
    if (retired_ || !session_.isOpen()) return;
    runScript();
    runJob();
    if (!openNext_.empty()) {
        const std::string next = openNext_;
        openNext_.clear();
        switchPage(next);
    }

    if (nudging_) {
        sinceNudge_ += dt;
        if (sinceNudge_ > 0.6f) {
            session_.endCoalesce();
            nudging_ = false;
        }
    }
    if (session_.revision() != seenRevision_) {
        seenRevision_ = session_.revision();
        sinceChange_ = 0.0f;
        previewDirty_ = true;
    }
    else {
        sinceChange_ += dt;
    }
    // Playing an event shows the page as edited: a rebuild that's due happens first
    const bool replay = !replay_.empty() && !session_.dragging();
    if (previewDirty_ && !session_.dragging() && (sinceChange_ >= REBUILD_DELAY || !page_ || replay)) rebuildPreview();
    if (replay && page_) {
        // As RetroFE does: the menus' events go through every menu and component
        page_->setSelectedItem();
        if (page_->getSelectedItem()) page_->triggerEventOnAllMenus(replay_);
        else page_->triggerEvent(replay_);
        LOG_INFO("ThemeEditor", "Playing " + replay_ + " on the preview");
    }
    if (replay) replay_.clear();
    if (page_) page_->update(dt);
    refreshPending();
}

void LayoutEditorScreen::rebuildPreview()
{
    previewDirty_ = false;
    if (page_ && session_.text() == builtText_ && builtCollection_ == previewName_) return;
    destroyPreview();
    builtText_ = session_.text();
    builtCollection_ = previewName_;
    builtRevision_ = session_.revision();
    engineIssues_.clear();
    std::string blocker = layoutBuildBlocker(builtText_);
    if (!blocker.empty()) {
        engineIssues_.push_back("Not previewed: " + blocker);
        return;
    }
    if (!ensureTarget()) return;
    layoutOverridden_ = true;
    ctx_.config->setProperty(OPTION_LAYOUT, folder_);
    CollectionInfo* collection = previewCollection();

    std::vector<std::string> issues;
    Page* page = nullptr;
    // A splash or Manage screen page has no wheel by design: the engine's note about that isn't a problem
    const bool noWheel = session_.page() == "splash.xml" || session_.page().rfind("forms/", 0) == 0;
    trace_.clear();
    {
        Logger::TapScope tap([&issues, noWheel](Logger::Zone, const std::string& component, const std::string& message) {
            if (noWheel && message.find("doesn't have any menus") != std::string::npos) return;
            if (Themes::pageBuildComponent(component)) issues.push_back("[" + component + "] " + message);
        });
        PageBuilder builder(folder_, "layout", *ctx_.config, ctx_.fontCache);
        builder.setLayoutXml(builtText_.c_str(), session_.pageDir());
        builder.setTrace(&trace_);
        page = builder.buildPage();
        if (!page) {
            delete collection;
        }
        else {
            // The boxes: what the page says, before anything animates
            captureGeometry();
            page->pushCollection(collection);
            page->onNewItemSelected();
            page->reallocateMenuSpritePoints();
            page->allocateGraphicsMemory();
            page->start();
        }
    }
    engineIssues_ = issues;
    if (!page) engineIssues_.push_back("PageBuilder couldn't build this page");
    page_ = page;
}

TracedView LayoutEditorScreen::viewOf(const ViewInfo& v, const std::string& path, const void* component, bool slot)
{
    TracedView t;
    t.path = path;
    t.component = component;
    t.slot = slot;
    t.x = v.X;
    t.y = v.Y;
    t.xOffset = v.XOffset;
    t.yOffset = v.YOffset;
    t.xOrigin = v.XOrigin;
    t.yOrigin = v.YOrigin;
    t.scaledW = v.ScaledWidth();
    t.scaledH = v.ScaledHeight();
    t.width = v.Width;
    t.height = v.Height;
    t.maxWidth = v.MaxWidth < FLT_MAX ? v.MaxWidth : -1.0f;
    t.maxHeight = v.MaxHeight < FLT_MAX ? v.MaxHeight : -1.0f;
    t.fontSize = v.FontSize;
    t.layer = static_cast<int>(v.Layer);
    return t;
}

void LayoutEditorScreen::captureGeometry()
{
    session_.clearGeometry();
    traced_.clear();
    extra_.clear();
    std::vector<TracedView> views;
    for (const PageBuilder::TraceEntry& t : trace_) {
        if (!t.component) continue;
        const ViewInfo& v = t.slot ? *t.slot : t.component->baseViewInfo;
        views.push_back(viewOf(v, t.path, t.component, t.slot != nullptr));
        // Art that isn't loaded yet gets its size once it is (refreshPending)
        if (!t.slot) {
            Traced tr;
            tr.path = t.path;
            tr.component = t.component;
            tr.base = v;
            tr.order = static_cast<int>(views.size()) - 1;
            traced_.push_back(tr);
        }
    }
    PageBoxes boxes = pageBoxes(session_.doc(), views);
    for (const auto& entry : boxes.geometry) session_.setGeometry(entry.first, entry.second);
    for (const auto& entry : boxes.extra) extra_.push_back(ExtraBox{ entry.second, entry.first });
    for (Traced& t : traced_) {
        auto it = boxes.geometry.find(t.path);
        t.pending = it != boxes.geometry.end() && it->second.approximate && !it->second.around && it->second.order == t.order;
    }
    // The slots' ViewInfos belong to the page: nothing keeps pointers to them past this
    trace_.clear();
}

void LayoutEditorScreen::refreshPending()
{
    // Art that loads after the build (reloadable images, videos) gives its box a real size
    if (!page_ || session_.dragging() || session_.revision() != builtRevision_) return;
    for (Traced& t : traced_) {
        if (!t.pending || !t.component) continue;
        const ViewInfo& live = t.component->baseViewInfo;
        if (live.ImageWidth <= 0 || live.ImageHeight <= 0) continue;
        ViewInfo v = t.base;
        v.ImageWidth = live.ImageWidth;
        v.ImageHeight = live.ImageHeight;
        const ElementGeom* old = session_.geometry(session_.doc().find(t.path));
        if (!old || old->around) { t.pending = false; continue; }
        ElementGeom g = geometryOf(session_.doc(), viewOf(v, t.path, t.component, false), old->order);
        if (g.approximate) continue;
        g.readOnly = old->readOnly;
        session_.setGeometry(t.path, g);
        t.pending = false;
    }
}

void LayoutEditorScreen::destroyPreview()
{
    traced_.clear();
    trace_.clear();
    if (!page_) return;
    page_->stop();
    page_->deInitialize();
    delete page_;
    page_ = nullptr;
}

bool LayoutEditorScreen::ensureTarget()
{
    int w = SDL::getWindowWidth(0);
    int h = SDL::getWindowHeight(0);
    if (target_ && w == targetW_ && h == targetH_) return true;
    SDL_LockMutex(SDL::getMutex());
    if (target_) SDL_DestroyTexture(target_);
    target_ = SDL_CreateTexture(ctx_.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_UnlockMutex(SDL::getMutex());
    if (!target_) {
        engineIssues_ = { "Can't create the preview's texture: " + std::string(SDL_GetError()) };
        return false;
    }
    targetW_ = w;
    targetH_ = h;
    return true;
}

CollectionInfo* LayoutEditorScreen::previewCollection()
{
    if (!master_ || masterName_ != previewName_) {
        master_.reset(ctx_.loadCollection && !previewName_.empty() ? ctx_.loadCollection(previewName_) : nullptr);
        masterName_ = previewName_;
    }
    auto* copy = new CollectionInfo(*ctx_.config, master_ ? master_->name : previewName_,
                                    master_ ? master_->listpath : "", "", master_ ? master_->metadataType : "", "");
    if (master_) {
        size_t count = std::min(master_->items.size(), PREVIEW_ITEMS);
        for (size_t i = 0; i < count; ++i) copy->items.push_back(new Item(*master_->items[i]));
        copy->menusort = master_->menusort;
        copy->sortType = master_->sortType;
    }
    copy->playlists["all"] = &copy->items;
    return copy;
}

// ---- jobs ----------------------------------------------------------------------

void LayoutEditorScreen::runJob()
{
    Job job = job_;
    job_ = Job::None;
    switch (job) {
    case Job::Save:
        save();
        break;
    case Job::SaveAndLeave:
        if (save()) {
            if (pendingPage_.empty()) back_ = true;
            else switchPage(pendingPage_);
        }
        pendingPage_.clear();
        break;
    case Job::Restore:
        restoreOriginal();
        break;
    case Job::NativePick:
        pickNative();
        break;
    case Job::Import: {
        // The copy blocks: a big file is announced first and copied on the next frame
        std::error_code ec;
        const uintmax_t size = fs::file_size(fs::path(jobPath_), ec);
        if (!ec && size > LARGE_COPY && status_.rfind("Copying ", 0) != 0) {
            setStatus("Copying " + fs::path(jobPath_).filename().string() + " (" + std::to_string(size / (1024 * 1024)) +
                      " MB) into the theme...", false);
            job_ = Job::Import;
            break;
        }
        importChosen(jobPath_, jobFor_);
        break;
    }
    case Job::None:
        break;
    }
}

// ---- files -----------------------------------------------------------------------

void LayoutEditorScreen::chooseFile(const FileFor& what)
{
    chooserFor_ = what;
    Themes::FileChooser::Request request;
    request.filter = what.kind;
    const std::string folder = session_.importFolder(what.kind);
    request.destination = "the theme's " + folder + " folder";
    if (what.addElement) {
        request.title = "Choose the " + what.kind + " to add to the page";
        request.note = "Not now adds it with the placeholder name " +
                       std::string(what.kind == "video" ? "videos/new.mp4" : "images/new.png") +
                       ", which isn't in the theme: RetroFE skips it until it names a file.";
    }
    else {
        request.title = "Choose the " + what.kind + " for " + session_.label(session_.doc().find(what.path));
    }
    std::error_code ec;
    const fs::path root = fs::path(ctx_.root);
    chooser_.open(request, fs::is_directory(lastBrowseDir_, ec) && Themes::insideFolder(lastBrowseDir_, root)
                               ? lastBrowseDir_ : fs::path(session_.themeDir()));
}

// The system's own file dialog: it blocks, so it runs here in update(), never while drawing
void LayoutEditorScreen::pickNative()
{
    if (!FileDialog::isSupported()) return;
    std::string path;
    const std::string filter = jobFor_.kind;
    if (!FileDialog::pickFile(ctx_.window, chooser_.request().title, Themes::chooserFilterLabel(filter), Themes::chooserPattern(filter), path)) {
        return;
    }
    job_ = Job::Import;
    jobPath_ = path;
}

// Copies a chosen file into the theme (a file already in it stays where it
// is) and uses it: for a new element, or as an element's src
bool LayoutEditorScreen::importChosen(const std::string& path, const FileFor& what)
{
    std::string rel, error;
    const std::string folder = session_.importFolder(what.kind);
    // The src as this page writes it: relative to the page's own folder, which PageBuilder looks in first
    if (!importIntoTheme(Utils::combinePath(ctx_.root, "layouts"), folder_, path, folder, session_.pageFolder(), rel, error)) {
        setStatus("Not added: " + error, true);
        return false;
    }
    lastBrowseDir_ = fs::path(path).parent_path();
    LOG_NOTICE("ThemeEditor", "Added " + path + " to layouts/" + folder_ + " as " + rel);
    if (what.addElement) {
        if (!session_.addElement(what.entry, what.type, rel)) {
            setStatus("Added " + rel + " to the theme, but the element couldn't be added to the page.", true);
            return false;
        }
        setStatus("Added " + session_.label(session_.selected()) + " to the page, showing " + rel + ".", false);
        return true;
    }
    if (!session_.selectPath(what.path) || session_.selectedElement()->value("src") == rel) {
        setStatus(session_.selectedElement() ? "It already shows " + rel + "." : "Added " + rel + " to the theme, but the element it was for has gone.",
                  !session_.selectedElement());
        return session_.selectedElement() != nullptr;
    }
    session_.endCoalesce();
    session_.setAttribute("src", rel);
    setStatus("Now showing " + rel + ".", false);
    return true;
}

void LayoutEditorScreen::addFromPalette(const std::string& entry, const std::string& type)
{
    const PaletteEntry* e = paletteEntry(entry);
    if (!e) return;
    if (!e->file.empty()) {
        FileFor what;
        what.addElement = true;
        what.entry = entry;
        what.type = type;
        what.kind = e->file;
        chooseFile(what);
        return;
    }
    if (session_.addElement(entry, type)) setStatus("Added " + session_.label(session_.selected()) + " to the page.", false);
}

bool LayoutEditorScreen::save()
{
    std::string error;
    const bool first = !hasOriginal_;
    if (!session_.save(error)) {
        setStatus("Not saved: " + error, true);
        LOG_ERROR("ThemeEditor", "Saving layouts/" + folder_ + "/" + session_.page() + " failed: " + error);
        return false;
    }
    hasOriginal_ = session_.hasOriginal();
    LOG_NOTICE("ThemeEditor", "Saved layouts/" + folder_ + "/" + session_.page() + " in place");
    std::string message = "Saved layouts/" + folder_ + "/" + session_.page() + ".";
    if (first && hasOriginal_) message += " The original is kept in " + session_.originalPath() + ".";
    // Anything the layout check didn't say about the page as it was opened
    std::vector<std::string> issues = checkLayoutXml(session_.text());
    std::vector<std::string> added;
    for (const std::string& issue : issues) {
        if (std::find(openIssues_.begin(), openIssues_.end(), issue) == openIssues_.end()) added.push_back(issue);
    }
    if (!added.empty()) {
        message += " The layout check found something new: " + added.front() +
                   (added.size() > 1 ? " (and " + std::to_string(added.size() - 1) + " more, in the log)" : "");
        for (const std::string& issue : added) LOG_WARNING("ThemeEditor", session_.page() + ": " + issue);
    }
    setStatus(message, !added.empty());
    std::string configured = Themes::configuredLayout(ctx_.root);
    if (Utils::toLower(configured.empty() ? originalLayout_ : configured) == Utils::toLower(folder_) && ctx_.requestReload) {
        ctx_.requestReload();
    }
    return true;
}

void LayoutEditorScreen::restoreOriginal()
{
    std::string error;
    if (!session_.restoreOriginal(error)) {
        setStatus("Not put back: " + error, true);
        return;
    }
    hasOriginal_ = session_.hasOriginal();
    openIssues_ = checkLayoutXml(session_.text());
    LOG_NOTICE("ThemeEditor", "Put back the original layouts/" + folder_ + "/" + session_.page());
    setStatus("Put back the original page. What was there is kept beside the original in manage/backups; "
              "Undo brings back what you were editing.", false);
    std::string configured = Themes::configuredLayout(ctx_.root);
    if (Utils::toLower(configured.empty() ? originalLayout_ : configured) == Utils::toLower(folder_) && ctx_.requestReload) {
        ctx_.requestReload();
    }
}

void LayoutEditorScreen::requestLeave(const std::string& toPage)
{
    if (session_.isOpen() && session_.dirty() && !leaveWithoutSaving_) {
        pendingPage_ = toPage;
        showClose_ = true;
        return;
    }
    if (toPage.empty()) back_ = true;
    else openNext_ = toPage;   // built and freed in update(), not while drawing
}

// ---- scripts --------------------------------------------------------------------

void LayoutEditorScreen::runScript()
{
    if (scriptAt_ >= script_.size()) return;
    if (scriptWait_ > 0) {
        scriptWait_--;
        return;
    }
    const EditorCommand& command = script_[scriptAt_++];
    std::string note;
    bool ok = runCommand(command, note);
    LOG_NOTICE("ThemeEditor", "script: " + command.name + (command.rest.empty() ? "" : " " + command.rest) +
               (ok ? " -> ok" : " -> failed") + (note.empty() ? "" : " (" + note + ")"));
}

bool LayoutEditorScreen::runCommand(const EditorCommand& command, std::string& note)
{
    CommandResult result = runLayoutCommand(session_, command, note);
    if (result != CommandResult::NotSession) return result == CommandResult::Ok;
    const std::string& n = command.name;
    const std::vector<std::string>& words = command.words;
    if (n == "wait") { scriptWait_ = words.empty() ? 0 : std::atoi(words[0].c_str()); return true; }
    if (n == "log") { note = command.rest; return true; }
    if (n == "page") {
        if (session_.dirty()) { note = "unsaved changes: save or undo first"; return false; }
        switchPage(command.rest);
        bool ok = session_.page() == command.rest;
        note = session_.page();
        return ok;
    }
    if (n == "save") { bool ok = save(); note = status_; return ok; }
    if (n == "restore") { restoreOriginal(); note = status_; return !statusIsError_; }
    if (n == "restoredialog") { showRestore_ = true; return true; }
    if (n == "deletedialog") { showDelete_ = session_.selected() > 0; return showDelete_; }
    if (n == "closedialogs") { showClose_ = showRestore_ = showDelete_ = showPalette_ = false; chooser_.close(); return true; }
    if (n == "palette") { showPalette_ = true; return true; }
    // A file copied in as a chosen one is: for a new element, or the selected element's src
    if (n == "addfile") {
        const PaletteEntry* entry = words.empty() ? nullptr : paletteEntry(words[0]);
        if (!entry || entry->file.empty()) return false;
        FileFor what;
        what.addElement = true;
        what.entry = entry->id;
        what.kind = entry->file;
        chooser_.close();
        bool ok = importChosen(command.tail(1), what);
        note = status_;
        return ok;
    }
    if (n == "srcfile") {
        const LayoutElement* e = session_.selectedElement();
        if (!e || session_.selected() == 0) return false;
        FileFor what;
        what.path = e->path;
        what.kind = srcKind(e->name);
        chooser_.close();
        bool ok = importChosen(command.rest, what);
        note = status_;
        return ok;
    }
    if (n == "browse") {
        // The editor's own chooser (never the system's dialog): for the selected
        // element's src, or (browse PALETTEID) for a new image or video
        if (!words.empty() && words[0] != "src") {
            const PaletteEntry* entry = paletteEntry(words[0]);
            if (!entry || entry->file.empty()) return false;
            addFromPalette(entry->id, "");
            return chooser_.isOpen();
        }
        const LayoutElement* e = session_.selectedElement();
        if (!e || session_.selected() == 0) return false;
        FileFor what;
        what.path = e->path;
        what.kind = srcKind(e->name);
        chooseFile(what);
        return true;
    }
    if (n == "browsedir") {
        chooser_.showFolder(fs::path(ctx_.root) / command.rest);
        return chooser_.isOpen();
    }
    if (n == "replay") {
        if (command.rest.empty()) return false;
        replay_ = command.rest.rfind("on", 0) == 0 ? engineEventName(command.rest) : command.rest;
        note = replay_;
        return true;
    }
    if (n == "endboxes") { showEnd_ = command.rest != "off"; return true; }
    if (n == "showanims") { scrollToAnimations_ = session_.selected() > 0; return scrollToAnimations_; }
    if (n == "openevent") {
        const int ev = session_.findInSelected(command.rest);
        if (ev < 0) return false;
        openEvent_ = session_.element(ev).path;
        return true;
    }
    if (n == "boxes") { showBoxes_ = command.rest != "off"; return true; }
    if (n == "preview") { previewName_ = command.rest; previewDirty_ = true; return true; }
    if (n == "rebuild") { builtText_.clear(); previewDirty_ = true; return true; }
    if (n == "close") { requestLeave(""); return true; }
    if (n == "discard") { leaveWithoutSaving_ = true; back_ = true; return true; }
    if (n == "status") { note = status_; return true; }
    return false;
}

// ---- ManagerUI hooks -------------------------------------------------------------

void LayoutEditorScreen::drawTargets()
{
    if (!page_ || !target_) return;
    SDL_Texture* previous = SDL_GetRenderTarget(ctx_.renderer);
    SDL_SetRenderTarget(ctx_.renderer, target_);
    SDL_SetRenderDrawColor(ctx_.renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx_.renderer);
    page_->draw();
    SDL_SetRenderTarget(ctx_.renderer, previous);
}

bool LayoutEditorScreen::escape()
{
    if (showClose_ || showRestore_ || showDelete_ || showPalette_ || chooser_.isOpen()) {
        showClose_ = showRestore_ = showDelete_ = showPalette_ = false;
        chooser_.close();
        pendingPage_.clear();
        return true;
    }
    if (session_.dragging()) {
        session_.cancelDrag();
        return true;
    }
    return false;
}

bool LayoutEditorScreen::askToLeave()
{
    if (session_.isOpen() && session_.dirty() && !leaveWithoutSaving_) {
        pendingPage_.clear();
        showClose_ = true;
        return true;
    }
    return false;
}

std::string LayoutEditorScreen::leaving()
{
    if (session_.isOpen() && session_.dirty() && !leaveWithoutSaving_) {
        return "This page has unsaved changes. Save them, or leave again to discard them.";
    }
    return "";
}

void LayoutEditorScreen::discard()
{
    leaveWithoutSaving_ = true;
}

void LayoutEditorScreen::setStatus(const std::string& message, bool isError)
{
    status_ = message;
    statusIsError_ = isError;
}

bool LayoutEditorScreen::takeBack()
{
    bool back = back_;
    back_ = false;
    return back;
}

// ---- drawing ----------------------------------------------------------------------

void LayoutEditorScreen::draw()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!navFlagsSaved_) {
        savedNavKeyboard_ = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        navFlagsSaved_ = true;
    }
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    const float s = ctx_.scale;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 6.0f * s));
    ImVec4 background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    background.w = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
    ImGui::Begin("Layout editor###layouteditor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (!session_.isOpen()) {
        ImGui::TextWrapped("The layout editor can't open layouts/%s.", folder_.c_str());
        ImGui::TextWrapped("%s", openError_.c_str());
        if (ImGui::Button("Close")) back_ = true;
        ImGui::End();
        return;
    }

    drawToolbar();
    if (scriptAt_ < script_.size()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.55f, 0.35f, 0.05f, 1.0f));
        ImGui::BeginChild("##scriptBanner", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 1.4f), ImGuiChildFlags_None);
        std::string file = fs::u8path(scriptPath_).filename().u8string();
        ImGui::Text("Running the editor script %s from -editorScript: %d of %d commands done",
                    file.c_str(), static_cast<int>(scriptAt_), static_cast<int>(script_.size()));
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    const float sideW = 290.0f * s;
    const float propsW = 340.0f * s;
    const float statusH = ImGui::GetTextLineHeightWithSpacing() * 4.2f;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("##pages", ImVec2(sideW, avail.y), ImGuiChildFlags_Borders);
    drawLeft();
    ImGui::EndChild();

    ImGui::SameLine();
    float canvasW = std::max(100.0f, avail.x - sideW - propsW - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::BeginChild("##canvasColumn", ImVec2(canvasW, avail.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginChild("##canvas", ImVec2(canvasW, avail.y - statusH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas();
    ImGui::EndChild();
    ImGui::BeginChild("##status", ImVec2(canvasW, 0), ImGuiChildFlags_None);
    if (!status_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Drag to move, drag a handle to resize, click again to pick the element underneath. Arrows nudge "
                           "(Shift: 10), Ctrl+Z / Ctrl+Y undo and redo, Ctrl+S saves. Dragging writes plain numbers: "
                           "x=\"center\" becomes a number once you drag it sideways.");
        ImGui::PopStyleColor();
    }
    for (const std::string& issue : engineIssues_) ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.4f, 1.0f), "Engine: %s", issue.c_str());
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##props", ImVec2(0, avail.y), ImGuiChildFlags_Borders);
    drawProperties();
    ImGui::EndChild();

    drawPopups();
    ImGui::End();
}

void LayoutEditorScreen::drawToolbar()
{
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Layout editor: %s / %s%s", folder_.c_str(), session_.page().c_str(), session_.dirty() ? " *" : "");
    ImGui::SameLine();
    ImGui::TextDisabled("(%d x %d, made by hand: edited in place)", session_.canvasW(), session_.canvasH());

    const char* labels[] = { "Undo", "Redo", "Save", "Put back the original...", "Close" };
    float width = 0.0f;
    for (const char* label : labels) width += ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
    ImGui::BeginDisabled(!session_.canUndo());
    if (ImGui::Button("Undo")) session_.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session_.canRedo());
    if (ImGui::Button("Redo")) session_.redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session_.dirty());
    if (ImGui::Button("Save")) job_ = Job::Save;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasOriginal_);
    if (ImGui::Button("Put back the original...")) showRestore_ = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(hasOriginal_ ? "The page as it was before the editor first saved it" :
                                         "Nothing to put back: the editor hasn't saved this page yet");
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) requestLeave("");

    ImGui::AlignTextToFramePadding();
    ImGui::Checkbox("Snap to grid", &session_.snap);
    ImGui::SameLine();
    ImGui::Checkbox("All boxes", &showBoxes_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Outline every element, not only the selected and the one under the mouse");
    ImGui::SameLine();
    ImGui::Checkbox("Where animations end", &showEnd_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A dashed box where an element's onEnter leaves it, when that isn't where it's placed "
                          "(Aeon Nox's top bar slides in from above the page)");
    }
    ImGui::SameLine();
    if (ImGui::Button("Play the start")) { builtText_.clear(); previewDirty_ = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Builds the preview again, so every onEnter plays");
    ImGui::SameLine();
    ImGui::TextDisabled("Preview with:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f * ctx_.scale);
    if (ImGui::BeginCombo("##previewCollection", previewName_.c_str())) {
        for (const std::string& name : collections_) {
            if (ImGui::Selectable(name.c_str(), name == previewName_)) {
                previewName_ = name;
                previewDirty_ = true;
            }
        }
        ImGui::EndCombo();
    }
}

void LayoutEditorScreen::drawLeft()
{
    ImGui::SeparatorText("Pages");
    ImGui::BeginChild("##pageList", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8.5f), ImGuiChildFlags_Borders);
    for (const std::string& page : pages_) {
        if (ImGui::Selectable(page.c_str(), page == session_.page()) && page != session_.page()) requestLeave(page);
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Elements");
    if (ImGui::Button("Add element...")) showPalette_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new image, video, text, game art, menu... after the selected element");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("This page's own elements, in file order, changed in place. New ones go after the selected "
                       "element, or at the end.");
    ImGui::PopStyleColor();
    ImGui::BeginChild("##elementList", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (int el : session_.listed()) {
        const LayoutElement& e = session_.element(el);
        ImGui::PushID(el);
        std::string label = std::string(static_cast<size_t>(std::max(0, e.depth - 1)) * 3, ' ') + session_.label(el);
        bool box = session_.hasBox(el) || el == 0;
        if (!box) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(label.c_str(), el == session_.selected())) session_.select(el);
        if (!box) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.path.empty() ? "<layout>" : e.path.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void LayoutEditorScreen::drawCanvas()
{
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 10.0f || size.y < 10.0f) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(24, 24, 28, 255));
    fit_ = canvasFit(size.x, size.y, session_.canvasW(), session_.canvasH());
    if (!fit_.ok) return;
    ImVec2 a(origin.x + fit_.x, origin.y + fit_.y);
    ImVec2 b(a.x + fit_.w, a.y + fit_.h);
    if (page_ && target_) {
        dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(target_)), a, b);
    }
    else {
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255));
        const char* text = previewDirty_ ? "Building the preview..." : "The preview couldn't be built (see below).";
        ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32(200, 200, 200, 255), text);
    }
    dl->AddRect(a, b, IM_COL32(90, 90, 100, 255));

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##canvasInput", size, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float sx = mouse.x - origin.x, sy = mouse.y - origin.y;
    const float grab = 6.0f * ctx_.scale;
    if (ImGui::IsItemActivated()) session_.pointerDown(fit_, sx, sy, grab);
    if (session_.dragging()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) session_.pointerMove(fit_, sx, sy);
        else session_.pointerUp();
    }

    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused() && !io.WantTextInput) {
        int step = io.KeyShift ? 10 : 1;
        int dx = 0, dy = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy += step;
        if (dx || dy) {
            session_.moveBy(dx, dy);
            sinceNudge_ = 0.0f;
            nudging_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && session_.selected() > 0) showDelete_ = true;
    }
    if (!io.WantTextInput && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) io.KeyShift ? session_.redo() : session_.undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) session_.redo();
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && session_.dirty()) job_ = Job::Save;
    }

    auto toStage = [&](float tx, float ty) {
        return ImVec2(origin.x + themeToCanvasX(fit_, tx), origin.y + themeToCanvasY(fit_, ty));
    };
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const int hot = hovered && !session_.dragging() ? session_.pick(canvasToThemeX(fit_, sx), canvasToThemeY(fit_, sy)) : -1;
    if (showBoxes_) {
        for (const ExtraBox& box : extra_) {
            dl->AddRect(toStage(static_cast<float>(box.rect.x), static_cast<float>(box.rect.y)),
                        toStage(static_cast<float>(box.rect.x + box.rect.w), static_cast<float>(box.rect.y + box.rect.h)),
                        IM_COL32(240, 160, 48, 60));
        }
    }
    for (int el : session_.listed()) {
        if (el == session_.selected() || !session_.hasBox(el) || session_.hidden(el)) continue;
        if (!showBoxes_ && el != hot) continue;
        DragRect r = session_.box(el);
        const bool isHot = el == hot;
        dl->AddRect(toStage(static_cast<float>(r.x), static_cast<float>(r.y)),
                    toStage(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h)),
                    colourFor(session_.element(el).name, isHot ? 230 : 110), 0.0f, 0, isHot ? 2.0f : 1.0f);
    }
    const int sel = session_.selected();
    // Where onEnter leaves an element: the selected one, and every other one with All boxes
    if (showEnd_ && !session_.dragging()) {
        for (int el : session_.listed()) {
            if (session_.hidden(el) || (el != sel && !showBoxes_)) continue;
            DragRect end;
            if (!session_.enterEndBox(el, end)) continue;
            const bool selected = el == sel;
            ImVec2 p1 = toStage(static_cast<float>(end.x), static_cast<float>(end.y));
            ImVec2 p2 = toStage(static_cast<float>(end.x + end.w), static_cast<float>(end.y + end.h));
            drawDashedRect(dl, p1, p2, colourFor(session_.element(el).name, selected ? 230 : 120), 6.0f * ctx_.scale);
            if (selected) {
                const char* text = "after onEnter";
                ImVec2 ts = ImGui::CalcTextSize(text);
                ImVec2 at(std::max(origin.x, p1.x), std::min(p2.y + 2.0f, origin.y + size.y - ts.y - 2.0f));
                dl->AddRectFilled(at, ImVec2(at.x + ts.x + 6.0f, at.y + ts.y + 2.0f), IM_COL32(0, 0, 0, 150));
                dl->AddText(ImVec2(at.x + 3.0f, at.y + 1.0f), IM_COL32(230, 230, 230, 255), text);
            }
        }
    }
    if (sel > 0 && session_.hasBox(sel) && !session_.hidden(sel)) {
        const std::string& name = session_.element(sel).name;
        const bool editable = session_.editableBox(sel);
        DragRect r = session_.dragMoved() ? session_.dragRect() : session_.box(sel);
        ImVec2 p1 = toStage(static_cast<float>(r.x), static_cast<float>(r.y));
        ImVec2 p2 = toStage(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h));
        dl->AddRectFilled(p1, p2, colourFor(name, 40));
        dl->AddRect(p1, p2, colourFor(name, 255), 0.0f, 0, 2.0f);
        if (editable) {
            for (int hnd = 0; hnd < 8; ++hnd) {
                float hx, hy;
                handlePoint(r, hnd, hx, hy);
                ImVec2 c = toStage(hx, hy);
                float hs = 4.0f * ctx_.scale;
                dl->AddRectFilled(ImVec2(c.x - hs, c.y - hs), ImVec2(c.x + hs, c.y + hs), IM_COL32(255, 255, 255, 255));
                dl->AddRect(ImVec2(c.x - hs, c.y - hs), ImVec2(c.x + hs, c.y + hs), IM_COL32(0, 0, 0, 255));
            }
        }
        std::string label = session_.label(sel);
        if (session_.dragMoved()) {
            label += "  " + std::to_string(r.x) + ", " + std::to_string(r.y) + "  " + std::to_string(r.w) + " x " + std::to_string(r.h);
        }
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        ImVec2 at(std::max(origin.x, p1.x), std::max(origin.y, p1.y - ts.y - 2.0f));
        dl->AddRectFilled(at, ImVec2(at.x + ts.x + 6.0f, at.y + ts.y + 2.0f), IM_COL32(0, 0, 0, 180));
        dl->AddText(ImVec2(at.x + 3.0f, at.y + 1.0f), IM_COL32(255, 255, 255, 255), label.c_str());
        if (hovered && !session_.dragging() && editable) {
            int handle = canvasHandleAt(fit_, r, sx, sy, grab);
            if (handle == CH_NW || handle == CH_SE) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            else if (handle == CH_NE || handle == CH_SW) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
            else if (handle == CH_N || handle == CH_S) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            else if (handle == CH_W || handle == CH_E) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            else if (handle == CH_BODY) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
    }
    if (hot >= 0 && hot != sel) ImGui::SetTooltip("%s", session_.label(hot).c_str());
    dl->PopClipRect();
}

void LayoutEditorScreen::drawDashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 colour, float dash)
{
    auto line = [&](ImVec2 p, ImVec2 q) {
        const float dx = q.x - p.x, dy = q.y - p.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 1.0f || dash < 1.0f) return;
        for (float t = 0.0f; t < length; t += dash * 2.0f) {
            const float e = std::min(length, t + dash);
            dl->AddLine(ImVec2(p.x + dx * t / length, p.y + dy * t / length), ImVec2(p.x + dx * e / length, p.y + dy * e / length),
                        colour, 1.5f);
        }
    };
    line(a, ImVec2(b.x, a.y));
    line(ImVec2(b.x, a.y), b);
    line(b, ImVec2(a.x, b.y));
    line(ImVec2(a.x, b.y), a);
}

void LayoutEditorScreen::drawAttribute(const LayoutElement& e, const std::string& name, const std::string& resolved)
{
    ImGui::PushID(name.c_str());
    const bool has = e.has(name);
    std::string value = e.value(name);
    // A menu item takes what it doesn't set from <itemDefaults>
    std::string hint = "(not set)";
    if (!has && e.name == "item" && e.parent > 0) {
        for (int child : session_.element(e.parent).children) {
            const LayoutElement& d = session_.element(child);
            if (d.name == "itemDefaults" && d.has(name)) hint = "(defaults: " + d.value(name) + ")";
        }
    }
    const float buttonW = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x * 0.46f));
    if (ImGui::InputTextWithHint("##value", hint.c_str(), &value)) {
        session_.setCoalesceKey("attr:" + name);
        session_.setAttribute(name, value);
    }
    if (ImGui::IsItemDeactivated()) session_.endCoalesce();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());
    if (!resolved.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("= %s", resolved.c_str());
    }
    if (name == "src" && (e.name == "image" || e.name == "video" || e.name == "sound")) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Choose...")) {
            FileFor what;
            what.path = e.path;
            what.kind = srcKind(e.name);
            chooseFile(what);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose the file; one from outside the theme is copied into it");
    }
    if (has) {
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - buttonW));
        if (ImGui::SmallButton("x")) session_.removeAttribute(name);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove %s", name.c_str());
    }
    ImGui::PopID();
}

void LayoutEditorScreen::drawProperties()
{
    const int sel = session_.selected();
    const LayoutElement* e = session_.selectedElement();
    if (!e) {
        ImGui::SeparatorText("Element");
        ImGui::TextWrapped("Click an element on the canvas or in the list to change it.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("This is a theme made by hand, so the layout editor changes its page file in place: only the "
                           "values you change are rewritten, and comments, spacing and everything else stay as they are. "
                           "The first save keeps the original in manage/backups; Put back the original restores it.");
        ImGui::PopStyleColor();
        return;
    }
    ImGui::SeparatorText(session_.label(sel).c_str());
    ImGui::TextDisabled("<%s>  %s  (line %d)", e->name.c_str(), e->path.empty() ? "the page" : e->path.c_str(),
                        lineOf(session_.text(), e->begin));
    const ElementGeom* g = session_.geometry(sel);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.45f, 1.0f));
    if (sel > 0 && !g) ImGui::TextWrapped("Not on the page as built (it draws nothing, or RetroFE skipped it): edit it here.");
    else if (g && g->readOnly && e->name == "menu") ImGui::TextWrapped("The box is around the menu's items: move the items, or the item defaults.");
    else if (g && g->readOnly) ImGui::TextWrapped("This menu makes its slots from these settings: change them here, not by dragging.");
    else if (g && g->approximate) ImGui::TextWrapped("Its size isn't known until it draws: the box is a guess.");
    std::vector<std::string> animated = session_.animatedProperties(sel);
    std::string moving;
    for (const std::string& p : animated) {
        if (p == "x" || p == "y" || p == "width" || p == "height" || p == "xOffset" || p == "yOffset") moving += (moving.empty() ? "" : ", ") + p;
    }
    if (!moving.empty()) ImGui::TextWrapped("Its animations change %s, so on screen it moves from where it's placed here.", moving.c_str());
    ImGui::PopStyleColor();

    const std::string path = e->path;
    auto resolvedFor = [this](const std::string& name) -> std::string {
        const ElementGeom* cur = session_.geometry(session_.selected());
        if (!cur) return "";
        if (name == "x") return number(cur->x);
        if (name == "y") return number(cur->y);
        // A guessed size (or a box drawn around a menu's slots) isn't what RetroFE resolved: not shown
        if (name == "width") return cur->approximate ? std::string() : number(cur->w);
        if (name == "height") return cur->approximate ? std::string() : number(cur->h);
        if (name == "xOffset") return number(cur->xOffset);
        if (name == "yOffset") return number(cur->yOffset);
        return "";
    };

    if (sel > 0) {
        ImGui::SeparatorText("Position, size and look");
        ImGui::PushID(path.c_str());
        for (const char* name : COMMON) {
            // Re-read each time: an edit replaces the parsed page
            if (const LayoutElement* now = session_.selectedElement()) drawAttribute(*now, name, resolvedFor(name));
        }
        ImGui::PopID();
        ImGui::TextDisabled("Layers: 0 is drawn first, 19 on top.");
        drawShows(sel);
    }

    ImGui::SeparatorText("All attributes");
    ImGui::PushID(path.c_str());
    ImGui::PushID("all");
    std::vector<std::string> names;
    if (const LayoutElement* now = session_.selectedElement()) {
        for (const LayoutAttr& a : now->attrs) names.push_back(a.name);
    }
    for (const std::string& name : names) {
        if (session_.selectedElement()) drawAttribute(*session_.selectedElement(), name, "");
    }
    ImGui::PopID();
    ImGui::PopID();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::InputTextWithHint("##newName", "name", &newName_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::InputTextWithHint("##newValue", "value", &newValue_);
    ImGui::SameLine();
    const std::string problem = newName_.empty() ? "" : LayoutSession::attributeNameProblem(newName_);
    ImGui::BeginDisabled(newName_.empty() || !problem.empty());
    if (ImGui::Button("Add")) {
        session_.endCoalesce();
        if (session_.setAttribute(newName_, newValue_)) { newName_.clear(); newValue_.clear(); }
    }
    ImGui::EndDisabled();
    if (!problem.empty()) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s", problem.c_str());
    e = session_.selectedElement();
    if (!e) return;

    if (sel > 0) drawAnimations(sel);
    if (!session_.selectedElement()) return;

    if (sel > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        if (session_.hasBox(sel)) {
            bool hidden = session_.hidden(sel);
            if (ImGui::Checkbox("Hide in the editor", &hidden)) session_.setHidden(sel, hidden);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only its box, here, to reach what's underneath; the page is unchanged");
        }
        if (ImGui::Button("Duplicate")) session_.duplicateSelected();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies the element, animations and all, right after itself");
        ImGui::SameLine();
        if (ImGui::Button("Delete...")) showDelete_ = true;
    }
}

// What a reloadable element shows (its type, or a video's imageType): the palette's choices
void LayoutEditorScreen::drawShows(int element)
{
    const LayoutElement& e = session_.element(element);
    const PaletteEntry* entry = paletteEntry(e.name);
    if (!entry || entry->typeAttr.empty() || entry->types.empty()) return;
    const std::string attr = entry->typeAttr;
    const std::string current = e.value(attr);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(attr == "imageType" ? "Art when there's no video:" : "Shows:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##shows", current.empty() ? "(not set)" : current.c_str())) {
        for (const std::string& t : entry->types) {
            if (ImGui::Selectable(t.c_str(), t == current) && t != current) {
                session_.endCoalesce();
                session_.setAttribute(attr, t);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its %s; any other value can be typed in All attributes", attr.c_str());
}

bool LayoutEditorScreen::drawAnimField(int element, const char* name, const char* hint, float width)
{
    const LayoutElement& e = session_.element(element);
    std::string value = e.value(name);
    const std::string key = "anim:" + e.path + ":" + name;
    ImGui::PushID(name);
    ImGui::SetNextItemWidth(width);
    bool changed = false;
    if (ImGui::InputTextWithHint("##v", hint, &value)) {
        session_.setCoalesceKey(key);
        changed = session_.setAttributeOf(element, name, value);
    }
    if (ImGui::IsItemDeactivated()) session_.endCoalesce();
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("%s", name);
    ImGui::PopID();
    return changed;
}

bool LayoutEditorScreen::drawAnimChoice(int element, const char* name, const std::vector<std::string>& choices, const char* none, float width)
{
    const std::string current = session_.element(element).value(name);
    bool changed = false;
    ImGui::PushID(name);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##c", current.empty() ? none : current.c_str())) {
        bool known = false;
        for (const std::string& c : choices) known = known || Utils::toLower(c) == Utils::toLower(current);
        if (!current.empty() && !known) ImGui::Selectable(current.c_str(), true);   // as the file has it
        for (const std::string& c : choices) {
            const bool same = Utils::toLower(c) == Utils::toLower(current);
            if (ImGui::Selectable(c.c_str(), same) && !same) {
                session_.endCoalesce();
                changed = session_.setAttributeOf(element, name, c);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name);
    ImGui::PopID();
    return changed;
}

void LayoutEditorScreen::drawAnimations(int element)
{
    ImGui::SeparatorText("Animations");
    if (scrollToAnimations_) {
        ImGui::SetScrollHereY(0.0f);
        scrollToAnimations_ = false;
    }
    const float s = ctx_.scale;
    const float buttonW = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    // An edit that adds or removes elements ends this frame's list (the indices after it moved)
    bool structural = false;
    std::vector<int> evs = session_.events(element);
    if (evs.empty()) ImGui::TextDisabled("None: it stays as it's placed.");
    for (size_t k = 0; k < evs.size() && !structural; ++k) {
        const int ev = evs[k];
        const LayoutElement& event = session_.element(ev);
        ImGui::PushID(event.path.c_str());
        std::string label = event.name;
        if (event.has("menuIndex")) label += " (menuIndex " + event.value("menuIndex") + ")";
        // Unfolded at first when there are only a couple
        ImGui::SetNextItemOpen(evs.size() <= 2, ImGuiCond_Once);
        if (event.path == openEvent_) {
            ImGui::SetNextItemOpen(true);
            openEvent_.clear();
        }
        const bool open = ImGui::TreeNodeEx("##event", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label.c_str());
        const std::string engineName = engineEventName(event.name);
        const float playW = ImGui::CalcTextSize("Play").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - playW - buttonW));
        if (ImGui::SmallButton("Play")) replay_ = engineName;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays %s on the preview (as RetroFE triggers \"%s\")", event.name.c_str(), engineName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) structural = session_.removeAnimation(ev);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove %s and its steps", event.name.c_str());
        if (open && !structural) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("menuIndex");
            ImGui::SameLine();
            drawAnimField(ev, "menuIndex", "any menu", 110.0f * s);
            std::vector<int> sets = session_.childrenNamed(ev, "set");
            for (size_t si = 0; si < sets.size() && !structural; ++si) {
                const int set = sets[si];
                ImGui::PushID(static_cast<int>(si));
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Step %d", static_cast<int>(si) + 1);
                ImGui::SameLine();
                drawAnimField(set, "duration", "seconds", 70.0f * s);
                ImGui::SameLine();
                ImGui::TextDisabled("s");
                ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - buttonW));
                if (ImGui::SmallButton("x")) structural = session_.removeAnimation(set);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this step");
                std::vector<int> animates = session_.childrenNamed(set, "animate");
                ImGui::Indent(12.0f * s);
                for (size_t ai = 0; ai < animates.size() && !structural; ++ai) {
                    const int a = animates[ai];
                    ImGui::PushID(static_cast<int>(ai));
                    const float half = (ImGui::GetContentRegionAvail().x - buttonW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                    drawAnimChoice(a, "type", tweenTypeNames(), "(type)", half);
                    ImGui::SameLine();
                    drawAnimChoice(a, "algorithm", tweenAlgorithmNames(), "linear", half);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) structural = session_.removeAnimation(a);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this change");
                    if (!structural) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("from");
                        ImGui::SameLine();
                        const float field = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("fromto").x -
                                             ImGui::GetStyle().ItemSpacing.x * 3.0f) * 0.5f;
                        drawAnimField(a, "from", "where it is", field);
                        ImGui::SameLine();
                        ImGui::TextDisabled("to");
                        ImGui::SameLine();
                        drawAnimField(a, "to", "value", field);
                    }
                    ImGui::PopID();
                }
                if (!structural && ImGui::SmallButton("+ change")) structural = session_.addAnimate(set);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Another change that plays with this step's");
                ImGui::Unindent(12.0f * s);
                ImGui::PopID();
            }
            if (!structural && ImGui::SmallButton("+ step")) structural = session_.addSet(ev);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A step that plays after the last one");
        }
        if (open) ImGui::TreePop();
        ImGui::PopID();
    }
    if (structural) return;

    // A new event
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.6f));
    if (ImGui::BeginCombo("##newEvent", newEvent_.c_str())) {
        for (const std::string& name : LayoutSession::eventNames()) {
            if (ImGui::Selectable(name.c_str(), name == newEvent_)) newEvent_ = name;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add event")) {
        if (session_.addEvent(newEvent_)) setStatus("Added " + newEvent_ + ": alpha from 0 to 1 in 0.2 seconds. Change it above.", false);
        return;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("When it plays: onEnter as the page shows, onMenuEnter... (RetroFE's events)");

    // Copy another element's
    std::string sourceLabel = "(copy animations from)";
    const int source = session_.doc().find(copySource_);
    if (!copySource_.empty() && source > 0) sourceLabel = session_.label(source);
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.6f));
    if (ImGui::BeginCombo("##copyFrom", sourceLabel.c_str())) {
        for (int other : session_.listed()) {
            if (other <= 0 || other == element || session_.events(other).empty()) continue;
            ImGui::PushID(other);
            const LayoutElement& o = session_.element(other);
            if (ImGui::Selectable(session_.label(other).c_str(), o.path == copySource_)) copySource_ = o.path;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %d events", o.path.c_str(), static_cast<int>(session_.events(other).size()));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(source <= 0 || source == element);
    if (ImGui::Button("Copy")) {
        const std::string from = sourceLabel;
        if (session_.copyAnimationsFrom(source)) setStatus("Its animations are now a copy of " + from + "'s (Undo brings back its own).", false);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Replaces this element's events with copies of the chosen element's");
}

void LayoutEditorScreen::drawPalette()
{
    ImGuiIO& io = ImGui::GetIO();
    syncPopup(PALETTE, showPalette_, paletteOpen_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f * ctx_.scale, 0), ImVec2(io.DisplaySize.x - 40.0f, io.DisplaySize.y - 40.0f));
    if (!ImGui::BeginPopupModal(PALETTE, &showPalette_, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (!showPalette_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const int sel = session_.selected();
    ImGui::TextWrapped("%s", sel > 0 ? ("Adds it after " + session_.label(sel) + ", in the middle of the page, on top.").c_str()
                                     : "Adds it at the end of the page, in the middle of it, on top.");
    ImGui::TextDisabled("It's written as RetroFE's own element; move it, size it and change it as any other.");
    ImGui::Separator();
    std::string add;
    if (ImGui::BeginTable("##palette", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 170.0f * ctx_.scale);
        ImGui::TableSetupColumn("add", ImGuiTableColumnFlags_WidthFixed);
        for (const PaletteEntry& entry : layoutPalette()) {
            ImGui::PushID(entry.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", entry.label.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("<%s>", entry.element.c_str());
            ImGui::TextDisabled("%s", entry.summary.c_str());
            ImGui::TableNextColumn();
            if (!entry.types.empty()) {
                std::string& chosen = paletteTypes_[entry.id];
                if (chosen.empty()) chosen = entry.types[0];
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##type", chosen.c_str())) {
                    for (const std::string& t : entry.types) {
                        if (ImGui::Selectable(t.c_str(), t == chosen)) chosen = t;
                    }
                    ImGui::EndCombo();
                }
            }
            else if (!entry.file.empty()) {
                ImGui::TextDisabled("asks for the %s", entry.file.c_str());
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("Add")) add = entry.id;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Cancel (Esc)")) {
        showPalette_ = false;
        ImGui::CloseCurrentPopup();
    }
    if (!add.empty()) {
        showPalette_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    if (!add.empty()) addFromPalette(add, paletteTypes_[add]);
}

void LayoutEditorScreen::syncPopup(const char* name, bool& show, bool& opened)
{
    if (opened && !ImGui::IsPopupOpen(name)) {
        show = false;
        opened = false;
    }
    if (show && !opened) {
        ImGui::OpenPopup(name);
        opened = true;
    }
}

void LayoutEditorScreen::drawPopups()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    drawPalette();
    std::string chosen;
    switch (chooser_.draw(ctx_.scale, fs::path(ctx_.root), fs::path(session_.themeDir()), chosen)) {
    case Themes::FileChooser::Result::Chosen:
        job_ = Job::Import;
        jobFor_ = chooserFor_;
        jobPath_ = chosen;
        break;
    case Themes::FileChooser::Result::SomewhereElse:
        // The system's dialog blocks: it opens from update(), after this frame
        job_ = Job::NativePick;
        jobFor_ = chooserFor_;
        break;
    case Themes::FileChooser::Result::Cancelled:
        // Not now: a new image or video goes in with its placeholder name
        if (chooserFor_.addElement && session_.addElement(chooserFor_.entry, chooserFor_.type)) {
            setStatus("Added " + session_.label(session_.selected()) + " with a placeholder file: choose one next to src.", true);
        }
        break;
    case Themes::FileChooser::Result::None:
        break;
    }

    syncPopup(UNSAVED, showClose_, closeOpen_);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(UNSAVED, &showClose_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!showClose_) ImGui::CloseCurrentPopup();
        ImGui::Text("Save the changes to %s?", session_.page().c_str());
        if (ImGui::Button("Save (Enter)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            job_ = Job::SaveAndLeave;
            showClose_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save (D)") || ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            showClose_ = false;
            ImGui::CloseCurrentPopup();
            if (pendingPage_.empty()) {
                leaveWithoutSaving_ = true;
                back_ = true;
            }
            else {
                openNext_ = pendingPage_;
                pendingPage_.clear();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)")) {
            showClose_ = false;
            pendingPage_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    syncPopup(RESTORE, showRestore_, restoreOpen_);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f * ctx_.scale, 0), ImVec2(720.0f * ctx_.scale, io.DisplaySize.y - 40.0f));
    if (ImGui::BeginPopupModal(RESTORE, &showRestore_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!showRestore_) ImGui::CloseCurrentPopup();
        ImGui::TextWrapped("Put back layouts/%s/%s as it was before the editor first saved it?", folder_.c_str(), session_.page().c_str());
        ImGui::TextWrapped("The page as it is now is kept in manage/backups as a numbered copy. Undo brings back what "
                           "you were editing, as unsaved changes.");
        if (ImGui::Button("Put it back (Enter)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            job_ = Job::Restore;
            showRestore_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)")) {
            showRestore_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    syncPopup(DELETE_ELEMENT, showDelete_, deleteOpen_);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(DELETE_ELEMENT, &showDelete_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!showDelete_) ImGui::CloseCurrentPopup();
        ImGui::Text("Delete %s from the page?", session_.label(session_.selected()).c_str());
        ImGui::TextDisabled("Undo brings it back until you leave the editor.");
        if (ImGui::Button("Delete (Enter)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            session_.deleteSelected();
            showDelete_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)")) {
            showDelete_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

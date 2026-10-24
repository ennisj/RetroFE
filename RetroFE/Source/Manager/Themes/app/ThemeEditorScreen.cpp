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
#include "ThemeEditorScreen.h"
#include "LayoutEditorScreen.h"
#include "ThemesApp.h"
#include "../ThemeProvider.h"
#include "../../FileDialog.h"
#include "../../Home.h"
#include "../../Setup.h"
#include "../../../Collection/CollectionInfo.h"
#include "../../../Collection/Item.h"
#include "../../../Database/Configuration.h"
#include "../../../Database/GlobalOpts.h"
#include "../../../Graphics/Page.h"
#include "../../../Graphics/PageBuilder.h"
#include "../../../Library/Indexer.h"
#include "../../../ThemeEditor/EditorSelection.h"
#include "../../../ThemeEditor/LayoutCheck.h"
#include "../../../SDL.h"
#include "../../../Utility/Log.h"
#include "../../../Utility/Utils.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

using namespace ThemeEditor;
namespace fs = std::filesystem;

namespace
{
    // How long the project has to be still before the preview is rebuilt, so
    // typing in a field doesn't rebuild on every key
    const float REBUILD_DELAY = 0.25f;
    // At most this many items of the preview collection go on the page
    const size_t PREVIEW_ITEMS = 60;
    // Files bigger than this are announced before they're copied (the copy blocks a frame)
    const uintmax_t LARGE_COPY = 16u * 1024u * 1024u;

    ImU32 colourOf(const std::string& hex, int alpha)
    {
        unsigned int value = 0x45A8E0;
        if (hex.size() == 6) value = static_cast<unsigned int>(std::strtoul(hex.c_str(), nullptr, 16));
        return IM_COL32((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, alpha);
    }

    void hexToFloats(const std::string& hex, float rgb[3])
    {
        unsigned int value = hex.size() == 6 ? static_cast<unsigned int>(std::strtoul(hex.c_str(), nullptr, 16)) : 0xFFFFFF;
        rgb[0] = ((value >> 16) & 0xFF) / 255.0f;
        rgb[1] = ((value >> 8) & 0xFF) / 255.0f;
        rgb[2] = (value & 0xFF) / 255.0f;
    }

    std::string floatsToHex(const float rgb[3])
    {
        char buffer[8];
        auto channel = [](float f) { return static_cast<int>(std::lround(std::max(0.0f, std::min(1.0f, f)) * 255.0f)); };
        std::snprintf(buffer, sizeof(buffer), "%02X%02X%02X", channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
        return buffer;
    }

    // Where a handle sits on a rect, in theme units
    void handlePoint(const DragRect& r, int handle, float& x, float& y)
    {
        const float x1 = static_cast<float>(r.x), y1 = static_cast<float>(r.y);
        const float x2 = static_cast<float>(r.x + r.w), y2 = static_cast<float>(r.y + r.h);
        const float hx[8] = { x1, (x1 + x2) * 0.5f, x2, x1, x2, x1, (x1 + x2) * 0.5f, x2 };
        const float hy[8] = { y1, y1, y1, (y1 + y2) * 0.5f, (y1 + y2) * 0.5f, y2, y2, y2 };
        x = hx[handle];
        y = hy[handle];
    }

    bool usesFontColour(const WidgetDef& def)
    {
        return def.fragment.find("FONTCOLOR") != std::string::npos;
    }

    // The chooser's (FileChooser.h)
    using Themes::insideFolder;
    std::string filterLabel(const std::string& filter) { return Themes::chooserFilterLabel(filter); }
    std::string patternFor(const std::string& filter) { return Themes::chooserPattern(filter); }

    const char* const SOUND_KINDS[4][2] = {
        { "load", "Start" }, { "unload", "Quit" }, { "highlight", "Highlight" }, { "select", "Select" }
    };
}

ThemeEditorScreen::ThemeEditorScreen(const Context& context, const std::string& folder)
    : ctx_(context)
    , folder_(folder)
{
    const std::string layouts = ctx_.layoutsDir();
    if (!session_.open(layouts, folder_, Themes::widgetLibrary(), openError_)) {
        LOG_ERROR("ThemeEditor", "Can't open layouts/" + folder_ + ": " + openError_);
    }
    else {
        LOG_INFO("ThemeEditor", "Editing layouts/" + folder_);
    }
    seenRevision_ = session_.revision();

    // The collection the preview shows: the one RetroFE shows, or Browse's for Home
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

    // The layout RetroFE uses now: what the setting goes back to, and what "Use this theme" compares with
    if (ctx_.config) ctx_.config->getProperty(OPTION_LAYOUT, originalLayout_);

    refreshTree();
    lastBrowseDir_ = fs::path(Utils::combinePath(ctx_.layoutsDir(), folder_));

    // retrofe -editorScript <file>: commands to run, one per frame, the
    // first time a theme is opened in this session only
    Themes::takeEditorScript(ctx_.config, script_, scriptPath_);

    // The on-screen keyboard for text fields, as for the forms
    std::string keyboard = "yes";
    if (ctx_.config) ctx_.config->getProperty("onScreenKeyboard", keyboard);
    pad_.setKeyboardEnabled(Forms::isTruthy(keyboard));
}

bool Themes::takeEditorScript(Configuration* config, std::vector<EditorCommand>& commands, std::string& path)
{
    static bool scriptUsed = false;
    std::string scriptPath;
    if (scriptUsed || !config || !config->getProperty("editorScript", scriptPath) || scriptPath.empty()) return false;
    scriptUsed = true;
    std::ifstream in(scriptPath, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    commands = parseEditorScript(text);
    path = scriptPath;
    if (!in) LOG_ERROR("ThemeEditor", "Can't read the editor script " + scriptPath);
    else LOG_NOTICE("ThemeEditor", "Running " + std::to_string(commands.size()) + " script commands from " + scriptPath);
    return true;
}

std::unique_ptr<Manager::ImGuiScreen> Themes::makeEditorScreen(const std::string& form, const ThemeEditorScreen::Context& context,
                                                               const std::map<std::string, std::string>& params)
{
    auto param = [&params](const char* key) {
        auto it = params.find(key);
        return it != params.end() ? it->second : std::string();
    };
    if (form == Themes::editorForm) return std::make_unique<ThemeEditorScreen>(context, param("id"));
    if (form == Themes::layoutEditorForm) return std::make_unique<LayoutEditorScreen>(context, param("id"), param("page"));
    return nullptr;
}

ThemeEditorScreen::~ThemeEditorScreen()
{
    // ManagerUI calls shutdown() from update() first; this only catches a missed call
    shutdown();
}

void ThemeEditorScreen::retire()
{
    // The next screen is built straight away, with the layout RetroFE was using
    if (layoutOverridden_ && ctx_.config) {
        ctx_.config->setProperty(OPTION_LAYOUT, originalLayout_);
        layoutOverridden_ = false;
    }
    pad_.detach();
    retired_ = true;
}

void ThemeEditorScreen::shutdown()
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

// ---- update: everything that builds or frees ------------------------------

void ThemeEditorScreen::update(float dt)
{
    if (retired_ || !session_.isOpen()) return;

    runScript();
    runJob();

    // The collections' menu tree follows the library index (rebuilt after Manage edits)
    if (ctx_.library && ctx_.library->status().generation != treeGeneration_) refreshTree();
    if (!fontsListed_) refreshFonts();

    // Arrow-key nudges with a pause between them are separate undo steps
    if (nudging_) {
        sinceNudge_ += dt;
        if (sinceNudge_ > 0.6f) {
            session_.endCoalesce();
            nudging_ = false;
        }
    }

    // Another page is shown (a click, a script, a template made or deleted)
    if (session_.pageIndex() != seenPage_) {
        seenPage_ = session_.pageIndex();
        previewDirty_ = true;
    }
    // Rebuild once the project has been still for a moment, and never mid-drag
    if (session_.revision() != seenRevision_) {
        seenRevision_ = session_.revision();
        sinceChange_ = 0.0f;
        previewDirty_ = true;
    }
    else {
        sinceChange_ += dt;
    }
    if (previewDirty_ && !session_.dragging() && (sinceChange_ >= REBUILD_DELAY || !page_)) {
        rebuildPreview();
    }

    if (page_) page_->update(dt);
}

void ThemeEditorScreen::rebuildPreview()
{
    previewDirty_ = false;
    builtRevision_ = session_.revision();
    GenResult gen = session_.previewXml(show_);
    genIssues_.clear();
    for (const std::string& e : gen.errors) genIssues_.push_back("Error: " + e);
    for (const std::string& w : gen.warnings) genIssues_.push_back(w);
    if (!gen.ok()) {
        // Keep showing the last page that built; the errors say why it's stale
        return;
    }
    if (page_ && gen.xml == builtXml_ && builtCollection_ == previewName_) {
        return;   // e.g. a widget's name changed: nothing on the page did
    }

    destroyPreview();
    builtXml_ = gen.xml;
    builtCollection_ = previewName_;
    engineIssues_.clear();
    // A page without components can't be drawn (Page::draw complains every
    // frame), and there's nothing to see: the canvas shows an empty page
    if (gen.xml.find(" layer=\"") == std::string::npos) return;
    // A widget file of the user's own can make a page PageBuilder would crash on
    std::string blocker = layoutBuildBlocker(gen.xml);
    if (!blocker.empty()) {
        engineIssues_.push_back("Not previewed: " + blocker);
        return;
    }
    if (!ensureTarget()) return;

    // Fonts and art resolve through the layout setting
    layoutOverridden_ = true;

    ctx_.config->setProperty(OPTION_LAYOUT, folder_);

    std::string themeDir = Utils::combinePath(ctx_.layoutsDir(), folder_);
    std::string pageDir = session_.pageFolder().empty() ? themeDir : (fs::path(themeDir) / session_.pageFolder()).string();

    // Loaded first: what loading a collection logs isn't about the page
    CollectionInfo* collection = previewCollection();

    std::vector<std::string> issues;
    Page* page = nullptr;
    // A splash page has no wheel by design: the engine's note about that isn't a problem
    const bool splash = ThemeProject::isSplashPage(session_.page().collection);
    {
        Logger::TapScope tap([&issues, splash](Logger::Zone, const std::string& component, const std::string& message) {
            if (splash && message.find("doesn't have any menus") != std::string::npos) return;
            if (Themes::pageBuildComponent(component)) issues.push_back("[" + component + "] " + message);
        });
        PageBuilder builder(folder_, "layout", *ctx_.config, ctx_.fontCache);
        builder.setLayoutXml(builtXml_.c_str(), pageDir);
        page = builder.buildPage();
        if (!page) {
            delete collection;
        }
        else {
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

void ThemeEditorScreen::destroyPreview()
{
    if (!page_) return;
    page_->stop();
    page_->deInitialize();
    delete page_;
    page_ = nullptr;
}

bool ThemeEditorScreen::ensureTarget()
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

CollectionInfo* ThemeEditorScreen::previewCollection()
{
    // Loaded once per collection; each page gets a copy of its first items,
    // because a page deletes its collections with it
    if (!master_ || masterName_ != previewName_) {
        master_.reset(ctx_.loadCollection && !previewName_.empty() ? ctx_.loadCollection(previewName_) : nullptr);
        masterName_ = previewName_;
    }
    auto* copy = new CollectionInfo(*ctx_.config, master_ ? master_->name : previewName_,
                                    master_ ? master_->listpath : "", "", master_ ? master_->metadataType : "", "");
    if (master_) {
        size_t count = std::min(master_->items.size(), PREVIEW_ITEMS);
        for (size_t i = 0; i < count; ++i) {
            copy->items.push_back(new Item(*master_->items[i]));
        }
        copy->menusort = master_->menusort;
        copy->sortType = master_->sortType;
    }
    copy->playlists["all"] = &copy->items;
    return copy;
}

// ---- jobs: work the draw pass asked for ------------------------------------

void ThemeEditorScreen::runJob()
{
    Job job = job_;
    job_ = Job::None;
    switch (job) {
    case Job::Save:
        save();
        break;
    case Job::SaveAndClose:
        if (save()) back_ = true;
        break;
    case Job::Use:
        use();
        break;
    case Job::NativePick:
        pickNative();
        break;
    case Job::Import: {
        // The copy blocks: a big file is announced first and copied on the next frame
        std::error_code ec;
        const uintmax_t size = fs::file_size(fs::path(jobPath_), ec);
        if (!ec && size > LARGE_COPY && !jobAnnounced_) {
            jobAnnounced_ = true;
            setStatus("Copying " + fs::path(jobPath_).filename().string() + " (" + std::to_string(size / (1024 * 1024)) +
                      " MB) into the theme...", false);
            job_ = Job::Import;
            break;
        }
        jobAnnounced_ = false;
        importChosen(jobPath_, jobRequest_);
        break;
    }
    case Job::None:
        break;
    }
}

bool ThemeEditorScreen::save()
{
    SaveInputs inputs;
    inputs.fontBytes = Themes::bundledFont();
    std::string error;
    if (!session_.save(inputs, report_, error)) {
        setStatus("Not saved: " + error, true);
        LOG_ERROR("ThemeEditor", "Saving layouts/" + folder_ + " failed: " + error);
        return false;
    }
    LOG_NOTICE("ThemeEditor", "Saved layouts/" + folder_ + ": " + std::to_string(report_.written.size()) + " files written, " +
               std::to_string(report_.removed.size()) + " pages removed, " + std::to_string(report_.warnings.size()) + " warnings");
    for (const std::string& removed : report_.removed) LOG_NOTICE("ThemeEditor", "Removed " + removed);
    for (const std::string& warning : report_.warnings) LOG_WARNING("ThemeEditor", warning);
    showReport_ = !report_.removed.empty() || !report_.warnings.empty();
    refreshHandMade();
    tplProblemKey_.clear();
    setStatus("Saved (" + std::to_string(report_.written.size()) + " files written).", false);

    // RetroFE shows the saved theme after the manager closes, if it's the one in use
    std::string configured = Themes::configuredLayout(ctx_.root);
    if (Utils::toLower(configured.empty() ? originalLayout_ : configured) == Utils::toLower(folder_) && ctx_.requestReload) {
        ctx_.requestReload();
    }
    return true;
}

void ThemeEditorScreen::use()
{
    if (session_.dirty()) {
        setStatus("Save the theme first.", true);
        return;
    }
    // What RetroFE would load: the saved main page
    std::string saved;
    {
        std::ifstream in(fs::path(session_.layoutsRoot()) / folder_ / "layout.xml", std::ios::binary);
        saved.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::string problem = Themes::mainPageProblem(saved);
    std::error_code ec;
    if (problem.empty() && !fs::is_regular_file(fs::path(session_.layoutsRoot()) / folder_ / "splash.xml", ec)) {
        problem = "This theme has no splash page, and RetroFE won't start without one. Save it to write one.";
    }
    if (!problem.empty()) {
        setStatus(problem, true);
        return;
    }
    std::string error;
    if (!Themes::setLayout(ctx_.root, folder_, error)) {
        setStatus(error, true);
        return;
    }
    LOG_NOTICE("ThemeEditor", "Set layout = " + folder_);
    originalLayout_ = folder_;   // what to leave the setting at when the editor closes
    if (ctx_.requestReload) ctx_.requestReload();
    setStatus("RetroFE switches to this theme when you close the manager.", false);
}

// The system's own file dialog: it blocks, so it runs here in update(), never while drawing
void ThemeEditorScreen::pickNative()
{
    if (!FileDialog::isSupported()) return;
    const FileRequest request = jobRequest_;
    std::string path;
    if (!FileDialog::pickFile(ctx_.window, request.title, filterLabel(request.filter), patternFor(request.filter), path)) return;
    // Copied next frame, as a file chosen in the editor's own chooser is
    job_ = Job::Import;
    jobPath_ = path;
}

// Copies a chosen file into the theme (fonts into fonts/, the rest into
// assets/; a file already in the theme stays where it is) and uses it
bool ThemeEditorScreen::importChosen(const std::string& path, const FileRequest& request)
{
    std::string rel, error;
    if (request.what == FileFor::Font) {
        // Only a real font goes into fonts/: RetroFE falls back to its default font for anything else
        const std::string problem = fontFileProblem(path);
        if (!problem.empty()) {
            setStatus("Not added: " + problem, true);
            return false;
        }
    }
    const std::string subfolder = request.what == FileFor::Font ? "fonts" : ASSETS_FOLDER;
    if (!importFile(session_.layoutsRoot(), folder_, path, subfolder, rel, error)) {
        setStatus("Not added: " + error, true);
        return false;
    }
    lastBrowseDir_ = fs::path(path).parent_path();
    bool used = false;
    switch (request.what) {
    case FileFor::WidgetVar:
        if (request.page >= 0 && request.page < session_.pageCount()) {
            if (session_.pageIndex() != request.page) session_.setPage(request.page);
            used = session_.selectId(request.widgetId) && (session_.setVar(request.key, rel) || session_.selectedWidget()->varValue(request.key, "") == rel);
        }
        break;
    case FileFor::Font:
        fontsListed_ = false;
        used = session_.setThemeFont(rel) || session_.project().font == rel;
        break;
    case FileFor::Sound:
        used = session_.setSound(request.key, rel) || session_.sound(request.key) == rel;
        break;
    case FileFor::SplashBackground:
        used = session_.setSplashBackground(rel) || session_.splashBackground() == rel;
        break;
    }
    if (!used) {
        setStatus("Added " + rel + " to the theme, but what it was for has gone.", true);
        return false;
    }
    setStatus("Using " + rel + ".", false);
    LOG_NOTICE("ThemeEditor", "Added " + path + " to layouts/" + folder_ + " as " + rel);
    return true;
}

ThemeEditorScreen::FileRequest ThemeEditorScreen::varRequest(const std::string& token) const
{
    FileRequest request;
    const WidgetInstance* w = session_.selectedWidget();
    const WidgetDef* def = session_.selectedDef();
    const WidgetVar* var = def ? def->var(token) : nullptr;
    if (!w || !var) return request;
    request.what = FileFor::WidgetVar;
    request.key = token;
    request.page = session_.pageIndex();
    request.widgetId = w->id;
    request.filter = var->filter;
    request.title = "Choose the " + var->name + " for " + w->name;
    return request;
}

void ThemeEditorScreen::openBrowser(const FileRequest& request)
{
    browserRequest_ = request;
    const fs::path root = fs::path(ctx_.root);
    std::error_code ec;
    Themes::FileChooser::Request shown;
    shown.filter = request.filter;
    shown.title = request.title;
    shown.note = request.note;
    shown.destination = request.what == FileFor::Font ? "the theme's fonts folder" : "the theme's assets folder";
    browser_.open(shown, fs::is_directory(lastBrowseDir_, ec) && insideFolder(lastBrowseDir_, root) ? lastBrowseDir_
                                                                                                  : fs::path(session_.layoutsRoot()) / folder_);
}

// After placing a widget: a file it needs that isn't in the theme yet (a
// Static Image's images/background.png) is asked for straight away
void ThemeEditorScreen::askForFiles()
{
    std::vector<std::string> missing = session_.missingFiles();
    if (missing.empty()) return;
    FileRequest request = varRequest(missing[0]);
    if (request.key.empty()) return;
    const std::string value = session_.selectedWidget()->varValue(request.key, session_.selectedDef()->var(request.key)->def);
    request.note = "Not now leaves it at " + (value.empty() ? std::string("nothing") : value) +
                   ", which isn't in the theme (saving warns about it).";
    openBrowser(request);
}

void ThemeEditorScreen::refreshFonts()
{
    fontsListed_ = true;
    fonts_ = themeFiles(session_.layoutsRoot(), folder_, "fonts", { ".ttf", ".otf" });
    fonts_.erase(std::remove(fonts_.begin(), fonts_.end(), std::string(BUNDLED_FONT)), fonts_.end());
}

void ThemeEditorScreen::refreshTree()
{
    tree_.clear();
    treeFromIndex_ = false;
    std::shared_ptr<const Library::Index> index = ctx_.library ? ctx_.library->index() : nullptr;
    if (ctx_.library) treeGeneration_ = ctx_.library->status().generation;
    if (!index) {
        for (const std::string& name : collections_) {
            TreeRow row;
            row.name = name;
            tree_.push_back(row);
        }
        refreshHandMade();
        return;
    }
    treeFromIndex_ = true;
    std::set<std::string> seen;
    auto rowFor = [&](const Library::CollectionRecord& record, int depth) {
        TreeRow row;
        row.name = record.name;
        row.depth = depth;
        row.menu = record.is(Library::KindMenu);
        if (!record.layout.empty() && Utils::toLower(record.layout) != Utils::toLower(folder_)) row.otherLayout = record.layout;
        return row;
    };
    for (const Library::MenuRow& menuRow : index->menuTree()) {
        if (menuRow.collection < 0 || menuRow.collection >= static_cast<int>(index->collections.size())) continue;
        const Library::CollectionRecord& record = index->collections[menuRow.collection];
        tree_.push_back(rowFor(record, menuRow.depth));
        seen.insert(record.name);
    }
    for (size_t i = 0; i + 1 < tree_.size(); ++i) tree_[i].children = tree_[i + 1].depth > tree_[i].depth;
    // Collections no menu lists still have pages
    bool header = false;
    for (const Library::CollectionRecord& record : index->collections) {
        if (record.reserved || seen.count(record.name)) continue;
        if (!header) {
            TreeRow none;
            none.depth = -1;   // "Not in a menu"
            tree_.push_back(none);
            header = true;
        }
        tree_.push_back(rowFor(record, 0));
    }
    refreshHandMade();
}

std::vector<std::string> ThemeEditorScreen::descendants(const std::string& collection) const
{
    std::vector<std::string> out;
    for (size_t i = 0; i < tree_.size(); ++i) {
        if (tree_[i].name != collection || tree_[i].depth < 0) continue;
        for (size_t j = i + 1; j < tree_.size() && tree_[j].depth > tree_[i].depth; ++j) {
            if (std::find(out.begin(), out.end(), tree_[j].name) == out.end() && tree_[j].name != collection) out.push_back(tree_[j].name);
        }
        break;
    }
    return out;
}

std::string ThemeEditorScreen::pageNameFor(const std::string& collection, bool menu) const
{
    PageVia via = PageVia::Default;
    const ThemePage& page = pageShownFor(session_.project(), collection, menu, &via);
    if (via == PageVia::Default) return "Default";
    if (via == PageVia::Menus) return page.displayName() + " (every menu)";
    return page.displayName();
}

void ThemeEditorScreen::openTemplateDialog(const std::string& collection, bool create, const std::string& basedOn)
{
    tplCollection_ = collection;
    tplFixedCollection_ = !collection.empty();
    tplBasedOn_ = basedOn;
    tplCascade_ = false;
    tplFilter_.clear();
    tplName_.clear();
    if (const ThemePage* seed = basedOn.empty() || basedOn == EditorSession::BLANK ? nullptr : session_.project().page(basedOn)) {
        tplName_ = seed->displayName() + " Copy";
    }
    else if (!collection.empty()) {
        tplName_ = collection;
    }
    tplChoice_ = 0;
    if (!create) {
        const ThemePage* cover = session_.project().coveredBy(collection);
        tplChoice_ = 1;
        const std::vector<std::string> templates = session_.templates();
        for (size_t i = 0; cover && i < templates.size(); ++i) {
            if (templates[i] == cover->collection) tplChoice_ = static_cast<int>(i) + 2;
        }
    }
    showTemplate_ = true;
}

// ---- the editor script (-editorScript) --------------------------------------

void ThemeEditorScreen::runScript()
{
    if (scriptAt_ >= script_.size()) return;
    if (scriptWait_ > 0) {
        scriptWait_--;
        return;
    }
    // The last command's change is in the preview first (the boxes are the
    // project's, but what a screenshot shows is the preview); capped
    const bool busy = session_.revision() != seenRevision_ || session_.pageIndex() != seenPage_ || previewDirty_ || job_ != Job::None;
    if (busy && scriptIdleFrames_ < 600) {
        scriptIdleFrames_++;
        return;
    }
    scriptIdleFrames_ = 0;
    const EditorCommand& command = script_[scriptAt_++];
    std::string note;
    bool ok = runCommand(command, note);
    LOG_NOTICE("ThemeEditor", "script: " + command.name + (command.rest.empty() ? "" : " " + command.rest) +
               (ok ? " -> ok" : " -> failed") + (note.empty() ? "" : " (" + note + ")"));
}

bool ThemeEditorScreen::runCommand(const EditorCommand& command, std::string& note)
{
    CommandResult result = runSessionCommand(session_, command, note);
    // Placing a widget asks for the files it needs, as the Add widget dialog does
    if (command.name == "add" && result == CommandResult::Ok) askForFiles();
    if (result != CommandResult::NotSession) return result == CommandResult::Ok;

    const std::string& n = command.name;
    const std::vector<std::string>& words = command.words;
    // Files, copied in as a chosen file is (the path is the rest of the line)
    auto importTo = [&](FileRequest request, const std::string& path) {
        browser_.close();
        bool ok = importChosen(path, request);
        note = status_;
        return ok;
    };
    if (n == "import") {
        if (words.empty()) return false;
        FileRequest request = varRequest(words[0]);
        return !request.key.empty() && importTo(request, command.tail(1));
    }
    if (n == "importfont") { FileRequest r; r.what = FileFor::Font; return importTo(r, command.rest); }
    if (n == "importsound") {
        if (words.empty()) return false;
        FileRequest r;
        r.what = FileFor::Sound;
        r.key = words[0];
        return importTo(r, command.tail(1));
    }
    if (n == "importsplash") { FileRequest r; r.what = FileFor::SplashBackground; return importTo(r, command.rest); }
    if (n == "browse") {
        const WidgetDef* def = session_.selectedDef();
        std::string token = words.empty() ? std::string() : words[0];
        for (size_t i = 0; token.empty() && def && i < def->vars.size(); ++i) {
            if (def->vars[i].type == "file") token = def->vars[i].token;
        }
        FileRequest request = varRequest(token);
        if (request.key.empty()) return false;
        openBrowser(request);
        return true;
    }
    if (n == "browsedir") {
        browser_.showFolder(fs::path(ctx_.root) / command.rest);
        return browser_.isOpen();
    }
    if (n == "tpldialog") { openTemplateDialog(command.rest, command.rest.empty(), ""); return true; }
    if (n == "open") { openSection_ = command.rest; return true; }
    if (n == "choose") {
        // Picks a collection in the Collections list, as a click does
        for (const TreeRow& row : tree_) {
            if (row.name != command.rest || row.depth < 0) continue;
            chosenCollection_ = row.name;
            previewName_ = row.name;
            const ThemePage* shown = &pageShownFor(session_.project(), row.name, row.menu);
            for (int i = 0; i < session_.pageCount(); ++i) {
                if (&session_.project().pages[i] == shown) session_.setPage(i);
            }
            previewDirty_ = true;
            note = "page " + std::to_string(session_.pageIndex());
            return true;
        }
        return false;
    }
    if (n == "tpldeletedialog") {
        if (!session_.project().page(command.rest) || command.rest.empty()) return false;
        delPrimary_ = command.rest;
        delReplacement_.clear();
        showDeleteTpl_ = true;
        return true;
    }
    if (n == "closedialogs") {
        showAdd_ = showReport_ = showTemplate_ = showDeleteTpl_ = false;
        browser_.close();
        return true;
    }
    if (n == "wait") { scriptWait_ = words.empty() ? 0 : std::atoi(words[0].c_str()); return true; }
    if (n == "log") { note = command.rest; return true; }
    if (n == "show") {
        if (words.size() < 2) return false;
        bool on = words[1] != "off";
        if (words[0] == "media") show_.includeMedia = on;
        else if (words[0] == "text") show_.includeText = on;
        else if (words[0] == "anims") show_.includeAnims = on;
        else return false;
        previewDirty_ = true;
        return true;
    }
    if (n == "preview") { previewName_ = command.rest; previewDirty_ = true; return true; }
    if (n == "rebuild") { builtXml_.clear(); previewDirty_ = true; return true; }
    if (n == "save") { return save(); }
    if (n == "use") { use(); return !statusIsError_; }
    if (n == "addDialog") { showAdd_ = true; return true; }
    if (n == "report") { showReport_ = true; return true; }
    if (n == "close") { requestClose(); return true; }
    if (n == "discard") { leaveWithoutSaving_ = true; back_ = true; return true; }
    return false;
}
// ---- ManagerUI hooks ----------------------------------------------------------

void ThemeEditorScreen::drawTargets()
{
    if (!page_ || !target_) return;
    SDL_Texture* previous = SDL_GetRenderTarget(ctx_.renderer);
    SDL_SetRenderTarget(ctx_.renderer, target_);
    SDL_SetRenderDrawColor(ctx_.renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx_.renderer);
    page_->draw();
    SDL_SetRenderTarget(ctx_.renderer, previous);
}

bool ThemeEditorScreen::escape()
{
    if (pad_.keyboardOpen()) {
        pad_.cancelKeyboard();
        return true;
    }
    if (showAdd_ || showClose_ || showReport_ || showTemplate_ || showDeleteTpl_ || browser_.isOpen()) {
        showAdd_ = showClose_ = showReport_ = showTemplate_ = showDeleteTpl_ = false;
        browser_.close();
        return true;
    }
    if (session_.dragging()) {
        session_.cancelDrag();
        return true;
    }
    // With the controller, Escape is Back: out of Move / Resize, out of a panel
    if (mode_ != ECM_PICK) {
        setMode(ECM_PICK);
        return true;
    }
    if (pad_.controllerActive() && pad_.region() != EF_CANVAS) {
        pad_.setRegion(EF_CANVAS);
        return true;
    }
    // Several selected: only the last one picked stays
    if (session_.selectionCount() > 1) {
        session_.select(session_.selected());
        return true;
    }
    return false;
}

// ---- RetroFE's mapped controls ------------------------------------------------------

bool ThemeEditorScreen::wantsControls() const
{
    return !retired_;
}

void ThemeEditorScreen::controlsHeld(const ControlsHeld& held)
{
    pad_.held(held);
    // Select let go in Pick with nothing pressed while it was held: it acts now (Move, or Add)
    if (heldSelect_.release(held[static_cast<size_t>(Control::Select)]) && !retired_ && session_.isOpen() &&
        !dialogOpen() && pad_.region() == EF_CANVAS && mode_ == ECM_PICK && scriptAt_ >= script_.size() &&
        !session_.dragging()) {
        canvasControl(EP_SELECT, true);
    }
}

void ThemeEditorScreen::beforeFrame()
{
    pad_.beforeFrame();
}

bool ThemeEditorScreen::dialogOpen() const
{
    return showAdd_ || showClose_ || showReport_ || showTemplate_ || showDeleteTpl_ || browser_.isOpen() || pad_.modalOpen();
}

void ThemeEditorScreen::control(Control control, bool repeat)
{
    if (retired_) return;
    // A theme that didn't open: Select (its Close button) or Back leaves
    if (!session_.isOpen()) {
        if (control == Control::Back || control == Control::Select) back_ = true;
        return;
    }
    // A script is driving: nobody else does
    if (scriptAt_ < script_.size()) return;
    repeats_ = repeat ? repeats_ + 1 : 0;
    if (pad_.takesControl(control)) return;
    const int press = static_cast<int>(control);
    if (dialogOpen()) {
        if (press >= EP_PAGE_UP) return;
        if (pad_.press(press, EL_DIALOG) == ER_CLOSE_DIALOG) escape();
        return;
    }
    if (session_.dragging()) return;   // the mouse is busy
    if (press == EP_PAGE_UP || press == EP_PAGE_DOWN) {
        pad_.setRegion(regionCycle(pad_.region(), press == EP_PAGE_UP ? -1 : 1));
        return;
    }
    if (pad_.region() == EF_CANVAS && !pad_.listOpen()) {
        canvasControl(press);
        return;
    }
    if (press >= EP_LETTER_UP) return;
    if (pad_.press(press, EL_PANEL) == ER_LEAVE_REGION) pad_.setRegion(EF_CANVAS);
}

void ThemeEditorScreen::setMode(EdCanvasMode mode)
{
    // A run of nudges is one undo step, from entering Move or Resize to leaving it
    if (mode_ != ECM_PICK || mode != ECM_PICK) session_.endCoalesce();
    mode_ = mode;
    if (mode != ECM_PICK) session_.setCoalesceKey("controller");
}

void ThemeEditorScreen::heldControl(int press)
{
    const auto& widgets = session_.page().widgets;
    switch (heldSelectPress(press)) {
    case EHA_ADD_DIR: {
        int dx = 0, dy = 0;
        pressDirection(press, dx, dy);
        std::vector<DragRect> rects;
        std::vector<int> index;
        int cur = -1;
        for (int i = 0; i < static_cast<int>(widgets.size()); ++i) {
            if (widgets[i].canvasHide) continue;
            DragRect r;
            canvasHitRect(widgets[i], r.x, r.y, r.w, r.h);
            if (i == session_.selected()) cur = static_cast<int>(rects.size());
            rects.push_back(r);
            index.push_back(i);
        }
        const int next = pickStep(rects, cur, dx, dy, session_.project().canvasW(), session_.project().canvasH());
        if (next < 0) setStatus("Nothing that way to add.", false);
        else session_.addSelected(index[next]);
        break;
    }
    case EHA_ADD_PREV:
    case EHA_ADD_NEXT: {
        const int next = listStep(session_.selected(), static_cast<int>(widgets.size()), press == EP_LETTER_UP ? -1 : 1);
        if (next >= 0) session_.addSelected(next);
        break;
    }
    case EHA_DROP:
        session_.dropPrimary();
        break;
    case EHA_NONE:
        break;
    }
    if (session_.selectionCount() > 1) {
        setStatus(std::to_string(session_.selectionCount()) + " widgets selected. Let go of Select, then Select moves them all; "
                  "the properties panel lines them up.", false);
    }
}

void ThemeEditorScreen::canvasControl(int press, bool released)
{
    // Select in Pick acts when it's let go: held, the directions add to the selection
    if (mode_ == ECM_PICK && press == EP_SELECT && !released) {
        heldSelect_.press();
        return;
    }
    if (heldSelect_.armed && heldSelectPress(press) != EHA_NONE) {
        if (heldSelect_.combine(repeats_ > 0)) heldControl(press);   // one per press
        return;
    }
    const ThemeProject& p = session_.project();
    const WidgetInstance* sel = session_.selectedWidget();
    const bool multi = session_.selectionCount() > 1;
    const EdCanvasDecision d = canvasPress(mode_, press, sel != nullptr, sel != nullptr, multi);
    if (d.mode != mode_ && d.action != ECA_MODE) setMode(d.mode);   // the selection went away mid-move
    int dx = 0, dy = 0;
    pressDirection(press, dx, dy);
    // Shift + an arrow that's a mapped direction still nudges by 10, as it did before the controller
    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
    if (shift && d.action == ECA_PICK && sel) {
        session_.moveBy(dx * 10, dy * 10);
        sinceNudge_ = 0.0f;   // a pause ends this run of nudges as one undo step (update())
        nudging_ = true;
        return;
    }
    switch (d.action) {
    case ECA_PICK: {
        // The visible widgets' boxes; hidden ones can't be picked, as with the mouse
        std::vector<DragRect> rects;
        std::vector<int> index;
        int cur = -1;
        const auto& widgets = session_.page().widgets;
        for (int i = 0; i < static_cast<int>(widgets.size()); ++i) {
            if (widgets[i].canvasHide) continue;
            DragRect r;
            canvasHitRect(widgets[i], r.x, r.y, r.w, r.h);
            if (i == session_.selected()) cur = static_cast<int>(rects.size());
            rects.push_back(r);
            index.push_back(i);
        }
        const int next = pickStep(rects, cur, dx, dy, p.canvasW(), p.canvasH());
        if (next < 0) pad_.setRegion(regionStep(EF_CANVAS, dx, dy));   // nothing that way: the panel there
        else session_.select(index[next]);
        break;
    }
    case ECA_MOVE:
    case ECA_RESIZE: {
        const int step = nudgeStep(session_.snap ? CANVAS_SNAP_GRID : 1, shift ? 12 : repeats_);
        session_.setCoalesceKey("controller");
        if (d.action == ECA_MOVE) {
            // The selection as one box (one widget: its own), kept on the canvas; as with the
            // mouse, moving keeps a warped widget's shape
            const DragRect u = unionRect(session_.selectionBoxes());
            const DragRect n = canvasNudgeMove(u, dx, dy, step, p.canvasW(), p.canvasH());
            session_.shiftSelection(n.x - u.x, n.y - u.y);
        }
        else {
            // From the handle Letter up / down chose; resizing makes a warped widget a plain box
            DragRect r;
            canvasHitRect(*sel, r.x, r.y, r.w, r.h);
            const WidgetDef* def = session_.selectedDef();
            const DragRect n = canvasNudgeResizeFrom(r, resizeHandle_, dx, dy, step, def ? def->minW : 8, def ? def->minH : 8,
                                                     p.canvasW(), p.canvasH());
            session_.setRect(n.x, n.y, n.w, n.h);
        }
        break;
    }
    case ECA_MODE:
        if (d.mode == ECM_RESIZE) resizeHandle_ = CH_SE;   // Resize starts from the bottom-right corner
        setMode(d.mode);
        break;
    case ECA_HANDLE_PREV:
    case ECA_HANDLE_NEXT:
        resizeHandle_ = resizeHandleStep(resizeHandle_, d.action == ECA_HANDLE_PREV ? -1 : 1);
        setStatus(std::string("Resizing from the ") + resizeHandleLabel(resizeHandle_) + ".", false);
        break;
    case ECA_COLLAPSE:
        session_.select(session_.selected());
        setStatus("Only " + (sel ? sel->name : std::string("one")) + " is selected now.", false);
        break;
    case ECA_ADD:
        showAdd_ = true;
        addFilter_.clear();
        break;
    case ECA_LEAVE_EDITOR:
        requestClose();
        break;
    case ECA_PREV:
    case ECA_NEXT: {
        const int next = listStep(session_.selected(), static_cast<int>(session_.page().widgets.size()), d.action == ECA_PREV ? -1 : 1);
        if (next >= 0) session_.select(next);
        break;
    }
    case ECA_CANT_MOVE:
    case ECA_NONE:
        break;
    }
}

std::string ThemeEditorScreen::controllerHints() const
{
    if (pad_.keyboardOpen()) return "";
    if (dialogOpen()) return "Arrows: move   Select: choose   Back: close the dialog";
    switch (pad_.region()) {
    case EF_CANVAS:
        if (mode_ == ECM_MOVE && session_.selectionCount() > 1) {
            return "MOVE: arrows move the " + std::to_string(session_.selectionCount()) + " widgets (hold to speed up)   Select or Back: done";
        }
        if (mode_ == ECM_MOVE) return "MOVE: arrows move the widget (hold to speed up)   Select: resize instead   Back: done";
        if (mode_ == ECM_RESIZE) {
            return std::string("RESIZE: arrows move its ") + resizeHandleLabel(resizeHandle_) +
                   "   Letter up/down: another corner or edge   Select or Back: done";
        }
        if (heldSelect_.armed) return "Holding Select: arrows add the widget that way, Letter up/down the previous / next, Back drops the last one";
        return std::string("PICK: arrows choose a widget (past the last one: the panel that way)   ") +
               (session_.selectedWidget() ? "Select: move it" : "Select: add a widget") +
               "   Hold Select + arrows: select several   Letter up/down: previous / next   Page up/down: regions   " +
               (session_.selectionCount() > 1 ? "Back: keep only the last one" : "Back: leave the editor");
    case EF_TOOLBAR:
        return "Toolbar: arrows move   Select: press   Down: the canvas   Back: the canvas";
    default:
        return "Up / Down: rows   Left / Right: change a value   Select: press, or type with the on-screen keyboard   "
               "Back: the canvas";
    }
}

bool ThemeEditorScreen::askToLeave()
{
    // Unsaved changes: Save / Don't save / Cancel, rather than "leave again to discard"
    if (session_.isOpen() && session_.dirty() && !leaveWithoutSaving_) {
        showClose_ = true;
        return true;
    }
    return false;
}

std::string ThemeEditorScreen::leaving()
{
    if (session_.isOpen() && session_.dirty() && !leaveWithoutSaving_) {
        return "This theme has unsaved changes. Save them, or leave again to discard them.";
    }
    return "";
}

void ThemeEditorScreen::discard()
{
    leaveWithoutSaving_ = true;
}

void ThemeEditorScreen::setStatus(const std::string& message, bool isError)
{
    status_ = message;
    statusIsError_ = isError;
}

bool ThemeEditorScreen::takeBack()
{
    bool back = back_;
    back_ = false;
    return back;
}

void ThemeEditorScreen::requestClose()
{
    if (session_.isOpen() && session_.dirty() && !leaveWithoutSaving_) {
        showClose_ = true;
    }
    else {
        back_ = true;
    }
}

// ---- drawing ------------------------------------------------------------------

void ThemeEditorScreen::draw()
{
    ImGuiIO& io = ImGui::GetIO();
    // ImGui's keyboard navigation is off while the editor is shown (the arrow
    // keys nudge widgets; Tab still moves between fields), its gamepad
    // navigation takes RetroFE's controls; retire() puts things back
    pad_.attach();
    pad_.beginFrame();
    int edgeX = 0, edgeY = 0;
    if (pad_.takeEdge(edgeX, edgeY)) pad_.setRegion(regionStep(pad_.region(), edgeX, edgeY));
    if (pad_.region() != EF_CANVAS && mode_ != ECM_PICK) setMode(ECM_PICK);
    const float s = ctx_.scale;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 6.0f * s));
    ImVec4 background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    background.w = 1.0f;   // the page behind doesn't show through
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
    ImGui::Begin("Theme editor###themeeditor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (!session_.isOpen()) {
        ImGui::TextWrapped("The theme editor can't open layouts/%s.", folder_.c_str());
        ImGui::TextWrapped("%s", openError_.c_str());
        if (ImGui::Button("Close")) back_ = true;
        ImGui::End();
        return;
    }

    pad_.beginRegion(EF_TOOLBAR, "##toolbar", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
    drawToolbar();
    ImGui::EndChild();

    // Nobody should wonder why the theme is changing by itself
    if (scriptAt_ < script_.size()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.55f, 0.35f, 0.05f, 1.0f));
        ImGui::BeginChild("##scriptBanner", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 1.4f), ImGuiChildFlags_None);
        std::string file = fs::u8path(scriptPath_).filename().u8string();
        ImGui::Text("Running the editor script %s from -editorScript: %d of %d commands done",
                    file.c_str(), static_cast<int>(scriptAt_), static_cast<int>(script_.size()));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", scriptPath_.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    const float sideW = 340.0f * s;
    const float propsW = 330.0f * s;
    const float statusH = ImGui::GetTextLineHeightWithSpacing() * 3.2f;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    pad_.beginRegion(EF_THEME, "##theme", ImVec2(sideW, avail.y), ImGuiChildFlags_Borders);
    drawThemePanel();
    ImGui::EndChild();

    ImGui::SameLine();
    float canvasW = std::max(100.0f, avail.x - sideW - propsW - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::BeginChild("##canvasColumn", ImVec2(canvasW, avail.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    pad_.beginRegion(EF_CANVAS, "##canvas", ImVec2(canvasW, avail.y - statusH), ImGuiChildFlags_Borders,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas();
    ImGui::EndChild();

    // Status, then what the generator and the engine said about the page
    ImGui::BeginChild("##status", ImVec2(canvasW, 0), ImGuiChildFlags_None);
    if (pad_.controllerActive() && !controllerHints().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.77f, 0.16f, 1.0f));
        ImGui::TextWrapped("%s", controllerHints().c_str());
        ImGui::PopStyleColor();
    }
    if (!status_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
    else if (!pad_.controllerActive()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Drag to move, drag a handle to resize, click again to pick the widget underneath. "
                           "Arrows nudge (Shift: 10), Delete removes, Ctrl+Z / Ctrl+Y undo and redo, Ctrl+S saves.");
        ImGui::PopStyleColor();
    }
    for (const std::string& issue : genIssues_) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", issue.c_str());
    for (const std::string& issue : engineIssues_) ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.4f, 1.0f), "Engine: %s", issue.c_str());
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();
    pad_.beginRegion(EF_PROPS, "##props", ImVec2(0, avail.y), ImGuiChildFlags_Borders);
    drawProperties();
    ImGui::EndChild();

    drawPopups();
    pad_.endFrame(ctx_.scale);
    ImGui::End();
}

void ThemeEditorScreen::drawToolbar()
{
    const ThemeProject& p = session_.project();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Theme editor: %s%s", p.name.c_str(), session_.dirty() ? " *" : "");
    ImGui::SameLine();
    ImGui::TextDisabled("(layouts/%s, %d x %d)", folder_.c_str(), p.canvasW(), p.canvasH());

    // Right-aligned actions
    const char* labels[] = { "Add widget", "Undo", "Redo", "Save", "Use this theme", "Close" };
    float width = 0.0f;
    for (const char* label : labels) width += ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
    if (ImGui::Button("Add widget")) { showAdd_ = true; addFilter_.clear(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!session_.canUndo());
    if (ImGui::Button("Undo")) session_.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session_.canRedo());
    if (ImGui::Button("Redo")) session_.redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save")) job_ = Job::Save;
    ImGui::SameLine();
    bool inUse = Utils::toLower(originalLayout_) == Utils::toLower(folder_);
    ImGui::BeginDisabled(session_.dirty() || inUse);
    if (ImGui::Button("Use this theme")) job_ = Job::Use;
    ImGui::EndDisabled();
    if (session_.dirty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Save the theme first");
    ImGui::SameLine();
    if (ImGui::Button("Close")) requestClose();

    // The preview's options
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Show:");
    ImGui::SameLine();
    if (ImGui::Checkbox("Media", &show_.includeMedia)) previewDirty_ = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Text", &show_.includeText)) previewDirty_ = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Animations", &show_.includeAnims)) previewDirty_ = true;
    ImGui::SameLine();
    ImGui::Checkbox("Snap to grid", &session_.snap);
    ImGui::SameLine();
    ImGui::Checkbox("Guides", &guidesOn_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("While dragging with the mouse, edges and centres line up with the other widgets' and the page's "
                          "centre, with a line to show it. Hold Alt to drag without them.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Preview with:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f * ctx_.scale);
    const bool open = ImGui::BeginCombo("##previewCollection", previewName_.c_str());
    // Left / Right: the previous / next collection
    if (const int step = open ? 0 : pad_.choice("Preview with")) {
        auto at = std::find(collections_.begin(), collections_.end(), previewName_);
        const int next = choiceStep(at == collections_.end() ? -1 : static_cast<int>(at - collections_.begin()),
                                    static_cast<int>(collections_.size()), step);
        if (next >= 0) {
            previewName_ = collections_[next];
            previewDirty_ = true;
        }
    }
    if (open) {
        for (const std::string& name : collections_) {
            if (ImGui::Selectable(name.c_str(), name == previewName_)) {
                previewName_ = name;
                previewDirty_ = true;
            }
            if (name == previewName_) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void ThemeEditorScreen::drawThemePanel()
{
    const ThemeProject& p = session_.project();
    ImGui::SeparatorText("Theme");
    std::string name = p.name;
    ImGui::SetNextItemWidth(-1.0f);
    const bool typed = ImGui::InputText("##themeName", &name);
    const bool keyboard = pad_.text("Theme name", name);
    if (typed || keyboard) {
        session_.setCoalesceKey("themeName");
        session_.setThemeName(name);
    }
    if (keyboard || ImGui::IsItemDeactivated()) session_.endCoalesce();
    int vertical = p.vertical ? 1 : 0;
    if (ImGui::RadioButton("Landscape", &vertical, 0)) session_.setVertical(false);
    ImGui::SameLine();
    if (ImGui::RadioButton("Portrait", &vertical, 1)) session_.setVertical(true);

    // A script's "open" command unfolds a section, so it shows in a screenshot
    auto unfold = [this](const char* section) {
        if (openSection_ == section || openSection_ == std::string("-") + section) {
            ImGui::SetNextItemOpen(openSection_[0] != '-');
            openSection_.clear();
        }
    };
    unfold("fonts");
    const bool fonts = ImGui::CollapsingHeader("Font and sounds");
    pad_.node();
    if (fonts) drawThemeProperties();
    unfold("splash");
    const bool splash = ImGui::CollapsingHeader("Splash");
    pad_.node();
    if (splash) drawSplash();
    drawPages();
    if (openSection_ == "collections") ImGui::SetScrollHereY(0.0f);   // and scrolled to
    unfold("collections");
    const bool collections = ImGui::CollapsingHeader("Collections", ImGuiTreeNodeFlags_DefaultOpen);
    pad_.node();
    if (collections) drawCollections();

    ImGui::SeparatorText("Widgets on this page");
    const auto& widgets = session_.page().widgets;
    if (widgets.empty()) ImGui::TextDisabled("None yet: choose Add widget.");
    else if (widgets.size() > 1) ImGui::TextDisabled("Ctrl-click or Shift-click for several.");
    // A widget picked elsewhere (the canvas, the controller) is scrolled to here
    bool scrollTo = session_.selected() != listPrimary_ && !listClicked_;
    listClicked_ = false;
    listPrimary_ = session_.selected();
    for (int i = 0; i < static_cast<int>(widgets.size()); ++i) {
        const WidgetInstance& w = widgets[i];
        const WidgetDef* def = session_.library().find(w.def);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float box = ImGui::GetTextLineHeight() * 0.7f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + box * 0.25f), ImVec2(pos.x + box, pos.y + box * 1.25f),
                                                  colourOf(def ? def->color : "", 255));
        ImGui::Dummy(ImVec2(box, box));
        ImGui::SameLine();
        if (w.canvasHide) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        std::string label = w.name + "##w" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), session_.isSelected(i))) {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl) session_.toggleSelected(i);
            else if (io.KeyShift && session_.selected() >= 0) {
                std::vector<int> order;
                for (int k = 0; k < static_cast<int>(widgets.size()); ++k) order.push_back(k);
                session_.setSelection(selectionRange(order, session_.selected(), i));
            }
            else session_.select(i);
            listClicked_ = true;
        }
        if (scrollTo && i == session_.selected()) {
            if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
            scrollTo = false;
        }
        if (w.canvasHide) ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("L19").x);
        ImGui::TextDisabled("L%d", w.layer);
    }
}

void ThemeEditorScreen::drawFileRow(const char* label, const std::string& value, const FileRequest& request,
                                    const std::function<void()>& clear)
{
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    const float valueX = ImGui::GetFontSize() * 6.0f;
    const float buttons = ImGui::CalcTextSize("Choose...").x + ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 4.0f +
                          ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const float buttonsX = std::max(valueX + ImGui::GetFontSize() * 3.0f, ImGui::GetContentRegionMax().x - buttons);
    // The file's name beside the label when it fits, else on a line of its own under it
    std::string shown = value.empty() ? std::string() : fs::path(value).filename().string();
    if (!value.empty() && !session_.themeHasFile(value)) shown = "(missing) " + shown;
    const float room = buttonsX - valueX - ImGui::GetStyle().ItemSpacing.x;
    const bool below = !shown.empty() && ImGui::CalcTextSize(shown.c_str()).x > room;
    ImGui::SameLine(valueX);
    if (value.empty()) ImGui::TextDisabled("none");
    else if (!below) ImGui::TextUnformatted(shown.c_str());
    if (!value.empty() && !below && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", value.c_str());
    ImGui::SameLine(buttonsX);
    if (ImGui::SmallButton("Choose...")) openBrowser(request);
    ImGui::SameLine();
    ImGui::BeginDisabled(value.empty());
    if (ImGui::SmallButton("x")) clear();
    ImGui::EndDisabled();
    if (!value.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("Take it off (the file stays in the theme folder)");
    if (below) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFontSize());
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
        ImGui::TextUnformatted(shown.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", value.c_str());
    }
    ImGui::PopID();
}

void ThemeEditorScreen::drawThemeProperties()
{
    const ThemeProject& p = session_.project();
    // The theme's font: the bundled Roboto, or one in its fonts/ folder
    std::string current = p.font.empty() ? std::string("Roboto (bundled)") : fs::path(p.font).stem().string();
    float import = ImGui::CalcTextSize("Import...").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Font");
    ImGui::SameLine(ImGui::GetFontSize() * 6.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - import);
    const bool open = ImGui::BeginCombo("##font", current.c_str());
    if (const int step = open ? 0 : pad_.choice("Font")) {
        // The bundled font, then the theme's own
        std::vector<std::string> all = { "" };
        all.insert(all.end(), fonts_.begin(), fonts_.end());
        auto at = std::find(all.begin(), all.end(), p.font);
        const int next = choiceStep(at == all.end() ? -1 : static_cast<int>(at - all.begin()), static_cast<int>(all.size()), step);
        if (next >= 0) session_.setThemeFont(all[next]);
    }
    if (open) {
        if (ImGui::Selectable("Roboto (bundled)##bundled", p.font.empty())) session_.setThemeFont("");
        for (const std::string& font : fonts_) {
            if (ImGui::Selectable((fs::path(font).stem().string() + "##" + font).c_str(), font == p.font)) session_.setThemeFont(font);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered() && !p.font.empty()) ImGui::SetTooltip("%s", p.font.c_str());
    if (ImGui::IsItemHovered() && !p.font.empty() && !session_.themeHasFile(p.font)) ImGui::SetTooltip("%s isn't in the theme", p.font.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Import...##font")) {
        FileRequest request;
        request.what = FileFor::Font;
        request.filter = "font";
        request.title = "Choose a font for the theme (it's copied into its fonts folder)";
        openBrowser(request);
    }

    // Sounds RetroFE plays with this theme
    for (const auto& kind : SOUND_KINDS) {
        FileRequest request;
        request.what = FileFor::Sound;
        request.key = kind[0];
        request.filter = "sound";
        request.title = std::string("Choose the ") + kind[1] + " sound";
        const std::string which = kind[0];
        drawFileRow(kind[1], session_.sound(which), request, [this, which] { session_.setSound(which, ""); });
    }
    ImGui::Spacing();
}

void ThemeEditorScreen::drawSplash()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped(session_.hasSplashPage() ? "RetroFE shows the splash page while it starts."
                                                : "RetroFE shows the theme's name while it starts. Give it text or a background to change that.");
    ImGui::PopStyleColor();
    std::string text = session_.splashText();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Text");
    ImGui::SameLine(ImGui::GetFontSize() * 6.0f);
    ImGui::SetNextItemWidth(-1.0f);
    const bool typed = ImGui::InputText("##splashText", &text);
    const bool keyboard = pad_.text("Splash text", text);
    if (typed || keyboard) {
        session_.setCoalesceKey("splashText");
        session_.setSplashText(text);
    }
    if (keyboard || ImGui::IsItemDeactivated()) session_.endCoalesce();
    FileRequest request;
    request.what = FileFor::SplashBackground;
    request.filter = "media";
    request.title = "Choose a background image or video for the splash page";
    drawFileRow("Background", session_.splashBackground(), request, [this] { session_.setSplashBackground(""); });
    if (ImGui::Button(session_.hasSplashPage() ? "Edit the splash page" : "Make a splash page")) {
        session_.makeSplashPage();
        for (int i = 0; i < session_.pageCount(); ++i) {
            if (ThemeProject::isSplashPage(session_.project().pages[i].collection)) session_.setPage(i);
        }
        previewDirty_ = true;
    }
    ImGui::Spacing();
}

void ThemeEditorScreen::drawPages()
{
    ImGui::SeparatorText("Pages");
    for (int i = 0; i < session_.pageCount(); ++i) {
        std::string label = session_.pageLabel(i) + "##page" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), i == session_.pageIndex())) {
            session_.setPage(i);
            // A collection's page previews with that collection
            const std::string& c = session_.page().collection;
            if (!c.empty() && !ThemeProject::isSplashPage(c) && c != MENUS_TEMPLATE) {
                previewName_ = c;
            }
            previewDirty_ = true;
        }
    }
    if (ImGui::Button("New template...")) openTemplateDialog("", true, "");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A page of its own for one or more collections");

    const ThemePage& page = session_.page();
    if (page.collection.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("The Default page: every collection without a template of its own.");
        ImGui::PopStyleColor();
        return;
    }
    if (ThemeProject::isSplashPage(page.collection)) return;

    // The template being edited
    const std::string primary = page.collection;
    ImGui::SameLine();
    if (ImGui::Button("Copy...")) openTemplateDialog("", true, primary);
    ImGui::SameLine();
    if (ImGui::Button("Delete...")) {
        delPrimary_ = primary;
        delReplacement_.clear();
        showDeleteTpl_ = true;
    }
    std::string name = page.displayName();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    const bool typed = ImGui::InputText("##tplName", &name);
    const bool keyboard = pad_.text("Template name", name);
    if (typed || keyboard) {
        session_.setCoalesceKey("tplName");
        session_.renameTemplate(primary, name);
    }
    if (keyboard || ImGui::IsItemDeactivated()) session_.endCoalesce();

    // Its collections
    ImGui::TextDisabled(primary == MENUS_TEMPLATE ? "Used for every menu without a page of its own, and:" : "Used for:");
    std::vector<std::string> covered;
    if (primary != MENUS_TEMPLATE) covered.push_back(primary);
    for (const std::string& m : page.members) covered.push_back(m);
    // Changes wait until the page has been drawn: moving a collection can
    // delete a template, and `page` refers into the project's pages
    std::string unassign, add;
    for (const std::string& c : covered) {
        ImGui::PushID(c.c_str());
        ImGui::Bullet();
        ImGui::TextUnformatted(c.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("x").x - ImGui::GetStyle().FramePadding.x * 2.0f);
        const bool last = covered.size() == 1 && primary != MENUS_TEMPLATE;
        ImGui::BeginDisabled(last);
        if (ImGui::SmallButton("x")) unassign = c;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(last ? "A template needs a collection: delete the template instead"
                                   : "Back to the Default page");
        }
        ImGui::PopID();
    }
    // More collections: the ones not on it already
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##addCollection", "Add a collection...", ImGuiComboFlags_HeightLarge)) {
        // Typing filters straight away; with the controller the list comes first
        if (ImGui::IsWindowAppearing() && !pad_.controllerActive()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##addFilter", "Filter", &tplFilter_);
        pad_.text("Filter collections", tplFilter_);
        const std::string filter = Utils::toLower(tplFilter_);
        std::set<std::string> listed;
        for (const TreeRow& row : tree_) {
            if (row.depth < 0 || row.name.empty() || page.covers(row.name) || !listed.insert(row.name).second) continue;
            if (!filter.empty() && Utils::toLower(row.name).find(filter) == std::string::npos) continue;
            std::string label = row.name + "  (now " + pageNameFor(row.name, row.menu) + ")##add" + row.name;
            bool handMade = isHandMade(row.name);
            ImGui::BeginDisabled(handMade);
            bool chosen = ImGui::Selectable(label.c_str());
            ImGui::EndDisabled();
            if (handMade && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Its page in this theme was written by hand; the editor won't overwrite it");
            }
            if (chosen) {
                add = row.name;
                ImGui::CloseCurrentPopup();
                break;
            }
        }
        ImGui::EndCombo();
    }
    if (!unassign.empty()) session_.assignTemplate(unassign, "");
    else if (!add.empty()) session_.assignTemplate(add, primary);
}

void ThemeEditorScreen::refreshHandMade()
{
    // Read once per tree and after saving, not per frame: each is a file read
    handMade_.clear();
    std::set<std::string> names;
    for (const TreeRow& row : tree_) {
        if (row.depth >= 0 && !row.name.empty()) names.insert(row.name);
    }
    for (const std::string& name : names) {
        if (session_.handMadePage(name)) handMade_.insert(name);
    }
}

bool ThemeEditorScreen::isHandMade(const std::string& collection) const
{
    return handMade_.count(collection) != 0;
}

void ThemeEditorScreen::drawCollections()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("The page each collection gets. A collection without a template uses the Default page "
                       "(RetroFE doesn't pass a menu's page on to the collections in it).");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##treeFilter", "Filter collections", &treeFilter_);
    pad_.text("Filter collections", treeFilter_);
    const std::string filter = Utils::toLower(treeFilter_);
    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(tree_.size()); ++i) {
        if (!filter.empty() && (tree_[i].depth < 0 || Utils::toLower(tree_[i].name).find(filter) == std::string::npos)) continue;
        rows.push_back(i);
    }
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const float height = line * std::min(12.0f, std::max(3.0f, static_cast<float>(rows.size()))) + ImGui::GetStyle().WindowPadding.y;
    // NavFlattened: the controller moves into the list like any other rows
    ImGui::BeginChild("##tree", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), line);
    while (clipper.Step()) {
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
            const TreeRow& row = tree_[rows[k]];
            ImGui::PushID(rows[k]);
            if (row.depth < 0) {
                ImGui::TextDisabled("Not in a menu:");
                ImGui::PopID();
                continue;
            }
            const std::string page = row.otherLayout.empty() ? pageNameFor(row.name, row.menu) : "theme " + row.otherLayout;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row.depth * ImGui::GetFontSize() * 0.8f);
            const std::string label = row.name + (row.children ? " >" : "");
            if (ImGui::Selectable(label.c_str(), row.name == chosenCollection_)) {
                chosenCollection_ = row.name;
                // Preview it, on the page RetroFE would show it with
                previewName_ = row.name;
                const ThemePage* shown = &pageShownFor(session_.project(), row.name, row.menu);
                for (int i = 0; i < session_.pageCount(); ++i) {
                    if (&session_.project().pages[i] == shown) session_.setPage(i);
                }
                previewDirty_ = true;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openTemplateDialog(row.name, false, "");
            const float w = ImGui::CalcTextSize(page.c_str()).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - w));
            if (page == "Default" || !row.otherLayout.empty()) ImGui::TextDisabled("%s", page.c_str());
            else ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", page.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (!treeFromIndex_) ImGui::TextDisabled(ctx_.library ? "Reading the menus..." : "No menu tree: collections are listed by name.");
    if (!chosenCollection_.empty()) {
        bool menu = false;
        std::string other;
        for (const TreeRow& row : tree_) {
            if (row.name == chosenCollection_) { menu = row.menu; other = row.otherLayout; break; }
        }
        ImGui::TextWrapped("%s uses %s.", chosenCollection_.c_str(),
                           other.empty() ? ("the " + pageNameFor(chosenCollection_, menu) + " page").c_str()
                                         : ("the theme " + other + " (its own layout setting)").c_str());
        if (ImGui::Button("Change its template...")) openTemplateDialog(chosenCollection_, false, "");
    }
}

void ThemeEditorScreen::drawCanvas()
{
    const ThemeProject& p = session_.project();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 10.0f || size.y < 10.0f) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(24, 24, 28, 255));

    fit_ = canvasFit(size.x, size.y, p.canvasW(), p.canvasH());
    if (!fit_.ok) return;
    ImVec2 a(origin.x + fit_.x, origin.y + fit_.y);
    ImVec2 b(a.x + fit_.w, a.y + fit_.h);
    if (page_ && target_) {
        // The texture holds the page stretched over the window; drawn into the fit it's the right shape again
        dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(target_)), a, b);
    }
    else {
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255));
        bool empty = builtXml_.find(" layer=\"") == std::string::npos && !builtXml_.empty();
        const char* text = empty ? "An empty page: choose Add widget." :
                           previewDirty_ ? "Building the preview..." : "The preview couldn't be built (see below).";
        ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32(200, 200, 200, 255), text);
    }
    dl->AddRect(a, b, IM_COL32(90, 90, 100, 255));

    // Input: the whole canvas is one button, for the mouse (the controller has its own modes)
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::InvisibleButton("##canvasInput", size, ImGuiButtonFlags_MouseButtonLeft);
    ImGui::PopItemFlag();
    const bool hovered = ImGui::IsItemHovered() && !pad_.keyboardOpen();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const float sx = mouse.x - origin.x, sy = mouse.y - origin.y;
    const float grab = 6.0f * ctx_.scale;
    // Alignment guides follow the toolbar's setting, off while Alt is held (a script sets its own)
    if (scriptAt_ >= script_.size()) {
        session_.guides = guidesOn_ && !io.KeyAlt;
        session_.guideReachPx = 6.0f * ctx_.scale;
    }
    // Ctrl or Shift: toggle the widget clicked, or a marquee that adds
    if (ImGui::IsItemActivated()) session_.pointerDown(fit_, sx, sy, grab, io.KeyCtrl || io.KeyShift);
    if (session_.dragging()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) session_.pointerMove(fit_, sx, sy);
        else session_.pointerUp();
    }

    // Keyboard, while the canvas has focus and no field is being typed in. An
    // arrow that's also a mapped direction acts as the controller's (Pick,
    // Move, Resize), not twice.
    if (ImGui::IsWindowFocused() && !io.WantTextInput && !pad_.directionHeld() && !pad_.keyboardOpen()) {
        int step = io.KeyShift ? 10 : 1;
        int dx = 0, dy = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy += step;
        if (dx || dy) {
            session_.moveBy(dx, dy);
            sinceNudge_ = 0.0f;   // a pause ends this run of nudges as one undo step (update())
            nudging_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) session_.removeSelected();
    }
    if (!io.WantTextInput && io.KeyCtrl && !pad_.keyboardOpen()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) io.KeyShift ? session_.redo() : session_.undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) session_.redo();
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) job_ = Job::Save;
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) session_.selectAll();
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) session_.duplicateSelected();
    }

    // The wireframe, from the project (never from the engine)
    auto toStage = [&](float tx, float ty) {
        return ImVec2(origin.x + themeToCanvasX(fit_, tx), origin.y + themeToCanvasY(fit_, ty));
    };
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const auto& widgets = session_.page().widgets;
    int hoveredWidget = hovered ? canvasPick(session_.page(), canvasToThemeX(fit_, sx), canvasToThemeY(fit_, sy)) : -1;
    for (int i = 0; i < static_cast<int>(widgets.size()); ++i) {
        const WidgetInstance& w = widgets[i];
        if (w.canvasHide || session_.isSelected(i)) continue;
        const WidgetDef* def = session_.library().find(w.def);
        int x, y, ww, hh;
        canvasHitRect(w, x, y, ww, hh);
        bool hot = i == hoveredWidget;
        dl->AddRect(toStage(static_cast<float>(x), static_cast<float>(y)),
                    toStage(static_cast<float>(x + ww), static_cast<float>(y + hh)),
                    colourOf(def ? def->color : "", hot ? 230 : 120), 0.0f, 0, hot ? 2.0f : 1.0f);
    }
    // The controller's Move and Resize modes show in the outline
    const ImU32 modeColour = mode_ == ECM_MOVE ? IM_COL32(80, 220, 255, 255) : mode_ == ECM_RESIZE ? IM_COL32(255, 96, 200, 255) : 0;
    const bool multi = session_.selectionCount() > 1;
    // Several selected: each one filled and outlined, moving with a group drag
    if (multi) {
        const int ox = session_.groupDrag() && session_.dragMoved() ? session_.dragDx() : 0;
        const int oy = session_.groupDrag() && session_.dragMoved() ? session_.dragDy() : 0;
        for (int i : session_.selection()) {
            const WidgetInstance& w = widgets[i];
            if (w.canvasHide) continue;
            const WidgetDef* def = session_.library().find(w.def);
            DragRect r;
            canvasHitRect(w, r.x, r.y, r.w, r.h);
            ImVec2 p1 = toStage(static_cast<float>(r.x + ox), static_cast<float>(r.y + oy));
            ImVec2 p2 = toStage(static_cast<float>(r.x + r.w + ox), static_cast<float>(r.y + r.h + oy));
            dl->AddRectFilled(p1, p2, colourOf(def ? def->color : "", 40));
            const bool primary = i == session_.selected();
            dl->AddRect(p1, p2, modeColour ? modeColour : colourOf(def ? def->color : "", 255), 0.0f, 0,
                        (primary ? 3.0f : 2.0f) * (modeColour ? ctx_.scale : 1.0f));
        }
        // The box around them, dashed
        std::vector<DragRect> boxes = session_.selectionBoxes();
        DragRect u = unionRect(boxes);
        u.x += ox;
        u.y += oy;
        const ImVec2 u1 = toStage(static_cast<float>(u.x), static_cast<float>(u.y));
        const ImVec2 u2 = toStage(static_cast<float>(u.x + u.w), static_cast<float>(u.y + u.h));
        const float dash = 5.0f * ctx_.scale;
        for (float t = u1.x; t < u2.x; t += dash * 2.0f) {
            dl->AddLine(ImVec2(t, u1.y - 3.0f), ImVec2(std::min(t + dash, u2.x), u1.y - 3.0f), IM_COL32(255, 255, 255, 200));
            dl->AddLine(ImVec2(t, u2.y + 3.0f), ImVec2(std::min(t + dash, u2.x), u2.y + 3.0f), IM_COL32(255, 255, 255, 200));
        }
        for (float t = u1.y; t < u2.y; t += dash * 2.0f) {
            dl->AddLine(ImVec2(u1.x - 3.0f, t), ImVec2(u1.x - 3.0f, std::min(t + dash, u2.y)), IM_COL32(255, 255, 255, 200));
            dl->AddLine(ImVec2(u2.x + 3.0f, t), ImVec2(u2.x + 3.0f, std::min(t + dash, u2.y)), IM_COL32(255, 255, 255, 200));
        }
        std::string label = std::to_string(session_.selectionCount()) + " widgets";
        if (mode_ != ECM_PICK || session_.dragMoved()) label += "  " + std::to_string(u.x) + ", " + std::to_string(u.y);
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        ImVec2 at(u1.x, std::max(origin.y, u1.y - ts.y - 6.0f));
        dl->AddRectFilled(at, ImVec2(at.x + ts.x + 6.0f, at.y + ts.y + 2.0f), IM_COL32(0, 0, 0, 180));
        dl->AddText(ImVec2(at.x + 3.0f, at.y + 1.0f), IM_COL32(255, 255, 255, 255), label.c_str());
    }
    // Where the box being dragged lines up with others (or the page's centre)
    if (session_.dragMoved()) {
        for (const GuideLine& g : session_.guideLines()) {
            const ImVec2 p1 = g.vertical ? toStage(static_cast<float>(g.at), static_cast<float>(g.from))
                                         : toStage(static_cast<float>(g.from), static_cast<float>(g.at));
            const ImVec2 p2 = g.vertical ? toStage(static_cast<float>(g.at), static_cast<float>(g.to))
                                         : toStage(static_cast<float>(g.to), static_cast<float>(g.at));
            dl->AddLine(p1, p2, IM_COL32(255, 64, 160, 230), 1.5f * ctx_.scale);
        }
    }
    if (session_.marqueeActive() && session_.dragMoved()) {
        const DragRect m = session_.marqueeBox();
        const ImVec2 m1 = toStage(static_cast<float>(m.x), static_cast<float>(m.y));
        const ImVec2 m2 = toStage(static_cast<float>(m.x + m.w), static_cast<float>(m.y + m.h));
        dl->AddRectFilled(m1, m2, IM_COL32(120, 180, 255, 40));
        dl->AddRect(m1, m2, IM_COL32(120, 180, 255, 220), 0.0f, 0, 1.5f);
    }
    if (const WidgetInstance* sel = multi ? nullptr : session_.selectedWidget()) {
        const WidgetDef* def = session_.selectedDef();
        DragRect r;
        if (session_.dragMoved()) r = session_.dragRect();
        else canvasHitRect(*sel, r.x, r.y, r.w, r.h);
        ImVec2 p1 = toStage(static_cast<float>(r.x), static_cast<float>(r.y));
        ImVec2 p2 = toStage(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h));
        dl->AddRectFilled(p1, p2, colourOf(def ? def->color : "", 40));
        dl->AddRect(p1, p2, modeColour ? modeColour : colourOf(def ? def->color : "", 255), 0.0f, 0, modeColour ? 3.0f * ctx_.scale : 2.0f);
        for (int hnd = 0; hnd < 8; ++hnd) {
            float hx, hy;
            handlePoint(r, hnd, hx, hy);
            ImVec2 c = toStage(hx, hy);
            // Resize nudges move the handle Letter up / down chose
            const bool active = mode_ == ECM_RESIZE && hnd == resizeHandle_;
            float hs = (active ? 7.0f : 4.0f) * ctx_.scale;
            dl->AddRectFilled(ImVec2(c.x - hs, c.y - hs), ImVec2(c.x + hs, c.y + hs), active ? modeColour : IM_COL32(255, 255, 255, 255));
            dl->AddRect(ImVec2(c.x - hs, c.y - hs), ImVec2(c.x + hs, c.y + hs), IM_COL32(0, 0, 0, 255));
        }
        std::string label = sel->name;
        if (mode_ != ECM_PICK) {
            label += "  " + std::to_string(r.x) + ", " + std::to_string(r.y) + "  " + std::to_string(r.w) + " x " + std::to_string(r.h);
        }
        else if (session_.dragMoved()) {
            label += "  " + std::to_string(r.x) + ", " + std::to_string(r.y) + "  " + std::to_string(r.w) + " x " + std::to_string(r.h);
        }
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        ImVec2 at(p1.x, std::max(origin.y, p1.y - ts.y - 2.0f));
        dl->AddRectFilled(at, ImVec2(at.x + ts.x + 6.0f, at.y + ts.y + 2.0f), IM_COL32(0, 0, 0, 180));
        dl->AddText(ImVec2(at.x + 3.0f, at.y + 1.0f), IM_COL32(255, 255, 255, 255), label.c_str());

        // The cursor says what a press would do
        if (hovered && !session_.dragging()) {
            int handle = canvasHandleAt(fit_, r, sx, sy, grab);
            if (handle == CH_NW || handle == CH_SE) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            else if (handle == CH_NE || handle == CH_SW) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
            else if (handle == CH_N || handle == CH_S) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            else if (handle == CH_W || handle == CH_E) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            else if (handle == CH_BODY) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
    }
    if (hovered && hoveredWidget >= 0 && !session_.dragging() && hoveredWidget != session_.selected()) {
        ImGui::SetTooltip("%s", widgets[hoveredWidget].name.c_str());
    }
    // The controller's mode, while the canvas has its focus
    if (pad_.region() == EF_CANVAS && (pad_.controllerActive() || mode_ != ECM_PICK)) {
        const char* text = mode_ == ECM_MOVE ? "MOVE" : mode_ == ECM_RESIZE ? "RESIZE" : "PICK";
        const ImU32 colour = mode_ == ECM_MOVE ? IM_COL32(80, 220, 255, 255) : mode_ == ECM_RESIZE ? IM_COL32(255, 96, 200, 255)
                                                                                                   : IM_COL32(255, 196, 40, 255);
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const float pad = 6.0f * ctx_.scale;
        const ImVec2 at(origin.x + pad, origin.y + pad);
        dl->AddRectFilled(at, ImVec2(at.x + ts.x + pad * 2.0f, at.y + ts.y + pad), IM_COL32(0, 0, 0, 200), 4.0f * ctx_.scale);
        dl->AddRect(at, ImVec2(at.x + ts.x + pad * 2.0f, at.y + ts.y + pad), colour, 4.0f * ctx_.scale);
        dl->AddText(ImVec2(at.x + pad, at.y + pad * 0.5f), colour, text);
    }
    dl->PopClipRect();
}

void ThemeEditorScreen::drawProperties()
{
    const WidgetInstance* w = session_.selectedWidget();
    const WidgetDef* def = session_.selectedDef();
    if (!w) {
        ImGui::SeparatorText("Widget");
        ImGui::TextWrapped("Click a widget on the canvas or in the list to change it, or choose Add widget.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Several at once: Ctrl-click or Shift-click them, or drag a box around them from an empty part of "
                           "the canvas (with Ctrl or Shift, from anywhere). With a controller, hold Select and press a direction.");
        ImGui::PopStyleColor();
        return;
    }
    if (session_.selectionCount() > 1) {
        drawSelectionTools();
        w = session_.selectedWidget();
        def = session_.selectedDef();
        if (!w) return;
    }
    ImGui::SeparatorText(def ? def->name.c_str() : w->def.c_str());
    if (def && !def->summary.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s", def->summary.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PushItemWidth(-ImGui::CalcTextSize("Opacity %").x - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);

    auto endEdit = [this]() {
        if (ImGui::IsItemDeactivated()) session_.endCoalesce();
    };

    std::string name = w->name;
    const bool typedName = ImGui::InputText("Name", &name);
    const bool keyboardName = pad_.text("Name", name);
    if (typedName || keyboardName) { session_.setCoalesceKey("name"); session_.setName(name); }
    if (keyboardName) session_.endCoalesce();
    endEdit();

    // The controller steps each by the snap grid (Left / Right) or types one
    // (Select). The pad registers the item just drawn, so it's called after it.
    int rect[4] = { w->x, w->y, w->w, w->h };
    const int big = 100000;
    const char* const rectNames[4] = { "X", "Y", "Width", "Height" };
    const char* const rectKeys[4] = { "x", "y", "w", "h" };
    for (int i = 0; i < 4; ++i) {
        bool changed = ImGui::InputInt(rectNames[i], &rect[i], 5, 50);
        changed = pad_.number(rectNames[i], rect[i], 5, i < 2 ? -big : 1, big) || changed;
        if (changed) {
            session_.setCoalesceKey(rectKeys[i]);
            session_.setRect(rect[0], rect[1], rect[2], rect[3]);
        }
        endEdit();
    }

    int layer = w->layer;
    bool layerChanged = ImGui::SliderInt("Layer", &layer, 0, MAX_LAYER);
    layerChanged = pad_.number("Layer", layer, 1, 0, MAX_LAYER) || layerChanged;
    if (layerChanged) {
        session_.setCoalesceKey("layer");
        session_.setLayer(layer);
    }
    endEdit();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 is drawn first, 19 on top");
    int alpha = w->alphaPct;
    bool alphaChanged = ImGui::SliderInt("Opacity %", &alpha, 0, 100);
    alphaChanged = pad_.number("Opacity %", alpha, 5, 0, 100) || alphaChanged;
    if (alphaChanged) {
        session_.setCoalesceKey("alpha");
        session_.setAlpha(alpha);
    }
    endEdit();

    if (def && usesFontColour(*def)) {
        float rgb[3];
        hexToFloats(w->fontColor.empty() ? "FFFFFF" : w->fontColor, rgb);
        if (ImGui::ColorEdit3("Text colour", rgb, ImGuiColorEditFlags_NoInputs)) {
            session_.setCoalesceKey("fontColor");
            session_.setFontColor(floatsToHex(rgb));
        }
        endEdit();
        // The controller types the colour as RRGGBB
        std::string hex = w->fontColor;
        if (pad_.text("Text colour (RRGGBB)", hex)) {
            const std::string colour = normalizeColour(hex);
            if (!colour.empty() || hex.empty()) session_.setFontColor(colour);
            else setStatus("Not a colour: " + hex + " (six hex digits, like FFAA00).", true);
        }
        ImGui::SameLine();
        if (w->fontColor.empty()) ImGui::TextDisabled("(theme's)");
        else if (ImGui::SmallButton("Clear##fontColor")) session_.setFontColor("");
    }

    bool hidden = w->canvasHide;
    if (ImGui::Checkbox("Hide in the editor", &hidden)) session_.setCanvasHidden(hidden);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only hides it here, to reach what's underneath; the theme still shows it");

    if (def && !def->vars.empty()) {
        ImGui::SeparatorText("Settings");
        for (const WidgetVar& var : def->vars) drawVar(var);
    }
    ImGui::PopItemWidth();

    if (def && !def->info.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Writes: %s", def->info.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (session_.selectionCount() > 1) return;   // the group's Duplicate and Delete are at the top
    if (ImGui::Button("Duplicate")) {
        if (session_.duplicateSelected()) setStatus("Added a copy, 20 units down and right (Ctrl+D).", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete widget")) session_.removeSelected();
}

void ThemeEditorScreen::alignSelection(int op)
{
    static const char* const what[] = { "left edges", "centres", "right edges", "tops", "middles", "bottoms" };
    const int n = session_.selectionCount();
    if (session_.alignSelected(op)) setStatus("Lined up the " + std::to_string(n) + " widgets' " + what[op] + ". Undo takes it back.", false);
    else setStatus("They're lined up that way already.", false);
}

void ThemeEditorScreen::distributeSelection(bool horizontal)
{
    const int n = session_.selectionCount();
    if (session_.distributeSelected(horizontal)) {
        setStatus("Spaced the " + std::to_string(n) + " widgets evenly " + (horizontal ? "across" : "down") +
                  ": the outer two stayed where they were.", false);
    }
    else setStatus("They're evenly spaced that way already.", false);
}

void ThemeEditorScreen::drawSelectionTools()
{
    const int n = session_.selectionCount();
    const WidgetInstance* primary = session_.selectedWidget();
    ImGui::SeparatorText((std::to_string(n) + " widgets selected").c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Dragging one moves them all; the arrows nudge them all; Delete deletes them all. They line up on the "
                       "box around them.");
    ImGui::PopStyleColor();
    const float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    const char* const labels[6] = { "Left", "Centre", "Right", "Top", "Middle", "Bottom" };
    for (int op = 0; op < 6; ++op) {
        if (op % 3) ImGui::SameLine();
        if (ImGui::Button(labels[op], ImVec2(third, 0))) alignSelection(op);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Line their %s up", labels[op]);
    }
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(n < 3);
    if (ImGui::Button("Space across", ImVec2(half, 0))) distributeSelection(true);
    ImGui::SameLine();
    if (ImGui::Button("Space down", ImVec2(half, 0))) distributeSelection(false);
    ImGui::EndDisabled();
    if (n < 3 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Spacing needs three or more");
    if (ImGui::Button("Duplicate them")) {
        if (session_.duplicateSelected()) setStatus("Added " + std::to_string(n) + " copies, 20 units down and right; they're selected.", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete them")) session_.removeSelected();
    drawSharedSettings();
    if (primary) {
        if (ImGui::Button("Only this one")) session_.select(session_.selected());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep only %s selected", primary->name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Below: %s, the last one picked.", primary->name.c_str());
        ImGui::PopStyleColor();
    }
}

void ThemeEditorScreen::drawSharedSettings()
{
    const int n = session_.selectionCount();
    if (n < 2) return;
    const std::vector<EditorSession::SharedSetting> settings = session_.sharedSettings();
    if (settings.empty()) return;
    ImGui::SeparatorText(("All " + std::to_string(n) + " at once").c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("What they all have. A value set here goes to every one, as one change Undo takes back. "
                       "\"(several values)\": they differ.");
    ImGui::PopStyleColor();
    ImGui::PushID("shared");
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    for (const EditorSession::SharedSetting& s : settings) {
        ImGui::PushID(s.name.c_str());
        const bool mixed = s.value.mixed;
        const std::string label = s.var ? s.var->name : s.name == "layer" ? "Layer" : s.name == "alpha" ? "Opacity %" : "Text colour";
        auto apply = [this, &s](const std::string& value) {
            session_.setCoalesceKey("shared:" + s.name);
            bool ok = false;
            if (s.name == "layer" && !s.var) ok = session_.setLayerAll(std::atoi(value.c_str()));
            else if (s.name == "alpha" && !s.var) ok = session_.setAlphaAll(std::atoi(value.c_str()));
            else if (s.name == "fontColor" && !s.var) {
                // Written once it's a whole colour (or emptied: the theme's own)
                const std::string colour = normalizeColour(value);
                if (value.empty() || !colour.empty()) ok = session_.setFontColorAll(colour);
            }
            else ok = session_.setVarAll(s.name, value);
            return ok;
        };
        const VarKind kind = s.var ? varKind(*s.var) : VarKind::Text;
        if (s.var && (kind == VarKind::Bool || kind == VarKind::Choice)) {
            // One of its values for all of them
            std::vector<std::string> options;
            if (kind == VarKind::Bool) options = { "true", "false" };
            else for (const std::string& o : s.var->options) options.push_back(o == "-" ? std::string() : o);
            const std::string shown = mixed ? "(several values)" : (s.value.value.empty() ? "(none)" : s.value.value);
            const bool open = ImGui::BeginCombo(label.c_str(), shown.c_str());
            if (const int step = open ? 0 : pad_.choice((label + " (all of them)").c_str())) {
                int cur = -1;
                for (size_t i = 0; i < options.size(); ++i) {
                    if (!mixed && options[i] == s.value.value) cur = static_cast<int>(i);
                }
                const int next = choiceStep(cur, static_cast<int>(options.size()), step);
                if (next >= 0) { apply(options[next]); session_.endCoalesce(); }
            }
            if (open) {
                for (const std::string& o : options) {
                    if (ImGui::Selectable(o.empty() ? "(none)" : o.c_str(), !mixed && o == s.value.value)) { apply(o); session_.endCoalesce(); }
                }
                ImGui::EndCombo();
            }
        }
        else {
            std::string value = mixed ? std::string() : s.value.value;
            const bool typed = ImGui::InputTextWithHint(label.c_str(), mixed ? "(several values)" : "", &value);
            const bool keyboard = pad_.text((label + " (all of them)").c_str(), value);
            // A field showing "(several values)" changes nothing until something is typed in it
            if ((typed || keyboard) && !(mixed && value.empty())) apply(value);
            if (keyboard || ImGui::IsItemDeactivated()) session_.endCoalesce();
            if (kind == VarKind::Colour || s.name == "fontColor") {
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Six hex digits, like FFAA00; empty: the theme's own");
            }
        }
        ImGui::PopID();
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
}

void ThemeEditorScreen::drawVar(const WidgetVar& var)
{
    const WidgetInstance* w = session_.selectedWidget();
    if (!w) return;
    std::string value = w->varValue(var.token, var.def);
    ImGui::PushID(var.token.c_str());
    const char* label = var.name.c_str();
    switch (varKind(var)) {
    case VarKind::Bool: {
        bool on = value == "true";
        if (ImGui::Checkbox(label, &on)) session_.setVar(var.token, on ? "true" : "false");
        break;
    }
    case VarKind::Choice: {
        std::string shown = value.empty() ? "(none)" : value;
        const bool open = ImGui::BeginCombo(label, shown.c_str());
        // Left / Right: the previous / next option
        if (const int step = open ? 0 : pad_.choice(label)) {
            int cur = -1;
            for (size_t i = 0; i < var.options.size(); ++i) {
                if ((var.options[i] == "-" ? std::string() : var.options[i]) == value) cur = static_cast<int>(i);
            }
            const int next = choiceStep(cur, static_cast<int>(var.options.size()), step);
            if (next >= 0) session_.setVar(var.token, var.options[next] == "-" ? std::string() : var.options[next]);
        }
        if (open) {
            for (const std::string& option : var.options) {
                std::string optionValue = option == "-" ? std::string() : option;
                std::string optionLabel = optionValue.empty() ? "(none)" : optionValue;
                if (ImGui::Selectable(optionLabel.c_str(), optionValue == value)) session_.setVar(var.token, optionValue);
                if (optionValue == value) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        break;
    }
    case VarKind::Number: {
        double number = value.empty() ? 0.0 : std::atof(value.c_str());
        const bool typed = ImGui::InputDouble(label, &number, var.step, var.step * 10.0, "%g");
        const bool pad = pad_.number(label, number, var.step > 0.0 ? var.step : 1.0);
        if (typed || pad) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", number);
            session_.setCoalesceKey("var");
            session_.setVar(var.token, buffer);
        }
        if (ImGui::IsItemDeactivated()) session_.endCoalesce();
        if (value.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(auto)");
        }
        break;
    }
    case VarKind::Colour: {
        float rgb[3];
        hexToFloats(value.empty() ? "FFFFFF" : value, rgb);
        if (ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_NoInputs)) {
            session_.setCoalesceKey("var");
            session_.setVar(var.token, floatsToHex(rgb));
        }
        if (ImGui::IsItemDeactivated()) session_.endCoalesce();
        // The controller types it as RRGGBB
        std::string hex = value;
        if (pad_.text((var.name + " (RRGGBB)").c_str(), hex)) {
            const std::string colour = normalizeColour(hex);
            if (!colour.empty() || hex.empty()) session_.setVar(var.token, colour);
            else setStatus("Not a colour: " + hex + " (six hex digits, like FFAA00).", true);
        }
        ImGui::SameLine();
        if (value.empty()) ImGui::TextDisabled("(auto)");
        else if (ImGui::SmallButton("Clear")) session_.setVar(var.token, "");
        break;
    }
    case VarKind::File: {
        std::string text = value;
        float button = ImGui::CalcTextSize("Choose...").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - button);
        const bool typed = ImGui::InputText("##file", &text);
        const bool keyboard = pad_.text(label, text);
        if (typed || keyboard) { session_.setCoalesceKey("var"); session_.setVar(var.token, text); }
        if (keyboard || ImGui::IsItemDeactivated()) session_.endCoalesce();
        ImGui::SameLine();
        if (ImGui::Button("Choose...")) openBrowser(varRequest(var.token));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The file is copied into the theme's assets folder");
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        if (!session_.themeHasFile(value)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
            ImGui::TextWrapped(value.empty() ? "No file chosen." : "%s isn't in the theme yet: choose a file.", value.c_str());
            ImGui::PopStyleColor();
        }
        break;
    }
    case VarKind::Raw: {
        std::string text = value;
        const bool typed = ImGui::InputTextMultiline(label, &text, ImVec2(0, ImGui::GetTextLineHeight() * 4.0f));
        const bool keyboard = pad_.text(label, text, true);
        if (typed || keyboard) {
            session_.setCoalesceKey("var");
            session_.setVar(var.token, text);
        }
        if (keyboard || ImGui::IsItemDeactivated()) session_.endCoalesce();
        break;
    }
    case VarKind::Text: {
        std::string text = value;
        const bool typed = ImGui::InputText(label, &text);
        const bool keyboard = pad_.text(label, text);
        if (typed || keyboard) { session_.setCoalesceKey("var"); session_.setVar(var.token, text); }
        if (keyboard || ImGui::IsItemDeactivated()) session_.endCoalesce();
        break;
    }
    }
    ImGui::PopID();
}

void ThemeEditorScreen::syncPopup(const char* name, bool& show, bool& opened)
{
    // ImGui closes a modal by itself too (Escape through its keyboard
    // navigation, the X): that's the user closing it, not a cue to reopen it
    if (opened && !ImGui::IsPopupOpen(name)) {
        show = false;
        opened = false;
    }
    if (show && !opened) {
        ImGui::OpenPopup(name);
        opened = true;
    }
}

void ThemeEditorScreen::drawAddWidget()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(std::min(io.DisplaySize.x - 40.0f, 760.0f * ctx_.scale),
                                    std::min(io.DisplaySize.y - 40.0f, 560.0f * ctx_.scale)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    syncPopup("Add widget", showAdd_, addOpen_);
    if (!ImGui::BeginPopupModal("Add widget", &showAdd_)) return;
    if (!showAdd_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::SetNextItemWidth(-1.0f);
    // Typing searches straight away; with the controller the list comes first
    // (Select on the search field types with the on-screen keyboard)
    if (ImGui::IsWindowAppearing() && !pad_.controllerActive()) ImGui::SetKeyboardFocusHere();
    // Enter adds the first widget the search finds, so it works from the keyboard
    bool addFirst = ImGui::InputTextWithHint("##filter", "Search widgets, Enter adds the first", &addFilter_,
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    pad_.text("Search widgets", addFilter_);
    std::string filter = Utils::toLower(addFilter_);

    ImGui::BeginChild("##defs", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    static const std::pair<const char*, const char*> categories[] = {
        { "menu", "Wheels and menus" }, { "media", "Art and video" }, { "text", "Text" }, { "static", "Fixed images, video and text" }
    };
    for (const auto& category : categories) {
        bool header = false;
        for (const WidgetDef& def : session_.library().defs) {
            if (def.category != category.first) continue;
            if (!filter.empty() && Utils::toLower(def.name).find(filter) == std::string::npos &&
                Utils::toLower(def.summary).find(filter) == std::string::npos) {
                continue;
            }
            if (!header) {
                ImGui::SeparatorText(category.second);
                header = true;
            }
            ImGui::PushID(def.id.c_str());
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float box = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + box, pos.y + box), colourOf(def.color, 255), 3.0f);
            ImGui::Dummy(ImVec2(box, box));
            ImGui::SameLine();
            if (ImGui::Selectable(def.name.c_str()) || addFirst) {
                addFirst = false;
                showAdd_ = false;
                ImGui::CloseCurrentPopup();
                if (session_.addWidget(def.id)) askForFiles();
            }
            ImGui::SameLine(260.0f * ctx_.scale);
            ImGui::TextDisabled("%s", def.summary.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void ThemeEditorScreen::drawBrowser()
{
    std::string path;
    const Themes::FileChooser::Result result =
        browser_.draw(ctx_.scale, fs::path(ctx_.root), fs::path(session_.layoutsRoot()) / folder_, path);
    if (result == Themes::FileChooser::Result::Chosen) {
        job_ = Job::Import;
        jobRequest_ = browserRequest_;
        jobPath_ = path;
    }
    else if (result == Themes::FileChooser::Result::SomewhereElse) {
        // The system's dialog blocks: it opens from update(), after this frame
        job_ = Job::NativePick;
        jobRequest_ = browserRequest_;
    }
}

void ThemeEditorScreen::drawTemplateDialog()
{
    ImGuiIO& io = ImGui::GetIO();
    syncPopup("Collection template", showTemplate_, templateOpen_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f * ctx_.scale, 0), ImVec2(io.DisplaySize.x - 40.0f, io.DisplaySize.y - 40.0f));
    if (!ImGui::BeginPopupModal("Collection template", &showTemplate_, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (!showTemplate_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const ThemeProject& p = session_.project();
    const std::vector<std::string> templates = session_.templates();
    bool menu = tplCollection_ == MENUS_TEMPLATE;
    std::string otherLayout;
    for (const TreeRow& row : tree_) {
        if (row.name == tplCollection_) { menu = row.menu; otherLayout = row.otherLayout; break; }
    }

    // Which collection
    if (tplFixedCollection_) {
        ImGui::Text("Collection: %s", tplCollection_.c_str());
    }
    else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Collection");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(320.0f * ctx_.scale);
        const std::string shown = tplCollection_.empty() ? std::string("Choose...") :
                                  tplCollection_ == MENUS_TEMPLATE ? std::string("_menu (every menu)") : tplCollection_;
        if (ImGui::BeginCombo("##tplCollection", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::IsWindowAppearing() && !pad_.controllerActive()) ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##tplFilter", "Filter", &tplFilter_);
            pad_.text("Filter collections", tplFilter_);
            const std::string filter = Utils::toLower(tplFilter_);
            if (filter.empty() && ImGui::Selectable("_menu (every menu of collections without a page of its own)", tplCollection_ == MENUS_TEMPLATE)) {
                tplCollection_ = MENUS_TEMPLATE;
                if (tplName_.empty()) tplName_ = "Menus";
            }
            std::set<std::string> listed;
            for (const TreeRow& row : tree_) {
                if (row.depth < 0 || row.name.empty() || !listed.insert(row.name).second) continue;
                if (!filter.empty() && Utils::toLower(row.name).find(filter) == std::string::npos) continue;
                if (ImGui::Selectable((row.name + "##coll").c_str(), row.name == tplCollection_)) {
                    tplCollection_ = row.name;
                    if (tplName_.empty()) tplName_ = row.name;
                }
            }
            ImGui::EndCombo();
        }
    }
    if (!tplCollection_.empty()) {
        if (!otherLayout.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Its layout setting uses the theme %s, so RetroFE doesn't show this theme for it.",
                               otherLayout.c_str());
        }
        ImGui::TextDisabled("Uses now: %s", pageNameFor(tplCollection_, menu).c_str());
    }
    ImGui::Separator();

    // What it should use
    ImGui::RadioButton("The Default page", &tplChoice_, 1);
    for (size_t i = 0; i < templates.size(); ++i) {
        const ThemePage* t = p.page(templates[i]);
        std::string label = t->displayName();
        if (templates[i] == MENUS_TEMPLATE) label += " (every menu)";
        label += "##tpl" + std::to_string(i);
        ImGui::RadioButton(label.c_str(), &tplChoice_, static_cast<int>(i) + 2);
    }
    ImGui::RadioButton("A new template", &tplChoice_, 0);
    if (tplChoice_ == 0) {
        ImGui::Indent();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name");
        ImGui::SameLine(ImGui::GetFontSize() * 5.0f);
        ImGui::SetNextItemWidth(260.0f * ctx_.scale);
        ImGui::InputText("##tplNewName", &tplName_);
        pad_.text("Template name", tplName_);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Based on");
        ImGui::SameLine(ImGui::GetFontSize() * 5.0f);
        ImGui::SetNextItemWidth(260.0f * ctx_.scale);
        auto seedLabel = [&](const std::string& seed) {
            if (seed.empty()) return std::string("The Default page");
            if (seed == EditorSession::BLANK) return std::string("An empty page");
            const ThemePage* t = p.page(seed);
            return t ? t->displayName() : seed;
        };
        if (ImGui::BeginCombo("##tplSeed", seedLabel(tplBasedOn_).c_str())) {
            if (ImGui::Selectable("The Default page", tplBasedOn_.empty())) tplBasedOn_.clear();
            if (ImGui::Selectable("An empty page", tplBasedOn_ == EditorSession::BLANK)) tplBasedOn_ = EditorSession::BLANK;
            for (const std::string& t : templates) {
                if (ImGui::Selectable((seedLabel(t) + "##seed" + t).c_str(), tplBasedOn_ == t)) tplBasedOn_ = t;
            }
            ImGui::EndCombo();
        }
        ImGui::Unindent();
    }

    // The collections under a menu: RetroFE doesn't pass a menu's page on, so this does it here
    const std::vector<std::string> under = tplCollection_.empty() ? std::vector<std::string>() : descendants(tplCollection_);
    if (!under.empty()) {
        std::string label = "Also the " + std::to_string(under.size()) + " collection" + (under.size() == 1 ? "" : "s") + " under " + tplCollection_;
        ImGui::Checkbox(label.c_str(), &tplCascade_);
    }
    else {
        tplCascade_ = false;
    }

    // What happens, and what can't
    const std::string target = tplChoice_ >= 2 && tplChoice_ - 2 < static_cast<int>(templates.size()) ? templates[tplChoice_ - 2] : std::string();
    std::string problem;
    std::string summary;
    if (tplCollection_.empty()) {
        problem = "Choose a collection.";
    }
    else if (tplChoice_ == 0) {
        // Worked out when the inputs change, not every frame (it reads the collection's page file)
        const std::string key = tplCollection_ + '\n' + tplName_ + '\n' + std::to_string(session_.revision());
        if (key != tplProblemKey_) {
            tplProblemKey_ = key;
            tplProblem_ = session_.templateProblem(tplCollection_, tplName_);
        }
        problem = tplProblem_;
        summary = "Makes the template " + tplName_ + " for " + tplCollection_ + " and starts editing it.";
    }
    else {
        const ThemePage* cover = p.coveredBy(tplCollection_);
        const std::string current = cover ? cover->collection : std::string();
        if (current == target && !tplCascade_) problem = tplCollection_ + " uses that already.";
        else if (!target.empty() && isHandMade(tplCollection_)) {
            problem = "collections/" + tplCollection_ + "/layout/layout.xml in this theme was written by hand; the editor won't overwrite it.";
        }
        summary = tplCollection_ + " will use " + (target.empty() ? std::string("the Default page") : p.page(target)->displayName()) + ".";
        if (current != target && !session_.removesTemplate(tplCollection_).empty()) {
            summary += " That deletes the template " + session_.removesTemplate(tplCollection_) + ": " + tplCollection_ +
                       " is its only collection (Undo brings it back).";
        }
    }
    if (!problem.empty()) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s", problem.c_str());
    else ImGui::TextWrapped("%s", summary.c_str());

    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button("Apply")) {
        std::vector<std::string> also = tplCascade_ ? under : std::vector<std::string>();
        bool ok = false;
        int skipped = 0;
        if (tplChoice_ == 0) {
            ok = session_.newTemplate(tplCollection_, tplName_, tplBasedOn_, also);
        }
        else {
            also.insert(also.begin(), tplCollection_);
            ok = session_.assignTemplate(also, target, &skipped);
            // Show the collection on its page now
            const ThemePage* shown = &pageShownFor(session_.project(), tplCollection_, menu);
            for (int i = 0; ok && i < session_.pageCount(); ++i) {
                if (&session_.project().pages[i] == shown) session_.setPage(i);
            }
        }
        if (ok) {
            if (tplCollection_ != MENUS_TEMPLATE) previewName_ = tplCollection_;
            previewDirty_ = true;
            setStatus(skipped ? std::to_string(skipped) + " collection(s) kept their hand-made pages." : std::string(), false);
        }
        else {
            setStatus("Nothing changed.", true);
        }
        showTemplate_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        showTemplate_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ThemeEditorScreen::drawDeleteTemplate()
{
    ImGuiIO& io = ImGui::GetIO();
    syncPopup("Delete template", showDeleteTpl_, deleteTplOpen_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f * ctx_.scale, 0), ImVec2(io.DisplaySize.x - 40.0f, io.DisplaySize.y - 40.0f));
    if (!ImGui::BeginPopupModal("Delete template", &showDeleteTpl_, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const ThemePage* t = session_.project().page(delPrimary_);
    if (!showDeleteTpl_ || !t || delPrimary_.empty()) {
        showDeleteTpl_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::Text("Delete the template %s?", t->displayName().c_str());
    std::string list;
    if (delPrimary_ != MENUS_TEMPLATE) list = delPrimary_;
    for (const std::string& m : t->members) list += (list.empty() ? "" : ", ") + m;
    if (!list.empty()) {
        ImGui::TextWrapped("It's used for %s. They'll use:", list.c_str());
        auto label = [&](const std::string& primary) {
            const ThemePage* r = primary.empty() ? nullptr : session_.project().page(primary);
            return r ? r->displayName() : std::string("The Default page");
        };
        ImGui::SetNextItemWidth(300.0f * ctx_.scale);
        if (ImGui::BeginCombo("##replacement", label(delReplacement_).c_str())) {
            if (ImGui::Selectable("The Default page", delReplacement_.empty())) delReplacement_.clear();
            for (const std::string& other : session_.templates()) {
                if (other == delPrimary_ || other == MENUS_TEMPLATE) continue;
                if (ImGui::Selectable((label(other) + "##repl" + other).c_str(), other == delReplacement_)) delReplacement_ = other;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::TextDisabled("Its page files go when the theme is saved. Undo brings the template back.");
    if (ImGui::Button("Delete")) {
        if (!session_.deleteTemplate(delPrimary_, delReplacement_)) setStatus("The template wasn't deleted.", true);
        previewDirty_ = true;
        showDeleteTpl_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        showDeleteTpl_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();   // the controller starts on Cancel
    ImGui::EndPopup();
}

void ThemeEditorScreen::drawPopups()
{
    drawAddWidget();
    drawBrowser();
    drawTemplateDialog();
    drawDeleteTemplate();

    ImGuiIO& io = ImGui::GetIO();
    syncPopup("Saved", showReport_, reportOpen_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f * ctx_.scale, 0), ImVec2(io.DisplaySize.x - 40.0f, io.DisplaySize.y - 40.0f));
    if (ImGui::BeginPopupModal("Saved", &showReport_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!showReport_) ImGui::CloseCurrentPopup();
        ImGui::Text("%d files written.", static_cast<int>(report_.written.size()));
        if (!report_.removed.empty()) {
            ImGui::SeparatorText("Pages removed");
            ImGui::TextWrapped("These pages were written by an earlier save and aren't part of the theme any more:");
            for (const std::string& removed : report_.removed) ImGui::BulletText("%s", removed.c_str());
        }
        if (!report_.warnings.empty()) {
            ImGui::SeparatorText("Warnings");
            for (const std::string& warning : report_.warnings) {
                ImGui::Bullet();
                ImGui::TextWrapped("%s", warning.c_str());
            }
        }
        // Enter answers from the keyboard, unless it's the mapped Select (which presses the focused button)
        if (ImGui::Button("OK (Enter)") || (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !pad_.selectHeld())) {
            showReport_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    syncPopup("Unsaved changes", showClose_, closeOpen_);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", &showClose_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!showClose_) ImGui::CloseCurrentPopup();
        ImGui::Text("Save the changes to %s?", session_.project().name.c_str());
        // Keyboard answers too (ImGui's keyboard navigation is off in the editor),
        // but not a key that's a mapped control: the controller presses the focused button
        const bool keys = !pad_.selectHeld() && !pad_.controllerActive();
        if (ImGui::Button("Save (Enter)") || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
            job_ = Job::SaveAndClose;
            showClose_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save (D)") || (keys && ImGui::IsKeyPressed(ImGuiKey_D, false))) {
            leaveWithoutSaving_ = true;
            back_ = true;
            showClose_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)")) {
            showClose_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

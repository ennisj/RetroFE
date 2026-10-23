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

    int handleFromName(const std::string& name)
    {
        static const char* names[] = { "nw", "n", "ne", "w", "e", "sw", "s", "se" };
        for (int i = 0; i < 8; ++i) {
            if (name == names[i]) return i;
        }
        return CH_NONE;
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
}

ThemeEditorScreen::ThemeEditorScreen(const Context& context, const std::string& folder)
    : ctx_(context)
    , folder_(folder)
{
    const std::string layouts = Utils::combinePath(ctx_.root, "layouts");
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

    // RETROFE_EDITOR_SCRIPT: commands to run, one per frame
    if (const char* scriptPath = std::getenv("RETROFE_EDITOR_SCRIPT")) {
        std::ifstream in(scriptPath);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string trimmed = Utils::trimEnds(line);
            if (!trimmed.empty() && trimmed[0] != '#') script_.push_back(trimmed);
        }
        if (!script_.empty()) LOG_NOTICE("ThemeEditor", "Running " + std::to_string(script_.size()) + " script commands from " + scriptPath);
    }
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
    if (!ensureTarget()) return;

    // Fonts and art resolve through the layout setting
    if (!layoutOverridden_ && ctx_.config) {
        ctx_.config->getProperty(OPTION_LAYOUT, originalLayout_);
        layoutOverridden_ = true;
    }
    ctx_.config->setProperty(OPTION_LAYOUT, folder_);

    std::string themeDir = Utils::combinePath(ctx_.root, "layouts", folder_);
    std::string pageDir = session_.pageFolder().empty() ? themeDir : (fs::path(themeDir) / session_.pageFolder()).string();

    // Loaded first: what loading a collection logs isn't about the page
    CollectionInfo* collection = previewCollection();

    std::vector<std::string> issues;
    Logger::setTap([&issues](Logger::Zone zone, const std::string& component, const std::string& message) {
        if (zone == Logger::ZONE_ERROR || zone == Logger::ZONE_WARNING) issues.push_back("[" + component + "] " + message);
    });
    PageBuilder builder(folder_, "layout", *ctx_.config, ctx_.fontCache);
    builder.setLayoutXml(builtXml_.c_str(), pageDir);
    Page* page = builder.buildPage();
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
    Logger::setTap(nullptr);

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
    case Job::PickFile:
        pickFile();
        break;
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

void ThemeEditorScreen::pickFile()
{
    const WidgetDef* def = session_.selectedDef();
    const WidgetVar* var = def ? def->var(jobToken_) : nullptr;
    if (!var || !FileDialog::isSupported()) return;
    std::string filterName = "All files", pattern = "*.*";
    if (var->filter == "image") { filterName = "Images"; pattern = "*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp"; }
    else if (var->filter == "video") { filterName = "Videos"; pattern = "*.mp4;*.mkv;*.avi;*.webm;*.mov;*.m4v"; }
    else if (var->filter == "text") { filterName = "Text files"; pattern = "*.txt"; }
    std::string path;
    if (!FileDialog::pickFile(ctx_.window, "Choose a file for " + var->name, filterName, pattern, path)) return;
    std::string rel, error;
    if (!importAsset(session_.layoutsRoot(), folder_, path, rel, error)) {
        setStatus(error, true);
        return;
    }
    session_.setVar(var->token, rel);
}

// ---- RETROFE_EDITOR_SCRIPT --------------------------------------------------

void ThemeEditorScreen::runScript()
{
    if (scriptAt_ >= script_.size()) return;
    if (scriptWait_ > 0) {
        scriptWait_--;
        return;
    }
    // Don't act while the preview still has to catch up, unless told to wait anyway
    const std::string line = script_[scriptAt_++];
    bool ok = runCommand(line);
    LOG_NOTICE("ThemeEditor", "script: " + line + (ok ? " -> ok" : " -> failed"));
}

bool ThemeEditorScreen::runCommand(const std::string& line)
{
    std::istringstream in(line);
    std::string command;
    in >> command;
    auto rest = [&in]() {
        std::string text;
        std::getline(in, text);
        return Utils::trimEnds(text);
    };
    const int cw = session_.project().canvasW();
    const int ch = session_.project().canvasH();
    // The script works in theme units: a canvas drawn at 1:1
    const CanvasFit unit = canvasFit(static_cast<float>(cw), static_cast<float>(ch), cw, ch);

    if (command == "wait") { in >> scriptWait_; return true; }
    if (command == "log") { LOG_NOTICE("ThemeEditor", "script says: " + rest()); return true; }
    if (command == "page") { int n = -1; in >> n; session_.setPage(n); return session_.pageIndex() == n; }
    if (command == "select") { int n = -1; in >> n; session_.select(n); return session_.selected() == n; }
    if (command == "selectid") { return session_.selectId(rest()); }
    if (command == "add") { std::string id; in >> id; return session_.addWidget(id); }
    if (command == "click") {
        float x = 0, y = 0;
        in >> x >> y;
        session_.pointerDown(unit, x, y, 0.5f);
        session_.pointerUp();
        LOG_NOTICE("ThemeEditor", "script: selected " + std::to_string(session_.selected()));
        return true;
    }
    if (command == "move" || command == "resize") {
        const WidgetInstance* w = session_.selectedWidget();
        if (!w) return false;
        DragRect r;
        canvasHitRect(*w, r.x, r.y, r.w, r.h);
        float sx = r.x + r.w * 0.5f, sy = r.y + r.h * 0.5f;
        if (command == "resize") {
            std::string name;
            in >> name;
            int handle = handleFromName(name);
            if (handle == CH_NONE) return false;
            handlePoint(r, handle, sx, sy);
        }
        float dx = 0, dy = 0;
        in >> dx >> dy;
        session_.pointerDown(unit, sx, sy, 0.5f);
        session_.pointerMove(unit, sx + dx, sy + dy);
        session_.pointerUp();
        w = session_.selectedWidget();
        if (w) LOG_NOTICE("ThemeEditor", "script: " + w->id + " now " + std::to_string(w->x) + "," + std::to_string(w->y) +
                          " " + std::to_string(w->w) + "x" + std::to_string(w->h));
        return true;
    }
    if (command == "nudge") { int dx = 0, dy = 0; in >> dx >> dy; return session_.moveBy(dx, dy); }
    if (command == "name") { return session_.setName(rest()); }
    if (command == "layer") { int n = 0; in >> n; return session_.setLayer(n); }
    if (command == "alpha") { int n = 0; in >> n; return session_.setAlpha(n); }
    if (command == "colour" || command == "color") { return session_.setFontColor(rest()); }
    if (command == "var") { std::string token; in >> token; return session_.setVar(token, rest()); }
    if (command == "delete") { return session_.removeSelected(); }
    if (command == "undo") { return session_.undo(); }
    if (command == "redo") { return session_.redo(); }
    if (command == "show") {
        std::string what, onOff;
        in >> what >> onOff;
        bool on = onOff != "off";
        if (what == "media") show_.includeMedia = on;
        else if (what == "text") show_.includeText = on;
        else if (what == "anims") show_.includeAnims = on;
        else return false;
        previewDirty_ = true;
        return true;
    }
    if (command == "preview") { previewName_ = rest(); previewDirty_ = true; return true; }
    if (command == "rebuild") { builtXml_.clear(); previewDirty_ = true; return true; }
    if (command == "save") { return save(); }
    if (command == "use") { use(); return !statusIsError_; }
    if (command == "addDialog") { showAdd_ = true; return true; }
    if (command == "report") { showReport_ = true; return true; }
    if (command == "close") { requestClose(); return true; }
    if (command == "discard") { leaveWithoutSaving_ = true; back_ = true; return true; }
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
    if (showAdd_ || showClose_ || showReport_) {
        showAdd_ = showClose_ = showReport_ = false;
        return true;
    }
    if (session_.dragging()) {
        session_.cancelDrag();
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

    drawToolbar();

    const float sideW = 270.0f * s;
    const float propsW = 330.0f * s;
    const float statusH = ImGui::GetTextLineHeightWithSpacing() * 3.2f;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("##theme", ImVec2(sideW, avail.y), ImGuiChildFlags_Borders);
    drawThemePanel();
    ImGui::EndChild();

    ImGui::SameLine();
    float canvasW = std::max(100.0f, avail.x - sideW - propsW - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::BeginChild("##canvasColumn", ImVec2(canvasW, avail.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginChild("##canvas", ImVec2(canvasW, avail.y - statusH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas();
    ImGui::EndChild();

    // Status, then what the generator and the engine said about the page
    ImGui::BeginChild("##status", ImVec2(canvasW, 0), ImGuiChildFlags_None);
    if (!status_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
    else {
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
    ImGui::BeginChild("##props", ImVec2(0, avail.y), ImGuiChildFlags_Borders);
    drawProperties();
    ImGui::EndChild();

    drawPopups();
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
    bool inUse = Utils::toLower(originalLayout_) == Utils::toLower(folder_) && layoutOverridden_;
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

void ThemeEditorScreen::drawThemePanel()
{
    const ThemeProject& p = session_.project();
    ImGui::SeparatorText("Theme");
    std::string name = p.name;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##themeName", &name)) {
        session_.setCoalesceKey("themeName");
        session_.setThemeName(name);
    }
    if (ImGui::IsItemDeactivated()) session_.endCoalesce();
    int vertical = p.vertical ? 1 : 0;
    if (ImGui::RadioButton("Landscape", &vertical, 0)) session_.setVertical(false);
    ImGui::SameLine();
    if (ImGui::RadioButton("Portrait", &vertical, 1)) session_.setVertical(true);
    ImGui::TextDisabled("Font: %s", p.font.empty() ? "Roboto (bundled)" : p.font.c_str());

    ImGui::SeparatorText("Pages");
    for (int i = 0; i < session_.pageCount(); ++i) {
        std::string label = session_.pageLabel(i) + "##page" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), i == session_.pageIndex())) {
            session_.setPage(i);
            // A collection's page previews with that collection
            const std::string& c = session_.page().collection;
            if (!c.empty() && !ThemeProject::isSplashPage(c) &&
                std::find(collections_.begin(), collections_.end(), c) != collections_.end()) {
                previewName_ = c;
            }
            previewDirty_ = true;
        }
    }
    if (session_.pageCount() == 1) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Pages for particular collections can't be added in this version.");
        ImGui::PopStyleColor();
    }

    ImGui::SeparatorText("Widgets on this page");
    const auto& widgets = session_.page().widgets;
    if (widgets.empty()) ImGui::TextDisabled("None yet: choose Add widget.");
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
        if (ImGui::Selectable(label.c_str(), i == session_.selected())) session_.select(i);
        if (w.canvasHide) ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("L19").x);
        ImGui::TextDisabled("L%d", w.layer);
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

    // Input: the whole canvas is one button
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

    // Keyboard, while the canvas has focus and no field is being typed in
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused() && !io.WantTextInput) {
        int step = io.KeyShift ? 10 : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) session_.moveBy(-step, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) session_.moveBy(step, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) session_.moveBy(0, -step);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) session_.moveBy(0, step);
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) session_.removeSelected();
    }
    if (!io.WantTextInput && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) io.KeyShift ? session_.redo() : session_.undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) session_.redo();
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) job_ = Job::Save;
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
        if (w.canvasHide || i == session_.selected()) continue;
        const WidgetDef* def = session_.library().find(w.def);
        int x, y, ww, hh;
        canvasHitRect(w, x, y, ww, hh);
        bool hot = i == hoveredWidget;
        dl->AddRect(toStage(static_cast<float>(x), static_cast<float>(y)),
                    toStage(static_cast<float>(x + ww), static_cast<float>(y + hh)),
                    colourOf(def ? def->color : "", hot ? 230 : 120), 0.0f, 0, hot ? 2.0f : 1.0f);
    }
    if (const WidgetInstance* sel = session_.selectedWidget()) {
        const WidgetDef* def = session_.selectedDef();
        DragRect r;
        if (session_.dragMoved()) r = session_.dragRect();
        else canvasHitRect(*sel, r.x, r.y, r.w, r.h);
        ImVec2 p1 = toStage(static_cast<float>(r.x), static_cast<float>(r.y));
        ImVec2 p2 = toStage(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h));
        dl->AddRectFilled(p1, p2, colourOf(def ? def->color : "", 40));
        dl->AddRect(p1, p2, colourOf(def ? def->color : "", 255), 0.0f, 0, 2.0f);
        for (int hnd = 0; hnd < 8; ++hnd) {
            float hx, hy;
            handlePoint(r, hnd, hx, hy);
            ImVec2 c = toStage(hx, hy);
            float hs = 4.0f * ctx_.scale;
            dl->AddRectFilled(ImVec2(c.x - hs, c.y - hs), ImVec2(c.x + hs, c.y + hs), IM_COL32(255, 255, 255, 255));
            dl->AddRect(ImVec2(c.x - hs, c.y - hs), ImVec2(c.x + hs, c.y + hs), IM_COL32(0, 0, 0, 255));
        }
        std::string label = sel->name;
        if (session_.dragMoved()) {
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
    dl->PopClipRect();
}

void ThemeEditorScreen::drawProperties()
{
    const WidgetInstance* w = session_.selectedWidget();
    const WidgetDef* def = session_.selectedDef();
    if (!w) {
        ImGui::SeparatorText("Widget");
        ImGui::TextWrapped("Click a widget on the canvas or in the list to change it, or choose Add widget.");
        return;
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
    if (ImGui::InputText("Name", &name)) { session_.setCoalesceKey("name"); session_.setName(name); }
    endEdit();

    int rect[4] = { w->x, w->y, w->w, w->h };
    if (ImGui::InputInt("X", &rect[0], 5, 50)) { session_.setCoalesceKey("x"); session_.setRect(rect[0], rect[1], rect[2], rect[3]); }
    endEdit();
    if (ImGui::InputInt("Y", &rect[1], 5, 50)) { session_.setCoalesceKey("y"); session_.setRect(rect[0], rect[1], rect[2], rect[3]); }
    endEdit();
    if (ImGui::InputInt("Width", &rect[2], 5, 50)) { session_.setCoalesceKey("w"); session_.setRect(rect[0], rect[1], rect[2], rect[3]); }
    endEdit();
    if (ImGui::InputInt("Height", &rect[3], 5, 50)) { session_.setCoalesceKey("h"); session_.setRect(rect[0], rect[1], rect[2], rect[3]); }
    endEdit();

    int layer = w->layer;
    if (ImGui::SliderInt("Layer", &layer, 0, MAX_LAYER)) { session_.setCoalesceKey("layer"); session_.setLayer(layer); }
    endEdit();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 is drawn first, 19 on top");
    int alpha = w->alphaPct;
    if (ImGui::SliderInt("Opacity %", &alpha, 0, 100)) { session_.setCoalesceKey("alpha"); session_.setAlpha(alpha); }
    endEdit();

    if (def && usesFontColour(*def)) {
        float rgb[3];
        hexToFloats(w->fontColor.empty() ? "FFFFFF" : w->fontColor, rgb);
        if (ImGui::ColorEdit3("Text colour", rgb, ImGuiColorEditFlags_NoInputs)) {
            session_.setCoalesceKey("fontColor");
            session_.setFontColor(floatsToHex(rgb));
        }
        endEdit();
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
    if (ImGui::Button("Delete widget")) session_.removeSelected();
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
        if (ImGui::BeginCombo(label, shown.c_str())) {
            for (const std::string& option : var.options) {
                std::string optionValue = option == "-" ? std::string() : option;
                std::string optionLabel = optionValue.empty() ? "(none)" : optionValue;
                if (ImGui::Selectable(optionLabel.c_str(), optionValue == value)) session_.setVar(var.token, optionValue);
            }
            ImGui::EndCombo();
        }
        break;
    }
    case VarKind::Number: {
        double number = value.empty() ? 0.0 : std::atof(value.c_str());
        if (ImGui::InputDouble(label, &number, var.step, var.step * 10.0, "%g")) {
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
        ImGui::SameLine();
        if (value.empty()) ImGui::TextDisabled("(auto)");
        else if (ImGui::SmallButton("Clear")) session_.setVar(var.token, "");
        break;
    }
    case VarKind::File: {
        std::string text = value;
        float button = ImGui::CalcTextSize("Choose...").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - button);
        if (ImGui::InputText("##file", &text)) { session_.setCoalesceKey("var"); session_.setVar(var.token, text); }
        if (ImGui::IsItemDeactivated()) session_.endCoalesce();
        ImGui::SameLine();
        ImGui::BeginDisabled(!FileDialog::isSupported());
        if (ImGui::Button("Choose...")) { job_ = Job::PickFile; jobToken_ = var.token; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        break;
    }
    case VarKind::Raw: {
        std::string text = value;
        if (ImGui::InputTextMultiline(label, &text, ImVec2(0, ImGui::GetTextLineHeight() * 4.0f))) {
            session_.setCoalesceKey("var");
            session_.setVar(var.token, text);
        }
        if (ImGui::IsItemDeactivated()) session_.endCoalesce();
        break;
    }
    case VarKind::Text: {
        std::string text = value;
        if (ImGui::InputText(label, &text)) { session_.setCoalesceKey("var"); session_.setVar(var.token, text); }
        if (ImGui::IsItemDeactivated()) session_.endCoalesce();
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
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##filter", "Search widgets", &addFilter_);
    std::string filter = Utils::toLower(addFilter_);

    ImGui::BeginChild("##defs", ImVec2(0, 0), ImGuiChildFlags_None);
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
            if (ImGui::Selectable(def.name.c_str())) {
                session_.addWidget(def.id);
                showAdd_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(260.0f * ctx_.scale);
            ImGui::TextDisabled("%s", def.summary.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void ThemeEditorScreen::drawPopups()
{
    drawAddWidget();

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
        if (ImGui::Button("OK")) {
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
        if (ImGui::Button("Save")) {
            job_ = Job::SaveAndClose;
            showClose_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) {
            leaveWithoutSaving_ = true;
            back_ = true;
            showClose_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            showClose_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

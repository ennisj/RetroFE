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

#include "ThemeEditorScreen.h"
#include "../../../Graphics/PageBuilder.h"
#include "../../../Graphics/ViewInfo.h"
#include "../../../ThemeEditor/LayoutSession.h"
#include <memory>
#include <string>
#include <vector>

// The theme editor's layout-native mode: an ImGui screen (form
// "theme-layout-editor", params id = the theme's folder, page = the page)
// that edits a hand-made theme's pages in place (ThemeEditor/LayoutSession).
// Left: the theme's pages and the page's elements. Middle: the page as
// RetroFE's PageBuilder builds it from the edited text, with every element's
// box over it (PageBuilder::setTrace says which element each component came
// from). Right: the selected element's attributes.
//
// It moves, resizes and changes attributes of the elements a page already
// has, and can duplicate or delete one; it doesn't add new elements. Saving
// changes only the bytes edited and keeps the original in manage/backups.
//
// As in the widget editor, the preview page is built in update(), never
// while drawing, the `layout` setting points at the theme while it's open,
// and -editorScript runs here too (the layout commands in EditorScript.h
// plus the screen's own).
class LayoutEditorScreen : public Manager::ImGuiScreen
{
public:
    LayoutEditorScreen(const ThemeEditorScreen::Context& context, const std::string& folder, const std::string& page);
    ~LayoutEditorScreen() override;

    void update(float dt) override;
    void drawTargets() override;
    void draw() override;
    bool escape() override;
    bool askToLeave() override;
    std::string leaving() override;
    void discard() override;
    void setStatus(const std::string& message, bool isError) override;
    bool takeBack() override;
    void retire() override;
    void shutdown() override;

private:
    enum class Job { None, Save, SaveAndLeave, Restore };
    // Where the components came from, as traced, for the boxes
    struct Traced
    {
        std::string path;
        Component* component = nullptr;
        ViewInfo* slot = nullptr;
        ViewInfo base;            // as built, before any animation
        bool pending = false;     // its art wasn't loaded yet: its size follows when it is
    };
    // A box that's drawn but isn't its element's own (a vertical menu's other slots)
    struct ExtraBox
    {
        ThemeEditor::DragRect rect;
        std::string path;
    };

    bool openPage(const std::string& page);
    void rebuildPreview();
    void destroyPreview();
    bool ensureTarget();
    CollectionInfo* previewCollection();
    ThemeEditor::ElementGeom geomFrom(const ViewInfo& v, const std::string& path, int order) const;
    void captureGeometry();
    void refreshPending();
    void runJob();
    bool save();
    void restoreOriginal();
    void runScript();
    bool runCommand(const ThemeEditor::EditorCommand& command, std::string& note);
    void requestLeave(const std::string& toPage);

    void drawToolbar();
    void drawLeft();
    void drawCanvas();
    void drawProperties();
    void drawAttribute(const ThemeEditor::LayoutElement& e, const std::string& name, const std::string& resolved);
    void drawPopups();
    void syncPopup(const char* name, bool& show, bool& opened);

    ThemeEditorScreen::Context ctx_;
    std::string folder_;
    std::string openError_;
    ThemeEditor::LayoutSession session_;
    std::vector<std::string> pages_;
    std::vector<std::string> openIssues_;   // what the layout check said when the page was opened
    bool hasOriginal_ = false;

    // The preview
    Page* page_ = nullptr;
    SDL_Texture* target_ = nullptr;
    int targetW_ = 0, targetH_ = 0;
    std::vector<PageBuilder::TraceEntry> trace_;
    std::vector<Traced> traced_;
    std::vector<ExtraBox> extra_;
    std::string builtText_;
    std::string builtCollection_;
    unsigned builtRevision_ = 0;
    bool previewDirty_ = true;
    float sinceChange_ = 0.0f;
    unsigned seenRevision_ = 0;
    std::vector<std::string> engineIssues_;
    std::unique_ptr<CollectionInfo> master_;
    std::string masterName_;
    std::string previewName_;
    std::vector<std::string> collections_;
    bool showBoxes_ = true;

    std::string originalLayout_;
    bool layoutOverridden_ = false;
    ThemeEditor::CanvasFit fit_;

    Job job_ = Job::None;
    std::string pendingPage_;     // a page to open once the unsaved changes are answered
    std::string status_;
    bool statusIsError_ = false;
    std::string newName_, newValue_;
    bool showClose_ = false, closeOpen_ = false;
    bool showRestore_ = false, restoreOpen_ = false;
    bool showDelete_ = false, deleteOpen_ = false;
    bool back_ = false;
    bool leaveWithoutSaving_ = false;
    bool retired_ = false;
    bool navFlagsSaved_ = false, savedNavKeyboard_ = false;
    bool nudging_ = false;
    float sinceNudge_ = 0.0f;

    std::vector<ThemeEditor::EditorCommand> script_;
    std::string scriptPath_;
    size_t scriptAt_ = 0;
    int scriptWait_ = 0;
};

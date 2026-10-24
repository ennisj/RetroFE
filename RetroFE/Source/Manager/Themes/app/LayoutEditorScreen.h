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

#include "FileChooser.h"
#include "ThemeEditorScreen.h"
#include "../../../Graphics/PageBuilder.h"
#include "../../../Graphics/ViewInfo.h"
#include "../../../ThemeEditor/LayoutSession.h"
#include "../../../ThemeEditor/LayoutTwin.h"
#include "imgui.h"
#include <filesystem>
#include <map>
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
// It moves, resizes and changes attributes of the page's elements,
// duplicates or deletes one, adds new ones from a palette (asking for the
// file of an image or video, copied into the theme), and edits their
// animations (events, steps, changes), playing an event on the preview.
// Saving changes only the bytes edited and keeps the original in
// manage/backups. Several elements can be selected at once (moved, aligned,
// spaced, duplicated, deleted together). A page with a 4:3 twin (layout.xml
// and "layout 4x3.xml") can be edited together with it: the twin is a
// second LayoutSession, edits here are copied there (ThemeEditor/
// LayoutTwin.h) as linked undo steps, Show switches between the two with
// the same elements selected, and Save saves both.
//
// As in the widget editor, the preview page is built in update(), never
// while drawing, the `layout` setting points at the theme while it's open,
// -editorScript runs here too (the layout commands in EditorScript.h plus
// the screen's own), and RetroFE's mapped controls drive it (EditorPad.h):
// the regions, the canvas's Pick / Move / Resize modes over the elements and
// menu items, the dialogs and the on-screen keyboard.
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
    bool wantsControls() const override;
    void controlsHeld(const ControlsHeld& held) override;
    void control(Control control, bool repeat) override;
    void beforeFrame() override;

private:
    enum class Job { None, Save, SaveAndLeave, Restore, Import, NativePick };
    // What a chosen file is for: a new element (from the palette), or the src of an element
    struct FileFor
    {
        bool addElement = false;
        std::string entry, type;   // the palette entry and its type (addElement)
        std::string path;          // the element whose src it becomes (otherwise)
        std::string kind;          // image, video, sound
    };
    // Where the components came from, as traced, for the boxes
    struct Traced
    {
        std::string path;
        Component* component = nullptr;
        ViewInfo base;            // as built, before any animation
        int order = 0;            // its place in the trace
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
    static ThemeEditor::TracedView viewOf(const ViewInfo& v, const std::string& path, const void* component, bool slot);
    // Opens another page from update() only (the old page's preview is freed first)
    void switchPage(const std::string& page);
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
    void drawShows(int element);
    void drawAnimations(int element);
    bool drawAnimField(int element, const char* name, const char* hint, float width);
    bool drawAnimChoice(int element, const char* name, const std::vector<std::string>& choices, const char* none, float width);
    void drawPalette();
    void drawPopups();
    void drawDashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 colour, float dash);
    // Files
    void chooseFile(const FileFor& what);
    bool importChosen(const std::string& path, const FileFor& what);
    void pickNative();
    void addFromPalette(const std::string& entry, const std::string& type);
    void syncPopup(const char* name, bool& show, bool& opened);
    // Several selected: align, distribute, duplicate, delete
    void drawSelectionTools();
    void drawSharedAttributes();   // what they all have; a change goes to every one
    void alignSelection(int op);
    void distributeSelection(bool horizontal);
    // The page's twin (layout.xml / layout 4x3.xml), edited together
    std::string twinName() const;
    bool openTwin();
    void closeTwin();
    void syncTwin();              // copies this page's edits since the last sync to the twin
    void switchToTwin();          // from update(): the twin becomes the page shown, the same elements selected
    void undoBoth();              // undo / redo here, and the twin's linked step with it
    void redoBoth();
    bool anyDirty() const;
    void drawTwinInfo(int element);
    // An operation made on this page and, with "Also apply" on, on the
    // twin's own elements too (align and distribute from its own boxes;
    // adding, duplicating and deleting at the matching place), one undo
    // step; `what` names it for the note. True when either page changed.
    bool onBoth(const ThemeEditor::TwinOp& op, const std::string& what);
    bool alsoOnTwin() const { return alsoTwin_ && twin_.isOpen(); }
    // The structural edits, through onBoth
    bool duplicateSelected();
    bool deleteSelected();
    bool addElement(const std::string& entry, const std::string& type, const std::string& src = "");
    // destructive: it removes or replaces animations (only where the element is surely the same there)
    bool animationEdit(int element, const std::function<bool(ThemeEditor::LayoutSession&, int)>& edit, const std::string& what,
                       bool destructive = false);
    // A script waits for this before its next command: the preview is
    // rebuilt after the last change, nothing is being opened or saved
    bool scriptBusy() const;
    // The controller
    bool dialogOpen() const;
    void canvasControl(int press, bool released = false);
    void heldControl(int press);
    void setMode(ThemeEditor::EdCanvasMode mode);
    std::string controllerHints() const;

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
    std::string openNext_;        // a page to open on the next update()
    std::string status_;
    bool statusIsError_ = false;
    std::string newName_, newValue_;
    bool showClose_ = false, closeOpen_ = false;
    bool showRestore_ = false, restoreOpen_ = false;
    bool showDelete_ = false, deleteOpen_ = false;
    bool back_ = false;
    bool leaveWithoutSaving_ = false;
    bool retired_ = false;
    bool nudging_ = false;
    float sinceNudge_ = 0.0f;

    // RetroFE's mapped controls
    Themes::EditorPad pad_;
    ThemeEditor::EdCanvasMode mode_ = ThemeEditor::ECM_PICK;
    int repeats_ = 0;
    ThemeEditor::HeldSelect heldSelect_;     // Select held in Pick: adds to the selection
    int resizeHandle_ = ThemeEditor::CH_SE;  // the handle Resize mode drags

    // The element list follows a selection made elsewhere (the canvas, the controller)
    int listPrimary_ = -1;
    bool listClicked_ = false;

    // The twin page (layout.xml and layout 4x3.xml)
    ThemeEditor::LayoutSession twin_;
    std::vector<std::string> twinIssues_;   // the layout check when the twin was opened
    bool alsoTwin_ = false;      // edits here are copied to the twin
    bool switchTwin_ = false;    // switch on the next update()
    ThemeEditor::TwinSync twinSync_;   // what has been copied to the twin so far
    std::string twinNote_;       // what the last copy did

    // Adding elements and files
    bool showPalette_ = false, paletteOpen_ = false;
    std::map<std::string, std::string> paletteTypes_;   // the type chosen for each typed entry
    Themes::FileChooser chooser_;
    FileFor chooserFor_;
    FileFor jobFor_;
    std::string jobPath_;
    std::filesystem::path lastBrowseDir_;

    // Animations
    std::string newEvent_ = "onEnter";
    std::string copySource_;      // the path of the element to copy animations from
    std::string replay_;          // an event to play on the preview (engine name), from update()
    bool showEnd_ = true;         // dashed boxes where an event leaves elements
    std::string endEvent_ = "onEnter";   // which event: onEnter, onMenuScroll..., or "*" for every one
    bool scrollToAnimations_ = false;   // a script's showanims: the panel scrolls to Animations
    std::string openEvent_;       // a script's openevent: the event (full path) to unfold

    std::vector<ThemeEditor::EditorCommand> script_;
    std::string scriptPath_;
    size_t scriptAt_ = 0;
    int scriptWait_ = 0;
    int scriptIdleFrames_ = 0;    // frames waited for the preview since the last command (capped)

    // Alignment guides while dragging (Alt held: none)
    bool guidesOn_ = true;
};

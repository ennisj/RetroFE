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

// What the theme editor screen does, without SDL or ImGui: the open theme,
// the page being edited, the selection, pointer drags on the canvas, edits,
// undo/redo and saving. The screen (Manager/Themes/app/ThemeEditorScreen)
// only draws this and feeds it the user's input, so every decision here is
// tested by retrofe_tests.
//
// Every change goes through one place: it takes an undo snapshot (the whole
// project, which is small), applies the change and bumps revision(). The
// screen regenerates and rebuilds the preview page when the revision moves,
// outside the render pass.
//
// Pointer protocol (stage coordinates, i.e. pixels inside the canvas, with
// the fit that maps them to the theme):
//   pointerDown  on the selected widget's handle: resize; on its body: move,
//                and a click without moving selects the next widget under
//                the point (click-through); elsewhere: select what's there
//                (the top one) and move it
//   pointerMove  only the wireframe follows (dragRect)
//   pointerUp    commits, snapped, as one undo step; a drag that ends where
//                it started changes nothing
// Escape cancels a drag (cancelDrag).
//
// Several widgets can be selected (selection(); the last is the primary,
// the one the properties panel shows): Ctrl/Shift-click toggles one, a drag
// that starts on empty canvas (or with Ctrl/Shift anywhere) draws a marquee
// that selects the widgets wholly inside it, and a drag on any selected
// widget moves them all. Nudges, delete, duplicate, align and distribute
// act on all of them, each one undo step. Resizing is one widget's (the
// primary's): with several selected there are no handles.

#include "EditorCanvas.h"
#include "EditorSelection.h"
#include "ThemeGenerate.h"
#include "ThemeProject.h"
#include "ThemeStore.h"
#include "WidgetDefs.h"
#include <functional>
#include <string>
#include <vector>

namespace ThemeEditor
{
    // How the properties panel edits a widget variable
    enum class VarKind { Bool, Choice, Number, Colour, File, Text, Raw };
    VarKind varKind(const WidgetVar& var);

    // "FFAA00" for "#ffaa00" / "ffaa00", "" for anything that isn't a colour
    std::string normalizeColour(const std::string& text);

    class EditorSession
    {
    public:
        // Opens layouts/<folder>. False, with a reason, for a folder that
        // isn't editor-made or whose project doesn't load.
        bool open(const std::string& layoutsRoot, const std::string& folder, const WidgetLibrary& lib,
                  std::string& error);
        bool isOpen() const { return lib_ != nullptr; }

        const ThemeProject& project() const { return project_; }
        const std::string& folder() const { return folder_; }
        const std::string& layoutsRoot() const { return layoutsRoot_; }
        const WidgetLibrary& library() const { return *lib_; }

        // Pages: 0 is the Default page, then templates, then the splash page
        int pageCount() const { return static_cast<int>(project_.pages.size()); }
        int pageIndex() const { return page_; }
        void setPage(int index);
        const ThemePage& page() const { return project_.pages[page_]; }
        // A page's label: "Default", "Arcade (also NES, SNES)", "Splash"
        std::string pageLabel(int index) const;
        // The page's folder relative to the theme: "" for the Default and
        // splash pages, "collections/<c>/layout" for a template
        std::string pageFolder() const;

        // Selection on the current page: the primary (-1 = none) and every
        // selected widget, the primary last. select() makes it just one.
        int selected() const { return selected_; }
        void select(int index);
        bool selectId(const std::string& id);
        const WidgetInstance* selectedWidget() const;
        const WidgetDef* selectedDef() const;
        const std::vector<int>& selection() const { return selection_; }
        int selectionCount() const { return static_cast<int>(selection_.size()); }
        bool isSelected(int index) const;
        // Several at once (the last one the primary); indices that aren't widgets are dropped
        void setSelection(const std::vector<int>& indices);
        void toggleSelected(int index);   // Ctrl-click
        void addSelected(int index);      // joins as the primary
        bool dropPrimary();               // the primary leaves; the one picked before it is the primary
        void selectAll();                 // every widget not hidden in the editor
        // The selected widgets' boxes (canvasHitRect), in selection() order
        std::vector<DragRect> selectionBoxes() const;

        // The canvas. additive: Ctrl or Shift was held (toggle, or a marquee that adds)
        void pointerDown(const CanvasFit& fit, float sx, float sy, float grabPx, bool additive = false);
        void pointerMove(const CanvasFit& fit, float sx, float sy);
        void pointerUp();
        void cancelDrag();
        bool dragging() const { return drag_.active; }
        bool dragMoved() const { return drag_.active && drag_.moved; }
        int dragHandle() const { return drag_.handle; }
        // While dragging, where the wireframe is drawn (theme units); for a
        // group, the box around them
        DragRect dragRect() const { return drag_.rect; }
        // A drag of several: every selected widget's wireframe moves by this
        bool groupDrag() const { return drag_.active && drag_.group; }
        int dragDx() const { return drag_.rect.x - drag_.start.x; }
        int dragDy() const { return drag_.rect.y - drag_.start.y; }
        // A marquee being drawn, theme units
        bool marqueeActive() const { return drag_.active && drag_.marquee; }
        DragRect marqueeBox() const;
        // Snapping to the 5-unit grid; off moves by whole units
        bool snap = true;
        // Alignment guides while dragging (EditorSelection.h), within
        // guideReachPx screen pixels; off unless the screen turns them on
        bool guides = false;
        float guideReachPx = 6.0f;
        const std::vector<GuideLine>& guideLines() const { return guideLines_; }

        // Edits on the selection; false when nothing changed
        bool moveBy(int dx, int dy);   // keyboard nudge, not snapped; a group stays on the canvas as one box
        // Moves the primary so its box (canvasHitRect) starts at x, y, and
        // the others with it, as a drag of its body does: a warped widget
        // keeps its corners' shape. Coalesces with the current key (setCoalesceKey).
        bool moveTo(int x, int y);
        // Every selected widget moves by dx, dy (as is: no snapping or
        // clamping). Coalesces with the current key.
        bool shiftSelection(int dx, int dy);
        // Copies of the selected widgets, 20 units down and right (up and
        // left where that's off the canvas), which become the selection
        bool duplicateSelected();
        // Lines the selected widgets up on the box around them (AlignOp,
        // EditorSelection.h), or spaces three or more evenly
        bool alignSelected(int op);
        bool distributeSelected(bool horizontal);
        // Sets the box; a warped widget becomes a plain box (as a resize drag does)
        bool setRect(int x, int y, int w, int h);
        bool setName(const std::string& name);
        bool setLayer(int layer);      // clamped to 0..MAX_LAYER
        bool setAlpha(int pct);        // clamped to 0..100
        bool setFontColor(const std::string& hex);   // "" clears
        bool setVar(const std::string& token, const std::string& value);
        bool resetVar(const std::string& token);
        bool setCanvasHidden(bool hidden);
        // The same on every selected widget, one undo step (typing coalesces
        // as the single ones do). A setting only some of them have (a widget
        // variable another kind of widget hasn't) changes where it's had. A
        // colour variable takes only a whole colour (normalised) or empty.
        bool setLayerAll(int layer);
        bool setAlphaAll(int pct);
        bool setFontColorAll(const std::string& hex);
        bool setVarAll(const std::string& token, const std::string& value);
        bool resetVarAll(const std::string& token);
        // What the selected widgets have: layer, opacity, text colour (only
        // when every one uses one), and each variable every one's kind of
        // widget has (by token and the same way of editing it), in the
        // primary's order. `name` is "layer", "alpha", "fontColor" or a token.
        struct SharedSetting
        {
            std::string name;
            const WidgetVar* var = nullptr;   // the primary's, for a variable
            SharedValue value;
        };
        std::vector<SharedSetting> sharedSettings() const;
        bool removeSelected();   // every selected widget
        // Places a widget from the library, centred, and selects it
        bool addWidget(const std::string& defId);

        // Theme properties
        bool setThemeName(const std::string& name);
        bool setVertical(bool vertical);   // reflows every page
        // A font in the theme ("fonts/x.ttf"); "" = the bundled Roboto
        bool setThemeFont(const std::string& rel);
        // kind: load, unload, highlight or select; rel "" = none
        bool setSound(const std::string& kind, const std::string& rel);
        std::string sound(const std::string& kind) const;

        // The splash page (RetroFE shows it while it starts). Without one,
        // saving writes a page with the theme's name. Setting its text or
        // background makes one (the name, centred) if there's none yet.
        bool hasSplashPage() const;
        bool makeSplashPage();
        std::string splashText() const;        // its first Static Text; the theme's name without a page
        std::string splashBackground() const;  // its background file, "" = none
        bool setSplashText(const std::string& text);
        // An image or a video (by extension) filling the page, under the
        // text; "" removes it
        bool setSplashBackground(const std::string& rel);

        // Collection templates. A template is a page for named collections
        // (ThemePage: its primary collection plus members); every collection
        // not named uses the Default page (or, for menus of collections, the
        // "_menu" template when there is one: pageShownFor).
        static const char* const BLANK;   // basedOn: an empty page
        // Why a template can't be made for a collection with this name; ""
        // when it can. The collection must be a safe folder name and mustn't
        // have a page written by hand in the theme.
        std::string templateProblem(const std::string& collection, const std::string& name) const;
        // A new template for `collection`, a copy of basedOn ("" = the
        // Default page, BLANK, or a template's primary), which becomes the
        // page being edited. The collection leaves the template it used.
        // alsoFor: more collections that join it (e.g. the ones under a
        // menu), in the same undo step; ones that can't are skipped.
        bool newTemplate(const std::string& collection, const std::string& name, const std::string& basedOn,
                         const std::vector<std::string>& alsoFor = {});
        // Puts a collection on a template (by primary; "" = the Default page)
        bool assignTemplate(const std::string& collection, const std::string& primary);
        // Several at once, one undo step. `skipped` counts the ones that
        // can't join (a page written by hand, a name that can't be a folder).
        bool assignTemplate(const std::vector<std::string>& collections, const std::string& primary, int* skipped = nullptr);
        // The template that moving `collection` off its page would delete
        // (it's that template's only collection), else ""
        std::string removesTemplate(const std::string& collection) const;
        // Deletes a template; its collections move to replacement ("" = Default)
        bool deleteTemplate(const std::string& primary, const std::string& replacement);
        bool renameTemplate(const std::string& primary, const std::string& name);
        // The templates' primaries, in page order
        std::vector<std::string> templates() const;
        // Is collections/<c>/layout/layout.xml in the theme a page written by hand?
        bool handMadePage(const std::string& collection) const;

        // The selected widget's file settings (images, videos, text files)
        // whose file isn't in the theme folder: what to ask for after placing it
        std::vector<std::string> missingFiles() const;
        // Is there a file at this path in the theme folder?
        bool themeHasFile(const std::string& rel) const;

        // Consecutive edits with the same key (typing in a field, dragging a
        // slider) are one undo step; any other change starts a new one
        void setCoalesceKey(const std::string& key) { coalesce_ = key; }
        // The field was let go of: its next edit is a new undo step
        void endCoalesce() { coalesce_.clear(); lastChangeKey_.clear(); }

        bool canUndo() const { return !undo_.empty(); }
        bool canRedo() const { return !redo_.empty(); }
        bool undo();
        bool redo();

        // Changed since opened or last saved
        bool dirty() const { return revision_ != savedRevision_; }
        // Moves on every change, undo and redo, so the preview knows to rebuild
        unsigned revision() const { return revision_; }

        // The current page as the preview builds it: canvasPreview, with the
        // Show toggles in `show`, pointed at this theme's folder
        GenResult previewXml(const GenOptions& show) const;

        // Saves into the theme's folder (ThemeStore::saveTheme)
        bool save(const SaveInputs& inputs, SaveReport& report, std::string& error);

        static const size_t UNDO_LIMIT = 100;

    private:
        struct Snapshot
        {
            ThemeProject project;
            int page = 0;
            int selected = -1;
            std::vector<int> selection;
            unsigned revision = 0;
        };
        struct Drag
        {
            bool active = false;
            bool moved = false;
            bool cycleOnClick = false;   // began on the selected widget's body
            bool group = false;          // several selected widgets move together
            bool marquee = false;        // a marquee from empty canvas
            bool additive = false;       // Ctrl / Shift: the marquee adds to the selection
            int clicked = -1;            // a group drag: the widget it began on (a click selects just it)
            int handle = CH_NONE;
            float startSx = 0, startSy = 0;
            float startTx = 0, startTy = 0;
            float endTx = 0, endTy = 0;  // a marquee's other corner
            DragRect start;
            DragRect rect;
        };

        WidgetInstance* selectedMutable();
        // Applies `edit` to every selected widget (with its definition); one
        // undo step when any of them changed
        bool editEach(const std::string& key, const std::function<bool(WidgetInstance&, const WidgetDef*)>& edit);
        // Moves widget i by dx, dy (a warp's corners too), no undo step
        void shiftWidget(int i, int dx, int dy);
        // Sets the selection list and the primary from it
        void assignSelection(const std::vector<int>& indices);
        // Takes an undo snapshot (unless coalescing) before a change
        void beginChange(const std::string& key);
        void changed();
        Snapshot snapshot() const;
        void restore(const Snapshot& s);
        void clampSelection();
        // After pages were added or removed: the splash page goes last, and
        // the page being edited is found again (by a collection it covered)
        void pagesChanged(const std::vector<std::string>& covered, const std::string& goTo);
        std::vector<std::string> coveredByCurrent() const;
        // A collection on a template whose name differs from this one only in case, else ""
        std::string caseClash(const std::string& collection) const;
        // The template with this name (ignoring case), leaving out the one whose primary is `except`
        const ThemePage* templateNamed(const std::string& name, const std::string& except) const;
        WidgetInstance* splashWidget(const std::string& def);

        const WidgetLibrary* lib_ = nullptr;
        std::string layoutsRoot_;
        std::string folder_;
        ThemeProject project_;
        int page_ = 0;
        int selected_ = -1;
        std::vector<int> selection_;   // every selected widget, selected_ last
        Drag drag_;
        std::vector<GuideLine> guideLines_;
        std::vector<Snapshot> undo_;
        std::vector<Snapshot> redo_;
        std::string coalesce_;
        std::string lastChangeKey_;
        unsigned revision_ = 0;
        unsigned savedRevision_ = 0;
        unsigned nextRevision_ = 1;
    };
}

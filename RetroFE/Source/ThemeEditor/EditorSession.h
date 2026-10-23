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

#include "EditorCanvas.h"
#include "ThemeGenerate.h"
#include "ThemeProject.h"
#include "ThemeStore.h"
#include "WidgetDefs.h"
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

        // Selection on the current page; -1 = none
        int selected() const { return selected_; }
        void select(int index);
        bool selectId(const std::string& id);
        const WidgetInstance* selectedWidget() const;
        const WidgetDef* selectedDef() const;

        // The canvas
        void pointerDown(const CanvasFit& fit, float sx, float sy, float grabPx);
        void pointerMove(const CanvasFit& fit, float sx, float sy);
        void pointerUp();
        void cancelDrag();
        bool dragging() const { return drag_.active; }
        bool dragMoved() const { return drag_.active && drag_.moved; }
        int dragHandle() const { return drag_.handle; }
        // While dragging, where the wireframe is drawn (theme units)
        DragRect dragRect() const { return drag_.rect; }
        // Snapping to the 5-unit grid; off moves by whole units
        bool snap = true;

        // Edits on the selected widget; false when nothing changed
        bool moveBy(int dx, int dy);   // keyboard nudge, not snapped
        bool setRect(int x, int y, int w, int h);
        bool setName(const std::string& name);
        bool setLayer(int layer);      // clamped to 0..MAX_LAYER
        bool setAlpha(int pct);        // clamped to 0..100
        bool setFontColor(const std::string& hex);   // "" clears
        bool setVar(const std::string& token, const std::string& value);
        bool resetVar(const std::string& token);
        bool setCanvasHidden(bool hidden);
        bool removeSelected();
        // Places a widget from the library, centred, and selects it
        bool addWidget(const std::string& defId);

        // Theme properties
        bool setThemeName(const std::string& name);
        bool setVertical(bool vertical);   // reflows every page

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
            unsigned revision = 0;
        };
        struct Drag
        {
            bool active = false;
            bool moved = false;
            bool cycleOnClick = false;   // began on the selected widget's body
            int handle = CH_NONE;
            float startSx = 0, startSy = 0;
            float startTx = 0, startTy = 0;
            DragRect start;
            DragRect rect;
        };

        WidgetInstance* selectedMutable();
        // Takes an undo snapshot (unless coalescing) before a change
        void beginChange(const std::string& key);
        void changed();
        Snapshot snapshot() const;
        void restore(const Snapshot& s);
        void clampSelection();

        const WidgetLibrary* lib_ = nullptr;
        std::string layoutsRoot_;
        std::string folder_;
        ThemeProject project_;
        int page_ = 0;
        int selected_ = -1;
        Drag drag_;
        std::vector<Snapshot> undo_;
        std::vector<Snapshot> redo_;
        std::string coalesce_;
        std::string lastChangeKey_;
        unsigned revision_ = 0;
        unsigned savedRevision_ = 0;
        unsigned nextRevision_ = 1;
    };
}

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

// The theme editor's layout-native mode, without SDL or ImGui: one page file
// of a hand-made theme (Aeon Nox, say), edited in place. It moves, resizes
// and changes the attributes of the elements the page already has, and can
// duplicate or delete one; it never adds a widget or rewrites the file.
// Every edit changes the file's text at exactly the bytes concerned
// (LayoutText.h), so comments, spacing, attribute order, quotes and line
// endings survive a save untouched.
//
// Where elements are on the page comes from the engine: the screen builds
// the page with PageBuilder (tracing which element each component came
// from) and hands the session each element's geometry (setGeometry). Moving
// or resizing turns the box back into attribute values:
//   move    x and y become numbers (x="center" stays as it is unless the
//           element is dragged sideways). An element that sets xOffset but
//           not x (menu items, whose x comes from <itemDefaults>) moves by
//           its xOffset instead; the same for y / yOffset.
//   resize  width and/or height become numbers (only the ones that
//           changed), and x / y follow so the edges not dragged stay put.
//
// The pointer protocol is EditorSession's (EditorCanvas.h): down on the
// selected element's handle resizes, on its body moves (a click without
// moving picks the next element underneath), elsewhere selects the top one.
//
// Saving writes the file atomically. Before the first save changes it, the
// original is kept under <root>/manage/backups/layouts/<theme>/<page>
// (Library::Backups, as every Manage screen does), and restoreOriginal()
// puts it back. Only files inside layouts/<theme> are ever written, and a
// page the widget editor wrote (it carries the editor's marker) isn't
// opened here: the widget editor owns it.

#include "EditorCanvas.h"
#include "LayoutText.h"
#include <map>
#include <string>
#include <vector>

namespace ThemeEditor
{
    // An element's box as the engine built it, theme units. The drawn box is
    // left = x + xOffset - xOrigin * w, top = y + yOffset - yOrigin * h.
    struct ElementGeom
    {
        float x = 0, y = 0;               // x and y as resolved ("center" -> 960)
        float xOffset = 0, yOffset = 0;
        float xOrigin = 0, yOrigin = 0;   // fractions of the drawn size
        float w = 0, h = 0;               // the drawn size
        int layer = 0;
        int order = 0;                    // the order the engine built it in (later on top within a layer)
        bool approximate = false;         // the size was guessed (text without a width, art not loaded yet)
        bool readOnly = false;            // shown, not editable (a vertical menu's generated slots)
        int left() const { return roundDelta(x + xOffset - xOrigin * w); }
        int top() const { return roundDelta(y + yOffset - yOrigin * h); }
    };

    class LayoutSession
    {
    public:
        // The pages a theme folder has that this mode edits, relative to it
        // with '/': layout.xml and its variants ("layout 4x3.xml",
        // "layout - 1.xml", "16x9layout.xml"), splash.xml,
        // collections/<c>/layout/*.xml and forms/**/*.xml
        static std::vector<std::string> themePages(const std::string& themeDir);

        // Opens layouts/<folder>/<page> under the RetroFE folder `root`.
        // False, with a reason, for a page outside the theme folder, one that
        // doesn't parse, or one the widget editor wrote.
        bool open(const std::string& root, const std::string& folder, const std::string& page, std::string& error);
        bool isOpen() const { return open_; }

        const std::string& root() const { return root_; }
        const std::string& folder() const { return folder_; }
        const std::string& page() const { return page_; }      // "collections/NES/layout/layout.xml"
        std::string themeDir() const;
        std::string file() const;                                // the page's full path
        std::string pageDir() const;                             // the folder its relative paths resolve against

        const LayoutDoc& doc() const { return doc_; }
        const std::string& text() const { return doc_.text; }
        const LayoutElement& element(int i) const { return doc_.elements[i]; }
        int elementCount() const { return static_cast<int>(doc_.elements.size()); }
        // The page's design size: the root's width and height when they're
        // numbers, else what the screen set (the engine's, for "stretch")
        int canvasW() const;
        int canvasH() const;
        void setCanvasSize(int w, int h) { engineW_ = w; engineH_ = h; }

        // What the element list shows, in document order: the root, its
        // children, and a menu's <itemDefaults> and <item>s under it
        std::vector<int> listed() const;
        // A label for the list: "image (images/topbar.png)", "menu item 3"
        std::string label(int element) const;
        // Animation events on an element (<onEnter> ...), with how many sets each has
        std::vector<std::string> animations(int element) const;
        // The view properties its animations change (x, y, alpha...), so the
        // panel can say that moving it changes where an animation starts
        std::vector<std::string> animatedProperties(int element) const;

        // Geometry, by element path, from the engine; cleared when paths change
        void clearGeometry() { geom_.clear(); }
        void setGeometry(const std::string& path, const ElementGeom& g) { geom_[path] = g; }
        const ElementGeom* geometry(int element) const;
        bool hasBox(int element) const { return geometry(element) != nullptr; }
        DragRect box(int element) const;
        bool editableBox(int element) const;

        // Canvas-only hiding, to reach what's underneath (not saved)
        bool hidden(int element) const;
        void setHidden(int element, bool hidden);

        // Selection; -1 = none
        int selected() const { return selected_; }
        void select(int element);
        bool selectPath(const std::string& path);
        const LayoutElement* selectedElement() const;

        // The element at a theme-space point (highest layer, then the last
        // built); -1 for none. pickNext: click-through from `current`.
        int pick(float tx, float ty) const;
        int pickNext(float tx, float ty, int current) const;

        // The canvas (see EditorSession)
        void pointerDown(const CanvasFit& fit, float sx, float sy, float grabPx);
        void pointerMove(const CanvasFit& fit, float sx, float sy);
        void pointerUp();
        void cancelDrag();
        bool dragging() const { return drag_.active; }
        bool dragMoved() const { return drag_.active && drag_.moved; }
        DragRect dragRect() const { return drag_.rect; }
        bool snap = true;

        // Edits on the selected element; false when nothing changed
        bool setAttribute(const std::string& name, const std::string& value);
        bool removeAttribute(const std::string& name);
        // Why a name can't be an attribute's ("" when it can)
        static std::string attributeNameProblem(const std::string& name);
        bool setLayer(int layer);            // clamped to 0..MAX_LAYER
        bool moveBy(int dx, int dy);         // a nudge, not snapped
        // The box moves to r (theme units), as a drag would leave it
        bool setBox(const DragRect& r, bool resize);
        bool duplicateSelected();
        bool deleteSelected();

        // Typing in one field is one undo step (EditorSession's coalescing)
        void setCoalesceKey(const std::string& key) { coalesce_ = key; }
        void endCoalesce() { coalesce_.clear(); lastKey_.clear(); }

        bool canUndo() const { return !undo_.empty(); }
        bool canRedo() const { return !redo_.empty(); }
        bool undo();
        bool redo();
        bool dirty() const { return doc_.text != saved_; }
        unsigned revision() const { return revision_; }
        // Moves when elements were added or removed (paths shifted)
        unsigned structure() const { return structure_; }

        // Writes the page (the original kept first); false, with a reason,
        // when the file changed on disk since it was read, or isn't inside
        // the theme folder
        bool save(std::string& error);
        // True once an original of this page is kept in the backups
        bool hasOriginal() const;
        std::string originalPath() const;
        // Puts the kept original back and reloads it (the current file is
        // kept beside it as a numbered copy). Clears the undo history.
        bool restoreOriginal(std::string& error);

        static const size_t UNDO_LIMIT = 100;

    private:
        struct Snapshot
        {
            std::string text;
            std::string selected;   // the selected element's path
        };
        struct Drag
        {
            bool active = false;
            bool moved = false;
            bool cycleOnClick = false;
            int handle = CH_NONE;
            float startSx = 0, startSy = 0;
            float startTx = 0, startTy = 0;
            DragRect start;
            DragRect rect;
        };

        // Replaces the text (one undo step unless coalescing) and reparses
        bool change(const std::string& text, const std::string& key, const std::string& selectAfter = "");
        bool load(const std::string& text, std::string& error);
        // Where a drag of the selected element left its box: write the attributes
        bool commitBox(const DragRect& from, const DragRect& to, bool resize);
        bool insideTheme(std::string& error) const;
        std::string selectedPath() const;

        bool open_ = false;
        std::string root_, folder_, page_;
        LayoutDoc doc_;
        std::string saved_;      // the text as it is on disk (read or last saved)
        std::map<std::string, ElementGeom> geom_;
        std::map<std::string, bool> hidden_;
        int selected_ = -1;
        int engineW_ = 0, engineH_ = 0;
        Drag drag_;
        std::vector<Snapshot> undo_, redo_;
        std::string coalesce_, lastKey_;
        unsigned revision_ = 0;
        unsigned structure_ = 0;
    };
}

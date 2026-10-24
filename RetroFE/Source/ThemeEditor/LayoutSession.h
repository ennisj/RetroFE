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
// and changes the attributes of the page's elements, duplicates or deletes
// one, adds new ones from a palette of raw elements, and edits their
// animations; it never adds a widget or rewrites the file.
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
// Several elements can be selected, as in EditorSession: Ctrl/Shift-click,
// a marquee from empty canvas, a drag on any of them moves them all; nudges,
// align, distribute, duplicate and delete act on all of them, each one undo
// step (one text change). Boxes that can't be edited (a vertical menu's
// slots) can be selected but don't move.
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
        // A box that isn't the element's own (a menu without a size: the
        // box around its slots). x, y... stay what the engine resolved.
        bool around = false;
        DragRect aroundBox;
        float exactLeft() const { return x + xOffset - xOrigin * w; }
        float exactTop() const { return y + yOffset - yOrigin * h; }
        int left() const { return around ? aroundBox.x : roundDelta(exactLeft()); }
        int top() const { return around ? aroundBox.y : roundDelta(exactTop()); }
    };

    // What the screen reads from a traced component (PageBuilder::setTrace)
    // to make its box: its ViewInfo as built, before anything animates.
    struct TracedView
    {
        std::string path;
        const void* component = nullptr;   // identifies the component (a menu and its slots share it)
        bool slot = false;                 // a menu slot, not the component itself
        float x = 0, y = 0, xOffset = 0, yOffset = 0, xOrigin = 0, yOrigin = 0;
        float scaledW = 0, scaledH = 0;    // ViewInfo::ScaledWidth/Height (0 or less: not known yet)
        float width = -1, height = -1;     // what the layout set (-1 = not set)
        float maxWidth = -1, maxHeight = -1;   // -1 = no maximum
        float fontSize = -1;
        int layer = 0;
    };

    // The boxes for a page, from what was traced, in build order: one per
    // element (the first component built from it) plus the extra boxes
    // drawn but not the element's own (a vertical menu's other slots).
    struct PageBoxes
    {
        std::map<std::string, ElementGeom> geometry;
        std::vector<std::pair<std::string, DragRect>> extra;
    };
    // Unknown sizes are guessed (and marked approximate): what the layout
    // set, else for text its font size and value's length, else 64. A slot
    // whose element isn't in the text (the empty <itemDefaults> PageBuilder
    // gives a menu without one) counts as its menu's. A vertical menu's
    // slots are read-only; a menu without a size gets the box around its
    // slots (around, read-only).
    PageBoxes pageBoxes(const LayoutDoc& doc, const std::vector<TracedView>& views);
    // One view's geometry (the guess included)
    ElementGeom geometryOf(const LayoutDoc& doc, const TracedView& v, int order);

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

        // Selection: the primary (-1 = none; 0 is the page itself) and every
        // selected element, the primary last. select() makes it just one.
        // The page itself is only ever selected alone.
        int selected() const { return selected_; }
        void select(int element);
        bool selectPath(const std::string& path);
        const LayoutElement* selectedElement() const;
        const std::vector<int>& selection() const { return selection_; }
        int selectionCount() const { return static_cast<int>(selection_.size()); }
        bool isSelected(int element) const;
        void setSelection(const std::vector<int>& elements);
        void toggleSelected(int element);   // Ctrl-click
        void addSelected(int element);      // joins as the primary
        bool dropPrimary();
        void selectAll();                   // every element with a box on the canvas, not hidden
        // The selected elements whose boxes can be moved, in selection() order
        std::vector<int> movableSelection() const;

        // The element at a theme-space point (highest layer, then the last
        // built); -1 for none. pickNext: click-through from `current`.
        int pick(float tx, float ty) const;
        int pickNext(float tx, float ty, int current) const;

        // The canvas (see EditorSession). additive: Ctrl or Shift was held
        void pointerDown(const CanvasFit& fit, float sx, float sy, float grabPx, bool additive = false);
        void pointerMove(const CanvasFit& fit, float sx, float sy);
        void pointerUp();
        void cancelDrag();
        bool dragging() const { return drag_.active; }
        bool dragMoved() const { return drag_.active && drag_.moved; }
        DragRect dragRect() const { return drag_.rect; }
        bool groupDrag() const { return drag_.active && drag_.group; }
        int dragDx() const { return drag_.rect.x - drag_.start.x; }
        int dragDy() const { return drag_.rect.y - drag_.start.y; }
        bool marqueeActive() const { return drag_.active && drag_.marquee; }
        DragRect marqueeBox() const;
        bool snap = true;

        // Edits on the selected element; false when nothing changed
        bool setAttribute(const std::string& name, const std::string& value);
        bool removeAttribute(const std::string& name);
        // Why a name can't be an attribute's ("" when it can)
        static std::string attributeNameProblem(const std::string& name);
        bool setLayer(int layer);            // clamped to 0..MAX_LAYER
        bool moveBy(int dx, int dy);         // a nudge of every movable selected element, not snapped
        // The box moves to r (theme units), as a drag would leave it
        bool setBox(const DragRect& r, bool resize);
        // Every movable selected element moves by dx, dy (as is); coalesces with the current key
        bool shiftSelection(int dx, int dy);
        // Every selected element (one inside another selected one goes with it)
        bool duplicateSelected();
        bool deleteSelected();
        // Lines the movable selected elements up on the box around them
        // (AlignOp, EditorSelection.h), or spaces three or more evenly; the
        // positions are written as a drag writes them
        bool alignSelected(int op);
        bool distributeSelected(bool horizontal);

        // Adding an element from the palette (LayoutPalette.h), as text:
        // after the selected element (the page element it's part of: a
        // menu's, for one of its items), else at the end of the page, with
        // its top-left corner placed so it sits in the middle of the page and
        // on the layer newElementLayer(). It's selected. `type` replaces the
        // entry's default type ("" keeps it), `src` is the file of an image
        // or video, as the page refers to it ("" leaves a placeholder name).
        bool addElement(const std::string& paletteId, const std::string& type = "", const std::string& src = "");
        // One above the highest layer an element of the page sets, at most MAX_LAYER
        int newElementLayer() const;
        // The folder (in the theme, '/' separated) a chosen file of this kind
        // ("image", "video", anything else) is copied into: the page's own
        // images/ (video/ or videos/ for videos) when its folder has one,
        // else the theme's, else assets/
        std::string importFolder(const std::string& kind) const;
        // The page's folder in the theme: "" for layout.xml, "collections/NES/layout"
        std::string pageFolder() const;

        // Animations. An element's events are its children named on...
        // (<onEnter>, <onMenuEnter>...), each holding <set duration=> steps
        // that play one after another, each holding <animate> changes that
        // play together. The edits change the text as the others do, and
        // take an event, set or step of the selected element (or the
        // element itself, for setAttributeOf); they keep the selection.
        std::vector<int> events(int element) const;
        std::vector<int> childrenNamed(int element, const std::string& name) const;
        // The events PageBuilder reads (LayoutCheck's list)
        static std::vector<std::string> eventNames();
        bool addEvent(const std::string& event);   // with one set and one step: alpha from 0 to 1 in 0.2 s
        bool addSet(int event);                    // a step after the last: alpha to 1 in 0.2 s
        bool addAnimate(int set);                  // another change in that step: alpha to 1
        bool removeAnimation(int element);         // an event, set or animate
        // An empty value inside an animation removes the attribute (PageBuilder
        // reads from="" / to="" as 0, not "where it is", and menuIndex="" as menu 0)
        bool setAttributeOf(int element, const std::string& name, const std::string& value);
        bool removeAttributeOf(int element, const std::string& name);
        // The selected element's events become copies of `source`'s (its own
        // go first); false when they'd be the same or source has none to give
        bool copyAnimationsFrom(int source);
        // Is `element` the selected element or inside it?
        bool insideSelected(int element) const;
        // A path relative to the selected element ("onEnter[0]/set[1]") to an element, else -1
        int findInSelected(const std::string& relativePath) const;

        // Where the element's box is once its onEnter has played (the last
        // value each animate of it gives x, y, xOffset, yOffset, width,
        // height, resolved as PageBuilder does), when that differs from the
        // box as placed: Aeon Nox's top bar sits above the page until its
        // onEnter brings y to 50. False when it has no box or doesn't move.
        bool enterEndBox(int element, DragRect& out) const;
        // The same for another event (onMenuScroll, onHighlightEnter...): the
        // box once its last step has played, starting from the box as placed
        bool eventEndBox(int element, const std::string& event, DragRect& out) const;
        // The element's events (by name, in file order, each once) whose end box isn't the placed box
        std::vector<std::string> movingEvents(int element) const;

        // Typing in one field is one undo step (EditorSession's coalescing)
        void setCoalesceKey(const std::string& key) { coalesce_ = key; }
        void endCoalesce() { coalesce_.clear(); lastKey_.clear(); }

        bool canUndo() const { return !undo_.empty(); }
        bool canRedo() const { return !redo_.empty(); }
        bool undo();
        bool redo();

        // Two pages edited together (layout.xml and layout 4x3.xml): a step
        // here and the step it caused on the other page carry the same link,
        // so the screen undoes and redoes them together. 0 = not linked.
        unsigned undoLink() const { return undo_.empty() ? 0 : undo_.back().link; }
        unsigned redoLink() const { return redo_.empty() ? 0 : redo_.back().link; }
        // Tags the step undo() would take back
        void linkUndo(unsigned link) { if (!undo_.empty()) undo_.back().link = link; }
        // That step's identity (the same while edits merge into it; 0 = none)
        // and the text before it, so a copy can be worked out from where the
        // whole step began rather than edit by edit
        unsigned undoId() const { return undo_.empty() ? 0 : undo_.back().id; }
        const std::string& undoText() const { return undo_.empty() ? doc_.text : undo_.back().text; }
        // Replaces the text as an edit tagged `link`: merged into the last
        // step when that has the same tag (typing), else a new one. The
        // selection is kept (by path).
        bool applyLinked(const std::string& text, unsigned link);
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
        // kept beside it as a numbered copy). Refused, as save() is, when the
        // file changed on disk since it was read. Undoable: Undo brings back
        // the text that was being edited, as unsaved changes.
        bool restoreOriginal(std::string& error);

        static const size_t UNDO_LIMIT = 100;

    private:
        struct Snapshot
        {
            std::string text;
            std::vector<std::string> selection;   // the selected elements' paths, the primary last
            unsigned link = 0;
            unsigned id = 0;                      // the step's identity (kept through undo and redo)
        };
        struct Drag
        {
            bool active = false;
            bool moved = false;
            bool cycleOnClick = false;
            bool group = false;      // several selected elements move together
            bool marquee = false;    // a marquee from empty canvas
            bool additive = false;   // Ctrl / Shift: the marquee adds
            int clicked = -1;        // a group drag: the element it began on
            int handle = CH_NONE;
            float startSx = 0, startSy = 0;
            float startTx = 0, startTy = 0;
            float endTx = 0, endTy = 0;
            DragRect start;
            DragRect rect;
        };
        // A box moved (or resized) from `from` to `to`
        struct BoxEdit
        {
            int element = -1;
            DragRect from, to;
            bool resize = false;
        };

        // Replaces the text (one undo step unless coalescing) and reparses.
        // The selection is found again by its paths, unless selectAfter names
        // what to select ("\x01": nothing)
        bool change(const std::string& text, const std::string& key, const std::string& selectAfter = "");
        bool change(const std::string& text, const std::string& key, const std::vector<std::string>& selectAfter);
        bool load(const std::string& text, std::string& error);
        // Where a drag of the selected element left its box: write the attributes
        bool commitBox(const DragRect& from, const DragRect& to, bool resize);
        // Several boxes at once: one text change
        bool commitBoxes(const std::vector<BoxEdit>& edits, const std::string& key);
        // The attributes that put an element's box at `to`, and its geometry after
        bool boxWrites(const BoxEdit& edit, std::vector<std::pair<std::string, std::string>>& writes, ElementGeom& after) const;
        bool insideTheme(std::string& error) const;
        std::string selectedPath() const;
        std::vector<std::string> selectionPaths() const;
        void selectPaths(const std::vector<std::string>& paths);
        void assignSelection(const std::vector<int>& elements);
        // The selected elements that aren't inside another selected one, in document order
        std::vector<int> topSelected() const;

        bool open_ = false;
        std::string root_, folder_, page_;
        LayoutDoc doc_;
        std::string saved_;      // the text as it is on disk (read or last saved)
        std::map<std::string, ElementGeom> geom_;
        std::map<std::string, bool> hidden_;
        int selected_ = -1;
        std::vector<int> selection_;   // every selected element, selected_ last
        int engineW_ = 0, engineH_ = 0;
        Drag drag_;
        std::vector<Snapshot> undo_, redo_;
        std::string coalesce_, lastKey_;
        unsigned revision_ = 0;
        unsigned structure_ = 0;
        unsigned nextStep_ = 0;
    };
}

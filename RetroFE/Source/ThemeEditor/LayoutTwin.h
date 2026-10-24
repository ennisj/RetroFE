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

// A page and its twin: layout.xml and "layout 4x3.xml" are usually the same
// elements, in the same order, at different positions (Aeon Nox's 16:9 and
// 4:3 pages differ only in numbers). The layout editor can edit them
// together: an edit on one page is copied to the same element on the other,
// as text, changing only those bytes there too.
//
//   - Which element is "the same": it's looked for under its parent's match
//     (the page, the same menu). A menu's items by their offset from the
//     selected="true" item (a 9-item wheel and a 7-item one pair up around
//     the middle; the outer ones have no counterpart), or by index= for a
//     vertical menu's. Anything else: the one in the same place among as
//     many siblings of its tag, with the same identifying attributes (type,
//     imageType, src, id, location, index, selected, a text's value); else
//     the only one with those (or the same one of as many); else the one in
//     the same place among as many that differs only by src / value. An
//     event's steps and changes follow their element. No match, no copy.
//   - What is copied: attribute changes only. A position or size (x, y,
//     xOffset, yOffset, width, height, maxWidth, maxHeight) set to a number
//     moves there by the same amount, scaled to that page's size and
//     rounded to a whole unit (a move of 30 across a 1920-wide page is 25
//     across a 1600-wide one); "center",
//     "50%" and the like resolve first, as PageBuilder does, and one set on
//     neither page counts from 0. Anything else (colours, fonts, layers,
//     sources, animation values, a keyword typed into x) is copied as it is
//     where the other page had the same value as this one, and left alone
//     where it already differed; so is the page's own size. The result says
//     what wasn't copied.
//   - Adding, deleting and duplicating elements, events or steps isn't
//     copied as a change (attributeChanges says the structure changed).
//     Instead the screen makes those operations on both pages (runOnBoth):
//     the same operation on the matching elements there, when every one
//     it acts on has a match. Align and distribute go the same way, each
//     page lining up its own boxes (twinGeometry), since copying the moves
//     lines them up on one page only.
//
// Pure: no filesystem. LayoutSession holds each page.

#include "LayoutText.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ThemeEditor
{
    // The page edited together with `page`: "layout.xml" and "layout 4x3.xml"
    // in the same folder, either way round, when both are in `pages`; "" otherwise
    std::string twinPage(const std::string& page, const std::vector<std::string>& pages);

    // The element of `to` that is `element` of `from`; -1 when there's none
    // (or it can't be told apart). The page itself matches the page.
    int matchElement(const LayoutDoc& from, int element, const LayoutDoc& to);

    // An attribute that changed between two versions of a page
    struct AttrChange
    {
        std::string path;           // the element, as it was before
        std::string name;
        bool had = false;           // set before
        std::string before;
        bool has = false;           // set after (false: removed)
        std::string after;
    };
    // The attribute changes from `before` to `after`; false (and nothing)
    // when elements came, went or moved between them
    bool attributeChanges(const LayoutDoc& before, const LayoutDoc& after, std::vector<AttrChange>& out);

    struct MirrorResult
    {
        std::string text;                   // the other page with the changes copied
        int applied = 0;                    // attributes changed there
        std::vector<std::string> skipped;   // what wasn't copied, and why
    };
    // Where a scaled position or size started on both pages, so a run of
    // separate edits (ten 1-unit nudges, each its own step) is scaled as one
    // move and not rounded step by step: the other page gets its value then
    // plus the whole move since, scaled. `expect` is what the other page's
    // value is taken to be (as the copy before this step left it; written is
    // what the latest copy wrote); when it isn't that (it was changed there),
    // the origin starts again from the values as they are.
    struct TwinOrigin
    {
        std::string here, there;     // the values on both pages when it started
        std::string expect, written;
    };
    // By element path (on this page) and attribute name
    using TwinOrigins = std::map<std::string, TwinOrigin>;

    // Copies `changes`, made on a page whose text was `before` (its design
    // size thisW x thisH), to `twin` (twinW x twinH). With `origins`, scaled
    // values are worked out from where they started and the origins kept up
    // to date.
    MirrorResult mirrorChanges(const LayoutDoc& before, const std::vector<AttrChange>& changes, const LayoutDoc& twin,
                               int thisW, int thisH, int twinW, int twinH, TwinOrigins* origins = nullptr);

    // The page's design width or height: its <layout>'s, when that's a number, else `fallback`
    int designSize(const LayoutDoc& doc, bool width, int fallback);

    class LayoutSession;

    // Two open pages kept together: what has been copied so far
    struct TwinSync
    {
        std::string synced;     // this page's text when it was last copied
        unsigned step = 0;      // its undo step then (LayoutSession::undoId)
        std::string stepBase;   // its text before that step's copy began
        unsigned links = 0;     // link numbers handed out
        TwinOrigins origins;    // where scaled values started (mirrorChanges)
        // Nothing to copy from here on (after an undo, a switch, opening a
        // page); the origins go too
        void reset(const LayoutSession& here);
    };
    enum class TwinCopy { Nothing, Copied, Structure };
    // Copies what `here` changed since the last copy to `there`, as a step
    // linked to here's step (LayoutSession::applyLinked), so Undo takes both.
    // While here's edits merge into one step (typing, a run of nudges), the
    // copy is worked out again from where that step began on both pages, so
    // in-between values (a field typed empty, each nudge's rounding) don't
    // add up. Structure: elements came or went, nothing was copied.
    TwinCopy syncTwinPages(LayoutSession& here, LayoutSession& there, TwinSync& state, MirrorResult& result);
    // A linked step that fell off the bottom of one page's history (UNDO_LIMIT)
    // takes the other page's history up to its half with it, so Undo never
    // reaches a step on one page whose other half is gone. syncTwinPages does
    // this too.
    void pairEvictions(LayoutSession& a, LayoutSession& b);

    struct ElementGeom;

    // The box of `element` (geometry `g` here, on a page hereW x hereH) on the
    // other page, as its element `match` there: the position and origin from
    // that page's own attributes, as PageBuilder reads them (a menu item's
    // <itemDefaults> included); the size from its size here, changed by
    // whatever size attributes (width, height, min/max, fontSize) differ
    // there, as ViewInfo scales (aspect kept). A size that was a guess here
    // is guessed there the same way; one that can't be told (art clamped by
    // a maximum here and set differently there) is scaled with the page and
    // marked approximate. False for the page, no match, or a box drawn
    // around a menu's slots.
    bool twinGeometry(const LayoutDoc& here, int element, const ElementGeom& g, int hereW, int hereH,
                      const LayoutDoc& there, int match, int thereW, int thereH, ElementGeom& out);

    // A group operation made on both pages, each on its own elements, rather
    // than copied as per-element changes: align and distribute work from
    // each page's own boxes (copying the moves would line them up on one page
    // only), and adding, duplicating and deleting elements, events and steps
    // happens at the matching place there.
    struct TwinOp
    {
        // The elements here the operation acts on (beyond the selection that
        // `selection` gives), each of which must have a match there
        std::vector<int> touches;
        // Act on the movable selection (align, distribute) rather than the whole selection
        bool movable = false;
        // Act on the primary only (adding an element after it, its animations)
        bool primaryOnly = false;
        // The elements need their boxes there (twinGeometry)
        bool boxes = false;
        // Run there even when nothing changed here (lining up what's already lined up here, not there)
        bool evenIfUnchanged = false;
        // The operation, on either page; `map` turns an element of this page
        // into the same element on the page it runs on (itself here). There,
        // the selection is the matching elements, the primary's match last.
        std::function<bool(LayoutSession& page, const std::function<int(int)>& map)> run;
    };
    struct TwinOpResult
    {
        bool here = false;                  // it changed this page
        bool there = false;                 // and the other one
        bool linked = false;                // the two are one undo step
        std::vector<std::string> missing;   // what has no match there (nothing was done there), labels
    };
    // Copies anything pending first (syncTwinPages), runs the operation here,
    // then, when everything it acts on has a match there, on the other page
    // as a step linked to here's (Undo takes both). Afterwards nothing is
    // left for syncTwinPages to copy from it. When something has no match,
    // only this page changes and `missing` says what; the caller decides
    // whether to let syncTwinPages copy it the old way.
    TwinOpResult runOnBoth(LayoutSession& here, LayoutSession& there, TwinSync& state, const TwinOp& op);
}

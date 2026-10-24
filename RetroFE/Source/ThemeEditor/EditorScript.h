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

// Editor scripts: a file of editor commands, one per line, so the editor can
// be checked without a mouse (ImGui can't be driven by posted input). Given
// once, on the command line (retrofe -editorScript <file>), and run the first
// time a theme is opened in that session; see docs/THEME_EDITOR.md.
//
// Parsing and the commands that only change the session are here, pure and
// tested; the screen runs the rest (wait, save, use, show, ...).

#include "EditorSession.h"
#include "LayoutSession.h"
#include <string>
#include <vector>

namespace ThemeEditor
{
    struct EditorCommand
    {
        std::string name;                 // "move"; "" for a blank or # comment line
        std::vector<std::string> words;   // the words after the name
        std::string rest;                 // everything after the name, trimmed

        // Everything after the first n words, trimmed ("var TOKEN some text" -> tail(1) = "some text")
        std::string tail(size_t n) const;
    };

    EditorCommand parseEditorCommand(const std::string& line);

    // Every command in a script's text, blank lines and comments left out
    std::vector<EditorCommand> parseEditorScript(const std::string& text);

    enum class CommandResult { Ok, Failed, NotSession };

    // Runs a command that only needs the session: page, select, selectid,
    // add, click X Y, move DX DY, resize HANDLE DX DY, nudge DX DY, name,
    // layer, alpha, colour, var, delete, undo, redo; templates:
    // tplnew COLLECTION blank|default|PRIMARY NAME..., tplassign COLLECTION
    // PRIMARY|default, tpldelete PRIMARY [REPLACEMENT|default], tplrename
    // PRIMARY NAME...; theme properties: font PATH|default, sound KIND
    // PATH|none, splashtext TEXT, splashbg PATH|none; several at once:
    // addselect N, toggleselect N, selectall, selection (notes it), clickadd
    // X Y (a Ctrl-click), marquee X1 Y1 X2 Y2 [add] (without add it must
    // start on empty canvas), align left|center|right|top|middle|bottom,
    // distribute h|v, duplicate. Coordinates and distances are theme units.
    // NotSession for anything else. `note` says what happened (the
    // selection, the widget's new rect).
    CommandResult runSessionCommand(EditorSession& session, const EditorCommand& command, std::string& note);

    // The same for the layout-native mode (LayoutSession): select N (in the
    // element list's order), selectpath PATH ("-" = the page itself), click
    // X Y, move DX DY, resize HANDLE DX DY, nudge DX DY, attr NAME VALUE...,
    // rmattr NAME, get NAME (notes the value), layer N, duplicate, delete,
    // undo, redo, snap on|off; add PALETTEID [TYPE|-] [SRC...]; animations,
    // by paths relative to the selected element ("onEnter[0]/set[0]", "-"
    // for the element itself): event NAME, addset EVENT, addanimate SET,
    // rmanim PATH, animattr PATH NAME VALUE..., rmanimattr PATH NAME,
    // getanim PATH NAME, copyanims SOURCEPATH, events (notes them), endbox
    // [EVENT] (notes where onEnter, or that event, leaves it), movingevents;
    // several at once: addpath PATH, togglepath PATH, selectall, selection,
    // clickadd X Y, marquee X1 Y1 X2 Y2 [add], align ..., distribute h|v
    // (duplicate and delete take every selected element). NotSession for
    // anything else.
    CommandResult runLayoutCommand(LayoutSession& session, const EditorCommand& command, std::string& note);
}

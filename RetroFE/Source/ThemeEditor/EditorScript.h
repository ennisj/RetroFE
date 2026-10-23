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
    // PATH|none, splashtext TEXT, splashbg PATH|none. Coordinates and
    // distances are theme units. NotSession for anything else. `note` says
    // what happened (the selection, the widget's new rect).
    CommandResult runSessionCommand(EditorSession& session, const EditorCommand& command, std::string& note);
}

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

// What saving a theme writes, worked out without touching the disk. An
// editor-made theme is an ordinary layout folder with its project inside:
//
//   layouts/<Theme>/layout.xml                          the Default page
//   layouts/<Theme>/collections/<c>/layout/layout.xml   collection templates
//   layouts/<Theme>/splash.xml                          the splash page, if any
//   layouts/<Theme>/editor/project.xml                  the project
//   layouts/<Theme>/assets/                             files the project imported
//   layouts/<Theme>/fonts/Roboto-Regular.ttf            the bundled font (+ its licence)
//   layouts/<Theme>/collections/_common/medium_artwork/firstLetter/   letter tiles
//
// Every page starts with the editor's marker (ThemeGenerate.h), which is
// what marks the folder as editor-made. ThemeStore.h does the writing.
//
// Ported from One Saucier's theme editor (app/editor/theme_export.*). Pure.

#include "ThemeGenerate.h"
#include <string>
#include <vector>

namespace ThemeEditor
{
    extern const char* const PROJECT_FILE;          // "editor/project.xml"
    extern const char* const ASSETS_FOLDER;         // "assets"
    extern const char* const BUNDLED_FONT;          // "fonts/Roboto-Regular.ttf"
    extern const char* const BUNDLED_FONT_LICENSE;  // "fonts/LICENSE-Apache-2.0.txt"
    extern const char* const LETTER_TILES_FOLDER;   // "collections/_common/medium_artwork/firstLetter"

    // What's at a theme folder: nothing (fresh ground), an editor-made theme,
    // or anything else (a hand-made theme, never overwritten or deleted)
    enum ThemeFolderKind { TF_FRESH = 0, TF_EDITOR = 1, TF_FOREIGN = 2 };

    // From layout.xml's first line: "" = no file
    ThemeFolderKind classifyFirstLine(const std::string& firstLine);

    struct PlannedFile
    {
        std::string relPath;   // '/' separated, under the theme folder
        std::string content;
    };

    struct SavePlan
    {
        std::vector<PlannedFile> files;   // project.xml first
        std::vector<std::string> errors;  // any error -> nothing may be written
        std::vector<std::string> warnings;
        bool ok() const { return errors.empty(); }
    };

    struct SaveInputs
    {
        std::string fontBytes;      // the bundled font (Roboto); empty = not written, with a warning
        std::string fontLicense;    // its licence text; empty = the one compiled in
        std::string themeDir;       // the theme's folder name; "" = themeFolderName(project name)
        bool checkLayouts = true;   // run checkLayoutXml over each page, issues become warnings
        // A project without a splash page still gets a splash.xml: RetroFE
        // won't start on a layout that has none. It shows the theme's name.
        // ThemeStore turns it off when the folder has a hand-made splash.xml.
        bool splashFallback = true;
    };

    // The full save: project.xml, every page generated with animations on and
    // nothing filtered (the Show toggles are an editor aid), shared templates
    // fanned out to each member collection, the bundled font when the project
    // uses it, and the letter tiles when a page uses firstLetter art. A page
    // that doesn't generate (including one that doesn't parse) or a
    // collection whose name isn't folderNameSafe is an error.
    // Second-screen pages aren't written yet (a warning says so).
    SavePlan buildSavePlan(const ThemeProject& p, const WidgetLibrary& lib, const SaveInputs& inputs);

    // The splash page a theme gets when it has none of its own: the theme's
    // name, centred (a Static Text with id theme_name). False without the
    // static_text widget.
    bool defaultSplashPage(const ThemeProject& p, const WidgetLibrary& lib, ThemePage& out);

    // Is `rel` a position a page is saved to: layout.xml, splash.xml or
    // collections/<one folder>/layout/layout.xml? (layout1.xml too: One
    // Saucier's second screens, so stale ones are cleaned up.)
    bool isPagePath(const std::string& rel);

    // The pages a previous save wrote that this plan no longer does.
    // `existingEditorPages` is every page position in the folder whose first
    // line is the marker, so nothing hand-made is ever offered. The root
    // layout.xml is never stale: it's what marks the folder as editor-made.
    std::vector<std::string> stalePages(const std::vector<std::string>& planRelPaths,
                                        const std::vector<std::string>& existingEditorPages);

    // The Apache 2.0 licence text compiled in for the bundled font
    std::string bundledFontLicense();
}

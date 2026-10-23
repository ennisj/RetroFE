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

// Editor-made themes on disk. A theme's project lives inside its own layout
// folder (layouts/<Theme>/editor/project.xml, see ThemeExport.h), so saving
// regenerates the theme in place and an editable theme is just a layout
// folder that can be copied, packaged or deleted like any other.
//
// The guard: a folder whose layout.xml doesn't start with the editor's
// marker is somebody's hand-made theme. Nothing here writes into it or
// deletes it.
//
// Every file is written to "<name>.tmp" and renamed over the old one, so a
// failure never leaves a half-written file under the real name. A file whose
// content hasn't changed isn't rewritten. Folder names are single path
// components under layoutsRoot; anything path-shaped is refused.
//
// Adapted from One Saucier's project store and export
// (app/editor/project_store.*, theme_export.*), with std::filesystem.

#include "ThemeExport.h"
#include <string>
#include <vector>

namespace ThemeEditor
{
    struct ThemeSummary
    {
        std::string folder;           // the folder under layouts/ (what `layout =` names)
        std::string name;             // the project's name, else the folder
        ThemeFolderKind kind = TF_FOREIGN;
        bool hasProject = false;      // editor/project.xml exists
        bool projectOk = false;       // ...and parses
        bool vertical = false;
        int pages = 0;
        int widgets = 0;
        std::string error;            // why the project didn't load
    };

    // Every theme folder, sorted by name (case-insensitively). Editor-made
    // themes carry their project's summary.
    std::vector<ThemeSummary> listThemes(const std::string& layoutsRoot);

    // What's at layouts/<folder>:
    //   no folder, an empty one, or only editor/
    //   and/or assets/ folders (no project)      -> TF_FRESH
    //   layout.xml starts with the marker        -> TF_EDITOR
    //   no layout.xml but editor/project.xml     -> TF_EDITOR (a save that stopped early)
    //   anything else                            -> TF_FOREIGN
    ThemeFolderKind probeTheme(const std::string& layoutsRoot, const std::string& folder);

    // A single path component: not empty, no separators or ':', not "." / ".."
    bool themeFolderValid(const std::string& folder);

    bool loadTheme(const std::string& layoutsRoot, const std::string& folder, ThemeProject& out, std::string& error);

    struct SaveReport
    {
        std::vector<std::string> written;    // files rewritten (unchanged ones are skipped)
        std::vector<std::string> removed;    // stale pages removed
        std::vector<std::string> warnings;
    };

    // Saves the project into layouts/<folder>: project.xml first, then every
    // page, the font and the letter tiles, then removes pages an earlier save
    // wrote that this one doesn't (only pages carrying the marker, never one
    // that is the same file as a page just written). Refused, before
    // anything is written, for a hand-made folder, when the plan has an
    // error, or when a page would replace a hand-made page (no marker)
    // inside the theme. A first save that fails removes what it wrote.
    bool saveTheme(const std::string& layoutsRoot, const std::string& folder, const ThemeProject& p,
                   const WidgetLibrary& lib, const SaveInputs& inputs, SaveReport* report, std::string& error);

    // A free folder for a name: themeFolderName(name), then "<name> 2", "<name> 3"...
    std::string freeThemeFolder(const std::string& layoutsRoot, const std::string& name);

    // The first unused "New Theme N"
    std::string nextThemeName(const std::vector<ThemeSummary>& themes);

    // Creates a theme with an empty Default page (1920x1080, or 1080x1920
    // when vertical) in a free folder named after it
    bool createTheme(const std::string& layoutsRoot, const std::string& name, bool vertical, const WidgetLibrary& lib,
                     const SaveInputs& inputs, std::string& folderOut, std::string& error);

    // Copies an editor-made theme, files and all, as "<name> Copy" in a free
    // folder, and saves it there (which re-marks its pages)
    bool duplicateTheme(const std::string& layoutsRoot, const std::string& folder, const WidgetLibrary& lib,
                        const SaveInputs& inputs, std::string& folderOut, std::string& error);

    // Deletes an editor-made theme folder. Refused for anything else. Links
    // inside it are removed, never followed.
    bool deleteTheme(const std::string& layoutsRoot, const std::string& folder, std::string& error);

    // Copies a file into the theme's assets/ and answers the path pages use
    // ("assets/<name>"). An identical file already there is reused; a
    // different one with the same name gets -2, -3... added. Refused for a
    // theme that has never been saved (TF_FRESH).
    bool importAsset(const std::string& layoutsRoot, const std::string& folder, const std::string& sourcePath,
                     std::string& relOut, std::string& error);

    // importAsset into another folder of the theme (e.g. "fonts"). A file
    // that's already inside the theme folder isn't copied: relOut is its
    // path in the theme as it is.
    bool importFile(const std::string& layoutsRoot, const std::string& folder, const std::string& sourcePath,
                    const std::string& subfolder, std::string& relOut, std::string& error);

    // The files in a folder of the theme whose extension (lower case, with
    // the dot) is listed, as theme paths ("fonts/x.ttf"), sorted
    std::vector<std::string> themeFiles(const std::string& layoutsRoot, const std::string& folder,
                                        const std::string& subfolder, const std::vector<std::string>& extensions);

    // Is there a page somebody wrote by hand (no marker) at
    // collections/<collection>/layout/layout.xml? Saving never overwrites one.
    bool handMadeCollectionPage(const std::string& layoutsRoot, const std::string& folder, const std::string& collection);

    // The *.widget files in a folder (for overlayWidgetFiles), sorted
    WidgetFiles readWidgetFolder(const std::string& dir);

    // Writes `content` to path.tmp and renames it over `path`
    bool writeFileAtomic(const std::string& path, const std::string& content, std::string& error);
}

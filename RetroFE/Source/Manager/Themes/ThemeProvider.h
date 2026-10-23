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

#include "../Provider.h"
#include "../../ThemeEditor/ThemeExport.h"
#include "../../ThemeEditor/WidgetDefs.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The Themes screens: the installed layouts (layouts/<name>/), what each one
// is, and using, editing, creating, duplicating and deleting them. Editing,
// duplicating and deleting are for themes made with the theme editor only;
// a hand-made theme (Aeon Nox included) is never written to.
//
//   theme:list            results: every installed theme (row id = folder)
//   theme:name, folder, kind, size, pages, widgets, inUse, editNote, problem
//                         facts about the theme param:id names
//   theme:isEditor, canEdit, isActive, isHandMade   flags for visibleWhen
//   theme:newName, theme:newOrientation             the New theme screen
//   options="theme:orientations"
// Actions: theme.use, theme.edit (opens the theme-editor screen),
// theme.create, theme.duplicate, theme.delete
namespace Themes
{
    // What the editor screen's form id is: ManagerUI draws it with ImGui
    extern const char* const editorForm;

    // Where things are, so tests can point the provider at a temporary root
    struct Paths
    {
        std::string root;                        // the RetroFE folder: settings*.conf and layouts/
        std::function<std::string()> fontBytes;  // the bundled font (Roboto) new themes get; may be empty
    };

    // One installed theme, as the Themes list shows it
    struct ThemeFacts
    {
        std::string folder;
        std::string name;                       // the project's name for editor-made themes, else the folder
        ThemeEditor::ThemeFolderKind kind = ThemeEditor::TF_FOREIGN;
        bool active = false;                    // the `layout` setting names it
        int width = 0, height = 0;              // the main page's design size; 0 = unknown
        bool hasLayout = false;                 // layout.xml exists
        std::string mainProblem;                // editor-made: mainPageProblem of its layout.xml
        bool hasSplash = false;
        std::vector<std::string> collectionPages;   // collections/<c>/layout/*.xml, sorted
        std::vector<std::string> variants;      // other main pages: "layout - 1", "layout 4x3"
        bool projectOk = false;                 // editor-made: project.xml loads
        std::string projectError;
        int editorPages = 0, widgets = 0;       // editor-made: from the project
    };

    // The <layout> root's width and height in a layout file's text; false when it has none
    bool layoutSize(const std::string& xml, int& width, int& height);

    // Why RetroFE can't run on an editor-made theme whose main page is this, or "":
    // without a menu (a wheel) there's nothing to browse with
    std::string mainPageProblem(const std::string& layoutXml);

    // Every folder under <root>/layouts, sorted by name
    std::vector<ThemeFacts> scanThemes(const std::string& root, const std::string& activeLayout);

    // The `layout` the settings files set; "" when none does
    std::string configuredLayout(const std::string& root);

    // Writes `layout = <folder>` where the settings files set it (settings.conf if none does)
    bool setLayout(const std::string& root, const std::string& folder, std::string& error);

    // The stock widget library, built once
    const ThemeEditor::WidgetLibrary& widgetLibrary();

    class ThemeProvider : public Manager::Provider
    {
    public:
        ThemeProvider(const Manager::Services& services, Paths paths);

        std::string prefix() const override { return "theme"; }
        void refresh() override;
        void enter(const Manager::Screen& screen) override;
        std::string get(const std::string& key, const Manager::Screen& screen) override;
        void set(const std::string& key, const std::string& value, const Manager::Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Manager::Screen& screen) override;
        std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Manager::Screen& screen) override;
        std::string actionProblem(const std::string& action, const Manager::Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Manager::Screen& screen) override;

        // The themes as last scanned
        const std::vector<ThemeFacts>& themes() const { return themes_; }

    private:
        void scan();
        const ThemeFacts* find(const std::string& folder) const;
        std::string activeLayout() const;
        ThemeEditor::SaveInputs saveInputs() const;
        std::string layoutsRoot() const;

        Manager::Services services_;
        Paths paths_;
        std::vector<ThemeFacts> themes_;
        std::string newName_;
        std::string newOrientation_ = "landscape";
    };

    // The provider RetroFE registers (Manager/Themes/app/ThemesApp.cpp): the
    // running root and the bundled font compiled into RetroFE
    std::unique_ptr<Manager::Provider> makeProvider(const Manager::Services& services);
}

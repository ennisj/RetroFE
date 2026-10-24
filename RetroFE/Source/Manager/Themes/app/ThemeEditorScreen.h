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

#include "FileChooser.h"
#include "../../ImGuiScreen.h"
#include "../../../ThemeEditor/EditorScript.h"
#include "../../../ThemeEditor/EditorSession.h"
#include <SDL2/SDL.h>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class CollectionInfo;
class Configuration;
class FontCache;
class Page;
namespace Library { class Indexer; }

// The theme editor: an ImGui screen inside the manager, opened by the Edit
// action on a theme made with the editor (form "theme-editor", param id =
// the theme's folder). Three regions: the theme and its pages on the left,
// the canvas in the middle, the selected widget's properties on the right.
//
// The canvas shows the page as RetroFE's own PageBuilder builds it from the
// generated XML (setLayoutXml), drawn into a texture of its own and shown
// letterboxed, with a wireframe of the project's widgets over it. Edits
// change the project (ThemeEditor::EditorSession); the preview page is
// rebuilt from the regenerated XML in update(), never while drawing.
//
// While it's open the `layout` setting points at the theme, so its fonts and
// art resolve as they will when it's used; closing puts it back.
//
// retrofe -editorScript <file> runs editor commands from a file, one per
// frame, the first time a theme is opened in that session, with a banner
// while it runs, so the editor can be checked without a mouse
// (ThemeEditor/EditorScript.h).
class ThemeEditorScreen : public Manager::ImGuiScreen
{
public:
    struct Context
    {
        Configuration* config = nullptr;
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        FontCache* fontCache = nullptr;
        float scale = 1.0f;
        std::string root;   // the RetroFE folder (layouts/ is under it)
        // Builds a collection the way RetroFE does (the caller owns it), and
        // the name of the one RetroFE shows now
        std::function<CollectionInfo*(const std::string&)> loadCollection;
        std::function<std::string()> currentCollection;
        std::function<void()> requestReload;
        // The shared library index (Services::library): the menu tree the
        // template coverage is shown on. May be null.
        Library::Indexer* library = nullptr;
    };

    ThemeEditorScreen(const Context& context, const std::string& folder);
    ~ThemeEditorScreen() override;

    void update(float dt) override;
    void drawTargets() override;
    void draw() override;
    bool escape() override;
    bool askToLeave() override;
    std::string leaving() override;
    void discard() override;
    void setStatus(const std::string& message, bool isError) override;
    bool takeBack() override;
    void retire() override;
    void shutdown() override;

private:
    enum class Job { None, Save, SaveAndClose, Use, NativePick, Import };

    // What a chosen file is for
    enum class FileFor { WidgetVar, Font, Sound, SplashBackground };
    struct FileRequest
    {
        FileFor what = FileFor::WidgetVar;
        std::string key;        // the var's token, or the sound's kind
        int page = -1;          // the widget's page and id (the selection may change)
        std::string widgetId;
        std::string filter;     // image, video, text, font, sound, "" = any
        std::string title;
        std::string note;       // what happens without a file
    };
    // A collection in the menu tree, as the Collections list shows it
    struct TreeRow
    {
        std::string name;
        int depth = 0;
        bool menu = false;
        bool children = false;
        std::string otherLayout;   // collections.<c>.layout names another theme
    };

    void rebuildPreview();
    void destroyPreview();
    bool ensureTarget();
    CollectionInfo* previewCollection();
    void runJob();
    bool save();
    void use();
    void pickNative();
    bool importChosen(const std::string& path, const FileRequest& request);
    void openBrowser(const FileRequest& request);
    FileRequest varRequest(const std::string& token) const;
    void askForFiles();
    void refreshTree();
    void refreshHandMade();
    bool isHandMade(const std::string& collection) const;
    void refreshFonts();
    std::vector<std::string> descendants(const std::string& collection) const;
    std::string pageNameFor(const std::string& collection, bool menu) const;
    void openTemplateDialog(const std::string& collection, bool create, const std::string& basedOn);
    void runScript();
    bool runCommand(const ThemeEditor::EditorCommand& command, std::string& note);

    void drawToolbar();
    void drawThemePanel();
    void drawThemeProperties();
    void drawSplash();
    void drawPages();
    void drawCollections();
    void drawFileRow(const char* label, const std::string& value, const FileRequest& request,
                     const std::function<void()>& clear);
    void drawBrowser();
    void drawTemplateDialog();
    void drawDeleteTemplate();
    void drawCanvas();
    void drawProperties();
    void drawVar(const ThemeEditor::WidgetVar& var);
    void drawAddWidget();
    void drawPopups();
    void syncPopup(const char* name, bool& show, bool& opened);
    void requestClose();

    Context ctx_;
    std::string folder_;
    std::string openError_;
    ThemeEditor::EditorSession session_;

    // The preview
    ThemeEditor::GenOptions show_;
    Page* page_ = nullptr;
    SDL_Texture* target_ = nullptr;
    int targetW_ = 0, targetH_ = 0;
    std::string builtXml_;
    std::string builtCollection_;
    unsigned builtRevision_ = 0;
    bool previewDirty_ = true;
    float sinceChange_ = 0.0f;
    unsigned seenRevision_ = 0;
    int seenPage_ = 0;
    std::vector<std::string> genIssues_;      // the generator's errors and warnings
    std::vector<std::string> engineIssues_;   // what PageBuilder logged building it
    std::unique_ptr<CollectionInfo> master_;  // the preview collection as loaded
    std::string masterName_;
    std::string previewName_;                 // the collection the preview shows
    std::vector<std::string> collections_;

    // `layout` while the editor is open
    std::string originalLayout_;
    bool layoutOverridden_ = false;

    // The canvas as last drawn
    ThemeEditor::CanvasFit fit_;

    // Work queued from draw() for update()
    Job job_ = Job::None;
    FileRequest jobRequest_;
    std::string jobPath_;
    bool jobAnnounced_ = false;   // a big copy's status line has been shown

    // Files: the chooser (ImGui, inside the RetroFE folder) and what it's open for
    Themes::FileChooser browser_;
    FileRequest browserRequest_;
    std::filesystem::path lastBrowseDir_;
    std::vector<std::string> fonts_;   // the theme's fonts/*.ttf|otf
    bool fontsListed_ = false;

    // Collections and templates
    std::vector<TreeRow> tree_;
    unsigned treeGeneration_ = 0;
    bool treeFromIndex_ = false;
    std::string treeFilter_;
    std::string chosenCollection_;          // picked in the Collections list
    std::string openSection_;               // a section a script asked to unfold
    std::set<std::string> handMade_;        // collections whose page in the theme was written by hand
    std::string tplProblemKey_, tplProblem_;
    bool showTemplate_ = false, templateOpen_ = false;
    std::string tplCollection_;             // "" = choose one in the dialog
    bool tplFixedCollection_ = false;
    int tplChoice_ = 0;                     // 0 = a new template, 1 = Default, 2.. = templates()[n - 2]
    std::string tplName_;
    std::string tplBasedOn_;                // "" = Default, EditorSession::BLANK, or a template
    bool tplCascade_ = false;
    std::string tplFilter_;
    bool showDeleteTpl_ = false, deleteTplOpen_ = false;
    std::string delPrimary_;
    std::string delReplacement_;

    std::string status_;
    bool statusIsError_ = false;
    ThemeEditor::SaveReport report_;
    bool showReport_ = false;
    bool showAdd_ = false;
    bool showClose_ = false;
    bool addOpen_ = false, reportOpen_ = false, closeOpen_ = false;   // ImGui has the popup open
    std::string addFilter_;
    bool back_ = false;
    bool leaveWithoutSaving_ = false;
    bool retired_ = false;   // left: nothing more is built
    bool navFlagsSaved_ = false, savedNavKeyboard_ = false;   // ImGui keyboard navigation, off while shown
    bool nudging_ = false;   // arrow-key nudges since the last pause
    float sinceNudge_ = 0.0f;

    // -editorScript
    std::vector<ThemeEditor::EditorCommand> script_;
    std::string scriptPath_;
    size_t scriptAt_ = 0;
    int scriptWait_ = 0;
};

namespace Themes
{
    // The screen for a theme editor form: the widget editor (editorForm) or
    // the layout-native editor (layoutEditorForm, LayoutEditorScreen.h).
    // params: id = the theme's folder, page = a page in it (layout editor)
    std::unique_ptr<Manager::ImGuiScreen> makeEditorScreen(const std::string& form, const ThemeEditorScreen::Context& context,
                                                           const std::map<std::string, std::string>& params);

    // retrofe -editorScript <file>: its commands, the first time an editor
    // (either kind) is opened in this session only; false after that
    bool takeEditorScript(Configuration* config, std::vector<ThemeEditor::EditorCommand>& commands, std::string& path);
}

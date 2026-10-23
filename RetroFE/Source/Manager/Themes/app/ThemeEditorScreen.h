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

#include "../../ImGuiScreen.h"
#include "../../../ThemeEditor/EditorScript.h"
#include "../../../ThemeEditor/EditorSession.h"
#include <SDL2/SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CollectionInfo;
class Configuration;
class FontCache;
class Page;

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
    enum class Job { None, Save, SaveAndClose, Use, PickFile };

    void rebuildPreview();
    void destroyPreview();
    bool ensureTarget();
    CollectionInfo* previewCollection();
    void runJob();
    bool save();
    void use();
    void pickFile();
    void runScript();
    bool runCommand(const ThemeEditor::EditorCommand& command, std::string& note);

    void drawToolbar();
    void drawThemePanel();
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
    std::string jobToken_;

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

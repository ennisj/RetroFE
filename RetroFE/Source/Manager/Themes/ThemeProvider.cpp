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
#include "ThemeProvider.h"
#include "../../Library/Files.h"
#include "../../Builtin/Builtin.h"
#include "../../Database/Configuration.h"
#include "../../Database/GlobalOpts.h"
#include "../../Forms/SettingsFile.h"
#include "../../Graphics/LayoutCompat.h"
#include "../../ThemeEditor/ThemeProject.h"
#include "../../ThemeEditor/ThemeStore.h"
#include "../../Utility/Log.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace Themes
{
    const char* const editorForm = "theme-editor";
    const char* const layoutEditorForm = "theme-layout-editor";

    namespace
    {
        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool sameFolder(const std::string& a, const std::string& b)
        {
            // Folder names compare without case: Windows and macOS treat them so
            return !a.empty() && lower(a) == lower(b);
        }

        bool readText(const fs::path& path, std::string& out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        // An attribute's value inside one tag's text, or "" when it isn't there
        std::string attributeIn(const std::string& tag, const std::string& name)
        {
            size_t pos = 0;
            while ((pos = tag.find(name, pos)) != std::string::npos) {
                bool startsWord = pos > 0 && std::isspace(static_cast<unsigned char>(tag[pos - 1]));
                size_t at = pos + name.size();
                while (at < tag.size() && std::isspace(static_cast<unsigned char>(tag[at]))) ++at;
                if (startsWord && at < tag.size() && tag[at] == '=') {
                    ++at;
                    while (at < tag.size() && std::isspace(static_cast<unsigned char>(tag[at]))) ++at;
                    if (at < tag.size() && (tag[at] == '"' || tag[at] == '\'')) {
                        size_t end = tag.find(tag[at], at + 1);
                        if (end != std::string::npos) return tag.substr(at + 1, end - at - 1);
                    }
                    return "";
                }
                pos = at;
            }
            return "";
        }

        std::string plural(int n, const std::string& one, const std::string& many)
        {
            return std::to_string(n) + " " + (n == 1 ? one : many);
        }

        std::string joinNames(const std::vector<std::string>& names, size_t most)
        {
            std::string text;
            for (size_t i = 0; i < names.size() && i < most; ++i) {
                text += (i ? ", " : "") + names[i];
            }
            if (names.size() > most) text += ", ...";
            return text;
        }

        // The orientation a size reads as
        std::string sizeText(int w, int h)
        {
            if (w <= 0 || h <= 0) return "";
            std::string text = std::to_string(w) + " x " + std::to_string(h);
            text += w > h ? ", landscape" : (w < h ? ", portrait" : ", square");
            return text;
        }
    }

    bool layoutSize(const std::string& xml, int& width, int& height)
    {
        width = height = 0;
        size_t pos = 0;
        while ((pos = xml.find("<layout", pos)) != std::string::npos) {
            char next = pos + 7 < xml.size() ? xml[pos + 7] : '\0';
            if (std::isspace(static_cast<unsigned char>(next)) || next == '>' || next == '/') break;
            pos += 7;
        }
        if (pos == std::string::npos) return false;
        size_t end = xml.find('>', pos);
        std::string tag = xml.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        std::string w = attributeIn(tag, "width");
        std::string h = attributeIn(tag, "height");
        try {
            width = w.empty() ? 0 : std::stoi(w);
            height = h.empty() ? 0 : std::stoi(h);
        }
        catch (...) {
            width = height = 0;   // "stretch" and the like: follows the screen
        }
        return width > 0 && height > 0;
    }

    bool pageBuildComponent(const std::string& component)
    {
        static const char* const elsewhere[] = {
            "Collections", "CollectionInfoBuilder", "Menu", "MetadataDatabase", "Database", "HiScores",
            "Catalog", "Configuration", "Manager", "ThemeEditor", "Input"
        };
        for (const char* name : elsewhere) {
            if (component == name) return false;
        }
        return true;
    }

    std::string mainPageProblem(const std::string& layoutXml)
    {
        size_t at = 0;
        while ((at = layoutXml.find("<menu", at)) != std::string::npos) {
            char next = at + 5 < layoutXml.size() ? layoutXml[at + 5] : '\0';
            if (std::isspace(static_cast<unsigned char>(next)) || next == '>' || next == '/') return "";
            at += 5;
        }
        return "Its main page has no wheel yet, so RetroFE would have nothing to browse with. "
               "Edit it and add a wheel (Add widget, Wheels and menus) first.";
    }

    std::string layoutsFolder(const std::string& root, const std::string& layouts)
    {
        return layouts.empty() ? (fs::path(root) / "layouts").string() : layouts;
    }

    std::string themePreview(const std::string& dir)
    {
        std::error_code ec;
        const fs::path folder(dir);
        for (const char* name : { "preview.png", "preview.jpg", "preview.jpeg", "preview.webp", "screenshot.png", "thumbnail.png" }) {
            if (fs::is_regular_file(folder / name, ec)) return Library::narrow(folder / name);
        }
        // The first <image src="..."> a page shows whose file is there (paths are the theme folder's)
        auto firstImage = [&](const fs::path& page) -> std::string {
            std::string xml;
            if (!readText(page, xml)) return "";
            size_t at = 0;
            while ((at = xml.find("<image", at)) != std::string::npos) {
                size_t end = xml.find('>', at);
                if (end == std::string::npos) break;
                std::string tag = xml.substr(at, end - at);
                at = end;
                size_t src = tag.find(" src=\"");
                if (src == std::string::npos) continue;
                size_t start = src + 6;
                size_t close = tag.find('"', start);
                if (close == std::string::npos) continue;
                std::string path = tag.substr(start, close - start);
                if (path.empty() || path.find('%') != std::string::npos) continue;   // a placeholder, not a file
                fs::path file = (folder / fs::path(path)).lexically_normal();
                if (fs::is_regular_file(file, ec)) return Library::narrow(file);
            }
            return "";
        };
        std::string splash = firstImage(folder / "splash.xml");
        return splash.empty() ? firstImage(folder / "layout.xml") : splash;
    }

    std::vector<ThemeFacts> scanThemes(const std::string& root, const std::string& activeLayout, const std::string& layoutsDir)
    {
        std::vector<ThemeFacts> result;
        const fs::path layouts = layoutsFolder(root, layoutsDir);
        for (const ThemeEditor::ThemeSummary& summary : ThemeEditor::listThemes(layouts.string())) {
            ThemeFacts facts;
            facts.folder = summary.folder;
            facts.name = summary.name.empty() ? summary.folder : summary.name;
            facts.kind = summary.kind;
            facts.active = sameFolder(summary.folder, activeLayout);
            facts.projectOk = summary.projectOk;
            facts.projectError = summary.error;
            facts.editorPages = summary.pages;
            facts.widgets = summary.widgets;

            std::error_code ec;
            const fs::path dir = layouts / summary.folder;
            std::string xml;
            facts.hasLayout = readText(dir / "layout.xml", xml);
            if (!facts.hasLayout && fs::is_regular_file(dir / "layout.lua", ec)) {
                // Fork compat: a layout.lua whose build() names the main page (Graphics/LayoutCompat.h)
                const std::string target = LayoutCompat::luaBuildTarget((dir / "layout.lua").string(), dir.string());
                facts.hasLayout = !target.empty() && readText(target, xml);
            }
            if (facts.hasLayout) layoutSize(xml, facts.width, facts.height);
            if (facts.kind == ThemeEditor::TF_EDITOR) facts.mainProblem = facts.hasLayout ? mainPageProblem(xml) : "";
            if (facts.kind == ThemeEditor::TF_EDITOR && summary.projectOk && (facts.width <= 0 || facts.height <= 0)) {
                facts.width = summary.vertical ? 1080 : 1920;
                facts.height = summary.vertical ? 1920 : 1080;
            }
            facts.hasSplash = fs::is_regular_file(dir / "splash.xml", ec);
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                std::string file = it->path().filename().string();
                // "layout - 1.xml" (cycled with the layout key), "layout 4x3.xml" (randomLayout, aspect pages)
                if (it->is_regular_file(ec) && file.rfind("layout", 0) == 0 && file != "layout.xml" &&
                    it->path().extension() == ".xml") {
                    facts.variants.push_back(it->path().stem().string());
                }
            }
            for (fs::directory_iterator it(dir / "collections", ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_directory(ec)) continue;
                std::string collection = it->path().filename().string();
                if (collection == "_common") continue;   // shared art, not a page
                std::error_code pageEc;
                bool hasPage = false;
                for (fs::directory_iterator page(it->path() / "layout", pageEc), pageEnd; !pageEc && page != pageEnd; page.increment(pageEc)) {
                    if (page->is_regular_file(pageEc) && page->path().extension() == ".xml") {
                        hasPage = true;
                        break;
                    }
                }
                const fs::path lua = it->path() / "layout" / "layout.lua";
                if (!hasPage && fs::is_regular_file(lua, pageEc)) {
                    const std::string target = LayoutCompat::luaBuildTarget(lua.string(), dir.string());
                    hasPage = !target.empty() && fs::is_regular_file(target, pageEc);
                }
                if (hasPage) facts.collectionPages.push_back(collection);
            }
            facts.preview = themePreview(Library::narrow(dir));
            auto byName = [](const std::string& a, const std::string& b) { return lower(a) < lower(b); };
            std::sort(facts.collectionPages.begin(), facts.collectionPages.end(), byName);
            std::sort(facts.variants.begin(), facts.variants.end(), byName);
            result.push_back(std::move(facts));
        }
        return result;
    }

    std::string configuredLayout(const std::string& root)
    {
        SettingsFile settings(root);
        settings.load();
        return settings.get(OPTION_LAYOUT);
    }

    bool setLayout(const std::string& root, const std::string& folder, std::string& error)
    {
        SettingsFile settings(root);
        settings.load();
        std::string initial = std::string("# Created by RetroFE.\n"
                                          "# Run \"retrofe -createconfig\" to get a reference of every setting.\n")
            + Builtin::settings;
        return settings.set(OPTION_LAYOUT, folder, initial, error);
    }

    const ThemeEditor::WidgetLibrary& widgetLibrary()
    {
        static ThemeEditor::WidgetLibrary lib;
        static bool built = false;
        if (!built) {
            built = true;
            std::string error;
            if (!ThemeEditor::buildWidgetLibrary(ThemeEditor::embeddedWidgetFiles(), lib, error)) {
                LOG_ERROR("Themes", "The theme editor's widgets don't load: " + error);
            }
        }
        return lib;
    }

    ThemeProvider::ThemeProvider(const Manager::Services& services, Paths paths)
        : services_(services)
        , paths_(std::move(paths))
    {
    }

    std::string ThemeProvider::layoutsRoot() const
    {
        return layoutsFolder(paths_.root, paths_.layouts);
    }

    std::string ThemeProvider::activeLayout() const
    {
        // What the settings files say (what RetroFE uses after a reload), else what it runs now
        std::string layout = configuredLayout(paths_.root);
        if (layout.empty()) services_.config.getProperty(OPTION_LAYOUT, layout);
        return layout;
    }

    void ThemeProvider::scan()
    {
        themes_ = scanThemes(paths_.root, activeLayout(), paths_.layouts);
    }

    void ThemeProvider::refresh()
    {
        themes_.clear();
        newName_.clear();
    }

    void ThemeProvider::enter(const Manager::Screen& screen)
    {
        if (screen.form == "themes" || screen.form == "theme" || screen.form == "theme-new") {
            scan();
        }
        if (screen.form == "theme-new" && newName_.empty()) {
            std::vector<ThemeEditor::ThemeSummary> summaries;
            for (const ThemeFacts& t : themes_) {
                ThemeEditor::ThemeSummary s;
                s.folder = t.folder;
                s.name = t.name;
                summaries.push_back(s);
            }
            newName_ = ThemeEditor::nextThemeName(summaries);
        }
    }

    const ThemeFacts* ThemeProvider::find(const std::string& folder) const
    {
        for (const ThemeFacts& t : themes_) {
            if (t.folder == folder) return &t;
        }
        return nullptr;
    }

    ThemeEditor::SaveInputs ThemeProvider::saveInputs() const
    {
        ThemeEditor::SaveInputs inputs;
        if (paths_.fontBytes) inputs.fontBytes = paths_.fontBytes();
        return inputs;
    }

    std::string ThemeProvider::get(const std::string& key, const Manager::Screen& screen)
    {
        if (key == "newName") return newName_;
        if (key == "newOrientation") return newOrientation_;

        const ThemeFacts* t = find(screen.param("id"));
        bool editor = t && t->kind == ThemeEditor::TF_EDITOR;
        auto flag = [](bool b) { return std::string(b ? "yes" : "no"); };

        if (key == "isEditor") return flag(editor);
        if (key == "isHandMade") return flag(t && !editor);
        if (key == "canEdit") return flag(editor && t->projectOk);
        if (key == "canOpen") return flag(t && (editor || t->hasLayout || t->hasSplash || !t->collectionPages.empty()));
        if (key == "isActive") return flag(t && t->active);
        if (!t) return "";

        if (key == "name") return t->name;
        if (key == "preview") return t->preview;
        if (key == "folder") return t->folder == t->name ? "" : "layouts/" + t->folder;
        if (key == "kind") return editor ? "Made with the theme editor" : "Made by hand";
        if (key == "size") {
            std::string size = sizeText(t->width, t->height);
            return size.empty() && t->hasLayout ? "Follows the screen" : size;
        }
        if (key == "inUse") return t->active ? "Yes, this is the theme RetroFE uses" : "";
        if (key == "pages") {
            if (!t->hasLayout) return editor ? "Not saved yet" : "No layout.xml: RetroFE can't use this folder";
            std::string text = "Main page";
            if (!t->variants.empty()) {
                text += ", " + plural(static_cast<int>(t->variants.size()), "other main page", "other main pages") +
                        " (" + joinNames(t->variants, 4) + ")";
            }
            if (t->hasSplash) text += ", splash page";
            if (!t->collectionPages.empty()) {
                text += ", " + plural(static_cast<int>(t->collectionPages.size()), "collection page", "collection pages") +
                        " (" + joinNames(t->collectionPages, 6) + ")";
            }
            return text;
        }
        if (key == "widgets") {
            if (!editor || !t->projectOk) return "";
            return plural(t->widgets, "widget", "widgets") + " on " + plural(t->editorPages, "page", "pages");
        }
        if (key == "editNote") {
            if (editor && !t->projectOk) {
                return "This theme was made with the editor, but its project (layouts/" + t->folder +
                       "/editor/project.xml) doesn't load: " + t->projectError;
            }
            // A folder with no pages (OneSauce's "default" holds wallpapers) has no Edit to explain
            if (!editor && get("canOpen", screen) == "yes") {
                return "This theme was made by hand. Edit opens its pages in the layout editor, which moves, "
                       "resizes and changes the settings of a page's elements, adds new ones and edits their "
                       "animations, rewriting only what changes. The first save keeps the original page in "
                       "manage/backups, and Put back the original restores it.";
            }
            return "";
        }
        if (key == "problem") {
            return t->hasLayout ? "" : "This folder has no layout.xml, so RetroFE can't use it.";
        }
        return "";
    }

    void ThemeProvider::set(const std::string& key, const std::string& value, const Manager::Screen&)
    {
        if (key == "newName") newName_ = value;
        else if (key == "newOrientation") newOrientation_ = value == "portrait" ? "portrait" : "landscape";
    }

    Forms::Rows ThemeProvider::rows(const std::string& key, size_t limit, const Manager::Screen&)
    {
        Forms::Rows rows;
        if (key != "list") return rows;
        if (themes_.empty()) scan();
        rows.total = themes_.size();
        for (const ThemeFacts& t : themes_) {
            if (rows.items.size() >= limit) break;
            Forms::Row row;
            row.id = t.folder;
            row.label = t.name;
            std::string detail;
            if (t.active) detail = "In use";
            if (t.kind == ThemeEditor::TF_EDITOR) detail += std::string(detail.empty() ? "" : " - ") + "Editor";
            std::string size = sizeText(t.width, t.height);
            if (!size.empty()) detail += std::string(detail.empty() ? "" : " - ") + size.substr(0, size.find(','));
            row.detail = detail;
            rows.items.push_back(row);
        }
        return rows;
    }

    std::vector<std::pair<std::string, std::string>> ThemeProvider::options(const std::string& key, const Manager::Screen&)
    {
        if (key == "orientations") {
            return { { "landscape", "Landscape (1920 x 1080)" }, { "portrait", "Portrait (1080 x 1920)" } };
        }
        return {};
    }

    std::string ThemeProvider::actionProblem(const std::string& action, const Manager::Screen& screen)
    {
        if (action == "create") {
            std::string name = newName_;
            name.erase(0, name.find_first_not_of(" \t"));
            return name.empty() ? "Give the new theme a name." : "";
        }
        const ThemeFacts* t = find(screen.param("id"));
        if (action == "use" || action == "edit" || action == "duplicate" || action == "delete") {
            if (!t) return "This theme isn't installed any more.";
        }
        bool editor = t && t->kind == ThemeEditor::TF_EDITOR;
        if (action == "use") {
            if (!t->hasLayout) return "This theme has no layout.xml yet.";
            if (t->active) return "RetroFE already uses this theme.";
            if (!t->hasSplash) {
                // RetroFE stops at start-up on a layout without one, and then nothing can switch back
                return editor ? "This theme has no splash page yet, and RetroFE won't start without one. "
                                "Edit it and save: saving writes one."
                              : "This theme has no splash.xml, and RetroFE won't start without one.";
            }
            return t->mainProblem;
        }
        if (action == "edit" && !editor) {
            return get("canOpen", screen) == "yes" ? "" : "This folder has no page files to edit.";
        }
        if (action == "edit" || action == "duplicate") {
            if (!editor) return "Only themes made with the theme editor can be duplicated.";
            if (!t->projectOk) return "This theme's project doesn't load: " + t->projectError;
            return "";
        }
        if (action == "delete") {
            if (!editor) return "Only themes made with the theme editor can be deleted here.";
            if (t->active) return "RetroFE uses this theme. Use another theme first.";
            return "";
        }
        return Provider::actionProblem(action, screen);
    }

    Forms::Model::Result ThemeProvider::runAction(const std::string& action, const Manager::Screen& screen)
    {
        Forms::Model::Result result;
        result.message = actionProblem(action, screen);
        if (!result.message.empty()) return result;

        const std::string folder = screen.param("id");
        if (action == "use") {
            if (!setLayout(paths_.root, folder, result.message)) return result;
            LOG_NOTICE("Themes", "Set layout = " + folder);
            services_.requestReload();
            scan();
            result.ok = true;
            result.message = "RetroFE switches to this theme after it reloads, when you close Manage.";
        }
        else if (action == "edit") {
            // Widgets for an editor-made theme; the page's own elements for a hand-made one
            const ThemeFacts* t = find(folder);
            result.ok = true;
            result.open = t && t->kind == ThemeEditor::TF_EDITOR ? editorForm : layoutEditorForm;
            result.keepParams = true;
        }
        else if (action == "create") {
            std::string created;
            if (!ThemeEditor::createTheme(layoutsRoot(), newName_, newOrientation_ == "portrait", widgetLibrary(),
                                          saveInputs(), created, result.message)) {
                return result;
            }
            LOG_NOTICE("Themes", "Created theme layouts/" + created);
            newName_.clear();
            scan();
            result.ok = true;
            result.message = "Created. Choose Edit to add widgets to it.";
            result.open = "theme";
            result.params = { { "id", created } };
        }
        else if (action == "duplicate") {
            std::string copy;
            if (!ThemeEditor::duplicateTheme(layoutsRoot(), folder, widgetLibrary(), saveInputs(), copy, result.message)) {
                return result;
            }
            LOG_NOTICE("Themes", "Duplicated layouts/" + folder + " as layouts/" + copy);
            scan();
            result.ok = true;
            result.message = "This is the copy.";
            result.open = "theme";
            result.params = { { "id", copy } };
        }
        else if (action == "delete") {
            if (!ThemeEditor::deleteTheme(layoutsRoot(), folder, result.message)) return result;
            LOG_NOTICE("Themes", "Deleted layouts/" + folder);
            scan();
            result.ok = true;
            result.message = "Deleted.";
            result.back = true;
        }
        return result;
    }
}

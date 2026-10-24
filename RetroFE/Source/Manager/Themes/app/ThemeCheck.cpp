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
#include "ThemesApp.h"
#include "../ThemeProvider.h"
#include "../../../Database/Configuration.h"
#include "../../../Database/GlobalOpts.h"
#include "../../../Graphics/FontCache.h"
#include "../../../Graphics/LayoutCompat.h"
#include "../../../Graphics/Page.h"
#include "../../../Graphics/PageBuilder.h"
#include "../../../SDL.h"
#include "../../../ThemeEditor/LayoutCheck.h"
#include "../../../ThemeEditor/ThemeGenerate.h"
#include "../../../ThemeEditor/ThemeStore.h"
#include "../../../Utility/Log.h"
#include "../../../Utility/Utils.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;
using namespace ThemeEditor;

namespace
{
    // A page to check: where it lives and what it says
    struct CheckPage
    {
        std::string label;     // "layout.xml", "collections/NES/layout/layout.xml"
        std::string folder;    // the folder its relative paths resolve against
        std::string xml;
        std::string file;      // set for a hand-made page: built from the file itself
    };

    bool readText(const fs::path& path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }

    // Every page file of a hand-made theme
    std::vector<CheckPage> handMadePages(const fs::path& dir)
    {
        std::vector<CheckPage> pages;
        std::error_code ec;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::string name = it->path().filename().string();
            // layout.xml, "layout - N.xml", randomLayout pages ("layout 4x3.xml") and splash.xml
            if (it->is_regular_file(ec) && it->path().extension() == ".xml" &&
                (name == "splash.xml" || name.rfind("layout", 0) == 0)) {
                files.push_back(it->path());
            }
        }
        for (fs::directory_iterator it(dir / "collections", ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code pageEc;
            for (fs::directory_iterator page(it->path() / "layout", pageEc), pageEnd; !pageEc && page != pageEnd; page.increment(pageEc)) {
                if (page->is_regular_file(pageEc) && page->path().extension() == ".xml") files.push_back(page->path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            CheckPage page;
            page.label = fs::relative(file, dir, ec).generic_string();
            page.folder = file.parent_path().string();
            page.file = file.string();
            readText(file, page.xml);
            pages.push_back(page);
        }
        // Fork compat: a layout.lua with no layout.xml beside it draws the XML
        // file its build() names (Graphics/LayoutCompat.h), with the lua file's
        // folder as the page's folder. Each file is checked once.
        std::vector<fs::path> luas = { dir / "layout.lua" };
        for (fs::directory_iterator it(dir / "collections", ec), end; !ec && it != end; it.increment(ec)) {
            luas.push_back(it->path() / "layout" / "layout.lua");
        }
        // Compared as the same file, however the path is spelled
        auto key = [](const fs::path& p) {
            std::error_code keyEc;
            fs::path canonical = fs::weakly_canonical(p, keyEc);
            const std::string spelled = (keyEc ? p.lexically_normal() : canonical).generic_string();
#ifdef WIN32
            return Utils::toLower(spelled);   // Windows paths aren't case-sensitive
#else
            return spelled;
#endif
        };
        std::vector<std::string> seen;
        for (const fs::path& file : files) seen.push_back(key(file));
        for (const fs::path& lua : luas) {
            std::error_code luaEc;
            if (!fs::is_regular_file(lua, luaEc) || fs::exists(lua.parent_path() / "layout.xml", luaEc)) continue;
            const fs::path target = LayoutCompat::luaBuildTarget(lua.string(), dir.string());
            if (target.empty() || !fs::is_regular_file(target, luaEc) ||
                std::find(seen.begin(), seen.end(), key(target)) != seen.end()) {
                continue;
            }
            seen.push_back(key(target));
            CheckPage page;
            page.label = fs::relative(target, dir, luaEc).generic_string() + " (from " + fs::relative(lua, dir, luaEc).generic_string() + ")";
            page.folder = lua.parent_path().string();
            readText(target, page.xml);
            pages.push_back(page);
        }
        return pages;
    }
}

int Themes::runThemeCheck(Configuration& config, const std::string& layout, const std::string& reportFile)
{
    std::vector<std::string> lines;
    int problems = 0;
    auto say = [&lines](const std::string& line) { lines.push_back(line); };
    auto problem = [&](const std::string& line) { problems++; lines.push_back("  problem: " + line); };
    auto finish = [&](int status) {
        lines.push_back(status == 0 ? "OK: nothing wrong found." :
                        status == 1 ? "FAILED: " + std::to_string(problems) + " problem(s)." : "FAILED: couldn't check.");
        std::string text;
        for (const std::string& line : lines) text += line + "\n";
        std::fputs(text.c_str(), stdout);
        std::fflush(stdout);
        if (!reportFile.empty()) {
            std::ofstream out(reportFile, std::ios::binary | std::ios::trunc);
            out << text;
        }
        LOG_NOTICE("ThemeCheck", "Checked layouts/" + layout + ": status " + std::to_string(status));
        return status;
    };

    const std::string layoutsRoot = Configuration::layoutsPath();
    const fs::path dir = fs::path(layoutsRoot) / layout;
    say("Theme check: layouts/" + layout);
    if (!themeFolderValid(layout) || !fs::is_directory(dir)) {
        say("There's no theme folder layouts/" + layout + ".");
        return finish(2);
    }

    // The pages, as RetroFE will read them
    std::vector<CheckPage> pages;
    ThemeFolderKind kind = probeTheme(layoutsRoot, layout);
    if (kind == TF_EDITOR) {
        ThemeProject project;
        std::string error;
        if (!loadTheme(layoutsRoot, layout, project, error)) {
            say("Made with the theme editor, but its project doesn't load: " + error);
            return finish(2);
        }
        say("Made with the theme editor: \"" + project.name + "\", " + std::to_string(project.canvasW()) + " x " +
            std::to_string(project.canvasH()) + ", " + std::to_string(project.pages.size()) + " page(s).");
        const WidgetLibrary& lib = widgetLibrary();
        GenOptions opt;
        opt.themeDir = layout;
        for (const ThemePage& page : project.pages) {
            CheckPage check;
            std::string rel;
            if (page.collection.empty()) rel = "layout.xml";
            else if (ThemeProject::isSplashPage(page.collection)) rel = "splash.xml";
            else rel = "collections/" + page.collection + "/layout/layout.xml";
            check.label = rel;
            check.folder = (dir / fs::path(rel).parent_path()).string();
            GenResult gen = generateLayoutXml(project, page, lib, opt);
            for (const std::string& e : gen.errors) problem(rel + ": " + e);
            for (const std::string& w : gen.warnings) say("  warning: " + rel + ": " + w);
            if (!gen.ok()) continue;
            check.xml = gen.xml;
            // What's on disk should be what the project generates
            std::string saved;
            if (!readText(dir / rel, saved)) problem(rel + " hasn't been saved (save the theme in the editor)");
            else if (saved != gen.xml) say("  note: " + rel + " on disk differs from the project; saving the theme rewrites it");
            pages.push_back(check);
        }
    }
    else if (kind == TF_FOREIGN) {
        say("Made by hand: checking its page files as they are.");
        pages = handMadePages(dir);
        if (pages.empty()) problem("no layout.xml or other page files");
    }
    else {
        say("The folder has no theme in it.");
        return finish(2);
    }

    // What RetroFE needs to start on it
    std::error_code ec;
    if (!fs::is_regular_file(dir / "splash.xml", ec)) {
        problem("no splash.xml: RetroFE won't start on this theme (saving it in the editor writes one)");
    }
    std::string mainXml;
    std::string luaMain;
    if (!fs::exists(dir / "layout.xml", ec) && fs::is_regular_file(dir / "layout.lua", ec)) {
        luaMain = LayoutCompat::luaBuildTarget((dir / "layout.lua").string(), dir.string());
    }
    if (!luaMain.empty() && fs::is_regular_file(luaMain, ec)) {
        say("layout.lua builds " + fs::relative(luaMain, dir, ec).generic_string() + " (RetroFE reads only its build())");
    }
    else if (!readText(dir / "layout.xml", mainXml)) problem("no layout.xml");
    else if (kind == TF_EDITOR && !mainPageProblem(mainXml).empty()) problem("layout.xml: " + mainPageProblem(mainXml));

    // The vocabulary this fork's PageBuilder reads
    for (const CheckPage& page : pages) {
        for (const std::string& issue : checkLayoutXml(page.xml)) problem(page.label + ": " + issue);
    }

    // The real engine: build each page in a hidden window and collect what it logs
    SDL::setHiddenWindows(true);
    if (!SDL::initialize(config)) {
        say("  note: SDL didn't start, so the pages weren't built with PageBuilder (" + std::string(SDL_GetError()) + ")");
    }
    else {
        FontCache fontCache;
        fontCache.initialize();
        std::string previousLayout;
        config.getProperty(OPTION_LAYOUT, previousLayout);
        config.setProperty(OPTION_LAYOUT, layout);
        for (const CheckPage& page : pages) {
            std::string blocker = layoutBuildBlocker(page.xml);
            if (!blocker.empty()) {
                problem(page.label + ": not built: " + blocker);
                continue;
            }
            std::vector<std::string> logged;
            Page* built = nullptr;
            {
                Logger::TapScope tap([&logged](Logger::Zone, const std::string& component, const std::string& message) {
                    if (pageBuildComponent(component)) logged.push_back("[" + component + "] " + message);
                });
                PageBuilder builder(layout, "layout", config, &fontCache);
                if (!page.file.empty()) builder.setLayoutFile(page.file);
                else builder.setLayoutXml(page.xml.c_str(), page.folder);
                built = builder.buildPage();
            }
            if (!built) problem(page.label + ": PageBuilder couldn't build it");
            for (const std::string& line : logged) problem(page.label + ": engine: " + line);
            if (built) {
                built->deInitialize();
                delete built;
                say("  built " + page.label + (logged.empty() ? " cleanly" : ""));
            }
        }
        config.setProperty(OPTION_LAYOUT, previousLayout);
        fontCache.deInitialize();
        SDL::deInitialize();
    }
    return finish(problems ? 1 : 0);
}

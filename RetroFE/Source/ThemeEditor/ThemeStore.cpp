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

#include "ThemeStore.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace ThemeEditor
{
    namespace
    {
        bool readFile(const fs::path& path, std::string& out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return !in.bad();
        }

        // The first line of a file, without its line ending; "" when unreadable
        std::string firstLine(const fs::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return std::string();
            std::string line;
            std::getline(in, line);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            return line;
        }

        bool sameContent(const fs::path& a, const fs::path& b)
        {
            std::error_code ec;
            if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
            std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
            if (!fa || !fb) return false;
            char ba[65536], bb[65536];
            while (fa && fb) {
                fa.read(ba, sizeof(ba));
                fb.read(bb, sizeof(bb));
                if (fa.gcount() != fb.gcount() || std::memcmp(ba, bb, static_cast<size_t>(fa.gcount())) != 0) return false;
            }
            return fa.eof() && fb.eof();
        }

        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // Page positions under a theme folder that carry the editor's marker
        std::vector<std::string> scanEditorPages(const fs::path& dir)
        {
            std::vector<std::string> out;
            for (const char* page : { "layout.xml", "layout1.xml", "splash.xml" }) {
                if (isEditorMarker(firstLine(dir / page))) out.push_back(page);
            }
            std::error_code ec;
            std::vector<std::string> collections;
            for (fs::directory_iterator it(dir / "collections", ec), end; !ec && it != end; it.increment(ec)) {
                if (it->is_directory(ec)) collections.push_back(it->path().filename().string());
            }
            std::sort(collections.begin(), collections.end());
            for (const auto& c : collections) {
                for (const char* page : { "layout.xml", "layout1.xml" }) {
                    const std::string rel = "collections/" + c + "/layout/" + page;
                    if (isEditorMarker(firstLine(dir / rel))) out.push_back(rel);
                }
            }
            return out;
        }

        std::string safeFileName(const std::string& name)
        {
            std::string out;
            for (char c : name) {
                const bool bad = static_cast<unsigned char>(c) < 0x20 || c == '\\' || c == '/' || c == ':' || c == '*' ||
                                 c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
                out += bad ? '_' : c;
            }
            if (out.empty() || out == "." || out == "..") out = "asset";
            return out;
        }
    }

    bool writeFileAtomic(const std::string& path, const std::string& content, std::string& error)
    {
        const fs::path target(path);
        fs::path tmp = target;
        tmp += ".tmp";
        std::error_code ec;
        if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) {
                error = "can't write " + tmp.string();
                return false;
            }
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            out.flush();
            const bool ok = static_cast<bool>(out);
            out.close();
            if (!ok || out.fail()) {
                fs::remove(tmp, ec);
                error = "couldn't write " + path + " (is the drive full?); the existing file was left as it was";
                return false;
            }
        }
        fs::rename(tmp, target, ec);
        if (ec) {
            std::error_code ignore;
            fs::remove(tmp, ignore);
            error = "can't replace " + path + ": " + ec.message();
            return false;
        }
        return true;
    }

    bool themeFolderValid(const std::string& folder)
    {
        if (folder.empty() || folder == "." || folder == "..") return false;
        return folder.find_first_of("/\\:") == std::string::npos;
    }

    ThemeFolderKind probeTheme(const std::string& layoutsRoot, const std::string& folder)
    {
        if (!themeFolderValid(folder)) return TF_FOREIGN;
        const fs::path dir = fs::path(layoutsRoot) / folder;
        std::error_code ec;
        if (!fs::exists(dir, ec)) return TF_FRESH;
        if (!fs::is_directory(dir, ec)) return TF_FOREIGN;
        const fs::path layout = dir / "layout.xml";
        if (fs::exists(layout, ec)) return isEditorMarker(firstLine(layout)) ? TF_EDITOR : TF_FOREIGN;
        if (fs::exists(dir / PROJECT_FILE, ec)) return TF_EDITOR;
        if (fs::is_empty(dir, ec) && !ec) return TF_FRESH;
        return TF_FOREIGN;
    }

    bool loadTheme(const std::string& layoutsRoot, const std::string& folder, ThemeProject& out, std::string& error)
    {
        if (!themeFolderValid(folder)) { error = "invalid theme folder '" + folder + "'"; return false; }
        std::string xml;
        if (!readFile(fs::path(layoutsRoot) / folder / PROJECT_FILE, xml)) {
            error = "'" + folder + "' has no theme editor project (" + PROJECT_FILE + ")";
            return false;
        }
        return projectParse(xml, out, error);
    }

    std::vector<ThemeSummary> listThemes(const std::string& layoutsRoot)
    {
        std::vector<ThemeSummary> out;
        std::error_code ec;
        for (fs::directory_iterator it(layoutsRoot, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            ThemeSummary s;
            s.folder = it->path().filename().string();
            s.name = s.folder;
            s.kind = probeTheme(layoutsRoot, s.folder);
            std::error_code pec;
            s.hasProject = fs::exists(it->path() / PROJECT_FILE, pec);
            if (s.hasProject) {
                ThemeProject p;
                if (loadTheme(layoutsRoot, s.folder, p, s.error)) {
                    s.projectOk = true;
                    s.name = p.name;
                    s.vertical = p.vertical;
                    s.pages = static_cast<int>(p.pages.size());
                    for (const auto& pg : p.pages) s.widgets += static_cast<int>(pg.widgets.size());
                }
            }
            out.push_back(s);
        }
        std::sort(out.begin(), out.end(), [](const ThemeSummary& a, const ThemeSummary& b) {
            const std::string la = lower(a.name), lb = lower(b.name);
            if (la != lb) return la < lb;
            return a.folder < b.folder;
        });
        return out;
    }

    bool saveTheme(const std::string& layoutsRoot, const std::string& folder, const ThemeProject& p,
                   const WidgetLibrary& lib, const SaveInputs& inputs, SaveReport* report, std::string& error)
    {
        error.clear();
        if (report) *report = SaveReport();
        if (!themeFolderValid(folder)) { error = "invalid theme folder '" + folder + "'"; return false; }
        if (probeTheme(layoutsRoot, folder) == TF_FOREIGN) {
            error = "'" + folder + "' wasn't made with the theme editor, so it won't be overwritten";
            return false;
        }
        SaveInputs in = inputs;
        if (in.themeDir.empty()) in.themeDir = folder;   // pages refer to the folder they're in
        const SavePlan plan = buildSavePlan(p, lib, in);
        if (!plan.ok()) {
            error = plan.errors[0];
            if (plan.errors.size() > 1) error += " (and " + std::to_string(plan.errors.size() - 1) + " more)";
            return false;
        }

        const fs::path dir = fs::path(layoutsRoot) / folder;
        const std::vector<std::string> before = scanEditorPages(dir);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!fs::is_directory(dir, ec)) { error = "can't create " + dir.string(); return false; }

        std::vector<std::string> planPaths;
        for (const auto& f : plan.files) {
            planPaths.push_back(f.relPath);
            const fs::path path = dir / f.relPath;
            std::string existing;
            if (readFile(path, existing) && existing == f.content) continue;
            if (!writeFileAtomic(path.string(), f.content, error)) return false;
            if (report) report->written.push_back(f.relPath);
        }

        // Only once everything new is in place: remove pages the previous save
        // wrote that this one doesn't (marker-checked, so never a hand-made page)
        for (const std::string& rel : stalePages(planPaths, before)) {
            const fs::path path = dir / rel;
            fs::remove(path, ec);
            if (fs::exists(path, ec)) {
                error = "couldn't remove the old page " + rel + "; it would keep overriding that collection";
                return false;
            }
            if (report) report->removed.push_back(rel);
            if (rel.compare(0, 12, "collections/") == 0) {
                // Its emptied layout/ folder and collection folder go too
                const fs::path layoutDir = path.parent_path();
                if (fs::is_empty(layoutDir, ec) && !ec) fs::remove(layoutDir, ec);
                const fs::path collectionDir = layoutDir.parent_path();
                if (fs::is_empty(collectionDir, ec) && !ec) fs::remove(collectionDir, ec);
            }
        }

        if (report) {
            report->warnings = plan.warnings;
            // Files the widgets point at that aren't in the theme
            for (const auto& page : p.pages) {
                const std::string label = page.collection.empty() ? std::string("Default") : page.displayName();
                for (const auto& w : page.widgets) {
                    const WidgetDef* def = lib.find(w.def);
                    if (!def) continue;
                    for (const auto& v : def->vars) {
                        if (v.type != "file") continue;
                        const std::string value = w.varValue(v.token, v.def);
                        if (!value.empty() && !fs::exists(dir / value, ec)) {
                            report->warnings.push_back(label + ": widget '" + w.id + "': " + value + " isn't in the theme folder");
                        }
                    }
                }
            }
        }
        return true;
    }

    std::string freeThemeFolder(const std::string& layoutsRoot, const std::string& name)
    {
        const std::string base = themeFolderName(name);
        std::string candidate = base;
        std::error_code ec;
        for (int n = 2; fs::exists(fs::path(layoutsRoot) / candidate, ec); ++n) candidate = base + " " + std::to_string(n);
        return candidate;
    }

    std::string nextThemeName(const std::vector<ThemeSummary>& themes)
    {
        for (int n = 1;; ++n) {
            const std::string want = "New Theme " + std::to_string(n);
            const std::string wantLower = lower(want);
            bool taken = false;
            for (const auto& t : themes) {
                if (lower(t.name) == wantLower || lower(t.folder) == wantLower) { taken = true; break; }
            }
            if (!taken) return want;
        }
    }

    bool createTheme(const std::string& layoutsRoot, const std::string& name, bool vertical, const WidgetLibrary& lib,
                     const SaveInputs& inputs, std::string& folderOut, std::string& error)
    {
        std::string clean = name;
        while (!clean.empty() && std::isspace(static_cast<unsigned char>(clean.back()))) clean.pop_back();
        while (!clean.empty() && std::isspace(static_cast<unsigned char>(clean.front()))) clean.erase(clean.begin());
        if (clean.empty()) clean = "New Theme";
        ThemeProject p;
        p.name = clean;
        p.vertical = vertical;
        folderOut = freeThemeFolder(layoutsRoot, clean);
        return saveTheme(layoutsRoot, folderOut, p, lib, inputs, nullptr, error);
    }

    bool duplicateTheme(const std::string& layoutsRoot, const std::string& folder, const WidgetLibrary& lib,
                        const SaveInputs& inputs, std::string& folderOut, std::string& error)
    {
        folderOut.clear();
        if (!themeFolderValid(folder)) { error = "invalid theme folder '" + folder + "'"; return false; }
        if (probeTheme(layoutsRoot, folder) != TF_EDITOR) {
            error = "'" + folder + "' wasn't made with the theme editor";
            return false;
        }
        ThemeProject p;
        if (!loadTheme(layoutsRoot, folder, p, error)) return false;
        p.name += " Copy";
        const std::string target = freeThemeFolder(layoutsRoot, p.name);
        const fs::path src = fs::path(layoutsRoot) / folder;
        const fs::path dst = fs::path(layoutsRoot) / target;
        std::error_code ec;
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::skip_symlinks, ec);
        if (ec) {
            std::error_code ignore;
            fs::remove_all(dst, ignore);
            error = "couldn't copy '" + folder + "': " + ec.message();
            return false;
        }
        if (!saveTheme(layoutsRoot, target, p, lib, inputs, nullptr, error)) {
            std::error_code ignore;
            fs::remove_all(dst, ignore);
            return false;
        }
        folderOut = target;
        return true;
    }

    bool deleteTheme(const std::string& layoutsRoot, const std::string& folder, std::string& error)
    {
        if (!themeFolderValid(folder)) { error = "invalid theme folder '" + folder + "'"; return false; }
        const fs::path dir = fs::path(layoutsRoot) / folder;
        std::error_code ec;
        if (!fs::exists(dir, ec)) { error = "'" + folder + "' doesn't exist"; return false; }
        if (probeTheme(layoutsRoot, folder) != TF_EDITOR) {
            error = "'" + folder + "' wasn't made with the theme editor, so it won't be deleted";
            return false;
        }
        fs::remove_all(dir, ec);
        if (ec || fs::exists(dir)) {
            error = "couldn't delete '" + folder + "'" + (ec ? ": " + ec.message() : std::string());
            return false;
        }
        return true;
    }

    bool importAsset(const std::string& layoutsRoot, const std::string& folder, const std::string& sourcePath,
                     std::string& relOut, std::string& error)
    {
        relOut.clear();
        if (!themeFolderValid(folder)) { error = "invalid theme folder '" + folder + "'"; return false; }
        if (probeTheme(layoutsRoot, folder) == TF_FOREIGN) {
            error = "'" + folder + "' wasn't made with the theme editor";
            return false;
        }
        std::error_code ec;
        const fs::path src(sourcePath);
        if (!fs::is_regular_file(src, ec)) { error = "not a readable file: " + sourcePath; return false; }
        const fs::path dir = fs::path(layoutsRoot) / folder / ASSETS_FOLDER;
        fs::create_directories(dir, ec);
        const std::string base = safeFileName(src.filename().string());
        const size_t dot = base.find_last_of('.');
        const std::string stem = (dot == std::string::npos || dot == 0) ? base : base.substr(0, dot);
        const std::string ext = (dot == std::string::npos || dot == 0) ? std::string() : base.substr(dot);
        std::string name = base;
        for (int n = 2;; ++n) {
            const fs::path dst = dir / name;
            if (!fs::exists(dst, ec)) {
                fs::path tmp = dst;
                tmp += ".tmp";
                fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
                if (!ec) fs::rename(tmp, dst, ec);
                if (ec) {
                    std::error_code ignore;
                    fs::remove(tmp, ignore);
                    error = "couldn't copy " + sourcePath + ": " + ec.message();
                    return false;
                }
                break;
            }
            if (sameContent(src, dst)) break;   // the same file: reuse it
            name = stem + "-" + std::to_string(n) + ext;
        }
        relOut = std::string(ASSETS_FOLDER) + "/" + name;
        return true;
    }

    WidgetFiles readWidgetFolder(const std::string& dir)
    {
        WidgetFiles out;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".widget") continue;
            std::string text;
            if (readFile(it->path(), text)) out.emplace_back(it->path().filename().string(), text);
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return out;
    }
}

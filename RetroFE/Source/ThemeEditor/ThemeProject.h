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

// The theme editor's project model: the editable source of truth for an
// editor-made theme. A project is a named, oriented set of pages (the Default
// page plus collection templates), each holding placed widget instances. The
// layout XML is always generated from this model (ThemeGenerate.h) and never
// edited in place.
//
// Stored as project.xml (attribute-only elements), parsed with rapidxml. The
// parser is strict on purpose: a truncated or broken project.xml fails to load
// with a reason instead of loading with pages missing.
//
// Ported from One Saucier's theme editor (app/editor/theme_project.*). Pure:
// no SDL, no ImGui, no filesystem. See docs/design/theme-editor.md.

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ThemeEditor
{
    using Attributes = std::vector<std::pair<std::string, std::string>>;

    // The highest layer the engine draws: Page has NUM_LAYERS = 20 (0..19)
    // and drops a component on any other layer. Every layer the editor
    // writes, offsets included, is clamped to 0..MAX_LAYER.
    static const int MAX_LAYER = 19;
    static inline int clampLayer(int layer) { return layer < 0 ? 0 : (layer > MAX_LAYER ? MAX_LAYER : layer); }

    struct WidgetInstance
    {
        std::string def;    // the WidgetDef id this was placed from
        std::string id;     // [a-z0-9_]+, unique per page; written as the component's id
        std::string name;   // display name ("Game Video 1")
        int x = 0, y = 0;   // theme units (1920x1080 or 1080x1920)
        int w = 0, h = 0;
        int layer = 0;      // 0..MAX_LAYER
        int alphaPct = 100; // opacity 0..100, written as alpha="0.NN"

        // A per-widget font (a theme-relative path); empty uses the theme's font.
        std::string font;
        // The widget's font colour (6 hex digits, no '#'); empty = not set.
        std::string fontColor;

        // One Saucier's "Transform Video": four free corners, TL, TR, BL, BR.
        // This engine has no transform= attribute, so the generator draws a
        // warped widget as the corners' bounding box (and says so). Kept so a
        // One Saucier project round-trips.
        bool warp = false;
        int wx[4] = { 0, 0, 0, 0 };
        int wy[4] = { 0, 0, 0, 0 };
        int wwx = 0, wwy = 0, www = 0, wwh = 0;   // the transform's source window

        void warpFromRect();
        void warpBounds(int& bx, int& by, int& bw, int& bh) const;

        // Editor-only flags, never written into the layout
        bool autoScroll = true;     // the canvas scrolls this wheel to demonstrate it
        bool canvasHide = false;    // hidden on the canvas only

        // Menu widgets: the wheel's shape (0 / -1 = the definition's default)
        int menuSlots = 0;
        int menuFadePct = 0;
        int menuCurvePx = 0;
        int menuAngleDeg = 0;
        int menuPerspDeg = 0;
        int menuItemH = 0;
        bool menuPulse = false;
        int menuIdleDimPct = -1;
        bool menuGhost = false;
        int menuGhostDx = -1;
        int menuGhostSpanPct = -1;
        int menuGhostItemPct = -1;
        int menuGhostAlphaPct = -1;
        std::string menuGhostMode;
        int menuScrollMs = 0;
        int menuAccelMs = 0;

        // Art strips (linear ribbons)
        int stripPitchPx = 0;
        int stripItemMaxW = 0;
        int stripDimPct = -1;
        int stripSelW = 0;
        int stripSelH = 0;
        // A verbatim ladder: itemDefaults and item attribute strings, '|'-joined,
        // plus each item's child XML. Non-empty replaces the computed slots.
        std::string stripItemDefsRaw;
        std::string stripItemsRaw;
        std::string stripItemKidsRaw;

        // Variable overrides, token -> value ("true"/"false" for bools). A token
        // that isn't here takes the definition's default.
        Attributes vars;

        // Attributes of <widget> this version doesn't know, kept as they were
        Attributes extra;

        std::string varValue(const std::string& token, const std::string& fallback) const;
        void setVar(const std::string& token, const std::string& value);
        void clearVar(const std::string& token);
    };

    struct ThemePage
    {
        std::string collection;             // "" = the Default page
        std::string tplName;                // template name (defaults to the collection)
        std::vector<std::string> members;   // further collections using this template
        std::vector<WidgetInstance> widgets;

        // Second-screen pages (One Saucier's layout1.xml). Kept so a project
        // round-trips; not exported yet (this engine places components on a
        // monitor with monitor= rather than a second file).
        std::vector<ThemePage> mons;
        int monW = 0, monH = 0;
        int monSpaceW() const { return monW ? monW : 1920; }
        int monSpaceH() const { return monH ? monH : 1080; }

        bool covers(const std::string& c) const;
        std::string displayName() const;
        const WidgetInstance* findWidget(const std::string& id) const;
    };

    struct ThemeProject
    {
        static const int VERSION = 1;
        std::string name = "New Theme";
        bool vertical = false;
        std::vector<ThemePage> pages;   // pages[0] is always the Default page

        // Theme properties: theme-relative paths, empty = not set
        std::string font;               // empty = the bundled Roboto
        std::string soundLoad, soundUnload, soundHighlight, soundSelect;

        // An imported theme's own coordinate space; 0 = 1920x1080 / 1080x1920
        int authorW = 0, authorH = 0;

        // Root attributes this version doesn't use (One Saucier's artSource,
        // intro and THEMES-collection art), kept as they were
        Attributes extra;

        ThemeProject() { pages.push_back(ThemePage()); }

        // The splash page's reserved collection name ('*' can't be in a folder name)
        static const char* splashPage() { return "*Splash*"; }
        static bool isSplashPage(const std::string& c) { return c == "*Splash*"; }

        int canvasW() const { return authorW ? authorW : (vertical ? 1080 : 1920); }
        int canvasH() const { return authorH ? authorH : (vertical ? 1920 : 1080); }

        ThemePage* page(const std::string& collection);
        const ThemePage* page(const std::string& collection) const;
        // The page covering a collection: its own, or the template it's a member of
        ThemePage* coveredBy(const std::string& collection);
        const ThemePage* coveredBy(const std::string& collection) const;
    };

    // project.xml. projectParse repairs a missing or misplaced Default page but
    // fails on anything structurally broken: malformed XML, no <themeProject>,
    // a newer version, a widget without def/id, duplicate ids on a page.
    std::string projectSerialize(const ThemeProject& p);
    bool projectParse(const std::string& xml, ThemeProject& out, std::string& error);

    // Escapes the five XML entities
    std::string xmlEscape(const std::string& s);

    // Lowercase [a-z0-9-] slug, runs collapsed, never empty ("New Theme 1" -> "new-theme-1")
    std::string editorSlug(const std::string& name);

    // A folder name for a theme under layouts/: characters Windows forbids and
    // control bytes become spaces, runs collapse, edges trim (trailing dots
    // too), device names (CON, COM1, ...) get " Theme" added. Never empty.
    std::string themeFolderName(const std::string& themeName);

    // Is `name` safe as one folder name on every platform: not empty, not
    // "." or "..", no control or Windows-forbidden characters, no leading
    // space or dot, no trailing space or dot, not a device name. Whatever
    // themeFolderName returns passes.
    bool folderNameSafe(const std::string& name);

    // The next numbered identity for placing a widget on a page:
    // "<defId>_<n>" / "<defName> <n>", n one past the highest in use
    void nextIdentity(const ThemePage& page, const std::string& defId, const std::string& defName,
                      std::string& outId, std::string& outName);

    // Switching orientation scales every page's geometry per axis between
    // 1920x1080 and 1080x1920. A no-op when the orientation already matches.
    void reflowOrientation(ThemeProject& p, bool vertical);

    // Collection templates. Creating seeds a copy of `seed` (nullptr = blank);
    // the one-argument form copies the Default page. An existing page is
    // returned as it is. The Default page can't be deleted.
    ThemePage* createTemplate(ThemeProject& p, const std::string& collection);
    ThemePage* createTemplate(ThemeProject& p, const std::string& collection, const ThemePage* seed);
    bool deleteTemplate(ThemeProject& p, const std::string& collection);

    // Shared templates: attach adds a collection to the template whose primary
    // is tplPrimary (refused when the collection has its own page); detach
    // removes a member.
    bool attachCollection(ThemeProject& p, const std::string& tplPrimary, const std::string& collection);
    bool detachCollection(ThemeProject& p, const std::string& collection);

    // Moves a collection to the template whose primary is targetPrimary
    // ("" = Default). Returns the primary of a template that emptied and was
    // removed, else "".
    std::string reassign(ThemeProject& p, const std::string& collection, const std::string& targetPrimary);

    // The template that applies to a collection: its own or its membership,
    // else the nearest covered ancestor in the menu tree (the engine keeps the
    // current page when a collection has no override). nullptr = Default.
    const ThemePage* effectiveCover(const ThemeProject& p, const std::string& collection,
                                    const std::function<std::string(const std::string&)>& parentOf,
                                    bool* inherited = nullptr);

    // Creates a named template for a collection, seeded from seedPrimary
    // ("" = Default, "\x01" = blank, else that template). `erased` gets the
    // primary of a template that emptied and was removed.
    ThemePage* createNamed(ThemeProject& p, const std::string& collection, const std::string& name,
                           const std::string& seedPrimary, std::string* erased = nullptr);

    // Deletes a template, moving its collections to replacementPrimary ("" = Default)
    bool deleteTemplateTo(ThemeProject& p, const std::string& primary, const std::string& replacementPrimary);

    // The template RetroFE uses for every menu of collections that has no
    // page of its own: this fork's PageBuilder reads collections/_menu/layout
    // for those (Graphics/LayoutPages.h)
    static const char* const MENUS_TEMPLATE = "_menu";

    // Which page this fork's RetroFE shows for a collection. Unlike One
    // Saucier's engine, a collection never inherits its parent menu's page
    // here: entering a collection always builds a page from
    // collections/<c>/layout, else (for a menu of collections)
    // collections/_menu/layout, else the theme's own layout.xml.
    enum class PageVia { Own, Member, Menus, Default };
    const ThemePage& pageShownFor(const ThemeProject& p, const std::string& collection, bool isMenu,
                                  PageVia* via = nullptr);
}

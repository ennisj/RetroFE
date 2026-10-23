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

// Widget definitions: the theme editor's unit of composition. A widget is a
// fragment of layout XML with {{TOKEN}} sites (see ThemeGenerate.h), plus the
// variables the properties panel edits.
//
// Definitions are data. The stock set lives in Source/ThemeEditor/widgets/
// and is compiled in at build time (CMake/EmbedWidgets.cmake); more can be
// laid over it from a folder with overlayWidgetFiles.
//
// The .widget format is sectioned plain text rather than XML, because the
// fragment IS XML and nesting it would mean escaping every bracket. A line
// whose trimmed form is [widget], [var], [fragment] or [animDefaults] starts a
// section; [widget] and [var] hold "key = value" lines (# comments allowed);
// [fragment] and [animDefaults] keep their lines verbatim. Any other [name]
// line is an error, so a typo can't swallow the rest of the file.
//
// Ported from One Saucier's theme editor (app/editor/widget_defs.*). Pure.

#include <string>
#include <utility>
#include <vector>

namespace ThemeEditor
{
    struct WidgetVar
    {
        std::string token;   // "LOOP": used as {{LOOP}}, {{LOOP:a:b}}, {{?LOOP}}
        std::string name;    // label in the properties panel
        std::string type;    // "bool" | "text" | "file" (a theme-relative path) | "raw" (substituted verbatim)
        std::string def;     // default: "true"/"false" for bool
        std::string filter;  // file vars: "image" | "video" | "text"
        std::vector<std::string> options;   // text vars: a dropdown ('-' in the file = the empty value)
        bool numeric = false;               // text vars: a stepper
        double step = 1.0;
        double minV = 0.0, maxV = 0.0;      // clamp, when minV < maxV
    };

    struct WidgetDef
    {
        std::string id;        // [a-z0-9_]+, unique in the library
        std::string name;      // "Game Video"
        std::string category;  // "static" | "text" | "media" | "menu"
        std::string color;     // identity colour, 6 hex digits
        std::string glyph;     // short palette label ("T")
        std::string thumb;     // palette thumbnail path (optional)
        std::string summary;   // one-line description
        std::string info;      // the fixed attributes, shown read-only
        std::string base;      // a preset: fragment, animations and vars come from this widget
        bool warpable = false; // One Saucier's transform= widgets; this engine can't warp, see ThemeGenerate.h
        int w = 0, h = 0;      // default size when placed
        int minW = 8, minH = 8;
        int layer = 0;         // default layer 0..20
        bool menu = false;     // a scrolling menu (its fragment names {{MENU_ITEMS}})
        bool horizontal = false;
        bool strip = false;    // an art strip: a ribbon of neighbouring items
        int arc = 0;           // menu bulge, percent of width (-100..100)
        std::string fragment;
        std::string animDefaults;
        std::vector<WidgetVar> vars;

        const WidgetVar* var(const std::string& token) const;
    };

    // Parses one .widget file. False with a one-line reason on any problem.
    bool parseWidgetDef(const std::string& text, WidgetDef& out, std::string& error);

    struct WidgetLibrary
    {
        std::vector<WidgetDef> defs;   // palette order = file order
        const WidgetDef* find(const std::string& id) const;
    };

    using WidgetFiles = std::vector<std::pair<std::string, std::string>>;   // (file name, contents)

    // Builds a library from files (the caller reads them). Fails on the first
    // unparsable file or duplicate id, naming the file, and resolves presets
    // (one level: a base can't itself have a base).
    bool buildWidgetLibrary(const WidgetFiles& files, WidgetLibrary& out, std::string& error);

    // The stock widget files compiled into RetroFE, sorted by file name
    WidgetFiles embeddedWidgetFiles();

    // Lays `overlay` over `base`: a file with a base file's name replaces it,
    // others are added. The result is sorted by file name.
    WidgetFiles overlayWidgetFiles(const WidgetFiles& base, const WidgetFiles& overlay);

    // Exactly 6 hex digits, no '#'
    bool widgetColorValid(const std::string& c);
    // [a-z0-9_]+, non-empty: shared by definition and instance ids
    bool widgetIdentValid(const std::string& s);
}

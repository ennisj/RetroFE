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

#include "LayoutCheck.h"
#include "ThemeProject.h"
#include <rapidxml.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

// Every list here mirrors Graphics/PageBuilder.cpp (and Animate/Tween.cpp for
// the tween names). When PageBuilder learns an attribute, add it here; the
// drift test fails if a name here stops appearing there.

namespace ThemeEditor
{
    namespace
    {
        using Node = rapidxml::xml_node<>;
        using Names = std::set<std::string>;

        // PageBuilder::buildViewInfo, read for every component and menu item
        const char* const VIEW_INFO[] = {
            "x", "y", "xOffset", "yOffset", "xOrigin", "yOrigin", "height", "width", "fontSize", "fontColor",
            "minHeight", "minWidth", "maxHeight", "maxWidth", "alpha", "angle", "layer", "backgroundColor",
            "backgroundAlpha", "reflection", "reflectionDistance", "reflectionScale", "reflectionAlpha",
            "containerX", "containerY", "containerWidth", "containerHeight", "monitor", "volume", "restart",
            "additive", "pauseOnScroll", "transform", nullptr
        };
        // PageBuilder::addFont, read from the component itself
        const char* const FONT[] = { "font", "fontColor", "loadFontSize", nullptr };

        // The animation events PageBuilder::createTweenInstance reads
        const char* const EVENTS[] = {
            "onEnter", "onExit", "onIdle", "onMenuIdle", "onMenuScroll", "onPlaylistScroll", "onHighlightEnter",
            "onHighlightExit", "onMenuEnter", "onMenuExit", "onGameEnter", "onGameExit", "onPlaylistEnter",
            "onPlaylistExit", "onPlaylistNextEnter", "onPlaylistNextExit", "onPlaylistPrevEnter", "onPlaylistPrevExit",
            "onMenuJumpEnter", "onMenuJumpExit", "onAttractEnter", "onAttract", "onAttractExit", "onJukeboxJump",
            "onGameInfoEnter", "onGameInfoExit", "onCollectionInfoEnter", "onCollectionInfoExit", "onBuildInfoEnter",
            "onBuildInfoExit", "onMenuActionInputEnter", "onMenuActionInputExit", "onMenuActionSelectEnter",
            "onMenuActionSelectExit", nullptr
        };

        // Tween::getTweenProperty (matched lower case)
        const char* const TWEENS[] = {
            "x", "y", "angle", "alpha", "width", "height", "xorigin", "yorigin", "xoffset", "yoffset", "fontsize",
            "backgroundalpha", "maxwidth", "maxheight", "layer", "containerx", "containery", "containerwidth",
            "containerheight", "volume", "nop", "restart", nullptr
        };
        // Tween::getTweenType (lower case; an unknown name silently means linear).
        // "easeonoutquintic" is the engine's own spelling.
        const char* const ALGORITHMS[] = {
            "easeinquadratic", "easeoutquadratic", "easeinoutquadratic", "easeincubic", "easeoutcubic",
            "easeinoutcubic", "easeinquartic", "easeoutquartic", "easeinoutquartic", "easeinquintic",
            "easeoutquintic", "easeonoutquintic", "easeinsine", "easeoutsine", "easeinoutsine", "easeinexponential",
            "easeoutexponential", "easeinoutexponential", "easeincircular", "easeoutcircular", "easeinoutcircular",
            "linear", nullptr
        };

        // The words PageBuilder::getAnimationEvents and get{Horizontal,Vertical}Alignment
        // read in from= / to= (anything else that isn't a number is 0)
        const Names TWEEN_KEYWORDS = { "left", "top", "center", "right", "bottom", "stretch" };

        struct ElementRule
        {
            const char* name;
            bool viewInfo;               // reads buildViewInfo + addFont attributes
            const char* const* own;      // attributes it reads besides those
        };

        // Component attributes read in PageBuilder::buildComponents / loadReloadableImages / buildMenu.
        // "id" is accepted everywhere: it carries the editor's widget id (PageBuilder
        // reads it as a number where it reads it at all, and nothing uses it).
        const char* const CONTAINER[] = { "id", "menuScrollReload", nullptr };
        const char* const IMAGE[] = { "id", "src", "menuScrollReload", nullptr };
        const char* const VIDEO[] = { "id", "src", "numLoops", "softOverlay", "menuScrollReload", "animationDoneRemove", nullptr };
        const char* const TEXT[] = { "id", "value", "menuScrollReload", nullptr };
        const char* const STATUS_TEXT[] = { "id", "menuScrollReload", nullptr };
        const char* const MEDIA[] = {
            "id", "type", "imageType", "mode", "selectedOffset", "randomSelect", "jukebox", "jukeboxNumLoops",
            "textFallback", "useTextureCache", "menuScrollReload", "textFormat", nullptr
        };
        const char* const RELOADABLE_TEXT[] = {
            "id", "type", "mode", "textFormat", "timeFormat", "location", "singlePrefix", "singlePostfix",
            "pluralPrefix", "pluralPostfix", "menuScrollReload", nullptr
        };
        const char* const SCROLLING_TEXT[] = {
            "id", "type", "mode", "textFormat", "location", "singlePrefix", "singlePostfix", "pluralPrefix",
            "pluralPostfix", "alignment", "selectedOffset", "direction", "scrollingSpeed", "startPosition",
            "startTime", "endTime", "menuScrollReload", nullptr
        };
        // Fork compat (OneSauce themes): a text file scroller, src resolved like an image's
        const char* const FILE_SCROLLING_TEXT[] = {
            "id", "src", "textFormat", "alignment", "direction", "scrollingSpeed", "startPosition", "startTime",
            "endTime", "menuScrollReload", nullptr
        };
        const char* const HISCORES[] = {
            "id", "textFormat", "selectedOffset", "scrollingSpeed", "startTime", "excludedColumns",
            "baseColumnPadding", "baseRowPadding", "maxRows", "menuScrollReload", nullptr
        };
        const char* const MENU[] = {
            "id", "type", "mode", "imageType", "videoType", "scrollTime", "scrollAcceleration", "minScrollTime",
            "orientation", "selectedImage", "textFallback", "useTextureCache", "usePerspective", "topLeft",
            "topRight", "bottomLeft", "bottomRight", "menuIndex", nullptr
        };
        const char* const MENU_ITEM[] = { "selected", "index", "spacing", nullptr };
        const char* const SOUND[] = { "type", "src", nullptr };
        const char* const LAYOUT[] = { "width", "height", "font", "fontColor", "loadFontSize", "minShowTime", "controls", "monitor", nullptr };
        const char* const EVENT[] = { "menuIndex", nullptr };
        const char* const SET[] = { "duration", nullptr };
        const char* const ANIMATE[] = { "type", "from", "to", "algorithm", "setting", "playlist", nullptr };

        const ElementRule COMPONENTS[] = {
            { "container", true, CONTAINER },
            { "image", true, IMAGE },
            { "video", true, VIDEO },
            { "text", true, TEXT },
            { "statusText", true, STATUS_TEXT },
            { "reloadableImage", true, MEDIA },
            { "reloadableAudio", true, MEDIA },
            { "reloadableVideo", true, MEDIA },
            { "reloadableText", true, RELOADABLE_TEXT },
            { "reloadableScrollingText", true, SCROLLING_TEXT },
            { "scrollingText", true, FILE_SCROLLING_TEXT },
            { "reloadableHiscores", true, HISCORES },
            { "menu", true, MENU },
        };

        // Built by PageBuilder::buildForms; the editor never writes them, so
        // their attributes aren't checked
        const char* const UNCHECKED[] = { "form", "keyboard", nullptr };

        const char* const MEDIA_MODES[] = { "system", "systemlayout", "layout", "commonlayout", "common", "menu", "layout_preferred", nullptr };
        const char* const MENU_MODES[] = { "layout", "common", "commonlayout", nullptr };
        const char* const SOUND_TYPES[] = { "load", "unload", "highlight", "select", nullptr };

        bool inList(const std::string& name, const char* const* list)
        {
            for (; *list; ++list) {
                if (name == *list) return true;
            }
            return false;
        }

        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // Page::NUM_LAYERS is 20 (the drift test checks it)
        const int LAST_LAYER = MAX_LAYER;

        bool number(const std::string& s, double& out)
        {
            if (s.empty()) return false;
            char* end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && *end == '\0';
        }

        bool integer(const std::string& s, double& out)
        {
            return number(s, out) && out == static_cast<double>(static_cast<long>(out));
        }

        std::string nameOf(const Node* n) { return std::string(n->name(), n->name_size()); }

        const rapidxml::xml_attribute<>* attribute(const Node* n, const char* name) { return n->first_attribute(name); }

        std::string value(const Node* n, const char* name)
        {
            const rapidxml::xml_attribute<>* a = n->first_attribute(name);
            return a ? std::string(a->value(), a->value_size()) : std::string();
        }

        std::string describe(const Node* n)
        {
            std::string d = "<" + nameOf(n);
            if (const rapidxml::xml_attribute<>* id = n->first_attribute("id")) d += " id=\"" + std::string(id->value()) + "\"";
            return d + ">";
        }

        struct Checker
        {
            std::vector<std::string> issues;

            void issue(const Node* n, const std::string& what) { issues.push_back(describe(n) + ": " + what); }

            // The values the engine reads as plain numbers (convertInt/convertFloat
            // turn anything else into 0 without a word)
            void checkValues(const Node* n)
            {
                double v = 0;
                if (attribute(n, "layer")) {
                    if (!integer(value(n, "layer"), v)) issue(n, "layer '" + value(n, "layer") + "' isn't a whole number");
                    else if (v < 0 || v > LAST_LAYER) issue(n, "layer " + value(n, "layer") + " is outside 0.." + std::to_string(LAST_LAYER) + " (the engine drops it)");
                }
                if (attribute(n, "alpha")) {
                    if (!number(value(n, "alpha"), v)) issue(n, "alpha '" + value(n, "alpha") + "' isn't a number");
                    else if (v < 0 || v > 1) issue(n, "alpha " + value(n, "alpha") + " is outside 0..1");
                }
                if (attribute(n, "duration") && (!number(value(n, "duration"), v) || v < 0)) {
                    issue(n, "duration '" + value(n, "duration") + "' isn't a number of seconds");
                }
                if (attribute(n, "loadFontSize") && (!integer(value(n, "loadFontSize"), v) || v < 1)) {
                    issue(n, "loadFontSize '" + value(n, "loadFontSize") + "' isn't a size");
                }
            }

            void checkAttributes(const Node* n, bool viewInfo, const char* const* own)
            {
                checkValues(n);
                for (const rapidxml::xml_attribute<>* a = n->first_attribute(); a; a = a->next_attribute()) {
                    const std::string name(a->name(), a->name_size());
                    if (inList(name, own)) continue;
                    if (viewInfo && (inList(name, VIEW_INFO) || inList(name, FONT))) continue;
                    issue(n, "attribute '" + name + "' isn't read by this engine");
                }
            }

            // Present but empty: PageBuilder reads from="" as 0 (not "where it
            // is"), to="" as 0 and menuIndex="" as menu 0
            void checkNotEmpty(const Node* n, const char* name, const char* means)
            {
                if (attribute(n, name) && value(n, name).empty()) issue(n, std::string(name) + "=\"\" is empty (" + means + ")");
            }

            void checkEvent(const Node* ev)
            {
                checkAttributes(ev, false, EVENT);
                checkNotEmpty(ev, "menuIndex", "it means menu 0; leave it out for every menu");
                for (const Node* c = ev->first_node(); c; c = c->next_sibling()) {
                    if (c->type() != rapidxml::node_element) continue;
                    if (nameOf(c) != "set") { issue(c, "isn't read inside " + describe(ev)); continue; }
                    checkAttributes(c, false, SET);
                    if (!attribute(c, "duration")) issue(c, "animation set has no duration");
                    for (const Node* a = c->first_node(); a; a = a->next_sibling()) {
                        if (a->type() != rapidxml::node_element) continue;
                        if (nameOf(a) != "animate") { issue(a, "isn't read inside <set>"); continue; }
                        checkAttributes(a, false, ANIMATE);
                        const std::string type = lower(value(a, "type"));
                        if (!attribute(a, "type")) {
                            issue(a, "animate has no type");
                        }
                        else if (!inList(type, TWEENS)) {
                            issue(a, "animate type '" + value(a, "type") + "' isn't supported");
                        }
                        else if (!attribute(a, "to") && type != "nop" && type != "restart") {
                            issue(a, "animate has no 'to'");
                        }
                        else if (type == "layer") {
                            double v = 0;
                            if (!integer(value(a, "to"), v) || v < 0 || v > LAST_LAYER) {
                                issue(a, "animates to layer " + value(a, "to") + ", outside 0.." + std::to_string(LAST_LAYER));
                            }
                        }
                        checkNotEmpty(a, "from", "it starts at 0; leave it out to start from where it is");
                        if (attribute(a, "to") && type != "nop" && type != "restart") {
                            const std::string to = value(a, "to");
                            double v = 0;
                            if (to.empty()) {
                                issue(a, "to=\"\" is empty (it means 0)");
                            }
                            else if (!number(to, v) && !TWEEN_KEYWORDS.count(to) &&
                                     !(to.back() == '%' && number(to.substr(0, to.size() - 1), v))) {
                                issue(a, "animates to '" + to + "', which isn't a number, a percentage or left/center/right/top/bottom/stretch (it means 0)");
                            }
                        }
                        if (attribute(a, "algorithm") && !inList(lower(value(a, "algorithm")), ALGORITHMS)) {
                            issue(a, "algorithm '" + value(a, "algorithm") + "' isn't known (it would play as linear)");
                        }
                    }
                }
            }

            // Children of a component: animation events, plus items in menus
            void checkChildren(const Node* n, bool menu)
            {
                for (const Node* c = n->first_node(); c; c = c->next_sibling()) {
                    if (c->type() != rapidxml::node_element) continue;
                    const std::string name = nameOf(c);
                    if (inList(name, EVENTS)) { checkEvent(c); continue; }
                    if (menu && (name == "item" || name == "itemDefaults")) {
                        checkAttributes(c, true, MENU_ITEM);
                        checkChildren(c, false);
                        continue;
                    }
                    issue(c, "isn't read inside " + describe(n));
                }
            }

            void checkComponent(const Node* n, const ElementRule& rule)
            {
                const std::string name = rule.name;
                checkAttributes(n, rule.viewInfo, rule.own);
                const bool menu = name == "menu";
                checkChildren(n, menu);

                if (const rapidxml::xml_attribute<>* mode = attribute(n, "mode")) {
                    const std::string m(mode->value(), mode->value_size());
                    if (!m.empty() && !inList(m, menu ? MENU_MODES : MEDIA_MODES)) issue(n, "mode '" + m + "' isn't supported here");
                }
                if ((name == "image" || name == "video" || name == "scrollingText") && !attribute(n, "src")) issue(n, "has no src");
                if (name == "text" && !attribute(n, "value")) issue(n, "has no value");
                if ((name == "reloadableImage" || name == "reloadableText" || name == "reloadableScrollingText") && !attribute(n, "type")) {
                    issue(n, "has no type");
                }
                if ((name == "reloadableVideo" || name == "reloadableAudio") && !attribute(n, "imageType")) {
                    issue(n, "has no imageType for when there's no video");
                }
                if ((name == "reloadableText" || name == "reloadableScrollingText") && value(n, "type") == "file" &&
                    value(n, "location").empty()) {
                    issue(n, "type=\"file\" needs a location");
                }
                if (menu && !n->first_node("itemDefaults")) issue(n, "menu has no <itemDefaults> (RetroFE uses empty defaults: each item has only what it sets itself)");
                if (menu && value(n, "type") == "custom" && !n->first_node("item")) issue(n, "custom menu has no <item> slots, so it shows nothing");
            }
        };
    }

    std::string layoutParseError(const std::string& xml)
    {
        std::vector<char> buffer(xml.begin(), xml.end());
        buffer.push_back('\0');
        rapidxml::xml_document<> doc;
        try {
            doc.parse<0>(buffer.data());
        }
        catch (const rapidxml::parse_error& e) {
            return std::string("not valid XML: ") + e.what();
        }
        return doc.first_node("layout") ? std::string() : std::string("no <layout> root");
    }

    std::string layoutBuildBlocker(const std::string& xml)
    {
        // A <menu> without <itemDefaults> used to crash PageBuilder; it now
        // gets empty defaults (Graphics/MenuXml.h), so only XML it can't
        // read at all is kept from it
        return layoutParseError(xml);
    }

    std::vector<std::string> checkLayoutXml(const std::string& xml)
    {
        Checker check;
        std::vector<char> buffer(xml.begin(), xml.end());
        buffer.push_back('\0');
        rapidxml::xml_document<> doc;
        try {
            doc.parse<0>(buffer.data());
        }
        catch (const rapidxml::parse_error& e) {
            check.issues.push_back(std::string("not valid XML: ") + e.what());
            return check.issues;
        }
        const Node* layout = doc.first_node("layout");
        if (!layout) {
            check.issues.push_back("no <layout> root");
            return check.issues;
        }
        check.checkAttributes(layout, false, LAYOUT);
        if (!attribute(layout, "width") || !attribute(layout, "height")) check.issue(layout, "needs width and height");

        for (const Node* c = layout->first_node(); c; c = c->next_sibling()) {
            if (c->type() != rapidxml::node_element) continue;
            const std::string name = nameOf(c);
            if (name == "sound") {
                check.checkAttributes(c, false, SOUND);
                if (!attribute(c, "src") || !attribute(c, "type")) check.issue(c, "sound needs src and type");
                else if (!inList(value(c, "type"), SOUND_TYPES)) check.issue(c, "sound type '" + value(c, "type") + "' isn't supported");
                continue;
            }
            if (inList(name, UNCHECKED)) continue;
            const ElementRule* rule = nullptr;
            for (const auto& r : COMPONENTS) {
                if (name == r.name) { rule = &r; break; }
            }
            if (!rule) { check.issue(c, "isn't an element this engine builds"); continue; }
            check.checkComponent(c, *rule);
        }
        return check.issues;
    }

    LayoutVocabulary layoutVocabulary()
    {
        LayoutVocabulary v;
        Names attrs, elements;
        auto add = [](Names& to, const char* const* list) {
            for (; *list; ++list) to.insert(*list);
        };
        add(attrs, VIEW_INFO);
        add(attrs, FONT);
        for (const auto& r : COMPONENTS) {
            elements.insert(r.name);
            add(attrs, r.own);
        }
        add(attrs, MENU_ITEM);
        add(attrs, SOUND);
        add(attrs, LAYOUT);
        add(attrs, EVENT);
        add(attrs, SET);
        add(attrs, ANIMATE);
        add(elements, UNCHECKED);
        for (const char* e : { "layout", "sound", "item", "itemDefaults", "set", "animate" }) elements.insert(e);
        Names values;
        add(values, MEDIA_MODES);
        add(values, MENU_MODES);
        add(values, SOUND_TYPES);
        v.attributes.assign(attrs.begin(), attrs.end());
        v.elements.assign(elements.begin(), elements.end());
        v.values.assign(values.begin(), values.end());
        for (const char* const* e = EVENTS; *e; ++e) v.events.push_back(*e);
        for (const char* const* t = TWEENS; *t; ++t) v.tweens.push_back(*t);
        for (const char* const* a = ALGORITHMS; *a; ++a) v.tweens.push_back(*a);
        return v;
    }
}

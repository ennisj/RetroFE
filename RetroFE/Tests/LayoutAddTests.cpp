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

// The layout-native editor's additions: new elements from the palette and
// animation edits, inserted as text (ThemeEditor/LayoutText withChild,
// ThemeEditor/LayoutPalette, ThemeEditor/LayoutSession), files copied into a
// hand-made theme (ThemeStore importIntoTheme) and where onEnter leaves a box.

#include "TestSupport.h"
#include "../Source/ThemeEditor/EditorScript.h"
#include "../Source/ThemeEditor/LayoutCheck.h"
#include "../Source/ThemeEditor/LayoutPalette.h"
#include "../Source/ThemeEditor/LayoutSession.h"
#include "../Source/ThemeEditor/LayoutText.h"
#include "../Source/ThemeEditor/ThemeStore.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace ThemeEditor;

namespace
{
    // Aeon Nox's style: CRLF, a BOM, comments heading elements, blank lines
    // between them, one-line animation events with aligned columns
    const std::string NOX =
        "\xEF\xBB\xBF<layout width=\"1920\" height=\"1080\" font=\"fonts/a.ttf\">\r\n"
        "\r\n"
        "    <!-- Background -->\r\n"
        "    <image src=\"images/bg.png\" x=\"0\" y=\"0\" width=\"1920\" height=\"1080\" layer=\"0\"/>\r\n"
        "\r\n"
        "    <!-- Top Bar -->\r\n"
        "    <image src=\"images/topbar.png\" x=\"0\" y=\"-50\" width=\"stretch\" height=\"50\" yOrigin=\"bottom\" layer=\"5\">\r\n"
        "        <onExit                    ><set duration=\".1\"><animate type=\"alpha\"           to=\"0\" /></set></onExit>\r\n"
        "        <onEnter>          <set duration=\".1\"><animate type=\"y\"     from=\"0\"  to=\"50\"/></set></onEnter>\r\n"
        "    </image>\r\n"
        "\r\n"
        "    <text value=\"Hello\" x=\"10\" y=\"10\" fontSize=\"30\" layer=\"4\"/> <!-- greeting -->\r\n"
        "    <menu type=\"custom\">\r\n"
        "        <itemDefaults x=\"center\" y=\"980\" width=\"270\" height=\"300\" xOrigin=\"center\" layer=\"12\"/>\r\n"
        "        <item xOffset=\"-300\"/>\r\n"
        "        <item xOffset=\"0\" selected=\"true\"/>\r\n"
        "    </menu>\r\n"
        "    <!-- the end -->\r\n"
        "</layout>\r\n";

    LayoutDoc scan(const std::string& text)
    {
        LayoutDoc doc;
        std::string error;
        check(parseLayoutText(text, doc, error), "scans: " + error);
        return doc;
    }

    bool rapidxmlReads(const std::string& text) { return layoutParseError(text).empty(); }

    // The text with `inserted` put in at `at`, as the expected result of an insert
    std::string with(const std::string& text, size_t at, const std::string& inserted)
    {
        return text.substr(0, at) + inserted + text.substr(at);
    }

    std::string makeNoxRoot(const std::string& name)
    {
        std::string root = makeRoot(name);
        writeFile(root + "/layouts/Nox/layout.xml", NOX);
        writeFile(root + "/layouts/Nox/splash.xml", "<layout width=\"1920\" height=\"1080\"></layout>");
        writeFile(root + "/layouts/Nox/images/bg.png", "png");
        return root;
    }

    // The engine's boxes for NOX
    void feed(LayoutSession& s)
    {
        ElementGeom bg; bg.w = 1920; bg.h = 1080;
        s.setGeometry("image[0]", bg);
        ElementGeom bar; bar.x = 0; bar.y = -50; bar.yOrigin = 1.0f; bar.w = 1920; bar.h = 50; bar.layer = 5; bar.order = 1;
        s.setGeometry("image[1]", bar);
        ElementGeom text; text.x = 10; text.y = 10; text.w = 75; text.h = 30; text.layer = 4; text.order = 2;
        s.setGeometry("text[0]", text);
    }

    std::string lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
}

static Test::Suite layoutAdd("layout-native adding and animations", [] {
    Test::run("inserting text: after a sibling, in the file's indentation and line ending", [] {
        const LayoutDoc doc = scan(NOX);
        size_t at = 0;
        // At the end of the page: after the last element's line, before the comment that ends the file
        std::string out = withChild(doc, 0, "<image src=\"a.png\"/>", -1, &at);
        const size_t menuEnd = NOX.find("    <!-- the end -->");
        check(out == with(NOX, menuEnd, "    <image src=\"a.png\"/>\r\n"), "a line of its own after </menu>, CRLF, 4 spaces");
        check(out.compare(0, 3, "\xEF\xBB\xBF") == 0 && out.compare(at, 6, "<image") == 0, "the BOM stays; `at` is the new '<'");
        check(rapidxmlReads(out), "rapidxml reads it");

        // After the background: before the blank line and the comment that heads the top bar
        out = withChild(doc, 0, "<text value=\"x\"/>", doc.find("image[0]"), &at);
        const size_t bgLineEnd = NOX.find("\r\n", NOX.find("images/bg.png")) + 2;
        check(out == with(NOX, bgLineEnd, "    <text value=\"x\"/>\r\n"), "right after the sibling's line");

        // A sibling with a comment after it on its line: the new one goes after the comment
        out = withChild(doc, 0, "<image src=\"b.png\"/>", doc.find("text[0]"));
        const size_t textLineEnd = NOX.find("\r\n", NOX.find("<!-- greeting -->")) + 2;
        check(out == with(NOX, textLineEnd, "    <image src=\"b.png\"/>\r\n"), "the comment stays with its element");

        // A multi-line fragment takes the file's unit at every level
        out = withChild(doc, 0, "<menu>\n    <itemDefaults/>\n    <item>\n        <onEnter/>\n    </item>\n</menu>", -1);
        check(out.find("    <menu>\r\n        <itemDefaults/>\r\n        <item>\r\n            <onEnter/>\r\n        </item>\r\n    </menu>\r\n    <!-- the end -->")
                  != std::string::npos, "nested lines indented by the file's unit, CRLF");

        // Tabs and LF
        const std::string tabs = "<layout width=\"10\" height=\"10\">\n\t<image src=\"a\"/>\n\t<menu>\n\t\t<itemDefaults/>\n\t</menu>\n</layout>";
        const LayoutDoc tdoc = scan(tabs);
        checkEqual(indentUnit(tdoc), std::string("\t"), "a tab-indented file's unit");
        out = withChild(tdoc, 0, "<menu>\n    <itemDefaults/>\n</menu>", tdoc.find("image[0]"));
        check(out == "<layout width=\"10\" height=\"10\">\n\t<image src=\"a\"/>\n\t<menu>\n\t\t<itemDefaults/>\n\t</menu>\n\t<menu>\n\t\t<itemDefaults/>\n\t</menu>\n</layout>",
              "tabs and LF, and the last line without a line ending");
        checkEqual(indentUnit(scan("<layout>\n  <menu>\n    <item/>\n  </menu>\n</layout>")), std::string("  "), "a two-space file");
    });

    Test::run("inserting text: parents without children, self-closing, and inline siblings", [] {
        std::string out = withChild(scan("<layout width=\"10\" height=\"10\"/>"), 0, "<text value=\"a\"/>");
        checkEqual(out, std::string("<layout width=\"10\" height=\"10\">\n    <text value=\"a\"/>\n</layout>"), "a self-closing root opens");
        out = withChild(scan("<layout width=\"10\" height=\"10\"></layout>"), 0, "<text value=\"a\"/>");
        checkEqual(out, std::string("<layout width=\"10\" height=\"10\">\n    <text value=\"a\"/>\n</layout>"), "an empty root on one line");
        out = withChild(scan("<layout>\r\n  <!-- nothing yet -->\r\n</layout>\r\n"), 0, "<text value=\"a\"/>");
        checkEqual(out, std::string("<layout>\r\n  <!-- nothing yet -->\r\n    <text value=\"a\"/>\r\n</layout>\r\n"),
                   "before an end tag on its own line, after the comment");

        // An event into a self-closing component on its own line: the component opens over lines
        const LayoutDoc doc = scan(NOX);
        const int text = doc.find("text[0]");
        out = withChild(doc, text, eventFragment("onEnter"));
        check(out.find("layer=\"4\">\r\n        " + eventFragment("onEnter") + "\r\n    </text> <!-- greeting -->\r\n") != std::string::npos,
              "<text ...> then the event on its own line, then </text>, the comment after it kept");
        check(rapidxmlReads(out), "rapidxml reads it");

        // Into Aeon Nox's one-line events: inline, as they're written
        const int set = doc.find("image[1]/onEnter[0]/set[0]");
        out = withChild(doc, set, animateFragment());
        check(out.find("to=\"50\"/>" + animateFragment() + "</set></onEnter>") != std::string::npos, "an animate after the animate, on its line");
        const int enter = doc.find("image[1]/onEnter[0]");
        out = withChild(doc, enter, setFragment());
        check(out.find("</set> " + setFragment() + "</onEnter>") != std::string::npos, "a set after the set, spaced as the set is from <onEnter>");
        out = withChild(scan("<layout><image src='a'><onEnter><set duration='1'/></onEnter></image></layout>"),
                        3, animateFragment());
        check(out.find("<set duration='1'>" + animateFragment() + "</set>") != std::string::npos, "a self-closing set opens inline");
        out = withChild(scan("<layout><image src='a'><onEnter></onEnter></image></layout>"), 2, setFragment());
        check(out.find("<onEnter>" + setFragment() + "</onEnter>") != std::string::npos, "an empty event on one line");

        // Siblings on one line: spaced as they are
        out = withChild(scan("<layout><image src='a'/> <image src='b'/></layout>"), 0, "<text value='c'/>", 2);
        checkEqual(out, std::string("<layout><image src='a'/> <image src='b'/> <text value='c'/></layout>"), "inline, the space they have");
        out = withChild(scan("<layout>\n  <image src='a'/><image src='b'/>\n</layout>"), 0, "<text value='c'/>", 1);
        checkEqual(out, std::string("<layout>\n  <image src='a'/><text value='c'/><image src='b'/>\n</layout>"),
                   "between two on a line: inline, as close as they are");
        out = withChild(scan("<layout>\n  <image src='a'/> <image src='b'/>\n</layout>"), 0, "<text value='c'/>", 1);
        checkEqual(out, std::string("<layout>\n  <image src='a'/> <text value='c'/> <image src='b'/>\n</layout>"), "or as spaced");

        // The line ending is the sibling's line's, not the file's first
        out = withChild(scan("<layout>\n  <image src='a'/>\r\n</layout>"), 0, "<text value='c'/>", 1);
        checkEqual(out, std::string("<layout>\n  <image src='a'/>\r\n  <text value='c'/>\r\n</layout>"), "CRLF after a CRLF line in a mixed file");

        // A byte order mark isn't part of the first line
        out = withChild(scan("\xEF\xBB\xBF<layout width=\"1\" height=\"1\"></layout>"), 0, "<text value=\"a\"/>");
        checkEqual(out, std::string("\xEF\xBB\xBF<layout width=\"1\" height=\"1\">\n    <text value=\"a\"/>\n</layout>"),
                   "an empty root after a BOM opens over lines");
        const LayoutDoc bomTabs = scan("\xEF\xBB\xBF<layout>\n\t<image src='a'/>\n</layout>\n");
        checkEqual(indentUnit(bomTabs), std::string("\t"), "a BOM'd tab file's unit is a tab");
        checkEqual(indentationOf(bomTabs, 0), std::string(""), "the root after a BOM starts its line");
        out = withChild(scan("\xEF\xBB\xBF<layout/>"), 0, "<text value=\"a\"/>");
        checkEqual(out, std::string("\xEF\xBB\xBF<layout>\n    <text value=\"a\"/>\n</layout>"), "a self-closing root after a BOM");
        check(withChild(scan("<layout><image src='a'/></layout>"), 0, "<x/>", 5) == "<layout><image src='a'/></layout>",
              "an `after` that isn't a child changes nothing");
    });

    Test::run("the palette: every element passes the layout check and parses with rapidxml", [] {
        check(layoutPalette().size() >= 12, "the palette has the element types");
        std::set<std::string> elements;
        const LayoutDoc nox = scan(NOX);
        const std::vector<std::string> noxIssues = checkLayoutXml(NOX);
        for (const PaletteEntry& entry : layoutPalette()) {
            elements.insert(entry.element);
            const std::vector<std::string> types = entry.types.empty() ? std::vector<std::string>{ "" } : entry.types;
            for (const std::string& type : types) {
                const std::string fragment = paletteFragment(entry, type, entry.file.empty() ? "" : "images/x.png", 760, 390, 13);
                const std::string page = "<layout width=\"1920\" height=\"1080\">\n" + fragment + "\n</layout>\n";
                std::vector<std::string> issues = checkLayoutXml(page);
                check(issues.empty(), entry.id + " (" + type + ") passes the check" + (issues.empty() ? "" : ": " + issues[0]));
                check(rapidxmlReads(page), entry.id + " parses with rapidxml");
                LayoutDoc doc = scan(page);
                check(doc.elements.size() >= 2 && doc.elements[1].name == entry.element, entry.id + " is a <" + entry.element + ">");
                if (!entry.typeAttr.empty()) check(doc.elements[1].value(entry.typeAttr) == type, entry.id + " shows " + type);
                // Inserted into a hand-made page, nothing new to report
                const std::string inserted = withChild(nox, 0, fragment);
                check(checkLayoutXml(inserted) == noxIssues, entry.id + " inserted into a page adds no issue");
            }
        }
        for (const char* e : { "image", "video", "text", "statusText", "container", "reloadableImage", "reloadableVideo",
                               "reloadableText", "reloadableScrollingText", "menu" }) {
            check(elements.count(e) == 1, std::string("the palette has <") + e + ">");
        }
        const PaletteEntry* image = paletteEntry("image");
        check(image && paletteFragment(*image, "", "a&b.png", 1, 2, 3).find("src=\"a&amp;b.png\" x=\"1\" y=\"2\"") != std::string::npos,
              "a file name is encoded");

        // The names the animation editor offers are the engine's
        std::set<std::string> offered, known;
        for (const std::string& t : tweenTypeNames()) offered.insert(lower(t));
        for (const std::string& a : tweenAlgorithmNames()) offered.insert(lower(a));
        for (const std::string& t : layoutVocabulary().tweens) known.insert(t);
        check(offered == known, "tween types and algorithms are LayoutCheck's (" + std::to_string(offered.size()) + " vs " +
                                    std::to_string(known.size()) + ")");
        checkEqual(engineEventName("onMenuEnter"), std::string("menuEnter"), "the engine's event names");
        checkEqual(engineEventName("onAttract"), std::string("attract"), "");
        const std::vector<std::string> events = LayoutSession::eventNames();
        check(std::find(events.begin(), events.end(), "onHighlightEnter") != events.end() && events.size() >= 30, "the events PageBuilder reads");
    });

    Test::run("adding elements: after the selection or at the end, selected, paths of the rest kept", [] {
        std::string root = makeNoxRoot("layout_add");
        LayoutSession s;
        std::string error;
        check(s.open(root, "Nox", "layout.xml", error), "open: " + error);
        feed(s);
        const LayoutDoc before = s.doc();
        check(s.newElementLayer() == 13, "one above the highest layer (12): " + std::to_string(s.newElementLayer()));

        // Nothing selected: at the end; every other path and element as it was
        check(s.addElement("text"), "a text added");
        const LayoutElement* added = s.selectedElement();
        check(added && added->path == "text[1]" && added->value("x") == "810" && added->value("y") == "520" && added->value("layer") == "13",
              "selected, in the middle of the page, on top: " + (added ? added->path + " " + added->value("x") + "," + added->value("y") : std::string()));
        for (const LayoutElement& e : before.elements) {
            if (e.path.empty()) continue;   // the root holds the new one
            const int now = s.doc().find(e.path);
            check(now >= 0 && s.text().substr(s.element(now).begin, s.element(now).end - s.element(now).begin) ==
                                  before.text.substr(e.begin, e.end - e.begin), "kept: " + e.path);
        }
        check(checkLayoutXml(s.text()) == checkLayoutXml(NOX), "no new layout issues");
        check(s.undo() && s.text() == NOX, "undo: the file as it was");

        // After the background: images after it move up one, nothing else
        s.selectPath("image[0]");
        check(s.addElement("image", "", "images/bg.png"), "an image after the background");
        check(s.selectedElement()->path == "image[1]" && s.selectedElement()->value("src") == "images/bg.png", "the new one is image[1]");
        check(s.doc().find("image[2]") > 0 && s.element(s.doc().find("image[2]")).value("src") == "images/topbar.png", "the top bar is image[2]");
        check(s.doc().find("text[0]") > 0 && s.doc().find("menu[0]/item[1]") > 0, "other kinds keep their paths");
        check(!s.hasBox(s.doc().find("image[0]")), "the boxes go until the page is built again (paths moved)");
        s.undo();

        // A menu item selected: after its menu
        s.selectPath("menu[0]/item[1]");
        check(s.addElement("reloadableText", "year"), "game text");
        check(s.selectedElement()->path == "reloadableText[0]" && s.selectedElement()->value("type") == "year", "with the chosen type");
        LayoutDoc doc = scan(s.text());
        check(doc.elements[s.selected()].begin > doc.elements[doc.find("menu[0]")].end, "after the menu");
        check(s.text().find("    </menu>\r\n    <reloadableText type=\"year\"") != std::string::npos, "on its own line");

        // A wheel: a menu with its defaults and items, indented in the file's unit
        check(s.addElement("menu-custom"), "a wheel");
        check(s.selectedElement()->path == "menu[1]" && s.doc().find("menu[1]/itemDefaults[0]") > 0 && s.doc().find("menu[1]/item[2]") > 0,
              "with item defaults and three items");
        check(s.text().find("\r\n        <itemDefaults x=\"960\" y=\"540\"") != std::string::npos, "its items centred, indented a level");
        check(checkLayoutXml(s.text()) == checkLayoutXml(NOX), "still nothing new for the layout check");
        check(!s.addElement("nonsense"), "an unknown palette entry adds nothing");
        check(s.save(error), "saved: " + error);
        check(readFile(root + "/layouts/Nox/layout.xml") == s.text() && rapidxmlReads(s.text()), "as edited, readable");
    });

    Test::run("animations: add, edit and remove steps as text, the boxes kept", [] {
        std::string root = makeNoxRoot("layout_anim");
        LayoutSession s;
        std::string error;
        s.open(root, "Nox", "layout.xml", error);
        feed(s);
        s.selectPath("image[1]");
        const int bar = s.selected();
        std::vector<int> events = s.events(bar);
        check(events.size() == 2 && s.element(events[1]).name == "onEnter", "the top bar's events, in file order");
        const int set = s.doc().find("image[1]/onEnter[0]/set[0]");
        check(s.childrenNamed(set, "animate").size() == 1, "one change in its step");

        check(s.addAnimate(set), "a change added");
        check(s.selectedElement() && s.selectedElement()->path == "image[1]", "the element stays selected");
        check(s.hasBox(s.selected()), "and its box stays (only animation paths moved)");
        const int added = s.findInSelected("onEnter[0]/set[0]/animate[1]");
        check(added > 0, "found by a path relative to the element");
        s.setCoalesceKey("t");
        check(s.setAttributeOf(added, "type", "alpha") == false, "setting the value it has changes nothing");
        check(s.setAttributeOf(added, "from", "0") && s.setAttributeOf(added, "to", "0.5"), "edited");
        s.endCoalesce();
        check(s.text().find("to=\"50\"/><animate type=\"alpha\" to=\"0.5\" from=\"0\"/></set></onEnter>") != std::string::npos,
              "inline, attributes in place: " + s.text().substr(s.text().find("<onEnter>"), 120));
        check(s.setAttributeOf(added, "from", "") && !s.element(added).has("from"), "an empty from is removed, not written empty");
        check(s.setAttributeOf(set, "duration", "0.3") && s.element(set).value("duration") == "0.3", "the step's duration");
        check(s.removeAnimation(added), "removed");
        check(s.setAttributeOf(set, "duration", ".1"), "the duration back");
        check(s.text() == NOX, "the file as it was, byte for byte");

        check(s.addSet(s.findInSelected("onEnter[0]")) && s.findInSelected("onEnter[0]/set[1]/animate[0]") > 0, "a second step");
        check(s.addEvent("onMenuEnter") && s.findInSelected("onMenuEnter[0]/set[0]/animate[0]") > 0, "a new event");
        check(s.text().find("</onEnter>\r\n        " + eventFragment("onMenuEnter") + "\r\n    </image>") != std::string::npos,
              "on its own line after the last event");
        check(checkLayoutXml(s.text()) == checkLayoutXml(NOX) && rapidxmlReads(s.text()), "nothing new for the layout check");
        check(!s.addEvent("notAnEvent") && !s.addEvent("on Enter"), "only event names");
        check(!s.addSet(bar) && !s.addAnimate(s.findInSelected("onEnter[0]")), "a set goes in an event, a change in a set");
        check(!s.removeAnimation(bar), "the element itself isn't removed here");
        check(!s.setAttributeOf(s.doc().find("text[0]"), "x", "1"), "only the selected element and what's inside it");
        while (s.canUndo()) s.undo();
        check(s.text() == NOX, "undone, all of it");
    });

    Test::run("animations: copying another element's", [] {
        std::string root = makeNoxRoot("layout_copy");
        LayoutSession s;
        std::string error;
        s.open(root, "Nox", "layout.xml", error);
        s.selectPath("text[0]");
        check(s.copyAnimationsFrom(s.doc().find("image[1]")), "the text takes the top bar's");
        const std::string expected =
            "    <text value=\"Hello\" x=\"10\" y=\"10\" fontSize=\"30\" layer=\"4\">\r\n"
            "        <onExit                    ><set duration=\".1\"><animate type=\"alpha\"           to=\"0\" /></set></onExit>\r\n"
            "        <onEnter>          <set duration=\".1\"><animate type=\"y\"     from=\"0\"  to=\"50\"/></set></onEnter>\r\n"
            "    </text> <!-- greeting -->\r\n";
        check(s.text().find(expected) != std::string::npos, "copied as written, in order, indented as its children");
        check(s.events(s.selected()).size() == 2 && rapidxmlReads(s.text()), "two events");

        // Copying onto an element that has its own: they're replaced
        s.selectPath("image[1]");
        s.addEvent("onGameEnter");
        s.selectPath("menu[0]/item[0]");
        s.addEvent("onHighlightEnter");
        s.selectPath("image[1]");
        check(s.copyAnimationsFrom(s.doc().find("menu[0]/item[0]")), "the bar takes the item's");
        std::vector<int> evs = s.events(s.selected());
        check(evs.size() == 1 && s.element(evs[0]).name == "onHighlightEnter", "its own are gone, the copy is there");
        check(!s.copyAnimationsFrom(s.doc().find("image[0]")), "an element without animations has none to give");
        check(!s.copyAnimationsFrom(s.selected()), "nor does the element itself");
        while (s.canUndo()) s.undo();
        check(s.text() == NOX, "undone");
    });

    Test::run("where onEnter leaves a box", [] {
        std::string root = makeNoxRoot("layout_end");
        LayoutSession s;
        std::string error;
        s.open(root, "Nox", "layout.xml", error);
        feed(s);
        const int bar = s.doc().find("image[1]");
        DragRect placed = s.box(bar);
        check(placed.y == -100 && placed.h == 50, "placed above the page (y -50, bottom origin)");
        DragRect end;
        check(s.enterEndBox(bar, end) && end.x == 0 && end.y == 0 && end.w == 1920 && end.h == 50, "onEnter brings it to the top of the page");
        check(!s.enterEndBox(s.doc().find("image[0]"), end), "no onEnter: no end box");
        s.selectPath("text[0]");
        s.addEvent("onEnter");   // alpha only
        check(!s.enterEndBox(s.doc().find("text[0]"), end), "an onEnter that doesn't move it");
        const int a = s.findInSelected("onEnter[0]/set[0]/animate[0]");
        s.setAttributeOf(a, "type", "x");
        s.setAttributeOf(a, "to", "center");
        check(s.enterEndBox(s.doc().find("text[0]"), end) && end.x == 960 && end.y == 10, "x to center: 960, as PageBuilder resolves it");
        s.addSet(s.findInSelected("onEnter[0]"));
        const int b = s.findInSelected("onEnter[0]/set[1]/animate[0]");
        s.setAttributeOf(b, "type", "width");
        s.setAttributeOf(b, "to", "50%");
        check(s.enterEndBox(s.doc().find("text[0]"), end) && end.w == 960, "the last step's width, a percentage of the page");
        std::string note;
        s.selectPath("image[1]");
        check(runLayoutCommand(s, parseEditorCommand("endbox"), note) == CommandResult::Ok && note == "ends at 0,0 1920x50", "endbox: " + note);
    });

    Test::run("files for a hand-made theme: copied into its own folders", [] {
        std::string root = makeNoxRoot("layout_files");
        const std::string layouts = root + "/layouts";
        writeFile(root + "/outside/new.png", "art");
        writeFile(root + "/outside/other/new.png", "other art");
        std::string rel, error;
        check(!importFile(layouts, "Nox", root + "/outside/new.png", "images", rel, error), "the widget editor's import refuses a hand-made theme");
        check(importIntoTheme(layouts, "Nox", root + "/outside/new.png", "images", "", rel, error) && rel == "images/new.png" &&
              readFile(layouts + "/Nox/images/new.png") == "art", "copied into images/: " + error);
        check(importIntoTheme(layouts, "Nox", root + "/outside/new.png", "images", "", rel, error) && rel == "images/new.png", "the same file again: reused");
        check(importIntoTheme(layouts, "Nox", root + "/outside/other/new.png", "images", "", rel, error) && rel == "images/new-2.png" &&
              readFile(layouts + "/Nox/images/new.png") == "art", "a different one with the name: -2, nothing replaced");
        check(importIntoTheme(layouts, "Nox", layouts + "/Nox/images/bg.png", "assets", "", rel, error) && rel == "images/bg.png",
              "a file already in the theme is used where it is");
        check(!importIntoTheme(layouts, "..", root + "/outside/new.png", "images", "", rel, error), "not outside layouts");
        check(!importIntoTheme(layouts, "Nox", root + "/outside/new.png", "../x", "", rel, error), "nor outside the theme");
        check(!importIntoTheme(layouts, "Nox", root + "/outside/new.png", "images/../../x", "", rel, error), "nor through ..");
        check(!importIntoTheme(layouts, "Nox", root + "/outside/new.png", "images", "../Other", rel, error), "relative to a folder of the theme only");

        LayoutSession s;
        s.open(root, "Nox", "layout.xml", error);
        checkEqual(s.pageFolder(), std::string(""), "the main page is in the theme folder");
        checkEqual(s.importFolder("image"), std::string("images"), "images go in the theme's images/");
        checkEqual(s.importFolder("video"), std::string("assets"), "videos in assets/ when it has no video folder");
        fs::create_directories(layouts + "/Nox/video");
        checkEqual(s.importFolder("video"), std::string("video"), "else its video/");

        // A collection's page: PageBuilder resolves src against the page's own folder
        writeFile(layouts + "/Nox/collections/NES/layout/layout.xml", "<layout width=\"1920\" height=\"1080\">\n</layout>\n");
        LayoutSession c;
        check(c.open(root, "Nox", "collections/NES/layout/layout.xml", error), "a collection's page opens: " + error);
        const std::string pageDir = c.pageFolder();
        checkEqual(pageDir, std::string("collections/NES/layout"), "its folder");
        checkEqual(c.importFolder("image"), std::string("images"), "without images/ of its own: the theme's");
        check(importIntoTheme(layouts, "Nox", root + "/outside/new.png", c.importFolder("image"), pageDir, rel, error) &&
              rel == "../../../images/new.png", "the src is relative to the page's folder: " + rel);
        check(fs::exists(fs::path(layouts + "/Nox/" + pageDir) / rel), "and names the file from there");
        check(importIntoTheme(layouts, "Nox", layouts + "/Nox/images/bg.png", "assets", pageDir, rel, error) &&
              rel == "../../../images/bg.png", "a file already in the theme, from the page's folder: " + rel);
        fs::create_directories(layouts + "/Nox/" + pageDir + "/images");
        checkEqual(c.importFolder("image"), pageDir + "/images", "the page's own images/ first");
        check(importIntoTheme(layouts, "Nox", root + "/outside/new.png", c.importFolder("image"), pageDir, rel, error) &&
              rel == "images/new.png" && readFile(layouts + "/Nox/" + pageDir + "/images/new.png") == "art",
              "copied beside the page: " + rel);
        check(c.addElement("image", "", rel) && c.selectedElement()->value("src") == "images/new.png", "and used by the new image");
    });

    Test::run("the layout check: empty and unreadable animation values", [] {
        auto issuesFor = [](const std::string& animate, const std::string& event = "onEnter") {
            return checkLayoutXml("<layout width=\"10\" height=\"10\"><image src=\"a.png\"><" + event + "><set duration=\"1\">" + animate +
                                  "</set></" + event.substr(0, event.find(' ')) + "></image></layout>");
        };
        check(issuesFor("<animate type=\"x\" to=\"center\"/>").empty(), "a keyword");
        check(issuesFor("<animate type=\"width\" to=\"50%\"/>").empty() && issuesFor("<animate type=\"y\" from=\"-20\" to=\".5\"/>").empty(),
              "a percentage and numbers");
        check(issuesFor("<animate type=\"nop\"/>").empty(), "nop needs no to");
        check(issuesFor("<animate type=\"x\" from=\"\" to=\"1\"/>").size() == 1, "from=\"\" is reported (it starts at 0)");
        check(issuesFor("<animate type=\"x\" to=\"\"/>").size() == 1, "to=\"\" is reported");
        std::vector<std::string> issues = issuesFor("<animate type=\"x\" to=\"middle\"/>");
        check(issues.size() == 1 && issues[0].find("'middle'") != std::string::npos, "a word PageBuilder doesn't read is reported");
        check(issuesFor("<animate type=\"x\" to=\"1\"/>", "onEnter menuIndex=\"\"").size() == 1, "menuIndex=\"\" is reported");
    });

    Test::run("script commands for adding and animations", [] {
        std::string root = makeNoxRoot("layout_add_script");
        LayoutSession s;
        std::string error;
        s.open(root, "Nox", "layout.xml", error);
        std::string note;
        check(runLayoutCommand(s, parseEditorCommand("add reloadableImage logo"), note) == CommandResult::Ok &&
              s.selectedElement()->value("type") == "logo", "add with a type: " + note);
        check(runLayoutCommand(s, parseEditorCommand("add image - images/bg.png"), note) == CommandResult::Ok &&
              s.selectedElement()->value("src") == "images/bg.png", "add an image with its file");
        check(runLayoutCommand(s, parseEditorCommand("event onEnter"), note) == CommandResult::Ok, "event");
        check(runLayoutCommand(s, parseEditorCommand("addanimate onEnter[0]/set[0]"), note) == CommandResult::Ok, "addanimate");
        check(runLayoutCommand(s, parseEditorCommand("animattr onEnter[0]/set[0]/animate[1] type y"), note) == CommandResult::Ok, "animattr");
        runLayoutCommand(s, parseEditorCommand("getanim onEnter[0]/set[0]/animate[1] type"), note);
        checkEqual(note, std::string("type=\"y\""), "getanim");
        check(runLayoutCommand(s, parseEditorCommand("rmanimattr onEnter[0]/set[0]/animate[1] to"), note) == CommandResult::Ok, "rmanimattr");
        check(runLayoutCommand(s, parseEditorCommand("addset onEnter[0]"), note) == CommandResult::Ok, "addset");
        check(runLayoutCommand(s, parseEditorCommand("rmanim onEnter[0]/set[1]"), note) == CommandResult::Ok, "rmanim");
        check(runLayoutCommand(s, parseEditorCommand("events"), note) == CommandResult::Ok && note == "onEnter (1 set)", "events: " + note);
        check(runLayoutCommand(s, parseEditorCommand("copyanims image[1]"), note) == CommandResult::Ok && s.events(s.selected()).size() == 2,
              "copyanims");
        check(runLayoutCommand(s, parseEditorCommand("rmanim nowhere[0]"), note) == CommandResult::Failed, "a path that isn't there");
    });
});

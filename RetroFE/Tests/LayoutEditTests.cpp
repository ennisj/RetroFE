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

// The theme editor's layout-native mode: editing a hand-made layout file in
// place (ThemeEditor/LayoutText, ThemeEditor/LayoutSession) and the element
// paths PageBuilder traces (Graphics/LayoutNodePath.h).

#include "TestSupport.h"
#include "../Source/Graphics/LayoutNodePath.h"
#include "../Source/ThemeEditor/EditorScript.h"
#include "../Source/ThemeEditor/LayoutCheck.h"
#include "../Source/ThemeEditor/LayoutSession.h"
#include "../Source/ThemeEditor/LayoutText.h"
#include "../Source/ThemeEditor/ThemeGenerate.h"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace ThemeEditor;

namespace
{
    // A hand-made page in the styles layouts are written in: CRLF, comments
    // (one holding a tag), a declaration, single and double quotes, entities,
    // self-closing and open tags, attributes one per line, a custom menu
    const std::string HAND =
        "\xEF\xBB\xBF<?xml version=\"1.0\"?>\r\n"
        "<!-- Hand: <image src=\"not-an-element.png\"/> -->\r\n"
        "<layout width=\"1920\" height=\"1080\" font='fonts/a.ttf'>\r\n"
        "    <sound type=\"load\" src=\"sounds/load.wav\" />\r\n"
        "    <image src=\"images/bg.png\" x=\"0\" y=\"0\" width=\"1920\" height=\"1080\" layer=\"0\"/>\r\n"
        "\r\n"
        "    <!-- the logo -->\r\n"
        "    <image src='images/logo.png' x=\"center\" y=\"100\" width=\"400\" height=\"200\" xOrigin=\"center\" layer=\"3\">\r\n"
        "        <onEnter><set duration=\".2\"><animate type=\"alpha\" from=\"0\" to=\"1\"/></set></onEnter>\r\n"
        "        <onMenuEnter><set duration=\".2\"><animate type=\"y\" to=\"120\"/></set></onMenuEnter>\r\n"
        "    </image>\r\n"
        "    <text value=\"Tom &amp; Jerry &quot;live&quot;\" x=\"10\" y=\"10\" fontSize=\"30\" layer=\"4\"/>\r\n"
        "    <reloadableText type=\"title\"\r\n"
        "                    x=\"100\"\r\n"
        "                    y=\"900\"/>\r\n"
        "    <menu type=\"custom\" x=\"0\" y=\"0\">\r\n"
        "        <itemDefaults x=\"center\" y=\"980\" width=\"270\" height=\"300\" xOrigin=\"center\" layer=\"12\"/>\r\n"
        "        <item xOffset=\"-300\"/>\r\n"
        "        <item xOffset=\"0\" selected=\"true\"/>\r\n"
        "        <item xOffset=\"300\"/>\r\n"
        "    </menu>\r\n"
        "</layout>\r\n";

    // Every element's path as rapidxml + LayoutNodePath see it, in document order
    void rapidxmlPaths(rapidxml::xml_node<>* node, std::vector<std::string>& out)
    {
        for (rapidxml::xml_node<>* child = node->first_node(); child; child = child->next_sibling()) {
            if (child->type() != rapidxml::node_element) continue;
            out.push_back(LayoutNodePath::of(child));
            rapidxmlPaths(child, out);
        }
    }

    std::vector<std::string> enginePaths(const std::string& text)
    {
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back('\0');
        rapidxml::xml_document<> doc;
        doc.parse<0>(buffer.data());
        std::vector<std::string> out;
        rapidxml::xml_node<>* root = doc.first_node("layout");
        if (!root) return out;
        out.push_back(LayoutNodePath::of(root));
        rapidxmlPaths(root, out);
        return out;
    }

    std::vector<std::string> textPaths(const LayoutDoc& doc)
    {
        std::vector<std::string> out;
        for (const LayoutElement& e : doc.elements) out.push_back(e.path);
        return out;
    }

    // What changed between two texts: the bytes before and after that are the same
    void diffRegion(const std::string& a, const std::string& b, size_t& prefix, size_t& suffix)
    {
        prefix = 0;
        while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
        suffix = 0;
        while (suffix < a.size() - prefix && suffix < b.size() - prefix && a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix]) ++suffix;
    }

    std::string changed(const std::string& before, const std::string& after)
    {
        size_t p = 0, s = 0;
        diffRegion(before, after, p, s);
        return after.substr(p, after.size() - p - s);
    }

    // A RetroFE root holding the hand-made theme "Hand"
    std::string makeHandRoot(const std::string& name)
    {
        std::string root = makeRoot(name);
        writeFile(root + "/layouts/Hand/layout.xml", HAND);
        writeFile(root + "/layouts/Hand/splash.xml", "<layout width=\"1920\" height=\"1080\"></layout>");
        writeFile(root + "/layouts/Hand/layout 4x3.xml", "<layout width=\"1440\" height=\"1080\"></layout>");
        writeFile(root + "/layouts/Hand/collections/NES/layout/layout.xml", "<layout width=\"1920\" height=\"1080\"></layout>");
        writeFile(root + "/layouts/Hand/forms/keyboard/layout.xml", "<layout width=\"1920\" height=\"1080\"></layout>");
        writeFile(root + "/layouts/Hand/forms/layout.xml", "<layout width=\"1920\" height=\"1080\"></layout>");
        writeFile(root + "/layouts/Hand/images/bg.png", "png");
        writeFile(root + "/layouts/Other/layout.xml", "<layout width=\"10\" height=\"10\"></layout>");
        return root;
    }

    // Geometry as the engine would report it for HAND (1920x1080)
    void feedGeometry(LayoutSession& s)
    {
        ElementGeom bg; bg.w = 1920; bg.h = 1080; bg.order = 0;
        s.setGeometry("image[0]", bg);
        ElementGeom logo; logo.x = 960; logo.y = 100; logo.xOrigin = 0.5f; logo.w = 400; logo.h = 200; logo.layer = 3; logo.order = 1;
        s.setGeometry("image[1]", logo);
        ElementGeom text; text.x = 10; text.y = 10; text.w = 200; text.h = 30; text.layer = 4; text.order = 2; text.approximate = true;
        s.setGeometry("text[0]", text);
        for (int i = 0; i < 3; ++i) {
            ElementGeom item;
            item.x = 960; item.y = 980; item.xOffset = static_cast<float>(-300 + 300 * i); item.xOrigin = 0.5f;
            item.w = 270; item.h = 300; item.layer = 12; item.order = 10 + i;
            s.setGeometry("menu[0]/item[" + std::to_string(i) + "]", item);
        }
    }

    CanvasFit unitFit() { return canvasFit(1920.0f, 1080.0f, 1920, 1080); }

    void drag(LayoutSession& s, float x1, float y1, float x2, float y2)
    {
        s.pointerDown(unitFit(), x1, y1, 4.0f);
        s.pointerMove(unitFit(), x2, y2);
        s.pointerUp();
    }
}

static Test::Suite layoutEdit("layout-native editing", [] {
    Test::run("layout text: elements, paths and attributes are found where they are", [] {
        LayoutDoc doc;
        std::string error;
        check(parseLayoutText(HAND, doc, error), "the hand-made page scans: " + error);
        check(doc.elements.size() == 17, "17 elements (the commented-out image isn't one), got " + std::to_string(doc.elements.size()));
        checkEqual(doc.elements[0].path, "", "the root's path");
        check(doc.find("image[1]") > 0 && doc.elements[doc.find("image[1]")].value("src") == "images/logo.png", "the second image, single-quoted");
        check(doc.find("menu[0]/item[2]") > 0, "menu items");
        check(doc.find("image[1]/onMenuEnter[0]/set[0]/animate[0]") > 0, "animations have paths too");
        const LayoutElement& text = doc.elements[doc.find("text[0]")];
        checkEqual(text.value("value"), "Tom & Jerry \"live\"", "entities are decoded");
        check(text.selfClosing && doc.elements[doc.find("image[1]")].selfClosing == false, "self-closing or not");
        const LayoutAttr* font = doc.elements[0].attr("font");
        check(font && font->quote == '\'' && HAND.substr(font->valueBegin, font->valueEnd - font->valueBegin) == "fonts/a.ttf",
              "an attribute's value is located in the text");

        check(!parseLayoutText("<layout>\n<image>\n</layout>", doc, error) && error.find("line 1") != std::string::npos,
              "an element left open names its line: " + error);
        check(!parseLayoutText("<layout></image></layout>", doc, error), "an end tag with nothing open");
        // rapidxml (parse<0>) doesn't check end tags' names, and shipped layouts depend on that
        check(parseLayoutText("<layout><image src='a'><onMenuScroll></onMenuJumpScroll></image></layout>", doc, error) &&
              doc.find("image[0]/onMenuScroll[0]") > 0, "an end tag closes the innermost element, whatever its name");
        check(!parseLayoutText("<layout x=1></layout>", doc, error), "an unquoted value is refused");
        check(!parseLayoutText("<page/>", doc, error), "a root that isn't <layout>");
        check(!parseLayoutText("<layout><!-- open </layout>", doc, error), "an unclosed comment");
        check(parseLayoutText("<layout><![CDATA[ <image/> ]]><text value='a>b'/></layout>", doc, error) &&
              doc.elements.size() == 2 && doc.elements[1].value("value") == "a>b", "CDATA is skipped and > in a value is fine");
    });

    Test::run("layout text: paths match what PageBuilder traces (rapidxml + LayoutNodePath)", [] {
        LayoutDoc doc;
        std::string error;
        check(parseLayoutText(HAND, doc, error), "scans");
        std::vector<std::string> engine = enginePaths(HAND);
        std::vector<std::string> text = textPaths(doc);
        check(engine == text, "the same paths in the same order (" + std::to_string(engine.size()) + " vs " + std::to_string(text.size()) + ")");

        // The pages shipped with RetroFE: every element of every page
        const fs::path layouts = fs::path(RETROFE_SOURCE_DIR) / ".." / ".." / "Package" / "Environment" / "Common" / "layouts";
        std::error_code ec;
        if (!fs::is_directory(layouts, ec)) {
            std::cout << "  (skipped: no shipped layouts at " << layouts.string() << ")" << std::endl;
            return;
        }
        int pages = 0;
        for (fs::recursive_directory_iterator it(layouts, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".xml") continue;
            std::string xml = readFile(it->path().string());
            if (!layoutParseError(xml).empty()) continue;   // not a page
            LayoutDoc page;
            bool ok = parseLayoutText(xml, page, error);
            check(ok, it->path().string() + " scans: " + error);
            if (ok) check(enginePaths(xml) == textPaths(page), it->path().string() + ": paths agree");
            pages++;
        }
        check(pages >= 10, "the shipped pages were checked (" + std::to_string(pages) + ")");
    });

    Test::run("layout text: an edit changes only the value, whatever the file's style", [] {
        LayoutDoc doc;
        std::string error;
        parseLayoutText(HAND, doc, error);
        const int logo = doc.find("image[1]");

        std::string out = withAttribute(doc, logo, "y", "150");
        checkEqual(changed(HAND, out), "5", "only the value's bytes change (100 -> 150)");
        check(out.find("\r\n") != std::string::npos && out.substr(0, 3) == "\xEF\xBB\xBF", "CRLF and the BOM stay");
        check(withAttribute(doc, logo, "y", "100") == HAND, "the same value changes nothing");

        out = withAttribute(doc, 0, "font", "fonts/b's.ttf");
        check(out.find("font='fonts/b&apos;s.ttf'") != std::string::npos, "single quotes kept, the quote escaped");
        out = withAttribute(doc, doc.find("text[0]"), "value", "A < B & \"C\"");
        check(out.find("value=\"A &lt; B &amp; &quot;C&quot;\"") != std::string::npos, "entities written for an edited value");
        check(out.find("Tom &amp;") == std::string::npos, "the old value is gone");
        LayoutDoc again;
        check(parseLayoutText(out, again, error) && again.elements[again.find("text[0]")].value("value") == "A < B & \"C\"",
              "and read back the same");

        out = withAttribute(doc, doc.find("text[0]"), "alpha", "0.5");
        checkEqual(changed(HAND, out), " alpha=\"0.5\"", "a new attribute goes after the last one");
        check(out.find("layer=\"4\" alpha=\"0.5\"/>") != std::string::npos, "before the self-closing />");
        out = withAttribute(doc, doc.find("reloadableText[0]"), "width", "300");
        check(out.find("y=\"900\"\r\n                    width=\"300\"/>") != std::string::npos,
              "one attribute per line: the new one gets its own line, indented the same");
        out = withAttribute(doc, doc.find("image[1]/onEnter[0]"), "menuIndex", "1");
        check(out.find("<onEnter menuIndex=\"1\">") != std::string::npos, "an element with no attributes gets one after its name");

        out = withoutAttribute(doc, logo, "xOrigin");
        checkEqual(changed(HAND, out), "", "removing deletes bytes only");
        check(out.size() + std::string(" xOrigin=\"center\"").size() == HAND.size() && out.find("height=\"200\" layer=\"3\"") != std::string::npos,
              "the attribute and the space before it");
    });

    Test::run("layout text: delete and duplicate take whole lines and keep the rest", [] {
        LayoutDoc doc;
        std::string error;
        parseLayoutText(HAND, doc, error);
        const int bg = doc.find("image[0]");
        std::string out = withoutElement(doc, bg);
        check(out.find("images/bg.png") == std::string::npos, "the element is gone");
        check(out.size() + doc.elements[bg].end - doc.elements[bg].begin + 6 == HAND.size(), "with its indentation and CRLF only");
        check(out.find("/>\r\n\r\n    <!-- the logo -->") != std::string::npos, "the blank line and comment after it stay");
        LayoutDoc after;
        check(parseLayoutText(out, after, error) && after.find("image[0]") > 0 &&
              after.elements[after.find("image[0]")].value("src") == "images/logo.png", "the next image is image[0] now");
        check(withoutElement(doc, 0) == HAND, "the root can't be deleted");

        const int logo = doc.find("image[1]");
        out = withDuplicate(doc, logo);
        const std::string original = HAND.substr(doc.elements[logo].begin, doc.elements[logo].end - doc.elements[logo].begin);
        check(parseLayoutText(out, after, error), "the copy scans");
        const int copy = after.find(duplicatePath("image[1]"));
        check(copy > 0 && out.substr(after.elements[copy].begin, after.elements[copy].end - after.elements[copy].begin) == original,
              "image[2] is the copy, animations and all");
        check(out.find("    </image>\r\n    <image src='images/logo.png'") != std::string::npos, "on its own lines, indented");
        checkEqual(duplicatePath("menu[0]/item[2]"), "menu[0]/item[3]", "the copy's path");
    });

    Test::run("layout session: opening, refusing, and the pages of a theme", [] {
        std::string root = makeHandRoot("layout_open");
        std::vector<std::string> pages = LayoutSession::themePages(root + "/layouts/Hand");
        std::vector<std::string> expected = { "layout.xml", "layout 4x3.xml", "splash.xml", "collections/NES/layout/layout.xml",
                                              "forms/keyboard/layout.xml", "forms/layout.xml" };
        check(pages == expected, "layout.xml, variants, splash, collection pages, forms pages");

        LayoutSession s;
        std::string error;
        check(s.open(root, "Hand", "layout.xml", error), "a hand-made page opens: " + error);
        check(s.canvasW() == 1920 && s.canvasH() == 1080, "its size");
        check(!s.dirty() && !s.canUndo(), "clean");
        check(s.listed().size() == 11, "the list: the page, its 6 children, item defaults and 3 items = " + std::to_string(s.listed().size()));
        std::vector<std::string> anims = s.animations(s.doc().find("image[1]"));
        check(anims.size() == 2 && anims[0] == "onEnter (1 set)", "animations are listed");
        std::vector<std::string> props = s.animatedProperties(s.doc().find("image[1]"));
        check(props.size() == 2 && props[1] == "y", "and what they animate");

        LayoutSession t;
        check(!t.open(root, "Hand", "../Other/layout.xml", error), "a page outside the theme is refused");
        check(!t.open(root, "..", "Hand/layout.xml", error), "so is a folder that isn't a theme name");
        check(!t.open(root, "Hand", root + "/layouts/Other/layout.xml", error), "and an absolute path");
        check(!t.open(root, "Hand", "missing.xml", error), "a page that doesn't exist");
        writeFile(root + "/layouts/Hand/broken.xml", "<layout><image></layout>");
        check(!t.open(root, "Hand", "broken.xml", error) && !error.empty(), "one that doesn't parse: " + error);
        writeFile(root + "/layouts/Hand/editor.xml", editorMarker("x") + "\n<layout width=\"1\" height=\"1\"/>");
        check(!t.open(root, "Hand", "editor.xml", error) && error.find("theme editor") != std::string::npos,
              "a page the widget editor wrote belongs to it");
    });

    Test::run("layout session: moving and resizing write numbers only where the box changed", [] {
        std::string root = makeHandRoot("layout_move");
        LayoutSession s;
        std::string error;
        s.open(root, "Hand", "layout.xml", error);
        feedGeometry(s);
        const int logo = s.doc().find("image[1]");
        DragRect r = s.box(logo);
        check(r.x == 760 && r.y == 100 && r.w == 400 && r.h == 200, "the logo's box from its geometry (centred origin)");

        // Clicking selects the top element; a vertical drag keeps x="center"
        drag(s, 960, 150, 960, 150);
        check(s.selected() == logo, "a click picks the logo over the background");
        drag(s, 960, 150, 960, 182);
        const LayoutElement* e = s.selectedElement();
        check(e && e->value("x") == "center" && e->value("y") == "130", "y snapped to 130, x still center: y=" + (e ? e->value("y") : ""));
        check(s.box(logo).y == 130, "the box stays where it was dropped");
        check(s.dirty() && s.canUndo(), "an undo step");

        // A box whose edge isn't on the grid isn't snapped sideways by a vertical drag
        ElementGeom odd = *s.geometry(logo);
        odd.w = 403;
        s.setGeometry("image[1]", odd);
        r = s.box(logo);
        drag(s, 960, 200, 961, 240);
        check(s.selectedElement()->value("x") == "center" && s.box(logo).x == r.x, "x stays center when the drag is straight down");
        check(s.undo(), "undone");
        odd.w = 400;
        odd.y = 130;
        s.setGeometry("image[1]", odd);

        // Sideways: x becomes a number
        drag(s, 960, 200, 1000, 200);
        e = s.selectedElement();
        checkEqual(e->value("x"), "1000", "x becomes the resolved centre plus the move");

        // Resize from the east handle: width changes, and so does x (the origin is the centre)
        r = s.box(logo);
        drag(s, static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h / 2), static_cast<float>(r.x + r.w + 100), static_cast<float>(r.y + r.h / 2));
        e = s.selectedElement();
        check(e->value("width") == "500" && e->value("height") == "200" && e->value("x") == "1050",
              "width 500, height untouched, x follows the centre: x=" + e->value("x") + " width=" + e->value("width"));
        check(s.box(logo).x == r.x && s.box(logo).w == 500, "the west edge didn't move");

        // A menu item moves by its xOffset (its x comes from <itemDefaults>)
        const int item = s.doc().find("menu[0]/item[2]");
        DragRect ir = s.box(item);
        check(ir.x == 1125, "the third item's box");
        drag(s, static_cast<float>(ir.x + 10), static_cast<float>(ir.y + 10), static_cast<float>(ir.x + 60), static_cast<float>(ir.y + 10));
        e = s.selectedElement();
        check(s.selected() == item && e->value("xOffset") == "350" && !e->has("x"), "xOffset 300 -> 350, no x written");

        // Nudges and the layer
        check(s.moveBy(0, -3) && s.selectedElement()->value("y") == "977", "a nudge writes y (no own yOffset): " + s.selectedElement()->value("y"));
        check(s.setLayer(25) && s.selectedElement()->value("layer") == "19", "layers are clamped to 0..19");

        // Everything else in the file is as it was
        std::string text = s.text();
        check(text.find("<!-- Hand: <image src=\"not-an-element.png\"/> -->\r\n") != std::string::npos &&
              text.find("<onMenuEnter><set duration=\".2\"><animate type=\"y\" to=\"120\"/></set></onMenuEnter>\r\n") != std::string::npos &&
              text.find("font='fonts/a.ttf'") != std::string::npos,
              "comments, animations and quotes are untouched");
        check(checkLayoutXml(text) == checkLayoutXml(HAND), "the layout check says the same as before");
    });

    Test::run("layout session: attributes, undo and redo", [] {
        std::string root = makeHandRoot("layout_undo");
        LayoutSession s;
        std::string error;
        s.open(root, "Hand", "layout.xml", error);
        check(s.selectPath("text[0]"), "select the text");
        s.setCoalesceKey("value");
        s.setAttribute("value", "T");
        s.setAttribute("value", "To");
        s.setAttribute("value", "Tom");
        s.endCoalesce();
        check(s.selectedElement()->value("value") == "Tom", "typed");
        check(s.setAttribute("fontColor", "ff0000") && s.removeAttribute("layer"), "add one, remove one");
        check(!s.setAttribute("1bad", "x") && !LayoutSession::attributeNameProblem("a b").empty(), "bad names are refused");
        check(s.undo() && s.selectedElement()->value("layer") == "4", "undo brings the layer back");
        check(s.undo() && !s.selectedElement()->has("fontColor"), "and takes the colour off");
        check(s.undo() && s.selectedElement()->value("value") == "Tom & Jerry \"live\"", "the typing was one step");
        check(!s.dirty() && s.text() == HAND && !s.canUndo(), "back to the file exactly");
        check(s.redo() && s.redo() && s.selectedElement()->has("fontColor"), "redo");
        check(s.selectedElement()->path == "text[0]", "the selection follows");

        // Duplicate and delete, and their undo
        s.selectPath("image[1]");
        check(s.duplicateSelected() && s.selectedElement()->path == "image[2]", "the copy is selected");
        check(s.deleteSelected() && s.selected() < 0, "deleted");
        const unsigned structure = s.structure();
        check(s.selectPath("menu[0]/item[1]") && s.deleteSelected() && s.selectedElement() && s.selectedElement()->path == "menu[0]",
              "deleting an item selects its menu");
        check(s.structure() != structure, "paths changed");
        check(s.undo() && s.doc().find("menu[0]/item[2]") > 0, "undo puts the item back");
    });

    Test::run("layout session: saving keeps the original, and it can be put back", [] {
        std::string root = makeHandRoot("layout_save");
        const std::string file = root + "/layouts/Hand/layout.xml";
        const std::string backup = root + "/manage/backups/layouts/Hand/layout.xml";
        LayoutSession s;
        std::string error;
        s.open(root, "Hand", "layout.xml", error);
        check(!s.hasOriginal(), "no original kept before a change");
        s.selectPath("image[0]");
        s.setAttribute("alpha", "0.5");
        check(s.save(error), "saved: " + error);
        check(!s.dirty(), "clean after saving");
        check(readFile(backup) == HAND, "the original was kept in manage/backups first");
        std::string saved = readFile(file);
        checkEqual(changed(HAND, saved), " alpha=\"0.5\"", "the file differs by the edit only");
        check(!fs::exists(file + ".tmp"), "no temp file left");

        s.setAttribute("alpha", "0.25");
        check(s.save(error) && readFile(backup) == HAND, "a second save leaves the original alone");
        const std::string second = readFile(file);

        // Something else changes the file: saving over it is refused
        writeFile(file, saved + "<!-- edited elsewhere -->");
        s.setAttribute("alpha", "1");
        check(!s.save(error) && error.find("changed by something else") != std::string::npos, "refused: " + error);
        writeFile(file, second);   // the other program's change undone

        check(s.hasOriginal() && s.restoreOriginal(error), "put back the original: " + error);
        check(readFile(file) == HAND && s.text() == HAND && !s.dirty() && s.canUndo(), "the file and the session are the original (undoable)");
        check(fs::exists(root + "/manage/backups/layouts/Hand/layout (2).xml"), "what was there is kept as a numbered copy");

        // A page that went away isn't written again
        LayoutSession other;
        check(other.open(root, "Hand", "splash.xml", error), "open another page");
        other.selectPath("");
        other.setAttribute("width", "1280");
        std::error_code ec;
        fs::remove_all(root + "/layouts/Hand", ec);
        check(!other.save(error) && !fs::exists(root + "/layouts/Hand/splash.xml"), "a page that's gone isn't written: " + error);
    });

    Test::run("layout text: encoded values read back the same in rapidxml", [] {
        const std::string values[] = { "plain", "A & B", "<tag>", "say \"hi\"", "it's", "both ' and \"", "&amp; already",
                                       "x > y", "%I:%M:%S %p", "" };
        for (const std::string& value : values) {
            for (char quote : { '"', '\'' }) {
                std::string xml = std::string("<layout a=") + quote + xmlEncodeAttr(value, quote) + quote + "/>";
                std::vector<char> buffer(xml.begin(), xml.end());
                buffer.push_back('\0');
                rapidxml::xml_document<> doc;
                bool parsed = true;
                try { doc.parse<0>(buffer.data()); } catch (...) { parsed = false; }
                rapidxml::xml_node<>* root = parsed ? doc.first_node("layout") : nullptr;
                rapidxml::xml_attribute<>* a = root ? root->first_attribute("a") : nullptr;
                check(a && std::string(a->value()) == value, "rapidxml reads back [" + value + "] in " + quote + " quotes");
                LayoutDoc ours;
                std::string error;
                check(parseLayoutText(xml, ours, error) && ours.elements[0].value("a") == value, "and so does the scanner");
            }
        }
    });

    Test::run("layout session: exact moves for half-unit boxes, W/N resizes, x with xOffset", [] {
        std::string root = makeHandRoot("layout_exact");
        LayoutSession s;
        std::string error;
        s.open(root, "Hand", "layout.xml", error);
        feedGeometry(s);
        const int logo = s.doc().find("image[1]");
        // An odd width with a centred origin: the left edge is on a half unit
        ElementGeom odd = *s.geometry(logo);
        odd.w = 403;
        s.setGeometry("image[1]", odd);
        s.select(logo);
        const int startX = s.box(logo).x;
        for (int i = 1; i <= 3; ++i) {
            s.endCoalesce();
            check(s.moveBy(1, 0), "nudge " + std::to_string(i));
            checkEqual(s.selectedElement()->value("x"), std::to_string(960 + i), "each nudge moves by exactly 1");
            check(s.box(logo).x == startX + i, "and so does the box");
        }
        // A snapped drag lands on the grid
        DragRect r = s.box(logo);
        drag(s, static_cast<float>(r.x + 100), 200, static_cast<float>(r.x + 140), 200);
        check(s.box(logo).x % 5 == 0, "a snapped drag lands on the grid: " + std::to_string(s.box(logo).x));

        // W and N handles on a 400 x 200 box, centred across, top origin
        odd.w = 400;
        odd.x = 1000;
        odd.y = 130;
        s.setGeometry("image[1]", odd);
        s.setAttribute("x", "1000");
        s.setAttribute("y", "130");
        r = s.box(logo);   // 800,130 400x200
        check(r.x == 800 && r.y == 130, "the box to resize");
        drag(s, static_cast<float>(r.x), static_cast<float>(r.y + r.h / 2), static_cast<float>(r.x - 100), static_cast<float>(r.y + r.h / 2));
        const LayoutElement* e = s.selectedElement();
        check(e->value("width") == "500" && e->value("x") == "950" && e->value("height") == "200",
              "west handle: width 500, x 950 keeps the east edge (x=" + e->value("x") + ")");
        check(s.box(logo).x == 700 && s.box(logo).x + s.box(logo).w == 1200, "the east edge stayed at 1200");
        r = s.box(logo);
        drag(s, static_cast<float>(r.x + r.w / 2), static_cast<float>(r.y), static_cast<float>(r.x + r.w / 2), static_cast<float>(r.y - 30));
        e = s.selectedElement();
        check(e->value("height") == "230" && e->value("y") == "100" && e->value("width") == "500",
              "north handle: height 230, y 100 (y=" + e->value("y") + ")");

        // An element with both x and xOffset moves by x
        writeFile(root + "/layouts/Hand/both.xml",
                  "<layout width=\"1920\" height=\"1080\">\n  <image src=\"a.png\" x=\"100\" xOffset=\"20\" y=\"50\" width=\"10\" height=\"10\"/>\n</layout>\n");
        LayoutSession b;
        check(b.open(root, "Hand", "both.xml", error), "open: " + error);
        ElementGeom g;
        g.x = 100; g.xOffset = 20; g.y = 50; g.w = 10; g.h = 10;
        b.setGeometry("image[0]", g);
        b.selectPath("image[0]");
        check(b.box(b.selected()).x == 120, "the box includes the offset");
        check(b.moveBy(10, 0), "moved");
        check(b.selectedElement()->value("x") == "110" && b.selectedElement()->value("xOffset") == "20", "x moves, xOffset stays");
    });

    Test::run("layout session: put back the original is undoable and checks the disk; hidden boxes", [] {
        std::string root = makeHandRoot("layout_restore");
        const std::string file = root + "/layouts/Hand/layout.xml";
        LayoutSession s;
        std::string error;
        s.open(root, "Hand", "layout.xml", error);
        s.selectPath("image[0]");
        s.setAttribute("alpha", "0.5");
        check(s.save(error), "saved");
        const std::string saved = readFile(file);
        s.setAttribute("alpha", "0.25");   // unsaved

        writeFile(file, saved + "<!-- elsewhere -->");
        check(!s.restoreOriginal(error) && error.find("changed by something else") != std::string::npos,
              "refused after another program changed the file: " + error);
        check(readFile(file) == saved + "<!-- elsewhere -->", "and nothing was written");
        writeFile(file, saved);

        check(s.restoreOriginal(error), "put back: " + error);
        check(readFile(file) == HAND && s.text() == HAND && !s.dirty(), "the original, on disk and here");
        check(s.canUndo() && s.undo(), "undoable");
        s.selectPath("image[0]");
        check(s.dirty() && s.selectedElement()->value("alpha") == "0.25", "Undo brings back the unsaved edit");

        // Hidden boxes are by path: they don't move to another element when paths shift
        s.setHidden(s.doc().find("image[1]"), true);
        check(s.hidden(s.doc().find("image[1]")), "hidden");
        s.selectPath("image[0]");
        s.duplicateSelected();
        check(!s.hidden(s.doc().find("image[1]")) && !s.hidden(s.doc().find("image[2]")), "cleared when paths change");
    });

    Test::run("layout session: nothing is written when the backup can't be kept, or outside the theme", [] {
        std::string root = makeHandRoot("layout_unwritable");
        const std::string file = root + "/layouts/Hand/layout.xml";
        writeFile(root + "/manage", "a file where the backups folder would go");
        LayoutSession s;
        std::string error;
        s.open(root, "Hand", "layout.xml", error);
        s.selectPath("image[0]");
        s.setAttribute("alpha", "0.5");
        check(!s.save(error) && !error.empty(), "the save is refused when the original can't be kept: " + error);
        check(readFile(file) == HAND && s.dirty(), "the page is untouched and the edit still there");

        // A folder inside the theme that leads outside it (a junction or link)
        const std::string outside = root + "/elsewhere/layout";
        writeFile(outside + "/layout.xml", "<layout width=\"10\" height=\"10\"></layout>");
        fs::create_directories(root + "/layouts/Hand/collections/Linked");
        const std::string link = root + "/layouts/Hand/collections/Linked/layout";
        bool linked = false;
        std::error_code ec;
#ifdef _WIN32
        std::string command = "mklink /J \"" + fs::path(link).make_preferred().string() + "\" \"" +
                              fs::path(outside).make_preferred().string() + "\" >nul 2>&1";
        linked = std::system(command.c_str()) == 0 && fs::exists(link + "/layout.xml");
#else
        fs::create_directory_symlink(outside, link, ec);
        linked = !ec && fs::exists(link + "/layout.xml");
#endif
        if (!linked) {
            std::cout << "  (skipped: couldn't make a junction or link)" << std::endl;
            return;
        }
        LayoutSession t;
        check(!t.open(root, "Hand", "collections/Linked/layout/layout.xml", error) && error.find("isn't inside") != std::string::npos,
              "a page reached through a junction out of the theme isn't opened: " + error);
        fs::remove(link, ec);   // the junction, not what it points at
        check(fs::exists(outside + "/layout.xml"), "the outside folder is untouched");
    });

    Test::run("layout boxes: from traced views to boxes", [] {
        const std::string xml =
            "<layout width=\"1920\" height=\"1080\">\n"
            "  <image src=\"a.png\" x=\"10\" y=\"20\"/>\n"
            "  <text value=\"Hello\" x=\"100\" y=\"100\" fontSize=\"20\"/>\n"
            "  <menu type=\"custom\">\n"
            "    <itemDefaults width=\"100\" height=\"50\"/>\n"
            "    <item x=\"200\" y=\"500\"/>\n"
            "    <item x=\"400\" y=\"520\"/>\n"
            "  </menu>\n"
            "  <menu y=\"600\" height=\"200\" width=\"300\"/>\n"
            "</layout>\n";
        LayoutDoc doc;
        std::string error;
        check(parseLayoutText(xml, doc, error), "scans");
        int image = 1, menuA = 2, menuB = 3;
        std::vector<TracedView> views;
        TracedView v;
        v.path = "image[0]"; v.component = &image; v.x = 10; v.y = 20; v.scaledW = 64; v.scaledH = 32; v.layer = 2;
        views.push_back(v);
        v = TracedView(); v.path = "text[0]"; v.component = &image + 1; v.x = 100; v.y = 100; v.fontSize = 20;
        views.push_back(v);
        v = TracedView(); v.path = "menu[0]"; v.component = &menuA;
        views.push_back(v);
        v = TracedView(); v.path = "menu[0]/item[0]"; v.component = &menuA; v.slot = true; v.x = 200; v.y = 500; v.scaledW = 100; v.scaledH = 50;
        views.push_back(v);
        v.path = "menu[0]/item[1]"; v.x = 400; v.y = 520;
        views.push_back(v);
        v = TracedView(); v.path = "menu[1]"; v.component = &menuB; v.y = 600; v.scaledW = 300; v.scaledH = 200;
        views.push_back(v);
        for (int i = 0; i < 3; ++i) {
            // A vertical menu without <itemDefaults>: its slots come from the empty one PageBuilder adds
            v = TracedView(); v.path = "menu[1]/itemDefaults[0]"; v.component = &menuB; v.slot = true;
            v.y = 600.0f + 60.0f * i; v.scaledW = 300; v.scaledH = 60;
            views.push_back(v);
        }
        PageBoxes boxes = pageBoxes(doc, views);
        const ElementGeom& img = boxes.geometry["image[0]"];
        check(!img.approximate && img.left() == 10 && img.top() == 20 && img.w == 64 && img.layer == 2, "an image's own box");
        const ElementGeom& text = boxes.geometry["text[0]"];
        check(text.approximate && text.h == 20 && text.w == 50, "text without a width: font size by value length, a guess");
        const ElementGeom& menu = boxes.geometry["menu[0]"];
        check(menu.around && menu.readOnly && menu.aroundBox.x == 200 && menu.aroundBox.y == 500 &&
              menu.aroundBox.w == 300 && menu.aroundBox.h == 70, "a size-less menu: the box around its items");
        check(menu.x == 0 && menu.y == 0, "its resolved position stays the engine's");
        check(!boxes.geometry["menu[0]/item[1]"].readOnly, "custom menu items can be dragged");
        const ElementGeom& vertical = boxes.geometry["menu[1]"];
        check(!vertical.around && vertical.h == 200, "a vertical menu with a size keeps its own box");
        check(boxes.geometry.count("menu[1]/itemDefaults[0]") == 0, "the synthetic item defaults have no box of their own");
        check(boxes.extra.size() == 3 && boxes.extra[0].first == "menu[1]", "its slots are drawn as its menu's extra boxes");
    });

    Test::run("layout session: script commands", [] {
        std::string root = makeHandRoot("layout_script");
        LayoutSession s;
        std::string error;
        s.open(root, "Hand", "layout.xml", error);
        feedGeometry(s);
        std::string note;
        check(runLayoutCommand(s, parseEditorCommand("selectpath image[1]"), note) == CommandResult::Ok, "selectpath");
        check(runLayoutCommand(s, parseEditorCommand("move 0 40"), note) == CommandResult::Ok, "move: " + note);
        runLayoutCommand(s, parseEditorCommand("get y"), note);
        checkEqual(note, "y=\"140\"", "get notes the value");
        check(runLayoutCommand(s, parseEditorCommand("attr fontColor 00ff00"), note) == CommandResult::Ok, "attr");
        check(runLayoutCommand(s, parseEditorCommand("rmattr fontColor"), note) == CommandResult::Ok, "rmattr");
        check(runLayoutCommand(s, parseEditorCommand("select 0"), note) == CommandResult::Ok && s.selected() == 0, "select by list position");
        check(runLayoutCommand(s, parseEditorCommand("move 5 5"), note) == CommandResult::Failed, "the page itself has no box");
        check(runLayoutCommand(s, parseEditorCommand("undo"), note) == CommandResult::Ok, "undo");
        check(runLayoutCommand(s, parseEditorCommand("save"), note) == CommandResult::NotSession, "save is the screen's");
    });
});

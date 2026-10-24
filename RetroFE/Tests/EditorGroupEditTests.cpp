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

// The theme editors acting on several things at once, the follow-ups of the
// "several at once" round: group operations made on the 4:3 twin's own
// elements (align and distribute from that page's boxes, structural edits at
// the matching place: LayoutTwin's twinGeometry and runOnBoth), one attribute
// on every selected element or widget ("(several values)"), and the
// alignment guides while dragging (EditorSelection.h).

#include "TestSupport.h"
#include "../Source/Manager/Themes/ThemeProvider.h"
#include "../Source/ThemeEditor/EditorScript.h"
#include "../Source/ThemeEditor/EditorSelection.h"
#include "../Source/ThemeEditor/EditorSession.h"
#include "../Source/ThemeEditor/LayoutSession.h"
#include "../Source/ThemeEditor/LayoutText.h"
#include "../Source/ThemeEditor/LayoutTwin.h"
#include "../Source/ThemeEditor/ThemeStore.h"
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace ThemeEditor;

namespace
{
    // A 16:9 page and its 4:3 twin. The texts have no size of their own (the
    // engine's box for them is a guess from fontSize), as Aeon Nox's.
    const std::string WIDE =
        "<layout width=\"1920\" height=\"1080\">\n"
        "    <image src=\"images/bg.png\" x=\"0\" y=\"0\" width=\"1920\" height=\"1080\" layer=\"0\"/>\n"
        "    <reloadableText type=\"manufacturer\" x=\"145\" y=\"135\" maxWidth=\"300\" xOrigin=\"left\" yOrigin=\"center\" fontSize=\"40\" layer=\"10\"/>\n"
        "    <reloadableText type=\"rating\" x=\"620\" y=\"215\" maxWidth=\"300\" xOrigin=\"left\" yOrigin=\"center\" fontSize=\"40\" layer=\"10\"/>\n"
        "    <reloadableText type=\"year\" x=\"1100\" y=\"300\" maxWidth=\"300\" xOrigin=\"left\" yOrigin=\"center\" fontSize=\"40\" layer=\"10\"/>\n"
        "    <image src=\"images/logo.png\" x=\"1500\" y=\"700\" width=\"300\" height=\"150\" layer=\"5\">\n"
        "        <onEnter><set duration=\".3\"><animate type=\"alpha\" from=\"0\" to=\"1\"/></set></onEnter>\n"
        "    </image>\n"
        "    <text value=\"Wide only\" x=\"10\" y=\"10\" fontSize=\"20\" layer=\"3\"/>\n"
        "</layout>\n";
    const std::string NARROW =
        "<layout width=\"1600\" height=\"1200\">\r\n"
        "  <image src=\"images/bg.png\" x=\"0\" y=\"0\" width=\"1600\" height=\"1200\" layer=\"0\"/>\r\n"
        "  <reloadableText type=\"manufacturer\" x=\"90\" y=\"138\" maxWidth=\"300\" xOrigin=\"left\" yOrigin=\"center\" fontSize=\"40\" layer=\"10\"/>\r\n"
        "  <reloadableText type=\"rating\" x=\"510\" y=\"223\" maxWidth=\"300\" xOrigin=\"left\" yOrigin=\"center\" fontSize=\"40\" layer=\"10\"/>\r\n"
        "  <reloadableText type=\"year\" x=\"900\" y=\"330\" maxWidth=\"300\" xOrigin=\"left\" yOrigin=\"center\" fontSize=\"36\" layer=\"10\"/>\r\n"
        "  <image src=\"images/logo.png\" x=\"1200\" y=\"800\" width=\"240\" height=\"120\" layer=\"5\">\r\n"
        "    <onEnter><set duration=\".3\"><animate type=\"alpha\" from=\"0\" to=\"1\"/></set></onEnter>\r\n"
        "  </image>\r\n"
        "</layout>\r\n";

    LayoutDoc parse(const std::string& text)
    {
        LayoutDoc doc;
        std::string error;
        parseLayoutText(text, doc, error);
        return doc;
    }

    std::string value(const std::string& text, const std::string& path, const std::string& name)
    {
        LayoutDoc doc = parse(text);
        const int i = doc.find(path);
        return i < 0 ? "(no element)" : doc.elements[i].value(name, "(unset)");
    }

    std::string str(const DragRect& r)
    {
        return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h);
    }

    // The engine's boxes for WIDE, as the screen would hand them over: the
    // texts' sizes are guesses (geometryOf), the images' their own
    void feed(LayoutSession& s)
    {
        const LayoutDoc& doc = s.doc();
        int order = 0;
        for (const LayoutElement& e : doc.elements) {
            if (e.path.empty() || (e.name != "image" && e.name != "reloadableText" && e.name != "text")) continue;
            TracedView v;
            v.path = e.path;
            v.x = static_cast<float>(std::atof(e.value("x").c_str()));
            v.y = static_cast<float>(std::atof(e.value("y").c_str()));
            v.yOrigin = e.value("yOrigin") == "center" ? 0.5f : 0.0f;
            v.fontSize = e.has("fontSize") ? static_cast<float>(std::atof(e.value("fontSize").c_str())) : -1.0f;
            v.maxWidth = e.has("maxWidth") ? static_cast<float>(std::atof(e.value("maxWidth").c_str())) : -1.0f;
            if (e.name == "image") {
                v.scaledW = v.width = static_cast<float>(std::atof(e.value("width").c_str()));
                v.scaledH = v.height = static_cast<float>(std::atof(e.value("height").c_str()));
            }
            s.setGeometry(e.path, geometryOf(doc, v, order++));
        }
    }

    struct Pair
    {
        std::string root;
        LayoutSession here, there;
        TwinSync state;
    };

    bool openPair(Pair& p, const std::string& name, const std::string& wide = WIDE, const std::string& narrow = NARROW)
    {
        p.root = makeRoot(name);
        writeFile(p.root + "/layouts/Wide/layout.xml", wide);
        writeFile(p.root + "/layouts/Wide/layout 4x3.xml", narrow);
        std::string error;
        if (!p.here.open(p.root, "Wide", "layout.xml", error) || !p.there.open(p.root, "Wide", "layout 4x3.xml", error)) return false;
        p.state.reset(p.here);
        feed(p.here);
        return true;
    }

    // Undo on both pages, as the screen's undoBoth does
    void undoBoth(Pair& p)
    {
        const unsigned link = p.here.undoLink();
        p.here.undo();
        if (link && p.there.undoLink() == link) p.there.undo();
        p.state.reset(p.here);
    }

    TwinOp alignOp(int op)
    {
        TwinOp t;
        t.movable = t.boxes = t.evenIfUnchanged = true;
        t.run = [op](LayoutSession& page, const std::function<int(int)>&) { return page.alignSelected(op); };
        return t;
    }

    void select(LayoutSession& s, const std::vector<std::string>& paths)
    {
        std::vector<int> sel;
        for (const std::string& p : paths) sel.push_back(s.doc().find(p));
        s.setSelection(sel);
    }

    CanvasFit unitFit() { return canvasFit(1920.0f, 1080.0f, 1920, 1080); }

    bool openThree(const std::string& root, EditorSession& s)
    {
        SaveInputs inputs;
        inputs.fontBytes = readFile(std::string(RETROFE_THIRD_PARTY_DIR) + "/fonts/Roboto-Regular.ttf");
        std::string error, folder;
        const std::string layouts = root + "/layouts";
        if (!createTheme(layouts, "Group", false, Themes::widgetLibrary(), inputs, folder, error)) return false;
        if (!s.open(layouts, folder, Themes::widgetLibrary(), error)) return false;
        const int rects[3][4] = { { 100, 100, 200, 100 }, { 500, 300, 200, 100 }, { 900, 600, 200, 100 } };
        for (const auto& r : rects) {
            if (!s.addWidget("static_text") || !s.setRect(r[0], r[1], r[2], r[3])) return false;
        }
        return s.page().widgets.size() == 3;
    }
}

static Test::Suite editorGroupEdit("editor group edits", [] {
    Test::run("twin boxes: from the other page's own attributes, sizes as the engine scales them", [] {
        const LayoutDoc wide = parse(WIDE), narrow = parse(NARROW);
        // A text whose size is a guess: the same guess there, at its own place
        TracedView v;
        v.path = "reloadableText[0]";
        v.x = 145;
        v.y = 135;
        v.yOrigin = 0.5f;
        v.fontSize = 40;
        v.maxWidth = 300;
        const ElementGeom g = geometryOf(wide, v, 1);
        ElementGeom t;
        check(twinGeometry(wide, wide.find("reloadableText[0]"), g, 1920, 1080, narrow, narrow.find("reloadableText[0]"), 1600, 1200, t),
              "the manufacturer has a box there");
        check(t.x == 90 && t.y == 138 && t.yOrigin == 0.5f && t.approximate, "at its own x, y and origin");
        check(t.top() == 118 && t.w == 300 && t.h == 40, "its box there: " + std::to_string(t.top()) + " " + std::to_string(t.w) + "x" + std::to_string(t.h));
        // A smaller font there: a smaller guess
        v.path = "reloadableText[2]";
        const ElementGeom year = geometryOf(wide, v, 3);
        check(twinGeometry(wide, wide.find("reloadableText[2]"), year, 1920, 1080, narrow, narrow.find("reloadableText[2]"), 1600, 1200, t) &&
              t.h == 36, "the year's font is 36 there");
        // An image with its own size on both: that page's
        ElementGeom img;
        img.x = 1500;
        img.y = 700;
        img.w = 300;
        img.h = 150;
        check(twinGeometry(wide, wide.find("image[1]"), img, 1920, 1080, narrow, narrow.find("image[1]"), 1600, 1200, t) && t.x == 1200 &&
              t.y == 800 && t.w == 240 && t.h == 120 && !t.approximate, "the logo there: its own place and size");

        // Sizes the engine works out: height alone keeps the art's shape, maxWidth clamps it
        const LayoutDoc a = parse("<layout width=\"1920\" height=\"1080\"><image src=\"a.png\" x=\"10\" y=\"10\" height=\"100\"/></layout>");
        const LayoutDoc b = parse("<layout width=\"1600\" height=\"1200\"><image src=\"a.png\" x=\"20\" y=\"30\" height=\"80\" maxWidth=\"100\"/></layout>");
        ElementGeom art;
        art.x = 10;
        art.y = 10;
        art.w = 200;   // 2:1 art at height 100
        art.h = 100;
        check(twinGeometry(a, 1, art, 1920, 1080, b, 1, 1600, 1200, t) && t.w == 100 && t.h == 50 && !t.approximate,
              "height 80 makes it 160 wide, maxWidth 100 brings it to 100x50: " + std::to_string(t.w) + "x" + std::to_string(t.h));
        // Its own size unknown (clamped here) and not set there: scaled with the page, marked a guess
        const LayoutDoc c = parse("<layout width=\"1920\" height=\"1080\"><image src=\"a.png\" maxWidth=\"200\"/></layout>");
        const LayoutDoc d = parse("<layout width=\"1600\" height=\"1200\"><image src=\"a.png\" maxWidth=\"400\"/></layout>");
        check(twinGeometry(c, 1, art, 1920, 1080, d, 1, 1600, 1200, t) && t.approximate && t.w > 166 && t.w < 167, "an unknown size is scaled");
        // A menu item takes what it doesn't set from <itemDefaults>
        const LayoutDoc m1 = parse("<layout width=\"1920\" height=\"1080\"><menu type=\"custom\"><itemDefaults x=\"center\" y=\"900\" width=\"200\" height=\"100\"/>"
                                   "<item xOffset=\"-300\"/></menu></layout>");
        const LayoutDoc m2 = parse("<layout width=\"1600\" height=\"1200\"><menu type=\"custom\"><itemDefaults x=\"center\" y=\"1000\" width=\"180\" height=\"90\"/>"
                                   "<item xOffset=\"-250\"/></menu></layout>");
        ElementGeom item;
        item.x = 960;
        item.y = 900;
        item.xOffset = -300;
        item.w = 200;
        item.h = 100;
        const int i1 = m1.find("menu[0]/item[0]"), i2 = m2.find("menu[0]/item[0]");
        check(twinGeometry(m1, i1, item, 1920, 1080, m2, i2, 1600, 1200, t) && t.x == 800 && t.y == 1000 && t.xOffset == -250 && t.w == 180 && t.h == 90,
              "the item there: its defaults' place and size, its own offset");
        check(!twinGeometry(wide, 0, g, 1920, 1080, narrow, 0, 1600, 1200, t), "the page itself has no box");
    });

    Test::run("twin: align is made on the 4:3 page from its own boxes, one undo step", [] {
        Pair p;
        check(openPair(p, "group_twin_align"), "both pages open");
        select(p.here, { "reloadableText[0]", "reloadableText[1]" });
        const TwinOpResult r = runOnBoth(p.here, p.there, p.state, alignOp(AL_TOP));
        check(r.here && r.there && r.linked && r.missing.empty(), "lined up on both, linked");
        checkEqual(value(p.here.text(), "reloadableText[1]", "y"), "135", "16:9: the rating up to the manufacturer");
        checkEqual(value(p.there.text(), "reloadableText[0]", "y"), "138", "4:3: the manufacturer stays");
        checkEqual(value(p.there.text(), "reloadableText[1]", "y"), "138", "4:3: the rating lines up with it (not 134, a scaled move)");
        check(value(p.there.text(), "reloadableText[1]", "x") == "510", "its x untouched");
        check(p.there.selection().empty() && p.there.geometry(p.there.doc().find("reloadableText[1]")) == nullptr,
              "the 4:3 page's selection and borrowed boxes are as they were");
        // Nothing more to copy: the next sync does nothing
        MirrorResult m;
        check(syncTwinPages(p.here, p.there, p.state, m) == TwinCopy::Nothing, "the sync has nothing left");
        undoBoth(p);
        check(p.here.text() == WIDE && p.there.text() == NARROW, "one undo takes both back, byte for byte");

        // Already lined up here, not there: done there alone (its own undo step)
        feed(p.here);
        select(p.here, { "reloadableText[0]", "reloadableText[1]" });
        p.here.alignSelected(AL_TOP);
        p.state.reset(p.here);   // as if aligned before the twin was on
        feed(p.here);
        const TwinOpResult again = runOnBoth(p.here, p.there, p.state, alignOp(AL_TOP));
        check(!again.here && again.there && !again.linked, "lined up there only");
        checkEqual(value(p.there.text(), "reloadableText[1]", "y"), "138", "the 4:3 page lined up now");
    });

    Test::run("twin: distribute from the 4:3 page's own boxes", [] {
        Pair p;
        check(openPair(p, "group_twin_distribute"), "both pages open");
        select(p.here, { "reloadableText[0]", "reloadableText[1]", "reloadableText[2]" });
        TwinOp op;
        op.movable = op.boxes = op.evenIfUnchanged = true;
        op.run = [](LayoutSession& page, const std::function<int(int)>&) { return page.distributeSelected(false); };
        const TwinOpResult r = runOnBoth(p.here, p.there, p.state, op);
        check(r.here && r.there && r.linked, "spaced on both");
        // 16:9 tops 115, 195, 280 (h 40): gap (320 - 115 - 120) / 2 = 42.5 -> the middle at 115+40+42.5 = 197.5 -> 198, y 218
        checkEqual(value(p.here.text(), "reloadableText[1]", "y"), "218", "the middle one on 16:9");
        // 4:3 tops 118, 203, 312 (h 40, 40, 36): end 348, gaps (348-118-116)/2 = 57 -> the middle's top 215, y 235
        checkEqual(value(p.there.text(), "reloadableText[1]", "y"), "235", "the middle one on 4:3, from its own boxes");
    });

    Test::run("twin: an element that isn't on the other page stops the group op there", [] {
        Pair p;
        check(openPair(p, "group_twin_missing"), "both pages open");
        select(p.here, { "reloadableText[0]", "text[0]" });
        const TwinOpResult r = runOnBoth(p.here, p.there, p.state, alignOp(AL_LEFT));
        check(r.here && !r.there && r.missing.size() == 1, "lined up here only, one missing there");
        check(p.there.text() == NARROW, "the 4:3 page untouched (the screen then copies the moves in proportion)");
        MirrorResult m;
        check(syncTwinPages(p.here, p.there, p.state, m) == TwinCopy::Copied, "which the sync does");
    });

    Test::run("twin: duplicate, delete, add and animations at the matching place", [] {
        Pair p;
        check(openPair(p, "group_twin_structure"), "both pages open");
        auto op = [](std::function<bool(LayoutSession&, const std::function<int(int)>&)> run, bool primaryOnly = false) {
            TwinOp t;
            t.run = run;
            t.primaryOnly = primaryOnly;
            return t;
        };
        // Duplicate two: each copy right after its original, on both
        select(p.here, { "reloadableText[0]", "image[1]" });
        TwinOpResult r = runOnBoth(p.here, p.there, p.state, op([](LayoutSession& s, const std::function<int(int)>&) { return s.duplicateSelected(); }));
        check(r.here && r.there && r.linked, "duplicated on both");
        checkEqual(value(p.there.text(), "reloadableText[1]", "type"), "manufacturer", "the 4:3 copy right after its original");
        checkEqual(value(p.there.text(), "reloadableText[1]", "x"), "90", "a copy of the 4:3 element, its own numbers");
        check(p.there.doc().find("image[2]") >= 0 && p.there.text().find("\r\n") != std::string::npos, "and the logo's copy, CRLF kept");
        const LayoutDoc hereDoc = p.here.doc(), thereDoc = p.there.doc();
        bool paired = true;
        for (int i = 1; i < static_cast<int>(hereDoc.elements.size()); ++i) {
            if (hereDoc.elements[i].path == "text[0]") continue;
            const int j = matchElement(hereDoc, i, thereDoc);
            paired = paired && j >= 0 && thereDoc.elements[j].path == hereDoc.elements[i].path;
        }
        check(paired, "every element still matches its own afterwards");
        undoBoth(p);
        check(p.here.text() == WIDE && p.there.text() == NARROW, "one undo takes both back");

        // Delete: the matching element goes there too
        feed(p.here);
        select(p.here, { "reloadableText[1]" });
        r = runOnBoth(p.here, p.there, p.state, op([](LayoutSession& s, const std::function<int(int)>&) { return s.deleteSelected(); }));
        check(r.linked && p.there.text().find("rating") == std::string::npos && p.here.text().find("rating") == std::string::npos, "deleted on both");
        undoBoth(p);
        check(p.here.text() == WIDE && p.there.text() == NARROW, "and back");
        // One that's only here: nothing there, and said
        select(p.here, { "text[0]" });
        r = runOnBoth(p.here, p.there, p.state, op([](LayoutSession& s, const std::function<int(int)>&) { return s.deleteSelected(); }));
        check(r.here && !r.there && r.missing.size() == 1 && p.there.text() == NARROW, "a page-only element is deleted here only");
        p.here.undo();
        p.state.reset(p.here);

        // Add from the palette after the selected element: after its match there, centred on that page
        select(p.here, { "reloadableText[0]" });
        r = runOnBoth(p.here, p.there, p.state,
                      op([](LayoutSession& s, const std::function<int(int)>&) { return s.addElement("text", "", ""); }, true));
        check(r.here && r.there && r.linked, "added on both");
        const LayoutDoc added = parse(p.there.text());
        const int after = added.find("reloadableText[0]");
        const int next = after >= 0 && after + 1 < static_cast<int>(added.elements.size()) ? after + 1 : -1;
        check(next > 0 && added.elements[next].name == "text", "right after the manufacturer there too");
        check(next > 0 && std::atoi(added.elements[next].value("y").c_str()) > 540, "placed in the middle of the 1200-high page");
        undoBoth(p);
        check(p.here.text() == WIDE && p.there.text() == NARROW, "and back");

        // An event, then a step in it (by its element there)
        select(p.here, { "reloadableText[2]" });
        r = runOnBoth(p.here, p.there, p.state, op([](LayoutSession& s, const std::function<int(int)>&) { return s.addEvent("onEnter"); }, true));
        check(r.linked && value(p.there.text(), "reloadableText[2]/onEnter[0]/set[0]", "duration") != "(no element)", "an event on both");
        const int event = p.here.doc().find("reloadableText[2]/onEnter[0]");
        TwinOp step;
        step.primaryOnly = true;
        step.touches = { event };
        step.run = [event](LayoutSession& s, const std::function<int(int)>& map) { return s.addSet(map(event)); };
        r = runOnBoth(p.here, p.there, p.state, step);
        check(r.linked && value(p.there.text(), "reloadableText[2]/onEnter[0]/set[1]", "duration") != "(no element)", "a step on both");
        undoBoth(p);
        undoBoth(p);
        check(p.here.text() == WIDE && p.there.text() == NARROW, "both undone");
    });

    Test::run("one attribute on every selected element, byte for byte", [] {
        const std::string root = makeRoot("group_shared_layout");
        writeFile(root + "/layouts/Wide/layout.xml", WIDE);
        LayoutSession s;
        std::string error;
        check(s.open(root, "Wide", "layout.xml", error), "open: " + error);
        select(s, { "reloadableText[0]", "reloadableText[1]", "text[0]" });
        auto find = [](const std::vector<LayoutSession::SharedAttr>& all, const std::string& name) {
            for (const LayoutSession::SharedAttr& a : all) {
                if (a.name == name) return a.value;
            }
            return SharedValue();
        };
        std::vector<LayoutSession::SharedAttr> shared = s.sharedAttributes({ "x", "layer", "maxWidth", "alpha" });
        check(find(shared, "x").mixed && find(shared, "x").all, "x: several values");
        check(find(shared, "maxWidth").mixed && find(shared, "maxWidth").any && !find(shared, "maxWidth").all, "maxWidth: set on some");
        check(!find(shared, "alpha").mixed && !find(shared, "alpha").any, "alpha: set on none");
        check(find(shared, "fontSize").mixed, "fontSize, set on all three, listed too (differs)");
        bool hasType = false;
        for (const auto& a : shared) hasType = hasType || a.name == "type";
        check(!hasType, "type isn't on the text: not listed");
        // Typing a layer: one step for the run, written on all three
        s.setCoalesceKey("shared:layer");
        check(s.setAttributeAll("layer", "1") && s.setAttributeAll("layer", "12"), "typed");
        s.endCoalesce();
        check(value(s.text(), "reloadableText[0]", "layer") == "12" && value(s.text(), "text[0]", "layer") == "12", "every one has it");
        shared = s.sharedAttributes({ "layer" });
        check(!find(shared, "layer").mixed && find(shared, "layer").value == "12", "now the same");
        check(s.setAttributeAll("alpha", "0.5"), "an attribute none had");
        check(s.text().find("alpha=\"0.5\"") != std::string::npos && value(s.text(), "text[0]", "alpha") == "0.5", "added on each");
        check(s.removeAttributeAll("maxWidth") && value(s.text(), "reloadableText[0]", "maxWidth") == "(unset)" &&
              value(s.text(), "reloadableText[1]", "maxWidth") == "(unset)" && value(s.text(), "reloadableText[2]", "maxWidth") == "300",
              "removed from the selected ones that had it, not from the others");
        s.undo();
        s.undo();
        check(s.undo() && s.text() == WIDE, "three steps, back to the bytes");
        check(s.selectionCount() == 3, "the selection kept");
        // Scripts
        std::string note;
        check(runLayoutCommand(s, parseEditorCommand("attrall fontColor ff0000"), note) == CommandResult::Ok &&
              value(s.text(), "reloadableText[1]", "fontColor") == "ff0000", "attrall");
        check(runLayoutCommand(s, parseEditorCommand("shared fontColor x"), note) == CommandResult::Ok &&
              note == "fontColor \"ff0000\"; x (several values)", "shared: " + note);
    });

    Test::run("one attribute on several, copied to the twin by the usual rules", [] {
        Pair p;
        check(openPair(p, "group_shared_twin"), "both pages open");
        select(p.here, { "reloadableText[0]", "reloadableText[1]" });
        p.here.setAttributeAll("fontColor", "ffcc00");
        p.here.setAttributeAll("x", "200");
        MirrorResult m;
        check(syncTwinPages(p.here, p.there, p.state, m) == TwinCopy::Copied, "copied");
        check(value(p.there.text(), "reloadableText[0]", "fontColor") == "ffcc00" && value(p.there.text(), "reloadableText[1]", "fontColor") == "ffcc00",
              "a colour both pages agreed on goes to both");
        // x 145 -> 200 (+55 across 1920 = +46 across 1600) and 620 -> 200 (-420 = -350)
        checkEqual(value(p.there.text(), "reloadableText[0]", "x"), "136", "the first moved in proportion");
        checkEqual(value(p.there.text(), "reloadableText[1]", "x"), "160", "the second too");
    });

    Test::run("widget editor: one setting on every selected widget", [] {
        const std::string root = makeRoot("group_shared_widgets");
        EditorSession s;
        check(openThree(root, s), "a theme with three texts");
        s.setSelection({ 0, 1 });
        s.setLayer(4);
        s.setSelection({ 0, 1, 2 });
        auto find = [&s](const std::string& name) {
            for (const EditorSession::SharedSetting& v : s.sharedSettings()) {
                if (v.name == name) return v;
            }
            return EditorSession::SharedSetting();
        };
        check(find("layer").value.mixed, "layers differ");
        check(!find("alpha").value.mixed && find("alpha").value.value == "100", "opacity the same");
        check(find("VALUE").var != nullptr && find("CENTER").var != nullptr, "the texts' own settings are shared");
        check(find("fontColor").name == "fontColor", "they all have a text colour");
        check(s.setLayerAll(7), "layer for all");
        check(s.page().widgets[0].layer == 7 && s.page().widgets[1].layer == 7 && s.page().widgets[2].layer == 7, "all three on 7");
        check(!find("layer").value.mixed, "the same now");
        check(s.undo() && s.page().widgets[1].layer == 4 && s.page().widgets[2].layer != 7, "one undo step");
        s.setSelection({ 0, 1, 2 });
        check(s.setVarAll("CENTER", "yes") && s.page().widgets[2].varValue("CENTER", "") == "true", "a bool, as its widget takes it");
        check(s.setFontColorAll("#ff8800") && s.page().widgets[0].fontColor == "FF8800", "a colour, normalised");
        check(!s.setFontColorAll("nope"), "not a colour: nothing");
        check(!s.setAlphaAll(150) && s.page().widgets[1].alphaPct == 100, "150 is 100, as they are: nothing changes");
        check(s.setAlphaAll(40) && s.page().widgets[2].alphaPct == 40, "opacity for all");
        s.setCoalesceKey("shared:VALUE");
        check(s.setVarAll("VALUE", "H") && s.setVarAll("VALUE", "Hi"), "typing");
        s.endCoalesce();
        check(s.undo() && s.page().widgets[0].varValue("VALUE", "") != "Hi" && s.page().widgets[0].alphaPct == 40, "typing was one step");
        std::string note;
        check(runSessionCommand(s, parseEditorCommand("layerall 9"), note) == CommandResult::Ok && s.page().widgets[2].layer == 9, "layerall");
        check(runSessionCommand(s, parseEditorCommand("shared"), note) == CommandResult::Ok && note.find("layer \"9\"") != std::string::npos, "shared: " + note);
    });

    Test::run("guides: the nearest edge or centre within reach, and the lines", [] {
        const std::vector<DragRect> others = { makeRect(100, 100, 200, 100), makeRect(900, 500, 100, 51) };
        int shift = 0;
        check(guideShift(makeRect(104, 300, 50, 50), true, GE_ALL, others, 1920, 1080, 6, shift) && shift == -4, "left onto left (-4)");
        check(!guideShift(makeRect(110, 300, 50, 50), true, GE_ALL, others, 1920, 1080, 6, shift), "out of reach");
        check(guideShift(makeRect(908, 300, 50, 50), true, GE_ALL, others, 1920, 1080, 6, shift) && shift == 2,
              "its right edge (958) onto the page's centre, the nearest in reach");
        // Only its centre taking part: onto the page's centre
        check(guideShift(makeRect(907, 300, 100, 50), true, GE_MID, {}, 1920, 1080, 6, shift) && shift == 3, "centre onto the page's centre");
        // A centre on a half unit: an odd-sized box's
        check(guideShift(makeRect(300, 503, 10, 51), false, GE_MID, others, 1920, 1080, 6, shift) && shift == -3,
              "the middles of two 51-high boxes line up exactly (" + std::to_string(shift) + ")");
        std::vector<GuideLine> lines = guideLines(makeRect(100, 300, 50, 50), others, 1920, 1080);
        check(lines.size() == 1 && lines[0].vertical && lines[0].at == 100 && lines[0].from == 100 && lines[0].to == 350,
              "one line, joining the two boxes");
        lines = guideLines(makeRect(935, 0, 50, 20), {}, 1920, 1080);
        check(lines.size() == 1 && lines[0].at == 960 && lines[0].from == 0 && lines[0].to == 1080, "the page's centre: the whole page");

        // With the grid: an axis in reach lines up exactly, the other keeps the grid's value
        std::vector<GuideLine> out;
        DragRect r = applyGuides(makeRect(105, 305, 50, 50), makeRect(103, 307, 50, 50), CH_BODY, others, 1920, 1080, 6, true, true, out);
        check(r.x == 100 && r.y == 305, "x on the other's left, y on the grid: " + str(r));
        // An axis the pointer didn't move along isn't touched
        r = applyGuides(makeRect(105, 305, 50, 50), makeRect(103, 307, 50, 50), CH_BODY, others, 1920, 1080, 6, false, true, out);
        check(r.x == 105, "not along x");
        // Resizing from the right edge: only that edge lines up
        r = applyGuides(makeRect(200, 400, 95, 30), makeRect(200, 400, 97, 30), CH_E, others, 1920, 1080, 6, true, false, out);
        check(r.x == 200 && r.w == 100, "the right edge onto the other's right (300): " + str(r));
        r = applyGuides(makeRect(105, 400, 95, 30), makeRect(103, 400, 97, 30), CH_W, others, 1920, 1080, 6, true, false, out);
        check(r.x == 100 && r.x + r.w == 200, "the left edge onto 100, the right one where it was: " + str(r));
    });

    Test::run("guides: dragging with the mouse in both editors", [] {
        Pair p;
        check(openPair(p, "group_guides_layout"), "a page");
        LayoutSession& s = p.here;
        s.selectPath("image[1]");   // the logo, 1500,700 300x150
        s.guides = true;
        s.pointerDown(unitFit(), 1600, 750, 4.0f);
        // Its left edge 3 short of the year text's (1100) when dropped at 1103: lines up instead of the grid's 1105
        s.pointerMove(unitFit(), 1203, 640);
        check(s.dragRect().x == 1100 && !s.guideLines().empty(), "lined up with the year's left edge: " + str(s.dragRect()));
        s.pointerUp();
        check(value(s.text(), "image[1]", "x") == "1100" && s.guideLines().empty(), "dropped there");
        s.undo();
        feed(s);
        // Off (Alt held): the grid only
        s.guides = false;
        s.selectPath("image[1]");
        s.pointerDown(unitFit(), 1600, 750, 4.0f);
        s.pointerMove(unitFit(), 1203, 640);
        check(s.dragRect().x == 1105 && s.guideLines().empty(), "the grid's 1105: " + str(s.dragRect()));
        s.cancelDrag();

        // The widget editor
        const std::string root = makeRoot("group_guides_widgets");
        EditorSession w;
        check(openThree(root, w), "a theme with three texts");
        w.guides = true;
        w.select(2);   // 900,600 200x100
        w.pointerDown(unitFit(), 1000, 650, 4.0f);
        w.pointerMove(unitFit(), 603, 655);   // its left edge to 503: the second's left is 500
        check(w.dragRect().x == 500 && !w.guideLines().empty(), "the widget lines up with the other's left: " + str(w.dragRect()));
        w.pointerUp();
        check(w.page().widgets[2].x == 500, "dropped there");
        std::string note;
        check(runSessionCommand(w, parseEditorCommand("guides off"), note) == CommandResult::Ok && !w.guides, "a script turns them off");
    });
});

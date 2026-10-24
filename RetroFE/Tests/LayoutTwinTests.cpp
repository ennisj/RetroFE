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

// layout.xml and "layout 4x3.xml" edited together (ThemeEditor/LayoutTwin):
// which page is the twin, matching elements across the two, the attribute
// changes an edit made, and copying them to the other page byte for byte.

#include "TestSupport.h"
#include "../Source/ThemeEditor/LayoutSession.h"
#include "../Source/ThemeEditor/LayoutTwin.h"
#include "../Source/ThemeEditor/LayoutText.h"
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace ThemeEditor;

namespace
{
    // A 16:9 page and its 4:3 twin: the same elements, different numbers
    const std::string WIDE =
        "<layout width=\"1920\" height=\"1080\" font=\"fonts/a.ttf\">\n"
        "    <image src=\"images/bg.png\" x=\"0\" y=\"0\" width=\"1920\" height=\"1080\" layer=\"0\"/>\n"
        "    <image src=\"images/wall.png\" x=\"center\" y=\"530\" width=\"1704\" height=\"906\" xOrigin=\"center\" layer=\"9\">\n"
        "        <onEnter><set duration=\".3\"><animate type=\"alpha\" from=\"0\" to=\"1\"/></set></onEnter>\n"
        "    </image>\n"
        "    <reloadableText type=\"year\" x=\"145\" y=\"298\" fontSize=\"40\" fontColor=\"75c8f7\" layer=\"10\"/>\n"
        "    <reloadableText type=\"genre\" x=\"145\" y=\"215\" fontSize=\"40\" fontColor=\"75c8f7\" layer=\"10\"/>\n"
        "    <menu type=\"custom\">\n"
        "        <itemDefaults x=\"center\" y=\"980\" width=\"270\" height=\"300\"/>\n"
        "        <item xOffset=\"-300\"/>\n"
        "        <item xOffset=\"0\" selected=\"true\"/>\n"
        "    </menu>\n"
        "</layout>\n";
    const std::string NARROW =
        "<layout width=\"1600\" height=\"1200\" font=\"fonts/a.ttf\">\r\n"
        "  <image src=\"images/bg.png\" x=\"0\" y=\"0\" width=\"1600\" height=\"1200\" layer=\"0\"/>\r\n"
        "  <image src=\"images/wall.png\" x=\"center\" y=\"553\" width=\"1484\" height=\"952\" xOrigin=\"center\" layer=\"9\">\r\n"
        "    <onEnter><set duration=\".3\"><animate type=\"alpha\" from=\"0\" to=\"1\"/></set></onEnter>\r\n"
        "  </image>\r\n"
        "  <reloadableText type=\"year\" x=\"90\" y=\"310\" fontSize=\"40\" fontColor=\"75c8f7\" layer=\"10\"/>\r\n"
        "  <reloadableText type=\"genre\" x=\"90\" y=\"223\" fontSize=\"40\" fontColor=\"75c8f7\" layer=\"10\"/>\r\n"
        "  <menu type=\"custom\">\r\n"
        "    <itemDefaults x=\"center\" y=\"1100\" width=\"270\" height=\"300\"/>\r\n"
        "    <item xOffset=\"-250\"/>\r\n"
        "    <item xOffset=\"0\" selected=\"true\"/>\r\n"
        "  </menu>\r\n"
        "</layout>\r\n";

    LayoutDoc parse(const std::string& text)
    {
        LayoutDoc doc;
        std::string error;
        parseLayoutText(text, doc, error);
        return doc;
    }

    // `edit` applied to WIDE, mirrored onto NARROW
    MirrorResult mirror(const std::string& before, const std::string& after, const std::string& twin = NARROW)
    {
        std::vector<AttrChange> changes;
        const LayoutDoc a = parse(before), b = parse(after), t = parse(twin);
        attributeChanges(a, b, changes);
        return mirrorChanges(a, changes, t, 1920, 1080, 1600, 1200);
    }

    std::string set(const std::string& text, const std::string& path, const std::string& name, const std::string& value)
    {
        LayoutDoc doc = parse(text);
        return withAttribute(doc, doc.find(path), name, value);
    }

    std::string value(const std::string& text, const std::string& path, const std::string& name)
    {
        LayoutDoc doc = parse(text);
        const int i = doc.find(path);
        return i < 0 ? "(no element)" : doc.elements[i].value(name, "(unset)");
    }

    // The bytes that differ between two texts
    std::string changed(const std::string& before, const std::string& after)
    {
        size_t p = 0;
        while (p < before.size() && p < after.size() && before[p] == after[p]) ++p;
        size_t s = 0;
        while (s < before.size() - p && s < after.size() - p && before[before.size() - 1 - s] == after[after.size() - 1 - s]) ++s;
        return after.substr(p, after.size() - p - s);
    }
}

static Test::Suite layoutTwin("layout twin pages", [] {
    Test::run("twin pages: layout.xml and layout 4x3.xml, either way, in the same folder", [] {
        const std::vector<std::string> pages = { "layout.xml", "layout 4x3.xml", "splash.xml", "collections/NES/layout/layout.xml",
                                                 "collections/NES/layout/layout 4x3.xml", "collections/SNES/layout/layout.xml" };
        checkEqual(twinPage("layout.xml", pages), "layout 4x3.xml", "the main page's twin");
        checkEqual(twinPage("layout 4x3.xml", pages), "layout.xml", "and back");
        checkEqual(twinPage("collections/NES/layout/layout.xml", pages), "collections/NES/layout/layout 4x3.xml", "a collection's page");
        checkEqual(twinPage("collections/SNES/layout/layout.xml", pages), "", "a page without a 4:3 one");
        checkEqual(twinPage("splash.xml", pages), "", "splash has none");
        checkEqual(twinPage("layout.xml", { "layout.xml", "Layout 4X3.xml" }), "Layout 4X3.xml", "names in any case");
    });

    Test::run("twin pages: elements match by path and what they are", [] {
        const LayoutDoc a = parse(WIDE), b = parse(NARROW);
        for (size_t i = 0; i < a.elements.size(); ++i) {
            check(matchElement(a, static_cast<int>(i), b) == b.find(a.elements[i].path), "the same element: " + a.elements[i].path);
        }
        // One page has an extra element earlier: the rest are found by what they are
        const std::string extra = "<layout width=\"1600\" height=\"1200\">\n"
                                  "  <reloadableText type=\"title\" x=\"1\" y=\"1\"/>\n"
                                  "  <reloadableText type=\"genre\" x=\"90\" y=\"223\"/>\n"
                                  "  <reloadableText type=\"year\" x=\"90\" y=\"310\"/>\n"
                                  "</layout>\n";
        const LayoutDoc c = parse(extra);
        check(matchElement(a, a.find("reloadableText[0]"), c) == c.find("reloadableText[2]"), "the year text, moved down a place");
        check(matchElement(a, a.find("reloadableText[1]"), c) == c.find("reloadableText[1]"), "the genre text");
        check(matchElement(a, a.find("image[0]"), c) == -1, "an element the other page hasn't: no match");
        // Inside an event: the same step of the matched element
        check(matchElement(a, a.find("image[1]/onEnter[0]/set[0]/animate[0]"), b) == b.find("image[1]/onEnter[0]/set[0]/animate[0]"),
              "an animation follows its element");
        check(matchElement(a, a.find("image[1]/onEnter[0]/set[0]/animate[0]"), c) == -1, "and has none when its element has none");
        // Same path, different element: not the same
        const LayoutDoc d = parse("<layout><image src=\"images/other.png\"/><image src=\"images/bg.png\"/></layout>");
        check(matchElement(a, a.find("image[0]"), d) == d.find("image[1]"), "the background found by its src, not its place");
        // Several alike with different counts: can't tell
        const LayoutDoc e = parse("<layout><image src=\"images/bg.png\"/><image src=\"images/bg.png\"/></layout>");
        check(matchElement(a, a.find("image[0]"), e) == e.find("image[0]"), "same path and same src: that one");
        const LayoutDoc f = parse("<layout><text value=\"x\"/><image src=\"images/bg.png\"/><image src=\"images/bg.png\"/></layout>");
        check(matchElement(a, a.find("image[0]"), f) == f.find("image[0]"), "the first of two alike when it's at the same path");
        check(matchElement(a, 0, b) == 0, "the page is the page");

        // The pages shipped with RetroFE: Aeon Nox's layout.xml and its 4:3 page match element for element
        const fs::path nox = fs::path(RETROFE_SOURCE_DIR) / ".." / ".." / "Package" / "Environment" / "Common" / "layouts" / "Aeon Nox";
        std::error_code ec;
        if (!fs::is_regular_file(nox / "layout 4x3.xml", ec)) {
            std::cout << "  (skipped: no shipped Aeon Nox at " << nox.string() << ")" << std::endl;
            return;
        }
        const LayoutDoc wide = parse(readFile((nox / "layout.xml").string()));
        const LayoutDoc narrow = parse(readFile((nox / "layout 4x3.xml").string()));
        int matched = 0;
        for (size_t i = 0; i < wide.elements.size(); ++i) {
            const int j = matchElement(wide, static_cast<int>(i), narrow);
            if (j >= 0 && narrow.elements[j].path == wide.elements[i].path) matched++;
        }
        check(wide.elements.size() > 50 && matched == static_cast<int>(wide.elements.size()),
              "every element of Aeon Nox's page matches its 4:3 twin (" + std::to_string(matched) + " of " + std::to_string(wide.elements.size()) + ")");
    });

    Test::run("twin pages: what an edit changed", [] {
        std::vector<AttrChange> changes;
        const std::string edited = set(set(WIDE, "image[1]", "y", "560"), "reloadableText[0]", "alpha", "0.5");
        LayoutDoc a = parse(WIDE), b = parse(edited);
        check(attributeChanges(a, b, changes) && changes.size() == 2, "two changes");
        check(changes[0].path == "image[1]" && changes[0].name == "y" && changes[0].had && changes[0].before == "530" && changes[0].after == "560",
              "a changed value");
        check(changes[1].name == "alpha" && !changes[1].had && changes[1].has && changes[1].after == "0.5", "an added one");
        LayoutDoc removed = parse(withoutAttribute(a, a.find("image[1]"), "xOrigin"));
        check(attributeChanges(a, removed, changes) && changes.size() == 1 && changes[0].had && !changes[0].has, "a removed one");
        LayoutDoc fewer = parse(withoutElement(a, a.find("image[0]")));
        check(!attributeChanges(a, fewer, changes) && changes.empty(), "elements went: not attribute changes");
        check(attributeChanges(a, a, changes) && changes.empty(), "nothing changed");
    });

    Test::run("twin pages: copying an edit to the other page", [] {
        // The same value on both pages: the same new value, only those bytes
        MirrorResult r = mirror(WIDE, set(WIDE, "reloadableText[0]", "fontColor", "ff0000"));
        check(r.applied == 1 && r.skipped.empty(), "copied");
        checkEqual(changed(NARROW, r.text), "ff0000", "only the value changed there (its CRLF and indentation kept)");
        check(r.text.find("\r\n  <reloadableText type=\"year\"") != std::string::npos, "the other page's style is its own");

        // A position that differs: the same move, scaled to that page
        r = mirror(WIDE, set(WIDE, "reloadableText[0]", "y", "328"));
        checkEqual(value(r.text, "reloadableText[0]", "y"), "343", "y +30 on a 1080-high page is +33 on a 1200-high one (whole units)");
        r = mirror(WIDE, set(WIDE, "reloadableText[0]", "x", "241"));
        checkEqual(value(r.text, "reloadableText[0]", "x"), "170", "x +96 across 1920 is +80 across 1600");
        // "center" dragged sideways: resolved on each page first
        r = mirror(WIDE, set(WIDE, "image[1]", "x", "1000"));
        checkEqual(value(r.text, "image[1]", "x"), "833", "center (960) to 1000 there is center (800) + 33");
        // A size that differs
        r = mirror(WIDE, set(WIDE, "image[1]", "width", "1804"));
        checkEqual(value(r.text, "image[1]", "width"), "1567", "width +100 scaled");
        // A position set on neither page: from 0 on both
        r = mirror(WIDE, set(WIDE, "image[1]", "yOffset", "-27"));
        checkEqual(value(r.text, "image[1]", "yOffset"), "-30", "a new offset, scaled");
        // A size set on neither: in proportion to that page
        r = mirror(WIDE, set(WIDE, "reloadableText[1]", "width", "300"));
        checkEqual(value(r.text, "reloadableText[1]", "width"), "250", "a new width, scaled");

        // Something else that differs: left alone, and said
        const std::string redder = set(NARROW, "reloadableText[0]", "fontColor", "00ff00");
        r = mirror(WIDE, set(WIDE, "reloadableText[0]", "fontColor", "ff0000"), redder);
        check(r.applied == 0 && r.text == redder && r.skipped.size() == 1 && r.skipped[0].find("\"00ff00\" there") != std::string::npos,
              "a colour the other page has its own way isn't overwritten: " + (r.skipped.empty() ? "" : r.skipped[0]));
        // The page's own size never
        r = mirror(WIDE, set(WIDE, "", "width", "2560"));
        check(r.applied == 0 && r.skipped.size() == 1, "each page keeps its size");
        r = mirror(WIDE, set(WIDE, "", "font", "fonts/b.ttf"));
        checkEqual(value(r.text, "", "font"), "fonts/b.ttf", "but the page's font is copied");
        // Animations: the same value is copied, as is
        r = mirror(WIDE, set(WIDE, "image[1]/onEnter[0]/set[0]", "duration", "1"));
        checkEqual(value(r.text, "image[1]/onEnter[0]/set[0]", "duration"), "1", "a step's duration");
        // Removing
        LayoutDoc wide = parse(WIDE);
        r = mirror(WIDE, withoutAttribute(wide, wide.find("image[1]"), "xOrigin"));
        checkEqual(value(r.text, "image[1]", "xOrigin"), "(unset)", "a removed attribute goes there too");
        // Menu items: by place in the menu
        r = mirror(WIDE, set(WIDE, "menu[0]/item[0]", "xOffset", "-330"));
        checkEqual(value(r.text, "menu[0]/item[0]", "xOffset"), "-275", "an item's offset moves scaled (-250 - 25)");
        // Several at once (align): each copied
        r = mirror(WIDE, set(set(WIDE, "reloadableText[0]", "y", "215"), "reloadableText[1]", "fontSize", "44"));
        check(r.applied == 2 && value(r.text, "reloadableText[0]", "y") == "218" && value(r.text, "reloadableText[1]", "fontSize") == "44",
              "two changes, two edits there: y=" + value(r.text, "reloadableText[0]", "y"));
        // A changed src is still recognised for the next change of that element
        r = mirror(WIDE, set(set(WIDE, "image[1]", "src", "images/w2.png"), "image[1]", "layer", "8"));
        check(r.applied == 2 && value(r.text, "image[1]", "src") == "images/w2.png" && value(r.text, "image[1]", "layer") == "8",
              "src then layer");
        // Pages the same size: a position that was the same stays the same
        std::vector<AttrChange> changes;
        const LayoutDoc a = parse(WIDE), b = parse(set(WIDE, "image[1]", "y", "600"));
        attributeChanges(a, b, changes);
        MirrorResult same = mirrorChanges(a, changes, a, 1920, 1080, 1920, 1080);
        checkEqual(value(same.text, "image[1]", "y"), "600", "same size, same value");
        check(designSize(a, true, 0) == 1920 && designSize(parse("<layout width=\"stretch\"/>"), true, 7) == 7, "a page's design size");
    });

    Test::run("twin pages: two sessions edited together, undone together, saved", [] {
        // What the layout editor's screen does (syncTwin, undoBoth): an edit here, its copy
        // there as a linked step, Undo of both, and saving both pages byte for byte
        const std::string root = makeRoot("twin_sessions");
        writeFile(root + "/layouts/Wide/layout.xml", WIDE);
        writeFile(root + "/layouts/Wide/layout 4x3.xml", NARROW);
        LayoutSession here, there;
        std::string error;
        check(here.open(root, "Wide", "layout.xml", error) && there.open(root, "Wide", twinPage("layout.xml", LayoutSession::themePages(root + "/layouts/Wide")), error),
              "both pages open: " + error);
        TwinSync state;
        state.reset(here);
        auto sync = [&]() {
            MirrorResult r;
            return syncTwinPages(here, there, state, r);
        };
        auto undoBoth = [&]() {
            const unsigned link = here.undoLink();
            here.undo();
            if (link && there.undoLink() == link) there.undo();
            state.reset(here);
        };
        check(here.selectPath("reloadableText[0]"), "the year text");
        // Typing a colour, a character at a time: one step on each page
        here.setCoalesceKey("attr:fontColor");
        for (const char* typed : { "f", "ff", "ff00", "ff0000" }) {
            here.setAttribute("fontColor", typed);
            sync();
        }
        here.endCoalesce();
        check(value(there.text(), "reloadableText[0]", "fontColor") == "ff0000", "the colour on the 4:3 page");
        check(there.canUndo() && there.undoLink() == here.undoLink() && here.undoLink() != 0, "linked steps");
        // A move: y +30 here, +33.33 there
        here.setAttribute("y", "328");
        sync();
        checkEqual(value(there.text(), "reloadableText[0]", "y"), "343", "the move, scaled");
        // Adding an element isn't copied
        here.setAttribute("alpha", "0.5");
        sync();
        const std::string thereBefore = there.text();
        check(here.duplicateSelected(), "duplicated here");
        check(sync() == TwinCopy::Structure, "elements came: not copied");
        check(there.text() == thereBefore, "a duplicate stays on its page");
        undoBoth();
        check(there.text() == thereBefore, "and its undo leaves the other page alone");
        // Undo both, step by step, back to the files exactly
        undoBoth();
        check(value(there.text(), "reloadableText[0]", "alpha") == "(unset)", "alpha undone on both");
        undoBoth();
        undoBoth();
        check(here.text() == WIDE && there.text() == NARROW, "both pages back to their bytes");
        check(!here.canUndo() && !there.canUndo(), "nothing left to undo on either");
        // Redo the colour and save both
        here.redo();
        if (there.redoLink() == here.undoLink()) there.redo();
        check(here.save(error) && there.save(error), "both saved: " + error);
        const std::string saved = readFile(root + "/layouts/Wide/layout 4x3.xml");
        checkEqual(changed(NARROW, saved), "ff0000", "the 4:3 page on disk differs by the colour only");
        check(saved.find("\r\n") != std::string::npos, "with its own line endings");
        check(fs::exists(fs::path(root) / "manage" / "backups" / "layouts" / "Wide" / "layout 4x3.xml"), "its original kept, as for any page");
    });

    Test::run("twin pages: a step is copied as a whole, not edit by edit", [] {
        const std::string root = makeRoot("twin_steps");
        writeFile(root + "/layouts/Wide/layout.xml", WIDE);
        writeFile(root + "/layouts/Wide/layout 4x3.xml", NARROW);
        LayoutSession here, there;
        std::string error;
        check(here.open(root, "Wide", "layout.xml", error) && there.open(root, "Wide", "layout 4x3.xml", error), "open: " + error);
        TwinSync state;
        state.reset(here);
        MirrorResult r;
        // Typing y over (298 -> "" -> 400), copied after every key: the in-between values don't stick
        here.selectPath("reloadableText[0]");
        here.setCoalesceKey("attr:y");
        for (const char* typed : { "29", "2", "", "4", "40", "400" }) {
            here.setAttribute("y", typed);
            syncTwinPages(here, there, state, r);
        }
        here.endCoalesce();
        // +102 on a 1080-high page is +113.33 on a 1200-high one: 310 + 113 = 423
        checkEqual(value(there.text(), "reloadableText[0]", "y"), "423", "the twin gets the whole step's move");
        check(here.undoLink() != 0 && there.undoLink() == here.undoLink(), "one linked step each");
        here.undo();
        there.undo();
        check(here.text() == WIDE && there.text() == NARROW, "and one undo each takes it back");
        state.reset(here);
        // A run of nudges (one step) is scaled once, not rounded nudge by nudge
        here.selectPath("reloadableText[1]");
        here.setCoalesceKey("nudge");   // the screen's nudges coalesce into one step
        for (int i = 0; i < 30; ++i) {
            check(here.setAttribute("x", std::to_string(146 + i)), "nudge");
            syncTwinPages(here, there, state, r);
        }
        here.endCoalesce();
        // x 145 -> 175 (+30 across 1920) is +25 across 1600: 90 -> 115
        checkEqual(value(there.text(), "reloadableText[1]", "x"), "115", "the run moves the twin 25, not 30 (1 per rounded nudge)");
        // Two steps between copies are both copied
        state.reset(here);
        here.endCoalesce();
        here.setAttribute("fontSize", "44");
        here.setAttribute("layer", "11");
        check(syncTwinPages(here, there, state, r) == TwinCopy::Copied && value(there.text(), "reloadableText[1]", "fontSize") == "44" &&
              value(there.text(), "reloadableText[1]", "layer") == "11", "both changes reach the twin");
    });
});

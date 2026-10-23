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

// Tests for the theme editor core (Source/ThemeEditor), ported from One
// Saucier's editor tests and adapted to this fork's PageBuilder dialect and
// project-in-theme storage.

#include "TestSupport.h"
#include "../Source/ThemeEditor/EditorCanvas.h"
#include "../Source/ThemeEditor/EditorNav.h"
#include "../Source/ThemeEditor/LayoutCheck.h"
#include "../Source/ThemeEditor/LetterTiles.h"
#include "../Source/ThemeEditor/ThemeExport.h"
#include "../Source/ThemeEditor/ThemeGenerate.h"
#include "../Source/ThemeEditor/ThemeProject.h"
#include "../Source/ThemeEditor/ThemeStore.h"
#include "../Source/ThemeEditor/WidgetDefs.h"
#include <rapidxml.hpp>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace ThemeEditor;

namespace
{
    // A parsed XML document that keeps its buffer alive
    class XmlDoc
    {
    public:
        explicit XmlDoc(const std::string& xml) : buffer_(xml.begin(), xml.end())
        {
            buffer_.push_back('\0');
            try {
                doc_.parse<0>(buffer_.data());
                ok_ = true;
            }
            catch (const rapidxml::parse_error&) {
                ok_ = false;
            }
        }
        bool ok() const { return ok_; }
        rapidxml::xml_node<>* node(const char* name) { return ok_ ? doc_.first_node(name) : nullptr; }

    private:
        std::vector<char> buffer_;
        rapidxml::xml_document<> doc_;
        bool ok_ = false;
    };

    using Node = rapidxml::xml_node<>;

    std::string attr(const Node* n, const char* name)
    {
        const rapidxml::xml_attribute<>* a = n ? n->first_attribute(name) : nullptr;
        return a ? std::string(a->value()) : std::string();
    }

    bool hasAttr(const Node* n, const char* name) { return n && n->first_attribute(name); }

    std::vector<const Node*> children(const Node* n, const char* name)
    {
        std::vector<const Node*> out;
        for (const Node* c = n ? n->first_node(name) : nullptr; c; c = c->next_sibling(name)) out.push_back(c);
        return out;
    }

    std::string join(const std::vector<std::string>& lines)
    {
        std::string out;
        for (const auto& l : lines) out += "\n    " + l;
        return out;
    }

    std::string fontBytes()
    {
        return readFile(std::string(RETROFE_THIRD_PARTY_DIR) + "/fonts/Roboto-Regular.ttf");
    }

    WidgetLibrary stockLibrary()
    {
        WidgetLibrary lib;
        std::string error;
        buildWidgetLibrary(embeddedWidgetFiles(), lib, error);
        return lib;
    }

    const char* const PROBE_DEF =
        "# a probe widget\n"
        "[widget]\n"
        "id = probe\n"
        "name = Probe\n"
        "category = media\n"
        "color = 45A8E0\n"
        "glyph = P\n"
        "w = 100\n"
        "h = 50\n"
        "minw = 10\n"
        "minh = 10\n"
        "layer = 3\n"
        "\n"
        "[var]\n"
        "token = FLAG\n"
        "name = Flag\n"
        "type = bool\n"
        "default = true\n"
        "\n"
        "[var]\n"
        "token = TXT\n"
        "name = Text\n"
        "type = text\n"
        "default = images/a&b.png\n"
        "\n"
        "[fragment]\n"
        "<probe id=\"{{ID}}\" x=\"{{X}}\" y=\"{{Y}}\" w=\"{{W}}\" h=\"{{H}}\" cx=\"{{CX}}\" cy=\"{{CY}}\" x2=\"{{X2}}\" y2=\"{{Y2}}\" layer=\"{{LAYER}}\" src=\"{{TXT}}\" pick=\"{{FLAG:yes:no}}\">\n"
        "{{?FLAG}}ON {{/FLAG}}{{!FLAG}}OFF {{/FLAG}}{{ANIM_DEFAULTS}}\n"
        "</probe>\n"
        "\n"
        "[animDefaults]\n"
        "<anim w=\"{{W}}\"/>\n";

    WidgetInstance place(const std::string& def, const std::string& id, int x, int y, int w, int h, int layer)
    {
        WidgetInstance wi;
        wi.def = def;
        wi.id = id;
        wi.name = id;
        wi.x = x; wi.y = y; wi.w = w; wi.h = h; wi.layer = layer;
        return wi;
    }

    // One of each family plus the wheel (One Saucier's reference project)
    ThemeProject referenceProject()
    {
        ThemeProject p;
        p.name = "New Theme";
        p.pages[0].widgets.push_back(place("static_image", "static_image_1", 0, 0, 1920, 1080, 1));
        p.pages[0].widgets.back().setVar("SRC", "images/bg & co.png");
        p.pages[0].widgets.push_back(place("game_title", "game_title_1", 12, 10, 450, 102, 12));
        p.pages[0].widgets.push_back(place("game_story", "game_story_1", 12, 100, 450, 336, 8));
        p.pages[0].widgets.push_back(place("game_front", "game_front_1", 8, 282, 492, 472, 9));
        p.pages[0].widgets.push_back(place("game_video", "game_video_1", 720, 330, 624, 492, 10));
        p.pages[0].widgets.push_back(place("wheel_vertical", "wheel_vertical_1", 1560, 8, 308, 976, 14));
        return p;
    }

    // Generates a one-page project holding `w` and checks the result against PageBuilder's vocabulary
    std::vector<std::string> generateAndCheck(const WidgetLibrary& lib, const WidgetInstance& w, std::string* xmlOut = nullptr)
    {
        ThemeProject p;
        p.name = "Check";
        p.pages[0].widgets.push_back(w);
        GenResult r = generateLayoutXml(p, p.pages[0], lib);
        if (!r.ok()) return r.errors;
        if (xmlOut) *xmlOut = r.xml;
        return checkLayoutXml(r.xml);
    }

    bool pathExists(const fs::path& p)
    {
        std::error_code ec;
        return fs::exists(p, ec);
    }
}

void runThemeEditorTests()
{
    // --- widget definitions -------------------------------------------------------

    run("theme editor: widget definitions parse, refuse with reasons", [] {
        WidgetDef d;
        std::string error;
        check(parseWidgetDef(PROBE_DEF, d, error), "probe parses: " + error);
        check(d.id == "probe" && d.name == "Probe" && d.category == "media", "identity fields");
        check(d.color == "45A8E0" && d.glyph == "P", "colour and glyph");
        check(d.w == 100 && d.h == 50 && d.minW == 10 && d.minH == 10 && d.layer == 3, "geometry defaults");
        check(d.vars.size() == 2 && d.var("FLAG") && d.var("TXT"), "both vars");
        check(d.var("FLAG") && d.var("FLAG")->type == "bool" && d.var("FLAG")->def == "true", "FLAG is bool, default true");
        checkEqual(d.var("TXT") ? d.var("TXT")->def : "", "images/a&b.png", "text default kept raw");
        checkEqual(d.animDefaults, "<anim w=\"{{W}}\"/>", "animDefaults captured and trimmed");

        WidgetDef m;
        check(parseWidgetDef("[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 10\nh = 10\n[fragment]\n<t id=\"{{ID}}\"/>\n", m, error),
              "minimal def parses");
        check(m.minW == 8 && m.minH == 8 && m.layer == 0, "minw/minh/layer defaults");
        check(parseWidgetDef("[widget]\r\nid = crlf\r\nname = C\r\ncategory = text\r\ncolor = 000000\r\nw = 1\r\nh = 1\r\n[fragment]\r\n<t id=\"{{ID}}\"/>\r\n", m, error) &&
              m.fragment == "<t id=\"{{ID}}\"/>", "CRLF files parse and lose their carriage returns");

        struct Bad { const char* what; const char* text; };
        const Bad bads[] = {
            { "missing id", "[widget]\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "bad id charset", "[widget]\nid = Bad-Id\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "bad category", "[widget]\nid = a\nname = A\ncategory = wat\ncolor = 000000\nw = 1\nh = 1\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "bad colour", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 00FF0\nw = 1\nh = 1\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "missing size", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "missing fragment", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n" },
            { "fragment without ID", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[fragment]\n<t/>\n" },
            { "unknown section", "[widget]\nid = a\n[fragmnet]\nx\n" },
            { "unknown widget key", "[widget]\nid = a\nwat = 1\n" },
            { "content before a section", "id = a\n[widget]\n" },
            { "layer out of range", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\nlayer = 20\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "duplicate var token", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[var]\ntoken = T\nname = t\ntype = bool\ndefault = true\n[var]\ntoken = T\nname = t\ntype = bool\ndefault = true\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "bad var type", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[var]\ntoken = T\nname = t\ntype = int\ndefault = 1\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "bad bool default", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[var]\ntoken = T\nname = t\ntype = bool\ndefault = yes\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "file var without filter", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[var]\ntoken = F\nname = f\ntype = file\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "options on a bool", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[var]\ntoken = T\nname = t\ntype = bool\ndefault = true\noptions = a|b\n[fragment]\n<t id=\"{{ID}}\"/>\n" },
            { "menu without items", "[widget]\nid = a\nname = A\ncategory = menu\ncolor = 000000\nw = 1\nh = 1\nmenu = true\n[fragment]\n<menu id=\"{{ID}}\"/>\n" },
            { "items without menu", "[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[fragment]\n<t id=\"{{ID}}\">{{MENU_ITEMS}}</t>\n" },
            { "menu category clash", "[widget]\nid = a\nname = A\ncategory = media\ncolor = 000000\nw = 1\nh = 1\nmenu = true\n[fragment]\n<menu id=\"{{ID}}\">{{MENU_ITEMS}}</menu>\n" },
        };
        for (const auto& b : bads) {
            WidgetDef x;
            std::string e;
            check(!parseWidgetDef(b.text, x, e) && !e.empty(), std::string("refused with a reason: ") + b.what);
        }

        WidgetLibrary lib;
        WidgetFiles files = { { "p.widget", PROBE_DEF },
                              { "q.widget", "[widget]\nid = q\nname = Q\ncategory = text\ncolor = 000000\nw = 1\nh = 1\n[fragment]\n<t id=\"{{ID}}\"/>\n" } };
        check(buildWidgetLibrary(files, lib, error), "two-file library builds");
        check(lib.defs.size() == 2 && lib.defs[0].id == "probe" && lib.defs[1].id == "q", "library keeps file order");
        check(lib.find("q") && !lib.find("zz"), "library lookup");
        files.push_back({ "dup.widget", PROBE_DEF });
        check(!buildWidgetLibrary(files, lib, error) && error.find("dup.widget") != std::string::npos,
              "a duplicate id is refused, naming the file");

        // Presets take the base's fragment and vars; a default-only entry overrides the default
        WidgetFiles presets = { { "p.widget", PROBE_DEF },
                                { "r.widget", "[widget]\nid = r\nbase = probe\nname = R\ncategory = media\ncolor = 000000\nw = 5\nh = 5\n[var]\ntoken = TXT\ndefault = other.png\n" } };
        check(buildWidgetLibrary(presets, lib, error), "a preset resolves: " + error);
        const WidgetDef* r = lib.find("r");
        check(r && r->fragment == lib.find("probe")->fragment, "the preset carries its base's fragment");
        check(r && r->var("TXT") && r->var("TXT")->def == "other.png" && r->var("TXT")->type == "text",
              "the preset overrides just the default");
        presets[1].second = "[widget]\nid = r\nbase = nope\nname = R\ncategory = media\ncolor = 000000\nw = 5\nh = 5\n";
        check(!buildWidgetLibrary(presets, lib, error), "a preset of a missing base is refused");

        // Folder widgets lay over the stock ones by file name
        WidgetFiles overlay = overlayWidgetFiles({ { "a.widget", "A" }, { "b.widget", "B" } }, { { "b.widget", "B2" }, { "0.widget", "Z" } });
        check(overlay.size() == 3 && overlay[0].first == "0.widget" && overlay[2].second == "B2",
              "overlay replaces by name, adds the rest, sorted");

        check(widgetColorValid("aB12f0") && !widgetColorValid("12345") && !widgetColorValid("#12345f") && !widgetColorValid("12345g"),
              "colour validation edges");
        check(widgetIdentValid("a_2") && !widgetIdentValid("") && !widgetIdentValid("A") && !widgetIdentValid("a-b"),
              "identifier validation edges");
    });

    run("theme editor: template expansion", [] {
        WidgetDef d;
        std::string error;
        parseWidgetDef(PROBE_DEF, d, error);
        WidgetInstance w = place("probe", "probe_1", 10, 20, 100, 50, 3);
        GenOptions opt;
        ExpandResult r = expandFragment(d, w, opt);
        check(r.ok(), "default expansion resolves every site");
        check(r.text.find("id=\"probe_1\"") != std::string::npos, "{{ID}}");
        check(r.text.find("x=\"10\" y=\"20\" w=\"100\" h=\"50\"") != std::string::npos, "{{X}} {{Y}} {{W}} {{H}}");
        check(r.text.find("cx=\"60\" cy=\"45\" x2=\"110\" y2=\"70\"") != std::string::npos, "derived CX CY X2 Y2");
        check(r.text.find("src=\"images/a&amp;b.png\"") != std::string::npos, "text vars are escaped");
        check(r.text.find("pick=\"yes\"") != std::string::npos, "a bool branch takes the true arm");
        check(r.text.find("ON ") != std::string::npos && r.text.find("OFF ") == std::string::npos, "?gate kept, !gate dropped");
        check(r.text.find("<anim w=\"100\"/>") != std::string::npos, "ANIM_DEFAULTS spliced and expanded");

        w.setVar("FLAG", "false");
        r = expandFragment(d, w, opt);
        check(r.ok() && r.text.find("pick=\"no\"") != std::string::npos, "the override takes the false arm");
        check(r.text.find("OFF ") != std::string::npos && r.text.find("ON ") == std::string::npos, "!gate kept, ?gate dropped");
        w.setVar("TXT", "x<y\".png");
        r = expandFragment(d, w, opt);
        check(r.text.find("src=\"x&lt;y&quot;.png\"") != std::string::npos, "an override is escaped");
        opt.includeAnims = false;
        r = expandFragment(d, w, opt);
        check(r.ok() && r.text.find("<anim") == std::string::npos, "includeAnims off drops the block");

        opt = GenOptions();
        check(!expandText("a {{NOPE}} b", d, w, opt).ok(), "an unknown token is reported");
        checkEqual(expandText("a {{NOPE}} b", d, w, opt).text, "a {{NOPE}} b", "...and left in place");
        check(!expandText("{{TXT:a:b}}", d, w, opt).ok(), "a branch on a non-bool is reported");
        check(!expandText("{{?FLAG}}x", d, w, opt).ok(), "an unclosed gate is reported");
        check(!expandText("x {{/FLAG}}", d, w, opt).ok(), "a stray close is reported");
        check(!expandText("x {{OOPS", d, w, opt).ok(), "an unterminated site is reported");
        checkEqual(expandText("{{?FLAG}}hidden{{/FLAG}}ok", d, w, opt).text, "ok", "a false ?gate skips its body");
        checkEqual(expandText("{{!FLAG}}shown{{/FLAG}}ok", d, w, opt).text, "shownok", "a false !gate keeps its body");
        checkEqual(expandText("{{FS}}", d, w, opt).text, "36", "FS is 72% of the box height");
        checkEqual(expandText("{{?WARP}}warped{{/WARP}}flat", d, w, opt).text, "flat", "WARP is always false here");
        w.alphaPct = 60;
        checkEqual(expandText("{{ALPHA}}", d, w, opt).text, "0.60", "a 60% widget writes alpha 0.60");
        opt.themeDir = "My & Theme";
        checkEqual(expandText("{{THEMEDIR}}", d, w, opt).text, "My &amp; Theme", "THEMEDIR is the folder, escaped");
    });

    // --- project model -------------------------------------------------------------------

    run("theme editor: project.xml round-trips", [] {
        ThemeProject p;
        check(p.pages.size() == 1 && p.pages[0].collection.empty(), "a new project has the Default page");
        check(p.canvasW() == 1920 && p.canvasH() == 1080, "horizontal canvas 1920x1080");
        p.vertical = true;
        check(p.canvasW() == 1080 && p.canvasH() == 1920, "vertical canvas 1080x1920");
        p.authorW = 768;
        p.authorH = 1366;
        check(p.canvasW() == 768 && p.canvasH() == 1366, "an author space overrides the canvas");

        p.name = "A & \"B\" <C>";
        p.soundSelect = "sounds/select.wav";
        p.extra.emplace_back("artSource", "Q:/base_assets/layouts/Pacman");   // One Saucier's, kept as it was
        WidgetInstance w = place("game_video", "game_video_1", 720, 330, 624, 492, 10);
        w.name = "Game Video 1";
        w.autoScroll = false;
        w.alphaPct = 60;
        w.menuIdleDimPct = 30;
        w.warp = true;
        w.wx[0] = 10; w.wy[0] = 20; w.wx[1] = 600; w.wy[1] = 30; w.wx[2] = 20; w.wy[2] = 470; w.wx[3] = 640; w.wy[3] = 480;
        w.setVar("LOOP", "false");
        w.extra.emplace_back("future", "x");
        p.pages[0].widgets.push_back(w);
        ThemePage arcade;
        arcade.collection = "Arcade & Friends";
        arcade.tplName = "ARCADES";
        arcade.members = { "Neo Geo", "CPS2" };
        WidgetInstance t = place("game_title", "game_title_1", 12, 10, 450, 102, 12);
        t.name = "Game <Title> 1";
        t.font = "assets/Oswald-Bold.ttf";
        t.fontColor = "ffffff";
        arcade.widgets.push_back(t);
        ThemePage mon;
        mon.collection = arcade.collection;   // a second screen rides its page's collection
        mon.monW = 1280;
        mon.monH = 720;
        mon.widgets.push_back(place("game_front", "game_front_1", 0, 0, 1280, 720, 2));
        arcade.mons.push_back(mon);
        p.pages.push_back(arcade);

        const std::string xml = projectSerialize(p);
        ThemeProject q;
        std::string error;
        check(projectParse(xml, q, error), "the serialized project parses: " + error);
        check(q.name == p.name && q.vertical && q.authorW == 768 && q.authorH == 1366, "name, orientation and author space");
        checkEqual(q.soundSelect, "sounds/select.wav", "a theme sound");
        check(q.extra.size() == 1 && q.extra[0].first == "artSource", "an unknown root attribute is kept");
        check(q.pages.size() == 2 && q.pages[1].collection == "Arcade & Friends" && q.pages[1].tplName == "ARCADES" &&
              q.pages[1].members.size() == 2, "pages, template name and members");
        const WidgetInstance* rw = q.pages[0].findWidget("game_video_1");
        check(rw && rw->x == 720 && rw->y == 330 && rw->w == 624 && rw->h == 492 && rw->layer == 10, "geometry");
        check(rw && rw->varValue("LOOP", "true") == "false" && !rw->autoScroll && rw->alphaPct == 60 && rw->menuIdleDimPct == 30,
              "vars, editor flags, opacity and idle dim");
        check(rw && rw->warp && rw->wx[0] == 10 && rw->wy[3] == 480, "warp corners");
        check(rw && rw->extra.size() == 1 && rw->extra[0].first == "future", "an unknown widget attribute is kept");
        const WidgetInstance* rt = q.pages[1].findWidget("game_title_1");
        check(rt && rt->name == "Game <Title> 1" && rt->font == "assets/Oswald-Bold.ttf" && rt->fontColor == "ffffff",
              "names with XML specials, font and colour");
        check(rt && rt->autoScroll && rt->alphaPct == 100 && rt->menuIdleDimPct == -1, "absent attributes take their defaults");
        check(q.pages[1].mons.size() == 1 && q.pages[1].mons[0].monW == 1280 && q.pages[1].mons[0].widgets.size() == 1,
              "a second-screen page survives");
        checkEqual(projectSerialize(q), xml, "the round-trip is byte-stable");

        ThemeProject x;
        check(!projectParse("<notAProject/>", x, error), "no themeProject root is refused");
        check(!projectParse("<themeProject version=\"2\" name=\"n\" orientation=\"horizontal\"/>", x, error) &&
              error.find("newer") != std::string::npos, "a newer version is refused");
        check(!projectParse("<themeProject version=\"1\" name=\"n\" orientation=\"diagonal\"/>", x, error), "an unknown orientation is refused");
        check(!projectParse("<themeProject version=\"1\" name=\"n\" orientation=\"horizontal\"><page collection=\"\"><widget id=\"a_1\"/></page></themeProject>", x, error),
              "a widget without def is refused");
        check(!projectParse("<themeProject version=\"1\" name=\"n\" orientation=\"horizontal\"><page collection=\"\"><widget def=\"d\" id=\"a_1\"/><widget def=\"d\" id=\"a_1\"/></page></themeProject>", x, error),
              "duplicate widget ids are refused");
        check(!projectParse(xml.substr(0, xml.size() / 2), x, error),
              "a truncated project.xml fails to load (was: loaded with pages missing)");
        check(projectParse("<themeProject version=\"1\" name=\"n\" orientation=\"horizontal\"><page collection=\"Arcade\"/></themeProject>", x, error) &&
              x.pages.size() == 2 && x.pages[0].collection.empty() && x.pages[1].collection == "Arcade",
              "a missing Default page is repaired at the front");
        check(projectParse("<themeProject version=\"1\" name=\"n\" orientation=\"horizontal\"><page collection=\"A\"/><page collection=\"\"/><page collection=\"B\"/></themeProject>", x, error) &&
              x.pages.size() == 3 && x.pages[0].collection.empty() && x.pages[1].collection == "A" && x.pages[2].collection == "B",
              "a misplaced Default page is hoisted, the rest keep their order");
        // A One Saucier project with its deferred-asset manifest still loads
        check(projectParse("<themeProject version=\"1\" name=\"n\" orientation=\"horizontal\"><asset rel=\"assets/a.png\" src=\"x\"/><page collection=\"\"/></themeProject>", x, error),
              "a One Saucier asset manifest is ignored, not refused");

        ThemePage pg;
        std::string id, name;
        nextIdentity(pg, "game_video", "Game Video", id, name);
        check(id == "game_video_1" && name == "Game Video 1", "the first placement is number 1");
        pg.widgets.push_back(place("game_video", "game_video_1", 0, 0, 1, 1, 0));
        pg.widgets.push_back(place("game_video", "game_video_3", 0, 0, 1, 1, 0));
        pg.widgets.push_back(place("game_title", "game_title_9", 0, 0, 1, 1, 0));
        nextIdentity(pg, "game_video", "Game Video", id, name);
        check(id == "game_video_4" && name == "Game Video 4", "one past the highest, other definitions ignored");

        checkEqual(editorSlug("New Theme 1"), "new-theme-1", "slug: spaces to dashes, lower case");
        checkEqual(editorSlug("A--B  C!!"), "a-b-c", "slug: runs collapse");
        checkEqual(editorSlug("!!!"), "theme", "slug: never empty");
        checkEqual(themeFolderName("New Theme 1"), "New Theme 1", "folder: a clean name passes");
        checkEqual(themeFolderName("A/B:C*D?E\"F<G>H|I\\J"), "A B C D E F G H I J", "folder: forbidden characters become spaces");
        checkEqual(themeFolderName("  Trim Me.  "), "Trim Me", "folder: edges and trailing dots trim");
        checkEqual(themeFolderName(".."), "Theme", "folder: never .. or empty");
        checkEqual(themeFolderName("con"), "con Theme", "folder: device names are made safe");
    });

    run("theme editor: orientation reflow", [] {
        ThemeProject rp = referenceProject();
        const WidgetInstance before = rp.pages[0].widgets[1];
        reflowOrientation(rp, true);
        check(rp.canvasW() == 1080 && rp.canvasH() == 1920, "the canvas flips");
        const WidgetInstance& a = rp.pages[0].widgets[1];
        check(a.x == (before.x * 1080 + 960) / 1920 && a.y == (before.y * 1920 + 540) / 1080, "positions scale per axis");
        check(a.w == (before.w * 1080 + 960) / 1920 && a.h == (before.h * 1920 + 540) / 1080, "sizes scale per axis");
        reflowOrientation(rp, true);
        check(rp.canvasW() == 1080, "reflowing to the same orientation is a no-op");
    });

    run("theme editor: collection templates", [] {
        auto uses = [](ThemeProject& p, const std::string& c) -> std::string {
            const ThemePage* pg = p.coveredBy(c);
            return (pg && !pg->collection.empty()) ? pg->displayName() : "Default";
        };
        ThemeProject p;
        ThemePage comp;
        comp.collection = "Amstrad CPC";
        comp.tplName = "COMPUTERS";
        comp.members = { "Commodore 64", "Atari ST" };
        p.pages.push_back(comp);
        ThemePage cons;
        cons.collection = "NES";
        cons.tplName = "CONSOLES";
        cons.members = { "SNES" };
        p.pages.push_back(cons);
        checkEqual(uses(p, "Commodore 64"), "COMPUTERS", "a member uses its template");
        checkEqual(uses(p, "Xyz"), "Default", "an unassigned collection uses Default");
        reassign(p, "Commodore 64", "NES");
        check(uses(p, "Commodore 64") == "CONSOLES" && uses(p, "Atari ST") == "COMPUTERS", "a member moves, its sibling stays");
        reassign(p, "Amstrad CPC", "NES");
        check(uses(p, "Amstrad CPC") == "CONSOLES" && uses(p, "Atari ST") == "COMPUTERS", "the primary moves; the template re-anchors");
        const std::string gone = reassign(p, "Atari ST", "NES");
        check(!gone.empty() && uses(p, "Atari ST") == "CONSOLES", "moving the last collection reports the removed template");
        bool compGone = true;
        for (const auto& pg : p.pages) {
            if (pg.tplName == "COMPUTERS") compGone = false;
        }
        check(compGone, "an emptied template is removed");
        reassign(p, "SNES", "");
        checkEqual(uses(p, "SNES"), "Default", "reassigning to Default detaches");

        std::string erased;
        ThemePage* nt = createNamed(p, "Sega Genesis", "RETRO", "NES", &erased);
        check(nt && nt->tplName == "RETRO" && nt->collection == "Sega Genesis" && nt->widgets.size() == p.page("NES")->widgets.size(),
              "create-named seeds from the chosen template");

        // Re-creating a memberless template's own collection erases its page
        // first; the seed must be copied before that
        ThemeProject s;
        ThemePage a;
        a.collection = "AA";
        a.tplName = "ALPHA";
        a.widgets.push_back(place("d", "a0", 0, 0, 1, 1, 0));
        s.pages.push_back(a);
        ThemePage b;
        b.collection = "BB";
        b.tplName = "BETA";
        for (int i = 0; i < 3; ++i) b.widgets.push_back(place("d", "b" + std::to_string(i), 0, 0, 1, 1, 0));
        s.pages.push_back(b);
        ThemePage* n1 = createNamed(s, "AA", "ALPHA2", "BB", &erased);
        check(erased == "AA" && n1 && n1->widgets.size() == 3 && n1->widgets[0].id == "b0", "a seed stored after the erased page arrives intact");
        ThemePage* n2 = createNamed(s, "AA", "ALPHA3", "AA", &erased);
        check(n2 && n2->widgets.size() == 3, "seeding from the page being replaced copies it first");

        ThemeProject q;
        ThemePage qa;
        qa.collection = "AA";
        qa.tplName = "CONSOLES";
        qa.members = { "BB", "CC" };
        q.pages.push_back(qa);
        ThemePage qd;
        qd.collection = "DD";
        qd.tplName = "RETRO";
        q.pages.push_back(qd);
        deleteTemplateTo(q, "AA", "DD");
        check(uses(q, "AA") == "RETRO" && uses(q, "BB") == "RETRO" && uses(q, "CC") == "RETRO", "delete-to moves every collection");

        ThemeProject t = referenceProject();
        ThemePage* tp = createTemplate(t, "Arcade");
        check(tp && tp->collection == "Arcade" && tp->widgets.size() == t.pages[0].widgets.size(), "a template seeds as a copy of Default");
        check(createTemplate(t, "Arcade") == t.page("Arcade") && t.pages.size() == 2, "creating an existing template answers it");
        check(createTemplate(t, "Blank", nullptr)->widgets.empty(), "a null seed makes a blank template");
        check(deleteTemplate(t, "Blank") && !deleteTemplate(t, "") && !deleteTemplate(t, "Nope"), "delete: templates only");
        check(attachCollection(t, "Arcade", "Neo Geo") && t.coveredBy("Neo Geo") == t.page("Arcade"), "attach a member");
        check(!attachCollection(t, "", "X") && !attachCollection(t, "Arcade", "Arcade"), "never to Default, never a collection with its own page");
        check(detachCollection(t, "Neo Geo") && !t.coveredBy("Neo Geo"), "detach a member");

        // The effective template inherits down the menu tree
        std::map<std::string, std::string> parent = { { "Main", "" }, { "1 ARCADES", "Main" }, { "2 ARCADE COLLECTIONS", "1 ARCADES" },
                                                      { "Data East Classics", "2 ARCADE COLLECTIONS" }, { "NES", "Main" } };
        auto parentOf = [&parent](const std::string& c) { auto it = parent.find(c); return it == parent.end() ? std::string() : it->second; };
        ThemeProject e;
        ThemePage arc;
        arc.collection = "2 ARCADE COLLECTIONS";
        arc.tplName = "ARCADE COLLECTIONS";
        e.pages.push_back(arc);
        bool inherited = false;
        const ThemePage* c1 = effectiveCover(e, "Data East Classics", parentOf, &inherited);
        check(c1 && c1->displayName() == "ARCADE COLLECTIONS" && inherited, "a child without its own page inherits its ancestor's");
        check(!effectiveCover(e, "NES", parentOf, &inherited) && !inherited, "an uncovered branch is Default");
        std::map<std::string, std::string> loop = { { "A", "B" }, { "B", "A" } };
        check(!effectiveCover(e, "A", [&loop](const std::string& c) { return loop[c]; }), "a parent cycle ends");
    });

    // --- the stock library and the generator -------------------------------------------------

    run("theme editor: stock widget library", [] {
        WidgetLibrary lib;
        std::string error;
        const WidgetFiles files = embeddedWidgetFiles();
        const bool built = buildWidgetLibrary(files, lib, error);
        check(built, "the stock library builds: " + error);
        check(files.size() == 35 && lib.defs.size() == 35, "35 stock widgets");
        if (!built || lib.defs.size() != 35) return;
        const char* const expected[] = {
            "art_strip", "game_art", "game_bezel", "game_cabinet", "game_favorite", "game_front", "game_genre", "game_index",
            "game_letters", "game_logo", "game_manufacturer", "game_marquee", "game_meta", "game_players", "game_screentitle",
            "game_story", "game_title", "game_video", "game_year", "live_background", "playlist_name", "scroll_text",
            "splash_status", "static_image", "static_text", "static_video", "system_device", "system_logo", "system_marquee",
            "system_name", "system_time", "system_video", "text_scroller", "wheel_curved", "wheel_vertical",
        };
        for (const char* id : expected) check(lib.find(id) != nullptr, std::string("stock widget present: ") + id);
        int menus = 0;
        for (const auto& d : lib.defs) {
            if (d.menu) ++menus;
            check(!d.warpable, d.id + " doesn't offer Transform Video (no transform= here)");
            check(!d.info.empty(), d.id + " carries its read-only attribute line");
            for (const auto& v : d.vars) {
                check(v.def != "layout_preferred", d.id + "." + v.token + " doesn't default to layout_preferred");
                for (const auto& o : v.options) check(o != "layout_preferred", d.id + "." + v.token + " doesn't offer layout_preferred");
            }
        }
        check(menus == 3, "three menu widgets (vertical, curved, strip)");
        check(lib.find("art_strip")->strip && lib.find("wheel_curved")->arc == 40, "the strip and the curve declare themselves");
        check(lib.find("game_video")->w == 555 && lib.find("game_video")->h == 535, "survey default sizes kept");

        GenOptions opt;
        for (const auto& d : lib.defs) {
            if (d.menu) continue;
            WidgetInstance w = place(d.id, d.id + "_1", 0, 0, d.w, d.h, d.layer);
            ExpandResult r = expandFragment(d, w, opt);
            check(r.ok(), d.id + ": default expansion resolves every site");
            check(r.text.find("id=\"" + w.id + "\"") != std::string::npos, d.id + ": carries the instance id");
        }
        WidgetInstance w = place("system_logo", "sys", 0, 0, 400, 200, 5);
        ExpandResult r = expandFragment(*lib.find("system_logo"), w, opt);
        check(r.text.find("mode=\"system\"") != std::string::npos && r.text.find("type=\"logo\"") != std::string::npos,
              "System Logo writes type=logo mode=system");
        GenOptions inTheme;
        inTheme.themeDir = "My Theme";
        r = expandFragment(*lib.find("text_scroller"), w, inTheme);
        check(r.text.find("<reloadableScrollingText") != std::string::npos && r.text.find("type=\"file\"") != std::string::npos &&
              r.text.find("location=\"layouts/My Theme/text/scroller.txt\"") != std::string::npos,
              "Text File Scroller is a type=file scrolling text located in the theme folder");
    });

    run("theme editor: generator output", [] {
        WidgetLibrary lib = stockLibrary();

        // A golden page
        {
            ThemeProject g;
            g.name = "Golden";
            g.pages[0].widgets.push_back(place("game_title", "game_title_1", 12, 10, 450, 102, 12));
            GenResult r = generateLayoutXml(g, g.pages[0], lib);
            checkEqual(r.xml,
                       "<!-- RetroFE theme editor; project=golden; v=1 -->\n"
                       "<layout width=\"1920\" height=\"1080\" font=\"fonts/Roboto-Regular.ttf\" loadFontSize=\"48\" fontColor=\"E6E7EB\">\n"
                       "  <reloadableText id=\"game_title_1\" type=\"title\" x=\"12\" xOrigin=\"left\" y=\"61\" yOrigin=\"center\" fontSize=\"73\" maxHeight=\"102\" maxWidth=\"450\" textFormat=\"uppercase\" menuScrollReload=\"true\" alpha=\"1\" layer=\"12\">\n"
                       "    <onEnter><set duration=\"0.25\"><animate type=\"alpha\" from=\"0\" to=\"1\" algorithm=\"easeoutquadratic\"/></set></onEnter>\n"
                       "  </reloadableText>\n"
                       "</layout>\n",
                       "golden page");
            checkEqual(menuItemsXml(0, 0, 300, 900, 5, true, 0, 1920, 3),
                       "<itemDefaults x=\"150\" xOrigin=\"center\" yOrigin=\"center\" width=\"195\" maxHeight=\"300\" fontSize=\"120\" layer=\"5\"/>\n"
                       "<item y=\"150\"/>\n"
                       "<item y=\"450\" width=\"300\" maxHeight=\"500\" selected=\"true\"/>\n"
                       "<item y=\"750\"/>\n",
                       "golden wheel slots");
        }

        ThemeProject p = referenceProject();
        GenResult r = generateLayoutXml(p, p.pages[0], lib);
        check(r.ok(), "the reference project generates");
        check(r.warnings.empty(), "...without warnings" + join(r.warnings));
        check(r.xml.rfind("<!-- RetroFE theme editor; project=new-theme; v=1 -->\n", 0) == 0, "the marker is line one");
        XmlDoc doc(r.xml);
        Node* layout = doc.node("layout");
        check(layout != nullptr, "it parses with rapidxml to a <layout> root");
        check(attr(layout, "width") == "1920" && attr(layout, "height") == "1080", "the root carries the canvas size");
        check(attr(layout, "font") == "fonts/Roboto-Regular.ttf" && attr(layout, "loadFontSize") == "48", "the root carries the bundled font");
        check(children(layout, "image").size() == 1 && children(layout, "reloadableText").size() == 1 &&
              children(layout, "reloadableScrollingText").size() == 1 && children(layout, "reloadableImage").size() == 1 &&
              children(layout, "reloadableVideo").size() == 1 && children(layout, "menu").size() == 1,
              "one component per widget");
        Node* menu = layout ? layout->first_node("menu") : nullptr;
        check(menu && menu->first_node("itemDefaults"), "the menu has itemDefaults");
        int items = 0, selected = 0;
        for (const Node* it : children(menu, "item")) {
            ++items;
            if (attr(it, "selected") == "true") ++selected;
        }
        check(items == MENU_ITEM_SLOTS && selected == 1, "nine slots, one selected");
        check(attr(menu, "textFallback") == "true" && attr(menu, "mode").empty(), "wheel text fallback on, no mode");
        checkEqual(attr(layout ? layout->first_node("image") : nullptr, "src"), "images/bg & co.png", "static image src round-trips escaping");
        Node* video = layout ? layout->first_node("reloadableVideo") : nullptr;
        check(attr(video, "maxHeight") == "492" && !hasAttr(video, "height") && !hasAttr(video, "numLoops") &&
              !hasAttr(video, "hwdec") && attr(video, "mode").empty(), "video: aspect fit, no numLoops/hwdec, the game's own media");

        GenOptions noText;
        noText.includeText = false;
        GenResult rt = generateLayoutXml(p, p.pages[0], lib, noText);
        check(rt.xml.find("reloadableText") == std::string::npos && rt.xml.find("<menu") != std::string::npos, "text off drops text widgets");
        GenOptions noMedia;
        noMedia.includeMedia = false;
        GenResult rm = generateLayoutXml(p, p.pages[0], lib, noMedia);
        check(rm.xml.find("<image") == std::string::npos && rm.xml.find("<menu") == std::string::npos &&
              rm.xml.find("reloadableText") != std::string::npos, "media off drops static/media/menu");
        GenOptions noAnims;
        noAnims.includeAnims = false;
        check(generateLayoutXml(p, p.pages[0], lib, noAnims).xml.find("<onEnter") == std::string::npos, "anims off drops them");

        ThemeProject v = referenceProject();
        v.vertical = true;
        XmlDoc vdoc(generateLayoutXml(v, v.pages[0], lib).xml);
        check(attr(vdoc.node("layout"), "width") == "1080" && attr(vdoc.node("layout"), "height") == "1920", "vertical: 1080x1920 root");

        ThemeProject two = referenceProject();
        two.pages[0].widgets.push_back(place("wheel_vertical", "wheel_vertical_2", 0, 0, 300, 900, 14));
        check(generateLayoutXml(two, two.pages[0], lib).ok(), "two wheels generate");

        ThemeProject esc = referenceProject();
        esc.pages[0].widgets[5].setVar("WHEELFONTSIZE", "18");
        esc.pages[0].widgets[5].setVar("WHEELFONTCOLOR", "00FF00\" evil=\"1");
        esc.pages[0].widgets[5].setVar("WHEELSELCOLOR", "a&b<c");
        GenResult re = generateLayoutXml(esc, esc.pages[0], lib);
        check(re.ok() && re.xml.find("evil=\"1\"") == std::string::npos && re.xml.find("a&amp;b&lt;c") != std::string::npos,
              "hostile wheel text colours are escaped");
        check(XmlDoc(re.xml).ok(), "...and the page is still well-formed");

        ThemeProject bad = referenceProject();
        bad.pages[0].widgets[0].def = "no_such_widget";
        GenResult rb = generateLayoutXml(bad, bad.pages[0], lib);
        check(!rb.ok() && rb.errors[0].find("unknown definition") != std::string::npos && rb.xml.empty(), "an unknown definition is refused");
        ThemeProject dup = referenceProject();
        dup.pages[0].widgets[1].id = "static_image_1";
        check(!generateLayoutXml(dup, dup.pages[0], lib).ok(), "a duplicate id is refused");
        ThemeProject deg = referenceProject();
        deg.pages[0].widgets[2].w = 0;
        check(!generateLayoutXml(deg, deg.pages[0], lib).ok(), "a degenerate size is refused");
        WidgetLibrary lib2 = lib;
        WidgetDef bogus;
        std::string error;
        parseWidgetDef("[widget]\nid = bogus\nname = B\ncategory = text\ncolor = 000000\nw = 10\nh = 10\n[fragment]\n<text id=\"{{ID}}\" value=\"{{NOT_A_TOKEN}}\"/>\n", bogus, error);
        lib2.defs.push_back(bogus);
        ThemeProject bp;
        bp.pages[0].widgets.push_back(place("bogus", "bogus_1", 0, 0, 10, 10, 0));
        GenResult rbog = generateLayoutXml(bp, bp.pages[0], lib2);
        check(!rbog.ok() && rbog.errors[0].find("{{NOT_A_TOKEN}}") != std::string::npos, "an unresolved site is refused, named");

        ThemeProject wp = referenceProject();
        wp.pages[0].widgets[1].layer = 25;
        GenResult rw = generateLayoutXml(wp, wp.pages[0], lib);
        XmlDoc wdoc(rw.xml);
        check(rw.ok() && !rw.warnings.empty() && attr(wdoc.node("layout") ? wdoc.node("layout")->first_node("reloadableText") : nullptr, "layer") == "19",
              "layer 25 is written as 19 (the last layer drawn), with a warning");
        ThemeProject op = referenceProject();
        op.pages[0].widgets[1].x = 5000;
        rw = generateLayoutXml(op, op.pages[0], lib);
        check(rw.ok() && !rw.warnings.empty() && rw.warnings[0].find("outside") != std::string::npos, "an off-canvas widget warns");
    });

    run("theme editor: menu shapes", [] {
        XmlDoc arc("<m>" + menuItemsXml(100, 0, 300, 900, 14, true, 20) + "</m>");
        std::vector<const Node*> its = children(arc.node("m"), "item");
        Node* defaults = arc.node("m") ? arc.node("m")->first_node("itemDefaults") : nullptr;
        check(its.size() == static_cast<size_t>(MENU_ITEM_SLOTS), "an arc wheel keeps the slot count");
        check(hasAttr(defaults, "maxHeight") && !hasAttr(defaults, "height") && attr(defaults, "width") == "195",
              "resting slots: 65% width + maxHeight, never a stretch");
        check(its.size() == 9 && attr(its[4], "width") == "300" && attr(its[4], "x") == std::to_string(250 + 60), "the centre slot is full width and bulges");
        XmlDoc right("<m>" + menuItemsXml(1500, 0, 300, 900, 14, true, 20, 1920) + "</m>");
        std::vector<const Node*> rits = children(right.node("m"), "item");
        check(rits.size() == 9 && attr(rits[4], "x") == std::to_string(1650 - 60), "a right-edge wheel bulges toward the centre");
        auto count = [](const std::string& s) {
            int n = 0;
            for (size_t at = 0; (at = s.find("<item ", at)) != std::string::npos; at += 6) ++n;
            return n;
        };
        check(count(menuItemsXml(0, 0, 300, 900, 5, true, 0, 1920, 6)) == 7, "an even count rounds up to keep a centre");
        check(count(menuItemsXml(0, 0, 300, 900, 5, true, 0, 1920, 99)) == MENU_SLOTS_MAX, "the count clamps");
        const std::string faded = menuItemsXml(0, 0, 300, 900, 5, true, 0, 1920, 5, 100);
        int alphas = 0;
        for (size_t at = 0; (at = faded.find("alpha=\"", at)) != std::string::npos; at += 7) ++alphas;
        check(alphas == 2 && faded.find("alpha=\"0.00\"") != std::string::npos, "edge fade: only the two end slots");
        XmlDoc tilt("<m>" + menuItemsXml(0, 0, 400, 1300, 18, true, 0, 1920, 13, 0, 230, 39) + "</m>");
        std::vector<const Node*> tits = children(tilt.node("m"), "item");
        check(tits.size() == 13 && attr(tits.front(), "angle") == "39" && attr(tits.back(), "angle") == "-39" && attr(tits[6], "angle").empty(),
              "tilt ramps to the ends, never the centre");
        check(tits.size() == 13 && (std::atoi(attr(tits[7], "y").c_str()) - std::atoi(attr(tits[6], "y").c_str())) >
                                       (std::atoi(attr(tits[1], "y").c_str()) - std::atoi(attr(tits[0], "y").c_str())),
              "a tilted wheel foreshortens toward the ends");
        XmlDoc strip("<m>" + menuItemsXml(200, 700, 1400, 300, 9, false) + "</m>");
        Node* sd = strip.node("m") ? strip.node("m")->first_node("itemDefaults") : nullptr;
        check(attr(sd, "y") == "850" && hasAttr(sd, "height") && hasAttr(sd, "maxWidth") && !hasAttr(sd, "width"),
              "a horizontal strip: height + maxWidth on the row centre");
        const std::string ribbon = menuStripItemsXml(260, 700, 1400, 266, 6);
        check(count(ribbon) == 9 && ribbon.find("alpha=\"0\" selected=\"true\"") != std::string::npos, "an art strip hides its selected slot");
    });

    // --- the dialect audit -----------------------------------------------------------------

    run("theme editor: every stock widget writes only what PageBuilder reads", [] {
        WidgetLibrary lib = stockLibrary();
        std::string allXml;
        for (const auto& d : lib.defs) {
            WidgetInstance base = place(d.id, d.id + "_1", 100, 100, d.w, d.h, d.layer);
            std::vector<WidgetInstance> variants = { base };
            // Every bool both ways and every listed option, so gated branches are covered
            for (const auto& v : d.vars) {
                if (v.type == "bool") {
                    WidgetInstance w = base;
                    w.setVar(v.token, v.def == "true" ? "false" : "true");
                    variants.push_back(w);
                }
                for (const auto& o : v.options) {
                    WidgetInstance w = base;
                    w.setVar(v.token, o);
                    variants.push_back(w);
                }
            }
            WidgetInstance styled = base;
            styled.font = "assets/Oswald.ttf";
            styled.fontColor = "FF0000";
            styled.alphaPct = 50;
            styled.menuAccelMs = 300;
            variants.push_back(styled);
            for (size_t i = 0; i < variants.size(); ++i) {
                std::string xml;
                const std::vector<std::string> issues = generateAndCheck(lib, variants[i], &xml);
                check(issues.empty(), d.id + " variant " + std::to_string(i) + " checks clean:" + join(issues));
            }
        }

        // A page holding all 35, laid out on a grid
        ThemeProject all;
        all.name = "Every Widget";
        int i = 0;
        for (const auto& d : lib.defs) {
            all.pages[0].widgets.push_back(place(d.id, d.id + "_1", (i % 7) * 270, (i / 7) * 210, d.w, d.h, d.layer));
            ++i;
        }
        GenResult r = generateLayoutXml(all, all.pages[0], lib);
        check(r.ok(), "a page with every widget generates" + join(r.errors));
        const std::vector<std::string> issues = checkLayoutXml(r.xml);
        check(issues.empty(), "a page with every widget checks clean:" + join(issues));
        XmlDoc doc(r.xml);
        int components = 0;
        for (const Node* c = doc.node("layout") ? doc.node("layout")->first_node() : nullptr; c; c = c->next_sibling()) ++components;
        check(components == 35, "...with one component per widget");

        // Wheels with every shape knob
        WidgetInstance wheel = place("wheel_curved", "wheel_curved_1", 1500, -100, 360, 1280, 14);
        wheel.menuSlots = 13;
        wheel.menuFadePct = 100;
        wheel.menuCurvePx = 120;
        wheel.menuAngleDeg = 30;
        wheel.menuPerspDeg = 35;
        wheel.menuItemH = 120;
        wheel.menuPulse = true;
        wheel.menuIdleDimPct = 30;
        wheel.menuGhost = true;
        wheel.menuGhostMode = "system";
        wheel.menuScrollMs = 300;
        wheel.menuAccelMs = 200;
        std::string xml;
        std::vector<std::string> wi = generateAndCheck(lib, wheel, &xml);
        check(wi.empty(), "a wheel with pulse, ghost, idle dim, fade, tilt and curve checks clean:" + join(wi));
        check(xml.find("wheel_curved_1_ghost") != std::string::npos && xml.find("mode=\"system\"") == std::string::npos,
              "the ghost wheel is written, without a menu mode this engine ignores");
        WidgetInstance textWheel = place("wheel_vertical", "wheel_vertical_1", 0, 0, 300, 900, 14);
        textWheel.setVar("WHEELFONTSIZE", "18");
        textWheel.setVar("WHEELFONTCOLOR", "00FF00");
        textWheel.setVar("WHEELSELCOLOR", "000000");
        wi = generateAndCheck(lib, textWheel);
        check(wi.empty(), "a text wheel checks clean:" + join(wi));
        WidgetInstance ribbon = place("art_strip", "art_strip_1", 260, 700, 1400, 266, 6);
        ribbon.stripPitchPx = 300;
        ribbon.stripDimPct = 40;
        ribbon.stripSelW = 350;
        ribbon.stripSelH = 400;
        wi = generateAndCheck(lib, ribbon);
        check(wi.empty(), "a measured linear strip checks clean:" + join(wi));
        WidgetInstance ladder = place("art_strip", "art_strip_2", 0, 0, 1400, 266, 6);
        ladder.stripItemDefsRaw = "x=\"960\" y=\"800\" width=\"200\" layer=\"6\"";
        ladder.stripItemsRaw = "xOffset=\"-200\" alpha=\"0\"|selected=\"true\"|xOffset=\"200\"";
        ladder.stripItemKidsRaw = "||<onMenuExit><set duration=\"0.2\"><animate type=\"alpha\" to=\"0\"/></set></onMenuExit>";
        ladder.menuIdleDimPct = 30;
        wi = generateAndCheck(lib, ladder);
        check(wi.empty(), "a verbatim ladder checks clean:" + join(wi));
    });

    run("theme editor: One Saucier projects are translated, with warnings", [] {
        WidgetLibrary lib = stockLibrary();
        ThemeProject p;
        p.name = "Imported";
        WidgetInstance video = place("game_video", "game_video_1", 0, 0, 300, 400, 10);
        video.warp = true;
        video.wx[0] = 78; video.wy[0] = 500;
        video.wx[1] = 420; video.wy[1] = 514;
        video.wx[2] = 80; video.wy[2] = 1063;
        video.wx[3] = 424; video.wy[3] = 1052;
        video.setVar("ARTMODE", "layout_preferred");
        video.setVar("HWDEC", "true");   // a var this widget no longer has: ignored
        p.pages[0].widgets.push_back(video);
        GenResult r = generateLayoutXml(p, p.pages[0], lib);
        check(r.ok(), "the project generates");
        check(r.xml.find("transform=") == std::string::npos && r.xml.find("layout_preferred") == std::string::npos &&
              r.xml.find("hwdec") == std::string::npos, "no transform, layout_preferred or hwdec in the output");
        XmlDoc doc(r.xml);
        Node* v = doc.node("layout") ? doc.node("layout")->first_node("reloadableVideo") : nullptr;
        check(attr(v, "x") == std::to_string(78 + 346 / 2) && attr(v, "y") == std::to_string(500 + 563 / 2) &&
              attr(v, "width") == "346" && attr(v, "maxHeight") == "563",
              "a warped widget is drawn as its corners' bounding box");
        int warp = 0, mode = 0;
        for (const auto& w : r.warnings) {
            if (w.find("warped") != std::string::npos) ++warp;
            if (w.find("layout_preferred") != std::string::npos) ++mode;
        }
        check(warp == 1 && mode == 1, "each translation is reported once");
        check(checkLayoutXml(r.xml).empty(), "the translated page checks clean");

        // The editor still recognises One Saucier's marker
        check(isEditorMarker("<!-- one_saucier theme editor; project=x; v=1 -->"), "One Saucier's marker is editor-made");
        check(isEditorMarker("\xEF\xBB\xBF<!-- RetroFE theme editor; project=x; v=1 -->"), "a BOM before the marker is fine");
        check(!isEditorMarker("<layout width=\"1920\" height=\"1080\">") && !isEditorMarker(""), "anything else isn't");
    });

    run("theme editor: the layout check catches what PageBuilder ignores", [] {
        auto issuesFor = [](const std::string& body) {
            return checkLayoutXml("<layout width=\"1920\" height=\"1080\">" + body + "</layout>");
        };
        check(checkLayoutXml("<layout width=\"1\"").size() == 1, "malformed XML");
        check(checkLayoutXml("<page/>").size() == 1, "no <layout> root");
        check(checkLayoutXml("<layout/>").size() == 1, "no width/height");
        check(issuesFor("<scrollingText src=\"a.txt\"/>").size() == 1, "<scrollingText> isn't built");
        check(issuesFor("<reloadablePanningImage type=\"logo\"/>").size() == 1, "<reloadablePanningImage> isn't built");
        check(issuesFor("<reloadableVideo imageType=\"logo\" hwdec=\"yes\" minShowTime=\"5\" numLoops=\"0\"/>").size() == 3,
              "hwdec, minShowTime and numLoops on reloadableVideo are ignored");
        check(issuesFor("<image src=\"a.png\" transform=\"(0, 0), (1, 1); (0, 0), (1, 0), (0, 1), (1, 1); 50\"/>").size() == 1,
              "transform= is ignored");
        check(issuesFor("<reloadableImage type=\"logo\" mode=\"layout_preferred\"/>").size() == 1, "layout_preferred isn't a mode");
        check(issuesFor("<menu mode=\"system\"><itemDefaults/></menu>").size() == 1, "menus don't read system mode");
        check(issuesFor("<menu/>").size() == 1, "a menu without itemDefaults");
        check(issuesFor("<image/>").size() == 1 && issuesFor("<text/>").size() == 1 && issuesFor("<reloadableText/>").size() == 1,
              "missing src, value, type");
        check(issuesFor("<reloadableScrollingText type=\"file\"/>").size() == 1, "type=file without a location");
        check(issuesFor("<sound type=\"boom\" src=\"a.wav\"/>").size() == 1, "an unknown sound type");
        check(issuesFor("<image src=\"a.png\"><onEnter><set><animate type=\"alpha\" to=\"1\"/></set></onEnter></image>").size() == 1,
              "a set without duration");
        check(issuesFor("<image src=\"a.png\"><onEnter><set duration=\"1\"><animate type=\"wobble\" to=\"1\"/></set></onEnter></image>").size() == 1,
              "an unknown tween");
        check(issuesFor("<image src=\"a.png\"><onEnter><set duration=\"1\"><animate type=\"alpha\"/></set></onEnter></image>").size() == 1,
              "an animate without to");
        check(issuesFor("<image src=\"a.png\"><onEnter><set duration=\"1\"><animate type=\"alpha\" to=\"1\" algorithm=\"easeinoutquintic\"/></set></onEnter></image>").size() == 1,
              "an algorithm the engine doesn't know (it spells it easeonoutquintic)");
        check(issuesFor("<image src=\"a.png\"><onWobble/></image>").size() == 1, "an unknown event");
        check(issuesFor("<image src=\"a.png\" x=\"1\" fontColor=\"FFFFFF\" font=\"a.ttf\"><onMenuIdle menuIndex=\"i\"><set duration=\"1\"><animate type=\"nop\"/></set></onMenuIdle></image>").empty(),
              "a well-formed component is clean");
        check(issuesFor("<form/><keyboard/>").empty(), "forms and keyboards aren't the editor's business");
    });

    // A "still mentioned" check, not proof PageBuilder reads a name on that
    // element: it catches a name PageBuilder stops reading. It needs the
    // source tree, so a binary run elsewhere skips it.
    run("theme editor: the layout check's vocabulary is still PageBuilder's", [] {
        const std::string source = RETROFE_SOURCE_DIR;
        const std::string pageBuilder = readFile(source + "/Graphics/PageBuilder.cpp");
        const std::string tween = readFile(source + "/Graphics/Animate/Tween.cpp");
        const std::string pageH = readFile(source + "/Graphics/Page.h");
        if (pageBuilder.empty() || tween.empty() || pageH.empty()) {
            std::cout << "  skipped: the RetroFE sources aren't at " << source << std::endl;
            return;
        }
        const LayoutVocabulary v = layoutVocabulary();
        for (const auto& lists : { &v.elements, &v.attributes, &v.events, &v.values }) {
            for (const auto& name : *lists) {
                check(pageBuilder.find("\"" + name + "\"") != std::string::npos, "PageBuilder.cpp mentions \"" + name + "\"");
            }
        }
        for (const auto& name : v.tweens) {
            check(std::regex_search(tween, std::regex("\\{\\s*\"" + name + "\"\\s*,")), "Tween.cpp maps \"" + name + "\"");
        }
        check(v.attributes.size() > 80 && v.events.size() == 34 && v.tweens.size() == 44, "the vocabulary is complete");
        check(std::regex_search(pageH, std::regex("NUM_LAYERS\\s*=\\s*" + std::to_string(MAX_LAYER + 1) + "\\s*;")),
              "Page.h draws NUM_LAYERS = MAX_LAYER + 1 layers");
    });

    // --- letter tiles --------------------------------------------------------------------------

    run("theme editor: letter tiles", [] {
        const std::string ttf = fontBytes();
        check(!ttf.empty(), "the bundled font is readable");
        std::vector<std::pair<std::string, std::string>> tiles;
        std::string error;
        check(renderLetterTiles(ttf, LetterTileStyle(), tiles, error), "tiles render: " + error);
        check(tiles.size() == 38, "38 tiles: # A..Z 0..9 default");
        check(tiles.size() == 38 && tiles[0].first == "#.png" && tiles[1].first == "A.png" && tiles[26].first == "Z.png" &&
              tiles[27].first == "0.png" && tiles[36].first == "9.png" && tiles[37].first == "default.png", "tile names");
        check(tiles.size() == 38 && tiles[27].second == tiles[0].second, "digits highlight the # row");
        check(tiles.size() == 38 && tiles[37].second != tiles[0].second, "default.png highlights nothing");
        bool allPng = true, crcs = true;
        for (const auto& t : tiles) {
            const std::string& png = t.second;
            if (png.size() < 100 || png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) != 0) { allPng = false; continue; }
            // Every chunk: length, type, data, CRC over type + data; IEND last
            size_t at = 8;
            std::string last;
            while (at + 12 <= png.size()) {
                auto u8 = [&png](size_t i) { return static_cast<uLong>(static_cast<unsigned char>(png[i])); };
                const uLong len = (u8(at) << 24) | (u8(at + 1) << 16) | (u8(at + 2) << 8) | u8(at + 3);
                if (at + 12 + len > png.size()) { crcs = false; break; }
                const uLong stored = (u8(at + 8 + len) << 24) | (u8(at + 9 + len) << 16) | (u8(at + 10 + len) << 8) | u8(at + 11 + len);
                const uLong crc = crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(png.data() + at + 4), static_cast<uInt>(len + 4));
                if (crc != stored) crcs = false;
                last = png.substr(at + 4, 4);
                at += 12 + len;
            }
            if (at != png.size() || last != "IEND") crcs = false;
        }
        check(allPng, "every tile is a PNG");
        check(crcs, "every chunk's CRC is right and the file ends with IEND");
        if (!tiles.empty()) {
            const std::string& png = tiles[1].second;
            auto be32 = [&png](size_t at) {
                return (static_cast<unsigned>(static_cast<unsigned char>(png[at])) << 24) | (static_cast<unsigned>(static_cast<unsigned char>(png[at + 1])) << 16) |
                       (static_cast<unsigned>(static_cast<unsigned char>(png[at + 2])) << 8) | static_cast<unsigned>(static_cast<unsigned char>(png[at + 3]));
            };
            check(png.compare(12, 4, "IHDR") == 0 && be32(16) == 54 && be32(20) == 1080, "IHDR says 54x1080");
            // Inflate the image data and find the highlight colour
            const size_t idat = png.find("IDAT");
            const unsigned len = be32(idat - 4);
            std::vector<unsigned char> raw(static_cast<size_t>(54 * 4 + 1) * 1080);
            uLongf rawLen = static_cast<uLongf>(raw.size());
            const int z = uncompress(raw.data(), &rawLen, reinterpret_cast<const Bytef*>(png.data() + idat + 4), len);
            check(z == Z_OK && rawLen == raw.size(), "the image data inflates to 1080 rows of 54 RGBA pixels");
            bool gold = false;
            for (size_t row = 0; row < 80 && !gold; ++row) {           // the A row is near the top
                for (size_t px = 0; px < 54; ++px) {
                    const unsigned char* p = &raw[row * (54 * 4 + 1) + 1 + px * 4];
                    if (p[0] == 0xe7 && p[1] == 0xc8 && p[2] == 0x4c && p[3] > 0) { gold = true; break; }
                }
            }
            check(gold, "A.png highlights A");
        }
        std::vector<std::pair<std::string, std::string>> again;
        check(renderLetterTiles(ttf, LetterTileStyle(), again, error) && again.size() == tiles.size() && tiles.size() > 4 &&
              again[4].second == tiles[4].second, "rendering is deterministic");
        check(!renderLetterTiles("junk", LetterTileStyle(), tiles, error) && !error.empty(), "junk font bytes are refused");
        check(encodePng(nullptr, 1, 1).empty(), "encodePng refuses no image");
    });

    // --- canvas and controller decisions ---------------------------------------------------------

    run("theme editor: canvas geometry", [] {
        auto approx = [](double a, double b) { return std::fabs(a - b) < 1e-3; };
        CanvasFit f = canvasFit(2000.0f, 900.0f, 1920, 1080);
        check(f.ok && approx(f.scale, 900.0 / 1080.0) && approx(f.h, 900.0) && approx(f.w, 1600.0) && approx(f.x, 200.0), "a wide stage pillarboxes");
        f = canvasFit(960.0f, 900.0f, 1920, 1080);
        check(f.ok && approx(f.scale, 0.5) && approx(f.x, 0.0) && approx(f.h, 540.0) && approx(f.y, 180.0), "a tall stage letterboxes");
        check(!canvasFit(0, 100, 1920, 1080).ok && !canvasFit(100, 100, 0, 1080).ok, "degenerate inputs aren't ok");
        check(approx(canvasToThemeX(f, themeToCanvasX(f, 777.0f)), 777.0) && approx(canvasToThemeY(f, themeToCanvasY(f, 333.0f)), 333.0),
              "transforms round-trip");

        ThemePage pg;
        pg.widgets.push_back(place("d", "back", 0, 0, 1920, 1080, 1));
        pg.widgets.push_back(place("d", "mid", 100, 100, 400, 300, 9));
        pg.widgets.push_back(place("d", "top", 200, 150, 400, 300, 12));
        pg.widgets.push_back(place("d", "tie", 220, 170, 100, 100, 12));
        check(canvasPick(pg, 1800, 1000) == 0 && canvasPick(pg, 120, 120) == 1 && canvasPick(pg, 450, 300) == 2 &&
              canvasPick(pg, 250, 200) == 3 && canvasPick(pg, -50, -50) == -1, "pick: highest layer, then the later widget");
        check(canvasPickNext(pg, 250, 200, 3) == 2 && canvasPickNext(pg, 250, 200, 2) == 1 && canvasPickNext(pg, 250, 200, 0) == 3,
              "click-through visits the stack and wraps");
        pg.widgets[3].canvasHide = true;
        check(canvasPick(pg, 250, 200) == 2, "a canvas-hidden widget doesn't pick");
        pg.widgets[0].warp = true;
        pg.widgets[0].wx[0] = 3000; pg.widgets[0].wy[0] = 3000; pg.widgets[0].wx[1] = 3100; pg.widgets[0].wy[1] = 3000;
        pg.widgets[0].wx[2] = 3000; pg.widgets[0].wy[2] = 3100; pg.widgets[0].wx[3] = 3100; pg.widgets[0].wy[3] = 3100;
        check(canvasPick(pg, 3050, 3050) == 0, "a warped widget hit-tests its corners");

        WidgetDef d;
        d.w = 624;
        d.h = 492;
        int x, y, w, h;
        canvasPlaceCenter(d, 1920, 1080, x, y, w, h);
        check(w == 624 && h == 492 && x == (1920 - 624) / 2 && y == (1080 - 492) / 2, "placement centres the default size");
        d.w = 3000;
        d.h = 2000;
        canvasPlaceCenter(d, 1080, 1920, x, y, w, h);
        check(w == 1080 && h == 1920 && x == 0 && y == 0, "an oversized default shrinks to the canvas");

        check(canvasSnap(12, 5) == 10 && canvasSnap(13, 5) == 15 && canvasSnap(-12, 5) == -10 && canvasSnap(7, 1) == 7, "snap");
        CanvasFit unit = canvasFit(1920.0f, 1080.0f, 1920, 1080);
        DragRect r;
        r.x = 100; r.y = 100; r.w = 200; r.h = 100;
        check(canvasHandleAt(unit, r, 100, 100, 7) == CH_NW && canvasHandleAt(unit, r, 200, 98, 7) == CH_N &&
              canvasHandleAt(unit, r, 300, 200, 7) == CH_SE && canvasHandleAt(unit, r, 180, 140, 7) == CH_BODY &&
              canvasHandleAt(unit, r, 500, 500, 7) == CH_NONE && canvasHandleAt(CanvasFit(), r, 100, 100, 7) == CH_NONE,
              "handle hit-testing");
        DragRect m = canvasMove(r, 33.0f, -217.0f, 5, 1920, 1080);
        check(m.x == 135 && m.y == 0 && m.w == 200 && m.h == 100, "move snaps and clamps at the top");
        m = canvasMove(r, 5000.0f, 5000.0f, 5, 1920, 1080);
        check(m.x == 1720 && m.y == 980, "move clamps at the far corner");
        DragRect big;
        big.x = 0; big.y = -200; big.w = 300; big.h = 1480;
        m = canvasMove(big, 0.0f, 0.0f, 5, 1920, 1080);
        check(m.y == -200 && m.h == 1480, "a zero move never yanks an oversized wheel");
        DragRect rr = canvasResize(r, CH_SE, 47.0f, 23.0f, 8, 8, 5, 1920, 1080);
        check(rr.x == 100 && rr.y == 100 && rr.w == 245 && rr.h == 125, "SE resize");
        rr = canvasResize(r, CH_NW, 50.0f, 30.0f, 8, 8, 5, 1920, 1080);
        check(rr.x == 150 && rr.y == 130 && rr.x + rr.w == 300 && rr.y + rr.h == 200, "NW resize anchors the bottom right");
        rr = canvasResize(r, CH_E, -500.0f, 0.0f, 40, 8, 5, 1920, 1080);
        check(rr.x == 100 && rr.w == 40, "a collapse floors at the minimum");
        rr = canvasResize(r, CH_SE, 5000.0f, 5000.0f, 8, 8, 5, 1920, 1080);
        check(rr.x + rr.w == 1920 && rr.y + rr.h == 1080, "growth stops at the canvas");
        DragRect edge;
        edge.x = 1900; edge.y = 100; edge.w = 40; edge.h = 40;
        rr = canvasResize(edge, CH_E, 5000.0f, 0.0f, 40, 8, 5, 1920, 1080);
        check(rr.w == 40 && rr.x == 1900, "the minimum outranks the canvas clamp");
        rr = canvasResize(r, CH_BODY, 50.0f, 50.0f, 8, 8, 5, 1920, 1080);
        check(rr.x == r.x && rr.w == r.w, "the body isn't a resize handle");
    });

    run("theme editor: controller decisions", [] {
        ThemePage pg;
        pg.widgets.push_back(place("d", "left", 100, 100, 200, 200, 5));
        pg.widgets.push_back(place("d", "right", 700, 120, 200, 200, 5));
        pg.widgets.push_back(place("d", "below", 120, 700, 200, 200, 5));
        check(canvasNeighbor(ThemePage(), 0, 1, 0) == -1 && canvasNeighbor(pg, -1, 1, 0) == 0, "empty page / first pick");
        check(canvasNeighbor(pg, 0, 1, 0) == 1 && canvasNeighbor(pg, 0, 0, 1) == 2 && canvasNeighbor(pg, 1, -1, 0) == 0 &&
              canvasNeighbor(pg, 2, 0, -1) == 0, "directional steps");
        check(canvasNeighbor(pg, 1, 1, 0) == 1 && canvasNeighbor(pg, 2, 1, 0) == 1, "nothing that way stays; straight ahead wins");

        DragRect big;
        big.x = 0; big.y = -200; big.w = 300; big.h = 1480;
        DragRect n = canvasNudgeMove(big, 1, 0, 5, 1920, 1080);
        check(n.x == 5 && n.y == -200 && n.h == 1480, "a nudge never yanks an oversized wheel");
        n = canvasNudgeResize(big, 1, 0, 5, 8, 8, 1920, 1080);
        check(n.w == 305 && n.h == 1480, "a resize nudge keeps an overhang on the other axis");
        DragRect small;
        small.x = 1900; small.y = 0; small.w = 10; small.h = 10;
        check(canvasNudgeResize(small, 10, 0, 5, 8, 8, 1920, 1080).w == 20, "a resize nudge stops at the canvas");

        check(toolbarRowStep(ETB_UNDO, 1, true) == ETB_PREV && toolbarRowStep(ETB_LABELS, 1, true) == ETB_TRASH &&
              toolbarRowStep(ETB_TRASH, -1, true) == ETB_WIRE && toolbarRowStep(ETB_UNDO, -1, true) == -1 &&
              toolbarRowStep(ETB_UNDO, 1, false) == -1, "two-row toolbar steps");
        EdOverlays o;
        check(editorInputOwner(o) == EIO_EDITOR, "no overlay: the editor");
        o.tplHub = true;
        o.osk = true;
        check(editorInputOwner(o) == EIO_OSK, "the keyboard over the template hub gets the press");
        check(ladderCommitIndex(true, 3) == -1 && ladderCommitIndex(false, 3) == 3 && ladderCommitIndex(false, -1) == LADDER_COMMIT_DROP,
              "a rung edit whose rung went away is dropped");
        check(editTargetIndex(2, 3) == 2 && editTargetIndex(3, 3) == -1, "an edit lands where it began");
        const std::vector<std::string> list = { "A", "B", "C" };
        check(indexExcluding(list, std::string("A"), std::string("C")) == 1 && indexExcluding(list, std::string("B"), std::string("B")) == -1,
              "index in a filtered list");
        unsigned rgb = 0;
        check(hexRgb("E6E7EB", rgb) && rgb == 0xE6E7EB && hexRgb("#ff000080", rgb) && rgb == 0xFF0000 && !hexRgb("12345", rgb) &&
              !hexRgb("GGGGGG", rgb), "hex colours, either case");
        check(panelRowCount(4) == 7 && panelRowVar(EPR_VAR0 + 2) == 2, "panel rows");
        check(ladderAttrEffective(false, 0, false, 0, false, 0, true) == 1.0 && ladderAttrEffective(false, 0, false, 0, true, 400, false) == 400.0,
              "a rung row starts from what it shows");

        auto kids = [](const std::string& c) -> std::vector<std::string> {
            if (c == "Main") return { "Arcade", "Consoles" };
            if (c == "Arcade") return { "Fighters" };
            if (c == "Loop") return { "Loop" };
            return {};
        };
        std::vector<std::string> names;
        std::vector<int> depths;
        collectionTreeFlatten("Main", kids, names, depths);
        check(names.size() == 4 && names[1] == "Arcade" && depths[2] == 2 && names[3] == "Consoles", "pre-order tree flatten");
        names.clear();
        depths.clear();
        collectionTreeFlatten("Loop", kids, names, depths);
        check(names.size() == 1, "a menu cycle lists once");
    });

    // --- saving -------------------------------------------------------------------------------

    run("theme editor: the save plan", [] {
        WidgetLibrary lib = stockLibrary();
        ThemeProject p = referenceProject();
        p.name = "Plan Probe";
        createTemplate(p, "Arcade");
        p.page("Arcade")->members.push_back("Arcade Twin");
        createTemplate(p, ThemeProject::splashPage(), nullptr)->widgets.push_back(place("splash_status", "splash_status_1", 510, 980, 900, 60, 3));
        SaveInputs in;
        in.fontBytes = fontBytes();
        SavePlan plan = buildSavePlan(p, lib, in);
        check(plan.ok(), "the plan builds" + join(plan.errors));
        check(plan.warnings.empty(), "...without warnings" + join(plan.warnings));
        std::map<std::string, std::string> files;
        for (const auto& f : plan.files) files[f.relPath] = f.content;
        check(!plan.files.empty() && plan.files[0].relPath == "editor/project.xml", "project.xml comes first");
        check(files.count("layout.xml") && files.count("collections/Arcade/layout/layout.xml") &&
              files.count("collections/Arcade Twin/layout/layout.xml") && files.count("splash.xml"),
              "Default, template, member and splash pages");
        check(files["collections/Arcade Twin/layout/layout.xml"] == files["collections/Arcade/layout/layout.xml"],
              "a member gets its template's page");
        check(files["layout.xml"].find("src=\"images/bg &amp; co.png\"") != std::string::npos &&
              files["collections/Arcade/layout/layout.xml"].find("src=\"../../../images/bg &amp; co.png\"") != std::string::npos,
              "theme files are written relative to each page's folder");
        bool marked = true;
        for (const auto& f : files) {
            if (isPagePath(f.first) && !isEditorMarker(f.second.substr(0, f.second.find('\n')))) marked = false;
        }
        check(marked, "every page starts with the marker");
        check(files.count(BUNDLED_FONT) && files[BUNDLED_FONT] == in.fontBytes, "the bundled font is written");
        check(files.count(BUNDLED_FONT_LICENSE) && files[BUNDLED_FONT_LICENSE].find("Apache License") != std::string::npos,
              "...with its licence");
        check(files.count("collections/_common/medium_artwork/firstLetter/A.png") == 0, "no letter tiles without Game Letters");

        ThemeProject letters = p;
        letters.pages[0].widgets.push_back(place("game_letters", "game_letters_1", 1840, 60, 78, 960, 15));
        SavePlan lp = buildSavePlan(letters, lib, in);
        int tiles = 0;
        for (const auto& f : lp.files) {
            if (f.relPath.rfind(std::string(LETTER_TILES_FOLDER) + "/", 0) == 0) ++tiles;
        }
        check(lp.ok() && tiles == 38, "Game Letters brings its 38 tiles");
        SavePlan nofont = buildSavePlan(letters, lib, SaveInputs());
        check(nofont.ok() && nofont.warnings.size() == 2, "no font: two warnings (font, letters), not a refusal");

        ThemeProject custom = p;
        custom.font = "assets/Oswald.ttf";
        SavePlan cp = buildSavePlan(custom, lib, in);
        bool font = false;
        for (const auto& f : cp.files) {
            if (f.relPath == BUNDLED_FONT) font = true;
        }
        check(!font, "a theme with its own font doesn't get Roboto");

        ThemeProject hostile = p;
        hostile.pages[1].collection = "A/B";
        check(!buildSavePlan(hostile, lib, in).ok(), "a path-shaped collection refuses the plan");
        ThemeProject mons = p;
        mons.pages[1].mons.push_back(ThemePage());
        SavePlan mp = buildSavePlan(mons, lib, in);
        check(mp.ok() && mp.warnings.size() == 1 && mp.warnings[0].find("second-screen") != std::string::npos,
              "second-screen pages are reported, not saved");
        ThemeProject unchecked = p;
        unchecked.pages[0].widgets[0].setVar("SRC", "a.png\" transform=\"x");   // can't inject: it's escaped
        check(buildSavePlan(unchecked, lib, in).warnings.empty(), "an escaped value can't add an attribute");

        check(classifyFirstLine("") == TF_FRESH && classifyFirstLine("<!-- one_saucier theme editor; project=x; v=1 -->") == TF_EDITOR &&
              classifyFirstLine("<layout>") == TF_FOREIGN, "first-line verdicts");
        check(isPagePath("layout.xml") && isPagePath("splash.xml") && isPagePath("layout1.xml") &&
              isPagePath("collections/Arcade/layout/layout.xml") && isPagePath("collections/Sega Genesis/layout/layout1.xml"),
              "page positions");
        check(!isPagePath("collections/Arcade/layout/other.xml") && !isPagePath("collections/Arcade/layout.xml") &&
              !isPagePath("collections/A/B/layout/layout.xml") && !isPagePath("collections/../layout/layout.xml") &&
              !isPagePath("collections//layout/layout.xml") && !isPagePath(BUNDLED_FONT) &&
              !isPagePath("collections/_common/medium_artwork/firstLetter/A.png"), "not page positions");
        const std::vector<std::string> planPaths = { "editor/project.xml", "layout.xml", "collections/Arcade/layout/layout.xml" };
        const std::vector<std::string> onDisk = { "layout.xml", "layout1.xml", "splash.xml", "collections/Arcade/layout/layout.xml",
                                                  "collections/NES/layout/layout.xml", "collections/NES/layout/layout1.xml" };
        const std::vector<std::string> stale = stalePages(planPaths, onDisk);
        check(stale.size() == 4 && std::find(stale.begin(), stale.end(), "collections/NES/layout/layout.xml") != stale.end() &&
              std::find(stale.begin(), stale.end(), "collections/Arcade/layout/layout.xml") == stale.end(),
              "stale pages: what the plan no longer writes");
        check(stalePages({}, onDisk).size() == 5, "layout.xml is never stale");
        check(stalePages(planPaths, { "collections/NES/medium_artwork/video/x.mp4", "../x" }).empty(), "only page positions are stale");
    });

    run("theme editor: themes on disk", [] {
        WidgetLibrary lib = stockLibrary();
        const std::string root = makeRoot("theme_editor_layouts");
        SaveInputs in;
        in.fontBytes = fontBytes();
        std::string error;

        check(listThemes(root).empty() && listThemes(root + "/missing").empty(), "an empty or missing layouts folder lists nothing");
        checkEqual(nextThemeName(listThemes(root)), "New Theme 1", "the first new name");

        // A hand-made theme the editor must never touch
        writeFile(root + "/Aeon Nox/layout.xml", "<layout width=\"1920\" height=\"1080\"/>\n");
        writeFile(root + "/Aeon Nox/images/bg.png", "PNG");
        check(probeTheme(root, "Aeon Nox") == TF_FOREIGN, "a hand-made theme probes foreign");

        std::string folder;
        check(createTheme(root, "  My Theme  ", false, lib, in, folder, error), "create: " + error);
        checkEqual(folder, "My Theme", "the folder is the trimmed name");
        const fs::path dir = fs::path(root) / folder;
        check(pathExists(dir / "layout.xml") && pathExists(dir / "editor/project.xml") && pathExists(dir / BUNDLED_FONT) &&
              pathExists(dir / BUNDLED_FONT_LICENSE), "layout, project, font and licence are written");
        check(probeTheme(root, folder) == TF_EDITOR, "it probes as editor-made");
        std::string second;
        check(createTheme(root, "My Theme", true, lib, in, second, error) && second == "My Theme 2", "a second one gets a free folder");
        ThemeProject vp;
        check(loadTheme(root, second, vp, error) && vp.vertical && vp.name == "My Theme", "vertical, named as asked");
        XmlDoc vdoc(readFile((fs::path(root) / second / "layout.xml").string()));
        check(attr(vdoc.node("layout"), "width") == "1080" && attr(vdoc.node("layout"), "height") == "1920", "a vertical theme is 1080x1920");

        // Load, edit, save
        ThemeProject p;
        check(loadTheme(root, folder, p, error), "load: " + error);
        p.pages[0].widgets.push_back(place("game_title", "game_title_1", 12, 10, 450, 102, 12));
        p.pages[0].widgets.push_back(place("text_scroller", "text_scroller_1", 12, 900, 555, 44, 18));
        createTemplate(p, "Arcade");
        createTemplate(p, "NES");
        SaveReport report;
        check(saveTheme(root, folder, p, lib, in, &report, error), "save: " + error);
        check(pathExists(dir / "collections/Arcade/layout/layout.xml") && pathExists(dir / "collections/NES/layout/layout.xml"), "template pages written");
        check(readFile((dir / "layout.xml").string()).find("location=\"layouts/My Theme/text/scroller.txt\"") != std::string::npos,
              "pages refer to the folder they're in");
        bool missing = false;
        for (const auto& w : report.warnings) {
            if (w.find("text/scroller.txt") != std::string::npos) missing = true;
        }
        check(missing, "a file the widgets need but the theme lacks is reported");
        check(report.removed.empty(), "nothing stale the first time");
        ThemeProject back;
        check(loadTheme(root, folder, back, error) && back.pages.size() == 3 && back.pages[0].widgets.size() == 2, "the project reloads");
        check(saveTheme(root, folder, p, lib, in, &report, error) && report.written.empty(), "an unchanged save rewrites nothing");

        // Things beside a stale page that aren't the editor's
        writeFile((dir / "collections/NES/medium_artwork/logo/keep.png").string(), "media");
        writeFile((dir / "collections/Hand/layout/layout.xml").string(), "<layout width=\"1920\" height=\"1080\"/>\n");
        deleteTemplate(p, "Arcade");
        deleteTemplate(p, "NES");
        check(saveTheme(root, folder, p, lib, in, &report, error), "save after deleting templates: " + error);
        check(report.removed.size() == 2, "both deleted templates' pages are reported removed");
        check(!pathExists(dir / "collections/Arcade"), "an emptied collection folder goes");
        check(!pathExists(dir / "collections/NES/layout") && pathExists(dir / "collections/NES/medium_artwork/logo/keep.png"),
              "media beside a removed page stays");
        check(pathExists(dir / "collections/Hand/layout/layout.xml"), "a hand-made page (no marker) stays");

        // The guard
        ThemeProject hostile;
        hostile.name = "Aeon Nox";
        check(!saveTheme(root, "Aeon Nox", hostile, lib, in, nullptr, error) && error.find("won't be overwritten") != std::string::npos,
              "saving over a hand-made theme is refused");
        check(readFile(root + "/Aeon Nox/layout.xml") == "<layout width=\"1920\" height=\"1080\"/>\n", "...and it's untouched");
        check(!deleteTheme(root, "Aeon Nox", error) && pathExists(fs::path(root) / "Aeon Nox/images/bg.png"), "deleting it is refused");
        check(!duplicateTheme(root, "Aeon Nox", lib, in, second, error), "duplicating it is refused");
        check(!saveTheme(root, "../escape", p, lib, in, nullptr, error) && !deleteTheme(root, "..", error) &&
              !loadTheme(root, "a/b", p, error), "path-shaped folders are refused");

        // Probe rules
        fs::create_directories(fs::path(root) / "Empty");
        check(probeTheme(root, "Empty") == TF_FRESH && probeTheme(root, "Nothing") == TF_FRESH, "no folder or an empty one is fresh");
        writeFile(root + "/Stopped/editor/project.xml", projectSerialize(ThemeProject()));
        check(probeTheme(root, "Stopped") == TF_EDITOR, "a project without a layout.xml yet is editor-made");
        writeFile(root + "/Lua Only/layout.lua", "x");
        check(probeTheme(root, "Lua Only") == TF_FOREIGN, "a folder with anything else is foreign");
        writeFile(root + "/Old Saucier/layout.xml", "<!-- one_saucier theme editor; project=old; v=1 -->\n<layout width=\"1920\" height=\"1080\"/>\n");
        check(probeTheme(root, "Old Saucier") == TF_EDITOR, "One Saucier's export is editor-made");

        // Listing
        const std::vector<ThemeSummary> themes = listThemes(root);
        const ThemeSummary* mine = nullptr;
        const ThemeSummary* aeon = nullptr;
        for (const auto& t : themes) {
            if (t.folder == "My Theme") mine = &t;
            if (t.folder == "Aeon Nox") aeon = &t;
        }
        check(mine && mine->kind == TF_EDITOR && mine->projectOk && mine->widgets == 2 && mine->name == "My Theme", "an editor theme lists with its summary");
        check(aeon && aeon->kind == TF_FOREIGN && !aeon->hasProject, "a hand-made theme lists as foreign");
        check(!themes.empty() && themes[0].name == "Aeon Nox", "sorted by name");
        writeFile(root + "/Broken/editor/project.xml", "<themeProject");
        bool brokenListed = false;
        for (const auto& t : listThemes(root)) {
            if (t.folder == "Broken") brokenListed = t.hasProject && !t.projectOk && !t.error.empty();
        }
        check(brokenListed, "a broken project lists with its error");

        // Assets
        const std::string srcA = root + "_src/logo.png";
        writeFile(srcA, "AAA");
        std::string rel;
        check(importAsset(root, folder, srcA, rel, error) && rel == "assets/logo.png" && readFile((dir / rel).string()) == "AAA", "import copies into assets/");
        check(importAsset(root, folder, srcA, rel, error) && rel == "assets/logo.png", "the same file is reused");
        writeFile(root + "_src/other/logo.png", "BBB");
        check(importAsset(root, folder, root + "_src/other/logo.png", rel, error) && rel == "assets/logo-2.png", "a different file gets -2");
        check(!importAsset(root, folder, root + "_src/none.png", rel, error), "a missing source is refused");
        check(!importAsset(root, "Aeon Nox", srcA, rel, error), "importing into a hand-made theme is refused");

        // Duplicate and delete
        std::string copy;
        check(duplicateTheme(root, folder, lib, in, copy, error), "duplicate: " + error);
        checkEqual(copy, "My Theme Copy", "the copy's folder");
        ThemeProject cp;
        check(loadTheme(root, copy, cp, error) && cp.name == "My Theme Copy" && cp.pages[0].widgets.size() == 2, "the copy's project");
        check(readFile((fs::path(root) / copy / "layout.xml").string()).rfind("<!-- RetroFE theme editor; project=my-theme-copy;", 0) == 0,
              "the copy's pages are re-marked");
        check(pathExists(fs::path(root) / copy / "assets/logo-2.png") &&
              readFile((fs::path(root) / copy / "layout.xml").string()).find("layouts/My Theme Copy/") != std::string::npos,
              "assets come along and pages refer to the copy's folder");
        check(deleteTheme(root, copy, error) && !pathExists(fs::path(root) / copy), "an editor theme deletes");
        check(!deleteTheme(root, copy, error), "deleting a missing theme is refused");

        // Writes leave nothing behind
        bool tmpLeft = false;
        for (const auto& e : fs::recursive_directory_iterator(dir)) {
            if (e.path().extension() == ".tmp") tmpLeft = true;
        }
        check(!tmpLeft, "no .tmp files are left behind");
        const std::string target = root + "/atomic.txt";
        check(writeFileAtomic(target, "one", error) && writeFileAtomic(target, "two", error) && readFile(target) == "two",
              "an atomic write replaces an existing file");

        // Widget folders
        writeFile(root + "_widgets/extra.widget", "[widget]\nid = extra\nname = Extra\ncategory = text\ncolor = 000000\nw = 10\nh = 10\n[fragment]\n<text id=\"{{ID}}\" value=\"x\"/>\n");
        writeFile(root + "_widgets/readme.txt", "not a widget");
        const WidgetFiles own = readWidgetFolder(root + "_widgets");
        WidgetLibrary merged;
        check(own.size() == 1 && buildWidgetLibrary(overlayWidgetFiles(embeddedWidgetFiles(), own), merged, error) &&
              merged.defs.size() == 36 && merged.find("extra"), "a folder's widgets join the stock ones");

        std::error_code ec;
        fs::remove_all(root + "_src", ec);
        fs::remove_all(root + "_widgets", ec);
    });

    // --- review fixes ------------------------------------------------------------------------

    // Page draws NUM_LAYERS = 20 layers (0..19) and drops a component on any other
    run("theme editor: every layer written is one the engine draws (0..19)", [] {
        WidgetLibrary lib = stockLibrary();
        std::string error;
        WidgetDef d;
        check(!parseWidgetDef("[widget]\nid = a\nname = A\ncategory = text\ncolor = 000000\nw = 1\nh = 1\nlayer = 20\n[fragment]\n<t id=\"{{ID}}\"/>\n", d, error),
              "a definition's default layer 20 is refused");
        ThemeProject parsed;
        check(projectParse("<themeProject version=\"1\" name=\"n\" orientation=\"horizontal\"><page collection=\"\">"
                           "<widget def=\"game_title\" id=\"t_1\" layer=\"20\"/></page></themeProject>", parsed, error) &&
              parsed.pages[0].widgets[0].layer == MAX_LAYER, "a project's layer 20 loads as 19");
        for (const auto& def : lib.defs) check(def.layer <= MAX_LAYER, def.id + " defaults to a drawn layer");

        // Offsets stay in range: the ghost's selected slot (+6), the strip's popped and selected slots (+1)
        WidgetInstance wheel = place("wheel_vertical", "wheel_vertical_1", 0, 0, 300, 900, 18);
        wheel.menuGhost = true;
        std::string xml;
        std::vector<std::string> issues = generateAndCheck(lib, wheel, &xml);
        check(issues.empty(), "a ghost wheel at layer 18 checks clean:" + join(issues));
        check(xml.find("layer=\"24\"") == std::string::npos && xml.find("layer=\"19\"") != std::string::npos,
              "the ghost's selected slot stops at 19 (was 24)");
        for (int layer : { 19, 25 }) {
            WidgetInstance strip = place("art_strip", "art_strip_1", 0, 0, 1400, 266, layer);
            issues = generateAndCheck(lib, strip, &xml);
            check(issues.empty() && xml.find("layer=\"20\"") == std::string::npos && xml.find("layer=\"26\"") == std::string::npos,
                  "a strip at layer " + std::to_string(layer) + " writes no layer past 19:" + join(issues));
            strip.stripPitchPx = 300;
            strip.stripSelW = 350;
            issues = generateAndCheck(lib, strip, &xml);
            check(issues.empty() && xml.find("layer=\"20\"") == std::string::npos,
                  "a linear strip at layer " + std::to_string(layer) + " pops its selected slot no further than 19:" + join(issues));
        }
        check(menuItemsXml(0, 0, 300, 900, 40, true).find("layer=\"40\"") == std::string::npos, "menu slots clamp their layer");
        check(menuPulseXml(MenuSelGeom{ 0, 0, 10, 10, true }, 0).find("layer=\"0\"") != std::string::npos, "the pulse never goes below 0");

        auto issuesFor = [](const std::string& body) {
            return checkLayoutXml("<layout width=\"1920\" height=\"1080\">" + body + "</layout>");
        };
        check(issuesFor("<image src=\"a.png\" layer=\"20\"/>").size() == 1, "the check flags layer 20 on a component");
        check(issuesFor("<image src=\"a.png\" layer=\"-1\"/>").size() == 1, "...and a negative layer");
        check(issuesFor("<menu><itemDefaults layer=\"19\"/><item layer=\"20\"/></menu>").size() == 1, "...and on a menu item");
        check(issuesFor("<image src=\"a.png\" layer=\"x\"/>").size() == 1, "...and a layer that isn't a number");
        check(issuesFor("<image src=\"a.png\"><onEnter><set duration=\"1\"><animate type=\"layer\" to=\"20\"/></set></onEnter></image>").size() == 1,
              "...and an animation to layer 20");
        check(issuesFor("<image src=\"a.png\" alpha=\"1.5\"/>").size() == 1 && issuesFor("<image src=\"a.png\" alpha=\"half\"/>").size() == 1,
              "alpha outside 0..1, or not a number");
        check(issuesFor("<image src=\"a.png\"><onEnter><set duration=\"soon\"><animate type=\"alpha\" to=\"1\"/></set></onEnter></image>").size() == 1,
              "a duration that isn't a number");
        check(issuesFor("<text value=\"x\" loadFontSize=\"big\"/>").size() == 1, "a loadFontSize that isn't a size");
        check(issuesFor("<image src=\"a.png\" layer=\"19\" alpha=\"0.60\"/>").empty(), "layer 19 and alpha 0.60 are fine");
    });

    run("theme editor: a generated page that doesn't parse is an error", [] {
        WidgetLibrary lib = stockLibrary();
        ThemeProject p;
        p.name = "Raw";
        WidgetInstance art = place("game_art", "game_art_1", 0, 0, 400, 300, 9);
        art.setVar("NATURAL", "true");
        art.setVar("NATPOSSET", "true");
        art.setVar("NATPOS", "x=\"1\" <oops");   // a raw value goes in verbatim
        p.pages[0].widgets.push_back(art);
        GenResult r = generateLayoutXml(p, p.pages[0], lib);
        check(!r.ok() && r.xml.empty() && r.errors[0].find("isn't valid layout XML") != std::string::npos,
              "a broken raw value makes the page an error, not a warning");
        SaveInputs in;
        check(!buildSavePlan(p, lib, in).ok(), "...and the save plan refuses it");

        ThemeProject q;
        q.name = "Ladder";
        WidgetInstance ladder = place("art_strip", "art_strip_1", 0, 0, 1400, 266, 6);
        ladder.stripItemsRaw = "selected=\"true|x=\"1\"";   // an unterminated quote
        q.pages[0].widgets.push_back(ladder);
        check(!generateLayoutXml(q, q.pages[0], lib).ok(), "a broken verbatim ladder is an error");
        check(layoutParseError("<layout/>").empty() && !layoutParseError("<page/>").empty() && !layoutParseError("<layout").empty(),
              "layoutParseError: fine, no root, not XML");

        // Bool values are normalised: a stored value can't reach the XML raw
        WidgetDef probe;
        std::string error;
        parseWidgetDef(PROBE_DEF, probe, error);
        WidgetInstance w = place("probe", "probe_1", 0, 0, 10, 10, 0);
        w.setVar("FLAG", "yes\" evil=\"1");
        checkEqual(expandText("{{FLAG}}", probe, w, GenOptions()).text, "false", "a hostile bool value writes false");
        w.setVar("FLAG", "true");
        checkEqual(expandText("{{FLAG}}", probe, w, GenOptions()).text, "true", "a true bool writes true");
    });

    run("theme editor: folder names are safe as written", [] {
        for (const char* name : { "New Theme", "  .hidden ", "Trailing.", "con", "COM1.txt", "a\tb", "Theme...", ". x" }) {
            check(folderNameSafe(themeFolderName(name)), std::string("themeFolderName(\"") + name + "\") is safe: " + themeFolderName(name));
        }
        check(folderNameSafe("Neo Geo") && folderNameSafe("Sega Genesis (US)"), "ordinary names are safe");
        check(!folderNameSafe("Arcade.") && !folderNameSafe("Arcade ") && !folderNameSafe(" Arcade") && !folderNameSafe("con") &&
              !folderNameSafe("nul.txt") && !folderNameSafe("A/B") && !folderNameSafe("..") && !folderNameSafe(""),
              "trailing dots and spaces, device names and separators aren't");
        WidgetLibrary lib = stockLibrary();
        for (const char* collection : { "Arcade.", "Arcade ", "con" }) {
            ThemeProject p;
            createTemplate(p, collection);
            check(!buildSavePlan(p, lib, SaveInputs()).ok(), std::string("a template for \"") + collection + "\" is refused");
            ThemeProject m;
            createTemplate(m, "Arcade");
            m.page("Arcade")->members.push_back(collection);
            check(!buildSavePlan(m, lib, SaveInputs()).ok(), std::string("a member \"") + collection + "\" is refused");
        }
    });

    run("theme editor: saving never overwrites a hand-made page, and never strands a folder", [] {
        WidgetLibrary lib = stockLibrary();
        const std::string root = makeRoot("theme_editor_review");
        SaveInputs in;
        in.fontBytes = fontBytes();
        std::string error, folder;
        check(createTheme(root, "Guarded", false, lib, in, folder, error), "create: " + error);
        const fs::path dir = fs::path(root) / folder;

        // H1: a hand-written page inside an editor theme
        const std::string handMade = "<layout width=\"1920\" height=\"1080\"><text value=\"mine\"/></layout>\n";
        writeFile((dir / "collections/NES/layout/layout.xml").string(), handMade);
        ThemeProject p;
        loadTheme(root, folder, p, error);
        const std::string projectBefore = readFile((dir / PROJECT_FILE).string());
        createTemplate(p, "Arcade");
        p.page("Arcade")->members.push_back("NES");
        SaveReport report;
        const bool saved = saveTheme(root, folder, p, lib, in, &report, error);
        check(!saved && error.find("collections/NES/layout/layout.xml") != std::string::npos && error.find("by hand") != std::string::npos,
              "adding NES to a template is refused while NES has a hand-made page: " + error);
        check(readFile((dir / "collections/NES/layout/layout.xml").string()) == handMade, "...the hand-made page is untouched");
        check(readFile((dir / PROJECT_FILE).string()) == projectBefore && !pathExists(dir / "collections/Arcade"),
              "...and nothing else was written");
        p.page("Arcade")->members.clear();
        createTemplate(p, "NES");
        check(!saveTheme(root, folder, p, lib, in, nullptr, error), "a template of its own for NES is refused too");
        fs::remove_all(dir / "collections/NES");
        check(saveTheme(root, folder, p, lib, in, nullptr, error), "once the page is moved away, it saves: " + error);

        // M2: a page renamed only in case is the same file on a case-insensitive drive
        ThemeProject renamed = p;
        renamed.page("NES")->collection = "nes";
        renamed.page("nes")->tplName = "nes";
        check(saveTheme(root, folder, renamed, lib, in, &report, error), "save with the collection renamed in case: " + error);
        check(isEditorMarker(readFile((dir / "collections/nes/layout/layout.xml").string()).substr(0, 40)),
              "the page just written is still there (was: removed as stale on Windows)");

        // M1: a first save that fails leaves nothing behind
        const fs::path fresh = fs::path(root) / "Fresh";
        fs::create_directories(fresh / PROJECT_FILE);   // a folder where project.xml must go: the write fails
        check(probeTheme(root, "Fresh") == TF_FRESH, "a folder holding only editor/ is fresh");
        ThemeProject f;
        f.name = "Fresh";
        check(!saveTheme(root, "Fresh", f, lib, in, nullptr, error), "the blocked save fails");
        check(probeTheme(root, "Fresh") == TF_FRESH && !pathExists(fresh / "layout.xml"), "...and the folder is still fresh, not foreign");
        fs::remove_all(fresh / "editor");
        fs::create_directories(fresh / ASSETS_FOLDER);
        check(probeTheme(root, "Fresh") == TF_FRESH, "a folder holding only assets/ is fresh");
        writeFile((fresh / "notes.txt").string(), "x");
        check(probeTheme(root, "Fresh") == TF_FOREIGN, "...anything else makes it foreign");
        writeFile(root + "_src/logo.png", "PNG");
        std::string rel;
        check(!importAsset(root, "Never Saved", root + "_src/logo.png", rel, error) && error.find("save") != std::string::npos &&
              !pathExists(fs::path(root) / "Never Saved"), "importing into a theme that was never saved is refused, nothing created");
        check(importAsset(root, folder, root + "_src/logo.png", rel, error), "importing into a saved theme works");
        std::error_code ec;
        fs::remove_all(root + "_src", ec);
    });

    // Not a test: with RETROFE_THEME_SAMPLE_DIR set to a layouts folder, saves
    // "Every Widget" there (every stock widget, wheel knobs on, a Main
    // template and a splash page), to run through the real engine headless:
    // see docs/design/theme-editor.md
    if (const char* sampleDir = std::getenv("RETROFE_THEME_SAMPLE_DIR")) {
        WidgetLibrary lib = stockLibrary();
        ThemeProject p;
        p.name = "Every Widget";
        int i = 0;
        for (const auto& d : lib.defs) {
            WidgetInstance w = place(d.id, d.id + "_1", (i % 7) * 270, (i / 7) * 210, std::min(d.w, 260), std::min(d.h, 200), d.layer);
            if (d.menu && !d.strip) {
                w.h = 900;
                w.menuPulse = true;
                w.menuGhost = true;
                w.menuIdleDimPct = 30;
                w.menuFadePct = 100;
                w.menuAngleDeg = 20;
            }
            p.pages[0].widgets.push_back(w);
            ++i;
        }
        createTemplate(p, "Main");
        ThemePage* splash = createTemplate(p, ThemeProject::splashPage(), nullptr);
        splash->widgets.push_back(place("static_text", "static_text_1", 760, 480, 400, 80, 1));
        splash->widgets.push_back(place("splash_status", "splash_status_1", 510, 980, 900, 60, 3));
        const std::string folder = "Every Widget";
        std::string error;
        SaveInputs in;
        in.fontBytes = fontBytes();
        SaveReport report;
        bool saved = saveTheme(sampleDir, folder, p, lib, in, &report, error);
        // The files the static widgets point at, then save again for the report
        const std::vector<unsigned char> pixels(4 * 4 * 4, 0x80);
        writeFile((fs::path(sampleDir) / folder / "images/background.png").string(), encodePng(pixels.data(), 4, 4));
        writeFile((fs::path(sampleDir) / folder / "text/scroller.txt").string(), "Scrolling text from the theme folder\n");
        saved = saved && saveTheme(sampleDir, folder, p, lib, in, &report, error);
        std::cout << "sample theme " << (saved ? "saved to " + std::string(sampleDir) + "/" + folder : "not saved: " + error) << std::endl;
        for (const auto& w : report.warnings) std::cout << "  warning: " << w << std::endl;
    }
}

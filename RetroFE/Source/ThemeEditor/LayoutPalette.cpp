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
#include "LayoutPalette.h"
#include "LayoutText.h"
#include <cctype>

namespace ThemeEditor
{
    namespace
    {
        PaletteEntry entry(const char* id, const char* label, const char* element, const char* summary, int w, int h,
                           bool sized, const char* body)
        {
            PaletteEntry e;
            e.id = id;
            e.label = label;
            e.element = element;
            e.summary = summary;
            e.w = w;
            e.h = h;
            e.sized = sized;
            e.body = body;
            return e;
        }

        std::vector<PaletteEntry> makePalette()
        {
            std::vector<PaletteEntry> p;
            PaletteEntry e;

            e = entry("image", "Image", "image", "A picture of the theme's own (a PNG, JPG...), always shown", 400, 300, true,
                      "<image src=\"{SRC}\" x=\"{X}\" y=\"{Y}\" width=\"{W}\" height=\"{H}\" layer=\"{LAYER}\"/>");
            e.file = "image";
            p.push_back(e);

            e = entry("video", "Video", "video", "A video of the theme's own, always shown", 640, 360, true,
                      "<video src=\"{SRC}\" x=\"{X}\" y=\"{Y}\" width=\"{W}\" height=\"{H}\" layer=\"{LAYER}\"/>");
            e.file = "video";
            p.push_back(e);

            p.push_back(entry("text", "Text", "text", "Fixed text in the page's font", 300, 40, false,
                              "<text value=\"Text\" x=\"{X}\" y=\"{Y}\" fontSize=\"40\" layer=\"{LAYER}\"/>"));

            p.push_back(entry("statusText", "Status text", "statusText", "What RetroFE is doing (launching, loading...)", 400, 30, false,
                              "<statusText x=\"{X}\" y=\"{Y}\" fontSize=\"30\" layer=\"{LAYER}\"/>"));

            p.push_back(entry("container", "Container (a box)", "container", "A coloured box, to put things on", 400, 300, true,
                              "<container x=\"{X}\" y=\"{Y}\" width=\"{W}\" height=\"{H}\" backgroundColor=\"000000\" "
                              "backgroundAlpha=\"0.5\" layer=\"{LAYER}\"/>"));

            e = entry("reloadableImage", "Game art", "reloadableImage", "The selected game's art of one kind (its type)", 400, 300, true,
                      "<reloadableImage type=\"{TYPE}\" x=\"{X}\" y=\"{Y}\" width=\"{W}\" height=\"{H}\" layer=\"{LAYER}\"/>");
            e.typeAttr = "type";
            e.types = { "logo", "artwork_front", "screenshot", "fanart", "marquee", "device", "icon" };
            p.push_back(e);

            e = entry("reloadableVideo", "Game video", "reloadableVideo",
                      "The selected game's video, with art of one kind when it has none", 594, 445, true,
                      "<reloadableVideo imageType=\"{TYPE}\" x=\"{X}\" y=\"{Y}\" width=\"{W}\" height=\"{H}\" layer=\"{LAYER}\"/>");
            e.typeAttr = "imageType";
            e.types = { "screenshot", "artwork_front", "logo", "fanart" };
            p.push_back(e);

            e = entry("reloadableText", "Game text", "reloadableText", "A detail of the selected game or collection (its type)", 300, 40, false,
                      "<reloadableText type=\"{TYPE}\" x=\"{X}\" y=\"{Y}\" fontSize=\"40\" layer=\"{LAYER}\"/>");
            e.typeAttr = "type";
            e.types = { "title", "year", "manufacturer", "developer", "genre", "rating", "score", "numberPlayers", "playCount",
                        "lastPlayed", "timeSpent", "collectionName", "collectionSize", "collectionIndex", "time" };
            p.push_back(e);

            e = entry("reloadableScrollingText", "Scrolling game text", "reloadableScrollingText",
                      "A long text of the selected game (its story...), scrolling", 600, 300, true,
                      "<reloadableScrollingText type=\"{TYPE}\" direction=\"vertical\" scrollingSpeed=\"20\" startTime=\"3\" "
                      "x=\"{X}\" y=\"{Y}\" width=\"{W}\" height=\"{H}\" fontSize=\"30\" layer=\"{LAYER}\"/>");
            e.typeAttr = "type";
            e.types = { "story", "description", "tagline" };
            p.push_back(e);

            p.push_back(entry("reloadableHiscores", "Hiscores", "reloadableHiscores", "The selected game's high score table", 600, 400, true,
                              "<reloadableHiscores x=\"{X}\" y=\"{Y}\" width=\"{W}\" height=\"{H}\" fontSize=\"30\" layer=\"{LAYER}\"/>"));

            p.push_back(entry("menu", "Menu (a list)", "menu", "A list of the games, one under another", 400, 500, true,
                              "<menu x=\"{X}\" y=\"{Y}\" width=\"{W}\" height=\"{H}\">\n"
                              "    <itemDefaults x=\"{X}\" width=\"{W}\" height=\"50\" fontSize=\"40\" alpha=\"0.6\" layer=\"{LAYER}\"/>\n"
                              "</menu>"));

            p.push_back(entry("menu-custom", "Menu (a wheel)", "menu", "The games side by side, the selected one in the middle", 900, 300, false,
                              "<menu type=\"custom\" orientation=\"horizontal\">\n"
                              "    <itemDefaults x=\"{CX}\" y=\"{CY}\" width=\"270\" height=\"200\" xOrigin=\"center\" yOrigin=\"center\" "
                              "fontSize=\"36\" alpha=\"0.6\" layer=\"{LAYER}\"/>\n"
                              "    <item xOffset=\"-300\"/>\n"
                              "    <item xOffset=\"0\" selected=\"true\" alpha=\"1\"/>\n"
                              "    <item xOffset=\"300\"/>\n"
                              "</menu>"));
            return p;
        }

        void replaceAll(std::string& s, const std::string& from, const std::string& to)
        {
            for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
        }
    }

    const std::vector<PaletteEntry>& layoutPalette()
    {
        static const std::vector<PaletteEntry> palette = makePalette();
        return palette;
    }

    const PaletteEntry* paletteEntry(const std::string& id)
    {
        for (const PaletteEntry& e : layoutPalette()) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    std::string paletteFragment(const PaletteEntry& entry, const std::string& type, const std::string& src, int x, int y, int layer)
    {
        std::string out = entry.body;
        std::string placeholder = entry.file == "video" ? "videos/new.mp4" : "images/new.png";
        replaceAll(out, "{SRC}", xmlEncodeAttr(src.empty() ? placeholder : src, '"'));
        replaceAll(out, "{TYPE}", xmlEncodeAttr(type.empty() ? (entry.types.empty() ? std::string() : entry.types[0]) : type, '"'));
        replaceAll(out, "{X}", std::to_string(x));
        replaceAll(out, "{Y}", std::to_string(y));
        replaceAll(out, "{W}", std::to_string(entry.w));
        replaceAll(out, "{H}", std::to_string(entry.h));
        replaceAll(out, "{CX}", std::to_string(x + entry.w / 2));
        replaceAll(out, "{CY}", std::to_string(y + entry.h / 2));
        replaceAll(out, "{LAYER}", std::to_string(layer));
        return out;
    }

    std::string eventFragment(const std::string& event)
    {
        return "<" + event + "><set duration=\"0.2\"><animate type=\"alpha\" from=\"0\" to=\"1\"/></set></" + event + ">";
    }

    std::string setFragment()
    {
        return "<set duration=\"0.2\"><animate type=\"alpha\" to=\"1\"/></set>";
    }

    std::string animateFragment()
    {
        return "<animate type=\"alpha\" to=\"1\"/>";
    }

    const std::vector<std::string>& tweenTypeNames()
    {
        static const std::vector<std::string> names = {
            "x", "y", "alpha", "width", "height", "xOffset", "yOffset", "xOrigin", "yOrigin", "angle", "fontSize",
            "backgroundAlpha", "maxWidth", "maxHeight", "layer", "containerX", "containerY", "containerWidth",
            "containerHeight", "volume", "nop", "restart"
        };
        return names;
    }

    const std::vector<std::string>& tweenAlgorithmNames()
    {
        static const std::vector<std::string> names = {
            "linear", "easeInQuadratic", "easeOutQuadratic", "easeInOutQuadratic", "easeInCubic", "easeOutCubic",
            "easeInOutCubic", "easeInQuartic", "easeOutQuartic", "easeInOutQuartic", "easeInQuintic", "easeOutQuintic",
            "easeOnOutQuintic", "easeInSine", "easeOutSine", "easeInOutSine", "easeInExponential", "easeOutExponential",
            "easeInOutExponential", "easeInCircular", "easeOutCircular", "easeInOutCircular"
        };
        return names;
    }

    std::string engineEventName(const std::string& element)
    {
        if (element.size() < 3 || element.compare(0, 2, "on") != 0) return element;
        std::string name = element.substr(2);
        name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
        return name;
    }
}

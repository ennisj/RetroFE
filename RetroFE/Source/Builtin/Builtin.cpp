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
#include "Builtin.h"
#include "../Manager/Home.h"

namespace Builtin
{

const char* const settings = R"(
log = ERROR,WARNING,NOTICE
numScreens = 1
fullscreen = no
horizontal = 1280
vertical = 720
windowBorder = true
windowResize = true
hideMouse = no
layout = Aeon Nox
firstCollection = Main
homeMenu = yes
)";

// Pads by SDL's controller names, so A selects and B goes back on any pad SDL
// has a mapping for. Buttons 0 and 1 as before, but only on joysticks SDL has
// no mapping for (arcade encoders such as a Zero Delay): on some mapped pads
// (a DualShock through DirectInput) joyButton1 is A, and would cross Select and
// Back. Directions also keep the first hat and stick, which are the same
// direction on mapped pads and work on unmapped joysticks.
const char* const controls = R"(
up = Up,joyDPUp,joyLeftY-,joyHat0Up,joyAxis1-
down = Down,joyDPDown,joyLeftY+,joyHat0Down,joyAxis1+
left = Left,joyDPLeft,joyLeftX-,joyHat0Left,joyAxis0-
right = Right,joyDPRight,joyLeftX+,joyHat0Right,joyAxis0+
pageUp = PageUp
pageDown = PageDown
select = Return,joyA,joyUnmappedButton0
back = Escape,joyB,joyUnmappedButton1
quit = Q
deadZone = 30
)";

// No font attribute: text falls back to the built-in font
static const char* const splashLayout = R"(
<layout width="1280" height="720" loadFontSize="48" fontColor="dedede" minShowTime="0.5">
  <text value="RetroFE" x="center" y="center" xOrigin="center" yOrigin="center" fontSize="96" layer="1"/>
</layout>
)";

static const char* const mainLayout = R"(
<layout width="1280" height="720" loadFontSize="48" fontColor="dedede">
  <text value="RetroFE" x="center" y="70" xOrigin="center" yOrigin="center" fontSize="56" layer="1"/>
  <reloadableText type="collectionName" x="center" y="130" xOrigin="center" yOrigin="center" fontSize="28" alpha="0.6" layer="1"/>
  <reloadableText type="collectionSize" singlePrefix="" singlePostfix=" item" pluralPrefix="" pluralPostfix=" items" x="center" y="165" xOrigin="center" yOrigin="center" fontSize="22" alpha="0.4" layer="1"/>

  <menu menuIndex="0" type="custom" orientation="vertical" textFallback="true" scrollTime=".15" layer="2">
    <itemDefaults x="center" xOrigin="center" yOrigin="center" fontSize="32" alpha="0.35" layer="2"/>
    <item y="270"/>
    <item y="330"/>
    <item selected="true" y="400" fontSize="44" alpha="1"/>
    <item y="470"/>
    <item y="530"/>
  </menu>

  <text value="No layout installed - using RetroFE's built-in layout" x="center" y="640" xOrigin="center" yOrigin="center" fontSize="20" alpha="0.4" layer="1"/>
  <text value="Enter: select    Esc: back    Q: quit" x="center" y="675" xOrigin="center" yOrigin="center" fontSize="20" alpha="0.4" layer="1"/>
</layout>
)";

// The Home menu as a carousel: left/right to choose
static const char* const homeLayout = R"(
<layout width="1280" height="720" loadFontSize="48" fontColor="dedede">
  <text value="RetroFE" x="center" y="110" xOrigin="center" yOrigin="center" fontSize="56" layer="1"/>

  <menu menuIndex="0" type="custom" orientation="horizontal" textFallback="true" scrollTime=".15" layer="2">
    <itemDefaults y="370" xOrigin="center" yOrigin="center" fontSize="36" alpha="0.35" layer="2"/>
    <item x="300"/>
    <item selected="true" x="640" fontSize="64" alpha="1"/>
    <item x="980"/>
  </menu>

  <text value="Left/Right: choose    Enter: select    Q: quit" x="center" y="675" xOrigin="center" yOrigin="center" fontSize="20" alpha="0.4" layer="1"/>
</layout>
)";

// A video collection (info.conf type = videos): its list, and the selected
// video playing beside it. Its own files are its video media
// (VideoCollections::useItemsAsVideoMedia).
static const char* const videosLayout = R"(
<layout width="1280" height="720" loadFontSize="48" fontColor="dedede">
  <reloadableText type="collectionName" x="40" y="50" xOrigin="left" yOrigin="center" fontSize="34" maxWidth="760" layer="1"/>
  <reloadableText type="collectionIndexSize" menuScrollReload="true" x="1240" y="50" xOrigin="right" yOrigin="center" fontSize="22" alpha="0.5" layer="1"/>

  <menu menuIndex="0" type="custom" orientation="vertical" textFallback="true" scrollTime=".15" layer="2">
    <itemDefaults x="40" xOrigin="left" yOrigin="center" fontSize="24" maxWidth="440" alpha="0.35" layer="2"/>
    <item y="170"/>
    <item y="225"/>
    <item y="280"/>
    <item selected="true" y="345" fontSize="32" alpha="1"/>
    <item y="410"/>
    <item y="465"/>
    <item y="520"/>
  </menu>

  <container x="530" y="110" width="710" height="400" backgroundColor="000000" backgroundAlpha="0.6" layer="1"/>
  <reloadableVideo type="video" imageType="screenshot" x="885" y="310" xOrigin="center" yOrigin="center" width="710" maxHeight="400" layer="2"/>
  <reloadableText type="title" x="885" y="555" xOrigin="center" yOrigin="center" fontSize="30" maxWidth="710" layer="2"/>

  <text value="Enter: play    Esc: back    Q: quit" x="center" y="675" xOrigin="center" yOrigin="center" fontSize="20" alpha="0.4" layer="1"/>
</layout>
)";

// Where a video collection's videos play (RetroFE's VideoPlayback). The menu
// is the list it moves through, not shown; jukebox="true" lets Select pause
// the video and tells RetroFE when it has ended.
static const char* const playerLayout = R"(
<layout width="1280" height="720" loadFontSize="48" fontColor="ffffff">
  <container x="0" y="0" width="1280" height="720" backgroundColor="000000" backgroundAlpha="1" layer="0"/>
  <reloadableVideo type="video" imageType="screenshot" jukebox="true" x="center" y="center" xOrigin="center" yOrigin="center" width="1280" maxHeight="720" layer="1"/>

  <menu menuIndex="0" type="custom" orientation="horizontal" textFallback="true" layer="2">
    <itemDefaults alpha="0"/>
    <item selected="true" x="0" y="0" alpha="0" fontSize="10"/>
  </menu>

  <container x="0" y="0" width="1280" height="80" backgroundColor="000000" backgroundAlpha="0.55" layer="3">
    <onEnter>          <set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="1" to="0"/></set></onEnter>
    <onHighlightEnter> <set duration=".01"><animate type="alpha" to="1"/></set><set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="1" to="0"/></set></onHighlightEnter>
  </container>
  <reloadableText type="title" x="30" y="40" xOrigin="left" yOrigin="center" fontSize="30" maxWidth="900" layer="4">
    <onEnter>          <set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="1" to="0"/></set></onEnter>
    <onHighlightEnter> <set duration=".01"><animate type="alpha" to="1"/></set><set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="1" to="0"/></set></onHighlightEnter>
  </reloadableText>
  <reloadableText type="collectionIndexSize" x="1250" y="40" xOrigin="right" yOrigin="center" fontSize="22" alpha="0.7" layer="4">
    <onEnter>          <set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="0.7" to="0"/></set></onEnter>
    <onHighlightEnter> <set duration=".01"><animate type="alpha" to="0.7"/></set><set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="0.7" to="0"/></set></onHighlightEnter>
  </reloadableText>

  <container x="0" y="660" width="1280" height="60" backgroundColor="000000" backgroundAlpha="0.55" layer="3">
    <onEnter>          <set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="1" to="0"/></set></onEnter>
    <onHighlightEnter> <set duration=".01"><animate type="alpha" to="1"/></set><set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="1" to="0"/></set></onHighlightEnter>
  </container>
  <reloadableText type="isPaused" x="center" y="center" xOrigin="center" yOrigin="center" fontSize="56" layer="4"/>
  <reloadableText type="current" x="30" y="690" xOrigin="left" yOrigin="center" fontSize="20" alpha="0.6" layer="4"/>
  <reloadableText type="duration" x="1250" y="690" xOrigin="right" yOrigin="center" fontSize="20" alpha="0.6" layer="4"/>
  <text value="Enter: pause    Left/Right: previous/next    Up/Down: seek    Esc: back to the list" x="center" y="690" xOrigin="center" yOrigin="center" fontSize="18" layer="4">
    <onEnter>          <set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="0.5" to="0"/></set></onEnter>
    <onHighlightEnter> <set duration=".01"><animate type="alpha" to="0.5"/></set><set duration="3"><animate type="nop"/></set><set duration="1"><animate type="alpha" from="0.5" to="0"/></set></onHighlightEnter>
  </text>
</layout>
)";

// Settings, Manage and Catalog when the layout doesn't style them
// (layouts/<layout>/forms/layout.xml or forms/<id>/layout.xml)
static const char* const formLayout = R"(
<layout width="1280" height="720" loadFontSize="40" fontColor="c8c8c8">
  <container x="0" y="0" width="1280" height="720" backgroundColor="000000" backgroundAlpha="0.75" layer="0"/>
  <form x="center" y="center" xOrigin="center" yOrigin="center" width="820" height="660"
        fontSize="22" fontColor="c8c8c8" focusColor="ffffff" rowHeight="34" labelWidth="260"
        rowDetailColor="8fa9c9" headerColor="8fa9c9" headerAlpha="1"
        separatorColor="ffffff" separatorAlpha="0.08" imageBackgroundColor="000000" imageBackgroundAlpha="0.3"
        statusColor="9fd8a4" infoColor="b4bcc6" errorColor="ff8f80" confirmBackgroundColor="c0392b" confirmBackgroundAlpha="0.45"
        backgroundColor="14181e" backgroundAlpha="1" titleDetail="info:pendingChanges"
        focusBackgroundColor="3d6fb6" focusBackgroundAlpha="0.55" layer="1"/>
</layout>
)";

// The on-screen keyboard when the layout doesn't style it (layouts/<layout>/forms/keyboard/layout.xml)
static const char* const keyboardLayout = R"(
<layout width="1280" height="720" loadFontSize="40" fontColor="c8c8c8">
  <keyboard x="center" y="700" xOrigin="center" yOrigin="bottom" width="940" height="400"
            fontSize="24" fontColor="c8c8c8" focusColor="ffffff"
            backgroundColor="0d1014" backgroundAlpha="1"
            keyColor="ffffff" keyAlpha="0.07" focusKeyColor="3d6fb6" focusKeyAlpha="0.75" layer="1"/>
</layout>
)";

const char* layoutXml(const std::string& page, const std::string& collection)
{
    if (page == "splash") {
        return splashLayout;
    }
    if (page == "form") {
        return formLayout;
    }
    if (page == "keyboard") {
        return keyboardLayout;
    }
    if (page == "layout") {
        return collection == Home::collectionName ? homeLayout : mainLayout;
    }
    if (page == "videos") {
        return videosLayout;
    }
    if (page == "player") {
        return playerLayout;
    }
    return nullptr;
}

}

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

const char* const controls = R"(
up = Up,joyHat0Up,joyAxis1-
down = Down,joyHat0Down,joyAxis1+
left = Left,joyHat0Left,joyAxis0-
right = Right,joyHat0Right,joyAxis0+
pageUp = PageUp
pageDown = PageDown
select = Return,joyButton0
back = Escape,joyButton1
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

  <text value="Left/Right: choose    Enter: select    F10: Settings    Q: quit" x="center" y="675" xOrigin="center" yOrigin="center" fontSize="20" alpha="0.4" layer="1"/>
</layout>
)";

// Settings, Manage and Catalog when the layout doesn't style them
// (layouts/<layout>/forms/layout.xml or forms/<id>/layout.xml)
static const char* const formLayout = R"(
<layout width="1280" height="720" loadFontSize="40" fontColor="c8c8c8">
  <container x="0" y="0" width="1280" height="720" backgroundColor="000000" backgroundAlpha="0.75" layer="0"/>
  <form x="center" y="center" xOrigin="center" yOrigin="center" width="820" height="660"
        fontSize="22" fontColor="c8c8c8" focusColor="ffffff" rowHeight="34" labelWidth="260"
        backgroundColor="14181e" backgroundAlpha="1"
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
    return nullptr;
}

}

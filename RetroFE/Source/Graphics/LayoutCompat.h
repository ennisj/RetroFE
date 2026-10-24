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

#include <string>

// Fork compatibility for layouts written for a newer RetroFE (the OneSauce
// themes). Only what the theme data itself makes clear is supported; see
// docs/ONESAUCE-COMPAT.md. No SDL here, so retrofe_tests can check it.
namespace LayoutCompat
{
    // A page drawn by a layout.lua instead of a layout.xml. RetroFE doesn't run
    // Lua; it only reads build() for its single
    //   pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. "<path>")
    // call, or a call to a function of a module the file require()s (looked up
    // in <theme>/lua/<module>.lua) whose body is that call with its argument
    // appended (luna.build(pageBuilder, "<path>")). layoutPath is the lua
    // file's folder. Returns the XML file it names, or "" (why says why) when
    // build() does anything else: a condition, several builds, no build.
    // The lua file's preUpdate/postUpdate hooks are ignored.
    std::string luaBuildTarget(const std::string& luaFile, const std::string& themeFolder, std::string* why = nullptr);

    // transform="(x, y), (w, h); (x1, y1), (x2, y2), (x3, y3), (x4, y4)[; n]"
    // or "(x, y, w, h), (x1, y1), ..., (x4, y4)[, n]": the layout rectangle
    // x, y, w, h is drawn into the quad whose corners are top-left, top-right,
    // bottom-left, bottom-right, with perspective. The trailing number is
    // ignored. The "quad (x, y, u, v), ..." form isn't supported.
    struct QuadTransform
    {
        float src[4]{ 0, 0, 0, 0 };  // x, y, w, h
        float dst[8]{ 0, 0, 0, 0, 0, 0, 0, 0 };
        double h[9]{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };  // unit square -> quad (Heckbert)

        // Where layout point (x, y) lands
        void apply(float x, float y, float& outX, float& outY) const;
    };

    bool parseTransform(const std::string& value, QuadTransform& out, std::string* why = nullptr);
}

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

#include "../../Forms/BuiltinForms.h"

namespace Manager
{
    // The Systems screens (videos = false: "systems", "system", "system-...",
    // bound to the "system" provider) or the same screens worded for video
    // collections (videos = true: "videos", "video", "video-...", bound to
    // "video"). Both are made from one set of templates (SystemForms.cpp).
    const Forms::BuiltinTable& systemForms(bool videos);
}

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
#include "../../Forms/BuiltinForms.h"
#include "../Systems/SystemForms.h"

// The Videos screens, opened from Manage. A video collection is a system
// whose files are videos, marked with "type = videos" in its info.conf
// (DECISIONS M7); "play = here" plays them in RetroFE, else its launcher (a
// video player) does (Collection/VideoCollections.h). These are the Systems
// screens worded for videos, with the Plays choice: "videos", "video" and
// "video-...", served by a second SystemProvider with prefix "video".
Forms::BuiltinTable Forms::videosForms()
{
    return Manager::systemForms(true);
}

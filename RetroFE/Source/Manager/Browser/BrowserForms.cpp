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

// The file and folder browser every <folder> and <file> field opens
// (BrowserProvider.h). One form for the starting places and every folder:
// param dir is the folder shown, none for the places.
namespace
{

const char* const browserForm = R"(
<form id="file-browser" title="Choose a file" bind="browse:title">
  <value label="For" bind="browse:for"/>
  <value label="Folder" bind="browse:where"/>
  <value label="Here" bind="browse:summary"/>
  <note bind="browse:note"/>
  <button label="Use this folder" action="browse.use" visibleWhen="browse:canUseFolder"/>
  <text label="Search" bind="browse:query" live="yes" hint="Part of the name" visibleWhen="browse:canSearch"/>
  <results bind="browse:places" open="file-browser" visibleWhen="browse:isTop"/>
  <results bind="browse:entries" open="file-browser" jump="letter" limit="200" visibleWhen="browse:isFolder"/>
  <note bind="browse:emptyNote"/>
  <button label="Browse with Windows..." action="browse.system" visibleWhen="browse:hasSystemDialog"/>
</form>
)";

}

namespace Forms
{

BuiltinTable browserForms()
{
    return {
        { "file-browser", browserForm },
    };
}

}

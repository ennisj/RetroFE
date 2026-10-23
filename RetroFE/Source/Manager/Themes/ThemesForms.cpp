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

// The Themes screens, opened from Manage (bindings in ThemeProvider.h).
// theme-editor is drawn by the theme editor itself, with ImGui
// (Manager/Themes/app/ThemeEditorScreen.h); its form only has to exist so
// the screen can be opened and returned from like any other.
namespace
{

const char* const themesForm = R"(
<form id="themes" title="Themes">
  <link label="New theme" open="theme-new"/>
  <results bind="theme:list" open="theme" empty="No themes are installed. Install one from the Catalog, or make a new one."/>
</form>
)";

const char* const themeForm = R"(
<form id="theme" title="Theme" bind="theme:name">
  <value label="In use" bind="theme:inUse"/>
  <value label="Made" bind="theme:kind"/>
  <value label="Size" bind="theme:size"/>
  <value label="Pages" bind="theme:pages"/>
  <value label="Widgets" bind="theme:widgets"/>
  <value label="Folder" bind="theme:folder"/>
  <note bind="theme:problem"/>
  <note bind="theme:editNote"/>
  <button label="Use this theme" action="theme.use" visibleWhen="!theme:isActive"/>
  <button label="Edit" action="theme.edit" visibleWhen="theme:isEditor"/>
  <button label="Duplicate" action="theme.duplicate" visibleWhen="theme:isEditor"/>
  <button label="Delete" action="theme.delete" visibleWhen="theme:isEditor" confirm="Delete this theme and its files?"/>
</form>
)";

const char* const themeNewForm = R"(
<form id="theme-new" title="New theme">
  <text label="Name" bind="theme:newName"/>
  <choice label="Orientation" bind="theme:newOrientation" options="theme:orientations"/>
  <note value="The new theme starts with an empty page. Edit it to place widgets: titles, art, videos and the game wheel."/>
  <button label="Create" action="theme.create"/>
</form>
)";

const char* const themeEditorForm = R"(
<form id="theme-editor" title="Theme editor">
  <note value="The theme editor draws this screen itself."/>
</form>
)";

}

Forms::BuiltinTable Forms::themesForms()
{
    return {
        { "themes", themesForm },
        { "theme", themeForm },
        { "theme-new", themeNewForm },
        { "theme-editor", themeEditorForm },
    };
}

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
#include "Form.h"
#include "BuiltinForms.h"

// The forms RetroFE ships with. Layouts can replace any of them with
// layouts/<layout>/forms/<id>.xml using the same elements and bindings.
// The Manage sections (games, menus, systems...) keep theirs in
// Manager/<Section>/<Section>Forms.cpp.
//
// Bindings:
//   settings:<key>   a global setting; edits are kept until settings.save.
//                    default="" is what RetroFE uses when no file sets it.
//   draft:<key>      form input that isn't saved anywhere by itself.
//                    default="" is its initial value.
//   info:<key>       computed, read-only values
//   param:<name>     the screen's parameters, e.g. param:id
//   <prefix>:<key>   a provider's values, e.g. game:title (see Manager/Provider.h)
namespace
{

// Controls first: on a cabinet it's the setting that matters most.
// Screen size sets horizontal and vertical together; Custom shows them.
const char* const settingsForm = R"(
<form id="settings" title="Settings">
  <link label="Controls" open="controls"/>
  <section label="Display">
    <toggle label="Fullscreen" bind="settings:fullscreen"/>
    <choice label="Monitor" bind="settings:screenNum0" options="displays" default="0"/>
    <choice label="Screen size" bind="settings:screenSize" options="screenSizes" enabledWhen="!settings:fullscreen"/>
    <text label="Width" bind="settings:horizontal" hint="pixels, stretch or auto" enabledWhen="!settings:fullscreen" visibleWhen="info:customScreenSize"/>
    <text label="Height" bind="settings:vertical" hint="pixels, stretch or auto" enabledWhen="!settings:fullscreen" visibleWhen="info:customScreenSize"/>
    <toggle label="Hide mouse" bind="settings:hideMouse" default="no"/>
    <toggle label="Capture mouse" bind="settings:captureMouse" default="no" enabledWhen="settings:fullscreen" help="Fullscreen only: keeps the mouse pointer on RetroFE's screen. In a window the pointer always moves freely."/>
    <note value="Fullscreen, monitor and screen size apply the next time RetroFE starts. Hide mouse and Capture mouse apply when you save; RetroFE reloads."/>
  </section>
  <section label="Menus">
    <choice label="Theme" bind="settings:layout" options="layouts"/>
    <toggle label="Start on Home" bind="settings:homeMenu" default="no"/>
    <choice label="Browse opens" bind="settings:firstCollection" options="collections" default="Main"/>
  </section>
  <section label="Video">
    <toggle label="Play videos" bind="settings:videoEnable" default="yes"/>
    <number label="Attract mode after" bind="settings:attractModeTime" min="0" max="3600" default="0" help="Seconds without input. 0 turns attract mode off."/>
  </section>
  <actions>
    <button label="Save" action="settings.save" primary="yes"/>
  </actions>
</form>
)";

// The hub: on a bare install the first launcher and system, then applying
// what's waiting, and one link per section.
const char* const manageForm = R"(
<form id="manage" title="Manage">
  <note visibleWhen="!info:hasCollections" panel="yes"
        value="Welcome! First add a launcher (the emulator program that runs the games), then a system (a folder of games and the launcher that starts them)."/>
  <link label="Add your first launcher/emulator" open="emulator-new" visibleWhen="!info:hasLaunchers"/>
  <results bind="system:firstGames" open="system-add-games" visibleWhen="system:hasFirstEmptySystem"/>
  <link label="Add your first system" open="system-add" visibleWhen="!system:hasSystems"/>
  <button label="Apply changes now" action="changes.apply" visibleWhen="info:hasPendingChanges"/>
  <link label="Games" open="games"
        help="Find a game to change its title and details, art and video, how it starts, its saves and playlists, or to start or hide it."/>
  <link label="Menus" open="menus" help="The menus Browse shows: what each one lists, in what order, and their art."/>
  <link label="Systems" open="systems"
        help="Add a system (a folder of games and the emulator that runs them), or change one's settings, launcher and art."/>
  <link label="Videos" open="videos" help="Collections of videos RetroFE plays, and how each one plays."/>
  <link label="Launchers" open="emulators"
        help="The launchers that start games: the program, its arguments, and which systems and games use each one."/>
  <link label="Themes" open="themes" help="How RetroFE looks: choose a theme, or change one with the theme and layout editors."/>
</form>
)";

// Every built-in form: these, then each Manage section's
Forms::BuiltinTable allForms()
{
    Forms::BuiltinTable forms = {
        { "settings", settingsForm },
        { "manage", manageForm },
    };
    for (auto section : { Forms::gamesForms, Forms::menusForms, Forms::systemsForms,
                          Forms::videosForms, Forms::emulatorsForms, Forms::themesForms,
                          Forms::controlsForms, Forms::browserForms }) {
        Forms::BuiltinTable more = section();
        forms.insert(forms.end(), more.begin(), more.end());
    }
    return forms;
}

}

namespace Forms
{

const char* builtinForm(const std::string& id)
{
    static const BuiltinTable forms = allForms();
    for (const BuiltinXml& form : forms) {
        if (id == form.id) {
            return form.xml;
        }
    }
    return nullptr;
}

std::vector<std::string> builtinFormIds()
{
    std::vector<std::string> ids;
    for (const BuiltinXml& form : allForms()) {
        ids.push_back(form.id);
    }
    return ids;
}

}

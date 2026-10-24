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
#include "RetroArchForms.h"
#include <string>
#include <utility>
#include <vector>

// The RetroArch screens of a game and of a system. Each template is expanded
// twice: {p} is the form id stem ("game" / "system"), the other {words} are
// the wording for one game or for a core's own files.
namespace
{

const char* const hubForm = R"XML(
<form id="{p}-retroarch" title="RetroArch" bind="retroarch:screenTitle">
  <note bind="retroarch:why"/>
  <value label="Core" bind="retroarch:core"/>
  <value label="Launcher" bind="retroarch:launcher"/>
  <note bind="retroarch:coreNote"/>
  <note bind="retroarch:reaches"/>
  <link label="Core options" open="{p}-core-options" status="retroarch:optionsStatus" visibleWhen="retroarch:isReady"/>
  <link label="Controls" open="{p}-controls" status="retroarch:controlsStatus" visibleWhen="retroarch:isReady"/>
  <section label="RetroArch settings" visibleWhen="retroarch:isReady">
    <note bind="retroarch:intro"/>
    <choice label="Aspect ratio" bind="retroarch:row:aspect" options="retroarch:choices:aspect"/>
    <choice label="Smooth picture" bind="retroarch:row:smooth" options="retroarch:choices:smooth"/>
    <choice label="Shader" bind="retroarch:row:shader" options="retroarch:choices:shader"/>
    <choice label="Bezel (overlay)" bind="retroarch:row:overlay" options="retroarch:choices:overlay"/>
    <choice label="Integer scaling" bind="retroarch:row:integer" options="retroarch:choices:integer"/>
    <choice label="Run-ahead (less input lag)" bind="retroarch:row:runahead" options="retroarch:choices:runahead"/>
    <choice label="Volume" bind="retroarch:row:volume" options="retroarch:choices:volume"/>
    <choice label="Resume where you left off" bind="retroarch:row:resume" options="retroarch:choices:resume"
            help="Saves the game's state when RetroArch closes and loads it when the game starts."/>
    <note bind="retroarch:disabledNote"/>
    <value label="Kept in" bind="retroarch:file"/>
  </section>
  <actions>
    <button label="Save" action="retroarch.save" visibleWhen="retroarch:isReady"/>
    <button label="Reset" action="retroarch.reset" visibleWhen="retroarch:isDirty"/>
  </actions>
  <actions>
    {useCore}<button label="Reset to original" action="retroarch.restore" visibleWhen="retroarch:canRestore"
            confirm="Put back these settings as they were before Manage changed them?"/>
  </actions>
</form>
)XML";

const char* const optionsForm = R"XML(
<form id="{p}-core-options" title="Core options" bind="retroarch:screenTitle">
  <note bind="retroarch:why"/>
  <note bind="retroarch:intro"/>
  <results bind="retroarch:coreOptions" open="{p}-core-option" param="key" visibleWhen="retroarch:isReady"
           empty="Manage has no options for this core yet. RetroArch's own menu has them all (Quick Menu, Core Options)."/>
  <note bind="retroarch:disabledNote"/>
  <value label="Kept in" bind="retroarch:file"/>
  <actions>
    {useCore}<button label="Reset to original" action="retroarch.restore" visibleWhen="retroarch:canRestore"
            confirm="Put back these options as they were before Manage changed them?"/>
  </actions>
</form>
)XML";

const char* const optionForm = R"XML(
<form id="{p}-core-option" title="Core option" bind="retroarch:optionTitle">
  <note bind="retroarch:why"/>
  <choice label="Value" bind="retroarch:opt" options="retroarch:optChoices"/>
  <note bind="retroarch:optionNote"/>
  <value label="Kept in" bind="retroarch:file"/>
  <actions>
    <button label="Save" action="retroarch.save" visibleWhen="retroarch:isReady"/>
    <button label="Reset" action="retroarch.reset" visibleWhen="retroarch:isDirty"/>
  </actions>
</form>
)XML";

const char* const controlsForm = R"XML(
<form id="{p}-controls" title="Controls" bind="retroarch:screenTitle">
  <note bind="retroarch:why"/>
  <section label="Player 1" visibleWhen="retroarch:isReady">
    <note bind="retroarch:intro"/>
    <choice label="Stick moves the D-pad" bind="retroarch:row:dpad" options="retroarch:choices:dpad"/>
    <choice label="A button sends" bind="retroarch:row:btn_a" options="retroarch:choices:btn_a"/>
    <choice label="B button sends" bind="retroarch:row:btn_b" options="retroarch:choices:btn_b"/>
    <choice label="X button sends" bind="retroarch:row:btn_x" options="retroarch:choices:btn_x"/>
    <choice label="Y button sends" bind="retroarch:row:btn_y" options="retroarch:choices:btn_y"/>
    <choice label="L button sends" bind="retroarch:row:btn_l" options="retroarch:choices:btn_l"/>
    <choice label="R button sends" bind="retroarch:row:btn_r" options="retroarch:choices:btn_r"/>
    <choice label="L2 button sends" bind="retroarch:row:btn_l2" options="retroarch:choices:btn_l2"/>
    <choice label="R2 button sends" bind="retroarch:row:btn_r2" options="retroarch:choices:btn_r2"/>
    <choice label="Select button sends" bind="retroarch:row:btn_select" options="retroarch:choices:btn_select"/>
    <choice label="Start button sends" bind="retroarch:row:btn_start" options="retroarch:choices:btn_start"/>
    <note bind="retroarch:disabledNote"/>
    <value label="Kept in" bind="retroarch:file"/>
  </section>
  <actions>
    <button label="Save" action="retroarch.save" visibleWhen="retroarch:isReady"/>
    <button label="Reset" action="retroarch.reset" visibleWhen="retroarch:isDirty"/>
  </actions>
  <actions>
    {useCore}<button label="Reset to original" action="retroarch.restore" visibleWhen="retroarch:canRestore"
            confirm="Put back these controls as they were before Manage changed them?"/>
  </actions>
</form>
)XML";

const char* const templates[][2] = {
    { "-retroarch", hubForm },
    { "-core-options", optionsForm },
    { "-core-option", optionForm },
    { "-controls", controlsForm },
};

using Words = std::vector<std::pair<std::string, std::string>>;

Words gameWords()
{
    return {
        { "{p}", "game" },
        { "{useCore}", "<button label=\"Use the system's settings\" action=\"retroarch.useCore\" visibleWhen=\"retroarch:canUseCore\"\n"
                       "            confirm=\"Use the system's settings? This game's own file goes (a copy is kept in manage/backups).\"/>\n    " },
    };
}

Words systemWords()
{
    return { { "{p}", "system" }, { "{useCore}", "" } };
}

std::string expand(std::string text, const Words& words)
{
    for (const auto& [from, to] : words) {
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
            text.replace(at, from.size(), to);
        }
    }
    return text;
}

struct Table
{
    std::vector<std::string> ids;
    std::vector<std::string> xml;
    Forms::BuiltinTable table;

    explicit Table(const Words& words)
    {
        std::string base = words.front().second;
        for (const auto& entry : templates) {
            ids.push_back(base + entry[0]);
            xml.push_back(expand(entry[1], words));
        }
        for (size_t i = 0; i < ids.size(); i++) {
            table.push_back({ ids[i].c_str(), xml[i].c_str() });
        }
    }
};

}

const Forms::BuiltinTable& Manager::retroArchForms(bool system)
{
    static const Table games(gameWords());
    static const Table systems(systemWords());
    return system ? systems.table : games.table;
}

const Forms::BuiltinTable& Manager::retroArchLauncherForms()
{
    static const Table launchers(Words{ { "{p}", "emulator" }, { "{useCore}", "" } });
    return launchers.table;
}

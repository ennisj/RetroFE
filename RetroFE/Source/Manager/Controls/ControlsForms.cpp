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

// Settings > Controls, served by ControlsProvider (prefix "control")
namespace
{

// Every control by group, with its inputs; Save writes them all
const char* const controlsForm = R"xml(
<form id="controls" title="Controls">
  <note bind="control:intro"/>
  <note bind="control:problems"/>
  <section label="Essential">
    <results bind="control:group:Essential" open="control"/>
  </section>
  <section label="Navigation">
    <results bind="control:group:Navigation" open="control"/>
  </section>
  <section label="Playlists and favourites">
    <results bind="control:group:Playlists" open="control"/>
  </section>
  <section label="Admin">
    <results bind="control:group:Admin" open="control"/>
  </section>
  <section label="Advanced">
    <results bind="control:group:Advanced" open="control"/>
  </section>
  <section label="Combos">
    <results bind="control:group:Combos" open="control"/>
  </section>
  <section label="Jukebox">
    <results bind="control:group:Jukebox" open="control"/>
  </section>
  <section label="Sticks and triggers">
    <number label="Dead zone" bind="control:deadZone" min="0" max="100"
            help="How far, in percent, a stick or trigger must move before it counts."/>
  </section>
  <actions>
    <button label="Save" action="control.save"/>
    <button label="Undo changes" action="control.reset" visibleWhen="control:isDirty"/>
  </actions>
</form>
)xml";

// One control (param:id): its inputs, add one by pressing it, the default
const char* const controlForm = R"xml(
<form id="control" title="Control" bind="control:title">
  <note bind="control:help"/>
  <section label="Inputs">
    <results bind="control:inputs" open="control-input" param="input" empty="No input: this control does nothing."/>
  </section>
  <note bind="control:notes"/>
  <button label="Add an input (press it now)" action="control.capture"/>
  <note value="Then press the key, button, stick or trigger within 5 seconds. Holding one down for 2 seconds cancels."/>
  <section label="Default">
    <value label="RetroFE's default" bind="control:defaultText"/>
    <button label="Put back the default" action="control.default"/>
  </section>
  <actions>
    <button label="Save" action="control.save"/>
    <button label="Undo changes" action="control.reset" visibleWhen="control:isDirty"/>
  </actions>
</form>
)xml";

// One input of a control (param:id, param:input, the token)
const char* const controlInputForm = R"xml(
<form id="control-input" title="Input" bind="control:inputTitle">
  <value label="Control" bind="control:title"/>
  <value label="In controls.conf" bind="control:inputToken"/>
  <note bind="control:inputNotes"/>
  <button label="Remove this input" action="control.remove"/>
</form>
)xml";

}

namespace Forms
{

BuiltinTable controlsForms()
{
    return {
        { "controls", controlsForm },
        { "control", controlForm },
        { "control-input", controlInputForm },
    };
}

}

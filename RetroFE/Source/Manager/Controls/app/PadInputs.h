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
#include <vector>

namespace Manager
{
    // Services::sameInputs: the other names one input has on the game
    // controllers open now, from SDL's mappings. joyA on a DualShock read
    // through DirectInput is joyButton1; joyButton1 there is joyA. Unnumbered
    // tokens, each once.
    std::vector<std::string> padSameInputs(const std::string& token);
}

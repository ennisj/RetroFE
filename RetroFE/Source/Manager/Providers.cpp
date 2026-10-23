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
#include "Provider.h"
#include "ManagerModel.h"
#include <memory>

// The providers behind the Manage sections (see Provider.h and
// docs/design/manage-forms.md). Each section has a slot for its include and
// one for its registration, e.g.
//
//   #include "Games/GameProvider.h"
//   model.addProvider(std::make_unique<GameProvider>(services));
//
// Replace your section's slot lines and keep the blank lines between slots,
// so sections added at the same time merge without conflicts.

// games

// menus

// systems

// videos

// emulators

// themes
#include "Themes/ThemeProvider.h"

void Manager::registerProviders(ManagerModel& model, const Services& services)
{
    (void)model;
    (void)services;

    // games

    // menus

    // systems

    // videos

    // emulators

    // themes
    model.addProvider(Themes::makeProvider(services));
}

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

// retrofe_tests: unit tests for code that doesn't need SDL.
// Build with -DRETROFE_BUILD_TESTS=ON.

#include "TestSupport.h"

int main()
{
    runFormsTests();
    runCatalogTests();
    runThemeEditorTests();

    std::cout << std::endl << Test::checks - Test::failures << "/" << Test::checks << " checks passed" << std::endl;
    return Test::failures == 0 ? 0 : 1;
}

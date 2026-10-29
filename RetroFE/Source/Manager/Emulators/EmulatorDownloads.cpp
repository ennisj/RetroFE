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
#include "EmulatorDownloads.h"

Forms::Row Manager::emulatorDownloadRow(const Catalog::Emulator& emulator, const Catalog::Entry* entry,
                                      const std::vector<std::string>& warnings, bool checking, bool supported)
{
    Forms::Row row;
    row.id = "emulator-" + emulator.id;
    row.label = emulator.name;
    row.params["listed"] = "yes";
    if (entry) {
        row.detail = entry->updateAvailable ? "Update to " + entry->package.version
                   : !entry->installedVersion.empty() ? "Installed " + entry->installedVersion
                   : entry->package.version + " available";
    }
    else {
        row.detail = !supported ? "Windows only" : checking ? "Checking..." : "Not offered by this release";
        for (const auto& warning : warnings) {
            if (warning.find(emulator.name) != std::string::npos || warning.find("limit") != std::string::npos) {
                row.detail = warning.find("no SHA-256 digest") != std::string::npos ? "No checksum on this release"
                           : warning.find("limit") != std::string::npos ? "GitHub's rate limit reached" : warning;
                break;
            }
        }
    }
    return row;
}

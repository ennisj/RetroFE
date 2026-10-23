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

#include "../Library/ConfFile.h"
#include <string>

// Reads and edits the global settings files the way RetroFE loads them:
// settings.conf, then settings1.conf .. settings15.conf, then
// settings_saved.conf, with later definitions winning. A value is written back
// to the file it effectively comes from, so overrides keep working, and only
// that line changes: comments, order and line endings are kept.
// (A thin wrapper over Library::ConfLayers, which does the work.)
class SettingsFile
{
public:
    explicit SettingsFile(const std::string& root);

    // Re-reads all the files
    void load();

    bool has(const std::string& key) const;

    // The effective value, or "" if no file sets it
    std::string get(const std::string& key) const;

    // Name of the file the effective value comes from, e.g. "settings1.conf"
    std::string source(const std::string& key) const;

    // Creates settings.conf with initialContent if it doesn't exist yet
    bool ensureExists(const std::string& initialContent, std::string& error);

    // Writes a value. If no file sets the key it's added to settings.conf,
    // which is created from initialContent first if it doesn't exist yet.
    bool set(const std::string& key, const std::string& value, const std::string& initialContent, std::string& error);

private:
    Library::ConfLayers layers_;  // in load order; file 0 is settings.conf
};

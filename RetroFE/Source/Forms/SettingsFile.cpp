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
#include "SettingsFile.h"

SettingsFile::SettingsFile(const std::string& root)
    : layers_(root, Library::ConfLayers::globalFileNames())
{
}

void SettingsFile::load()
{
    layers_.load();
}

bool SettingsFile::has(const std::string& key) const
{
    return layers_.has(key);
}

std::string SettingsFile::get(const std::string& key) const
{
    return layers_.get(key);
}

std::string SettingsFile::source(const std::string& key) const
{
    return layers_.source(key);
}

bool SettingsFile::ensureExists(const std::string& initialContent, std::string& error)
{
    return layers_.ensureExists(0, initialContent, error);
}

bool SettingsFile::set(const std::string& key, const std::string& value, const std::string& initialContent, std::string& error)
{
    return layers_.set(key, value, error, 0, initialContent);
}

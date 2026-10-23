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

#include <atomic>
#include <map>
#include <string>
#include <vector>

namespace Library
{
    // The flat key/value map RetroFE builds at start-up (ImportConfiguration
    // in Main.cpp), rebuilt without the Configuration singleton so it can run
    // on a worker thread against any root. Each value remembers the file it
    // came from, so an editor knows where to write it back.
    //
    // Lookups behave like Configuration's: get() expands %BASE_MEDIA_PATH% and
    // %BASE_ITEM_PATH% from the raw global values (default <root>/collections),
    // absolute paths resolve against the root, booleans accept yes/true/on.
    class Properties
    {
    public:
        struct Value
        {
            std::string value;
            std::string file;  // conf file it came from; "" for built-in defaults or values RetroFE derives
        };

        Properties() = default;
        explicit Properties(std::string root);

        // Loads everything ImportConfiguration loads (no command-line settings).
        // os is "windows", "linux" or "apple" (the launchers.<os> folders).
        // Returns false only if cancel was set.
        bool load(const std::string& os, const std::atomic<bool>* cancel = nullptr);

        // Only the global settings files (settings.conf .. settings_saved.conf)
        void loadGlobal();

        const std::string& root() const { return root_; }

        bool getRaw(const std::string& key, std::string& value) const;
        bool get(const std::string& key, std::string& value) const;
        std::string getOr(const std::string& key, const std::string& fallback = "") const;
        bool getBool(const std::string& key, bool& value) const;
        bool getAbsolutePath(const std::string& key, std::string& value) const;
        bool exists(const std::string& key) const;
        bool prefixExists(const std::string& prefix) const;
        // The file a key's value came from ("" when unknown or built-in)
        std::string sourceFile(const std::string& key) const;

        void set(const std::string& key, const std::string& value, const std::string& file = "");

        // Configuration::getCollectionAbsolutePath: list.path, else baseItemPath/<c>, else <root>/collections/<c>/roms
        std::string collectionPath(const std::string& collection) const;

        // Configuration::getMediaPropertyAbsolutePath: media.<type> (or media.system_artwork),
        // then the fork-compat media.basePath, then baseMediaPath/<c>/{medium_artwork/<type>|system_artwork}
        std::string mediaPath(const std::string& collection, const std::string& type, bool system) const;

        // Configuration::convertToAbsolutePath
        std::string absolute(const std::string& path) const;

        // Reads one conf file into the map: Configuration::import(collection, prefix, file)
        bool importFile(const std::string& collection, const std::string& prefix, const std::string& path);
        void importText(const std::string& prefix, const std::string& text, const std::string& file);

        // Every key starting with prefix + "."
        std::vector<std::string> keysWithPrefix(const std::string& prefix) const;

    private:
        void parseLine(const std::string& collection, std::string prefix, std::string line, const std::string& file);
        std::string expand(const std::string& value) const;

        std::string root_;
        std::map<std::string, Value> values_;
        std::string baseMediaPath_;
        std::string baseItemPath_;
    };
}

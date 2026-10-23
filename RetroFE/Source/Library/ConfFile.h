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

#include "Files.h"
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Library
{
    class Backups;

    // One "key = value" file (settings.conf, info.conf, a launcher .conf),
    // parsed the way Configuration::parseLine reads it: '#' starts a comment
    // anywhere on a line, key and value are trimmed of spaces and tabs, lines
    // without '=' are ignored. RetroFE lets the last definition of a key win
    // (Wins::Last); Wins::First is for readers that take the first one.
    //
    // Edits change only the lines they touch: a value is replaced keeping the
    // spacing around it and any trailing comment; new keys are appended with
    // the file's own line endings. save() writes atomically.
    class ConfFile
    {
    public:
        enum class Wins { Last, First };

        ConfFile() = default;
        explicit ConfFile(std::string path, Wins wins = Wins::Last);

        // Reads the file; a missing file loads as empty (exists() is false)
        bool load(std::string* error = nullptr);
        // Replaces the content in memory, e.g. to create a file from a template
        void setText(const std::string& text);

        const std::string& path() const { return path_; }
        bool exists() const { return exists_; }
        bool dirty() const { return dirty_; }
        std::string text() const { return lines_.join(); }

        bool has(const std::string& key) const;
        std::optional<std::string> get(const std::string& key) const;
        std::string get(const std::string& key, const std::string& fallback) const;

        // Effective key/value pairs in the order of the lines that define them
        std::vector<std::pair<std::string, std::string>> entries() const;

        // Sets a value in memory; false with a reason when key or value can't be written
        bool set(const std::string& key, const std::string& value, std::string& error);
        // Removes every line that defines key; false if none did
        bool remove(const std::string& key);

        // Writes the file if it changed (or doesn't exist yet). With backups,
        // the original is kept before the first change. A file that was
        // missing when loaded but exists now is never overwritten (an error).
        bool save(std::string& error, Backups* backups = nullptr);

        // RetroFE's parsing of one line, without the line ending
        static bool parseLine(const std::string& line, std::string& key, std::string& value);
        // Empty when writable, otherwise why not
        static std::string checkKey(const std::string& key);
        static std::string checkValue(const std::string& value);

    private:
        void reindex();

        std::string path_;
        Wins wins_ = Wins::Last;
        Lines lines_;
        bool exists_ = false;
        bool dirty_ = false;
        std::map<std::string, size_t> effective_;  // key -> index of the line holding its value
    };

    // Several conf files read in order into one set of keys, later files
    // winning: the global settings.conf, settings1..15.conf and
    // settings_saved.conf, or a collection's settings.conf, settings1..15.conf
    // and info.conf. A value is written back to the file it effectively comes
    // from, so overrides keep working.
    class ConfLayers
    {
    public:
        ConfLayers() = default;
        // fileNames are inside folder, in load order
        ConfLayers(std::string folder, std::vector<std::string> fileNames);

        // The global settings files, in Main.cpp's order
        static std::vector<std::string> globalFileNames();
        // A collection's settings files, in Main.cpp's order (info.conf last, so it wins)
        static std::vector<std::string> collectionFileNames();

        void load();

        bool has(const std::string& key) const;
        std::string get(const std::string& key) const;
        // Name of the file the effective value comes from ("" if none)
        std::string source(const std::string& key) const;
        int sourceIndex(const std::string& key) const;

        size_t size() const { return files_.size(); }
        ConfFile& file(size_t index) { return files_[index]; }
        const ConfFile& file(size_t index) const { return files_[index]; }
        const std::string& fileName(size_t index) const { return names_[index]; }
        int indexOf(const std::string& fileName) const;

        // Creates file index from initialContent if it doesn't exist yet; if
        // something else created it since load(), reads it instead
        bool ensureExists(size_t index, const std::string& initialContent, std::string& error, Backups* backups = nullptr);

        // Writes key where its effective value comes from; when no file sets it,
        // to file targetIfMissing (created first, from initialContent, if missing)
        bool set(const std::string& key, const std::string& value, std::string& error, size_t targetIfMissing = 0,
                 const std::string& initialContent = "", Backups* backups = nullptr);

        // Removes key from every file that sets it
        bool remove(const std::string& key, std::string& error, Backups* backups = nullptr);

    private:
        void reindex();

        std::string folder_;
        std::vector<std::string> names_;
        std::vector<ConfFile> files_;
        std::map<std::string, size_t> keys_;  // key -> index of the file its effective value is in
    };
}

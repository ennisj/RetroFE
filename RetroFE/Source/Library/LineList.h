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
#include <string>
#include <vector>

namespace Library
{
    class Backups;

    // A one-entry-per-line file: menu.txt, include.txt, exclude.txt,
    // <source>.sub and playlists/<name>.txt. Entries are read exactly as
    // RetroFE reads them (CollectionInfoBuilder::ImportBasicList and
    // MenuParser): everything from '#' on is a comment, '\r' is dropped, and a
    // line with nothing left is skipped. Entries are NOT trimmed: "Arcade " is
    // a different name from "Arcade" to RetroFE.
    //
    // Edits keep every other line byte for byte (comments, blank lines, line
    // endings, a missing final line break), and save() writes atomically.
    class LineList
    {
    public:
        enum class Match { Exact, Trimmed, IgnoreCase };  // IgnoreCase also trims

        LineList() = default;
        explicit LineList(std::string path);

        bool load(std::string* error = nullptr);
        const std::string& path() const { return path_; }
        bool exists() const { return exists_; }
        bool dirty() const { return dirty_; }
        std::string text() const { return lines_.join(); }

        // The entries in file order
        std::vector<std::string> entries() const;
        size_t size() const;

        // Position of entry in entries(), or -1
        int find(const std::string& entry, Match match = Match::Exact) const;

        // Adds a line before the entry at position (-1 or past the end: at the end of the file)
        bool add(const std::string& entry, std::string& error, int position = -1);
        // Removes every line holding entry; false if there was none
        bool remove(const std::string& entry, Match match = Match::Exact);
        // Moves the entry at from to position to; comments and blank lines stay where they are
        bool move(int from, int to);

        bool save(std::string& error, Backups* backups = nullptr);

        // Empty when entry can be written as a line, otherwise why not
        static std::string checkEntry(const std::string& entry);

        // RetroFE's entry for a line, or "" when the line holds none
        static std::string entryOf(const std::string& line);

        // "_<Collection>:<name>" entries (playlists, exclude_all.txt) name a
        // game of another collection; plain entries belong to the list's own.
        struct Qualified
        {
            std::string collection;
            std::string name;
            bool qualified = false;
        };
        static Qualified parseQualified(const std::string& entry, const std::string& ownCollection);
        // The entry for a game in a list owned by listCollection
        static std::string qualify(const std::string& gameCollection, const std::string& name,
                                   const std::string& listCollection);

    private:
        std::vector<size_t> entryLines() const;
        static bool matches(const std::string& entry, const std::string& wanted, Match match);

        std::string path_;
        Lines lines_;
        bool exists_ = false;
        bool dirty_ = false;
    };
}

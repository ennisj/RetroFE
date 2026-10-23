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

#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct sqlite3;

namespace Library
{
    // User metadata edits that survive a metadata re-import.
    //
    // meta.db is a cache that RetroFE rebuilds from the shipped XML in meta/
    // (hyperlist, mamelist, emuarc), and those files belong to the content: an
    // update overwrites them. So edits are kept in a separate layer:
    //
    //   <root>/manage/metadata/<metadata.type>.xml
    //
    // in hyperlist format, holding only the fields that were changed:
    //
    //   <menu>
    //     <game name="pacman"><description>Pac-Man (fixed)</description></game>
    //   </menu>
    //
    // RetroFE applies every file there right after importing meta/ (see
    // MetadataDatabase::importDirectory), field by field, so a shipped update
    // to the other fields still comes through. The file name's stem up to the
    // first '.' is the metadata type, as for meta/hyperlist. Element names are
    // hyperlist's: description (the title), year, manufacturer, developer,
    // genre, players, ctrltype, buttons, joyways, cloneof, rating, score.
    //
    // Edits are also written straight into meta.db, so they show on the next
    // reload without a full re-import.
    namespace MetadataOverrides
    {
        struct Field
        {
            const char* element;  // hyperlist element
            const char* column;   // Meta table column
            const char* label;    // for screens
        };
        // Every field, in display order
        const std::vector<Field>& fields();
        const Field* findField(const std::string& elementOrColumn);

        // name -> element -> value
        using Overrides = std::map<std::string, std::map<std::string, std::string>>;

        std::string folder(const std::string& root);
        std::string fileFor(const std::string& root, const std::string& metadataType);

        // Reads one override file; a missing file is empty
        bool read(const std::string& file, Overrides& overrides, std::string& error);
        // Writes it (atomically); an empty set removes the file
        bool write(const std::string& file, const Overrides& overrides, std::string& error);

        // Empty when value can be stored, otherwise why not
        std::string checkValue(const std::string& element, const std::string& value);

        // Applies one game's overrides to the Meta table. A game with no row
        // gets one, titled with its name unless the title is overridden.
        bool apply(sqlite3* db, const std::string& metadataType, const std::string& name,
                   const std::map<std::string, std::string>& values, std::string& error);

        // Applies every file in the folder; returns how many games were updated.
        // RetroFE calls this after importing meta/.
        int applyAll(sqlite3* db, const std::string& root);

        // The newest change time of the override folder and its files (for
        // RetroFE's refresh check); file_time_type::min() when there's none
        std::filesystem::file_time_type newestChange(const std::string& root);

        // The row RetroFE's import would give a game from the shipped files, read
        // the way MetadataDatabase::importDirectory reads them: meta/hyperlist/
        // and meta/mamelist/ (<type>*.xml), then meta/emuarc/*.dat (by header
        // name), each replacing the whole row. Fields an importer doesn't set
        // come back empty, as in the table.
        enum class Shipped
        {
            Found,     // values holds every field
            NotFound,  // no shipped file lists the game: its row came from the overrides alone
            Unknown,   // a matching file couldn't be read: don't guess
        };
        Shipped shipped(const std::string& root, const std::string& metadataType, const std::string& name,
                        std::map<std::string, std::string>& values, std::string* error = nullptr);

        // Deletes a game's row (for a game whose only metadata was the user's)
        bool removeRow(sqlite3* db, const std::string& metadataType, const std::string& name, std::string& error);
    }
}

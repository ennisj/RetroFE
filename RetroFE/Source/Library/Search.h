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

#include "Index.h"
#include <memory>
#include <string>
#include <vector>

namespace Library
{
    struct GameQuery
    {
        std::string text;             // case-insensitive substring of the name or title; "" = all
        std::string collection;       // only games under this collection (system, merged or menu); "" = all
        bool includeMissing = true;   // listed-but-missing games (include.txt + includeMissingItems)
        bool onlyWithoutMetadata = false;
        size_t offset = 0;
        size_t limit = 200;           // rows returned; total counts them all

        bool operator==(const GameQuery& o) const
        {
            return text == o.text && collection == o.collection && includeMissing == o.includeMissing
                && onlyWithoutMetadata == o.onlyWithoutMetadata && offset == o.offset && limit == o.limit;
        }
    };

    struct CollectionQuery
    {
        std::string text;             // case-insensitive substring of the name or title
        unsigned kinds = 0;           // any of these Kind flags; 0 = any
        std::string under;            // only collections below this menu; "" = all
        bool includeReserved = false; // Home / Catalog / Favorites
        size_t offset = 0;
        size_t limit = 200;

        bool operator==(const CollectionQuery& o) const
        {
            return text == o.text && kinds == o.kinds && under == o.under && includeReserved == o.includeReserved
                && offset == o.offset && limit == o.limit;
        }
    };

    template <typename Row>
    struct Results
    {
        std::vector<Row> rows;        // indices into Index::games / Index::collections
        size_t total = 0;             // matches before offset/limit
    };

    // Games are listed by collection, then title (Index order); collections by name
    Results<int> searchGames(const Index& index, const GameQuery& query);
    Results<int> searchCollections(const Index& index, const CollectionQuery& query);

    // Keeps a screen's last query and its results, so calling it every frame
    // costs nothing until the query or the snapshot changes
    class Search
    {
    public:
        const Results<int>& games(const std::shared_ptr<const Index>& index, const GameQuery& query);
        const Results<int>& collections(const std::shared_ptr<const Index>& index, const CollectionQuery& query);

    private:
        std::shared_ptr<const Index> gameIndex_;
        GameQuery gameQuery_;
        Results<int> gameResults_;
        bool hasGames_ = false;

        std::shared_ptr<const Index> collectionIndex_;
        CollectionQuery collectionQuery_;
        Results<int> collectionResults_;
        bool hasCollections_ = false;

        // The collection filter's game mask, kept while the collection stays the same
        std::shared_ptr<const Index> maskIndex_;
        std::string maskCollection_;
        std::vector<char> mask_;
    };
}

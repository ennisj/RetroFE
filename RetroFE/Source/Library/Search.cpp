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
#include "Search.h"
#include "Files.h"

namespace Library
{

namespace
{
    Results<int> filterGames(const Index& index, const GameQuery& query, const std::vector<char>* mask)
    {
        Results<int> results;
        std::string needle = toLower(query.text);
        for (size_t i = 0; i < index.games.size(); i++) {
            const GameRecord& game = index.games[i];
            if ((mask && !(*mask)[i]) || (!query.includeMissing && game.missing)
                || (query.onlyWithoutMetadata && game.hasMetadata) || !icontains(game.searchText, needle)) {
                continue;
            }
            if (results.total >= query.offset && results.rows.size() < query.limit) {
                results.rows.push_back(static_cast<int>(i));
            }
            results.total++;
        }
        return results;
    }
}

Results<int> searchGames(const Index& index, const GameQuery& query)
{
    if (query.collection.empty()) {
        return filterGames(index, query, nullptr);
    }
    int collection = index.findCollection(query.collection);
    if (collection < 0) {
        return {};
    }
    std::vector<char> mask = index.gameMask(collection);
    return filterGames(index, query, &mask);
}

Results<int> searchCollections(const Index& index, const CollectionQuery& query)
{
    Results<int> results;
    std::string needle = toLower(query.text);
    std::vector<char> under;
    if (!query.under.empty()) {
        under.assign(index.collections.size(), 0);
        int top = index.findCollection(query.under);
        if (top < 0) {
            return results;
        }
        for (const MenuRow& row : index.menuTree(index.collections[static_cast<size_t>(top)].name)) {
            if (row.collection >= 0 && row.collection != top) {
                under[static_cast<size_t>(row.collection)] = 1;
            }
        }
    }
    for (size_t i = 0; i < index.collections.size(); i++) {
        const CollectionRecord& record = index.collections[i];
        if ((query.kinds && !(record.kinds & query.kinds)) || (!query.includeReserved && record.reserved)
            || (!under.empty() && !under[i])) {
            continue;
        }
        if (!icontains(toLower(record.name), needle) && !icontains(toLower(record.title), needle)) {
            continue;
        }
        if (results.total >= query.offset && results.rows.size() < query.limit) {
            results.rows.push_back(static_cast<int>(i));
        }
        results.total++;
    }
    return results;
}

const Results<int>& Search::games(const std::shared_ptr<const Index>& index, const GameQuery& query)
{
    if (!index) {
        gameResults_ = {};
        hasGames_ = false;
        return gameResults_;
    }
    if (hasGames_ && index == gameIndex_ && query == gameQuery_) {
        return gameResults_;
    }
    const std::vector<char>* mask = nullptr;
    if (!query.collection.empty()) {
        if (maskIndex_ != index || maskCollection_ != query.collection) {
            int collection = index->findCollection(query.collection);
            mask_ = collection >= 0 ? index->gameMask(collection) : std::vector<char>(index->games.size(), 0);
            maskIndex_ = index;
            maskCollection_ = query.collection;
        }
        mask = &mask_;
    }
    gameResults_ = filterGames(*index, query, mask);
    gameIndex_ = index;
    gameQuery_ = query;
    hasGames_ = true;
    return gameResults_;
}

const Results<int>& Search::collections(const std::shared_ptr<const Index>& index, const CollectionQuery& query)
{
    if (!index) {
        collectionResults_ = {};
        hasCollections_ = false;
        return collectionResults_;
    }
    if (hasCollections_ && index == collectionIndex_ && query == collectionQuery_) {
        return collectionResults_;
    }
    collectionResults_ = searchCollections(*index, query);
    collectionIndex_ = index;
    collectionQuery_ = query;
    hasCollections_ = true;
    return collectionResults_;
}

}

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
#include "LineList.h"
#include "Backups.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{

LineList::LineList(std::string path)
    : path_(std::move(path))
{
}

bool LineList::load(std::string* error)
{
    lines_ = Lines();
    dirty_ = false;
    std::error_code ec;
    exists_ = fs::is_regular_file(path_, ec);
    if (!exists_) {
        return true;
    }
    std::string content;
    if (!readFile(path_, content)) {
        exists_ = false;
        if (error) {
            *error = "Could not read " + path_;
        }
        return false;
    }
    lines_ = Lines::parse(content);
    return true;
}

std::string LineList::entryOf(const std::string& line)
{
    // filterComments also removes '\r', as ImportBasicList relies on
    return Utils::filterComments(line);
}

std::vector<size_t> LineList::entryLines() const
{
    std::vector<size_t> result;
    for (size_t i = 0; i < lines_.lines.size(); i++) {
        if (!entryOf(lines_.lines[i].text).empty()) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<std::string> LineList::entries() const
{
    std::vector<std::string> result;
    for (size_t i : entryLines()) {
        result.push_back(entryOf(lines_.lines[i].text));
    }
    return result;
}

size_t LineList::size() const
{
    return entryLines().size();
}

bool LineList::matches(const std::string& entry, const std::string& wanted, Match match)
{
    switch (match) {
    case Match::Exact:
        return entry == wanted;
    case Match::Trimmed:
        return Utils::trimEnds(entry) == Utils::trimEnds(wanted);
    case Match::IgnoreCase:
        return iequals(Utils::trimEnds(entry), Utils::trimEnds(wanted));
    }
    return false;
}

int LineList::find(const std::string& entry, Match match) const
{
    std::vector<std::string> all = entries();
    for (size_t i = 0; i < all.size(); i++) {
        if (matches(all[i], entry, match)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string LineList::checkEntry(const std::string& entry)
{
    if (Utils::trimEnds(entry).empty()) {
        return "The entry is empty.";
    }
    if (Utils::trimEnds(entry) != entry) {
        return "An entry can't start or end with a space.";
    }
    if (entry.find('#') != std::string::npos) {
        return "An entry can't contain # (RetroFE reads the rest of the line as a comment).";
    }
    if (entry.find_first_of("\r\n") != std::string::npos) {
        return "An entry must be a single line.";
    }
    return "";
}

bool LineList::add(const std::string& entry, std::string& error, int position)
{
    error = checkEntry(entry);
    if (!error.empty()) {
        return false;
    }
    std::vector<size_t> positions = entryLines();
    if (position < 0 || static_cast<size_t>(position) >= positions.size()) {
        lines_.append(entry);
    }
    else {
        lines_.insert(positions[static_cast<size_t>(position)], entry);
    }
    dirty_ = true;
    return true;
}

bool LineList::remove(const std::string& entry, Match match)
{
    bool removed = false;
    std::vector<size_t> positions = entryLines();
    for (size_t i = positions.size(); i-- > 0;) {
        if (matches(entryOf(lines_.lines[positions[i]].text), entry, match)) {
            lines_.erase(positions[i]);
            removed = true;
        }
    }
    dirty_ |= removed;
    return removed;
}

bool LineList::move(int from, int to)
{
    std::vector<size_t> positions = entryLines();
    int count = static_cast<int>(positions.size());
    if (from < 0 || from >= count || to < 0 || to >= count) {
        return false;
    }
    if (from == to) {
        return true;
    }
    // Reorder the entry lines' text among the same slots; each slot keeps its
    // line ending, and comments and blank lines between entries don't move
    std::vector<std::string> texts;
    for (size_t i : positions) {
        texts.push_back(lines_.lines[i].text);
    }
    std::string moving = texts[static_cast<size_t>(from)];
    texts.erase(texts.begin() + from);
    texts.insert(texts.begin() + to, moving);
    for (size_t i = 0; i < positions.size(); i++) {
        lines_.lines[positions[i]].text = texts[i];
    }
    dirty_ = true;
    return true;
}

bool LineList::save(std::string& error, Backups* backups)
{
    if (!dirty_ && exists_) {
        return true;
    }
    if (backups && !backups->beforeChange(path_, error)) {
        return false;
    }
    if (!writeFileAtomic(path_, lines_.join(), error)) {
        return false;
    }
    exists_ = true;
    dirty_ = false;
    LOG_INFO("Library", "Saved " + path_);
    return true;
}

LineList::Qualified LineList::parseQualified(const std::string& entry, const std::string& ownCollection)
{
    // As CollectionInfoBuilder::loadPlaylistItems: "_<collection>:<name>"
    Qualified result;
    result.collection = ownCollection;
    result.name = entry;
    if (!entry.empty() && entry[0] == '_') {
        size_t colon = entry.find(':');
        if (colon != std::string::npos) {
            result.collection = entry.substr(1, colon - 1);
            result.name = entry.substr(colon + 1);
            result.qualified = true;
        }
        else {
            result.name = entry.substr(1);
        }
    }
    return result;
}

std::string LineList::qualify(const std::string& gameCollection, const std::string& name, const std::string& listCollection)
{
    if (gameCollection == listCollection && (name.empty() || name[0] != '_')) {
        return name;
    }
    return "_" + gameCollection + ":" + name;
}

}

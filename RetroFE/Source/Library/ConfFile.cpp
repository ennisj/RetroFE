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
#include "ConfFile.h"
#include "Backups.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{

ConfFile::ConfFile(std::string path, Wins wins)
    : path_(std::move(path))
    , wins_(wins)
{
}

bool ConfFile::load(std::string* error)
{
    lines_ = Lines();
    dirty_ = false;
    std::error_code ec;
    exists_ = fs::is_regular_file(path_, ec);
    if (exists_) {
        std::string content;
        if (!readFile(path_, content)) {
            if (error) {
                *error = "Could not read " + path_;
            }
            exists_ = false;
            reindex();
            return false;
        }
        lines_ = Lines::parse(content);
    }
    reindex();
    return true;
}

void ConfFile::setText(const std::string& text)
{
    lines_ = Lines::parse(text);
    dirty_ = true;
    reindex();
}

bool ConfFile::parseLine(const std::string& line, std::string& key, std::string& value)
{
    // Same steps as Configuration::parseLine (without a key prefix)
    std::string content = Utils::filterComments(line);
    size_t equals = content.find('=');
    if (equals == std::string::npos) {
        return false;
    }
    key = Utils::trimEnds(content.substr(0, equals));
    value = Utils::trimEnds(content.substr(equals + 1));
    return !key.empty();
}

void ConfFile::reindex()
{
    effective_.clear();
    for (size_t i = 0; i < lines_.lines.size(); i++) {
        std::string key;
        std::string value;
        if (parseLine(lines_.lines[i].text, key, value)) {
            if (wins_ == Wins::Last || !effective_.count(key)) {
                effective_[key] = i;
            }
        }
    }
}

bool ConfFile::has(const std::string& key) const
{
    return effective_.count(key) > 0;
}

std::optional<std::string> ConfFile::get(const std::string& key) const
{
    auto it = effective_.find(key);
    if (it == effective_.end()) {
        return std::nullopt;
    }
    std::string k;
    std::string value;
    parseLine(lines_.lines[it->second].text, k, value);
    return value;
}

std::string ConfFile::get(const std::string& key, const std::string& fallback) const
{
    std::optional<std::string> value = get(key);
    return value ? *value : fallback;
}

std::vector<std::pair<std::string, std::string>> ConfFile::entries() const
{
    std::vector<std::pair<size_t, std::string>> ordered;
    for (const auto& [key, line] : effective_) {
        ordered.emplace_back(line, key);
    }
    std::sort(ordered.begin(), ordered.end());
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& [line, key] : ordered) {
        result.emplace_back(key, *get(key));
    }
    return result;
}

std::string ConfFile::checkKey(const std::string& key)
{
    if (key.empty() || Utils::trimEnds(key) != key) {
        return "A setting name can't be empty or start or end with a space.";
    }
    if (key.find_first_of("=#\r\n") != std::string::npos) {
        return "A setting name can't contain =, # or a line break.";
    }
    return "";
}

std::string ConfFile::checkValue(const std::string& value)
{
    if (value.find('#') != std::string::npos) {
        return "A value can't contain # (RetroFE reads the rest of the line as a comment).";
    }
    if (value.find_first_of("\r\n") != std::string::npos) {
        return "A value must be a single line.";
    }
    return "";
}

bool ConfFile::set(const std::string& key, const std::string& rawValue, std::string& error)
{
    error = checkKey(key);
    if (error.empty()) {
        error = checkValue(rawValue);
    }
    if (!error.empty()) {
        return false;
    }
    // RetroFE trims values when it reads them, so surrounding spaces mean nothing
    std::string value = Utils::trimEnds(rawValue);

    auto it = effective_.find(key);
    if (it != effective_.end()) {
        // Replace just the value, keeping the spacing and any comment
        std::string& line = lines_.lines[it->second].text;
        size_t equals = line.find('=');
        size_t comment = line.find('#', equals + 1);
        size_t valueStart = line.find_first_not_of(" \t", equals + 1);
        bool noValue = valueStart == std::string::npos || (comment != std::string::npos && valueStart >= comment);
        std::string spacing = noValue ? " " : line.substr(equals + 1, valueStart - equals - 1);
        std::string trailing;
        if (comment != std::string::npos) {
            // Keep the whitespace in front of the comment too
            size_t beforeComment = line.find_last_not_of(" \t", comment - 1);
            trailing = line.substr(beforeComment == std::string::npos || beforeComment < equals ? comment : beforeComment + 1);
            if (noValue && !trailing.empty() && trailing[0] == '#') {
                trailing = " " + trailing;
            }
        }
        std::string updated = line.substr(0, equals + 1) + spacing + value + trailing;
        if (updated != line) {
            line = updated;
            dirty_ = true;
        }
        return true;
    }

    lines_.append(key + " = " + value);
    dirty_ = true;
    reindex();
    return true;
}

bool ConfFile::remove(const std::string& key)
{
    bool removed = false;
    for (size_t i = lines_.lines.size(); i-- > 0;) {
        std::string k;
        std::string value;
        if (parseLine(lines_.lines[i].text, k, value) && k == key) {
            lines_.erase(i);
            removed = true;
        }
    }
    if (removed) {
        dirty_ = true;
        reindex();
    }
    return removed;
}

bool ConfFile::save(std::string& error, Backups* backups)
{
    if (!dirty_ && exists_) {
        return true;
    }
    // Read as missing, but something has created it since (another editor of
    // the same file): writing would replace that file with this copy's lines
    std::error_code ec;
    if (!exists_ && fs::exists(path_, ec)) {
        error = path_ + " was created by something else meanwhile, so it wasn't overwritten. Try again.";
        return false;
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

// --- ConfLayers ---------------------------------------------------------------

ConfLayers::ConfLayers(std::string folder, std::vector<std::string> fileNames)
    : folder_(std::move(folder))
    , names_(std::move(fileNames))
{
    load();
}

std::vector<std::string> ConfLayers::globalFileNames()
{
    std::vector<std::string> names = { "settings.conf" };
    for (int i = 1; i < 16; i++) {
        names.push_back("settings" + std::to_string(i) + ".conf");
    }
    names.push_back("settings_saved.conf");
    return names;
}

std::vector<std::string> ConfLayers::collectionFileNames()
{
    std::vector<std::string> names = { "settings.conf" };
    for (int i = 1; i < 16; i++) {
        names.push_back("settings" + std::to_string(i) + ".conf");
    }
    names.push_back("info.conf");
    return names;
}

void ConfLayers::load()
{
    files_.clear();
    for (const std::string& name : names_) {
        ConfFile file(Utils::combinePath(folder_, name));
        file.load();
        files_.push_back(std::move(file));
    }
    reindex();
}

void ConfLayers::reindex()
{
    keys_.clear();
    for (size_t i = 0; i < files_.size(); i++) {
        for (const auto& [key, value] : files_[i].entries()) {
            keys_[key] = i;
        }
    }
}

bool ConfLayers::has(const std::string& key) const
{
    return keys_.count(key) > 0;
}

std::string ConfLayers::get(const std::string& key) const
{
    auto it = keys_.find(key);
    return it == keys_.end() ? "" : files_[it->second].get(key, "");
}

std::string ConfLayers::source(const std::string& key) const
{
    auto it = keys_.find(key);
    return it == keys_.end() ? "" : names_[it->second];
}

int ConfLayers::sourceIndex(const std::string& key) const
{
    auto it = keys_.find(key);
    return it == keys_.end() ? -1 : static_cast<int>(it->second);
}

int ConfLayers::indexOf(const std::string& fileName) const
{
    for (size_t i = 0; i < names_.size(); i++) {
        if (names_[i] == fileName) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool ConfLayers::ensureExists(size_t index, const std::string& initialContent, std::string& error, Backups* backups)
{
    ConfFile& file = files_[index];
    if (file.exists()) {
        return true;
    }
    // The flag is from the last load: another editor (e.g. a second
    // SettingsFile) may have created the file since. Then read it, never replace it
    std::error_code ec;
    if (fs::exists(file.path(), ec)) {
        if (!file.load(&error)) {
            return false;
        }
        reindex();
        return true;
    }
    // Templates are written with the platform's line endings
    std::string text;
    for (const Line& line : Lines::parse(initialContent).lines) {
        text += line.text + defaultNewline();
    }
    file.setText(text);
    if (!file.save(error, backups)) {
        return false;
    }
    reindex();
    return true;
}

bool ConfLayers::set(const std::string& key, const std::string& value, std::string& error, size_t targetIfMissing,
                     const std::string& initialContent, Backups* backups)
{
    if (!keys_.count(key) && !ensureExists(targetIfMissing, initialContent, error, backups)) {
        return false;
    }
    auto it = keys_.find(key);
    ConfFile& file = files_[it == keys_.end() ? targetIfMissing : it->second];
    if (!file.set(key, value, error) || !file.save(error, backups)) {
        return false;
    }
    reindex();
    return true;
}

bool ConfLayers::remove(const std::string& key, std::string& error, Backups* backups)
{
    for (ConfFile& file : files_) {
        if (file.remove(key) && !file.save(error, backups)) {
            return false;
        }
    }
    reindex();
    return true;
}

}

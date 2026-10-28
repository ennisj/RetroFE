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
#include "PathBrowser.h"
#include "../../Library/Files.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
    const char* const retrofePath = "%RETROFE_PATH%";

    std::string lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string trim(const std::string& text)
    {
        size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return "";
        }
        size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    // A value without the quotes a launcher's program may have around it
    std::string unquote(const std::string& text)
    {
        std::string value = trim(text);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }

    bool startsWith(const std::string& text, const std::string& start)
    {
        return text.size() >= start.size() && lower(text.substr(0, start.size())) == lower(start);
    }

    // Normalised, without a trailing separator unless it's a drive's top ("C:\", "/")
    fs::path normal(const fs::path& path)
    {
        fs::path p = path.lexically_normal();
        if (p.has_relative_path() && !p.has_filename()) {
            p = p.parent_path();
        }
        return p;
    }

    // Two names of a path, as the file system compares them
    bool sameName(const fs::path& a, const fs::path& b)
    {
#ifdef _WIN32
        return lower(a.string()) == lower(b.string());
#else
        return a == b;
#endif
    }

    // A key that is the same for the same folder
    std::string folderKey(const fs::path& path)
    {
        std::string key = normal(path).generic_string();
#ifdef _WIN32
        key = lower(key);
#endif
        return key;
    }

    std::string withSeparators(size_t n)
    {
        std::string digits = std::to_string(n);
        for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
            digits.insert(static_cast<size_t>(i), ",");
        }
        return digits;
    }

    std::string counted(size_t n, const std::string& one, const std::string& many, bool capped = false)
    {
        return withSeparators(n) + (capped ? "+ " : " ") + (n == 1 && !capped ? one : many);
    }

    std::int64_t secondsOf(fs::file_time_type time)
    {
        // C++17 has no clock_cast: move it across by the two clocks' difference now
        auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
    }

    std::string extensionOf(const std::string& name)
    {
        size_t dot = name.find_last_of('.');
        return dot == std::string::npos || dot == 0 ? "" : lower(name.substr(dot + 1));
    }
}

namespace Browser
{

std::vector<std::string> parseExtensions(const std::string& text)
{
    std::vector<std::string> extensions;
    std::string part;
    auto flush = [&] {
        std::string ext = trim(part);
        part.clear();
        // "*.exe" and ".exe" both mean exe; "*" and "*.*" mean anything
        if (ext.rfind("*.", 0) == 0) ext = ext.substr(2);
        else if (ext.rfind("*", 0) == 0) ext = ext.substr(1);
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        ext = lower(ext);
        if (ext.empty() || ext == "*") return;
        if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end()) {
            extensions.push_back(ext);
        }
    };
    for (char c : text) {
        if (c == ';' || c == ',' || c == ' ' || c == '|') {
            flush();
        }
        else {
            part += c;
        }
    }
    flush();
    return extensions;
}

bool matches(const std::string& name, const std::vector<std::string>& extensions)
{
    if (extensions.empty()) {
        return true;
    }
    std::string ext = extensionOf(name);
    return !ext.empty() && std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

bool isHidden(const std::string& name)
{
    if (name.empty() || name[0] == '.') {
        return true;
    }
    std::string l = lower(name);
    return l == "$recycle.bin" || l == "system volume information" || l == "$windows.~bt" || l == "$windows.~ws"
        || l == "recycler" || l == "found.000";
}

Listing listFolder(const fs::path& dir, const std::vector<std::string>& extensions, bool withFiles)
{
    Listing listing;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::none, ec);
    if (ec) {
        listing.error = ec.message();
        return listing;
    }
    listing.ok = true;
    for (fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        std::string name;
        if (!Library::narrowName(it->path().filename(), name) || isHidden(name)) {
            continue;
        }
#ifdef _WIN32
        DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN)
            && (attributes & FILE_ATTRIBUTE_SYSTEM)) continue;
#endif
        std::error_code fec;
        if (it->is_directory(fec)) {
            listing.folders++;
            Entry entry;
            entry.name = name;
            entry.lowerName = lower(name);
            entry.folder = true;
            listing.entries.push_back(entry);
        }
        else if (it->is_regular_file(fec)) {
            listing.files++;
            if (!matches(name, extensions)) {
                continue;
            }
            listing.matching++;
            if (withFiles) {
                Entry entry;
                entry.name = name;
                entry.lowerName = lower(name);
                entry.size = it->file_size(fec);
                auto time = it->last_write_time(fec);
                entry.modified = fec ? 0 : secondsOf(time);
                listing.entries.push_back(entry);
            }
        }
    }
    if (ec) {
        listing.ok = false;
        listing.error = ec.message();
    }
    std::sort(listing.entries.begin(), listing.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.folder != b.folder) return a.folder;
        return a.lowerName != b.lowerName ? a.lowerName < b.lowerName : a.name < b.name;
    });
    return listing;
}

Count countFolder(const fs::path& dir, const std::vector<std::string>& extensions, size_t cap)
{
    Count count;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::none, ec);
    if (ec) {
        return count;
    }
    count.ok = true;
    size_t seen = 0;
    for (fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (++seen > cap) {
            count.capped = true;
            break;
        }
        std::string name;
        if (!Library::narrowName(it->path().filename(), name) || isHidden(name)) {
            continue;
        }
#ifdef _WIN32
        DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN)
            && (attributes & FILE_ATTRIBUTE_SYSTEM)) continue;
#endif
        std::error_code fec;
        if (it->is_directory(fec)) {
            count.folders++;
        }
        else if (it->is_regular_file(fec)) {
            count.files++;
            if (!extensions.empty() && matches(name, extensions)) {
                count.matching++;
            }
        }
    }
    if (ec) count.ok = false;
    return count;
}

std::string extensionList(const std::vector<std::string>& extensions)
{
    std::string text;
    for (size_t i = 0; i < extensions.size(); i++) {
        if (i > 0) {
            text += i + 1 == extensions.size() ? " or " : ", ";
        }
        text += extensions[i];
    }
    return text;
}

std::string countText(const Count& count, const std::vector<std::string>& extensions)
{
    if (!count.ok) {
        return "Can't be read";
    }
    if (!extensions.empty()) {
        if (count.matching > 0) {
            return counted(count.matching, "file", "files", count.capped) + " matching " + extensionList(extensions);
        }
        if (count.folders > 0) {
            return counted(count.folders, "folder", "folders", count.capped);
        }
        return count.files > 0 ? "No files matching " + extensionList(extensions) : "Empty";
    }
    std::string text;
    if (count.folders > 0) {
        text = counted(count.folders, "folder", "folders", count.capped && count.files == 0);
    }
    if (count.files > 0) {
        text += (text.empty() ? "" : ", ") + counted(count.files, "file", "files", count.capped);
    }
    return text.empty() ? "Empty" : text;
}

std::string sizeText(std::uintmax_t bytes)
{
    if (bytes < 1024) {
        return std::to_string(bytes) + (bytes == 1 ? " byte" : " bytes");
    }
    const char* const units[] = { "KB", "MB", "GB", "TB" };
    double value = static_cast<double>(bytes) / 1024.0;
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit++;
    }
    char text[32];
    std::snprintf(text, sizeof(text), value < 10.0 ? "%.1f %s" : "%.0f %s", value, units[unit]);
    return text;
}

std::string dateText(std::int64_t seconds)
{
    if (seconds <= 0) {
        return "";
    }
    std::time_t time = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &time) != 0) return "";
#else
    if (!localtime_r(&time, &local)) return "";
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
    return text;
}

std::string fileDetail(const Entry& entry)
{
    std::string date = dateText(entry.modified);
    return sizeText(entry.size) + (date.empty() ? "" : ", " + date);
}

bool isPicture(const std::string& name)
{
    static const std::vector<std::string> pictures = { "png", "jpg", "jpeg", "gif", "webp", "bmp" };
    return matches(name, pictures);
}

// --- Paths ------------------------------------------------------------------------

fs::path parentFolder(const fs::path& path)
{
    fs::path p = normal(path);
#ifdef _WIN32
    // A UNC share is the browse root, rather than the server above it.
    if (!p.root_name().empty() && p.root_name().native().size() > 2) {
        auto part = p.relative_path();
        if (part.empty() || part == part.filename()) return {};
    }
#endif
    fs::path parent = p.parent_path();
    return parent == p || !p.has_relative_path() ? fs::path() : parent;
}

bool isRelativeStyle(const std::string& value)
{
    std::string v = unquote(value);
    if (v.empty()) {
        return false;
    }
    if (startsWith(v, retrofePath)) {
        return true;
    }
    if (v.find('%') != std::string::npos) {
        return false;   // another variable: leave it alone
    }
    if (v.size() >= 2 && std::isalpha(static_cast<unsigned char>(v[0])) && v[1] == ':') return false;
    if (v.rfind("\\\\", 0) == 0) return false;
    fs::path p(v);
    return p.is_relative() && !p.has_root_name();
}

fs::path resolve(const std::string& value, const fs::path& root)
{
    std::string v = unquote(value);
    if (v.empty()) {
        return {};
    }
    if (startsWith(v, retrofePath)) {
        std::string rest = v.substr(std::string(retrofePath).size());
        while (!rest.empty() && (rest[0] == '/' || rest[0] == '\\')) {
            rest.erase(0, 1);
        }
#ifndef _WIN32
        std::replace(rest.begin(), rest.end(), '\\', '/');
#endif
        return normal(root / rest);
    }
    fs::path p(v);
    if (p.is_relative() && !p.has_root_name()) {
        p = root / p;
    }
    return normal(p);
}

bool insideRoot(const fs::path& path, const fs::path& root, fs::path* relative)
{
    fs::path a = normal(path);
    fs::path b = normal(root);
    if (a.empty() || b.empty()) {
        return false;
    }
    auto ai = a.begin();
    for (auto bi = b.begin(); bi != b.end(); ++bi) {
        if (bi->empty()) {
            continue;
        }
        if (ai == a.end() || !sameName(*ai, *bi)) {
            return false;
        }
        ++ai;
    }
    if (relative) {
        fs::path rest;
        for (; ai != a.end(); ++ai) {
            if (!ai->empty()) {
                rest /= *ai;
            }
        }
        *relative = rest;
    }
    return true;
}

std::string storedValue(const fs::path& chosen, const fs::path& root, const std::string& current)
{
    fs::path relative;
    std::string now = unquote(current);
    if (isRelativeStyle(now) && insideRoot(chosen, root, &relative) && !relative.empty()) {
        std::string text = relative.generic_string();
        // The separator the value used, else the system's
        bool backslash = now.find('\\') != std::string::npos
            || (now.find('/') == std::string::npos && fs::path::preferred_separator == '\\');
        if (backslash) {
            std::replace(text.begin(), text.end(), '/', '\\');
        }
        if (startsWith(now, retrofePath)) {
            return std::string(retrofePath) + (backslash ? "\\" : "/") + text;
        }
        return text;
    }
    fs::path absolute = normal(chosen);
    absolute.make_preferred();
    return absolute.string();
}

std::string displayPath(const fs::path& path, const fs::path& root, bool relative)
{
    fs::path rest;
    if (relative && insideRoot(path, root, &rest)) {
        if (rest.empty()) {
            return "RetroFE's folder";
        }
        rest.make_preferred();
        return rest.string();
    }
    fs::path absolute = normal(path);
    absolute.make_preferred();
    return absolute.string();
}

std::string shortPath(const std::string& path, size_t maxLength)
{
    if (path.size() <= maxLength) {
        return path;
    }
    char separator = path.find('\\') != std::string::npos ? '\\' : '/';
    std::vector<std::string> parts;
    std::string part;
    for (char c : path) {
        if (c == '\\' || c == '/') {
            parts.push_back(part);
            part.clear();
        }
        else {
            part += c;
        }
    }
    parts.push_back(part);
    std::string head = parts.front();
    std::string tail;
    for (size_t i = parts.size() - 1; i > 0; i--) {
        std::string candidate = parts[i] + (tail.empty() ? "" : std::string(1, separator) + tail);
        if (head.size() + 5 + candidate.size() > maxLength && !tail.empty()) {
            break;
        }
        tail = candidate;
    }
    std::string text = head + separator + "..." + separator + tail;
    if (text.size() > maxLength && maxLength > 3) {
        text = "..." + tail.substr(tail.size() - std::min(tail.size(), maxLength - 3));
    }
    return text;
}

fs::path existingFolder(const fs::path& path)
{
    std::error_code ec;
    fs::path p = normal(path);
    while (!p.empty()) {
        if (fs::is_directory(p, ec)) {
            return p;
        }
        fs::path parent = parentFolder(p);
        if (parent.empty() || parent == p) {
            break;
        }
        p = parent;
    }
    return {};
}

fs::path valueFolder(const std::string& value, bool folderField, const fs::path& root)
{
    fs::path absolute = resolve(value, root);
    if (absolute.empty() || folderField) {
        return absolute;
    }
    std::error_code ec;
    return fs::is_directory(absolute, ec) ? absolute : absolute.parent_path();
}

fs::path openingFolder(const std::string& value, const std::string& start, bool folderField, const fs::path& root)
{
    std::error_code ec;
    if (!trim(value).empty()) {
        fs::path folder = valueFolder(value, folderField, root);
        if (fs::is_directory(folder, ec)) {
            return folder;
        }
    }
    if (!trim(start).empty()) {
        return existingFolder(valueFolder(start, false, root));
    }
    return {};
}

// --- Starting places ------------------------------------------------------------------

std::vector<Place> startingPlaces(const PlacesInput& input)
{
    std::vector<Place> places;
    std::vector<std::string> seen;
    auto add = [&](const fs::path& path, const std::string& label, const std::string& detail, bool mustExist) {
        std::error_code ec;
        if (path.empty() || (mustExist && !fs::is_directory(path, ec))) {
            return;
        }
        std::string key = folderKey(path);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            return;
        }
        seen.push_back(key);
        Place place;
        fs::path shown = normal(path);
        shown.make_preferred();
        place.path = shown;
        place.label = label.empty() ? shortPath(shown.string(), 64) : label;
        place.detail = detail;
        places.push_back(place);
    };
    std::error_code ec;
    if (!trim(input.value).empty()) {
        add(valueFolder(input.value, input.folderField, input.root), "", "Where it is now", true);
    }
    if (!trim(input.start).empty()) {
        add(existingFolder(valueFolder(input.start, false, input.root)), "", "Suggested", true);
    }
    add(input.root, "", "RetroFE's folder", true);
    for (const std::string& folder : input.recent) {
        add(fs::path(folder), "", "Chosen before", true);
    }
    for (const Place& place : input.userFolders) {
        add(place.path, place.label, place.detail, true);
    }
    for (const Drive& drive : input.drives) {
        std::string label = drive.path;
        if (!drive.name.empty()) {
            label += "  " + drive.name;
        }
        std::string detail;
        switch (drive.kind) {
        case Drive::Kind::Fixed: detail = "Drive"; break;
        case Drive::Kind::Removable: detail = "Removable drive"; break;
        case Drive::Kind::Network: detail = "Network drive"; break;
        case Drive::Kind::Optical: detail = "Disc drive"; break;
        case Drive::Kind::Other: detail = "Drive"; break;
        }
        if (drive.free > 0) {
            detail += ", " + sizeText(drive.free) + " free";
        }
        add(fs::path(drive.path), label, detail, false);
    }
    return places;
}

void remember(std::vector<std::string>& recent, const fs::path& folder, size_t max)
{
    fs::path shown = normal(folder);
    shown.make_preferred();
    std::string key = folderKey(shown);
    recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const std::string& r) { return folderKey(r) == key; }),
                 recent.end());
    recent.insert(recent.begin(), shown.string());
    if (recent.size() > max) {
        recent.resize(max);
    }
}

std::vector<size_t> filterEntries(const std::vector<Entry>& entries, const std::string& query)
{
    std::vector<size_t> shown;
    std::string q = lower(trim(query));
    for (size_t i = 0; i < entries.size(); i++) {
        if (q.empty() || entries[i].lowerName.find(q) != std::string::npos) {
            shown.push_back(i);
        }
    }
    return shown;
}

}

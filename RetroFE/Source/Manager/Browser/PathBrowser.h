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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The file and folder browser's logic, without screens: listing a folder
// filtered by a field's pattern, counting what folders hold, the places a
// browse starts from, and turning the path chosen into what the field stores
// (relative to RetroFE's folder where the field's value already is).
// BrowserProvider shows it as the file-browser form; the theme editor's
// chooser (Themes/app/FileChooser) lists folders with it too.
namespace Browser
{
    // Extensions from a field's pattern ("*.exe;*.bat") or a list of them
    // ("zip, 7z" or ".zip .7z"): lower case, without the dot, no duplicates.
    // "*" and "*.*" mean any file, so they give none.
    std::vector<std::string> parseExtensions(const std::string& text);

    // Whether a file name has one of the extensions (case doesn't matter);
    // with none, every file matches
    bool matches(const std::string& name, const std::vector<std::string>& extensions);

    // Names a browser doesn't show: hidden dot files, and Windows' own
    // folders at the top of a drive ($RECYCLE.BIN, System Volume Information)
    bool isHidden(const std::string& name);

    struct Entry
    {
        std::string name;
        std::string lowerName;
        bool folder = false;
        std::uintmax_t size = 0;     // files
        std::int64_t modified = 0;   // seconds since 1970, 0 if unknown
    };

    struct Listing
    {
        bool ok = false;
        std::string error;             // why the folder couldn't be read
        std::vector<Entry> entries;    // folders first, then files; by name, case aside
        size_t files = 0;              // every file in the folder, shown or not
        size_t matching = 0;           // the files matching the extensions
        size_t folders = 0;
    };

    // A folder's subfolders and, when withFiles, its files matching
    // extensions (any with none). Hidden names and names Windows can't spell
    // in the current code page are left out.
    Listing listFolder(const std::filesystem::path& dir, const std::vector<std::string>& extensions, bool withFiles);

    // What a folder holds, for the detail beside it: counts stop at cap
    struct Count
    {
        bool ok = false;
        size_t files = 0;
        size_t matching = 0;
        size_t folders = 0;
        bool capped = false;
    };
    Count countFolder(const std::filesystem::path& dir, const std::vector<std::string>& extensions, size_t cap = 10000);

    // "212 files matching zip", "No files matching zip, 3 folders",
    // "12 folders, 40 files", "Empty", "Can't be read"
    std::string countText(const Count& count, const std::vector<std::string>& extensions);

    // "zip", "zip or 7z", "exe, bat or cmd"
    std::string extensionList(const std::vector<std::string>& extensions);

    // "512 bytes", "1.2 KB", "34 MB", "1.5 GB"
    std::string sizeText(std::uintmax_t bytes);

    // "2024-03-01" (local time), "" for 0
    std::string dateText(std::int64_t seconds);

    // A file's detail: "1.2 MB, 2024-03-01"
    std::string fileDetail(const Entry& entry);

    // Picture files, which a list shows as thumbnails
    bool isPicture(const std::string& name);

    std::filesystem::path parentFolder(const std::filesystem::path& path);

    // --- Paths --------------------------------------------------------------------

    // Whether a field's value is written relative to RetroFE's folder: a
    // relative path, or one starting with %RETROFE_PATH%
    bool isRelativeStyle(const std::string& value);

    // A field's value as an absolute path: %RETROFE_PATH% and relative paths
    // are from root. "" for "".
    std::filesystem::path resolve(const std::string& value, const std::filesystem::path& root);

    // Whether path is root or inside it (case aside on Windows), and the relative part
    bool insideRoot(const std::filesystem::path& path, const std::filesystem::path& root, std::filesystem::path* relative = nullptr);

    // What the field gets for the path chosen: written the way its value is
    // now, so a launcher's program relative to RetroFE's folder stays
    // relative (with %RETROFE_PATH% if it had it) when the new one is inside
    // that folder too; otherwise the absolute path, which is what the system
    // dialog gives and what the other fields expect.
    std::string storedValue(const std::filesystem::path& chosen, const std::filesystem::path& root, const std::string& current);

    // How a path is shown: relative to RetroFE's folder (as "RetroFE's folder"
    // for the folder itself) when relative is set and it's inside, otherwise absolute
    std::string displayPath(const std::filesystem::path& path, const std::filesystem::path& root, bool relative);

    // A long path with its middle left out, keeping the drive and the last
    // folders: "D:\...\Arcade\medium_artwork\video"
    std::string shortPath(const std::string& path, size_t maxLength);

    // The folder itself if it exists, else its nearest existing parent; "" if none
    std::filesystem::path existingFolder(const std::filesystem::path& path);

    // The folder a value points into: a folder field's folder, a file field's
    // file's folder. Values like ".../video/Sonic.*" (a slot's place) count
    // as files. "" when the value is empty.
    std::filesystem::path valueFolder(const std::string& value, bool folderField, const std::filesystem::path& root);

    // The folder a browse opens in, over the starting places: the folder of
    // the field's value when it's set and there, else the field's own start
    // (the nearest existing folder to it), else "" (the starting places).
    std::filesystem::path openingFolder(const std::string& value, const std::string& start, bool folderField,
                                        const std::filesystem::path& root);

    // --- Starting places ----------------------------------------------------------

    struct Drive
    {
        std::string path;    // "C:\" or "/media/usb"
        std::string name;    // its volume label, may be ""
        enum class Kind { Fixed, Removable, Network, Optical, Other } kind = Kind::Fixed;
        std::uintmax_t free = 0;   // 0 when unknown
    };

    struct Place
    {
        std::string label;
        std::string detail;
        std::filesystem::path path;
    };

    struct PlacesInput
    {
        std::filesystem::path root;          // RetroFE's folder
        std::string value;                   // the field's value now
        std::string start;                   // the field's start (a folder, or a slot's place)
        bool folderField = false;
        std::vector<std::string> recent;     // folders picked from before, newest first
        std::vector<Place> userFolders;      // Downloads, Desktop
        std::vector<Drive> drives;
    };

    // In order: the value's folder, the field's start, RetroFE's folder,
    // recent picks, the user's folders, then drives. Each folder once (case
    // aside on Windows), only folders that are there (drives always).
    std::vector<Place> startingPlaces(const PlacesInput& input);

    // Remembers a picked folder first in recent, at most max of them
    void remember(std::vector<std::string>& recent, const std::filesystem::path& folder, size_t max = 5);

    // Entries whose name contains query (case aside); all with an empty query
    std::vector<size_t> filterEntries(const std::vector<Entry>& entries, const std::string& query);

    // The machine's drives and the user's Downloads and Desktop (Drives.cpp)
    std::vector<Drive> listDrives();
    void driveDetails(Drive& drive);
    std::vector<Place> userFolders();
}

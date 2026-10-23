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

// Small file helpers shared by the library: whole-file reads, atomic writes
// (temp file + rename), and a line model that keeps every line's own ending
// so an edit changes only the lines it touches.

#include <filesystem>
#include <string>
#include <vector>

namespace Library
{
    // The whole file as bytes; false when it can't be opened
    bool readFile(const std::string& path, std::string& content);

    // Writes <path>.tmp next to the file, then renames it over the file, so a
    // failed write never leaves half a file. Creates the parent folder.
    bool writeFileAtomic(const std::string& path, const std::string& content, std::string& error);

    // Copies a file the same way: to a temp file beside the destination, then renamed into place
    bool copyFileAtomic(const std::string& from, const std::string& to, std::string& error);

    // "\r\n" on Windows, "\n" elsewhere: the line ending for files that don't exist yet
    const char* defaultNewline();

    // A file name as a narrow string, the way the rest of RetroFE spells paths.
    // False (and no exception) when Windows can't represent the name in the
    // current code page; RetroFE can't open such files either.
    bool narrowName(const std::filesystem::path& path, std::string& name);
    std::string narrow(const std::filesystem::path& path);

    std::string toLower(std::string text);
    bool iequals(const std::string& a, const std::string& b);
    bool icontains(const std::string& haystackLower, const std::string& needleLower);

    // Joins and tidies a path for display and comparison ("a/b/../c" -> "a/c")
    std::string normalPath(const std::string& path);

    // True when path is folder or inside it (lexically, after normalising)
    bool isInside(const std::string& path, const std::string& folder);

    // A file's lines, each with the ending it had ("\r\n", "\n", or "" for a
    // last line without one). join() gives back the exact bytes.
    struct Line
    {
        std::string text;
        std::string eol;
    };

    class Lines
    {
    public:
        static Lines parse(const std::string& content);
        std::string join() const;

        // The ending new lines get: the file's own (CRLF if any line uses it), else defaultNewline()
        std::string newline() const;

        // Appends a line, first ending the current last line if it has no line break
        void append(const std::string& text);

        // Inserts a line before index (index == size() appends)
        void insert(size_t index, const std::string& text);

        void erase(size_t index);

        std::vector<Line> lines;
        std::string fallbackNewline = defaultNewline();
    };
}

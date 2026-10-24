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
#include <string>
#include <utility>
#include <vector>

// The theme editors' file chooser: an ImGui modal listing the files of one
// kind inside the RetroFE folder (never outside it), with This theme /
// RetroFE folder to jump there, and Somewhere else... for the system's own
// dialog (which the caller opens from update(): it blocks). Shared by the
// widget editor and the layout editor; the caller copies the chosen file in.
namespace Themes
{
    // The file types a chooser offers for a filter: image, video, media,
    // text, font, sound; "" = any (lower case, with the dot)
    std::vector<std::string> chooserExtensions(const std::string& filter);
    std::string chooserFilterLabel(const std::string& filter);   // "Images"
    std::string chooserPattern(const std::string& filter);       // "*.png;*.jpg..." for the system dialog
    bool insideFolder(const std::filesystem::path& path, const std::filesystem::path& folder);

    class FileChooser
    {
    public:
        struct Request
        {
            std::string filter;        // image, video, ... (chooserExtensions)
            std::string title;         // "Choose the image for Box art"
            std::string note;          // what happens without a file ("" = none: the button says Cancel, else Not now)
            std::string destination;   // where the file goes: "the theme's assets folder"
        };
        enum class Result { None, Chosen, SomewhereElse, Cancelled };

        // Opens it in `dir` (the theme's folder when that isn't inside root)
        void open(const Request& request, const std::filesystem::path& dir);
        bool isOpen() const { return show_; }
        void close() { show_ = false; }
        // Shows another folder (a script's browsedir)
        void showFolder(const std::filesystem::path& dir);
        const Request& request() const { return request_; }

        // Draws the modal while it's open. Chosen: `path` is the file.
        // SomewhereElse: the caller opens the system's dialog from update().
        // Cancelled: its Cancel / Not now button (closing it otherwise is None).
        Result draw(float scale, const std::filesystem::path& root, const std::filesystem::path& theme, std::string& path);

    private:
        bool show_ = false, open_ = false;
        Request request_;
        std::filesystem::path dir_;
        bool listed_ = false;
        std::vector<std::pair<std::string, bool>> entries_;   // name, is a folder
        std::string selected_;
        bool focusList_ = false;   // a folder was entered without the mouse: focus its first entry
    };
}

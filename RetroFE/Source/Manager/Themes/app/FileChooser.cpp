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
#include "FileChooser.h"
#include "../../FileDialog.h"
#include "../../Browser/PathBrowser.h"
#include "../../../Utility/Utils.h"
#include "imgui.h"
#include <algorithm>

namespace fs = std::filesystem;

namespace Themes
{
    std::vector<std::string> chooserExtensions(const std::string& filter)
    {
        static const std::vector<std::string> image = { ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp" };
        static const std::vector<std::string> video = { ".mp4", ".mkv", ".avi", ".webm", ".mov", ".m4v", ".mpg", ".mpeg" };
        if (filter == "image") return image;
        if (filter == "video") return video;
        if (filter == "media") {
            std::vector<std::string> both = image;
            both.insert(both.end(), video.begin(), video.end());
            return both;
        }
        if (filter == "text") return { ".txt" };
        if (filter == "font") return { ".ttf", ".otf" };
        if (filter == "sound") return { ".wav", ".ogg", ".mp3", ".flac" };
        return {};
    }

    std::string chooserFilterLabel(const std::string& filter)
    {
        if (filter == "image") return "Images";
        if (filter == "video") return "Videos";
        if (filter == "media") return "Images and videos";
        if (filter == "text") return "Text files";
        if (filter == "font") return "Fonts";
        if (filter == "sound") return "Sounds";
        return "All files";
    }

    std::string chooserPattern(const std::string& filter)
    {
        std::string pattern;
        for (const std::string& ext : chooserExtensions(filter)) pattern += (pattern.empty() ? "*" : ";*") + ext;
        return pattern.empty() ? "*.*" : pattern;
    }

    bool insideFolder(const fs::path& path, const fs::path& folder)
    {
        std::error_code ec;
        const fs::path rel = fs::weakly_canonical(path, ec).lexically_relative(fs::weakly_canonical(folder, ec));
        return !ec && !rel.empty() && rel.begin()->string() != "..";
    }

    void FileChooser::open(const Request& request, const fs::path& dir)
    {
        request_ = request;
        dir_ = dir;
        listed_ = false;
        selected_.clear();
        show_ = true;
    }

    void FileChooser::showFolder(const fs::path& dir)
    {
        dir_ = dir;
        listed_ = false;
    }

    FileChooser::Result FileChooser::draw(float scale, const fs::path& root, const fs::path& theme, std::string& path)
    {
        const char* const name = "Choose a file";
        if (open_ && !ImGui::IsPopupOpen(name)) {
            show_ = false;
            open_ = false;
        }
        if (show_ && !open_) {
            ImGui::OpenPopup(name);
            open_ = true;
        }
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowSize(ImVec2(std::min(io.DisplaySize.x - 40.0f, 720.0f * scale),
                                        std::min(io.DisplaySize.y - 40.0f, 560.0f * scale)), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(name, &show_)) return Result::None;
        if (!show_) {
            // Closed (Escape, its close button): nothing chosen, nothing to do
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return Result::None;
        }
        Result result = Result::None;
        ImGui::TextWrapped("%s", request_.title.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Showing %s inside the RetroFE folder. The one you choose is copied into %s.",
                           Utils::toLower(chooserFilterLabel(request_.filter)).c_str(), request_.destination.c_str());
        if (!request_.note.empty()) ImGui::TextWrapped("%s", request_.note.c_str());
        ImGui::PopStyleColor();

        // Where: always inside the RetroFE folder
        std::error_code ec;
        const bool atRoot = !insideFolder(dir_, root);
        if (atRoot) dir_ = root;
        std::string where = dir_.lexically_relative(root).generic_string();
        ImGui::BeginDisabled(atRoot || where == "." || where.empty());
        if (ImGui::Button("Up")) {
            dir_ = dir_.parent_path();
            listed_ = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("This theme")) { dir_ = theme; listed_ = false; }
        ImGui::SameLine();
        if (ImGui::Button("RetroFE folder")) { dir_ = root; listed_ = false; }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", where.empty() || where == "." ? "(the RetroFE folder)" : where.c_str());

        // Listed once per folder, not every frame
        const bool relisted = !listed_;
        if (!listed_) {
            listed_ = true;
            entries_.clear();
            selected_.clear();
            // The file browser's listing: folders first, then the files of this kind, by name
            const Browser::Listing listing = Browser::listFolder(dir_, Browser::parseExtensions(chooserPattern(request_.filter)), true);
            for (const Browser::Entry& entry : listing.entries) {
                entries_.emplace_back(entry.name, entry.folder);
            }
        }

        const float buttonsH = ImGui::GetFrameHeightWithSpacing() * 1.6f;
        // NavFlattened: a controller moves into the list like any other rows
        ImGui::BeginChild("##files", ImVec2(0, -buttonsH), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
        bool choose = false;
        fs::path enter;
        // A new folder starts at its top; the controller's focus goes to its first entry when it was in the list
        const bool focusFirst = relisted && focusList_;
        if (relisted) ImGui::SetScrollY(0.0f);
        focusList_ = false;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(entries_.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& entry = entries_[i];
                ImGui::PushID(i);
                if (i == 0 && focusFirst) ImGui::SetKeyboardFocusHere();
                if (entry.second) {
                    if (ImGui::Selectable(("[" + entry.first + "]").c_str())) {
                        enter = dir_ / entry.first;
                        focusList_ = !ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                    }
                }
                else if (ImGui::Selectable(entry.first.c_str(), selected_ == entry.first, ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected_ = entry.first;
                    // A double click chooses it, as does Select on a controller (no mouse involved)
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) choose = true;
                }
                ImGui::PopID();
            }
        }
        if (entries_.empty()) ImGui::TextDisabled("No folders or %s here.", Utils::toLower(chooserFilterLabel(request_.filter)).c_str());
        ImGui::EndChild();
        if (!enter.empty()) {
            dir_ = enter;
            listed_ = false;
        }

        ImGui::BeginDisabled(selected_.empty());
        if (ImGui::Button("Choose") || (choose && !selected_.empty())) {
            path = (dir_ / selected_).string();
            result = Result::Chosen;
            show_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (FileDialog::isSupported()) {
            ImGui::SameLine();
            if (ImGui::Button("Somewhere else...")) {
                result = Result::SomewhereElse;
                show_ = false;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose a file anywhere, with the system's file dialog");
        }
        ImGui::SameLine();
        if (ImGui::Button(request_.note.empty() ? "Cancel" : "Not now")) {
            result = Result::Cancelled;
            show_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return result;
    }
}

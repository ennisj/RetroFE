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

// Where RetroFE looks for a game's, a collection's or a menu entry's media,
// in the order ReloadableMedia / ReloadableScrollingText look, and what it
// would find. Folders come from Configuration::getMediaPropertyAbsolutePath's
// rule: collections.<c>.media.<type>, then (fork-compat) media.basePath, then
// baseMediaPath/<c>/medium_artwork/<type> (system_artwork for collections).
//
// Fork compat (OneSauce media folders, LayoutCompat::lookupMediaStep): where
// <directory>/<basename>.<ext> is missing, a folder <directory>/<basename>/
// holding image (or video) files stands for it, and RetroFE shows one of its
// files at random. A file beside the folder wins. Not for text, and not at a
// game's own ROM folder.

#include "Index.h"
#include <string>
#include <vector>

namespace Library
{
    enum class MediaKind
    {
        Image,  // gif webp png jpg jpeg (reloadableImage)
        Video,  // mp4 avi mkv mp3 wav flac (reloadableVideo; it falls back to images itself)
        Text,   // <name>.txt (reloadableText type="story" and the like)
    };

    struct MediaProbe
    {
        std::string directory;
        std::string basename;     // file name without extension
        std::string why;          // "page collection", "game's collection", "game folder", "default", ...
        std::string file;         // what's there, or "" (a media folder: its first file)
        size_t folderFiles = 0;   // file is the first of a media folder of this many files
    };

    struct MediaSlot
    {
        std::string type;         // "logo", "video", "story", ...
        MediaKind kind = MediaKind::Image;
        std::string directory;    // the slot's own folder: where a new file goes
        std::string basename;     // the slot's own name
        std::string file;         // the file RetroFE would show, or ""
        bool own = false;         // file is the slot's own; false = empty or a fallback
        std::string fallback;     // when showing a fallback, where it comes from (MediaProbe::why)
        std::vector<MediaProbe> probes;  // every place looked, in order

        // Media folders. The slot's own folder <directory>/<basename>/, when
        // there is one (also when a file beside it wins, or it's empty), and
        // its files of this kind in natural order (full paths)
        std::string folder;
        std::vector<std::string> folderFiles;
        // file is the first of a media folder's files, standing for the one
        // RetroFE picks at random: the slot's own folder (own) or a fallback's
        size_t shownFolderFiles = 0;      // how many files that folder has; 0 = file is a plain file

        bool fromFolder() const { return shownFolderFiles > 0; }
        // The slot's own file is a single file beside its folder (which then isn't shown)
        bool fileBesideFolder() const { return own && !fromFolder() && !folder.empty(); }
    };

    // Media types to offer for a collection: RetroFE's standard folders,
    // OneSauce's extra ones, and any other folder its medium_artwork has
    std::vector<std::string> mediaTypes(const Index& index, int collection);
    // The kind a type usually holds (video* -> Video, story/history/... -> Text)
    MediaKind defaultKind(const std::string& type);

    // A game's media as a page shows it. pageCollection is the page the game
    // is shown on (a merged collection, say); -1 = its own collection.
    MediaSlot gameMedia(const Index& index, int game, const std::string& type, MediaKind kind, int pageCollection = -1);

    // A collection's own art: system_artwork/<type>.*
    MediaSlot collectionMedia(const Index& index, int collection, const std::string& type, MediaKind kind);

    // A menu's entry for a child collection: the menu's medium_artwork/<type>/<child>.*,
    // falling back to the child's system_artwork/<type>.*
    MediaSlot menuEntryMedia(const Index& index, int menu, const std::string& child, const std::string& type, MediaKind kind);

    // The file RetroFE would open for directory/basename (Utils::findMatchingFile's
    // rules: any case on Windows, the listed cases elsewhere), or false
    bool findMediaFile(const std::string& directory, const std::string& basename, MediaKind kind, std::string& file);

    // Extensions accepted when copying a file into a slot of this kind (lower case)
    const std::vector<std::string>& acceptedExtensions(MediaKind kind);

    // Media folders (see the top of this file). Whether a slot of this kind
    // can be a folder (images and videos), and the files of the folder
    // <directory>/<basename>/ RetroFE chooses from (extensions ignoring case,
    // natural order, full paths; empty when there's no such folder)
    bool mediaFoldersApply(MediaKind kind);
    std::vector<std::string> mediaFolderFiles(const std::string& directory, const std::string& basename, MediaKind kind);

    // Sentences for the screens. A slot's own folder, or "" when it has none:
    // "A folder of 3 files: RetroFE shows one at random. <folder>", saying
    // when a file beside it wins, or that it's empty
    std::string mediaFolderNote(const MediaSlot& slot);
    // What a slot shows, in a few words: "a folder of 3 files (one shown at
    // random)", the file's name, or "" when nothing
    std::string mediaShownText(const MediaSlot& slot);
}

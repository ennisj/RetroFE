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
#include "Media.h"
#include "Files.h"
#include "../Graphics/LayoutCompat.h"
#include "../Utility/Utils.h"
#include <cctype>
#include <filesystem>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{

namespace
{
    std::vector<std::string> lookupExtensions(MediaKind kind)
    {
        std::vector<std::string> base = kind == MediaKind::Image ? imageExtensions()
            : kind == MediaKind::Video ? videoExtensions() : std::vector<std::string>{ "txt" };
#ifdef WIN32
        return base;
#else
        if (kind == MediaKind::Text) {
            return base;  // ReloadableScrollingText opens <name>.txt as is
        }
        std::vector<std::string> both;
        for (const std::string& extension : base) {
            both.push_back(extension);
            std::string upper = extension;
            for (char& c : upper) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            both.push_back(upper);
        }
        return both;
#endif
    }

    std::string safeBasename(std::string basename)
    {
        // ReloadableMedia: Utils::replaceSlashesWithUnderscores
        Utils::replaceSlashesWithUnderscores(basename);
        return basename;
    }

    class SlotBuilder
    {
    public:
        SlotBuilder(MediaSlot& slot)
            : slot_(slot)
        {
        }

        // folders: a media folder <directory>/<basename>/ may stand for the
        // file (not at a game's own ROM folder, as ReloadableMedia)
        void probe(const std::string& directory, const std::string& basename, const std::string& why, bool folders = true)
        {
            if (directory.empty()) {
                return;
            }
            for (const MediaProbe& existing : slot_.probes) {
                if (existing.directory == directory && existing.basename == basename) {
                    return;
                }
            }
            MediaProbe probe{ directory, basename, why, "", 0 };
            findMediaFile(directory, basename, slot_.kind, probe.file);
            const bool first = slot_.probes.empty();
            if (first) {
                slot_.directory = directory;
                slot_.basename = basename;
            }
            // Where there's no file; and beside the slot's own file, to say both are there
            if (folders && mediaFoldersApply(slot_.kind) && !basename.empty() && (probe.file.empty() || first)) {
                std::vector<std::string> files = mediaFolderFiles(directory, basename, slot_.kind);
                std::error_code ec;
                if (first && fs::is_directory(Utils::combinePath(directory, basename), ec)) {
                    slot_.folder = Utils::combinePath(directory, basename);
                    slot_.folderFiles = files;
                }
                if (probe.file.empty() && !files.empty()) {
                    probe.file = files.front();
                    probe.folderFiles = files.size();
                }
            }
            if (!probe.file.empty() && slot_.file.empty()) {
                slot_.file = probe.file;
                slot_.own = first;
                slot_.fallback = slot_.own ? "" : why;
                slot_.shownFolderFiles = probe.folderFiles;
            }
            slot_.probes.push_back(std::move(probe));
        }

    private:
        MediaSlot& slot_;
    };
}

const std::vector<std::string>& acceptedExtensions(MediaKind kind)
{
    static const std::vector<std::string> images = { "png", "jpg", "jpeg", "gif", "webp" };
    static const std::vector<std::string> videos = { "mp4", "avi", "mkv", "mp3", "wav", "flac" };
    static const std::vector<std::string> texts = { "txt" };
    return kind == MediaKind::Image ? images : kind == MediaKind::Video ? videos : texts;
}

bool findMediaFile(const std::string& directory, const std::string& basename, MediaKind kind, std::string& file)
{
    // A few file checks per slot: much cheaper than listing a media folder of
    // thousands of files the first time a details screen opens. Windows file
    // names ignore case, as RetroFE's lower-cased listing does; elsewhere the
    // listed spellings (png, PNG, ...) are tried, as RetroFE does.
    std::error_code ec;
    if (directory.empty() || basename.empty() || !fs::is_directory(directory, ec)) {
        return false;
    }
    for (const std::string& extension : lookupExtensions(kind)) {
        std::string path = Utils::combinePath(directory, basename + "." + extension);
        if (fs::is_regular_file(path, ec)) {
            file = path;
            return true;
        }
    }
    return false;
}

bool mediaFoldersApply(MediaKind kind)
{
    // ReloadableMedia (images, videos); scrolling text reads <name>.txt only
    return kind == MediaKind::Image || kind == MediaKind::Video;
}

std::vector<std::string> mediaFolderFiles(const std::string& directory, const std::string& basename, MediaKind kind)
{
    if (!mediaFoldersApply(kind) || directory.empty() || basename.empty()) {
        return {};
    }
    // The lists RetroFE passes for folders (not cached here: a screen reads them afresh)
    return LayoutCompat::mediaFolderFiles(Utils::combinePath(directory, basename),
                                          kind == MediaKind::Image ? LayoutCompat::mediaFolderImageExtensions()
                                                                   : LayoutCompat::mediaFolderVideoExtensions());
}

namespace
{
    std::string files(size_t n)
    {
        return std::to_string(n) + (n == 1 ? " file" : " files");
    }

    std::string nameOf(const std::string& path)
    {
        std::string name;
        return narrowName(fs::path(path).filename(), name) ? name : path;
    }
}

std::string mediaFolderNote(const MediaSlot& slot)
{
    if (slot.folder.empty()) {
        return "";
    }
    if (slot.folderFiles.empty()) {
        return "Its folder " + slot.folder + " holds no " + (slot.kind == MediaKind::Video ? "video" : "picture")
            + " files, so RetroFE doesn't use it.";
    }
    std::string folder = "a folder of " + files(slot.folderFiles.size()) + " (" + slot.folder + ")";
    if (slot.fileBesideFolder()) {
        return "There's both a file, " + nameOf(slot.file) + ", and " + folder
            + ". The file wins, so RetroFE never shows the folder's files: clear the file to show them, or clear the folder.";
    }
    return "This is " + folder + ", not one file: RetroFE shows one of them at random each time it comes up "
        + "(removePlaylist, B on OneSauce's controls, moves to the next). A file named " + slot.basename
        + " beside the folder would win over it.";
}

std::string mediaShownText(const MediaSlot& slot)
{
    if (slot.file.empty()) {
        return "";
    }
    if (slot.fromFolder()) {
        return "a folder of " + files(slot.shownFolderFiles) + (slot.shownFolderFiles == 1 ? "" : " (one shown at random)");
    }
    return nameOf(slot.file);
}

MediaKind defaultKind(const std::string& type)
{
    std::string lower = toLower(type);
    if (lower.rfind("video", 0) == 0) {
        return MediaKind::Video;
    }
    if (lower == "story" || lower == "history" || lower == "description" || lower == "info") {
        return MediaKind::Text;
    }
    return MediaKind::Image;
}

std::vector<std::string> mediaTypes(const Index& index, int collection)
{
    std::vector<std::string> types = {
        "artwork_back", "artwork_front", "bezel", "logo", "marquee", "medium_back", "medium_front",
        "screenshot", "screentitle", "video", "videoPlayer", "videoSD",
        // OneSauce's
        "artwork_3d", "artwork_front_s", "cabinet", "fanart", "led_marquee", "neon", "story",
    };
    std::set<std::string> known;
    for (const std::string& type : types) {
        known.insert(toLower(type));
    }
    if (collection >= 0 && static_cast<size_t>(collection) < index.collections.size()) {
        // The folder holding <type> folders: the parent of any type's folder
        std::string medium = fs::path(index.properties.mediaPath(index.collections[static_cast<size_t>(collection)].name, "x", false))
            .parent_path().string();
        std::error_code ec;
        if (fs::is_directory(medium, ec)) {
            for (const auto& entry : fs::directory_iterator(medium, ec)) {
                std::string name;
                if (entry.is_directory(ec) && narrowName(entry.path().filename(), name) && known.insert(toLower(name)).second) {
                    types.push_back(name);
                }
            }
        }
    }
    return types;
}

MediaSlot gameMedia(const Index& index, int game, const std::string& type, MediaKind kind, int pageCollection)
{
    if (game < 0 || static_cast<size_t>(game) >= index.games.size()) {
        MediaSlot slot;
        slot.type = type;
        slot.kind = kind;
        return slot;
    }
    return gameMedia(index, index.games[static_cast<size_t>(game)], type, kind, pageCollection);
}

MediaSlot gameMedia(const Index& index, const GameRecord& record, const std::string& type, MediaKind kind, int pageCollection)
{
    MediaSlot slot;
    slot.type = type;
    slot.kind = kind;
    if (record.collection < 0 || static_cast<size_t>(record.collection) >= index.collections.size()) {
        return slot;
    }
    const std::string& own = index.collections[static_cast<size_t>(record.collection)].name;
    const std::string& page = pageCollection >= 0 && static_cast<size_t>(pageCollection) < index.collections.size()
        ? index.collections[static_cast<size_t>(pageCollection)].name : own;

    // ReloadableMedia's names: name, fullTitle, cloneof, then "default" (not for text)
    std::vector<std::pair<std::string, std::string>> names = { { record.name, "game name" }, { record.title, "title" } };
    if (!record.meta.cloneOf.empty()) {
        names.emplace_back(record.meta.cloneOf, "clone of");
    }
    if (kind != MediaKind::Text) {
        names.emplace_back("default", "default");
    }

    SlotBuilder builder(slot);
    std::string pageFolder = index.properties.mediaPath(page, type, false);
    std::string ownFolder = index.properties.mediaPath(own, type, false);
    for (const auto& [name, label] : names) {
        std::string basename = safeBasename(name);
        std::string why = label == "game name" ? "" : " (" + label + ")";
        builder.probe(pageFolder, basename, (page == own ? "game's collection" : "page collection") + why);
        builder.probe(ownFolder, basename, "game's collection" + why);
        if (kind != MediaKind::Text) {
            builder.probe(record.directory, type, "game's folder", false);
        }
    }
    if (kind == MediaKind::Text) {
        builder.probe(record.directory, type, "game's folder", false);
    }
    return slot;
}

MediaSlot collectionMedia(const Index& index, int collection, const std::string& type, MediaKind kind)
{
    MediaSlot slot;
    slot.type = type;
    slot.kind = kind;
    if (collection < 0 || static_cast<size_t>(collection) >= index.collections.size()) {
        return slot;
    }
    SlotBuilder builder(slot);
    builder.probe(index.properties.mediaPath(index.collections[static_cast<size_t>(collection)].name, type, true), type, "collection art");
    return slot;
}

MediaSlot menuEntryMedia(const Index& index, int menu, const std::string& child, const std::string& type, MediaKind kind)
{
    MediaSlot slot;
    slot.type = type;
    slot.kind = kind;
    if (menu < 0 || static_cast<size_t>(menu) >= index.collections.size()) {
        return slot;
    }
    const std::string& menuName = index.collections[static_cast<size_t>(menu)].name;
    std::vector<std::string> names = { child };
    if (kind != MediaKind::Text) {
        names.push_back("default");
    }
    // A menu entry's art can be renamed with collections.<child>.<type> (e.g. info.conf "logo = ...")
    std::string renamed;
    bool hasRename = index.properties.get("collections." + child + "." + type, renamed);

    SlotBuilder builder(slot);
    std::string menuFolder = index.properties.mediaPath(menuName, type, false);
    std::string childArt = index.properties.mediaPath(child, type, true);
    for (const std::string& name : names) {
        std::string basename = safeBasename(hasRename ? renamed : name);
        builder.probe(menuFolder, basename, name == "default" ? "menu (default)" : "menu");
        builder.probe(childArt, type, "child's collection art");
    }
    return slot;
}

}

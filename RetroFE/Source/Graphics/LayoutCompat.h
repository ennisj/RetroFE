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

#include <map>
#include <string>
#include <vector>

// Fork compatibility for layouts written for a newer RetroFE (the OneSauce
// themes). Only what the theme data itself makes clear is supported; see
// docs/ONESAUCE-COMPAT.md. No SDL here, so retrofe_tests can check it.
namespace LayoutCompat
{
    // A page drawn by a layout.lua instead of a layout.xml. RetroFE doesn't run
    // Lua; it only reads build() for its single
    //   pageBuilder:buildFromXmlFile(pageBuilder.layoutPath .. "<path>")
    // call, or a call to a function of a module the file require()s (looked up
    // in <theme>/lua/<module>.lua) whose body is that call with its argument
    // appended (luna.build(pageBuilder, "<path>")). <path> is relative to the
    // lua file's folder (a leading '/' or none). Returns the XML file it names,
    // or "" (why says why) when build() does anything else: a condition,
    // several builds, no build. The lua file's preUpdate/postUpdate hooks are
    // ignored.
    std::string luaBuildTarget(const std::string& luaFile, const std::string& themeFolder, std::string* why = nullptr);

    // transform="(x, y), (w, h); (x1, y1), (x2, y2), (x3, y3), (x4, y4)[; n]"
    // or "(x, y, w, h), (x1, y1), ..., (x4, y4)[, n]": the layout rectangle
    // x, y, w, h is drawn into the quad whose corners are top-left, top-right,
    // bottom-left, bottom-right, with perspective. The trailing number is
    // ignored. The "quad (x, y, u, v), ..." form isn't supported.
    struct QuadTransform
    {
        float src[4]{ 0, 0, 0, 0 };  // x, y, w, h
        float dst[8]{ 0, 0, 0, 0, 0, 0, 0, 0 };
        double h[9]{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };  // unit square -> quad (Heckbert)

        // Where layout point (x, y) lands. False when the point is at or behind
        // the quad's horizon (far outside the rectangle): don't draw it.
        bool apply(float x, float y, float& outX, float& outY) const;
    };

    // False (why says why) for anything that isn't a rectangle and four corners
    // of a convex quad in that order, or numbers that aren't finite.
    bool parseTransform(const std::string& value, QuadTransform& out, std::string* why = nullptr);

    // Coordinates a transformed point is clamped to (layout units)
    constexpr float maxCoordinate = 100000.0f;

    // mode="layout_preferred": layoutFolder when it has <basename>.<one of
    // extensions> (or a media folder <basename>/, below), else normalFolder
    std::string preferredFolder(const std::string& layoutFolder, const std::string& normalFolder,
                                const std::string& basename, const std::vector<std::string>& extensions);

    // Media folders. Where <prefix>.<ext> is missing, a folder <prefix>/
    // holding files with those extensions (any names) stands for it:
    // system_artwork/video/ for system_artwork/video.mp4,
    // medium_artwork/video/<game>/ for medium_artwork/video/<game>.mp4,
    // medium_artwork/background/default/ for .../default.png. One of its
    // files is shown; the media lookup's "another" request (removePlaylist,
    // OneSauce's B: "play different video") moves to the next.

    // The folder's files with one of extensions (compared ignoring case), in
    // natural order (naturalLess); empty when it isn't a folder. Full paths.
    std::vector<std::string> mediaFolderFiles(const std::string& folder, const std::vector<std::string>& extensions);

    // Which of files to show. current is the file showing now ("" if none):
    // it stays while it's one of them (the same folder serves the next item
    // too), or, when another is asked for, the next one in order does
    // (wrapping). Otherwise random picks one (any number: rand()).
    // files must not be empty.
    size_t pickMediaFile(const std::vector<std::string>& files, const std::string& current, bool another, unsigned random);

    // Names compared ignoring case, runs of digits as numbers ("Video 2" before "Video 10")
    bool naturalLess(const std::string& a, const std::string& b);

    // A file of the media folder <prefix>/ chosen by pickMediaFile, or false.
    // prefix is relative to RetroFE's folder or absolute. Cached until a
    // reload, like findMatchingFile: each parent folder's subfolder names are
    // read once (so a miss costs no file system call, and adds at most an
    // entry for a parent not seen before), each media folder's files once per
    // extension list (pass one of the lists below). Main thread only.
    bool findMediaFolderFile(const std::string& prefix, const std::vector<std::string>& extensions,
                             const std::string& current, bool another, unsigned random, std::string& file);

    // The extensions media folders take, lower case (compared ignoring case):
    // ImageBuilder's (with webp) and VideoBuilder's. Every caller passes these
    // same lists, since findMediaFolderFile keeps a listing per list.
    const std::vector<std::string>& mediaFolderImageExtensions();
    const std::vector<std::string>& mediaFolderVideoExtensions();

    // Forgets those caches (and nothing else), so folders and files added or
    // removed since show up: RetroFE's reload calls it with Utils::clearFileCache
    void clearMediaFolderCache();

    // Menu (scrolling list) art from a media folder, asked where the builders
    // found no <prefix>.<ext> file: findMediaFolderFile's random file, kept in
    // picks (the list's own, by prefix) so an item keeps its picture while
    // the list scrolls and builds it again, and items sharing a folder (a
    // "default/") show the same file, as with one file. Only hits are kept.
    bool menuFolderFile(const std::string& prefix, const std::vector<std::string>& extensions,
                        std::map<std::string, std::string>& picks, unsigned random, std::string& file);

    // RetroFE's lookup of <prefix>.<ext> (Utils::findMatchingFile, unchanged
    // when such a file exists), else findMediaFolderFile.
    bool findMedia(const std::string& prefix, const std::vector<std::string>& extensions,
                   const std::string& current, bool another, unsigned random, std::string& file);

    // One step of the reloadable media lookup (ReloadableMedia::findComponent):
    // <folder>/<basename>.<one of fileExtensions> as upstream (file set,
    // fromFolder false), else a media folder's file (fromFolder true), else
    // nothing. No media folder at a game's own ROM folder step (romFolder:
    // a PC game's video\ or music\ there isn't art), nor when a file with one
    // of folderExtensions exists (the image builders also take .webp).
    struct MediaLookup
    {
        std::string file;
        bool fromFolder = false;
    };
    MediaLookup lookupMediaStep(const std::string& folder, const std::string& basename, bool romFolder,
                                const std::vector<std::string>& fileExtensions, const std::vector<std::string>& folderExtensions,
                                const std::string& current, bool another, unsigned random);

    // <scrollingText src>: src against the page's folder, then the layout's
    // folder, as an image's src; "" when neither exists. Normalised.
    std::string themeFile(const std::string& pageFolder, const std::string& themeFolder, const std::string& src);
}

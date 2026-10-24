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
#include "VideoCollections.h"
#include "../Database/Configuration.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace VideoCollections
{

const char* const typeKey = "type";
const char* const typeVideos = "videos";
const char* const playKey = "play";
const char* const playHere = "here";
const char* const playLauncher = "launcher";

bool isVideoType(const std::string& type)
{
    return Utils::toLower(Utils::trimEnds(type)) == typeVideos;
}

bool playsHere(const std::string& type, const std::string& play)
{
    return isVideoType(type) && Utils::toLower(Utils::trimEnds(play)) == playHere;
}

bool isPlayValue(const std::string& play)
{
    std::string value = Utils::toLower(Utils::trimEnds(play));
    return value == playHere || value == playLauncher;
}

const std::vector<std::string>& playableExtensions()
{
    static const std::vector<std::string> extensions = { "mp4", "avi", "mkv", "mp3", "wav", "flac" };
    return extensions;
}

bool isPlayableExtension(const std::string& extension)
{
    const auto& known = playableExtensions();
    if (std::find(known.begin(), known.end(), extension) != known.end()) {
        return true;
    }
    std::string lower = Utils::toLower(extension);
#ifdef WIN32
    return std::find(known.begin(), known.end(), lower) != known.end();
#else
    // VideoBuilder knows "mp4" and "MP4", not "Mp4"
    std::string upper = extension;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper == extension && std::find(known.begin(), known.end(), lower) != known.end();
#endif
}

namespace
{
    std::string property(Configuration& config, const std::string& collection, const char* key)
    {
        std::string value;
        config.getProperty("collections." + collection + "." + key, value);
        return value;
    }
}

bool isVideoCollection(Configuration& config, const std::string& collection)
{
    return !collection.empty() && isVideoType(property(config, collection, typeKey));
}

bool playsHere(Configuration& config, const std::string& collection)
{
    return !collection.empty() && playsHere(property(config, collection, typeKey), property(config, collection, playKey));
}

bool useItemsAsVideoMedia(Configuration& config, const std::string& collection)
{
    if (!isVideoCollection(config, collection)) {
        return false;
    }
    std::string key = "collections." + collection + ".media.video";
    if (config.propertyExists(key)) {
        return false;
    }
    std::error_code ec;
    std::string usual;
    config.getMediaPropertyAbsolutePath(collection, "video", usual);
    if (std::filesystem::is_directory(usual, ec)) {
        return false;   // it has video media of its own
    }

    // The first list.path folder, as CollectionInfoBuilder reads it (";" separates several)
    std::string listPath;
    if (config.getProperty("collections." + collection + ".list.path", listPath)) {
        std::vector<std::string> folders;
        Utils::listToVector(listPath, folders, ';');
        listPath.clear();
        for (const std::string& folder : folders) {
            if (!Utils::trimEnds(folder).empty()) {
                listPath = Configuration::convertToAbsolutePath(Configuration::absolutePath, Utils::trimEnds(folder));
                break;
            }
        }
    }
    if (listPath.empty()) {
        config.getCollectionAbsolutePath(collection, listPath);
    }
    if (listPath.empty()) {
        return false;
    }
    config.setProperty(key, listPath);
    return true;
}

}

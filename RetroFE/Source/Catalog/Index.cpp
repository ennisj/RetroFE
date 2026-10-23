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
#include "Index.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool isHex(const std::string& s, size_t length)
    {
        return s.size() == length && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
    }

    std::string stringField(const json& object, const char* name)
    {
        auto it = object.find(name);
        return it != object.end() && it->is_string() ? it->get<std::string>() : "";
    }

    // Where a kind installs when a file doesn't say
    std::string defaultTarget(const std::string& kind)
    {
        if (kind == "collection") return "collections";
        if (kind == "layout") return "layouts";
        if (kind == "media") return "media";
        return "root";
    }

    // Reads one package; returns "" if it's usable, otherwise why not
    std::string readPackage(const json& entry, const std::string& location, const std::string& sourceName, Catalog::Package& package)
    {
        if (!entry.is_object()) {
            return "a package isn't a JSON object";
        }
        package = Catalog::Package();
        package.source = sourceName;
        package.id = stringField(entry, "id");
        if (!Catalog::isValidId(package.id)) {
            return "package id \"" + package.id + "\" isn't valid (letters, digits, . _ - only)";
        }
        std::string prefix = "package " + package.id + ": ";

        package.name = stringField(entry, "name");
        if (package.name.empty()) {
            package.name = package.id;
        }
        package.kind = lower(stringField(entry, "kind"));
        if (package.kind.empty()) {
            package.kind = "other";
        }
        package.version = stringField(entry, "version");
        package.description = stringField(entry, "description");

        if (auto artwork = entry.find("artwork"); artwork != entry.end() && artwork->is_object()) {
            for (const auto& [type, value] : artwork->items()) {
                if (value.is_string() && !value.get<std::string>().empty()) {
                    package.artwork[type] = Catalog::resolveLocation(location, value.get<std::string>());
                }
            }
        }

        if (auto requirements = entry.find("requires"); requirements != entry.end() && requirements->is_array()) {
            for (const json& requirement : *requirements) {
                Catalog::Requirement r;
                if (requirement.is_string()) {
                    r.id = requirement.get<std::string>();
                }
                else if (requirement.is_object()) {
                    r.id = stringField(requirement, "id");
                    r.version = stringField(requirement, "version");
                }
                if (Catalog::isValidId(r.id)) {
                    package.requirements.push_back(r);
                }
            }
        }

        auto files = entry.find("files");
        if (files == entry.end() || !files->is_array() || files->empty()) {
            return prefix + "no files";
        }
        for (const json& entryFile : *files) {
            if (!entryFile.is_object()) {
                return prefix + "a file isn't a JSON object";
            }
            Catalog::PackageFile file;
            std::string url = stringField(entryFile, "url");
            if (url.empty()) {
                return prefix + "a file has no url";
            }
            file.url = Catalog::resolveLocation(location, url);

            if (auto size = entryFile.find("size"); size != entryFile.end()) {
                if (!size->is_number_unsigned() && !(size->is_number_integer() && size->get<int64_t>() >= 0)) {
                    return prefix + "size isn't a whole number";
                }
                file.size = size->get<uint64_t>();
            }
            file.sha256 = lower(stringField(entryFile, "sha256"));
            if (!file.sha256.empty() && !isHex(file.sha256, 64)) {
                return prefix + "sha256 isn't 64 hex digits";
            }
            file.md5 = lower(stringField(entryFile, "md5"));
            if (!file.md5.empty() && !isHex(file.md5, 32)) {
                return prefix + "md5 isn't 32 hex digits";
            }

            file.target = lower(stringField(entryFile, "target"));
            if (file.target.empty()) {
                file.target = defaultTarget(package.kind);
            }
            if (!Catalog::isKnownTarget(file.target)) {
                return prefix + "unknown target \"" + file.target + "\"";
            }
            file.path = stringField(entryFile, "path");
            if (!Catalog::isSafeRelativePath(file.path)) {
                return prefix + "path \"" + file.path + "\" leaves its target";
            }
            package.files.push_back(file);
        }
        return "";
    }
}

namespace Catalog
{

bool isUrl(const std::string& location)
{
    std::string start = lower(location.substr(0, 8));
    return start.rfind("http://", 0) == 0 || start.rfind("https://", 0) == 0;
}

std::string resolveLocation(const std::string& base, const std::string& reference)
{
    if (isUrl(reference) || reference.empty()) {
        return reference;
    }
    if (isUrl(base)) {
        // Relative to the folder the index is in, ignoring any query
        std::string folder = base.substr(0, base.find_first_of("?#"));
        if (!reference.empty() && reference[0] == '/') {
            size_t hostEnd = folder.find('/', folder.find("//") + 2);
            return (hostEnd == std::string::npos ? folder : folder.substr(0, hostEnd)) + reference;
        }
        folder = folder.substr(0, folder.rfind('/') + 1);
        std::string rest = reference;
        while (true) {
            if (rest.rfind("./", 0) == 0) {
                rest.erase(0, 2);
            }
            else if (rest.rfind("../", 0) == 0) {
                rest.erase(0, 3);
                size_t hostEnd = folder.find('/', folder.find("//") + 2);
                size_t previous = folder.rfind('/', folder.size() - 2);
                if (previous != std::string::npos && previous >= hostEnd) {
                    folder = folder.substr(0, previous + 1);
                }
            }
            else {
                break;
            }
        }
        return folder + rest;
    }

    fs::path referencePath(reference);
    if (referencePath.is_absolute() || referencePath.has_root_name()) {
        return referencePath.lexically_normal().string();
    }
    return (fs::path(base).parent_path() / referencePath).lexically_normal().string();
}

bool isValidId(const std::string& id)
{
    if (id.empty() || id.size() > 100 || id[0] == '.') {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '_' || c == '-';
    });
}

bool isSafeRelativePath(const std::string& path)
{
    if (path.empty()) {
        return true;
    }
    if (path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) {
        return false;
    }
    std::string part;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (part == "..") {
                return false;
            }
            part.clear();
        }
        else {
            part += path[i];
        }
    }
    return true;
}

bool parseIndex(const std::string& text, const std::string& location, const std::string& sourceName,
                std::vector<Package>& packages, std::vector<std::string>& warnings, std::string& error)
{
    json document = json::parse(text, nullptr, false);
    if (document.is_discarded()) {
        error = "Not valid JSON";
        return false;
    }
    if (!document.is_object() || !document.contains("packages") || !document["packages"].is_array()) {
        error = "Not a catalog index: no \"packages\" list";
        return false;
    }
    if (auto format = document.find("format"); format != document.end() && format->is_number_integer() && format->get<int>() > 1) {
        warnings.push_back("Index format " + std::to_string(format->get<int>()) + " is newer than this RetroFE; some packages may be missing");
    }

    std::set<std::string> seen;
    for (const json& entry : document["packages"]) {
        Package package;
        std::string problem = readPackage(entry, location, sourceName, package);
        if (!problem.empty()) {
            warnings.push_back(problem);
            continue;
        }
        if (!seen.insert(package.id).second) {
            warnings.push_back("package " + package.id + " is listed twice; using the first");
            continue;
        }
        packages.push_back(std::move(package));
    }
    return true;
}

bool parsePackage(const std::string& text, const std::string& location, const std::string& sourceName,
                  Package& package, std::string& error)
{
    json document = json::parse(text, nullptr, false);
    if (document.is_discarded()) {
        error = "Not valid JSON";
        return false;
    }
    error = readPackage(document, location, sourceName, package);
    return error.empty();
}

}

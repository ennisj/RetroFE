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
#include "Sources.h"
#include "GitHub.h"
#include "Http.h"
#include "Index.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    std::string trim(const std::string& s)
    {
        size_t start = s.find_first_not_of(" \t\r");
        if (start == std::string::npos) {
            return "";
        }
        size_t end = s.find_last_not_of(" \t\r");
        return s.substr(start, end - start + 1);
    }

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool isValidName(const std::string& name)
    {
        return !name.empty() && !Catalog::isReservedName(name) && std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) != 0 || c == '_' || c == '-';
        });
    }

    bool readFile(const std::string& path, std::string& content)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return true;
    }

    bool writeFile(const std::string& path, const std::string& content)
    {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << content;
        return static_cast<bool>(file);
    }

    // "Some Pack v1.2" -> name "Some Pack", version "1.2"
    void splitVersion(const std::string& stem, std::string& name, std::string& version)
    {
        static const std::regex pattern(R"(^(.*?)[ _-]+v(\d[0-9A-Za-z.]*)$)");
        std::smatch match;
        if (std::regex_match(stem, match, pattern)) {
            name = match[1];
            version = match[2];
        }
        else {
            name = stem;
            version.clear();
        }
    }

    // A file name's stem made into a valid id: "Sega Genesis" -> "sega-genesis"
    std::string idFromName(const std::string& name)
    {
        std::string id;
        for (unsigned char c : name) {
            if (std::isalnum(c)) {
                id += static_cast<char>(std::tolower(c));
            }
            else if (!id.empty() && id.back() != '-') {
                id += '-';
            }
        }
        while (!id.empty() && id.back() == '-') {
            id.pop_back();
        }
        return id;
    }

    void readFolder(const Catalog::SourceConfig& source, Catalog::SourceContents& contents)
    {
        std::error_code ec;
        if (!fs::is_directory(source.path, ec)) {
            contents.error = "Folder not found: " + source.path;
            return;
        }

        // A folder can carry a full index instead of loose packages
        fs::path index = fs::path(source.path) / "catalog.json";
        if (fs::exists(index, ec)) {
            std::string text;
            if (!readFile(index.string(), text)) {
                contents.error = "Could not read " + index.string();
                return;
            }
            if (!Catalog::parseIndex(text, index.string(), source.name, contents.packages, contents.warnings, contents.error)) {
                contents.error = index.string() + ": " + contents.error;
            }
            return;
        }

        std::vector<fs::path> archives;
        for (const auto& entry : fs::directory_iterator(source.path, ec)) {
            if (entry.is_regular_file() && lower(entry.path().extension().string()) == ".zip") {
                archives.push_back(entry.path());
            }
        }
        std::sort(archives.begin(), archives.end());

        std::set<std::string> seen;
        for (const fs::path& archive : archives) {
            Catalog::Package package;
            fs::path sidecar = archive;
            sidecar += ".package.json";

            if (fs::exists(sidecar, ec)) {
                // The sidecar describes the archive next to it; its files default to that archive
                std::string text;
                json document = readFile(sidecar.string(), text) ? json::parse(text, nullptr, false) : json();
                if (!document.is_object()) {
                    contents.warnings.push_back(sidecar.filename().string() + ": not valid JSON");
                    continue;
                }
                if (!document.contains("files")) {
                    document["files"] = json::array({ json{ { "url", archive.filename().string() },
                                                            { "size", fs::file_size(archive, ec) } } });
                }
                std::string error;
                if (!Catalog::parsePackage(document.dump(), sidecar.string(), source.name, package, error)) {
                    contents.warnings.push_back(sidecar.filename().string() + ": " + error);
                    continue;
                }
            }
            else {
                // No description: name and version come from the file name
                std::string stem = archive.stem().string();
                splitVersion(stem, package.name, package.version);
                package.id = idFromName(package.name);
                if (!Catalog::isValidId(package.id)) {
                    contents.warnings.push_back(archive.filename().string() + ": can't make a package id from its name");
                    continue;
                }
                if (package.id == Catalog::appPackageId) {
                    contents.warnings.push_back(archive.filename().string() + ": RetroFE itself only installs from a GitHub source");
                    continue;
                }
                package.source = source.name;
                package.kind = "other";
                Catalog::PackageFile file;
                file.url = archive.string();
                file.size = fs::file_size(archive, ec);
                file.target = "root";
                package.files.push_back(file);
            }

            if (!seen.insert(package.id).second) {
                contents.warnings.push_back("package " + package.id + " is in the folder twice; using the first");
                continue;
            }
            contents.packages.push_back(std::move(package));
        }
    }

    void readIndex(const Catalog::SourceConfig& source, const std::string& cacheFolder, Catalog::Http* http,
                   Catalog::SourceContents& contents)
    {
        std::string text;
        std::string error;
        std::string cacheFile = (fs::path(cacheFolder) / source.name / "index.json").string();

        bool fetched = false;
        if (Catalog::isUrl(source.url)) {
            if (!http) {
                error = "Downloading isn't supported on this system yet";
            }
            else {
                fetched = http->get(source.url, text, error);
            }
        }
        else {
            fetched = readFile(source.url, text);
            if (!fetched) {
                error = "Could not read " + source.url;
            }
        }

        if (fetched) {
            // Keep the last good copy for when the source can't be reached
            std::vector<Catalog::Package> packages;
            std::vector<std::string> warnings;
            std::string parseError;
            if (Catalog::parseIndex(text, source.url, source.name, packages, warnings, parseError)) {
                if (Catalog::isUrl(source.url)) {
                    writeFile(cacheFile, text);
                }
                contents.packages = std::move(packages);
                contents.warnings = std::move(warnings);
                return;
            }
            error = parseError;
        }

        std::string cached;
        if (Catalog::isUrl(source.url) && readFile(cacheFile, cached)
            && Catalog::parseIndex(cached, source.url, source.name, contents.packages, contents.warnings, contents.error)) {
            contents.fromCache = true;
            contents.warnings.insert(contents.warnings.begin(), "Showing the last copy: " + error);
            return;
        }
        contents.error = error;
    }
}

namespace Catalog
{

bool loadSources(const std::string& file, std::vector<SourceConfig>& sources, std::string& error)
{
    sources.clear();
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return true;
    }
    std::ifstream in(file);
    if (!in) {
        error = "Could not read " + file;
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        // Only whole-line comments: URLs can contain #
        std::string content = trim(line);
        if (content.empty() || content[0] == '#') {
            continue;
        }
        size_t equals = content.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = trim(content.substr(0, equals));
        std::string value = trim(content.substr(equals + 1));

        // sources.<name>.<setting>
        if (key.rfind("sources.", 0) != 0) {
            continue;
        }
        size_t dot = key.find('.', 8);
        if (dot == std::string::npos) {
            continue;
        }
        std::string name = key.substr(8, dot - 8);
        std::string setting = key.substr(dot + 1);
        if (!isValidName(name)) {
            continue;
        }

        auto it = std::find_if(sources.begin(), sources.end(), [&](const SourceConfig& s) { return s.name == name; });
        if (it == sources.end()) {
            SourceConfig source;
            source.name = name;
            sources.push_back(source);
            it = sources.end() - 1;
        }
        if (setting == "type") it->type = lower(value);
        else if (setting == "url") it->url = value;
        else if (setting == "path") it->path = value;
        else if (setting == "repo") it->repo = value;
        else if (setting == "prerelease") {
            std::string v = lower(value);
            it->prerelease = v == "yes" || v == "true" || v == "on" || v == "1";
        }
        else if (setting == "app") {
            std::string v = lower(value);
            it->app = v == "yes" || v == "true" || v == "on" || v == "1";
        }
        else if (setting == "enabled") {
            std::string v = lower(value);
            it->enabled = !(v == "no" || v == "false" || v == "off" || v == "0");
        }
    }
    return true;
}

bool saveSources(const std::string& file, const std::vector<SourceConfig>& sources, std::string& error)
{
    std::ostringstream out;
    out << "# Catalog sources, one block per source: sources.<name>.<setting>\n"
        << "# Written by RetroFE; Manage > Sources adds and removes them.\n";
    for (const SourceConfig& source : sources) {
        out << "\n";
        out << "sources." << source.name << ".type = " << source.type << "\n";
        if (!source.url.empty()) {
            out << "sources." << source.name << ".url = " << source.url << "\n";
        }
        if (!source.path.empty()) {
            out << "sources." << source.name << ".path = " << source.path << "\n";
        }
        if (!source.repo.empty()) {
            out << "sources." << source.name << ".repo = " << source.repo << "\n";
        }
        if (source.prerelease) {
            out << "sources." << source.name << ".prerelease = yes\n";
        }
        if (source.app) {
            out << "sources." << source.name << ".app = yes\n";
        }
        out << "sources." << source.name << ".enabled = " << (source.enabled ? "yes" : "no") << "\n";
    }

    // Swap the new file in so a failed write can't lose the old one
    std::string temp = file + ".tmp";
    if (!writeFile(temp, out.str())) {
        error = "Could not write " + file;
        return false;
    }
    std::error_code ec;
    fs::rename(temp, file, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "Could not replace " + file;
        return false;
    }
    return true;
}

std::string validateSource(const SourceConfig& source, const std::vector<SourceConfig>& existing)
{
    if (!isValidName(source.name)) {
        return "Give the source a name: letters, digits, _ and - only.";
    }
    for (const SourceConfig& other : existing) {
        if (lower(other.name) == lower(source.name)) {
            return "There's already a source called " + other.name + ".";
        }
    }
    if (source.type == "index") {
        if (source.url.empty()) {
            return "Enter the address of the catalog index.";
        }
        if (!isUrl(source.url)) {
            std::error_code ec;
            if (!fs::is_regular_file(source.url, ec)) {
                return "That index file doesn't exist.";
            }
        }
        else if (!isAllowedLocation(source.url)) {
            return "Use an https:// address. Plain http:// only works on your own network.";
        }
        return "";
    }
    if (source.type == "folder") {
        std::error_code ec;
        if (source.path.empty() || !fs::is_directory(source.path, ec)) {
            return "Choose a folder that exists.";
        }
        return "";
    }
    if (source.type == "github") {
        if (!isValidRepository(source.repo)) {
            return std::string("Enter the repository as owner/name, e.g. ") + retrofeRepository + ".";
        }
        if (!source.url.empty() && (!isUrl(source.url) || !isAllowedLocation(source.url))) {
            return "Use an https:// address for the releases.";
        }
        for (const SourceConfig& other : existing) {
            if (other.type == "github" && lower(other.repo) == lower(source.repo)) {
                return "There's already a source for " + other.repo + ": " + other.name + ".";
            }
        }
        return "";
    }
    return "Choose what kind of source it is.";
}

SourceContents readSource(const SourceConfig& source, const std::string& cacheFolder, Http* http)
{
    SourceContents contents;
    if (source.type == "folder") {
        readFolder(source, contents);
    }
    else if (source.type == "index") {
        readIndex(source, cacheFolder, http, contents);
    }
    else if (source.type == "github") {
        readGitHub(source, cacheFolder, http, static_cast<int64_t>(std::time(nullptr)), contents);
    }
    else {
        contents.error = "Unknown source type \"" + source.type + "\"";
    }
    return contents;
}

}

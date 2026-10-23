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
#include "MetadataOverrides.h"
#include "Files.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <rapidxml.hpp>
#include <sqlite3.h>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{
namespace MetadataOverrides
{

namespace
{
    std::string escape(const std::string& text)
    {
        std::string result;
        for (char c : text) {
            switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += c;
            }
        }
        return result;
    }

    // The metadata type a meta/ or override file belongs to: its stem up to the first '.'
    std::string typeOf(const fs::path& file)
    {
        std::string stem;
        if (!narrowName(file.stem(), stem)) {
            return "";
        }
        return stem.substr(0, stem.find_first_of('.'));
    }

    bool parseFile(const std::string& file, Overrides& overrides, const std::string* onlyName, std::string& error)
    {
        std::string content;
        if (!readFile(file, content)) {
            error = "Could not read " + file;
            return false;
        }
        std::vector<char> buffer(content.begin(), content.end());
        buffer.push_back('\0');
        rapidxml::xml_document<> doc;
        try {
            doc.parse<0>(buffer.data());
        }
        catch (const rapidxml::parse_error& e) {
            error = file + " isn't valid XML: " + e.what();
            return false;
        }
        rapidxml::xml_node<>* root = doc.first_node("menu");
        if (!root) {
            error = file + " has no <menu> element";
            return false;
        }
        for (rapidxml::xml_node<>* game = root->first_node("game"); game; game = game->next_sibling("game")) {
            rapidxml::xml_attribute<>* nameAttribute = game->first_attribute("name");
            if (!nameAttribute || nameAttribute->value()[0] == '\0') {
                continue;
            }
            std::string name = nameAttribute->value();
            if (onlyName && name != *onlyName) {
                continue;
            }
            std::map<std::string, std::string>& values = overrides[name];
            for (rapidxml::xml_node<>* field = game->first_node(); field; field = field->next_sibling()) {
                if (field->type() == rapidxml::node_element && findField(field->name())) {
                    values[findField(field->name())->element] = field->value();
                }
            }
        }
        return true;
    }
}

const std::vector<Field>& fields()
{
    static const std::vector<Field> all = {
        { "description", "title", "Title" },
        { "manufacturer", "manufacturer", "Manufacturer" },
        { "developer", "developer", "Developer" },
        { "year", "year", "Year" },
        { "genre", "genre", "Genre" },
        { "players", "players", "Players" },
        { "rating", "rating", "Rating" },
        { "score", "score", "Score" },
        { "cloneof", "cloneOf", "Clone of" },
        { "ctrltype", "ctrltype", "Controls" },
        { "buttons", "buttons", "Buttons" },
        { "joyways", "joyways", "Joystick ways" },
    };
    return all;
}

const Field* findField(const std::string& elementOrColumn)
{
    for (const Field& field : fields()) {
        if (iequals(elementOrColumn, field.element) || iequals(elementOrColumn, field.column)) {
            return &field;
        }
    }
    // "title" as an element name means description
    if (iequals(elementOrColumn, "title")) {
        return &fields()[0];
    }
    return nullptr;
}

std::string folder(const std::string& root)
{
    return Utils::combinePath(root, "manage", "metadata");
}

std::string fileFor(const std::string& root, const std::string& metadataType)
{
    return Utils::combinePath(folder(root), metadataType + ".xml");
}

bool read(const std::string& file, Overrides& overrides, std::string& error)
{
    overrides.clear();
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return true;
    }
    return parseFile(file, overrides, nullptr, error);
}

bool write(const std::string& file, const Overrides& overrides, std::string& error)
{
    std::error_code ec;
    bool any = false;
    for (const auto& [name, values] : overrides) {
        any |= !values.empty();
    }
    if (!any) {
        fs::remove(file, ec);
        return !ec;
    }
    const std::string nl = defaultNewline();
    std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + nl
        + "<!-- Metadata changed in RetroFE's Manage screens. RetroFE applies these fields" + nl
        + "     over the shipped metadata in meta/ every time it imports it. -->" + nl
        + "<menu>" + nl;
    for (const auto& [name, values] : overrides) {
        if (values.empty()) {
            continue;
        }
        text += "\t<game name=\"" + escape(name) + "\">" + nl;
        // In the fields' order, so the file reads like a hyperlist
        for (const Field& field : fields()) {
            auto it = values.find(field.element);
            if (it != values.end()) {
                text += std::string("\t\t<") + field.element + ">" + escape(it->second) + "</" + field.element + ">" + nl;
            }
        }
        text += "\t</game>" + nl;
    }
    text += "</menu>" + nl;
    return writeFileAtomic(file, text, error);
}

std::string checkValue(const std::string& element, const std::string& value)
{
    const Field* field = findField(element);
    if (!field) {
        return "\"" + element + "\" isn't a metadata field.";
    }
    if (value.size() > 200) {
        return "Keep it to 200 characters or fewer.";
    }
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f) {
            return "Use a single line of text.";
        }
    }
    if (std::string(field->element) == "description" && Utils::trimEnds(value).empty()) {
        return "A game needs a title.";
    }
    return "";
}

bool apply(sqlite3* db, const std::string& metadataType, const std::string& name,
           const std::map<std::string, std::string>& values, std::string& error)
{
    if (!db) {
        error = "No metadata database";
        return false;
    }
    auto run = [&](const std::string& sql, const std::vector<std::string>& args) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(db);
            return false;
        }
        for (size_t i = 0; i < args.size(); i++) {
            sqlite3_bind_text(stmt, static_cast<int>(i + 1), args[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            error = sqlite3_errmsg(db);
        }
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    };

    auto title = values.find("description");
    std::string initialTitle = title != values.end() ? title->second : name;
    if (!run("INSERT OR IGNORE INTO Meta (collectionName, name, title) VALUES (?,?,?);", { metadataType, name, initialTitle })) {
        return false;
    }
    for (const auto& [element, value] : values) {
        const Field* field = findField(element);
        if (!field) {
            continue;
        }
        // Column names come from the fixed table above, never from the file
        if (!run(std::string("UPDATE Meta SET ") + field->column + " = ? WHERE collectionName = ? AND name = ?;",
                 { value, metadataType, name })) {
            return false;
        }
    }
    return true;
}

int applyAll(sqlite3* db, const std::string& root)
{
    std::error_code ec;
    std::string path = folder(root);
    if (!db || !fs::is_directory(path, ec)) {
        return 0;
    }
    int applied = 0;
    sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, nullptr);
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        std::string file;
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".xml" || !narrowName(entry.path(), file)) {
            continue;
        }
        std::string type = typeOf(entry.path());
        Overrides overrides;
        std::string error;
        if (type.empty() || !parseFile(file, overrides, nullptr, error)) {
            LOG_ERROR("Metadata", "Skipping metadata changes in " + file + ": " + error);
            continue;
        }
        LOG_INFO("Metadata", "Applying metadata changes from " + file);
        for (const auto& [name, values] : overrides) {
            if (!values.empty() && apply(db, type, name, values, error)) {
                applied++;
            }
            else if (!error.empty()) {
                LOG_ERROR("Metadata", "Could not apply metadata changes for " + name + ": " + error);
                error.clear();
            }
        }
    }
    sqlite3_exec(db, "COMMIT TRANSACTION;", nullptr, nullptr, nullptr);
    return applied;
}

fs::file_time_type newestChange(const std::string& root)
{
    fs::file_time_type newest = fs::file_time_type::min();
    std::error_code ec;
    std::string path = folder(root);
    if (!fs::is_directory(path, ec)) {
        return newest;
    }
    newest = fs::last_write_time(path, ec);
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        fs::file_time_type time = fs::last_write_time(entry.path(), ec);
        if (!ec && time > newest) {
            newest = time;
        }
    }
    return newest;
}

namespace
{
    std::map<std::string, std::string> emptyRow()
    {
        std::map<std::string, std::string> row;
        for (const Field& field : fields()) {
            row[field.element] = "";
        }
        return row;
    }

    // The files of one meta/ folder with an extension, in directory order (as RetroFE imports them)
    std::vector<fs::path> metaFiles(const std::string& root, const char* folder, const char* extension)
    {
        std::vector<fs::path> files;
        std::error_code ec;
        std::string path = Utils::combinePath(root, "meta", folder);
        if (!fs::is_directory(path, ec)) {
            return files;
        }
        for (const auto& entry : fs::directory_iterator(path, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == extension) {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    struct XmlFile
    {
        std::vector<char> buffer;
        rapidxml::xml_document<> doc;

        bool load(const fs::path& path, std::string& error)
        {
            std::string file;
            std::string content;
            if (!narrowName(path, file) || !readFile(file, content)) {
                error = "Could not read " + narrow(path);
                return false;
            }
            buffer.assign(content.begin(), content.end());
            buffer.push_back('\0');
            try {
                doc.parse<0>(buffer.data());
            }
            catch (const rapidxml::parse_error& e) {
                error = narrow(path) + " isn't valid XML: " + e.what();
                return false;
            }
            return true;
        }
    };

    std::string valueOf(rapidxml::xml_node<>* node, const char* child)
    {
        rapidxml::xml_node<>* found = node ? node->first_node(child) : nullptr;
        return found ? found->value() : "";
    }
}

Shipped shipped(const std::string& root, const std::string& metadataType, const std::string& name,
                std::map<std::string, std::string>& values, std::string* error)
{
    values.clear();
    bool found = false;
    std::string problem;

    // meta/hyperlist/<type>*.xml (MetadataDatabase::importHyperlist)
    for (const fs::path& path : metaFiles(root, "hyperlist", ".xml")) {
        if (typeOf(path) != metadataType) {
            continue;
        }
        Overrides one;
        if (!parseFile(narrow(path), one, &name, problem)) {
            if (error) {
                *error = problem;
            }
            return Shipped::Unknown;
        }
        if (one.count(name)) {
            // Importing replaces the whole row, so a later file's entry wins entirely
            values = emptyRow();
            for (const auto& [element, value] : one[name]) {
                values[element] = value;
            }
            found = true;
        }
    }

    // meta/mamelist/<type>*.xml (importMamelist): <mame><game|machine name= cloneof=>
    for (const fs::path& path : metaFiles(root, "mamelist", ".xml")) {
        if (typeOf(path) != metadataType) {
            continue;
        }
        XmlFile xml;
        if (!xml.load(path, problem)) {
            if (error) {
                *error = problem;
            }
            return Shipped::Unknown;
        }
        rapidxml::xml_node<>* mame = xml.doc.first_node("mame");
        if (!mame) {
            continue;
        }
        const char* nodeName = mame->first_node("game") ? "game" : "machine";
        for (rapidxml::xml_node<>* game = mame->first_node(nodeName); game; game = game->next_sibling()) {
            rapidxml::xml_attribute<>* attribute = game->first_attribute("name");
            if (!attribute || name != attribute->value()) {
                continue;
            }
            std::map<std::string, std::string> row = emptyRow();
            rapidxml::xml_node<>* description = game->first_node("description");
            row["description"] = description ? description->value() : attribute->value();
            row["year"] = valueOf(game, "year");
            row["manufacturer"] = valueOf(game, "manufacturer");
            row["genre"] = valueOf(game, "genre");
            rapidxml::xml_attribute<>* cloneOf = game->first_attribute("cloneof");
            row["cloneof"] = cloneOf ? cloneOf->value() : "";
            if (rapidxml::xml_node<>* input = game->first_node("input")) {
                if (rapidxml::xml_attribute<>* players = input->first_attribute("players")) {
                    row["players"] = players->value();
                }
                if (rapidxml::xml_attribute<>* buttons = input->first_attribute("buttons")) {
                    row["buttons"] = buttons->value();
                }
            }
            values = row;
            found = true;
        }
    }

    // meta/emuarc/*.dat (importEmuArclist): the type is the header name up to " - "
    for (const fs::path& path : metaFiles(root, "emuarc", ".dat")) {
        XmlFile xml;
        if (!xml.load(path, problem)) {
            if (error) {
                *error = problem;
            }
            return Shipped::Unknown;
        }
        rapidxml::xml_node<>* datafile = xml.doc.first_node("datafile");
        rapidxml::xml_node<>* header = datafile ? datafile->first_node("header") : nullptr;
        rapidxml::xml_node<>* headerName = header ? header->first_node("name") : nullptr;
        if (!headerName) {
            continue;
        }
        std::string type = headerName->value();
        type = type.substr(0, type.find(" - "));
        if (type != metadataType) {
            continue;
        }
        for (rapidxml::xml_node<>* game = datafile->first_node("game"); game; game = game->next_sibling("game")) {
            rapidxml::xml_node<>* emuarc = game->first_node("EmuArc");
            if (!emuarc || valueOf(game, "description") != name) {
                continue;
            }
            std::map<std::string, std::string> row = emptyRow();
            row["description"] = name;
            row["cloneof"] = valueOf(emuarc, "cloneof");
            row["manufacturer"] = valueOf(emuarc, "publisher");
            row["developer"] = valueOf(emuarc, "developer");
            row["year"] = valueOf(emuarc, "year");
            std::string subgenre = valueOf(emuarc, "subgenre");
            row["genre"] = valueOf(emuarc, "genre") + (subgenre.empty() ? "" : "_" + subgenre);
            row["rating"] = valueOf(emuarc, "ratings");
            row["score"] = valueOf(emuarc, "score");
            row["players"] = valueOf(emuarc, "players");
            values = row;
            found = true;
        }
    }
    return found ? Shipped::Found : Shipped::NotFound;
}

bool removeRow(sqlite3* db, const std::string& metadataType, const std::string& name, std::string& error)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM Meta WHERE collectionName = ? AND name = ?;", -1, &stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    sqlite3_bind_text(stmt, 1, metadataType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

}
}

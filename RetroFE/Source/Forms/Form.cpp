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
#include "Form.h"
#include "../Database/Configuration.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <rapidxml.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

using namespace rapidxml;

namespace
{
    const std::map<std::string, Forms::FieldType> fieldTypes = {
        { "section", Forms::FieldType::Section },
        { "actions", Forms::FieldType::Actions },
        { "text",    Forms::FieldType::Text },
        { "folder",  Forms::FieldType::Folder },
        { "file",    Forms::FieldType::File },
        { "toggle",  Forms::FieldType::Toggle },
        { "number",  Forms::FieldType::Number },
        { "choice",  Forms::FieldType::Choice },
        { "list",    Forms::FieldType::List },
        { "note",    Forms::FieldType::Note },
        { "button",  Forms::FieldType::Button },
        { "results", Forms::FieldType::Results },
        { "link",    Forms::FieldType::Link },
        { "value",   Forms::FieldType::Value },
        { "image",   Forms::FieldType::Image },
    };

    std::string attribute(const xml_node<>* node, const char* name)
    {
        const xml_attribute<>* attr = node->first_attribute(name);
        return attr ? attr->value() : "";
    }

    void parseFields(const xml_node<>* parent, std::vector<Forms::Field>& fields, const std::string& formId)
    {
        for (const xml_node<>* node = parent->first_node(); node; node = node->next_sibling()) {
            if (node->type() != node_element) {
                continue;
            }
            auto type = fieldTypes.find(node->name());
            if (type == fieldTypes.end()) {
                LOG_WARNING("Forms", "Form " + formId + ": ignoring unknown element <" + std::string(node->name()) + ">");
                continue;
            }

            Forms::Field field;
            field.type = type->second;
            field.label = attribute(node, "label");
            field.bind = attribute(node, "bind");
            field.action = attribute(node, "action");
            field.hint = attribute(node, "hint");
            field.help = attribute(node, "help");
            field.status = attribute(node, "status");
            field.enabledWhen = attribute(node, "enabledWhen");
            field.visibleWhen = attribute(node, "visibleWhen");
            field.defaultValue = attribute(node, "default");
            field.options = attribute(node, "options");
            field.filterName = attribute(node, "filter");
            field.pattern = attribute(node, "pattern");
            field.empty = attribute(node, "empty");
            field.open = attribute(node, "open");
            field.param = attribute(node, "param");
            field.live = Forms::isTruthy(attribute(node, "live"));
            field.min = Utils::convertInt(attribute(node, "min"));
            field.max = Utils::convertInt(attribute(node, "max"));
            int limit = Utils::convertInt(attribute(node, "limit"));
            if (limit > 0) {
                field.limit = limit;
            }
            int rows = Utils::convertInt(attribute(node, "rows"));
            if (rows > 0) {
                field.rows = rows;
            }
            if (field.type == Forms::FieldType::Note) {
                field.label = attribute(node, "value");
            }
            if (field.type == Forms::FieldType::Results && field.param.empty()) {
                field.param = "id";
            }
            if ((field.type == Forms::FieldType::Results || field.type == Forms::FieldType::Link) && field.open.empty()) {
                LOG_WARNING("Forms", "Form " + formId + ": <" + std::string(node->name()) + "> without open=\"\" doesn't open anything");
            }

            if (field.type == Forms::FieldType::Section || field.type == Forms::FieldType::Actions) {
                parseFields(node, field.children, formId);
            }
            fields.push_back(std::move(field));
        }
    }

    void applyDefaults(const std::vector<Forms::Field>& fields, Forms::Model& model)
    {
        for (const Forms::Field& field : fields) {
            if (!field.bind.empty() && !field.defaultValue.empty()) {
                model.setDefault(field.bind, field.defaultValue);
            }
            applyDefaults(field.children, model);
        }
    }

    // Walks fields in document order; index counts down to the one wanted
    const Forms::Field* walk(const std::vector<Forms::Field>& fields, int& index, const Forms::Field* wanted)
    {
        for (const Forms::Field& field : fields) {
            if (wanted ? &field == wanted : index == 0) {
                return &field;
            }
            index += wanted ? 1 : -1;
            if (const Forms::Field* found = walk(field.children, index, wanted)) {
                return found;
            }
        }
        return nullptr;
    }

    // 5312 -> "5,312"
    std::string withSeparators(size_t n)
    {
        std::string digits = std::to_string(n);
        for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
            digits.insert(static_cast<size_t>(i), ",");
        }
        return digits;
    }
}

namespace Forms
{

bool isTruthy(const std::string& value)
{
    std::string lower = Utils::toLower(Utils::trimEnds(value));
    return lower == "yes" || lower == "true" || lower == "on" || lower == "1";
}

bool isEnabled(const Field& field, Model& model)
{
    if (field.enabledWhen.empty()) {
        return true;
    }
    if (field.enabledWhen[0] == '!') {
        return !isTruthy(model.get(field.enabledWhen.substr(1)));
    }
    return isTruthy(model.get(field.enabledWhen));
}

bool isVisible(const Field& field, Model& model)
{
    if (field.visibleWhen.empty()) {
        return true;
    }
    if (field.visibleWhen[0] == '!') {
        return !isTruthy(model.get(field.visibleWhen.substr(1)));
    }
    return isTruthy(model.get(field.visibleWhen));
}

std::string noteText(const Field& field, Model& model)
{
    return field.bind.empty() ? field.label : model.get(field.bind);
}

std::string title(const Form& form, Model& model)
{
    if (!form.titleBind.empty()) {
        std::string value = model.get(form.titleBind);
        if (!value.empty()) {
            return value;
        }
    }
    return form.title;
}

int fieldIndex(const Form& form, const Field* field)
{
    if (!field) {
        return -1;
    }
    int index = 0;
    return walk(form.fields, index, field) ? index : -1;
}

const Field* fieldAt(const Form& form, int index)
{
    if (index < 0) {
        return nullptr;
    }
    return walk(form.fields, index, nullptr);
}

std::string rowsNote(const Rows& rows)
{
    if (rows.total <= rows.items.size()) {
        return "";
    }
    return "Showing " + withSeparators(rows.items.size()) + " of " + withSeparators(rows.total)
         + ". Refine the search to see the rest.";
}

Params mergeParams(const Params& current, const Params& added)
{
    Params params = current;
    for (const auto& [name, value] : added) {
        params[name] = value;
    }
    return params;
}

std::unique_ptr<Form> parse(const std::string& xml, std::string& error)
{
    // rapidxml parses in place
    std::vector<char> buffer(xml.begin(), xml.end());
    buffer.push_back('\0');

    xml_document<> doc;
    try {
        doc.parse<0>(buffer.data());
    }
    catch (const parse_error& e) {
        error = std::string("XML error: ") + e.what();
        return nullptr;
    }

    const xml_node<>* root = doc.first_node("form");
    if (!root) {
        error = "Missing <form> element";
        return nullptr;
    }

    auto form = std::make_unique<Form>();
    form->id = attribute(root, "id");
    form->title = attribute(root, "title");
    form->titleBind = attribute(root, "bind");
    parseFields(root, form->fields, form->id);
    return form;
}

std::unique_ptr<Form> load(const std::string& id, const std::string& layoutName)
{
    std::string error;

    if (!layoutName.empty()) {
        std::string path = Utils::combinePath(Configuration::absolutePath, "layouts", layoutName, "forms", id + ".xml");
        if (std::filesystem::exists(path)) {
            std::ifstream file(path, std::ios::binary);
            std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (auto form = parse(xml, error)) {
                LOG_INFO("Forms", "Using " + path);
                return form;
            }
            LOG_ERROR("Forms", path + ": " + error + ". Using the built-in form.");
        }
    }

    const char* builtin = builtinForm(id);
    if (!builtin) {
        LOG_ERROR("Forms", "No form called " + id);
        return nullptr;
    }
    auto form = parse(builtin, error);
    if (!form) {
        LOG_ERROR("Forms", "Built-in form " + id + ": " + error);
    }
    return form;
}

void applyDefaults(const Form& form, Model& model)
{
    ::applyDefaults(form.fields, model);
}

}

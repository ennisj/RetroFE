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
#include <algorithm>
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
            field.confirm = attribute(node, "confirm");
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

Params rowParams(const Field& results, const Row& row)
{
    Params params = row.params;
    std::string name = results.param.empty() ? "id" : results.param;
    if (!params.count(name)) {
        params[name] = row.id;
    }
    return params;
}

Params openParams(const Params& current, const Model::Result& result)
{
    if (!result.keepParams) {
        return result.params;
    }
    Params params = current;
    for (const auto& [name, value] : result.params) {
        params[name] = value;
    }
    return params;
}

std::string rowKey(const Row& row)
{
    std::string key = row.id;
    for (const auto& [name, value] : row.params) {
        key += '\x1f' + name + '=' + value;
    }
    return key;
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

std::string ellipsize(const std::string& text, float width, const std::function<float(const std::string&)>& measure)
{
    if (text.empty() || measure(text) <= width) {
        return text;
    }
    const std::string dots = "...";
    if (measure(dots) > width) {
        return "";
    }
    // Where each character starts, so a cut never splits one
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); i++) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            starts.push_back(i);
        }
    }
    auto shortened = [&](size_t characters) {
        std::string start = text.substr(0, characters < starts.size() ? starts[characters] : text.size());
        while (!start.empty() && (start.back() == ' ' || start.back() == '\t')) {
            start.pop_back();
        }
        return start + dots;
    };
    // The most characters that fit, found by halving (fewer characters never measure wider)
    size_t low = 0;
    size_t high = starts.size() - 1;
    while (low < high) {
        size_t middle = (low + high + 1) / 2;
        if (measure(shortened(middle)) <= width) {
            low = middle;
        }
        else {
            high = middle - 1;
        }
    }
    return shortened(low);
}

std::vector<std::string> wrapText(const std::string& text, float width, const std::function<float(const std::string&)>& measure)
{
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }
    auto characterEnd = [&](const std::string& s, size_t start) {
        size_t end = start + 1;
        while (end < s.size() && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) {
            end++;
        }
        return end;
    };
    size_t paragraphStart = 0;
    while (true) {
        size_t paragraphEnd = text.find('\n', paragraphStart);
        if (paragraphEnd == std::string::npos) {
            paragraphEnd = text.size();
        }
        std::string paragraph = text.substr(paragraphStart, paragraphEnd - paragraphStart);
        if (!paragraph.empty() && paragraph.back() == '\r') {
            paragraph.pop_back();
        }
        std::string line;
        size_t position = 0;
        while (position < paragraph.size()) {
            // The next word
            while (position < paragraph.size() && (paragraph[position] == ' ' || paragraph[position] == '\t')) {
                position++;
            }
            size_t wordEnd = paragraph.find_first_of(" \t", position);
            if (wordEnd == std::string::npos) {
                wordEnd = paragraph.size();
            }
            if (wordEnd == position) {
                break;
            }
            std::string word = paragraph.substr(position, wordEnd - position);
            position = wordEnd;

            std::string candidate = line.empty() ? word : line + " " + word;
            if (measure(candidate) <= width) {
                line = candidate;
                continue;
            }
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            // A word wider than a line on its own is split where it reaches the edge
            while (measure(word) > width) {
                size_t cut = characterEnd(word, 0);   // at least one character a line
                while (cut < word.size()) {
                    size_t next = characterEnd(word, cut);
                    if (measure(word.substr(0, next)) > width) {
                        break;
                    }
                    cut = next;
                }
                if (cut >= word.size()) {
                    break;   // a single character wider than the line
                }
                lines.push_back(word.substr(0, cut));
                word = word.substr(cut);
            }
            line = word;
        }
        lines.push_back(line);
        if (paragraphEnd + 1 >= text.size()) {
            break;   // the end, or a newline at the end
        }
        paragraphStart = paragraphEnd + 1;
    }
    return lines;
}

RowSplit splitRow(float labelNeeds, float detailNeeds, float width, float gap, float maxDetailShare)
{
    RowSplit split;
    width = std::max(0.0f, width);
    if (detailNeeds <= 0.0f) {
        split.label = width;
        return split;
    }
    float room = std::max(0.0f, width - gap);
    if (labelNeeds + detailNeeds <= room) {
        split.detail = detailNeeds;
    }
    else {
        split.detail = std::min(detailNeeds, std::max(room * maxDetailShare, room - labelNeeds));
    }
    split.label = room - split.detail;
    return split;
}

int refocusIndex(const std::vector<FocusTarget>& targets, int previous)
{
    int count = static_cast<int>(targets.size());
    if (count == 0) {
        return -1;
    }
    previous = std::clamp(previous, 0, count - 1);
    auto search = [&](FocusTarget wanted) {
        for (int i = previous; i >= 0; i--) {
            if (targets[i] == wanted) return i;
        }
        for (int i = previous + 1; i < count; i++) {
            if (targets[i] == wanted) return i;
        }
        return -1;
    };
    int found = search(FocusTarget::Free);
    return found >= 0 ? found : search(FocusTarget::Guarded);
}

}

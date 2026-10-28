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
#include <cmath>
#include <cstdlib>
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
            field.preview = attribute(node, "preview");
            field.severity = Forms::parseSeverity(attribute(node, "severity"));
            field.pin = !node->first_attribute("pin") || Forms::isTruthy(attribute(node, "pin"));
            field.panel = Forms::isTruthy(attribute(node, "panel"));
            field.live = Forms::isTruthy(attribute(node, "live"));
            field.multiline = field.type == Forms::FieldType::Text && Forms::isTruthy(attribute(node, "multiline"));
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

Severity parseSeverity(const std::string& text)
{
    std::string lower = Utils::toLower(Utils::trimEnds(text));
    return lower == "ok" ? Severity::Ok
         : lower == "warning" ? Severity::Warning
         : lower == "error" ? Severity::Error
         : Severity::None;
}

Severity noteSeverity(const Field& field, Model& model)
{
    Severity said = field.bind.empty() ? Severity::None : model.severity(field.bind);
    return said != Severity::None ? said : field.severity;
}

std::vector<const Field*> pinnedActions(const Form& form)
{
    std::vector<const Field*> pinned;
    for (const Field& field : form.fields) {
        if (field.type == FieldType::Actions) {
            if (pinned.empty() && !field.pin) {
                break;
            }
            if (field.pin) {
                pinned.push_back(&field);
                continue;
            }
        }
        if (!pinned.empty()) {
            break;   // only the blocks straight after the first
        }
    }
    return pinned;
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
        std::string path = Utils::combinePath(Configuration::layoutsPath(), layoutName, "forms", id + ".xml");
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
    if (starts.empty()) {
        return dots;   // only stray continuation bytes, e.g. Latin-1 text read as UTF-8
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

float scrollToShow(const std::vector<RowShape>& rows, int focus, float scroll, float rowHeight, float visibleHeight)
{
    const int count = static_cast<int>(rows.size());
    const float total = count * rowHeight;
    if (focus >= 0 && focus < count && rowHeight > 0.0f) {
        // The section header above the focused row too
        int top = focus;
        while (top > 0 && rows[top - 1].header) {
            top--;
        }
        float topY = top * rowHeight;
        float bottomY = (focus + 1) * rowHeight;
        // And what's attached under it
        for (int below = focus + 1; below < count && rows[below].attached; below++) {
            bottomY += rowHeight;
        }
        // At the last row that takes focus, everything below it (notes the user can't move to)
        bool lastFocusable = std::none_of(rows.begin() + focus + 1, rows.end(), [](const RowShape& r) { return r.focusable; });
        if (lastFocusable) {
            bottomY = total;
        }
        if (topY < scroll) {
            scroll = topY;
        }
        if (bottomY > scroll + visibleHeight) {
            // Whole rows at the top when that still shows the focused one
            float wanted = bottomY - visibleHeight;
            float snapped = std::ceil(wanted / rowHeight - 0.01f) * rowHeight;
            scroll = std::min(topY, snapped <= topY ? snapped : wanted);
        }
        // At the first, everything above it
        bool firstFocusable = std::none_of(rows.begin(), rows.begin() + focus, [](const RowShape& r) { return r.focusable; });
        if (firstFocusable && bottomY <= visibleHeight) {
            scroll = 0.0f;
        }
    }
    // Scrolled to the end, whole rows at the top too, and a gap under the last one
    float maxScroll = std::max(0.0f, total - visibleHeight);
    if (maxScroll > 0.0f && rowHeight > 0.0f) {
        maxScroll = std::ceil(maxScroll / rowHeight - 0.01f) * rowHeight;
    }
    return std::clamp(scroll, 0.0f, maxScroll);
}

namespace
{
    int firstFocusable(const std::vector<bool>& focusable)
    {
        auto found = std::find(focusable.begin(), focusable.end(), true);
        return found == focusable.end() ? -1 : static_cast<int>(found - focusable.begin());
    }

    int lastFocusable(const std::vector<bool>& focusable)
    {
        for (int i = static_cast<int>(focusable.size()) - 1; i >= 0; i--) {
            if (focusable[i]) return i;
        }
        return -1;
    }

    bool canFocus(const std::vector<bool>& focusable, int row)
    {
        return row >= 0 && row < static_cast<int>(focusable.size()) && focusable[row];
    }
}

FocusMove moveFocus(const std::vector<bool>& focusable, int current, bool inBar, int barSize, int direction, int returnRow)
{
    FocusMove move;
    if (inBar && barSize > 0) {
        move.row = direction < 0 ? (canFocus(focusable, returnRow) ? returnRow : lastFocusable(focusable))
                                 : firstFocusable(focusable);
        move.bar = move.row < 0;   // nothing else to go to: stay
        return move;
    }
    if (current < 0) {
        move.row = direction < 0 ? lastFocusable(focusable) : firstFocusable(focusable);
        move.bar = move.row < 0 && barSize > 0;
        return move;
    }
    for (int i = current + direction; i >= 0 && i < static_cast<int>(focusable.size()); i += direction) {
        if (focusable[i]) {
            move.row = i;
            return move;
        }
    }
    // Past the end: the bar, or round to the other end
    if (barSize > 0) {
        move.bar = true;
        return move;
    }
    move.row = direction < 0 ? lastFocusable(focusable) : firstFocusable(focusable);
    return move;
}

FocusMove jumpFocus(const std::vector<bool>& focusable, int current, bool inBar, int barSize, bool back, int pageRows, int returnRow)
{
    FocusMove move;
    if (inBar && barSize > 0) {
        if (!back) {
            move.bar = true;   // already there
            return move;
        }
        move.row = canFocus(focusable, returnRow) ? returnRow : lastFocusable(focusable);
        move.bar = move.row < 0;
        return move;
    }
    if (!back && barSize > 0) {
        move.bar = true;
        return move;
    }
    // A page up or down, at the nearest row that takes focus
    int count = static_cast<int>(focusable.size());
    if (current < 0) {
        move.row = back ? firstFocusable(focusable) : lastFocusable(focusable);
        return move;
    }
    int direction = back ? -1 : 1;
    int target = std::clamp(current + direction * std::max(1, pageRows), 0, std::max(0, count - 1));
    for (int i = target; i != current; i -= direction) {
        if (canFocus(focusable, i)) {
            move.row = i;
            return move;
        }
    }
    for (int i = target + direction; i >= 0 && i < count; i += direction) {
        if (canFocus(focusable, i)) {
            move.row = i;
            return move;
        }
    }
    move.row = current;
    return move;
}

int barTarget(const std::vector<bool>& ready)
{
    if (ready.empty()) {
        return -1;
    }
    int first = firstFocusable(ready);
    return first >= 0 ? first : 0;
}

std::vector<BarSlot> layoutBar(const std::vector<float>& widths, float width, float gap)
{
    std::vector<BarSlot> slots;
    slots.reserve(widths.size());
    float x = 0.0f;
    int line = 0;
    for (float wanted : widths) {
        BarSlot slot;
        slot.width = std::min(std::max(0.0f, wanted), std::max(0.0f, width));
        if (x > 0.0f && x + slot.width > width) {
            line++;
            x = 0.0f;
        }
        slot.x = x;
        slot.line = line;
        x += slot.width + gap;
        slots.push_back(slot);
    }
    return slots;
}

bool matchesFormId(const std::vector<std::string>& patterns, const std::string& id)
{
    if (patterns.empty()) {
        return true;
    }
    for (const std::string& pattern : patterns) {
        if (!pattern.empty() && pattern.back() == '*') {
            if (id.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0) {
                return true;
            }
        }
        else if (pattern == id) {
            return true;
        }
    }
    return false;
}

bool parseColor(const std::string& text, unsigned char rgb[3])
{
    std::string hex = !text.empty() && text[0] == '#' ? text.substr(1) : text;
    if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        rgb[i] = static_cast<unsigned char>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }
    return true;
}

bool parseNumber(const std::string& text, float& value)
{
    // Leading and trailing spaces are fine; anything else after the number isn't
    size_t start = text.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return false;
    }
    const char* begin = text.c_str() + start;
    char* end = nullptr;
    float parsed = std::strtof(begin, &end);
    if (end == begin || std::string(end).find_first_not_of(" \t") != std::string::npos || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
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

// --- Editing text ------------------------------------------------------------

size_t nextCharacter(const std::string& text, size_t position)
{
    if (position >= text.size()) {
        return text.size();
    }
    position++;
    while (position < text.size() && (static_cast<unsigned char>(text[position]) & 0xC0) == 0x80) {
        position++;
    }
    return position;
}

size_t previousCharacter(const std::string& text, size_t position)
{
    position = std::min(position, text.size());
    if (position == 0) {
        return 0;
    }
    position--;
    while (position > 0 && (static_cast<unsigned char>(text[position]) & 0xC0) == 0x80) {
        position--;
    }
    return position;
}

void TextEdit::insert(const std::string& typed)
{
    cursor = std::min(cursor, text.size());
    text.insert(cursor, typed);
    cursor += typed.size();
}

void TextEdit::backspace()
{
    cursor = std::min(cursor, text.size());
    size_t start = previousCharacter(text, cursor);
    text.erase(start, cursor - start);
    cursor = start;
}

void TextEdit::erase()
{
    cursor = std::min(cursor, text.size());
    text.erase(cursor, nextCharacter(text, cursor) - cursor);
}

void TextEdit::left()
{
    cursor = previousCharacter(text, cursor);
}

void TextEdit::right()
{
    cursor = nextCharacter(text, cursor);
}

std::vector<TextLine> layoutLines(const std::string& text, float width, const std::function<float(const std::string&)>& measure)
{
    std::vector<TextLine> lines;
    auto fits = [&](size_t start, size_t end) { return measure(text.substr(start, end - start)) <= width; };
    size_t paragraph = 0;
    while (true) {
        size_t newline = text.find('\n', paragraph);
        size_t paragraphEnd = newline == std::string::npos ? text.size() : newline;
        size_t start = paragraph;
        while (true) {
            if (start >= paragraphEnd || fits(start, paragraphEnd)) {
                lines.push_back({ start, paragraphEnd });
                break;
            }
            // The furthest run of spaces with what's before it fitting: the line ends after it
            size_t breakAt = std::string::npos;
            size_t from = start;
            while (true) {
                size_t space = text.find(' ', from);
                if (space == std::string::npos || space >= paragraphEnd) {
                    break;
                }
                if (space > start && !fits(start, space)) {
                    break;
                }
                size_t after = space;
                while (after < paragraphEnd && text[after] == ' ') {
                    after++;
                }
                if (space > start) {
                    breakAt = after;
                }
                from = after;
            }
            if (breakAt != std::string::npos && breakAt >= paragraphEnd) {
                lines.push_back({ start, paragraphEnd });   // only spaces left after it
                break;
            }
            if (breakAt != std::string::npos) {
                lines.push_back({ start, breakAt });
                start = breakAt;
                continue;
            }
            // A word too wide for a line on its own: cut where it reaches the edge, at least one character
            size_t cut = nextCharacter(text, start);
            while (cut < paragraphEnd) {
                size_t next = nextCharacter(text, cut);
                if (!fits(start, next)) {
                    break;
                }
                cut = next;
            }
            lines.push_back({ start, cut });
            start = cut;
        }
        if (newline == std::string::npos) {
            break;
        }
        paragraph = newline + 1;
    }
    return lines;
}

size_t lineAt(const std::vector<TextLine>& lines, size_t cursor)
{
    for (size_t i = lines.size(); i > 0; i--) {
        if (lines[i - 1].start <= cursor) {
            return i - 1;
        }
    }
    return 0;
}

namespace
{
    // The last cursor position that's drawn on line i (not past a wrap)
    size_t lastOnLine(const std::string& text, const std::vector<TextLine>& lines, size_t i)
    {
        const TextLine& line = lines[i];
        bool wraps = i + 1 < lines.size() && lines[i + 1].start == line.end;
        return wraps && line.end > line.start ? previousCharacter(text, line.end) : line.end;
    }
}

size_t moveCursorVertically(const std::string& text, const std::vector<TextLine>& lines, size_t cursor, int direction,
                            const std::function<float(const std::string&)>& measure)
{
    if (lines.empty()) {
        return cursor;
    }
    size_t current = lineAt(lines, cursor);
    if (direction < 0 && current == 0) {
        return 0;
    }
    if (direction > 0 && current + 1 >= lines.size()) {
        return text.size();
    }
    float x = measure(text.substr(lines[current].start, cursor - lines[current].start));
    size_t target = direction < 0 ? current - 1 : current + 1;
    size_t start = lines[target].start;
    size_t last = lastOnLine(text, lines, target);
    size_t best = start;
    float bestDistance = x;
    for (size_t position = nextCharacter(text, start); position <= last && position > start; position = nextCharacter(text, position)) {
        float distance = std::fabs(measure(text.substr(start, position - start)) - x);
        if (distance < bestDistance) {
            best = position;
            bestDistance = distance;
        }
        else if (distance > bestDistance) {
            break;   // moving further away from here on
        }
        if (position >= last) {
            break;
        }
    }
    return best;
}

size_t lineHome(const std::vector<TextLine>& lines, size_t cursor)
{
    return lines.empty() ? 0 : lines[lineAt(lines, cursor)].start;
}

size_t lineEnd(const std::string& text, const std::vector<TextLine>& lines, size_t cursor)
{
    return lines.empty() ? text.size() : lastOnLine(text, lines, lineAt(lines, cursor));
}

}

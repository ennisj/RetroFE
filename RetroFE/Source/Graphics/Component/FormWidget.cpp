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
#include "FormWidget.h"
#include "Image.h"
#include "../../Utility/Log.h"
#include "../../Utility/Utils.h"
#include <algorithm>

using Forms::Field;
using Forms::FieldType;

namespace
{
    bool isInteractive(FieldType type)
    {
        switch (type) {
        case FieldType::Text:
        case FieldType::Folder:
        case FieldType::File:
        case FieldType::Toggle:
        case FieldType::Number:
        case FieldType::Choice:
        case FieldType::Button:
            return true;
        default:
            return false;
        }
    }

    // Drop the last UTF-8 character
    void popCharacter(std::string& s)
    {
        while (!s.empty()) {
            unsigned char c = static_cast<unsigned char>(s.back());
            s.pop_back();
            if ((c & 0xC0) != 0x80) {
                break;
            }
        }
    }
}

FormWidget::FormWidget(Page& p, FontManager* font, FontManager* focusFont, int monitor)
    : Component(p)
    , painter_(p, font, focusFont)
{
    baseViewInfo.Monitor = monitor;
    baseViewInfo.Layout = page.getCurrentLayout();
}

FormWidget::~FormWidget()
{
    if (editing_) {
        finishEditing(false);
    }
    FormWidget::freeGraphicsMemory();
}

void FormWidget::bind(const Forms::Form* form, Forms::Model* model)
{
    form_ = form;
    model_ = model;
    focus_ = -1;
    focusedField_ = nullptr;
    scroll_ = 0.0f;
    status_.clear();
}

void FormWidget::allocateGraphicsMemory()
{
    Component::allocateGraphicsMemory();
    painter_.allocateGraphicsMemory(baseViewInfo.Monitor);
    if (!style_.focusImage.empty() && !focusImage_) {
        focusImage_ = std::make_unique<Image>(style_.focusImage, "", page, baseViewInfo.Monitor, false);
        focusImage_->allocateGraphicsMemory();
    }
}

void FormWidget::freeGraphicsMemory()
{
    Component::freeGraphicsMemory();
    painter_.freeGraphicsMemory();
    if (focusImage_) {
        focusImage_->freeGraphicsMemory();
        focusImage_.reset();
    }
}

void FormWidget::setStatus(const std::string& message, bool isError)
{
    status_ = message;
    statusIsError_ = isError;
}

std::optional<FormWidget::Browse> FormWidget::takeBrowse()
{
    std::optional<Browse> browse = browse_;
    browse_.reset();
    return browse;
}

std::optional<Forms::Model::Result> FormWidget::takeResult()
{
    std::optional<Forms::Model::Result> result = result_;
    result_.reset();
    return result;
}

// --- Layout ------------------------------------------------------------------

float FormWidget::fontSize() const
{
    return baseViewInfo.FontSize > 0 ? baseViewInfo.FontSize : 28.0f;
}

float FormWidget::rowHeight() const
{
    return style_.rowHeight > 0 ? style_.rowHeight : fontSize() * 1.6f;
}

float FormWidget::contentWidth() const
{
    float width = baseViewInfo.Width > 0 ? baseViewInfo.Width
        : static_cast<float>(page.getLayoutWidthByMonitor(baseViewInfo.Monitor)) - baseViewInfo.X;
    return width - fontSize() * 1.2f;   // padding on both sides
}

float FormWidget::labelWidth() const
{
    return style_.labelWidth > 0 ? style_.labelWidth : contentWidth() * 0.4f;
}

void FormWidget::addWrapped(RowKind kind, const std::string& text, bool indent, bool isError)
{
    float width = indent ? contentWidth() - labelWidth() : contentWidth();
    for (const std::string& line : painter_.wrap(text, width, fontSize())) {
        Row row;
        row.kind = kind;
        row.text = line;
        row.indent = indent;
        row.isError = isError;
        rows_.push_back(row);
    }
}

void FormWidget::addFields(const std::vector<Field>& fields)
{
    for (const Field& field : fields) {
        switch (field.type) {
        case FieldType::Section: {
            Row header;
            header.kind = RowKind::Header;
            header.text = field.label;
            rows_.push_back(header);
            addFields(field.children);
            break;
        }
        case FieldType::Actions:
            addFields(field.children);
            break;

        case FieldType::Note:
            addWrapped(RowKind::Note, field.label, false);
            break;

        case FieldType::List: {
            std::vector<std::string> values = model_->list(field.bind);
            if (values.empty()) {
                if (!field.empty.empty()) {
                    addWrapped(RowKind::Note, field.empty, false);
                }
                break;
            }
            Row row;
            row.kind = RowKind::Field;
            row.field = &field;
            rows_.push_back(row);
            break;
        }

        default: {
            Row row;
            row.kind = RowKind::Field;
            row.field = &field;
            row.enabled = Forms::isEnabled(field, *model_);
            row.focusable = isInteractive(field.type) && row.enabled;
            rows_.push_back(row);

            if (field.type == FieldType::Button) {
                std::string problem = model_->actionProblem(field.action);
                if (!problem.empty()) {
                    addWrapped(RowKind::Note, problem, true);
                }
                break;
            }
            if (!field.status.empty()) {
                std::string status = model_->get(field.status);
                if (!status.empty()) {
                    addWrapped(RowKind::Note, status, true);
                }
            }
            if (!field.help.empty()) {
                addWrapped(RowKind::Note, field.help, true);
            }
            if (!field.bind.empty()) {
                std::string source = model_->get("info:source:" + field.bind);
                if (!source.empty()) {
                    addWrapped(RowKind::Note, "Set in " + source, true);
                }
            }
            break;
        }
        }
    }
}

void FormWidget::buildRows()
{
    rows_.clear();
    if (!form_ || !model_) {
        return;
    }
    if (style_.showTitle && !form_->title.empty()) {
        Row title;
        title.kind = RowKind::Title;
        title.text = form_->title;
        rows_.push_back(title);
    }
    addFields(form_->fields);
    if (!status_.empty()) {
        addWrapped(RowKind::Status, status_, false, statusIsError_);
    }

    // Keep the same field focused while rows come and go around it
    focus_ = -1;
    for (size_t i = 0; i < rows_.size(); i++) {
        if (rows_[i].focusable && rows_[i].field == focusedField_) {
            focus_ = static_cast<int>(i);
        }
    }
    if (focus_ < 0) {
        for (size_t i = 0; i < rows_.size(); i++) {
            if (rows_[i].focusable) {
                focus_ = static_cast<int>(i);
                focusedField_ = rows_[i].field;
                break;
            }
        }
    }
}

void FormWidget::keepFocusVisible(float visibleHeight)
{
    float total = rows_.size() * rowHeight();
    if (focus_ >= 0) {
        // Show the section header above the focused field too
        int top = focus_;
        while (top > 0 && rows_[top - 1].kind == RowKind::Header) {
            top--;
        }
        float topY = top * rowHeight();
        float bottomY = (focus_ + 1) * rowHeight();
        if (topY < scroll_) {
            scroll_ = topY;
        }
        if (bottomY > scroll_ + visibleHeight) {
            scroll_ = bottomY - visibleHeight;
        }
        // At the first field, show everything above it
        bool firstFocusable = std::none_of(rows_.begin(), rows_.begin() + focus_, [](const Row& r) { return r.focusable; });
        if (firstFocusable && bottomY <= visibleHeight) {
            scroll_ = 0.0f;
        }
    }
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, total - visibleHeight));
}

// --- Input ---------------------------------------------------------------------

void FormWidget::moveFocus(int direction)
{
    if (rows_.empty()) {
        return;
    }
    int count = static_cast<int>(rows_.size());
    int start = focus_ < 0 ? (direction > 0 ? -1 : count) : focus_;
    for (int step = 1; step <= count; step++) {
        int index = ((start + direction * step) % count + count) % count;
        if (rows_[index].focusable) {
            focus_ = index;
            focusedField_ = rows_[index].field;
            return;
        }
    }
}

bool FormWidget::navigate(Nav nav)
{
    if (!form_ || !model_) {
        return nav != Nav::Back;
    }
    // While editing, the controls drive the on-screen keyboard instead
    if (editing_) {
        return true;
    }
    if (justFinishedEditing_ && (nav == Nav::Select || nav == Nav::Back)) {
        justFinishedEditing_ = false;
        return true;
    }
    if (rows_.empty()) {
        buildRows();
    }

    const Field* field = focus_ >= 0 ? rows_[focus_].field : nullptr;
    static const char* const names[] = { "Up", "Down", "Left", "Right", "Select", "Back" };
    LOG_DEBUG("Forms", std::string(names[static_cast<int>(nav)]) + " on " + (field ? field->label : "nothing"));
    switch (nav) {
    case Nav::Up:
        moveFocus(-1);
        break;
    case Nav::Down:
        moveFocus(1);
        break;
    case Nav::Left:
    case Nav::Right:
        if (field) {
            if ((field->type == FieldType::Folder || field->type == FieldType::File) && nav == Nav::Right) {
                startEditing(*field);
            }
            else {
                change(*field, nav == Nav::Right ? 1 : -1);
            }
        }
        break;
    case Nav::Select:
        if (field) {
            activate(*field);
        }
        break;
    case Nav::Back:
        return false;
    }
    return true;
}

void FormWidget::activate(const Field& field)
{
    switch (field.type) {
    case FieldType::Text:
    case FieldType::Number:
        startEditing(field);
        break;
    case FieldType::Folder:
    case FieldType::File:
        if (canBrowse_) {
            Browse browse;
            browse.bind = field.bind;
            browse.folder = field.type == FieldType::Folder;
            browse.title = field.label;
            browse.filterName = field.filterName;
            browse.pattern = field.pattern;
            browse_ = browse;
        }
        else {
            startEditing(field);
        }
        break;
    case FieldType::Toggle:
    case FieldType::Choice:
        change(field, 1);
        break;
    case FieldType::Button:
        if (model_->actionProblem(field.action).empty()) {
            result_ = model_->runAction(field.action);
        }
        break;
    default:
        break;
    }
}

void FormWidget::change(const Field& field, int direction)
{
    switch (field.type) {
    case FieldType::Toggle:
        model_->set(field.bind, Forms::isTruthy(model_->get(field.bind)) ? "no" : "yes");
        break;
    case FieldType::Number: {
        int value = Utils::convertInt(model_->get(field.bind)) + direction;
        if (field.max > field.min) {
            value = std::clamp(value, field.min, field.max);
        }
        model_->set(field.bind, std::to_string(value));
        break;
    }
    case FieldType::Choice: {
        auto options = model_->options(field.options);
        if (options.empty()) {
            break;
        }
        std::string value = model_->get(field.bind);
        int count = static_cast<int>(options.size());
        int index = 0;
        for (int i = 0; i < count; i++) {
            if (options[i].first == value) {
                index = i;
            }
        }
        index = ((index + direction) % count + count) % count;
        model_->set(field.bind, options[index].first);
        break;
    }
    default:
        break;
    }
}

void FormWidget::startEditing(const Field& field)
{
    editing_ = true;
    editBuffer_ = model_->get(field.bind);
    LOG_DEBUG("Forms", "Editing " + field.label);
    textInputWasActive_ = SDL_IsTextInputActive() == SDL_TRUE;
    SDL_StartTextInput();
}

void FormWidget::finishEditing(bool keep)
{
    if (!editing_) {
        return;
    }
    editing_ = false;
    justFinishedEditing_ = true;
    if (!textInputWasActive_) {
        SDL_StopTextInput();
    }
    if (keep && focusedField_ && model_) {
        if (focusedField_->type == FieldType::Number) {
            int value = Utils::convertInt(editBuffer_);
            if (focusedField_->max > focusedField_->min) {
                value = std::clamp(value, focusedField_->min, focusedField_->max);
            }
            editBuffer_ = std::to_string(value);
        }
        model_->set(focusedField_->bind, editBuffer_);
        LOG_DEBUG("Forms", focusedField_->label + " set to \"" + editBuffer_ + "\"");
    }
}

bool FormWidget::handleTextEvent(const SDL_Event& e)
{
    if (!editing_) {
        return false;
    }
    bool number = focusedField_ && focusedField_->type == FieldType::Number;

    if (e.type == SDL_TEXTINPUT) {
        std::string text = e.text.text;
        if (number) {
            text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return !(c == '-' || (c >= '0' && c <= '9')); }), text.end());
        }
        editBuffer_ += text;
        return true;
    }
    if (e.type == SDL_KEYDOWN) {
        SDL_Keycode key = e.key.keysym.sym;
        if (key == SDLK_BACKSPACE) {
            popCharacter(editBuffer_);
        }
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            finishEditing(true);
        }
        else if (key == SDLK_ESCAPE) {
            finishEditing(false);
        }
        else if (key == SDLK_v && (e.key.keysym.mod & KMOD_CTRL) && SDL_HasClipboardText()) {
            char* clipboard = SDL_GetClipboardText();
            std::string text = clipboard ? clipboard : "";
            SDL_free(clipboard);
            text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == '\r' || c == '\n'; }), text.end());
            editBuffer_ += text;
        }
        else {
            // Arrows and the like belong to the on-screen keyboard
            return false;
        }
        return true;
    }
    return e.type == SDL_TEXTEDITING;
}

// --- Drawing -----------------------------------------------------------------

std::string FormWidget::valueText(const Field& field, bool focused)
{
    if (editing_ && focused) {
        // Keep the end in view while typing, e.g. the folder at the end of a long path
        return painter_.tail(editBuffer_ + "_", contentWidth() - labelWidth(), fontSize());
    }
    switch (field.type) {
    case FieldType::Toggle:
        return Forms::isTruthy(model_->get(field.bind)) ? style_.onText : style_.offText;
    case FieldType::Number: {
        std::string value = model_->get(field.bind);
        return focused ? "< " + value + " >" : value;
    }
    case FieldType::Choice: {
        std::string value = model_->get(field.bind);
        std::string label = value;
        for (const auto& [optionValue, optionLabel] : model_->options(field.options)) {
            if (optionValue == value) {
                label = optionLabel;
            }
        }
        return focused ? "< " + label + " >" : label;
    }
    case FieldType::List: {
        std::vector<std::string> values = model_->list(field.bind);
        std::string text = std::to_string(values.size()) + ": ";
        for (size_t i = 0; i < values.size() && i < 3; i++) {
            text += (i ? ", " : "") + values[i];
        }
        return values.size() > 3 ? text + ", ..." : text;
    }
    case FieldType::Button:
        return "";
    default:
        return model_->get(field.bind);
    }
}

std::string FormWidget::hintText() const
{
    if (editing_) {
        return "Type, then Enter to keep or Esc to cancel. Ctrl+V pastes.";
    }
    const Field* field = focus_ >= 0 && focus_ < static_cast<int>(rows_.size()) ? rows_[focus_].field : nullptr;
    std::string hint;
    if (field) {
        switch (field->type) {
        case FieldType::Text:   hint = "Select: edit"; break;
        case FieldType::Number: hint = "Left/Right: change   Select: type"; break;
        case FieldType::Toggle: hint = "Select: change"; break;
        case FieldType::Choice: hint = "Left/Right: change"; break;
        case FieldType::Button: hint = "Select: " + field->label; break;
        case FieldType::Folder:
        case FieldType::File:   hint = canBrowse_ ? "Select: browse   Right: type" : "Select: edit"; break;
        default: break;
        }
    }
    return (hint.empty() ? "" : hint + "   ") + "Back: close";
}

void FormWidget::draw()
{
    Component::draw();
    justFinishedEditing_ = false;
    if (!form_ || !model_ || !painter_.hasFont() || baseViewInfo.Alpha <= 0.0f) {
        return;
    }

    buildRows();
    painter_.begin(baseViewInfo);

    const float size = fontSize();
    const float padding = size * 0.6f;
    const float left = baseViewInfo.XRelativeToOrigin() + padding;
    const float top = baseViewInfo.YRelativeToOrigin() + padding;
    const float width = contentWidth();
    const float height = (baseViewInfo.Height > 0 ? baseViewInfo.Height
        : static_cast<float>(page.getLayoutHeightByMonitor(baseViewInfo.Monitor)) - baseViewInfo.Y) - padding * 2.0f;
    const float hintsHeight = style_.showHints ? rowHeight() : 0.0f;
    const float visible = height - hintsHeight;
    const float row = rowHeight();
    const float textOffset = (row - size) / 2.0f;
    const float valueX = left + labelWidth();
    const float valueWidth = width - labelWidth();

    keepFocusVisible(visible);

    for (size_t i = 0; i < rows_.size(); i++) {
        float y = top + i * row - scroll_;
        if (y < top - 0.5f || y + row > top + visible + 0.5f) {
            continue;
        }
        const Row& r = rows_[i];
        bool focused = static_cast<int>(i) == focus_;
        float textY = y + textOffset;

        if (focused) {
            if (focusImage_) {
                ViewInfo& view = focusImage_->baseViewInfo;
                view.X = left - padding / 2.0f;
                view.Y = y;
                view.Width = width + padding;
                view.Height = row;
                view.Alpha = baseViewInfo.Alpha;
                view.Monitor = baseViewInfo.Monitor;
                view.Layout = baseViewInfo.Layout;
                focusImage_->draw();
            }
            else {
                painter_.rect(left - padding / 2.0f, y, width + padding, row, style_.focusBackground, style_.focusBackgroundAlpha);
            }
        }

        switch (r.kind) {
        case RowKind::Title:
            painter_.text(r.text, left, y + (row - size * 1.3f) / 2.0f, width, size * 1.3f, false, 1.0f);
            break;
        case RowKind::Header:
            painter_.text(r.text, left, textY, width, size * 0.85f, false, 0.7f);
            break;
        case RowKind::Note:
            painter_.text(r.text, r.indent ? valueX : left, textY, r.indent ? valueWidth : width, size * 0.85f, false, style_.noteAlpha);
            break;
        case RowKind::Status:
            painter_.text((r.isError ? "! " : "") + r.text, left, textY, width, size, false, 1.0f);
            break;
        case RowKind::Field: {
            const Field& field = *r.field;
            float alpha = r.enabled ? 1.0f : style_.disabledAlpha;
            painter_.text(field.label, left, textY, labelWidth(), size, focused, alpha);
            std::string value = valueText(field, focused);
            if (value.empty() && !field.hint.empty() && !(editing_ && focused)) {
                painter_.text(field.hint, valueX, textY, valueWidth, size, false, style_.noteAlpha * alpha);
            }
            else {
                painter_.text(value, valueX, textY, valueWidth, size, focused, alpha);
            }
            break;
        }
        }
    }

    if (style_.showHints) {
        painter_.text(hintText(), left, top + height - hintsHeight + textOffset, width, size * 0.85f, false, style_.noteAlpha);
    }
}

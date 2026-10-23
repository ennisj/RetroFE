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
#include "../../SDL.h"
#include "../../Utility/Log.h"
#include "../../Utility/Utils.h"
#include <algorithm>
#include <cmath>

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
        case FieldType::Link:
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

    // The screen is rebuilt at least this often, for values that change by
    // themselves, such as Catalog progress
    constexpr uint32_t refreshMs = 250;

    // New thumbnails loaded per frame, so paging through a list doesn't stall
    constexpr int thumbnailsPerFrame = 2;

    // Wrapped texts remembered; more and they're all forgotten
    constexpr size_t maxWrapped = 1024;
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
    focusedItem_.clear();
    scroll_ = 0.0f;
    status_.clear();
    confirm_.cancel();
    rows_.clear();
    stale_ = true;
}

Forms::Focus FormWidget::focus() const
{
    Forms::Focus focus;
    if (form_) {
        focus.field = Forms::fieldIndex(*form_, focusedField_);
        focus.item = focusedItem_;
        focus.scroll = scroll_;
    }
    return focus;
}

void FormWidget::restoreFocus(const Forms::Focus& focus)
{
    if (!form_) {
        return;
    }
    focusedField_ = Forms::fieldAt(*form_, focus.field);
    focusedItem_ = focus.item;
    scroll_ = focus.scroll;
    rows_.clear();
    stale_ = true;
}

void FormWidget::allocateGraphicsMemory()
{
    Component::allocateGraphicsMemory();
    painter_.allocateGraphicsMemory(baseViewInfo.Monitor);
    pictures_.setRenderer(SDL::getRenderer(baseViewInfo.Monitor));
    if (!style_.focusImage.empty() && !focusImage_) {
        focusImage_ = std::make_unique<Image>(style_.focusImage, "", page, baseViewInfo.Monitor, false);
        focusImage_->allocateGraphicsMemory();
    }
}

void FormWidget::freeGraphicsMemory()
{
    Component::freeGraphicsMemory();
    painter_.freeGraphicsMemory();
    pictures_.clear();
    if (focusImage_) {
        focusImage_->freeGraphicsMemory();
        focusImage_.reset();
    }
}

void FormWidget::setStatus(const std::string& message, bool isError)
{
    status_ = message;
    statusIsError_ = isError;
    stale_ = true;
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

float FormWidget::pixelScale() const
{
    int width = 0;
    int height = 0;
    SDL_Renderer* renderer = SDL::getRenderer(baseViewInfo.Monitor);
    int layoutWidth = page.getLayoutWidthByMonitor(baseViewInfo.Monitor);
    int layoutHeight = page.getLayoutHeightByMonitor(baseViewInfo.Monitor);
    if (!renderer || SDL_GetRendererOutputSize(renderer, &width, &height) != 0 || layoutWidth <= 0 || layoutHeight <= 0) {
        return 1.0f;
    }
    return std::max(1.0f, std::max(static_cast<float>(width) / layoutWidth, static_cast<float>(height) / layoutHeight));
}

const std::vector<std::string>& FormWidget::wrap(const std::string& text, float width)
{
    std::string key = std::to_string(std::lround(width)) + '\x1f' + text;
    auto found = wrapped_.find(key);
    if (found != wrapped_.end()) {
        return found->second;
    }
    if (wrapped_.size() >= maxWrapped) {
        wrapped_.clear();
    }
    return wrapped_.emplace(key, painter_.wrap(text, width, fontSize())).first->second;
}

void FormWidget::addWrapped(RowKind kind, const std::string& text, bool indent, bool isError)
{
    float width = indent ? contentWidth() - labelWidth() : contentWidth();
    for (const std::string& line : wrap(text, width)) {
        Row row;
        row.kind = kind;
        row.text = line;
        row.indent = indent;
        row.isError = isError;
        rows_.push_back(row);
    }
}

void FormWidget::addResults(const Field& field)
{
    if (!field.label.empty()) {
        Row header;
        header.kind = RowKind::Header;
        header.text = field.label;
        rows_.push_back(header);
    }
    Forms::Rows results = model_->rows(field.bind, static_cast<size_t>(field.limit));
    if (results.items.empty()) {
        if (!field.empty.empty()) {
            addWrapped(RowKind::Note, field.empty, false);
        }
        return;
    }
    std::string note = Forms::rowsNote(results);
    if (!note.empty()) {
        addWrapped(RowKind::Note, note, false);
    }
    bool thumbnails = std::any_of(results.items.begin(), results.items.end(),
                                  [](const Forms::Row& item) { return !item.image.empty(); });
    rows_.reserve(rows_.size() + results.items.size());
    for (Forms::Row& item : results.items) {
        Row row;
        row.kind = RowKind::Item;
        row.field = &field;
        row.item = Forms::rowKey(item);
        row.params = Forms::rowParams(field, item);
        row.text = std::move(item.label);
        row.detail = std::move(item.detail);
        row.image = std::move(item.image);
        row.thumbnail = thumbnails;
        row.focusable = true;
        rows_.push_back(std::move(row));
    }
}

void FormWidget::addValue(const Field& field)
{
    std::string value = model_->get(field.bind);
    if (value.empty()) {
        return;
    }
    // The first line goes next to the label, the rest under it
    bool first = true;
    for (const std::string& line : wrap(value, contentWidth() - labelWidth())) {
        Row row;
        row.kind = RowKind::Value;
        row.field = first ? &field : nullptr;
        row.text = line;
        row.indent = true;
        rows_.push_back(row);
        first = false;
    }
    if (!field.help.empty()) {
        addWrapped(RowKind::Note, field.help, true);
    }
}

void FormWidget::addImage(const Field& field)
{
    std::string path = model_->get(field.bind);
    if (path.empty()) {
        return;
    }
    // Loaded at the size it's drawn at; hidden when it can't be loaded (it's tried again later)
    Row row;
    row.kind = RowKind::Image;
    row.field = &field;
    row.image = path;
    row.span = std::max(1, field.rows);
    float scale = pixelScale();
    float width = field.label.empty() ? contentWidth() : contentWidth() - labelWidth();
    TextureCache::State state = TextureCache::State::Missing;
    pictures_.get(path, static_cast<int>(width * scale), static_cast<int>(row.span * rowHeight() * scale), true, &state);
    if (state != TextureCache::State::Ready) {
        return;
    }
    rows_.push_back(row);
    for (int i = 1; i < row.span; i++) {
        Row spacer;
        spacer.kind = RowKind::Spacer;
        rows_.push_back(spacer);
    }
}

void FormWidget::addFields(const std::vector<Field>& fields)
{
    for (const Field& field : fields) {
        if (!Forms::isVisible(field, *model_)) {
            continue;
        }
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

        case FieldType::Note: {
            std::string text = Forms::noteText(field, *model_);
            if (!text.empty()) {
                addWrapped(RowKind::Note, text, false);
            }
            break;
        }

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
            row.value = std::to_string(values.size()) + ": ";
            for (size_t i = 0; i < values.size() && i < 3; i++) {
                row.value += (i ? ", " : "") + values[i];
            }
            if (values.size() > 3) {
                row.value += ", ...";
            }
            rows_.push_back(row);
            break;
        }

        case FieldType::Results:
            addResults(field);
            break;

        case FieldType::Value:
            addValue(field);
            break;

        case FieldType::Image:
            addImage(field);
            break;

        default: {
            Row row;
            row.kind = RowKind::Field;
            row.field = &field;
            row.value = valueText(field);
            row.enabled = Forms::isEnabled(field, *model_);
            row.focusable = isInteractive(field.type) && row.enabled;
            rows_.push_back(row);

            if (field.type == FieldType::Button) {
                if (confirm_.armed() == &field) {
                    addWrapped(RowKind::Note, field.confirm + " Select again to confirm.", true, true);
                    break;
                }
                std::string problem = model_->actionProblem(field.action);
                if (!problem.empty()) {
                    addWrapped(RowKind::Note, problem, true);
                }
                break;
            }
            // A link shows its status on its own row
            if (!field.status.empty() && field.type != FieldType::Link) {
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
    stale_ = false;
    builtAt_ = SDL_GetTicks();
    rows_.clear();
    statusLines_.clear();
    if (!form_ || !model_) {
        return;
    }
    generation_ = model_->generation();
    std::string titleText = Forms::title(*form_, *model_);
    if (style_.showTitle && !titleText.empty()) {
        Row title;
        title.kind = RowKind::Title;
        title.text = titleText;
        rows_.push_back(title);
    }
    addFields(form_->fields);
    if (!status_.empty()) {
        statusLines_ = wrap((statusIsError_ ? "! " : "") + status_, contentWidth());
    }

    // Keep the same field, and result, focused while rows come and go around it.
    // When the focused result goes away (e.g. the search changed), stay in its list.
    focus_ = -1;
    int sameList = -1;
    for (size_t i = 0; i < rows_.size(); i++) {
        if (!rows_[i].focusable || rows_[i].field != focusedField_) {
            continue;
        }
        if (rows_[i].item == focusedItem_) {
            focus_ = static_cast<int>(i);
            break;
        }
        if (sameList < 0) {
            sameList = static_cast<int>(i);
        }
    }
    if (focus_ < 0) {
        focus_ = sameList;
    }
    if (focus_ < 0) {
        for (size_t i = 0; i < rows_.size(); i++) {
            if (rows_[i].focusable) {
                focus_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (focus_ >= 0) {
        focusedField_ = rows_[focus_].field;
        focusedItem_ = rows_[focus_].item;
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
        // And a confirm question or a problem under it
        for (size_t below = static_cast<size_t>(focus_) + 1; below < rows_.size() && rows_[below].kind == RowKind::Note && rows_[below].indent; below++) {
            bottomY += rowHeight();
        }
        if (topY < scroll_) {
            scroll_ = topY;
        }
        if (bottomY > scroll_ + visibleHeight) {
            scroll_ = std::min(topY, bottomY - visibleHeight);
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
            focusedItem_ = rows_[index].item;
            return;
        }
    }
}

// Left/Right in a results list: a page at a time, staying in the list
void FormWidget::pageResults(int direction)
{
    if (focus_ < 0) {
        return;
    }
    const Field* list = rows_[focus_].field;
    int target = focus_;
    for (int step = 0; step < pageRows_; step++) {
        int next = target + direction;
        if (next < 0 || next >= static_cast<int>(rows_.size()) || rows_[next].kind != RowKind::Item || rows_[next].field != list) {
            break;
        }
        target = next;
    }
    focus_ = target;
    focusedItem_ = rows_[target].item;
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
    if (stale_ || rows_.empty()) {
        buildRows();
    }
    stale_ = true;   // whatever happens next shows on the next draw

    const Row* row = focus_ >= 0 ? &rows_[focus_] : nullptr;
    const Field* field = row ? row->field : nullptr;
    static const char* const names[] = { "Up", "Down", "Left", "Right", "Select", "Back" };
    LOG_DEBUG("Forms", std::string(names[static_cast<int>(nav)]) + " on " + (field ? field->label : "nothing"));

    // Anything but confirming disarms a button waiting for confirmation; Back only does that
    if (confirm_.armed() && !(nav == Nav::Select && field == confirm_.armed())) {
        confirm_.cancel();
        if (nav == Nav::Back) {
            return true;
        }
    }

    switch (nav) {
    case Nav::Up:
        moveFocus(-1);
        break;
    case Nav::Down:
        moveFocus(1);
        break;
    case Nav::Left:
    case Nav::Right:
        if (row && row->kind == RowKind::Item) {
            pageResults(nav == Nav::Right ? 1 : -1);
        }
        else if (field) {
            if ((field->type == FieldType::Folder || field->type == FieldType::File) && nav == Nav::Right) {
                startEditing(*field);
            }
            else {
                change(*field, nav == Nav::Right ? 1 : -1);
            }
        }
        break;
    case Nav::Select:
        if (row && field) {
            activate(*row);
        }
        break;
    case Nav::Back:
        return false;
    }
    return true;
}

void FormWidget::activate(const Row& row)
{
    const Field& field = *row.field;
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
        if (model_->actionProblem(field.action).empty() && confirm_.select(field)) {
            result_ = model_->runAction(field.action);
        }
        break;
    case FieldType::Link:
    case FieldType::Results:
        if (!field.open.empty()) {
            Forms::Model::Result open;
            open.ok = true;
            open.open = field.open;
            if (field.type == FieldType::Results) {
                open.params = row.params;
            }
            else {
                open.keepParams = true;
            }
            result_ = open;
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
    editOriginal_ = editBuffer_;
    liveSent_ = editBuffer_;
    stale_ = true;
    LOG_DEBUG("Forms", "Editing " + field.label);
    textInputWasActive_ = SDL_IsTextInputActive() == SDL_TRUE;
    SDL_StartTextInput();
}

void FormWidget::sendLiveText()
{
    if (editing_ && focusedField_ && focusedField_->live && model_ && editBuffer_ != liveSent_) {
        liveSent_ = editBuffer_;
        model_->set(focusedField_->bind, editBuffer_);
        stale_ = true;
    }
}

void FormWidget::finishEditing(bool keep)
{
    if (!editing_) {
        return;
    }
    editing_ = false;
    justFinishedEditing_ = true;
    stale_ = true;
    if (!textInputWasActive_) {
        SDL_StopTextInput();
    }
    if (!focusedField_ || !model_) {
        return;
    }
    if (keep) {
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
    else if (focusedField_->live && liveSent_ != editOriginal_) {
        model_->set(focusedField_->bind, editOriginal_);
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
        sendLiveText();
        return true;
    }
    if (e.type == SDL_KEYDOWN) {
        SDL_Keycode key = e.key.keysym.sym;
        if (key == SDLK_BACKSPACE) {
            popCharacter(editBuffer_);
            sendLiveText();
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
            sendLiveText();
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

// What a field shows, read when the rows are built
std::string FormWidget::valueText(const Field& field)
{
    switch (field.type) {
    case FieldType::Toggle:
        return Forms::isTruthy(model_->get(field.bind)) ? style_.onText : style_.offText;
    case FieldType::Choice: {
        std::string value = model_->get(field.bind);
        for (const auto& [optionValue, optionLabel] : model_->options(field.options)) {
            if (optionValue == value) {
                return optionLabel;
            }
        }
        return value;
    }
    case FieldType::Link:
        return field.status.empty() ? "" : model_->get(field.status);
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
    const Row* row = focus_ >= 0 && focus_ < static_cast<int>(rows_.size()) ? &rows_[focus_] : nullptr;
    const Field* field = row ? row->field : nullptr;
    if (field && confirm_.armed() == field) {
        return "Select: " + field->label + "   Anything else: cancel";
    }
    std::string hint;
    if (row && row->kind == RowKind::Item) {
        hint = field && !field->open.empty() ? "Select: open   Left/Right: page" : "Left/Right: page";
    }
    else if (field) {
        switch (field->type) {
        case FieldType::Text:   hint = "Select: edit"; break;
        case FieldType::Number: hint = "Left/Right: change   Select: type"; break;
        case FieldType::Toggle: hint = "Select: change"; break;
        case FieldType::Choice: hint = "Left/Right: change"; break;
        case FieldType::Button: hint = "Select: " + field->label; break;
        case FieldType::Link:   hint = "Select: open"; break;
        case FieldType::Folder:
        case FieldType::File:   hint = canBrowse_ ? "Select: browse   Right: type" : "Select: edit"; break;
        default: break;
        }
    }
    return (hint.empty() ? "" : hint + "   ") + (backCloses_ ? "Back: close" : "Back: return");
}

// Fits the picture in the box, keeping its shape, at the box's top left
void FormWidget::drawPicture(SDL_Texture* picture, float x, float y, float maxWidth, float maxHeight)
{
    int w = 0;
    int h = 0;
    if (!picture || SDL_QueryTexture(picture, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0
        || maxWidth <= 0.0f || maxHeight <= 0.0f) {
        return;
    }
    float scale = std::min(maxWidth / static_cast<float>(w), maxHeight / static_cast<float>(h));
    painter_.texture(picture, x, y, w * scale, h * scale, 1.0f);
}

void FormWidget::draw()
{
    Component::draw();
    justFinishedEditing_ = false;
    if (!form_ || !model_ || !painter_.hasFont() || baseViewInfo.Alpha <= 0.0f) {
        return;
    }

    // Rebuilt after input, when the model says something changed, and now and then
    if (stale_ || model_->generation() != generation_ || SDL_GetTicks() - builtAt_ >= refreshMs) {
        buildRows();
    }
    pictures_.beginFrame(thumbnailsPerFrame);
    painter_.begin(baseViewInfo);

    const float size = fontSize();
    const float padding = size * 0.6f;
    const float left = baseViewInfo.XRelativeToOrigin() + padding;
    const float top = baseViewInfo.YRelativeToOrigin() + padding;
    const float width = contentWidth();
    const float height = (baseViewInfo.Height > 0 ? baseViewInfo.Height
        : static_cast<float>(page.getLayoutHeightByMonitor(baseViewInfo.Monitor)) - baseViewInfo.Y) - padding * 2.0f;
    const float row = rowHeight();
    const float hintsHeight = style_.showHints ? row : 0.0f;
    const float statusHeight = statusLines_.size() * row;
    const float visible = height - hintsHeight - statusHeight;
    const float textOffset = (row - size) / 2.0f;
    const float valueX = left + labelWidth();
    const float valueWidth = width - labelWidth();
    const float gap = size * 0.5f;
    const float scale = pixelScale();

    pageRows_ = std::max(1, static_cast<int>(visible / row) - 1);
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
            // isError marks a question that needs an answer, e.g. a confirmation
            painter_.text((r.isError ? "! " : "") + r.text, r.indent ? valueX : left, textY, r.indent ? valueWidth : width,
                          size * 0.85f, false, r.isError ? 1.0f : style_.noteAlpha);
            break;
        case RowKind::Value:
            if (r.field) {
                painter_.text(r.field->label, left, textY, labelWidth(), size, false, 1.0f);
            }
            painter_.text(r.text, valueX, textY, valueWidth, size, false, 1.0f);
            break;
        case RowKind::Item: {
            float x = left;
            if (r.thumbnail) {
                float thumbWidth = row * 1.4f;
                if (!r.image.empty()) {
                    SDL_Texture* thumbnail = pictures_.get(r.image, static_cast<int>(thumbWidth * scale),
                                                           static_cast<int>(row * 0.84f * scale), false);
                    drawPicture(thumbnail, x, y + row * 0.08f, thumbWidth, row * 0.84f);
                }
                x += thumbWidth + gap;
            }
            // The detail on the right, the label in what's left
            float detailWidth = 0.0f;
            if (!r.detail.empty()) {
                detailWidth = std::min(painter_.textWidth(r.detail, size), width * 0.4f);
                painter_.textWith(detailFont_, r.detail, left + width - detailWidth, textY, detailWidth, size, style_.rowDetailAlpha);
            }
            painter_.text(r.text, x, textY, left + width - x - detailWidth - (detailWidth > 0 ? gap : 0.0f), size, focused, 1.0f);
            break;
        }
        case RowKind::Image: {
            const Field& field = *r.field;
            bool labelled = !field.label.empty();
            if (labelled) {
                painter_.text(field.label, left, textY, labelWidth(), size, false, 1.0f);
            }
            // Loaded at the size addImage asked for; drawn smaller when its last rows are scrolled out
            float boxWidth = labelled ? valueWidth : width;
            SDL_Texture* picture = pictures_.get(r.image, static_cast<int>(boxWidth * scale),
                                                 static_cast<int>(r.span * row * scale), true);
            float boxHeight = std::min(r.span * row, top + visible - y) - row * 0.1f;
            drawPicture(picture, labelled ? valueX : left, y + row * 0.05f, boxWidth, boxHeight);
            break;
        }
        case RowKind::Spacer:
            break;
        case RowKind::Field: {
            const Field& field = *r.field;
            float alpha = r.enabled ? 1.0f : style_.disabledAlpha;
            painter_.text(field.label, left, textY, labelWidth(), size, focused, alpha);
            float fieldValueWidth = valueWidth;
            if (field.type == FieldType::Link && !style_.linkMarker.empty()) {
                float markerWidth = painter_.textWidth(style_.linkMarker, size);
                painter_.text(style_.linkMarker, left + width - markerWidth, textY, markerWidth, size, focused, alpha);
                fieldValueWidth -= markerWidth + gap;
            }
            bool editingThis = editing_ && focused;
            std::string value = r.value;
            if (editingThis) {
                // Keep the end in view while typing, e.g. the folder at the end of a long path
                value = painter_.tail(editBuffer_ + "_", valueWidth, size);
            }
            else if (focused && (field.type == FieldType::Number || field.type == FieldType::Choice)) {
                value = "< " + value + " >";
            }
            if (value.empty() && !field.hint.empty() && !editingThis) {
                painter_.text(field.hint, valueX, textY, fieldValueWidth, size, false, style_.noteAlpha * alpha);
            }
            else {
                painter_.text(value, valueX, textY, fieldValueWidth, size, focused, alpha);
            }
            break;
        }
        }
    }

    // The last result or warning stays in view above the hints
    float statusY = top + visible;
    for (const std::string& line : statusLines_) {
        painter_.text(line, left, statusY + textOffset, width, size, false, 1.0f);
        statusY += row;
    }
    if (style_.showHints) {
        painter_.text(hintText(), left, top + height - hintsHeight + textOffset, width, size * 0.85f, false, style_.noteAlpha);
    }
}

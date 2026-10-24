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

    // A field whose Select opens a native file or folder picker
    bool isPathField(const Field* field)
    {
        return field && (field->type == FieldType::Folder || field->type == FieldType::File);
    }

    bool isMultiline(const Field* field)
    {
        return field && field->type == FieldType::Text && field->multiline;
    }

    // The screen is rebuilt at least this often, for values that change by
    // themselves, such as Catalog progress
    constexpr uint32_t refreshMs = 250;

    // New thumbnails loaded per frame, so paging through a list doesn't stall
    constexpr int thumbnailsPerFrame = 2;

    // Wrapped texts remembered; more and they're all forgotten
    constexpr size_t maxWrapped = 1024;

    // Notes, headers and hints are drawn this much smaller than fields
    constexpr float noteScale = 0.85f;
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
    focusGuarded_ = false;
    focusedField_ = nullptr;
    focusedItem_.clear();
    scroll_ = 0.0f;
    status_.clear();
    confirm_.cancel();
    rows_.clear();
    stale_ = true;
    pictures_.recheck();   // a new screen: its pictures may have changed since they were last shown
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
    focus_ = -1;
    focusGuarded_ = false;
    rows_.clear();
    stale_ = true;
}

void FormWidget::setFont(FontRole role, FontManager* font)
{
    switch (role) {
    case FontRole::Detail: detailFont_ = font; break;
    case FontRole::Header: headerFont_ = font; break;
    case FontRole::Status: statusFont_ = font; break;
    case FontRole::Info:   infoFont_ = font; break;
    case FontRole::Error:  errorFont_ = font; break;
    }
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

void FormWidget::setStatus(const std::string& message, Forms::StatusKind kind)
{
    status_ = message;
    statusKind_ = kind;
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

const std::vector<Forms::TextLine>& FormWidget::layout(const std::string& text, float width)
{
    std::string key = std::to_string(std::lround(width)) + '\x1f' + text;
    auto found = layouts_.find(key);
    if (found != layouts_.end()) {
        return found->second;
    }
    if (layouts_.size() >= 64) {
        layouts_.clear();   // one per edit, so they pile up while typing
    }
    float size = fontSize();
    auto measure = [&](const std::string& s) { return painter_.textWidth(s, size); };
    return layouts_.emplace(key, Forms::layoutLines(text, width, measure)).first->second;
}

bool FormWidget::editingMultiline() const
{
    return editing_ && isMultiline(focusedField_);
}

float FormWidget::multilineWidth(const Field& field) const
{
    // Beside the label like other values, or the whole width without one
    return field.label.empty() ? contentWidth() : contentWidth() - labelWidth();
}

void FormWidget::addWrapped(RowKind kind, const std::string& text, bool indent, bool isError)
{
    float width = indent ? contentWidth() - labelWidth() : contentWidth();
    if (kind == RowKind::Note) {
        width /= noteScale;   // notes are drawn smaller, so more fits on a line
    }
    // isError marks a question that needs an answer, e.g. a confirmation
    for (const std::string& line : wrap(isError ? "! " + text : text, width)) {
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
            if (isMultiline(&field)) {
                // Its further lines, each a row of its own so scrolling works as for any row
                for (int line = 1; line < std::max(1, field.rows); line++) {
                    Row more;
                    more.kind = RowKind::TextLine;
                    more.field = &field;
                    more.line = line;
                    more.value = row.value;
                    more.enabled = row.enabled;
                    rows_.push_back(more);
                }
            }

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
    const int previousFocus = focus_;
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
        statusLines_ = wrap((statusKind_ == Forms::StatusKind::Error ? "! " : "") + status_, contentWidth());
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
    if (focus_ < 0 && focusedField_ && previousFocus >= 0) {
        // The focused field went away (e.g. a button that hides itself once
        // pressed): stay where it was, or go up. Not onto a file or folder field
        // when there's anything else, and if it has to be one, Select there
        // waits for a move, so a second press doesn't open a picker.
        std::vector<Forms::FocusTarget> targets;
        targets.reserve(rows_.size());
        for (const Row& r : rows_) {
            targets.push_back(!r.focusable ? Forms::FocusTarget::None
                              : isPathField(r.field) ? Forms::FocusTarget::Guarded
                              : Forms::FocusTarget::Free);
        }
        focus_ = Forms::refocusIndex(targets, previousFocus);
        focusGuarded_ = focus_ >= 0 && isPathField(rows_[focus_].field);
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
        // And a multi-line field's further lines, and a confirm question or a problem under it
        for (size_t below = static_cast<size_t>(focus_) + 1;
             below < rows_.size() && ((rows_[below].kind == RowKind::Note && rows_[below].indent) || rows_[below].kind == RowKind::TextLine);
             below++) {
            bottomY += rowHeight();
        }
        // At the last field, everything below it too (notes the user can't move to)
        bool lastFocusable = std::none_of(rows_.begin() + focus_ + 1, rows_.end(), [](const Row& r) { return r.focusable; });
        if (lastFocusable) {
            bottomY = total;
        }
        if (topY < scroll_) {
            scroll_ = topY;
        }
        if (bottomY > scroll_ + visibleHeight) {
            // Whole rows at the top when that still shows the focused one
            float wanted = bottomY - visibleHeight;
            float snapped = std::ceil(wanted / rowHeight() - 0.01f) * rowHeight();
            scroll_ = std::min(topY, snapped <= topY ? snapped : wanted);
        }
        // At the first field, show everything above it
        bool firstFocusable = std::none_of(rows_.begin(), rows_.begin() + focus_, [](const Row& r) { return r.focusable; });
        if (firstFocusable && bottomY <= visibleHeight) {
            scroll_ = 0.0f;
        }
    }
    // Scrolled to the end, whole rows at the top too, and a gap under the last one
    float maxScroll = std::max(0.0f, total - visibleHeight);
    if (maxScroll > 0.0f) {
        maxScroll = std::ceil(maxScroll / rowHeight() - 0.01f) * rowHeight();
    }
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
}

// --- Input ---------------------------------------------------------------------

void FormWidget::moveFocus(int direction)
{
    focusGuarded_ = false;
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

    // Only a Select straight after rows went away is held back on a file or folder field
    if (nav != Nav::Select) {
        focusGuarded_ = false;
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
        if (canBrowse_ && focusGuarded_) {
            break;   // focus only got here because rows went away; the user moves first
        }
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
            pictures_.recheck();   // it may have written artwork
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
    focusGuarded_ = false;
    edit_.set(model_->get(field.bind));   // the cursor at the end
    editTopLine_ = 0;
    editOriginal_ = edit_.text;
    liveSent_ = edit_.text;
    stale_ = true;
    LOG_DEBUG("Forms", "Editing " + field.label);
    textInputWasActive_ = SDL_IsTextInputActive() == SDL_TRUE;
    SDL_StartTextInput();
}

void FormWidget::sendLiveText()
{
    if (editing_ && focusedField_ && focusedField_->live && model_ && edit_.text != liveSent_) {
        liveSent_ = edit_.text;
        model_->set(focusedField_->bind, edit_.text);
        stale_ = true;
    }
}

void FormWidget::moveEditLine(int direction)
{
    if (!editingMultiline()) {
        return;
    }
    float size = fontSize();
    auto measure = [&](const std::string& s) { return painter_.textWidth(s, size); };
    const std::vector<Forms::TextLine>& lines = layout(edit_.text, multilineWidth(*focusedField_));
    edit_.cursor = Forms::moveCursorVertically(edit_.text, lines, edit_.cursor, direction, measure);
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
            int value = Utils::convertInt(edit_.text);
            if (focusedField_->max > focusedField_->min) {
                value = std::clamp(value, focusedField_->min, focusedField_->max);
            }
            edit_.set(std::to_string(value));
        }
        model_->set(focusedField_->bind, edit_.text);
        LOG_DEBUG("Forms", focusedField_->label + " set to \"" + edit_.text + "\"");
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
    bool multiline = editingMultiline();

    if (e.type == SDL_TEXTINPUT) {
        std::string text = e.text.text;
        if (number) {
            text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return !(c == '-' || (c >= '0' && c <= '9')); }), text.end());
        }
        edit_.insert(text);
        sendLiveText();
        return true;
    }
    if (e.type == SDL_KEYDOWN) {
        SDL_Keycode key = e.key.keysym.sym;
        bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
        if (key == SDLK_BACKSPACE) {
            edit_.backspace();
            sendLiveText();
        }
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            // In a multi-line field Enter starts a new line and Ctrl+Enter keeps the text
            if (multiline && !ctrl) {
                edit_.insert("\n");
                sendLiveText();
            }
            else {
                finishEditing(true);
            }
        }
        else if (key == SDLK_ESCAPE) {
            finishEditing(false);
        }
        else if (key == SDLK_v && ctrl && SDL_HasClipboardText()) {
            char* clipboard = SDL_GetClipboardText();
            std::string text = clipboard ? clipboard : "";
            SDL_free(clipboard);
            // Line breaks only where the field has lines, and always as "\n"
            text.erase(std::remove_if(text.begin(), text.end(), [&](char c) { return c == '\r' || (c == '\n' && !multiline); }),
                       text.end());
            edit_.insert(text);
            sendLiveText();
        }
        else if (multiline && (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN
                               || key == SDLK_HOME || key == SDLK_END || key == SDLK_DELETE
                               || key == SDLK_PAGEUP || key == SDLK_PAGEDOWN)) {
            // The cursor keys move in the text (the manager leaves them to the
            // on-screen keyboard while that's shown)
            float size = fontSize();
            auto measure = [&](const std::string& s) { return painter_.textWidth(s, size); };
            const std::vector<Forms::TextLine>& lines = layout(edit_.text, multilineWidth(*focusedField_));
            int page = std::max(1, focusedField_->rows - 1);
            switch (key) {
            case SDLK_LEFT: edit_.left(); break;
            case SDLK_RIGHT: edit_.right(); break;
            case SDLK_UP:
            case SDLK_DOWN:
                edit_.cursor = Forms::moveCursorVertically(edit_.text, lines, edit_.cursor, key == SDLK_UP ? -1 : 1, measure);
                break;
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
                for (int i = 0; i < page; i++) {
                    edit_.cursor = Forms::moveCursorVertically(edit_.text, lines, edit_.cursor, key == SDLK_PAGEUP ? -1 : 1, measure);
                }
                break;
            case SDLK_HOME: edit_.cursor = ctrl ? 0 : Forms::lineHome(lines, edit_.cursor); break;
            case SDLK_END: edit_.cursor = ctrl ? edit_.text.size() : Forms::lineEnd(edit_.text, lines, edit_.cursor); break;
            case SDLK_DELETE: edit_.erase(); sendLiveText(); break;
            default: break;
            }
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
        return editingMultiline() ? "Enter: new line   Ctrl+Enter: keep   Esc: cancel   Ctrl+V: paste"
                                  : "Type, then Enter to keep or Esc to cancel. Ctrl+V pastes.";
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
        case FieldType::File:
            hint = !canBrowse_ ? "Select: edit"
                : focusGuarded_ ? "Any direction, then Select: browse   Right: type"
                : "Select: browse   Right: type";
            break;
        default: break;
        }
    }
    return (hint.empty() ? "" : hint + "   ") + (backCloses_ ? "Back: close" : "Back: return");
}

// One row of a multi-line text field: line row.line of what's shown (while
// it's edited, counted from the first line in view), with the cursor
void FormWidget::drawTextLine(const Row& row, float x, float textY, bool highlighted, float alpha)
{
    const Field& field = *row.field;
    const float size = fontSize();
    const float width = multilineWidth(field);
    bool editingThis = editing_ && focusedField_ == &field;
    const std::string& text = editingThis ? edit_.text : row.value;
    if (text.empty() && !editingThis) {
        if (row.line == 0 && !field.hint.empty()) {
            painter_.text(field.hint, x, textY, width, size, false, style_.noteAlpha * alpha);
        }
        return;
    }
    const std::vector<Forms::TextLine>& lines = layout(text, width);
    size_t index = static_cast<size_t>((editingThis ? editTopLine_ : 0) + row.line);
    if (index >= lines.size()) {
        return;
    }
    const Forms::TextLine& line = lines[index];
    std::string shown = text.substr(line.start, line.end - line.start);
    while (!shown.empty() && (shown.back() == ' ' || shown.back() == '\r')) {
        shown.pop_back();   // spaces where a line wraps hang past the edge
    }
    int span = std::max(1, field.rows);
    if (!editingThis && row.line == span - 1 && lines.size() > static_cast<size_t>(span)) {
        shown += " ...";   // more than fits: shown in full while it's edited
    }
    painter_.text(shown, x, textY, width, size, highlighted, alpha);
    if (editingThis && Forms::lineAt(lines, edit_.cursor) == index) {
        size_t cursor = std::clamp(edit_.cursor, line.start, line.end);
        float caretX = x + painter_.textWidth(text.substr(line.start, cursor - line.start), size);
        float bar = painter_.textWidth("|", size);
        painter_.text("|", caretX - bar / 2.0f, textY, bar * 2.0f, size, true, alpha);
    }
}

// Fits the picture in the box, keeping its shape, at the box's top left or
// centred in it; backed: on imageBackgroundColor, a little larger than the picture
void FormWidget::drawPicture(SDL_Texture* picture, float x, float y, float maxWidth, float maxHeight, bool centered, bool backed,
                             float clipTop, float clipBottom)
{
    int w = 0;
    int h = 0;
    if (!picture || SDL_QueryTexture(picture, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0
        || maxWidth <= 0.0f || maxHeight <= 0.0f) {
        return;
    }
    float scale = std::min(maxWidth / static_cast<float>(w), maxHeight / static_cast<float>(h));
    float drawnWidth = w * scale;
    float drawnHeight = h * scale;
    if (centered) {
        x += (maxWidth - drawnWidth) / 2.0f;
        y += (maxHeight - drawnHeight) / 2.0f;
    }
    if (backed && style_.imageBackgroundAlpha > 0.0f) {
        float margin = fontSize() * 0.15f;
        float backTop = std::max(y - margin, clipTop);
        float backBottom = std::min(y + drawnHeight + margin, clipBottom);
        if (backBottom > backTop) {
            painter_.rect(x - margin, backTop, drawnWidth + margin * 2.0f, backBottom - backTop,
                          style_.imageBackground, style_.imageBackgroundAlpha);
        }
    }
    // Only the part between clipTop and clipBottom, e.g. while it's scrolled part way out
    float shownTop = std::max(y, clipTop);
    float shownBottom = std::min(y + drawnHeight, clipBottom);
    if (shownBottom - shownTop < 1.0f) {
        return;
    }
    if (shownTop == y && shownBottom == y + drawnHeight) {
        painter_.texture(picture, x, y, drawnWidth, drawnHeight, 1.0f);
        return;
    }
    SDL_Rect source{ 0, static_cast<int>(std::lround((shownTop - y) / scale)), w,
                     std::max(1, static_cast<int>(std::lround((shownBottom - shownTop) / scale))) };
    source.h = std::min(source.h, h - source.y);
    painter_.texture(picture, x, shownTop, drawnWidth, source.h * scale, 1.0f, &source);
}

void FormWidget::draw()
{
    Component::draw();
    justFinishedEditing_ = false;
    if (!form_ || !model_ || !painter_.hasFont() || baseViewInfo.Alpha <= 0.0f) {
        return;
    }

    // Rebuilt after input, when the model says something changed, and now and then
    if (model_->generation() != generation_) {
        // Something was written (e.g. a game's artwork): pictures check their files once
        pictures_.recheck();
    }
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

    // A multi-line field being edited scrolls within its rows to keep the cursor
    // in view, and the form scrolls to keep the cursor's row in view too (on a
    // short page the field may be taller than what's shown)
    if (editingMultiline() && focus_ >= 0) {
        const std::vector<Forms::TextLine>& lines = layout(edit_.text, multilineWidth(*focusedField_));
        int cursorLine = static_cast<int>(Forms::lineAt(lines, edit_.cursor));
        int span = std::max(1, focusedField_->rows);
        editTopLine_ = std::clamp(editTopLine_, cursorLine - span + 1, cursorLine);
        editTopLine_ = std::clamp(editTopLine_, 0, std::max(0, static_cast<int>(lines.size()) - span));
        float caretTop = (focus_ + cursorLine - editTopLine_) * row;
        if (caretTop < scroll_) {
            scroll_ = caretTop;
        }
        else if (caretTop + row > scroll_ + visible) {
            scroll_ = caretTop + row - visible;
        }
    }

    const float line = std::max(1.0f, size * 0.04f);   // separators
    const Field* armed = confirm_.armed();

    auto drawHighlight = [&](float y, float h) {
        if (focusImage_) {
            ViewInfo& view = focusImage_->baseViewInfo;
            view.X = left - padding / 2.0f;
            view.Y = y;
            view.Width = width + padding;
            view.Height = h;
            view.Alpha = baseViewInfo.Alpha;
            view.Monitor = baseViewInfo.Monitor;
            view.Layout = baseViewInfo.Layout;
            focusImage_->draw();
        }
        else {
            painter_.rect(left - padding / 2.0f, y, width + padding, h, style_.focusBackground, style_.focusBackgroundAlpha);
        }
    };
    // The focused multi-line field: one highlight over all its rows, cut to the
    // rows in view (it may be taller than the page shows)
    bool groupHighlight = focus_ >= 0 && rows_[focus_].kind == RowKind::Field && isMultiline(rows_[focus_].field);
    if (groupHighlight) {
        float from = 1e9f;
        float to = -1e9f;
        for (size_t i = static_cast<size_t>(focus_); i < rows_.size() && (i == static_cast<size_t>(focus_) || rows_[i].kind == RowKind::TextLine); i++) {
            float y = top + i * row - scroll_;
            if (y >= top - 0.5f && y + row <= top + visible + 0.5f) {   // the rows drawn, as below
                from = std::min(from, y);
                to = std::max(to, y + row);
            }
        }
        if (to > from) {
            drawHighlight(from, to - from);
        }
    }

    for (size_t i = 0; i < rows_.size(); i++) {
        float y = top + i * row - scroll_;
        const Row& r = rows_[i];
        // A row shows when all of it is in view, and a picture also when its
        // first row has scrolled out the top but some of the rest hasn't
        float rowsHeight = r.kind == RowKind::Image ? r.span * row : row;
        bool inView = y >= top - 0.5f && y + row <= top + visible + 0.5f;
        bool partlyAbove = r.kind == RowKind::Image && y < top - 0.5f && y + rowsHeight >= top + row * 0.5f;
        if (!inView && !partlyAbove) {
            continue;
        }
        bool focused = static_cast<int>(i) == focus_;
        bool confirming = focused && armed && r.field == armed;
        float textY = y + textOffset;
        // A multi-line field's further lines are highlighted with it (drawn above, as one block)
        bool highlighted = focused || (r.kind == RowKind::TextLine && focus_ >= 0 && rows_[focus_].field == r.field
                                       && rows_[focus_].kind == RowKind::Field);

        if (focused) {
            if (!groupHighlight) {
                drawHighlight(y, row);
            }
            if (confirming && style_.confirmBackgroundAlpha > 0.0f) {
                painter_.rect(left - padding / 2.0f, y, width + padding, row, style_.confirmBackground, style_.confirmBackgroundAlpha);
            }
        }

        switch (r.kind) {
        case RowKind::Title:
            painter_.text(r.text, left, y + (row - size * 1.3f) / 2.0f, width, size * 1.3f, false, 1.0f);
            break;
        case RowKind::Header: {
            float headerSize = size * noteScale;
            painter_.textWith(headerFont_, r.text, left, y + (row - headerSize) / 2.0f, width, headerSize, style_.headerAlpha);
            if (style_.separatorAlpha > 0.0f) {
                painter_.rect(left, y + row - line, width, line, style_.separator, style_.separatorAlpha);
            }
            break;
        }
        case RowKind::Note: {
            // isError marks a question that needs an answer, e.g. a confirmation
            float noteSize = size * noteScale;
            float noteY = y + (row - noteSize) / 2.0f;
            float noteX = r.indent ? valueX : left;
            float noteWidth = r.indent ? valueWidth : width;
            if (r.isError) {
                painter_.textWith(errorFont_, r.text, noteX, noteY, noteWidth, noteSize, 1.0f);
            }
            else {
                painter_.text(r.text, noteX, noteY, noteWidth, noteSize, false, style_.noteAlpha);
            }
            break;
        }
        case RowKind::Value:
            if (r.field) {
                painter_.text(r.field->label, left, textY, labelWidth() - gap, size, false, 1.0f);
            }
            painter_.text(r.text, valueX, textY, valueWidth, size, false, 1.0f);
            break;
        case RowKind::Item: {
            float x = left;
            if (r.thumbnail) {
                float thumbWidth = row * 1.4f;
                float thumbHeight = row * 0.84f;
                float thumbY = y + row * 0.08f;
                if (style_.imageBackgroundAlpha > 0.0f) {
                    // The same box on every row, so the names line up and a missing picture shows as a gap
                    painter_.rect(x, thumbY, thumbWidth, thumbHeight, style_.imageBackground, style_.imageBackgroundAlpha);
                }
                if (!r.image.empty()) {
                    SDL_Texture* thumbnail = pictures_.get(r.image, static_cast<int>(thumbWidth * scale),
                                                           static_cast<int>(thumbHeight * scale), false);
                    drawPicture(thumbnail, x, thumbY, thumbWidth, thumbHeight, true, false);
                }
                x += thumbWidth + gap;
            }
            // The detail on the right, as wide as it needs up to about half the row
            // when the label needs the room; whichever doesn't fit ends in "..."
            float available = left + width - x;
            float detailNeeds = r.detail.empty() ? 0.0f : painter_.textWidthWith(detailFont_, r.detail, size);
            Forms::RowSplit split = Forms::splitRow(painter_.textWidthWith(nullptr, r.text, size), detailNeeds, available, gap);
            if (split.detail > 0.0f) {
                float detailX = left + width - std::min(detailNeeds, split.detail);
                painter_.textWith(detailFont_, r.detail, detailX, textY, split.detail, size, style_.rowDetailAlpha);
            }
            painter_.text(r.text, x, textY, split.label, size, focused, 1.0f);
            // A line between this and the next row of the list
            if (style_.separatorAlpha > 0.0f && i + 1 < rows_.size() && rows_[i + 1].kind == RowKind::Item && rows_[i + 1].field == r.field) {
                painter_.rect(left, y + row - line / 2.0f, width, line, style_.separator, style_.separatorAlpha * 0.6f);
            }
            break;
        }
        case RowKind::Image: {
            const Field& field = *r.field;
            bool labelled = !field.label.empty();
            if (labelled && inView) {
                painter_.text(field.label, left, textY, labelWidth() - gap, size, false, 1.0f);
            }
            // Loaded at the size addImage asked for; cut off where its rows are scrolled out
            float boxWidth = labelled ? valueWidth : width;
            SDL_Texture* picture = pictures_.get(r.image, static_cast<int>(boxWidth * scale),
                                                 static_cast<int>(r.span * row * scale), true);
            drawPicture(picture, labelled ? valueX : left, y + row * 0.1f, boxWidth, rowsHeight - row * 0.2f, false, true,
                        top, top + visible);
            break;
        }
        case RowKind::Spacer:
            break;
        case RowKind::TextLine:
            drawTextLine(r, r.field->label.empty() ? left : valueX, textY, highlighted, r.enabled ? 1.0f : style_.disabledAlpha);
            break;
        case RowKind::Field: {
            const Field& field = *r.field;
            float alpha = r.enabled ? 1.0f : style_.disabledAlpha;
            painter_.text(field.label, left, textY, labelWidth() - gap, size, focused, alpha);
            if (isMultiline(&field)) {
                drawTextLine(r, field.label.empty() ? left : valueX, textY, focused, alpha);
                break;
            }
            float fieldValueWidth = valueWidth;
            bool isLink = field.type == FieldType::Link;
            if (isLink && !style_.linkMarker.empty()) {
                float markerWidth = painter_.textWidthWith(detailFont_, style_.linkMarker, size);
                if (focused) {
                    painter_.text(style_.linkMarker, left + width - markerWidth, textY, markerWidth, size, true, alpha);
                }
                else {
                    painter_.textWith(detailFont_, style_.linkMarker, left + width - markerWidth, textY, markerWidth, size, style_.rowDetailAlpha * alpha);
                }
                fieldValueWidth -= markerWidth + gap;
            }
            bool editingThis = editing_ && focused;
            std::string value = r.value;
            if (editingThis) {
                // Keep the end in view while typing, e.g. the folder at the end of a long path
                value = painter_.tail(edit_.text + "_", valueWidth, size);
            }
            else if (focused && (field.type == FieldType::Number || field.type == FieldType::Choice)) {
                // The arrows stay when the value is too long
                float arrows = painter_.textWidth("<  >", size);
                value = "< " + painter_.fit(nullptr, value, fieldValueWidth - arrows, size) + " >";
            }
            if (value.empty() && !field.hint.empty() && !editingThis) {
                painter_.text(field.hint, valueX, textY, fieldValueWidth, size, false, style_.noteAlpha * alpha);
            }
            else if (isLink && !focused) {
                // A link's status reads like a result's detail
                painter_.textWith(detailFont_, value, valueX, textY, fieldValueWidth, size, style_.rowDetailAlpha * alpha);
            }
            else {
                painter_.text(value, valueX, textY, fieldValueWidth, size, focused, alpha);
            }
            break;
        }
        }
    }

    // The last result or warning stays in view above the hints; the rows'
    // visible height leaves room for it, so it never covers the focused row
    float statusY = top + visible;
    // infoColor when nothing happened; without one, the normal text colour (a null font)
    FontManager* statusFont = statusKind_ == Forms::StatusKind::Error ? errorFont_
                            : statusKind_ == Forms::StatusKind::Info  ? infoFont_
                                                                      : statusFont_;
    for (const std::string& statusLine : statusLines_) {
        painter_.textWith(statusFont, statusLine, left, statusY + textOffset, width, size, 1.0f);
        statusY += row;
    }
    if (style_.showHints) {
        const float hintSize = size * noteScale;
        painter_.text(hintText(), left, top + height - hintsHeight + (row - hintSize) / 2.0f, width, hintSize, false, style_.noteAlpha);
    }
}

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
#include "FormView.h"
#include "FileDialog.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cstdint>

using Forms::Field;
using Forms::FieldType;

namespace
{
    float textWidth(const std::string& text)
    {
        return ImGui::CalcTextSize(text.c_str()).x;
    }

    // Text the model gives a severity: in its color, else muted like other notes
    void severityText(Forms::Severity severity, const std::string& text)
    {
        switch (severity) {
        case Forms::Severity::Ok:      ImGui::TextColored(ImVec4(0.62f, 0.89f, 0.65f, 1.0f), "%s", text.c_str()); break;
        case Forms::Severity::Warning: ImGui::TextColored(ImVec4(0.95f, 0.77f, 0.43f, 1.0f), "%s", text.c_str()); break;
        case Forms::Severity::Error:   ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.54f, 1.0f), "%s", text.c_str()); break;
        case Forms::Severity::None:    ImGui::TextDisabled("%s", text.c_str()); break;
        }
    }
}

void FormView::setRenderer(SDL_Renderer* renderer)
{
    pictures_.setRenderer(renderer);
}

void FormView::freeImages()
{
    pictures_.clear();
    discard_ = false;
}

void FormView::reset()
{
    confirm_.cancel();
    editing_ = nullptr;
    result_.reset();
    pictures_.recheck();
}

bool FormView::cancelConfirm()
{
    if (!confirm_.armed()) {
        return false;
    }
    confirm_.cancel();
    return true;
}

void FormView::open(const std::string& form, const Forms::Params& params, bool keepParams)
{
    if (form.empty()) {
        return;
    }
    Forms::Model::Result result;
    result.ok = true;
    result.open = form;
    result.params = params;
    result.keepParams = keepParams;
    result_ = result;
}

void FormView::draw(const Forms::Form& form, Forms::Model& model)
{
    if (discard_) {
        freeImages();
    }
    // Pictures check whether their files changed after something was written
    // (a new generation, an action, a new screen), never on a timer
    if (model.generation() != generation_) {
        generation_ = model.generation();
        pictures_.recheck();
    }
    pictures_.beginFrame(2);
    drawFields(form.fields, model);
}

// Text is edited in a buffer and given to the model when the field is left,
// so Escape cancels; a live field's model gets every change
bool FormView::inputText(const Field& field, Forms::Model& model, float width)
{
    // While this field is edited ImGui works on editBuffer_, otherwise on a copy of the model's value
    bool editingThis = editing_ == &field;
    std::string shown = editingThis ? std::string() : model.get(field.bind);
    std::string& text = editingThis ? editBuffer_ : shown;
    bool changed = false;
    if (field.multiline) {
        // Enter starts a new line; clicking away or Tab keeps the text, Escape cancels
        float height = ImGui::GetTextLineHeight() * static_cast<float>(std::max(1, field.rows))
                     + ImGui::GetStyle().FramePadding.y * 2.0f;
        changed = ImGui::InputTextMultiline("##value", &text, ImVec2(width, height));
        if (text.empty() && !field.hint.empty() && !ImGui::IsItemActive()) {
            // ImGui has no hint for multi-line fields: draw it as InputTextWithHint does
            ImVec2 at = ImGui::GetItemRectMin();
            at.x += ImGui::GetStyle().FramePadding.x;
            at.y += ImGui::GetStyle().FramePadding.y;
            ImGui::GetWindowDrawList()->AddText(at, ImGui::GetColorU32(ImGuiCol_TextDisabled), field.hint.c_str());
        }
    }
    else {
        ImGui::SetNextItemWidth(width);
        changed = ImGui::InputTextWithHint("##value", field.hint.c_str(), &text);
    }
    if (ImGui::IsItemActivated()) {
        editing_ = &field;
        editOriginal_ = model.get(field.bind);
        editBuffer_ = text;   // including anything typed this frame
    }
    if (editing_ == &field && changed && field.live) {
        model.set(field.bind, editBuffer_);
    }
    if (ImGui::IsItemDeactivated() && editing_ == &field) {
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            model.set(field.bind, editBuffer_);
        }
        else if (field.live) {
            model.set(field.bind, editOriginal_);   // cancelled
        }
        editing_ = nullptr;
    }
    return changed;
}

std::optional<Forms::Model::Result> FormView::takeResult()
{
    std::optional<Forms::Model::Result> result = result_;
    result_.reset();
    if (result) {
        pictures_.recheck();   // an action ran (it may have written artwork) or a screen opens
    }
    return result;
}

void FormView::update(SDL_Window* window, Forms::Model& model)
{
    if (!browse_) {
        return;
    }
    Browse browse = *browse_;
    browse_.reset();

    std::string path = model.get(browse.bind);
    bool chosen = browse.folder
        ? FileDialog::pickFolder(window, browse.title, path)
        : FileDialog::pickFile(window, browse.title, browse.filterName, browse.pattern, path);
    if (chosen) {
        model.set(browse.bind, path);
        pictures_.recheck();
    }
}

void FormView::drawFields(const std::vector<Field>& fields, Forms::Model& model)
{
    for (const Field& field : fields) {
        ImGui::PushID(&field);
        drawField(field, model);
        ImGui::PopID();
    }
}

void FormView::label(const Field& field)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Forms::ellipsize(field.label, labelWidth() - ImGui::GetStyle().ItemSpacing.x, textWidth).c_str());
    ImGui::SameLine(labelWidth());
}

void FormView::drawNotes(const Field& field, Forms::Model& model)
{
    auto note = [&](const std::string& text, Forms::Severity severity = Forms::Severity::None) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelWidth());
        ImGui::PushTextWrapPos(0.0f);
        severityText(severity, text);
        ImGui::PopTextWrapPos();
    };

    if (!field.status.empty()) {
        std::string status = model.get(field.status);
        if (!status.empty()) {
            note(status, model.severity(field.status));
        }
    }
    if (!field.help.empty()) {
        note(field.help);
    }
    // Say where a setting comes from when an override file sets it
    if (!field.bind.empty()) {
        std::string source = model.get("info:source:" + field.bind);
        if (!source.empty()) {
            note("Set in " + source);
        }
    }
}

void FormView::drawField(const Field& field, Forms::Model& model)
{
    if (!Forms::isVisible(field, model)) {
        return;
    }
    bool enabled = Forms::isEnabled(field, model);
    ImGui::BeginDisabled(!enabled);

    switch (field.type) {
    case FieldType::Section:
        ImGui::SeparatorText(field.label.c_str());
        drawFields(field.children, model);
        break;

    case FieldType::Actions: {
        ImGui::Spacing();
        bool first = true;
        for (const Field& child : field.children) {
            if (!Forms::isVisible(child, model)) {
                continue;
            }
            if (!first) {
                ImGui::SameLine();
            }
            first = false;
            ImGui::PushID(&child);
            if (child.type == FieldType::Button) {
                drawButton(child, model, true);
            }
            else {
                drawField(child, model);
            }
            ImGui::PopID();
        }
        break;
    }

    case FieldType::Text:
        label(field);
        inputText(field, model, -FLT_MIN);
        drawNotes(field, model);
        break;

    case FieldType::Folder:
    case FieldType::File:
        drawPath(field, model, field.type == FieldType::Folder);
        break;

    case FieldType::Toggle: {
        label(field);
        bool value = Forms::isTruthy(model.get(field.bind));
        if (ImGui::Checkbox("##value", &value)) {
            model.set(field.bind, value ? "yes" : "no");
        }
        drawNotes(field, model);
        break;
    }

    case FieldType::Number: {
        label(field);
        int value = Utils::convertInt(model.get(field.bind));
        ImGui::SetNextItemWidth(140.0f * scale_);
        if (ImGui::InputInt("##value", &value)) {
            if (field.max > field.min) {
                value = std::clamp(value, field.min, field.max);
            }
            model.set(field.bind, std::to_string(value));
        }
        drawNotes(field, model);
        break;
    }

    case FieldType::Choice: {
        label(field);
        std::string value = model.get(field.bind);
        auto options = model.options(field.options);
        std::string preview = value;
        for (const auto& [optionValue, optionLabel] : options) {
            if (optionValue == value) {
                preview = optionLabel;
            }
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##value", preview.c_str())) {
            for (const auto& [optionValue, optionLabel] : options) {
                bool selected = optionValue == value;
                if (ImGui::Selectable(optionLabel.c_str(), selected)) {
                    model.set(field.bind, optionValue);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        drawNotes(field, model);
        break;
    }

    case FieldType::List: {
        std::vector<std::string> values = model.list(field.bind);
        if (values.empty()) {
            if (!field.empty.empty()) {
                ImGui::TextWrapped("%s", field.empty.c_str());
            }
            break;
        }
        // Real installs can have well over a hundred entries, so keep it compact
        std::string heading = field.label + " (" + std::to_string(values.size()) + ")";
        if (ImGui::TreeNode("##list", "%s", heading.c_str())) {
            float rows = static_cast<float>(std::min<size_t>(values.size(), 6));
            ImGui::BeginChild("##entries", ImVec2(0.0f, rows * ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_None);
            for (const std::string& value : values) {
                ImGui::TextUnformatted(value.c_str());
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
        break;
    }

    case FieldType::Note: {
        std::string text = Forms::noteText(field, model);
        if (!text.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            severityText(Forms::noteSeverity(field, model), text);
            ImGui::PopTextWrapPos();
        }
        break;
    }

    case FieldType::Button:
        drawButton(field, model, false);
        break;

    case FieldType::Results:
        drawResults(field, model);
        break;

    case FieldType::Link:
        drawLink(field, model);
        break;

    case FieldType::Value: {
        std::string value = model.get(field.bind);
        if (value.empty()) {
            break;
        }
        ImGui::TextUnformatted(Forms::ellipsize(field.label, labelWidth() - ImGui::GetStyle().ItemSpacing.x, textWidth).c_str());
        ImGui::SameLine(labelWidth());
        ImGui::PushTextWrapPos(0.0f);
        Forms::Severity severity = model.severity(field.bind);
        if (severity == Forms::Severity::None) {
            ImGui::TextUnformatted(value.c_str());
        }
        else {
            severityText(severity, value);
        }
        ImGui::PopTextWrapPos();
        if (!field.help.empty()) {
            drawNotes(field, model);
        }
        break;
    }

    case FieldType::Image:
        drawImage(field, model);
        break;
    }

    ImGui::EndDisabled();
}

void FormView::drawResults(const Field& field, Forms::Model& model)
{
    if (!field.label.empty()) {
        ImGui::SeparatorText(field.label.c_str());
    }
    Forms::Rows rows = model.rows(field.bind, static_cast<size_t>(field.limit));
    if (rows.items.empty()) {
        if (!field.empty.empty()) {
            ImGui::TextWrapped("%s", field.empty.c_str());
        }
        return;
    }
    std::string note = Forms::rowsNote(rows);
    if (!note.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", note.c_str());
        ImGui::PopTextWrapPos();
    }

    // Only the rows in view are drawn
    float visibleRows = static_cast<float>(std::min<size_t>(rows.items.size(), 12));
    ImGui::BeginChild("##results", ImVec2(0.0f, visibleRows * ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_None);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.items.size()));
    float gap = ImGui::GetStyle().ItemSpacing.x * 2.0f;
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const Forms::Row& row = rows.items[static_cast<size_t>(i)];
            ImGui::PushID(i);
            // The detail on the right and the label on the left share the row,
            // ending in "..." when they don't fit, rather than overlapping
            float available = ImGui::GetContentRegionAvail().x;
            // A row that stands for a screen to go to ends in the link marker
            std::string rowDetail = row.link ? (row.detail.empty() ? ">" : row.detail + "  >") : row.detail;
            float detailNeeds = rowDetail.empty() ? 0.0f : textWidth(rowDetail);
            Forms::RowSplit split = Forms::splitRow(textWidth(row.label), detailNeeds, available, gap);
            std::string label = Forms::ellipsize(row.label, split.label, textWidth);
            if (ImGui::Selectable((label + "##row").c_str())) {
                if (!row.action.empty()) {
                    if (model.actionProblem(row.action).empty()) {
                        result_ = model.runAction(row.action);
                    }
                }
                else {
                    open(field.open, Forms::rowParams(field, row), false);
                }
            }
            if (split.detail > 0.0f) {
                std::string detail = Forms::ellipsize(rowDetail, split.detail, textWidth);
                ImGui::SameLine(ImGui::GetContentRegionMax().x - textWidth(detail));
                severityText(row.severity, detail);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void FormView::drawLink(const Field& field, Forms::Model& model)
{
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float right = ImGui::GetContentRegionMax().x - textWidth(">");
    std::string status = field.status.empty() ? "" : model.get(field.status);
    std::string label = Forms::ellipsize(field.label, (status.empty() ? right : labelWidth()) - spacing, textWidth);
    if (ImGui::Selectable((label + "##link").c_str())) {
        open(field.open, {}, true);
    }
    // A link's help is a tooltip, so a list of links stays a list
    if (!field.help.empty() && (ImGui::IsItemHovered() || ImGui::IsItemFocused())) {
        ImGui::SetTooltip("%s", field.help.c_str());
    }
    if (!status.empty()) {
        ImGui::SameLine(labelWidth());
        severityText(model.severity(field.status), Forms::ellipsize(status, right - labelWidth() - spacing, textWidth));
    }
    ImGui::SameLine(right);
    ImGui::TextDisabled(">");
}

void FormView::drawImage(const Field& field, Forms::Model& model)
{
    std::string path = model.get(field.bind);
    if (path.empty()) {
        return;
    }
    // rows high, keeping its shape, and no wider than the window; loaded at that size
    float height = std::max(1, field.rows) * ImGui::GetTextLineHeightWithSpacing();
    float available = ImGui::GetContentRegionAvail().x - (field.label.empty() ? 0.0f : labelWidth());
    SDL_Texture* picture = pictures_.get(path, static_cast<int>(available), static_cast<int>(height), true);
    int w = 0;
    int h = 0;
    if (!picture || SDL_QueryTexture(picture, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0) {
        return;
    }
    if (!field.label.empty()) {
        ImGui::TextUnformatted(Forms::ellipsize(field.label, labelWidth() - ImGui::GetStyle().ItemSpacing.x, textWidth).c_str());
        ImGui::SameLine(labelWidth());
    }
    float width = height * static_cast<float>(w) / static_cast<float>(h);
    if (width > available && available > 0.0f) {
        height *= available / width;
        width = available;
    }
    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(picture)), ImVec2(width, height));
}

void FormView::drawPath(const Field& field, Forms::Model& model, bool folder)
{
    label(field);

    const char* browseLabel = "Browse...";
    float browseWidth = ImGui::CalcTextSize(browseLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    bool canBrowse = FileDialog::isSupported();

    inputText(field, model, canBrowse ? -(browseWidth + ImGui::GetStyle().ItemSpacing.x) : -FLT_MIN);
    if (canBrowse) {
        ImGui::SameLine();
        if (ImGui::Button(browseLabel)) {
            Browse browse;
            browse.bind = field.bind;
            browse.folder = folder;
            browse.title = field.label;
            browse.filterName = field.filterName;
            browse.pattern = field.pattern;
            browse_ = browse;
        }
    }
    drawNotes(field, model);
}

void FormView::drawButton(const Field& field, Forms::Model& model, bool inRow)
{
    std::string problem = model.actionProblem(field.action);

    if (!inRow) {
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelWidth());
    }
    // A button with confirm="" asks first, with Yes and No in its place
    if (confirm_.armed() == &field) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.confirm.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Yes") && confirm_.select(field) && problem.empty()) {
            result_ = model.runAction(field.action);
        }
        ImGui::SameLine();
        if (ImGui::Button("No")) {
            confirm_.cancel();
        }
        return;
    }

    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button(field.label.c_str()) && confirm_.select(field)) {
        result_ = model.runAction(field.action);
    }
    ImGui::EndDisabled();

    // Say what's missing next to the button
    if (!problem.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", problem.c_str());
    }
}

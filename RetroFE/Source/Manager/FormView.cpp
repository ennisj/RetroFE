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
#include "../Utility/Utils.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>

using Forms::Field;
using Forms::FieldType;

void FormView::draw(const Forms::Form& form, Forms::Model& model)
{
    drawFields(form.fields, model);
}

std::optional<Forms::Model::Result> FormView::takeResult()
{
    std::optional<Forms::Model::Result> result = result_;
    result_.reset();
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
    ImGui::TextUnformatted(field.label.c_str());
    ImGui::SameLine(labelWidth());
}

void FormView::drawNotes(const Field& field, Forms::Model& model)
{
    auto note = [&](const std::string& text) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelWidth());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::PopTextWrapPos();
    };

    if (!field.status.empty()) {
        std::string status = model.get(field.status);
        if (!status.empty()) {
            note(status);
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

    case FieldType::Text: {
        label(field);
        std::string value = model.get(field.bind);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##value", field.hint.c_str(), &value)) {
            model.set(field.bind, value);
        }
        drawNotes(field, model);
        break;
    }

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
            ImGui::TextDisabled("%s", text.c_str());
            ImGui::PopTextWrapPos();
        }
        break;
    }

    case FieldType::Button:
        drawButton(field, model, false);
        break;
    }

    ImGui::EndDisabled();
}

void FormView::drawPath(const Field& field, Forms::Model& model, bool folder)
{
    label(field);

    const char* browseLabel = "Browse...";
    float browseWidth = ImGui::CalcTextSize(browseLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    bool canBrowse = FileDialog::isSupported();

    std::string value = model.get(field.bind);
    ImGui::SetNextItemWidth(canBrowse ? -(browseWidth + ImGui::GetStyle().ItemSpacing.x) : -FLT_MIN);
    if (ImGui::InputTextWithHint("##value", field.hint.c_str(), &value)) {
        model.set(field.bind, value);
    }
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
    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button(field.label.c_str())) {
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

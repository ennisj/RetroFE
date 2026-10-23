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
#pragma once

#include "../Forms/Form.h"
#include <SDL2/SDL.h>
#include <map>
#include <optional>
#include <string>

// Draws any form with Dear ImGui. Call draw() between ImGui::Begin and End.
class FormView
{
public:
    ~FormView();

    void setScale(float scale) { scale_ = scale; }

    // Where <image> fields are loaded; they're drawn only once this is set
    void setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }

    // Frees the images loaded so far, before the renderer goes
    void freeImages();

    // The same at the start of the next draw(), for when the screen changes
    // during a frame whose draw data still uses them
    void discardImages() { discard_ = true; }

    void draw(const Forms::Form& form, Forms::Model& model);

    // Runs a requested file or folder picker. They block, so call this
    // outside rendering.
    void update(SDL_Window* window, Forms::Model& model);

    // The result of the action run since the last call, or a link or result
    // asking to open a form (Result::open), if any
    std::optional<Forms::Model::Result> takeResult();

private:
    struct Browse
    {
        std::string bind;
        bool folder = false;
        std::string title;
        std::string filterName;
        std::string pattern;
    };

    void drawFields(const std::vector<Forms::Field>& fields, Forms::Model& model);
    void drawField(const Forms::Field& field, Forms::Model& model);
    void drawPath(const Forms::Field& field, Forms::Model& model, bool folder);
    void drawButton(const Forms::Field& field, Forms::Model& model, bool inRow);
    void drawResults(const Forms::Field& field, Forms::Model& model);
    void drawLink(const Forms::Field& field, Forms::Model& model);
    void drawImage(const Forms::Field& field, Forms::Model& model);
    void drawNotes(const Forms::Field& field, Forms::Model& model);
    void label(const Forms::Field& field);
    void open(const std::string& form, const Forms::Params& params);
    SDL_Texture* texture(const std::string& path);
    float labelWidth() const { return 170.0f * scale_; }

    float scale_ = 1.0f;
    SDL_Renderer* renderer_ = nullptr;
    std::map<std::string, SDL_Texture*> textures_;   // by path; nullptr if it can't be loaded
    bool discard_ = false;
    std::optional<Browse> browse_;
    std::optional<Forms::Model::Result> result_;
};

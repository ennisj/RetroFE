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

#include "Component.h"
#include "WidgetPainter.h"
#include "../../Forms/Form.h"
#include <SDL2/SDL.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class FontManager;
class Image;

// The <form> layout element: draws a Forms::Form as rows the user moves
// through with RetroFE's controls. Its look comes from the layout:
//
//   <form x="240" y="80" width="800" height="560" fontSize="28" fontColor="dedede"
//         focusColor="ffffff" rowHeight="44" labelWidth="300"
//         focusBackgroundColor="ffffff" focusBackgroundAlpha="0.12" focusImage="images/row.png"
//         backgroundColor="000000" backgroundAlpha="0.85" onText="On" offText="Off"
//         rowDetailColor="9fb4d0" rowDetailAlpha="0.8" linkMarker=">"/>
//
// Up/Down move between fields; Left/Right change a toggle, number or choice,
// or page through a results list; Select edits text, picks a folder or file,
// presses a button, or opens a link or result; Back leaves.
class FormWidget : public Component
{
public:
    enum class Nav { Up, Down, Left, Right, Select, Back };

    struct Style
    {
        float rowHeight = 0.0f;             // 0: 1.6 x the font size
        float labelWidth = 0.0f;            // 0: 40% of the width
        SDL_Color focusBackground{ 255, 255, 255, 255 };
        float focusBackgroundAlpha = 0.12f;
        std::string focusImage;             // absolute path; drawn behind the focused row
        float noteAlpha = 0.55f;
        float disabledAlpha = 0.35f;
        float rowDetailAlpha = 0.7f;        // a result's detail, on the right
        std::string linkMarker = ">";       // at the right of a link
        std::string onText = "On";
        std::string offText = "Off";
        bool showTitle = true;
        bool showHints = true;
    };

    // A request for a native file or folder picker
    struct Browse
    {
        std::string bind;
        bool folder = false;
        std::string title;
        std::string filterName;
        std::string pattern;
    };

    FormWidget(Page& p, FontManager* font, FontManager* focusFont, int monitor);
    ~FormWidget() override;

    void setStyle(const Style& style) { style_ = style; }

    // The font for results' details (rowDetailColor); the normal font when null
    void setDetailFont(FontManager* font) { detailFont_ = font; }

    void bind(const Forms::Form* form, Forms::Model* model);

    // Where the user is, and putting them back there after returning to the form
    Forms::Focus focus() const;
    void restoreFocus(const Forms::Focus& focus);

    // Whether Select on a folder or file field asks for a native picker
    // (takeBrowse) rather than editing the path as text
    void setCanBrowse(bool canBrowse) { canBrowse_ = canBrowse; }

    // Whether Back closes the manager or returns to the previous screen, for the hints
    void setBackCloses(bool closes) { backCloses_ = closes; }

    // Returns false when Back was pressed outside a text field, i.e. the screen should close
    bool navigate(Nav nav);

    // Keyboard typing while a text field is being edited; returns true if used
    bool handleTextEvent(const SDL_Event& e);

    // The text field being edited, for an on-screen keyboard to type into
    bool isEditing() const { return editing_; }
    const Forms::Field* editingField() const { return editing_ ? focusedField_ : nullptr; }
    std::string& editText() { return editBuffer_; }
    void finishEditing(bool keep);

    void setStatus(const std::string& message, bool isError);
    std::optional<Browse> takeBrowse();

    // An action's result, or a link or result row asking to open a form
    // (Result::open with the parameters it adds)
    std::optional<Forms::Model::Result> takeResult();

    void allocateGraphicsMemory() override;
    void freeGraphicsMemory() override;
    void draw() override;

private:
    enum class RowKind
    {
        Title, Header, Field, Note, Status,
        Item,     // a row of a Results field
        Value,    // a Value field, or a further line of its value
        Image,    // an Image field, spanning `span` rows
        Spacer    // the rest of an image's rows
    };
    struct Row
    {
        RowKind kind = RowKind::Note;
        const Forms::Field* field = nullptr;
        std::string text;
        std::string item;       // Item: the result's id
        std::string detail;     // Item: shown on the right
        std::string image;      // Item: thumbnail; Image: the picture
        int span = 1;           // Image: rows it covers
        bool thumbnail = false; // Item: leave room for thumbnails in this list
        bool focusable = false;
        bool enabled = true;
        bool indent = false;    // notes under a field line up with its value
        bool isError = false;
    };

    void buildRows();
    void addFields(const std::vector<Forms::Field>& fields);
    void addResults(const Forms::Field& field);
    void addValue(const Forms::Field& field);
    void addImage(const Forms::Field& field);
    void addWrapped(RowKind kind, const std::string& text, bool indent, bool isError = false);
    void keepFocusVisible(float visibleHeight);
    void moveFocus(int direction);
    void pageResults(int direction);
    void activate(const Row& row);
    void change(const Forms::Field& field, int direction);
    void startEditing(const Forms::Field& field);
    void sendLiveText();
    std::string valueText(const Forms::Field& field, bool focused);
    std::string hintText() const;
    float contentWidth() const;
    float rowHeight() const;
    float labelWidth() const;
    float fontSize() const;

    // Images by path, loaded when first drawn; nullptr if it can't be loaded
    SDL_Texture* texture(const std::string& path);
    void freeTextures();
    void drawPicture(SDL_Texture* texture, float x, float y, float maxWidth, float maxHeight);

    WidgetPainter painter_;
    Style style_;
    FontManager* detailFont_ = nullptr;
    const Forms::Form* form_ = nullptr;
    Forms::Model* model_ = nullptr;

    std::vector<Row> rows_;
    int focus_ = -1;              // index into rows_
    const Forms::Field* focusedField_ = nullptr;  // kept across rebuilds
    std::string focusedItem_;     // and in a results list, the row's id
    float scroll_ = 0.0f;
    int pageRows_ = 10;           // rows that fit, for paging through results

    bool canBrowse_ = false;
    bool backCloses_ = true;
    bool editing_ = false;
    bool justFinishedEditing_ = false;  // the Enter/Esc that ended editing isn't also Select/Back
    bool textInputWasActive_ = false;
    std::string editBuffer_;
    std::string editOriginal_;    // live fields: what to put back if the edit is cancelled
    std::string liveSent_;        // live fields: what the model was last given

    std::string status_;
    bool statusIsError_ = false;
    std::optional<Browse> browse_;
    std::optional<Forms::Model::Result> result_;

    std::unique_ptr<Image> focusImage_;
    std::map<std::string, SDL_Texture*> textures_;
};

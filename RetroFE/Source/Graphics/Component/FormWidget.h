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
#include "TextureCache.h"
#include "WidgetPainter.h"
#include "../../Forms/Form.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
//         rowDetailColor="9fb4d0" rowDetailAlpha="0.8" linkMarker=">"
//         headerColor="75c8f7" separatorColor="75c8f7" separatorAlpha="0.2"
//         imageBackgroundColor="ffffff" imageBackgroundAlpha="0.06"
//         statusColor="9fe3a6" infoColor="b8c4d0" errorColor="ff9a8a" confirmBackgroundColor="d9442f" confirmBackgroundAlpha="0.3"/>
//
// Text too long for its column ends in "..." (see WidgetPainter).
//
// Up/Down move between fields; Left/Right change a toggle, number or choice,
// or page through a results list; Select edits text, picks a folder or file,
// presses a button, or opens a link or result; Back leaves.
//
// A multi-line text field (<text multiline="yes" rows="N">) shows N wrapped
// lines. While it's edited on a real keyboard, Enter starts a new line,
// Ctrl+Enter keeps the text, Escape cancels, and the arrows, Home, End, Page
// Up/Down and Delete move and delete in the text; the on-screen keyboard has
// keys for a new line and the cursor instead.
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
        float rowDetailAlpha = 0.7f;        // a result's detail, and a link's status and marker
        std::string linkMarker = ">";       // at the right of a link
        float headerAlpha = 0.7f;           // section headers (headerColor)
        SDL_Color separator{ 255, 255, 255, 255 };
        float separatorAlpha = 0.0f;        // a line under headers and between results; 0: none
        SDL_Color imageBackground{ 0, 0, 0, 255 };
        float imageBackgroundAlpha = 0.0f;  // behind pictures and thumbnails; 0: none
        SDL_Color confirmBackground{ 255, 255, 255, 255 };
        float confirmBackgroundAlpha = 0.0f; // over a button waiting for confirmation; 0: none
        std::string onText = "On";
        std::string offText = "Off";
        bool showTitle = true;
        bool showHints = true;
        // titleDetail="info:pendingChanges": a model binding drawn at the
        // right of the title row in infoColor, e.g. "2 changes apply when you
        // close Manage"; nothing when empty (the default) or without a title
        std::string titleDetail;
    };

    // Fonts of the same face in other colors; null means the normal font
    enum class FontRole
    {
        Detail,   // rowDetailColor: results' details, links' status and marker
        Header,   // headerColor: section headers
        Status,   // statusColor: the status line after an action that did something
        Info,     // infoColor: the status line when nothing happened (cancelled, undone), and prompts
        Error     // errorColor: errors, and the question of a button to confirm
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

    void setFont(FontRole role, FontManager* font);

    void bind(const Forms::Form* form, Forms::Model* model);

    // Where the user is, and putting them back there after returning to the form
    Forms::Focus focus() const;
    void restoreFocus(const Forms::Focus& focus);

    // Focuses the field key names (Forms::findField), now or as soon as it's
    // shown, unless the user moves first (Result::focus)
    void focusOn(const std::string& key);

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
    Forms::TextEdit& editText() { return edit_; }
    void finishEditing(bool keep);

    // A multi-line field being edited: the cursor up (-1) or down (+1) a line as drawn
    void moveEditLine(int direction);

    // A live field's model gets the text being edited; call after the
    // on-screen keyboard changed it (typed text does this itself)
    void sendLiveText();

    // The screen is rebuilt from the model on input, when the model's
    // generation changes, and a few times a second; this rebuilds it on the
    // next draw, e.g. after setting a value from outside. Pictures check
    // whether their files changed after this, a new screen, an action, or a
    // new generation, and at no other time.
    void invalidate() { stale_ = true; pictures_.recheck(); }

    // Shown under the form until the next action, e.g. a result or a warning
    void setStatus(const std::string& message, Forms::StatusKind kind);
    std::optional<Browse> takeBrowse();

    // An action's result, or a link or result row asking to open a form
    // (Result::open with its parameters)
    std::optional<Forms::Model::Result> takeResult();

    void allocateGraphicsMemory() override;
    void freeGraphicsMemory() override;
    void draw() override;

private:
    enum class RowKind
    {
        Title, Header, Field, Note,
        Item,     // a row of a Results field
        Value,    // a Value field, or a further line of its value
        Image,    // an Image field, spanning `span` rows
        Spacer,   // the rest of an image's rows
        TextLine  // a further line of a multi-line text field (`line` of it)
    };
    struct Row
    {
        RowKind kind = RowKind::Note;
        const Forms::Field* field = nullptr;
        std::string text;
        std::string value;      // Field: what it shows, read when the rows were built
        std::string item;       // Item: Forms::rowKey of the result
        std::string detail;     // Item: shown on the right
        std::string image;      // Item: thumbnail; Image: the picture
        Forms::Params params;   // Item: what choosing it opens its form with
        int span = 1;           // Image: rows it covers
        int line = 0;           // Field, TextLine of a multi-line text: which of its rows
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
    const std::vector<std::string>& wrap(const std::string& text, float width);
    const std::vector<Forms::TextLine>& layout(const std::string& text, float width);
    bool editingMultiline() const;
    float multilineWidth(const Forms::Field& field) const;
    void drawTextLine(const Row& row, float x, float textY, bool highlighted, float alpha);
    void keepFocusVisible(float visibleHeight);
    void moveFocus(int direction);
    void pageResults(int direction);
    void activate(const Row& row);
    void change(const Forms::Field& field, int direction);
    void startEditing(const Forms::Field& field);
    std::string valueText(const Forms::Field& field);
    std::string hintText() const;
    float contentWidth() const;
    float rowHeight() const;
    float labelWidth() const;
    float fontSize() const;
    float pixelScale() const;   // screen pixels per layout unit, for sizing pictures

    void drawPicture(SDL_Texture* texture, float x, float y, float maxWidth, float maxHeight, bool centered, bool backed,
                     float clipTop = -1e9f, float clipBottom = 1e9f);

    WidgetPainter painter_;
    Style style_;
    FontManager* detailFont_ = nullptr;
    FontManager* headerFont_ = nullptr;
    FontManager* statusFont_ = nullptr;
    FontManager* infoFont_ = nullptr;
    FontManager* errorFont_ = nullptr;
    const Forms::Form* form_ = nullptr;
    Forms::Model* model_ = nullptr;

    std::vector<Row> rows_;
    std::vector<std::string> statusLines_;   // status_, wrapped; drawn above the hints
    bool stale_ = true;           // rows_ need building again
    uint32_t builtAt_ = 0;        // SDL ticks
    unsigned generation_ = 0;     // the model's, when built
    std::unordered_map<std::string, std::vector<std::string>> wrapped_;   // by width and text
    std::unordered_map<std::string, std::vector<Forms::TextLine>> layouts_;   // multi-line fields', likewise
    int focus_ = -1;              // index into rows_
    bool focusGuarded_ = false;   // focus landed on a Guarded row (Forms::refocusTarget) because rows
                                  // went away: Select there waits for the user to move first
    std::vector<const Forms::Field*> shownFields_;   // focusable fields of the last build, to tell new ones
    std::string wantedFocus_;     // Result::focus not shown yet: focused when it is, until the user moves
    const Forms::Field* focusedField_ = nullptr;  // kept across rebuilds
    std::string focusedItem_;     // and in a results list, the row's key
    float scroll_ = 0.0f;
    int pageRows_ = 10;           // rows that fit, for paging through results
    Forms::Confirm confirm_;

    bool canBrowse_ = false;
    bool backCloses_ = true;
    bool editing_ = false;
    bool justFinishedEditing_ = false;  // the Enter/Esc that ended editing isn't also Select/Back
    bool textInputWasActive_ = false;
    Forms::TextEdit edit_;
    int editTopLine_ = 0;         // a multi-line field being edited: the first line shown
    std::string editOriginal_;    // live fields: what to put back if the edit is cancelled
    std::string liveSent_;        // live fields: what the model was last given

    std::string status_;
    Forms::StatusKind statusKind_ = Forms::StatusKind::Success;
    std::optional<Browse> browse_;
    std::optional<Forms::Model::Result> result_;

    std::unique_ptr<Image> focusImage_;
    TextureCache pictures_;
};

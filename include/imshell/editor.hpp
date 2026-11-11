#pragma once

#include "imgui.h"
#include "imshell/themes.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// an optional multi-line code editor (target imshell::editor): editing, selection, undo, line
// numbers, find and replace, a read-only mode, and hooks for what the app knows: highlighting
// and completion. nothing in imshell's core uses it, so an app that does not link the target
// pays nothing
namespace imshell {
    class TextEditor {
    public:
        // --- text ---
        // replaces the text; clears the selection, the undo history and the dirty flag
        void SetText(const std::string& text);
        [[nodiscard]] const std::string& GetText() const {
            return content_;
        }
        [[nodiscard]] bool HasSelection() const {
            return selectionActive_ && selectionAnchor_ != cursorIndex_;
        }
        [[nodiscard]] std::string GetSelectedText() const;
        [[nodiscard]] int GetCursorIndex() const { // a byte offset
            return cursorIndex_;
        }
        // set on every user edit, cleared by SetText or ClearContentDirty
        [[nodiscard]] bool IsContentDirty() const {
            return contentDirty_;
        }
        void ClearContentDirty() {
            contentDirty_ = false;
        }

        // --- drawing ---
        // call inside a window, once a frame; size as for BeginChild
        void Render(const char* label, ImVec2 size, bool border = false);
        void SetFocus(); // takes the keyboard on the next Render

        // --- options ---
        void SetPlaceholder(const std::string& text) { // shown while the text is empty
            placeholder_ = text;
        }
        void SetShowLineNumbers(bool show) {
            showLineNumbers_ = show;
        }
        void SetTabSize(int size) {
            tabSize_ = size;
        }
        // read-only still selects, copies and moves the cursor
        void SetReadOnly(bool readOnly) {
            readOnly_ = readOnly;
        }
        // ctrl+enter (cmd on macos)
        void SetSubmitCallback(std::function<void()> cb) {
            submitCallback_ = std::move(cb);
        }
        // ctrl+/ toggles this prefix on the selected lines ("//", "--", "#"); empty: off
        void SetLineComment(std::string prefix) {
            lineComment_ = std::move(prefix);
        }
        // enter after one of these words (any case) indents the next line, as after a bracket
        void SetIndentWords(std::vector<std::string> words) {
            indentWords_ = std::move(words);
        }

        // --- highlighting ---
        // what a byte of the text is; the palette gives each its colour
        enum class Token : uint8_t {
            Text,
            Keyword,
            String,
            Number,
            Comment,
            Function,
            Type,
            Operator
        };
        // what changed since the last call: text[start, newEnd) replaced what was [start, oldEnd).
        // the first call (and the first after SetHighlighter) has the whole text as new
        struct Edit {
            size_t start = 0;
            size_t oldEnd = 0;
            size_t newEnd = 0;
        };
        // the app's lexer or parser: fills tokens[i] for byte i of text. on entry the edited bytes
        // are Text and the rest keep the previous call's kinds, moved with the edit, so an
        // incremental parser recolours only what changed; a lexer that redoes everything sets every
        // byte. runs when the text changes, before the next draw; without one the text is plain
        using Highlighter =
            std::function<void(std::string_view text, std::span<Token> tokens, const Edit& edit)>;
        void SetHighlighter(Highlighter highlighter);
        // the highlight, brought up to date (tests)
        const std::vector<Token>& GetTokens();
        [[nodiscard]] ImU32 TokenColor(Token token) const;

        // --- completion ---
        struct CompletionItem {
            std::string text;       // the row's label
            std::string insertText; // empty: text
            std::string detailText; // dimmed, right-aligned
            std::string icon;       // a letter or an icon font glyph in front of the label
            Token iconColor = Token::Keyword;
        };
        // the items and the byte range of the text that the picked one replaces
        struct CompletionResponse {
            std::vector<CompletionItem> items;
            int replaceStart = 0;
            int replaceEnd = 0;
        };
        // asked after a typed word character or '.', and on ctrl+space or ctrl+. (forced).
        // no items: no popup. the editor draws the popup and handles its keys and clicks
        using CompletionProvider = std::function<CompletionResponse(std::string_view content,
                                                                    int cursorIndex, bool forced)>;
        void SetCompletionProvider(CompletionProvider provider) {
            completionProvider_ = std::move(provider);
        }
        // where suggestion row i was drawn (tests click it); nothing visible: (0,0)
        [[nodiscard]] ImVec2 AutocompleteRowPos(int i) const {
            return autocompleteVisible_ ? ImVec2(autocompleteMin_.x + 40.0f,
                                                 autocompleteMin_.y + autocompleteRow_ * (i + 0.5f))
                                        : ImVec2();
        }

        // --- gutter and markers ---
        // runnable ranges: a play icon in the gutter on each range's first line; hovering
        // outlines the range, clicking hands its byte offsets to onRun
        using RangeProvider =
            std::function<std::vector<std::pair<size_t, size_t>>(std::string_view content)>;
        using RunCallback = std::function<void(size_t begin, size_t end)>;
        void SetRunnableRanges(RangeProvider provider, RunCallback onRun);
        // a wavy underline under the token at a byte offset, the message on hover
        void SetErrorMarker(size_t offset, std::string message);
        void ClearErrorMarker() {
            errorOffset_ = -1;
        }
        [[nodiscard]] int GetErrorOffset() const {
            return errorOffset_; // -1: none
        }

        // --- colours ---
        struct Palette {
            ImU32 background;
            ImU32 text;
            ImU32 keyword;
            ImU32 string;
            ImU32 number;
            ImU32 comment;
            ImU32 function;
            ImU32 type;
            ImU32 operator_;
            ImU32 cursor;
            ImU32 selection;
            ImU32 lineNumber;
            ImU32 currentLineNumber;
            ImU32 currentLineBackground;
            ImU32 lineNumberBackground;
            ImU32 run;
            ImU32 error;
            bool operator==(const Palette&) const = default;
        };
        static Palette FromTheme(const Theme::Colors& colors);
        // without one the editor follows Theme::Current()
        void SetPalette(const Palette& palette) {
            palette_ = palette;
            ownPalette_ = true;
        }

    private:
        std::string content_;
        std::vector<Token> tokens_;
        std::vector<int> lineStarts_{0};

        int cursorIndex_ = 0;
        int preferredColumn_ = 0;
        float cursorBlinkTimer_ = 0.0f;

        int selectionAnchor_ = 0;
        bool selectionActive_ = false;
        bool isDragging_ = false;

        float scrollX_ = 0.0f;
        float scrollY_ = 0.0f;
        bool ensureCursorVisibleV_ = false;
        bool ensureCursorVisibleH_ = false;

        float lineHeight_ = 0.0f;
        float lineNumberWidth_ = 0.0f;
        ImVec2 textOrigin_;
        float cachedContentWidth_ = 0.0f;
        bool contentWidthDirty_ = true;
        bool contentDirty_ = false;

        bool showLineNumbers_ = true;
        int tabSize_ = 4;
        bool readOnly_ = false;
        bool focusRequested_ = false;
        std::string placeholder_;
        std::string lineComment_;
        std::vector<std::string> indentWords_;
        std::function<void()> submitCallback_;

        Palette palette_{};
        bool ownPalette_ = false;
        Highlighter highlighter_;
        std::string highlighted_; // the text tokens_ was last highlighted for
        bool highlightDirty_ = true;

        struct Snapshot {
            std::string content;
            int cursorIndex;
        };
        std::vector<Snapshot> undoStack_;
        std::vector<Snapshot> redoStack_;
        std::string lastSnapshotContent_;

        RangeProvider rangeProvider_;
        RunCallback onRun_;
        std::vector<std::pair<size_t, size_t>> runRanges_;
        bool runRangesDirty_ = true;
        double runRangesEditTime_ = 0.0;
        int hoveredRun_ = -1; // last frame's, so the click is not also a text click
        ImVec2 textAreaMin_{}, textAreaMax_{};
        int errorOffset_ = -1;
        std::string errorMessage_;

        CompletionProvider completionProvider_;
        std::vector<CompletionItem> completions_;
        int autocompleteReplaceStart_ = 0;
        int autocompleteReplaceEnd_ = 0;
        bool autocompleteVisible_ = false;
        int autocompleteIndex_ = 0;
        int autocompleteScrollOffset_ = 0;
        ImVec2 autocompleteMin_, autocompleteMax_; // last drawn popup, for clicks
        float autocompleteRow_ = 0.0f;

        bool findVisible_ = false;
        bool findFocusRequested_ = false;
        char findBuf_[256] = {};
        char replaceBuf_[256] = {};
        bool findCaseSensitive_ = false;
        bool findWholeWord_ = false;
        std::vector<int> findMatches_;
        int findCurrentMatch_ = -1;

        // content
        void edited(); // after every change to content_
        int getLineFromPos(int pos) const;
        int getColumnFromPos(int pos) const;
        int getPosFromLineColumn(int line, int column) const;
        int getLineEnd(int line) const;
        void rehighlight();

        // input
        void handleKeyboardInput();
        void handleCharacterInput();
        void handleEnterKey();
        void handleBackspaceKey();
        void handleDeleteKey();
        void handleTabKey();
        void handleArrowKeys();
        void handleHomeEnd();
        void handleSelectAll();
        void handleClipboard();
        void handleUndoRedo();
        void toggleLineComment();
        void handleMouseInput();
        int getCharIndexFromScreenPos(ImVec2 screenPos) const;
        void handleDoubleClick(int charIndex);

        int selectionStart() const;
        int selectionEnd() const;
        void deleteSelection();
        void clearSelection();
        void selectRange(int from, int to); // and scroll to it

        void ensureCursorVisible();
        void insertTextAtCursor(const std::string& text);
        void pushUndoSnapshot();
        void restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to);
        int findWordStart(int pos) const;

        // drawing
        void renderLineNumbers(ImDrawList* drawList, int startLine, int endLine);
        void renderRunButtons(int startLine, int endLine);
        void renderErrorMarker(ImDrawList* drawList);
        void renderText(ImDrawList* drawList, int startLine, int endLine);
        void renderCursor(ImDrawList* drawList);
        void renderSelection(ImDrawList* drawList, int startLine, int endLine);
        void renderCurrentLineHighlight(ImDrawList* drawList);
        float xAt(int lineStart, int pos) const; // screen x of pos on the line starting there
        float getScreenX(int pos) const;         // on pos's own line

        void updateAutoComplete(bool forced = false);
        void renderAutoComplete();
        void applyAutoComplete();
        void dismissAutoComplete();

        void openFind();
        void closeFind();
        void performFind();
        void findStep(int by);
        void replaceOne();
        void replaceAll();
        void renderFindReplace();
        void renderFindHighlights(ImDrawList* drawList, int startLine, int endLine);
    };
} // namespace imshell

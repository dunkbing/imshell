#include "imshell/editor.hpp"

#include "imgui_internal.h"
#include "imshell/button.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace imshell {
    namespace {
        constexpr int kAutocompleteMaxVisible = 8;
        constexpr size_t kMaxCompletions = 50;

        ImU32 toImU32(const ImVec4& c, int alpha = 255) {
            return IM_COL32(static_cast<int>(c.x * 255), static_cast<int>(c.y * 255),
                            static_cast<int>(c.z * 255), alpha);
        }
        ImU32 withAlpha(ImU32 color, int alpha) {
            return (color & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);
        }
        bool isWordChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }
        bool isContinuation(char c) { // of a utf-8 sequence
            return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
        }
        std::string lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return s;
        }
    } // namespace

    // --- text ---

    void TextEditor::SetText(const std::string& text) {
        content_ = text;
        cursorIndex_ = static_cast<int>(text.size());
        clearSelection();
        edited();
        contentDirty_ = false; // a programmatic set is not a user edit
        undoStack_.clear();
        redoStack_.clear();
        lastSnapshotContent_ = content_;
    }

    std::string TextEditor::GetSelectedText() const {
        return HasSelection() ? content_.substr(selectionStart(), selectionEnd() - selectionStart())
                              : "";
    }

    void TextEditor::SetFocus() {
        focusRequested_ = true;
    }

    void TextEditor::SetHighlighter(Highlighter highlighter) {
        highlighter_ = std::move(highlighter);
        highlighted_.clear(); // the next call gets the whole text as new
        tokens_.clear();
        highlightDirty_ = true;
    }

    const std::vector<TextEditor::Token>& TextEditor::GetTokens() {
        if (highlightDirty_)
            rehighlight();
        return tokens_;
    }

    void TextEditor::rehighlight() {
        // the edit since the last highlight: what the old and new text share at both ends
        Edit edit{0, highlighted_.size(), content_.size()};
        while (edit.start < edit.oldEnd && edit.start < edit.newEnd &&
               highlighted_[edit.start] == content_[edit.start])
            ++edit.start;
        while (edit.oldEnd > edit.start && edit.newEnd > edit.start &&
               highlighted_[edit.oldEnd - 1] == content_[edit.newEnd - 1]) {
            --edit.oldEnd;
            --edit.newEnd;
        }
        // keep the kinds outside the edit, moved with it; the edited bytes start as Text
        tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(edit.start),
                      tokens_.begin() + static_cast<std::ptrdiff_t>(edit.oldEnd));
        tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(edit.start),
                       edit.newEnd - edit.start, Token::Text);
        highlighted_ = content_;
        if (highlighter_ && !content_.empty())
            highlighter_(content_, tokens_, edit);
        highlightDirty_ = false;
    }

    ImU32 TextEditor::TokenColor(Token token) const {
        const Palette p = ownPalette_ ? palette_ : FromTheme(theme());
        switch (token) {
        case Token::Keyword:
            return p.keyword;
        case Token::String:
            return p.string;
        case Token::Number:
            return p.number;
        case Token::Comment:
            return p.comment;
        case Token::Function:
            return p.function;
        case Token::Type:
            return p.type;
        case Token::Operator:
            return p.operator_;
        case Token::Text:
            break;
        }
        return p.text;
    }

    void TextEditor::SetRunnableRanges(RangeProvider provider, RunCallback onRun) {
        rangeProvider_ = std::move(provider);
        onRun_ = std::move(onRun);
        runRangesDirty_ = true;
    }

    void TextEditor::SetErrorMarker(size_t offset, std::string message) {
        errorOffset_ = static_cast<int>(std::min(offset, content_.size()));
        errorMessage_ = std::move(message);
    }

    TextEditor::Palette TextEditor::FromTheme(const Theme& t) {
        Palette p{};
        p.background = toImU32(t.background);
        p.text = toImU32(t.foreground);
        p.keyword = toImU32(t.syntax.keyword);
        p.string = toImU32(t.syntax.string);
        p.number = toImU32(t.syntax.number);
        p.comment = toImU32(t.syntax.comment);
        p.function = toImU32(t.syntax.function);
        p.type = toImU32(t.syntax.type);
        p.operator_ = toImU32(t.syntax.operator_);
        p.cursor = toImU32(t.foreground);
        p.selection = toImU32(t.primary, 90); // a grey surface vanishes on the background
        p.lineNumber = toImU32(t.syntax.comment);
        p.currentLineNumber = toImU32(t.foreground);
        p.currentLineBackground = toImU32(t.muted);
        p.lineNumberBackground = toImU32(t.background);
        p.run = toImU32(t.success);
        p.error = toImU32(t.destructive);
        return p;
    }

    // --- content ---

    void TextEditor::edited() {
        lineStarts_.clear();
        lineStarts_.push_back(0);
        for (size_t i = 0; i < content_.size(); ++i) {
            if (content_[i] == '\n')
                lineStarts_.push_back(static_cast<int>(i + 1));
        }
        contentWidthDirty_ = true;
        contentDirty_ = true;
        highlightDirty_ = true;
    }

    int TextEditor::getLineFromPos(int pos) const {
        auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), pos);
        return static_cast<int>(std::distance(lineStarts_.begin(), it)) - 1;
    }

    int TextEditor::getColumnFromPos(int pos) const {
        return pos - lineStarts_[getLineFromPos(pos)];
    }

    int TextEditor::getPosFromLineColumn(int line, int column) const {
        if (line < 0)
            return 0;
        if (line >= static_cast<int>(lineStarts_.size()))
            return static_cast<int>(content_.size());
        return std::min(lineStarts_[line] + column, getLineEnd(line));
    }

    int TextEditor::getLineEnd(int line) const {
        if (line + 1 < static_cast<int>(lineStarts_.size()))
            return lineStarts_[line + 1] - 1; // before the \n
        return static_cast<int>(content_.size());
    }

    // --- drawing ---

    void TextEditor::Render(const char* label, ImVec2 size, bool border) {
        ImGui::PushID(label);
        if (!ownPalette_)
            palette_ = FromTheme(theme());

        lineHeight_ = ImGui::GetTextLineHeightWithSpacing();

        if (showLineNumbers_) {
            const int maxLine = static_cast<int>(lineStarts_.size());
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", readOnly_ ? maxLine : std::max(maxLine, 999));
            lineNumberWidth_ = ImGui::CalcTextSize(buf).x + 14.0f;
            if (rangeProvider_)
                lineNumberWidth_ += ImGui::GetFontSize() + 6.0f; // the play icons
        } else {
            lineNumberWidth_ = 0.0f;
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, palette_.background);
        ImGui::BeginChild("##editor_outer", size, border ? ImGuiChildFlags_Borders : 0);

        if (focusRequested_) {
            ImGui::SetWindowFocus();
            focusRequested_ = false;
        }
        // a click anywhere in the editor focuses it
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            ImGui::SetWindowFocus();

        // this editor only: several can share a root window
        const bool isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        cursorBlinkTimer_ += ImGui::GetIO().DeltaTime;

        // not while an imgui text field (the find bar) is active: it reads the same char queue.
        // io.WantTextInput cannot tell: this editor sets it too, for the ime
        const ImGuiContext& g = *ImGui::GetCurrentContext();
        const bool fieldActive = g.ActiveId != 0 && g.ActiveId == g.InputTextState.ID;
        if (isFocused && !fieldActive)
            handleKeyboardInput();
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
            handleMouseInput();
        } else if (autocompleteVisible_) {
            handleMouseInput(); // the popup can hang past the editor
        }

        // every edit marks the highlight dirty: the ranges may have moved too
        if (rangeProvider_ && (highlightDirty_ || runRangesDirty_)) {
            // big texts: no icons while typing, one split after a pause, so a stale range is
            // never clickable
            const double now = ImGui::GetTime();
            if (highlightDirty_)
                runRangesEditTime_ = now;
            if (content_.size() < 256 * 1024 || now - runRangesEditTime_ > 0.3) {
                runRanges_ = rangeProvider_(content_);
                runRangesDirty_ = false;
            } else {
                runRanges_.clear();
                runRangesDirty_ = true;
            }
        }

        if (highlightDirty_) // after the input: the tokens match the text that is drawn
            rehighlight();

        if (showLineNumbers_) {
            // space only: numbers and play icons are drawn after the text area, which knows
            // the scroll. not a child window, which would sit over the icons
            ImGui::Dummy(ImVec2(lineNumberWidth_, ImGui::GetContentRegionAvail().y));
            ImGui::SameLine(0, 0);
        }

        const float textAreaWidth = ImGui::GetContentRegionAvail().x;
        const float spaceWidth = ImGui::CalcTextSize(" ").x;

        // the widest line, only when the content changed
        if (contentWidthDirty_) {
            float maxW = 0.0f;
            for (size_t i = 0; i < lineStarts_.size(); ++i) {
                float w = 0;
                for (int j = lineStarts_[i], end = getLineEnd(static_cast<int>(i)); j < end; ++j)
                    w += content_[j] == '\t'
                             ? spaceWidth * tabSize_
                             : ImGui::CalcTextSize(&content_[j], &content_[j] + 1).x;
                maxW = std::max(maxW, w);
            }
            cachedContentWidth_ = maxW + spaceWidth * 10;
            contentWidthDirty_ = false;
        }
        // read-only: no spare line to type into
        const float contentHeight =
            lineStarts_.size() * lineHeight_ + (readOnly_ ? 4.0f : lineHeight_);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowContentSize(ImVec2(cachedContentWidth_, contentHeight));
        ImGui::BeginChild("##text_area", ImVec2(textAreaWidth, -1), 0,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);

        scrollX_ = ImGui::GetScrollX();
        scrollY_ = ImGui::GetScrollY();
        textAreaMin_ = ImGui::GetWindowPos();
        textAreaMax_ = ImVec2(textAreaMin_.x + ImGui::GetWindowWidth(),
                              textAreaMin_.y + ImGui::GetWindowHeight());
        textOrigin_ = ImGui::GetCursorScreenPos();
        textOrigin_.x += 2.0f; // margins: the current-line highlight starts at the text
        textOrigin_.y += 2.0f;

        const int startLine = std::max(0, static_cast<int>(scrollY_ / lineHeight_) - 2);
        const int endLine =
            std::min(static_cast<int>(lineStarts_.size()) - 1,
                     static_cast<int>((scrollY_ + ImGui::GetWindowHeight()) / lineHeight_) + 2);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!readOnly_)
            renderCurrentLineHighlight(drawList);
        renderFindHighlights(drawList, startLine, endLine);
        renderSelection(drawList, startLine, endLine);
        renderText(drawList, startLine, endLine);
        // outline the range whose play icon is hovered (last frame: the icons are drawn after
        // this child), on top of the current-line highlight
        if (hoveredRun_ >= 0 && hoveredRun_ < static_cast<int>(runRanges_.size())) {
            const auto [begin, end] = runRanges_[hoveredRun_];
            const float top = textOrigin_.y + getLineFromPos(static_cast<int>(begin)) * lineHeight_;
            const float bottom =
                textOrigin_.y + (getLineFromPos(static_cast<int>(end)) + 1) * lineHeight_;
            drawList->AddRect(ImVec2(textAreaMin_.x + 1.0f, top),
                              ImVec2(textAreaMax_.x - 1.0f, bottom), palette_.run, 3.0f, 0, 1.5f);
        }
        renderErrorMarker(drawList);
        if (content_.empty() && !placeholder_.empty()) {
            float y = textOrigin_.y;
            const char* start = placeholder_.c_str();
            const char* end = start + placeholder_.size();
            while (start < end) {
                const char* lineEnd = static_cast<const char*>(memchr(start, '\n', end - start));
                if (!lineEnd)
                    lineEnd = end;
                drawList->AddText(ImVec2(textOrigin_.x, y), withAlpha(palette_.text, 0x60), start,
                                  lineEnd);
                y += lineHeight_;
                start = lineEnd < end ? lineEnd + 1 : end;
            }
        }
        if (isFocused && !readOnly_) {
            renderCursor(drawList);
            // asks for the soft keyboard and places the ime the way imgui's text fields do; with
            // WantTextInput the shells underline a composition here and global shortcuts stand back
            ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
            ime.WantVisible = true;
            ime.WantTextInput = true;
            ime.InputPos = ImVec2(getScreenX(cursorIndex_),
                                  textOrigin_.y + getLineFromPos(cursorIndex_) * lineHeight_);
            ime.InputLineHeight = lineHeight_;
            ime.ViewportId = ImGui::GetWindowViewport()->ID;
        }

        ensureCursorVisible();

        ImGui::SetCursorPosY(contentHeight);
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (showLineNumbers_) {
            renderLineNumbers(ImGui::GetWindowDrawList(), startLine, endLine);
            renderRunButtons(startLine, endLine);
        }
        if (autocompleteVisible_)
            renderAutoComplete();
        renderFindReplace();

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    void TextEditor::renderLineNumbers(ImDrawList* drawList, int startLine, int endLine) {
        char buf[16];
        const int currentLine = getLineFromPos(cursorIndex_);
        for (int i = startLine; i <= endLine; ++i) {
            // textOrigin_ was captured inside the scrolled text area: no scroll to subtract
            snprintf(buf, sizeof(buf), "%d", i + 1);
            const float x = textOrigin_.x - 10.0f - ImGui::CalcTextSize(buf).x;
            const ImU32 color =
                (i == currentLine && !readOnly_) ? palette_.currentLineNumber : palette_.lineNumber;
            drawList->AddText(ImVec2(x, textOrigin_.y + i * lineHeight_), color, buf);
        }
    }

    void TextEditor::renderErrorMarker(ImDrawList* drawList) {
        if (errorOffset_ < 0 || errorOffset_ > static_cast<int>(content_.size()))
            return;
        auto word = [&](int i) {
            const auto c = static_cast<unsigned char>(content_[i]);
            return std::isalnum(c) || c == '_' || c >= 0x80;
        };
        auto blank = [&](int i) { return std::isspace(static_cast<unsigned char>(content_[i])); };
        const int size = static_cast<int>(content_.size());
        int b = errorOffset_;
        // at the end or on a gap ('unexpected end'): mark the token before it
        while (b > 0 && (b == size || blank(b)))
            --b;
        if (b >= size || blank(b))
            return;
        int e = b + 1;
        if (word(b)) {
            while (b > 0 && word(b - 1))
                --b;
            while (e < size && word(e))
                ++e;
        }
        const float x0 = getScreenX(b);
        const float x1 = std::max(getScreenX(e), x0 + 6.0f);
        const float top = textOrigin_.y + getLineFromPos(b) * lineHeight_;
        const float y = top + lineHeight_; // just under the glyphs
        constexpr float step = 2.5f;
        ImVec2 points[256];
        int n = 0;
        for (float x = x0; x < x1 && n < 255; x += step, ++n)
            points[n] = ImVec2(x, y + ((n & 1) ? -1.2f : 0.8f));
        points[n++] = ImVec2(x1, y);
        drawList->AddPolyline(points, n, palette_.error, ImDrawFlags_None, 1.2f);
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(ImVec2(x0, top), ImVec2(x1, top + lineHeight_)))
            ImGui::SetTooltip("%s", errorMessage_.c_str());
    }

    void TextEditor::renderRunButtons(int startLine, int endLine) {
        hoveredRun_ = -1;
        if (!rangeProvider_)
            return;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float iconX = textAreaMin_.x - lineNumberWidth_ + 6.0f; // the gutter's left edge
        const float iconW = ImGui::GetFontSize();
        // put back after the buttons, directly: SetCursorScreenPos past the content asserts
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImVec2 cursor = window->DC.CursorPos;
        for (size_t r = 0; r < runRanges_.size(); ++r) {
            const auto [begin, end] = runRanges_[r];
            const int line = getLineFromPos(static_cast<int>(begin));
            if (line < startLine || line > endLine)
                continue;
            const float y = textOrigin_.y + line * lineHeight_;
            // a real item, so tests can click it by id (##run_<n>)
            char id[24];
            snprintf(id, sizeof(id), "##run_%zu", r);
            ImGui::SetCursorScreenPos(ImVec2(iconX - 3.0f, y));
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(iconW + 6.0f, lineHeight_));
            const bool hovered = ImGui::IsItemHovered();
            // dimmed until hovered, so a long text is not a column of bright icons. a drawn
            // triangle: no icon font needed
            const float s = iconW * 0.3f;
            const ImVec2 c(iconX + iconW * 0.5f, y + ImGui::GetFontSize() * 0.5f);
            drawList->AddTriangleFilled(ImVec2(c.x - s * 0.8f, c.y - s), ImVec2(c.x + s, c.y),
                                        ImVec2(c.x - s * 0.8f, c.y + s),
                                        hovered ? palette_.run : withAlpha(palette_.run, 0x99));
            if (!hovered)
                continue;
            hoveredRun_ = static_cast<int>(r);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (clicked && onRun_)
                onRun_(begin, end);
        }
        window->DC.CursorPos = cursor;
    }

    void TextEditor::renderText(ImDrawList* drawList, int startLine, int endLine) {
        const float spaceWidth = ImGui::CalcTextSize(" ").x;
        const ImU32 colors[] = {palette_.text,    palette_.keyword,  palette_.string,
                                palette_.number,  palette_.comment,  palette_.function,
                                palette_.type,    palette_.operator_};
        const char* contentEnd = content_.data() + content_.size();
        for (int lineNum = startLine; lineNum <= endLine; ++lineNum) {
            float xPos = textOrigin_.x;
            const float yPos = textOrigin_.y + lineNum * lineHeight_;
            for (int i = lineStarts_[lineNum], lineEnd = getLineEnd(lineNum); i < lineEnd; ++i) {
                if (content_[i] == '\t') {
                    const int col = static_cast<int>((xPos - textOrigin_.x) / spaceWidth);
                    xPos = textOrigin_.x + ((col / tabSize_) + 1) * tabSize_ * spaceWidth;
                    continue;
                }
                const char* charStart = &content_[i];
                const char* charEnd = charStart + 1;
                while (charEnd < contentEnd && isContinuation(*charEnd))
                    ++charEnd;
                const auto token = static_cast<size_t>(tokens_[i]);
                drawList->AddText(ImVec2(xPos, yPos),
                                  token < std::size(colors) ? colors[token] : palette_.text,
                                  charStart, charEnd);
                xPos += ImGui::CalcTextSize(charStart, charEnd).x;
                i += static_cast<int>(charEnd - charStart) - 1;
            }
        }
    }

    void TextEditor::renderCursor(ImDrawList* drawList) {
        const float blinkAlpha = (sinf(cursorBlinkTimer_ * 5.0f) + 1.0f) * 0.5f;
        if (blinkAlpha < 0.3f)
            return;
        const float x = getScreenX(cursorIndex_);
        const float y = textOrigin_.y + getLineFromPos(cursorIndex_) * lineHeight_;
        const int a = static_cast<int>(((palette_.cursor >> 24) & 0xFF) * blinkAlpha);
        drawList->AddLine(ImVec2(x, y), ImVec2(x, y + lineHeight_ - 1),
                          withAlpha(palette_.cursor, a), 2.0f);
    }

    void TextEditor::renderSelection(ImDrawList* drawList, int startLine, int endLine) {
        if (!HasSelection())
            return;
        const int selStart = selectionStart();
        const int selEnd = selectionEnd();
        const float spaceWidth = ImGui::CalcTextSize(" ").x;
        for (int lineNum = startLine; lineNum <= endLine; ++lineNum) {
            const int lineStart = lineStarts_[lineNum];
            const int lineEnd = (lineNum + 1 < static_cast<int>(lineStarts_.size()))
                                    ? lineStarts_[lineNum + 1]
                                    : static_cast<int>(content_.size());
            const int rangeStart = std::max(selStart, lineStart);
            const int rangeEnd = std::min(selEnd, lineEnd);
            if (rangeStart >= rangeEnd)
                continue;
            const float x1 = xAt(lineStart, rangeStart);
            float x2 = xAt(lineStart, rangeEnd);
            if (content_[rangeEnd - 1] == '\n')
                x2 += spaceWidth; // the selection runs past the newline
            const float y = textOrigin_.y + lineNum * lineHeight_;
            drawList->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + lineHeight_), palette_.selection);
        }
    }

    void TextEditor::renderCurrentLineHighlight(ImDrawList* drawList) {
        // not under a selection that spans lines
        if (HasSelection() && getLineFromPos(selectionStart()) != getLineFromPos(selectionEnd()))
            return;
        const float y = textOrigin_.y + getLineFromPos(cursorIndex_) * lineHeight_;
        const ImVec2 winPos = ImGui::GetWindowPos();
        drawList->AddRectFilled(ImVec2(winPos.x, y),
                                ImVec2(winPos.x + ImGui::GetWindowWidth(), y + lineHeight_),
                                palette_.currentLineBackground);
    }

    float TextEditor::xAt(int lineStart, int pos) const {
        const float spaceWidth = ImGui::CalcTextSize(" ").x;
        const int size = static_cast<int>(content_.size());
        float x = textOrigin_.x;
        for (int i = lineStart; i < pos && i < size; ++i) {
            if (content_[i] == '\n')
                break;
            if (content_[i] == '\t') {
                const int col = static_cast<int>((x - textOrigin_.x) / spaceWidth);
                x = textOrigin_.x + ((col / tabSize_) + 1) * tabSize_ * spaceWidth;
                continue;
            }
            int e = i + 1;
            while (e < size && isContinuation(content_[e]))
                ++e;
            x += ImGui::CalcTextSize(&content_[i], content_.data() + e).x;
            i = e - 1;
        }
        return x;
    }

    float TextEditor::getScreenX(int pos) const {
        return xAt(lineStarts_[getLineFromPos(pos)], pos);
    }

    void TextEditor::ensureCursorVisible() {
        if (ensureCursorVisibleV_) {
            const float cursorY = getLineFromPos(cursorIndex_) * lineHeight_;
            const float windowHeight = ImGui::GetWindowHeight();
            if (cursorY < scrollY_)
                ImGui::SetScrollY(cursorY);
            else if (cursorY + lineHeight_ > scrollY_ + windowHeight)
                ImGui::SetScrollY(cursorY + lineHeight_ - windowHeight);
            ensureCursorVisibleV_ = false;
        }
        if (ensureCursorVisibleH_) {
            const float cursorX = getScreenX(cursorIndex_) - textOrigin_.x;
            const float windowWidth = ImGui::GetWindowWidth();
            if (cursorX < scrollX_)
                ImGui::SetScrollX(std::max(0.0f, cursorX - 20.0f));
            else if (cursorX > scrollX_ + windowWidth - 20.0f)
                ImGui::SetScrollX(cursorX - windowWidth + 40.0f);
            ensureCursorVisibleH_ = false;
        }
    }

    // --- selection and editing helpers ---

    int TextEditor::selectionStart() const {
        return std::min(cursorIndex_, selectionAnchor_);
    }

    int TextEditor::selectionEnd() const {
        return std::max(cursorIndex_, selectionAnchor_);
    }

    void TextEditor::deleteSelection() {
        if (!HasSelection())
            return;
        const int start = selectionStart();
        content_.erase(start, selectionEnd() - start);
        cursorIndex_ = start;
        clearSelection();
        edited();
    }

    void TextEditor::clearSelection() {
        selectionActive_ = false;
        selectionAnchor_ = cursorIndex_;
    }

    void TextEditor::selectRange(int from, int to) {
        selectionAnchor_ = from;
        cursorIndex_ = to;
        selectionActive_ = true;
        ensureCursorVisibleV_ = true;
        ensureCursorVisibleH_ = true;
        cursorBlinkTimer_ = 0.0f;
    }

    int TextEditor::findWordStart(int pos) const {
        while (pos > 0 && isWordChar(content_[pos - 1]))
            --pos;
        return pos;
    }

    void TextEditor::insertTextAtCursor(const std::string& text) {
        deleteSelection();
        const int pos = std::clamp(cursorIndex_, 0, static_cast<int>(content_.size()));
        content_.insert(pos, text);
        cursorIndex_ = pos + static_cast<int>(text.size());
        clearSelection();
        edited();
    }

    void TextEditor::pushUndoSnapshot() {
        redoStack_.clear(); // an edit follows: what was undone is gone
        if (content_ == lastSnapshotContent_)
            return;
        undoStack_.push_back({lastSnapshotContent_, cursorIndex_});
        if (undoStack_.size() > 100)
            undoStack_.erase(undoStack_.begin());
        lastSnapshotContent_ = content_;
    }

    void TextEditor::restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to) {
        if (from.empty())
            return;
        to.push_back({content_, cursorIndex_});
        content_ = std::move(from.back().content);
        cursorIndex_ = std::min(from.back().cursorIndex, static_cast<int>(content_.size()));
        from.pop_back();
        clearSelection();
        edited();
        lastSnapshotContent_ = content_;
        ensureCursorVisibleV_ = true;
    }

    // --- keyboard ---

    void TextEditor::handleKeyboardInput() {
        const ImGuiIO& io = ImGui::GetIO();
        const bool ctrl = io.KeyCtrl || io.KeySuper;

        if (readOnly_) {
            if (ctrl) {
                handleClipboard(); // cut and paste are off when read-only
                handleSelectAll();
            }
            handleArrowKeys();
            handleHomeEnd();
            return;
        }

        // the popup's keys come first
        if (autocompleteVisible_) {
            const int count = static_cast<int>(completions_.size());
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                if (autocompleteIndex_ >= count - 1) {
                    autocompleteIndex_ = 0;
                    autocompleteScrollOffset_ = 0;
                } else if (++autocompleteIndex_ >=
                           autocompleteScrollOffset_ + kAutocompleteMaxVisible) {
                    autocompleteScrollOffset_ = autocompleteIndex_ - kAutocompleteMaxVisible + 1;
                }
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                if (autocompleteIndex_ <= 0) {
                    autocompleteIndex_ = count - 1;
                    autocompleteScrollOffset_ = std::max(0, count - kAutocompleteMaxVisible);
                } else if (--autocompleteIndex_ < autocompleteScrollOffset_) {
                    autocompleteScrollOffset_ = autocompleteIndex_;
                }
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
                applyAutoComplete();
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                dismissAutoComplete();
                return;
            }
        }

        if (findVisible_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            closeFind();
            return;
        }

        // ctrl+. always arrives; ctrl+space when the os does not take it
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Period, false) ||
                     ImGui::IsKeyPressed(ImGuiKey_Space, false))) {
            updateAutoComplete(true);
            return;
        }

        if (ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
                if (submitCallback_)
                    submitCallback_();
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_H)) {
                openFind();
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Slash)) {
                toggleLineComment();
                return;
            }
            handleClipboard();
            handleUndoRedo();
            handleSelectAll();
        }

        handleArrowKeys();
        handleHomeEnd();
        handleCharacterInput();
        handleEnterKey();
        handleBackspaceKey();
        handleDeleteKey();
        handleTabKey();
    }

    void TextEditor::handleCharacterInput() {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeySuper)
            return;

        std::string input;
        for (const ImWchar ch : io.InputQueueCharacters) {
            if (ch >= 32) {
                char buf[5] = {};
                ImTextCharToUtf8(buf, ch);
                input += buf;
            }
        }
        if (input.empty())
            return;

        pushUndoSnapshot();
        cursorBlinkTimer_ = 0.0f;
        ensureCursorVisibleV_ = true;
        ensureCursorVisibleH_ = true;

        // auto-close brackets and quotes
        if (input.size() == 1) {
            const char ch = input[0];
            const char next =
                cursorIndex_ < static_cast<int>(content_.size()) ? content_[cursorIndex_] : '\0';

            // typing the closing one right before it steps over it
            if ((ch == ')' || ch == ']' || ch == '}' || ch == '"' || ch == '\'') && next == ch) {
                cursorIndex_++;
                selectionAnchor_ = cursorIndex_;
                updateAutoComplete();
                return;
            }

            char closing = '\0';
            if (ch == '(')
                closing = ')';
            else if (ch == '[')
                closing = ']';
            else if (ch == '{')
                closing = '}';
            else if ((ch == '"' || ch == '\'') &&
                     !(cursorIndex_ > 0 && isWordChar(content_[cursorIndex_ - 1])))
                closing = ch; // not after a word: an apostrophe
            if (closing) {
                insertTextAtCursor(input + closing);
                cursorIndex_--;
                selectionAnchor_ = cursorIndex_;
                updateAutoComplete();
                return;
            }
        }

        insertTextAtCursor(input);
        updateAutoComplete();
    }

    void TextEditor::handleEnterKey() {
        if (!ImGui::IsKeyPressed(ImGuiKey_Enter) || autocompleteVisible_)
            return;

        pushUndoSnapshot();
        deleteSelection();

        // the current line's indentation, up to the cursor
        const int lineStart = lineStarts_[getLineFromPos(cursorIndex_)];
        std::string indent;
        for (int i = lineStart; i < cursorIndex_; ++i) {
            if (content_[i] != ' ' && content_[i] != '\t')
                break;
            indent += content_[i];
        }

        const char prev = cursorIndex_ > 0 ? content_[cursorIndex_ - 1] : '\0';
        const char next =
            cursorIndex_ < static_cast<int>(content_.size()) ? content_[cursorIndex_] : '\0';
        const bool afterBracket = prev == '{' || prev == '[' || prev == '(';

        // or after one of the host's block-opening words
        bool afterWord = false;
        if (!afterBracket && !indentWords_.empty()) {
            int wordEnd = cursorIndex_;
            while (wordEnd > lineStart && content_[wordEnd - 1] == ' ')
                wordEnd--;
            int wordStart = wordEnd;
            while (wordStart > lineStart &&
                   std::isalpha(static_cast<unsigned char>(content_[wordStart - 1])))
                wordStart--;
            const std::string last = lower(content_.substr(wordStart, wordEnd - wordStart));
            afterWord = !last.empty() &&
                        std::any_of(indentWords_.begin(), indentWords_.end(),
                                    [&](const std::string& w) { return lower(w) == last; });
        }

        const std::string tab(tabSize_, ' ');
        std::string toInsert = "\n" + indent;
        int advance = 0; // the cursor's place in toInsert, 0: its end
        if (afterBracket && (next == '}' || next == ']' || next == ')')) {
            // between a pair: three lines, the cursor on the indented middle one
            toInsert = "\n" + indent + tab + "\n" + indent;
            advance = static_cast<int>(indent.size() + tab.size() + 1);
        } else if (afterBracket || afterWord) {
            toInsert += tab;
        }
        content_.insert(cursorIndex_, toInsert);
        cursorIndex_ += advance ? advance : static_cast<int>(toInsert.size());
        clearSelection();
        edited();
        cursorBlinkTimer_ = 0.0f;
        ensureCursorVisibleV_ = true;
        ensureCursorVisibleH_ = true;
        dismissAutoComplete();
    }

    void TextEditor::handleBackspaceKey() {
        if (!ImGui::IsKeyPressed(ImGuiKey_Backspace))
            return;

        pushUndoSnapshot();

        // mac: cmd deletes to the line start, opt the word before; elsewhere ctrl+shift and ctrl
        const ImGuiIO& io = ImGui::GetIO();
        const bool mac = io.ConfigMacOSXBehaviors;
        // cmd arrives as ctrl or super depending on the event source
        const bool toLineStart = mac ? io.KeyCtrl || io.KeySuper : io.KeyCtrl && io.KeyShift;
        const bool toWordStart = !toLineStart && (mac ? io.KeyAlt : io.KeyCtrl);
        int from = cursorIndex_;
        if (toLineStart) {
            from = lineStarts_[getLineFromPos(cursorIndex_)];
        } else if (toWordStart) {
            while (from > 0 && (content_[from - 1] == ' ' || content_[from - 1] == '\t'))
                --from;
            const int word = findWordStart(from);
            if (word < from) {
                from = word;
            } else { // a run of punctuation counts as a word
                while (from > 0 && !isWordChar(content_[from - 1]) &&
                       !std::isspace(static_cast<unsigned char>(content_[from - 1])))
                    --from;
            }
        }

        if (HasSelection()) {
            deleteSelection();
        } else if (from < cursorIndex_) {
            selectionAnchor_ = from;
            selectionActive_ = true;
            deleteSelection();
        } else if (cursorIndex_ > 0) {
            // at the line start a plain backspace joins the lines
            int prevPos = cursorIndex_ - 1;
            while (prevPos > 0 && isContinuation(content_[prevPos]))
                --prevPos;
            int deleteLen = cursorIndex_ - prevPos;
            // an empty pair goes together
            if (cursorIndex_ < static_cast<int>(content_.size())) {
                const char prev = content_[prevPos];
                const char next = content_[cursorIndex_];
                if ((prev == '(' && next == ')') || (prev == '[' && next == ']') ||
                    (prev == '{' && next == '}') || (prev == '"' && next == '"') ||
                    (prev == '\'' && next == '\''))
                    deleteLen++;
            }
            content_.erase(prevPos, deleteLen);
            cursorIndex_ = prevPos;
            clearSelection();
            edited();
        }
        cursorBlinkTimer_ = 0.0f;
        ensureCursorVisibleV_ = true;
        ensureCursorVisibleH_ = true;
        // never opens the popup; an open one narrows as the user deletes
        if (autocompleteVisible_)
            updateAutoComplete();
    }

    void TextEditor::handleDeleteKey() {
        if (!ImGui::IsKeyPressed(ImGuiKey_Delete))
            return;

        pushUndoSnapshot();
        if (HasSelection()) {
            deleteSelection();
        } else if (cursorIndex_ < static_cast<int>(content_.size())) {
            int nextPos = cursorIndex_ + 1;
            while (nextPos < static_cast<int>(content_.size()) && isContinuation(content_[nextPos]))
                ++nextPos;
            content_.erase(cursorIndex_, nextPos - cursorIndex_);
            edited();
        }
        cursorBlinkTimer_ = 0.0f;
        if (autocompleteVisible_)
            updateAutoComplete();
    }

    void TextEditor::handleTabKey() {
        const ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsKeyPressed(ImGuiKey_Tab, false) || autocompleteVisible_ || io.KeyCtrl)
            return;

        pushUndoSnapshot();
        const int size = static_cast<int>(content_.size());
        if (HasSelection()) {
            // indent or unindent the selected lines
            const int startLine = getLineFromPos(selectionStart());
            const int endLine = getLineFromPos(selectionEnd());
            int offset = 0;
            for (int line = startLine; line <= endLine; ++line) {
                const int linePos = lineStarts_[line] + offset;
                if (!io.KeyShift) {
                    content_.insert(linePos, "\t");
                    ++offset;
                } else if (linePos < size + offset && content_[linePos] == '\t') {
                    content_.erase(linePos, 1);
                    --offset;
                }
            }
            cursorIndex_ += offset;
            selectionAnchor_ += (offset > 0) ? offset : 0;
            cursorIndex_ = std::clamp(cursorIndex_, 0, static_cast<int>(content_.size()));
            edited();
        } else if (io.KeyShift) {
            const int linePos = lineStarts_[getLineFromPos(cursorIndex_)];
            if (linePos < size && content_[linePos] == '\t') {
                content_.erase(linePos, 1);
                if (cursorIndex_ > linePos)
                    --cursorIndex_;
                clearSelection();
                edited();
            }
        } else {
            insertTextAtCursor("\t");
        }
        cursorBlinkTimer_ = 0.0f;
        ensureCursorVisibleH_ = true;
    }

    void TextEditor::handleArrowKeys() {
        const bool shift = ImGui::GetIO().KeyShift;
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        const int size = static_cast<int>(content_.size());

        auto startSelection = [&] {
            if (!shift) {
                if (HasSelection())
                    clearSelection();
            } else if (!selectionActive_) {
                selectionActive_ = true;
                selectionAnchor_ = cursorIndex_;
            }
        };
        auto finishMove = [&] {
            if (!shift)
                selectionAnchor_ = cursorIndex_;
            cursorBlinkTimer_ = 0.0f;
            ensureCursorVisibleH_ = true;
            ensureCursorVisibleV_ = true;
        };

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            const bool collapse = !shift && HasSelection();
            const int start = selectionStart();
            startSelection();
            if (collapse) {
                cursorIndex_ = start;
            } else if (ctrl) { // a word
                if (cursorIndex_ > 0) {
                    --cursorIndex_;
                    while (cursorIndex_ > 0 && !isWordChar(content_[cursorIndex_]))
                        --cursorIndex_;
                    cursorIndex_ = findWordStart(cursorIndex_);
                }
            } else if (cursorIndex_ > 0) {
                --cursorIndex_;
                while (cursorIndex_ > 0 && isContinuation(content_[cursorIndex_]))
                    --cursorIndex_;
            }
            finishMove();
            preferredColumn_ = getColumnFromPos(cursorIndex_);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            const bool collapse = !shift && HasSelection();
            const int end = selectionEnd();
            startSelection();
            if (collapse) {
                cursorIndex_ = end;
            } else if (ctrl) {
                if (cursorIndex_ < size) {
                    ++cursorIndex_;
                    while (cursorIndex_ < size && !isWordChar(content_[cursorIndex_]))
                        ++cursorIndex_;
                    while (cursorIndex_ < size && isWordChar(content_[cursorIndex_]))
                        ++cursorIndex_;
                }
            } else if (cursorIndex_ < size) {
                ++cursorIndex_;
                while (cursorIndex_ < size && isContinuation(content_[cursorIndex_]))
                    ++cursorIndex_;
            }
            finishMove();
            preferredColumn_ = getColumnFromPos(cursorIndex_);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !autocompleteVisible_) {
            startSelection();
            const int line = getLineFromPos(cursorIndex_);
            if (line > 0) {
                cursorIndex_ = getPosFromLineColumn(line - 1, preferredColumn_);
            } else { // on the first line: to its start
                cursorIndex_ = 0;
                preferredColumn_ = 0;
            }
            finishMove();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !autocompleteVisible_) {
            startSelection();
            const int line = getLineFromPos(cursorIndex_);
            if (line < static_cast<int>(lineStarts_.size()) - 1) {
                cursorIndex_ = getPosFromLineColumn(line + 1, preferredColumn_);
            } else { // on the last line: to its end
                cursorIndex_ = size;
                preferredColumn_ = getColumnFromPos(cursorIndex_);
            }
            finishMove();
        }
    }

    void TextEditor::handleHomeEnd() {
        const bool home = ImGui::IsKeyPressed(ImGuiKey_Home);
        if (!home && !ImGui::IsKeyPressed(ImGuiKey_End))
            return;
        const bool shift = ImGui::GetIO().KeyShift;
        if (shift && !selectionActive_) {
            selectionActive_ = true;
            selectionAnchor_ = cursorIndex_;
        }
        const int line = getLineFromPos(cursorIndex_);
        cursorIndex_ = home ? lineStarts_[line] : getLineEnd(line);
        if (!shift)
            clearSelection();
        preferredColumn_ = getColumnFromPos(cursorIndex_);
        cursorBlinkTimer_ = 0.0f;
        ensureCursorVisibleH_ = true;
    }

    void TextEditor::handleSelectAll() {
        if (!ImGui::GetIO().KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_A))
            return;
        selectionActive_ = true;
        selectionAnchor_ = 0;
        cursorIndex_ = static_cast<int>(content_.size());
    }

    void TextEditor::handleClipboard() {
        if (!ImGui::GetIO().KeyCtrl)
            return;
        const std::string sel = GetSelectedText();
        if (ImGui::IsKeyPressed(ImGuiKey_C) && !sel.empty())
            ImGui::SetClipboardText(sel.c_str());
        if (readOnly_)
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_X) && !sel.empty()) {
            ImGui::SetClipboardText(sel.c_str());
            pushUndoSnapshot();
            deleteSelection();
            cursorBlinkTimer_ = 0.0f;
            ensureCursorVisibleV_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V)) {
            const char* clipboard = ImGui::GetClipboardText();
            if (clipboard && *clipboard) {
                pushUndoSnapshot();
                insertTextAtCursor(clipboard);
                cursorBlinkTimer_ = 0.0f;
                ensureCursorVisibleV_ = true;
                ensureCursorVisibleH_ = true;
                dismissAutoComplete();
            }
        }
    }

    void TextEditor::handleUndoRedo() {
        if (!ImGui::GetIO().KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_Z))
            return;
        if (ImGui::GetIO().KeyShift) {
            restore(redoStack_, undoStack_);
            return;
        }
        // the edits since the last snapshot are a step of their own
        if (content_ != lastSnapshotContent_) {
            undoStack_.push_back({lastSnapshotContent_, cursorIndex_});
            lastSnapshotContent_ = content_;
        }
        restore(undoStack_, redoStack_);
    }

    void TextEditor::toggleLineComment() {
        if (lineComment_.empty())
            return;
        pushUndoSnapshot();

        int startLine, endLine;
        if (HasSelection()) {
            startLine = getLineFromPos(selectionStart());
            endLine = getLineFromPos(selectionEnd());
            // a selection ending at column 0 leaves that line out
            if (endLine > startLine && selectionEnd() == lineStarts_[endLine])
                --endLine;
        } else {
            startLine = endLine = getLineFromPos(cursorIndex_);
        }

        // the first non-blank of a line, and whether the prefix is there
        auto textStart = [&](int line) {
            int j = lineStarts_[line];
            const int le = getLineEnd(line);
            while (j < le && (content_[j] == ' ' || content_[j] == '\t'))
                ++j;
            return j;
        };
        auto commented = [&](int j) {
            return content_.compare(j, lineComment_.size(), lineComment_) == 0;
        };

        bool allCommented = true; // blank lines do not count
        for (int i = startLine; i <= endLine && allCommented; ++i) {
            const int j = textStart(i);
            allCommented = j >= getLineEnd(i) || commented(j);
        }

        int cursorOffset = 0;
        const int prefix = static_cast<int>(lineComment_.size());
        for (int i = startLine; i <= endLine; ++i) {
            const int j = textStart(i);
            if (!allCommented) {
                content_.insert(j, lineComment_ + " ");
                if (cursorIndex_ >= j)
                    cursorOffset += prefix + 1;
            } else if (commented(j)) { // the prefix and one space after it
                const int removeLen =
                    prefix + (j + prefix < getLineEnd(i) && content_[j + prefix] == ' ');
                content_.erase(j, removeLen);
                if (cursorIndex_ > j)
                    cursorOffset -= removeLen;
            }
            edited(); // the offsets shifted
        }

        cursorIndex_ =
            std::clamp(cursorIndex_ + cursorOffset, 0, static_cast<int>(content_.size()));
        clearSelection();
    }

    // --- mouse ---

    void TextEditor::handleMouseInput() {
        // a click on a suggestion picks it (the popup can hang outside the editor)
        if (autocompleteVisible_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect(autocompleteMin_, autocompleteMax_, false)) {
            const int row = int((ImGui::GetMousePos().y - autocompleteMin_.y) / autocompleteRow_);
            autocompleteIndex_ = autocompleteScrollOffset_ + row;
            applyAutoComplete();
            return;
        }
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            return;
        // a press on a gutter play icon runs the range; it is not a text click
        if (hoveredRun_ >= 0 && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                                 ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            return;

        const int charIndex = getCharIndexFromScreenPos(ImGui::GetMousePos());

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            handleDoubleClick(charIndex);
            return;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dismissAutoComplete();
            if (ImGui::GetIO().KeyShift) {
                if (!selectionActive_) {
                    selectionActive_ = true;
                    selectionAnchor_ = cursorIndex_;
                }
                cursorIndex_ = charIndex;
            } else {
                cursorIndex_ = charIndex;
                clearSelection();
            }
            isDragging_ = true;
            preferredColumn_ = getColumnFromPos(cursorIndex_);
            cursorBlinkTimer_ = 0.0f;
            ensureCursorVisibleH_ = true;
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && isDragging_) {
            selectionActive_ = true;
            cursorIndex_ = charIndex;
            preferredColumn_ = getColumnFromPos(cursorIndex_);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            isDragging_ = false;
    }

    int TextEditor::getCharIndexFromScreenPos(ImVec2 screenPos) const {
        // textOrigin_ was captured inside the scrolled text area: plain screen coordinates
        const int line = std::clamp(static_cast<int>((screenPos.y - textOrigin_.y) / lineHeight_),
                                    0, static_cast<int>(lineStarts_.size()) - 1);
        const int lineStart = lineStarts_[line];
        const int lineEnd = getLineEnd(line);
        const float spaceWidth = ImGui::CalcTextSize(" ").x;
        float x = textOrigin_.x;
        for (int i = lineStart; i < lineEnd;) {
            int e = i + 1;
            float next;
            if (content_[i] == '\t') {
                const int col = static_cast<int>((x - textOrigin_.x) / spaceWidth);
                next = textOrigin_.x + ((col / tabSize_) + 1) * tabSize_ * spaceWidth;
            } else {
                while (e < lineEnd && isContinuation(content_[e]))
                    ++e;
                next = x + ImGui::CalcTextSize(&content_[i], content_.data() + e).x;
            }
            if (screenPos.x < (x + next) * 0.5f)
                return i;
            x = next;
            i = e;
        }
        return lineEnd;
    }

    void TextEditor::handleDoubleClick(int charIndex) {
        const int size = static_cast<int>(content_.size());
        if (charIndex < 0 || charIndex >= size)
            return;
        int end = charIndex;
        while (end < size && isWordChar(content_[end]))
            ++end;
        selectionActive_ = true;
        selectionAnchor_ = findWordStart(charIndex);
        cursorIndex_ = end;
        preferredColumn_ = getColumnFromPos(cursorIndex_);
        cursorBlinkTimer_ = 0.0f;
    }

    // --- completion ---

    void TextEditor::updateAutoComplete(bool forced) {
        // opens on a word char or a member dot; the provider decides the rest
        const char prev = cursorIndex_ > 0 ? content_[cursorIndex_ - 1] : '\0';
        if (!completionProvider_ || (!forced && !isWordChar(prev) && prev != '.')) {
            dismissAutoComplete();
            return;
        }
        auto response = completionProvider_(content_, cursorIndex_, forced);
        if (response.items.empty()) {
            dismissAutoComplete();
            return;
        }
        if (response.items.size() > kMaxCompletions)
            response.items.resize(kMaxCompletions);
        completions_ = std::move(response.items);
        autocompleteVisible_ = true;
        autocompleteIndex_ = 0;
        autocompleteScrollOffset_ = 0;
        autocompleteReplaceStart_ = response.replaceStart;
        autocompleteReplaceEnd_ = response.replaceEnd;
    }

    void TextEditor::renderAutoComplete() {
        const float cursorX = getScreenX(cursorIndex_);
        const float cursorY = textOrigin_.y + (getLineFromPos(cursorIndex_) + 1) * lineHeight_;

        const ImGuiViewport* viewport = ImGui::GetWindowViewport();
        // a phone is narrower than the popup
        const float popupWidth = std::min(520.0f, viewport->WorkSize.x);
        constexpr float popupPadding = 6.0f;
        const int maxShow =
            std::min(static_cast<int>(completions_.size()), kAutocompleteMaxVisible);
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const float popupHeight = popupPadding * 2.0f + rowHeight * maxShow;

        const ImVec2 vpMin = viewport->WorkPos; // clear of a status bar
        const ImVec2 vpMax(vpMin.x + viewport->WorkSize.x, vpMin.y + viewport->WorkSize.y);
        const ImVec2 pos(std::clamp(cursorX, vpMin.x, std::max(vpMin.x, vpMax.x - popupWidth)),
                         std::clamp(cursorY, vpMin.y, std::max(vpMin.y, vpMax.y - popupHeight)));

        ImDrawList* drawList = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(viewport));
        const ImVec2 rectMax(pos.x + popupWidth, pos.y + popupHeight);
        autocompleteMin_ = ImVec2(pos.x, pos.y + popupPadding);
        autocompleteMax_ = ImVec2(rectMax.x, rectMax.y - popupPadding);
        autocompleteRow_ = rowHeight;
        drawList->AddRectFilled(pos, rectMax, palette_.background);
        drawList->AddRect(pos, rectMax, palette_.lineNumber);

        constexpr float iconWidth = 20.0f;
        for (int i = 0; i < maxShow; ++i) {
            const int itemIdx = i + autocompleteScrollOffset_;
            if (itemIdx >= static_cast<int>(completions_.size()))
                break;
            const CompletionItem& item = completions_[itemIdx];
            const float rowY = pos.y + popupPadding + rowHeight * i;
            if (itemIdx == autocompleteIndex_)
                drawList->AddRectFilled(ImVec2(pos.x + 2.0f, rowY),
                                        ImVec2(pos.x + popupWidth - 2.0f, rowY + rowHeight),
                                        palette_.selection);
            drawList->AddText(ImVec2(pos.x + popupPadding, rowY), TokenColor(item.iconColor),
                              item.icon.c_str());

            // the detail on the right, dimmed; the label is clipped short of it
            const float textX = pos.x + popupPadding + iconWidth;
            float textMaxX = rectMax.x - popupPadding;
            if (!item.detailText.empty()) {
                const float detailX = textMaxX - ImGui::CalcTextSize(item.detailText.c_str()).x;
                drawList->AddText(ImVec2(detailX, rowY), palette_.lineNumber,
                                  item.detailText.c_str());
                textMaxX = detailX - 12.0f;
            }
            drawList->PushClipRect(ImVec2(textX, rowY), ImVec2(textMaxX, rowY + rowHeight));
            drawList->AddText(ImVec2(textX, rowY), palette_.text, item.text.c_str());
            drawList->PopClipRect();
        }
    }

    void TextEditor::applyAutoComplete() {
        if (!autocompleteVisible_ || autocompleteIndex_ < 0 ||
            autocompleteIndex_ >= static_cast<int>(completions_.size()))
            return;

        pushUndoSnapshot();
        const CompletionItem& item = completions_[autocompleteIndex_];
        const std::string& completion = item.insertText.empty() ? item.text : item.insertText;
        const int size = static_cast<int>(content_.size());
        const int start = std::clamp(autocompleteReplaceStart_, 0, size);
        const int end = std::clamp(autocompleteReplaceEnd_, start, size);
        content_.replace(start, end - start, completion);
        cursorIndex_ = start + static_cast<int>(completion.size());
        clearSelection();
        edited();
        dismissAutoComplete();
    }

    void TextEditor::dismissAutoComplete() {
        autocompleteVisible_ = false;
        completions_.clear();
        autocompleteIndex_ = 0;
        autocompleteScrollOffset_ = 0;
    }

    // --- find and replace ---

    void TextEditor::openFind() {
        findVisible_ = true;
        findFocusRequested_ = true;
        // starts from the selection
        const std::string sel = GetSelectedText();
        if (!sel.empty() && sel.size() < sizeof(findBuf_) - 1) {
            snprintf(findBuf_, sizeof(findBuf_), "%s", sel.c_str());
            performFind();
        }
    }

    void TextEditor::closeFind() {
        findVisible_ = false;
        findMatches_.clear();
        findCurrentMatch_ = -1;
    }

    void TextEditor::performFind() {
        findMatches_.clear();
        findCurrentMatch_ = -1;
        std::string needle(findBuf_);
        if (needle.empty())
            return;

        const std::string haystack = findCaseSensitive_ ? content_ : lower(content_);
        if (!findCaseSensitive_)
            needle = lower(needle);
        const int len = static_cast<int>(needle.size());
        const int size = static_cast<int>(content_.size());
        for (size_t pos = 0; (pos = haystack.find(needle, pos)) != std::string::npos; pos += len) {
            const int at = static_cast<int>(pos);
            if (!findWholeWord_ || ((at == 0 || !isWordChar(content_[at - 1])) &&
                                    (at + len >= size || !isWordChar(content_[at + len]))))
                findMatches_.push_back(at);
        }
        if (findMatches_.empty())
            return;

        // the nearest match from the cursor on
        findCurrentMatch_ = 0;
        for (size_t i = 0; i < findMatches_.size(); ++i) {
            if (findMatches_[i] >= cursorIndex_) {
                findCurrentMatch_ = static_cast<int>(i);
                break;
            }
        }
        selectRange(findMatches_[findCurrentMatch_], findMatches_[findCurrentMatch_] + len);
    }

    void TextEditor::findStep(int by) {
        if (findMatches_.empty())
            return;
        const int count = static_cast<int>(findMatches_.size());
        findCurrentMatch_ = (findCurrentMatch_ + by + count) % count;
        const int at = findMatches_[findCurrentMatch_];
        selectRange(at, at + static_cast<int>(strlen(findBuf_)));
    }

    void TextEditor::replaceOne() {
        if (findMatches_.empty() || findCurrentMatch_ < 0)
            return;
        pushUndoSnapshot();
        const int at = findMatches_[findCurrentMatch_];
        content_.replace(at, strlen(findBuf_), replaceBuf_);
        cursorIndex_ = at + static_cast<int>(strlen(replaceBuf_));
        clearSelection();
        edited();
        performFind();
    }

    void TextEditor::replaceAll() {
        if (findMatches_.empty())
            return;
        pushUndoSnapshot();
        // from the end, so the earlier offsets stand
        for (auto it = findMatches_.rbegin(); it != findMatches_.rend(); ++it)
            content_.replace(*it, strlen(findBuf_), replaceBuf_);
        cursorIndex_ = std::min(cursorIndex_, static_cast<int>(content_.size()));
        clearSelection();
        edited();
        performFind();
    }

    void TextEditor::renderFindReplace() {
        if (!findVisible_)
            return;

        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();
        const float barWidth = std::min(460.0f, winWidth - 20.0f);
        ImGui::SetNextWindowPos(ImVec2(winPos.x + winWidth - barWidth - 10.0f, winPos.y + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(barWidth, 0));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, withAlpha(palette_.currentLineBackground, 240));
        ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(palette_.lineNumber, 200));

        // one bar per editor: the name carries this editor's id
        char name[40];
        snprintf(name, sizeof(name), "##find_replace_%08x", ImGui::GetID("find"));
        if (ImGui::Begin(name, nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings)) {
            const float inputWidth = barWidth - 140.0f - 16.0f;

            ImGui::SetNextItemWidth(inputWidth);
            if (ImGui::InputText("##find_input", findBuf_, sizeof(findBuf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                performFind();
                findStep(1);
            }
            if (ImGui::IsItemEdited())
                performFind();
            if (findFocusRequested_) {
                ImGui::SetKeyboardFocusHere(-1);
                findFocusRequested_ = false;
            }

            ImGui::SameLine();
            if (findBuf_[0]) { // the match count
                const bool noMatch = findMatches_.empty();
                if (noMatch)
                    ImGui::PushStyleColor(ImGuiCol_Text, palette_.error);
                ImGui::AlignTextToFramePadding();
                if (noMatch)
                    ImGui::TextUnformatted("0");
                else
                    ImGui::Text("%d/%d", findCurrentMatch_ + 1,
                                static_cast<int>(findMatches_.size()));
                if (noMatch)
                    ImGui::PopStyleColor();
                ImGui::SameLine();
            }

            auto toggle = [&](const char* label, const char* tip, bool& on) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      on ? palette_.selection : palette_.background);
                const bool pressed = SmallButton(label, ButtonVariant::Custom);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s (%s)", tip, on ? "on" : "off");
                if (pressed) {
                    on = !on;
                    performFind();
                }
                ImGui::SameLine();
            };
            toggle("Aa", "Match Case", findCaseSensitive_);
            toggle("W", "Match Whole Word", findWholeWord_);

            if (SmallButton("<"))
                findStep(-1);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Previous Match (Shift+F3)");
            ImGui::SameLine();
            if (SmallButton(">"))
                findStep(1);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Next Match (F3)");
            ImGui::SameLine();
            if (SmallButton("x"))
                closeFind();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Close (Escape)");

            ImGui::SetNextItemWidth(inputWidth);
            if (ImGui::InputText("##replace_input", replaceBuf_, sizeof(replaceBuf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                replaceOne();
            ImGui::SameLine();
            ImGui::BeginDisabled(findMatches_.empty());
            if (SmallButton("Replace"))
                replaceOne();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Replace Next Match");
            ImGui::SameLine();
            if (SmallButton("All"))
                replaceAll();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Replace All Matches");
            ImGui::EndDisabled();

            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                closeFind();
            if (ImGui::IsKeyPressed(ImGuiKey_F3))
                findStep(ImGui::GetIO().KeyShift ? -1 : 1);
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }

    void TextEditor::renderFindHighlights(ImDrawList* drawList, int startLine, int endLine) {
        const int len = static_cast<int>(strlen(findBuf_));
        if (!findVisible_ || findMatches_.empty() || len == 0 || startLine > endLine)
            return;
        const int startPos = lineStarts_[startLine];
        const int endPos = getLineEnd(endLine);
        for (size_t mi = 0; mi < findMatches_.size(); ++mi) {
            const int at = findMatches_[mi];
            if (at + len < startPos || at > endPos)
                continue;
            const int line = getLineFromPos(at);
            const float y = textOrigin_.y + line * lineHeight_;
            // tinted from the type colour; the current match stronger
            drawList->AddRectFilled(
                ImVec2(xAt(lineStarts_[line], at), y),
                ImVec2(xAt(lineStarts_[line], at + len), y + lineHeight_),
                withAlpha(palette_.type, static_cast<int>(mi) == findCurrentMatch_ ? 120 : 60));
        }
    }
} // namespace imshell

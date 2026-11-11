// mydear::TextEditor on a bare imgui context, driven with imgui's own input events: editing,
// selection, undo, the highlighter and completion hooks. the test engine's item hook (compiled
// into this test's own imgui) fails a duplicated item id. built with -DMYDEAR_BUILD_TESTS=ON
#include "mydear/editor.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cctype>
#include <cstdio>
#include <set>
#include <string>

namespace {
    std::set<ImGuiID> seen; // this frame's item ids
    int failures = 0;
} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect&,
                                 const ImGuiLastItemData*) {
    if (id && !seen.insert(id).second) {
        std::fprintf(stderr, "duplicate id 0x%08x\n", id);
        ++failures;
    }
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) {
    return nullptr;
}

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_TEXT(editor, expected)                                                               \
    do {                                                                                           \
        if ((editor).GetText() != (expected)) {                                                    \
            std::fprintf(stderr, "FAIL line %d: text is \"%s\"\n", __LINE__,                       \
                         (editor).GetText().c_str());                                              \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

namespace {
    using mydear::TextEditor;
    using Token = TextEditor::Token;

    TextEditor* editor = nullptr; // the one drawn (and focused)
    TextEditor* second = nullptr; // a second one in the same window

    void frames(int n = 3) {
        for (int i = 0; i < n; ++i) {
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            seen.clear();
            ImGui::NewFrame();
            GImGui->TestEngineHookItems = true;
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(800, 600));
            ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
            editor->Render("editor", ImVec2(-1, 250));
            if (second)
                second->Render("second", ImVec2(-1, 100), true);
            ImGui::End();
            ImGui::Render();
        }
    }

    void key(ImGuiKey k, ImGuiKey mod = ImGuiKey_None, ImGuiKey mod2 = ImGuiKey_None) {
        ImGuiIO& io = ImGui::GetIO();
        for (const bool down : {true, false}) {
            if (mod != ImGuiKey_None)
                io.AddKeyEvent(mod, down);
            if (mod2 != ImGuiKey_None)
                io.AddKeyEvent(mod2, down);
            frames(1);
            io.AddKeyEvent(k, down);
            frames(2);
        }
    }

    void type(const char* text) {
        ImGui::GetIO().AddInputCharactersUTF8(text);
        frames();
    }

    void click(ImVec2 pos) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(pos.x, pos.y);
        frames(2);
        io.AddMouseButtonEvent(0, true);
        frames(2);
        io.AddMouseButtonEvent(0, false);
        frames(2);
    }

    // a fresh, focused editor
    TextEditor& start(TextEditor& e, const std::string& text = "") {
        editor = &e;
        e.SetText(text);
        e.SetFocus();
        frames();
        return e;
    }

    void editing() {
        TextEditor e;
        start(e, "ab");
        CHECK(e.GetCursorIndex() == 2 && !e.IsContentDirty() && !e.HasSelection());
        type("c");
        CHECK_TEXT(e, "abc");
        CHECK(e.IsContentDirty());
        type("\xc3\xa9"); // two bytes, one character
        CHECK(e.GetCursorIndex() == 5);
        key(ImGuiKey_Backspace);
        CHECK_TEXT(e, "abc");
        key(ImGuiKey_Home);
        key(ImGuiKey_Delete);
        CHECK_TEXT(e, "bc");

        // pairs: typed together, stepped over, deleted together
        start(e);
        type("(");
        CHECK_TEXT(e, "()");
        CHECK(e.GetCursorIndex() == 1);
        type(")");
        CHECK_TEXT(e, "()");
        CHECK(e.GetCursorIndex() == 2);
        key(ImGuiKey_LeftArrow);
        key(ImGuiKey_Backspace);
        CHECK_TEXT(e, "");

        // enter keeps the indentation, and opens a pair over three lines
        start(e, "  foo");
        key(ImGuiKey_Enter);
        CHECK_TEXT(e, "  foo\n  ");
        type("{");
        key(ImGuiKey_Enter);
        CHECK_TEXT(e, "  foo\n  {\n      \n  }");
        e.SetIndentWords({"begin"});
        start(e, "BEGIN");
        key(ImGuiKey_Enter);
        CHECK_TEXT(e, "BEGIN\n    ");

        // ctrl+backspace deletes a word, ctrl+shift+backspace to the line start
        start(e, "one 1;\nword foo_bar,  baz.qux  ");
        key(ImGuiKey_Backspace, ImGuiMod_Ctrl); // trailing spaces and "qux"
        CHECK_TEXT(e, "one 1;\nword foo_bar,  baz.");
        key(ImGuiKey_Backspace, ImGuiMod_Ctrl); // "."
        CHECK_TEXT(e, "one 1;\nword foo_bar,  baz");
        key(ImGuiKey_Backspace, ImGuiMod_Ctrl, ImGuiMod_Shift);
        CHECK_TEXT(e, "one 1;\n");
        key(ImGuiKey_Backspace, ImGuiMod_Ctrl, ImGuiMod_Shift); // at the line start: joins
        CHECK_TEXT(e, "one 1;");

        // tab indents the selected lines, ctrl+/ comments them
        e.SetLineComment("//");
        start(e, "a\nb");
        key(ImGuiKey_A, ImGuiMod_Ctrl);
        key(ImGuiKey_Tab);
        CHECK_TEXT(e, "\ta\n\tb");
        key(ImGuiKey_Tab, ImGuiMod_Shift);
        CHECK_TEXT(e, "a\nb");
        key(ImGuiKey_A, ImGuiMod_Ctrl);
        key(ImGuiKey_Slash, ImGuiMod_Ctrl);
        CHECK_TEXT(e, "// a\n// b");
        key(ImGuiKey_A, ImGuiMod_Ctrl);
        key(ImGuiKey_Slash, ImGuiMod_Ctrl);
        CHECK_TEXT(e, "a\nb");
    }

    void selection() {
        TextEditor e;
        start(e, "hello world\nbye");
        key(ImGuiKey_UpArrow);
        key(ImGuiKey_Home);
        CHECK(e.GetCursorIndex() == 0);
        key(ImGuiKey_RightArrow, ImGuiMod_Ctrl, ImGuiMod_Shift);
        CHECK(e.HasSelection() && e.GetSelectedText() == "hello");
        key(ImGuiKey_RightArrow); // collapses to the selection's end
        CHECK(!e.HasSelection() && e.GetCursorIndex() == 5);
        key(ImGuiKey_End, ImGuiMod_Shift);
        CHECK(e.GetSelectedText() == " world");
        key(ImGuiKey_DownArrow, ImGuiMod_Shift);
        CHECK(e.GetSelectedText() == " world\nbye");
        key(ImGuiKey_A, ImGuiMod_Ctrl);
        CHECK(e.GetSelectedText() == e.GetText());
        type("x"); // typing replaces the selection
        CHECK_TEXT(e, "x");

        // read-only: selects, never edits
        start(e, "fixed");
        e.SetReadOnly(true);
        type("z");
        key(ImGuiKey_Backspace);
        key(ImGuiKey_Enter);
        CHECK_TEXT(e, "fixed");
        key(ImGuiKey_A, ImGuiMod_Ctrl);
        CHECK(e.GetSelectedText() == "fixed" && !e.IsContentDirty());
    }

    void undo() {
        TextEditor e;
        start(e, "a");
        type("b");
        type("c");
        CHECK_TEXT(e, "abc");
        key(ImGuiKey_Z, ImGuiMod_Ctrl);
        CHECK_TEXT(e, "ab");
        key(ImGuiKey_Z, ImGuiMod_Ctrl);
        CHECK_TEXT(e, "a");
        key(ImGuiKey_Z, ImGuiMod_Ctrl); // nothing older
        CHECK_TEXT(e, "a");
        key(ImGuiKey_Z, ImGuiMod_Ctrl, ImGuiMod_Shift);
        CHECK_TEXT(e, "ab");
        type("z"); // an edit drops the redo history
        key(ImGuiKey_Z, ImGuiMod_Ctrl, ImGuiMod_Shift);
        CHECK_TEXT(e, "abz");
        e.SetText("new"); // and SetText the undo history
        frames();
        key(ImGuiKey_Z, ImGuiMod_Ctrl);
        CHECK_TEXT(e, "new");
    }

    void highlighting() {
        // a fake lexer: digits are numbers, "let" is a keyword; it records what it was handed
        int runs = 0;
        TextEditor::Edit lastEdit;
        std::vector<Token> onEntry;
        TextEditor e;
        e.SetHighlighter([&](std::string_view text, std::span<Token> tokens,
                             const TextEditor::Edit& edit) {
            ++runs;
            lastEdit = edit;
            onEntry.assign(tokens.begin(), tokens.end());
            std::fill(tokens.begin(), tokens.end(), Token::Text);
            for (size_t i = 0; i < text.size(); ++i) {
                if (std::isdigit(static_cast<unsigned char>(text[i])))
                    tokens[i] = Token::Number;
            }
            for (size_t at = 0; (at = text.find("let", at)) != std::string_view::npos; at += 3)
                tokens[at] = tokens[at + 1] = tokens[at + 2] = Token::Keyword;
        });
        start(e, "let a = 42");
        const std::vector<Token> expected = {Token::Keyword, Token::Keyword, Token::Keyword,
                                             Token::Text,    Token::Text,    Token::Text,
                                             Token::Text,    Token::Text,    Token::Number,
                                             Token::Number};
        CHECK(e.GetTokens() == expected);
        const int before = runs;
        frames(5); // not again while nothing changes
        CHECK(runs == before);
        type("7");
        CHECK(runs == before + 1);
        CHECK(e.GetTokens().size() == e.GetText().size() && e.GetTokens().back() == Token::Number);
        // incremental: the edit, and the earlier kinds kept around it, moved with it
        CHECK(lastEdit.start == 10 && lastEdit.oldEnd == 10 && lastEdit.newEnd == 11);
        CHECK(std::equal(expected.begin(), expected.end(), onEntry.begin()) &&
              onEntry[10] == Token::Text);
        e.SetText("x let a = 427");
        e.GetTokens();
        CHECK(lastEdit.start == 0 && lastEdit.oldEnd == 0 && lastEdit.newEnd == 2);
        CHECK(onEntry[0] == Token::Text && onEntry[2] == Token::Keyword &&
              onEntry[12] == Token::Number);
        e.SetHighlighter([&](std::string_view, std::span<Token> tokens, const TextEditor::Edit& edit) {
            lastEdit = edit;
            onEntry.assign(tokens.begin(), tokens.end());
        });
        e.GetTokens(); // a new highlighter starts over: everything new, all Text
        CHECK(lastEdit.start == 0 && lastEdit.oldEnd == 0 && lastEdit.newEnd == 13);
        CHECK(onEntry == std::vector<Token>(13, Token::Text));

        // colours: the theme's, or the host's palette
        const auto theme = TextEditor::FromTheme(mydear::Theme::Current()->colors);
        CHECK(e.TokenColor(Token::Number) == theme.number);
        CHECK(e.TokenColor(Token::Keyword) == theme.keyword && theme.keyword != theme.text);
        auto palette = theme;
        palette.number = IM_COL32(1, 2, 3, 255);
        e.SetPalette(palette);
        CHECK(e.TokenColor(Token::Number) == IM_COL32(1, 2, 3, 255));

        TextEditor plain; // no highlighter: all text
        plain.SetText("let 1");
        CHECK(plain.GetTokens() == std::vector<Token>(5, Token::Text));
    }

    void completion() {
        // a fake provider: the words starting with the one before the cursor
        bool wasForced = false;
        TextEditor e;
        e.SetCompletionProvider([&](std::string_view content, int cursor, bool forced) {
            wasForced = forced;
            int from = cursor;
            while (from > 0 && std::isalpha(static_cast<unsigned char>(content[from - 1])))
                --from;
            const std::string_view word = content.substr(from, cursor - from);
            TextEditor::CompletionResponse response{.replaceStart = from, .replaceEnd = cursor};
            for (const char* w : {"alpha", "alpine", "beta"}) {
                if (std::string_view(w).starts_with(word))
                    response.items.push_back(
                        {.text = w, .detailText = "word", .icon = "W", .iconColor = Token::Type});
            }
            return response;
        });
        start(e);
        CHECK(e.AutocompleteRowPos(0).x == 0);
        type("al");
        CHECK(e.AutocompleteRowPos(0).x > 0 && !wasForced);
        key(ImGuiKey_DownArrow); // the second row
        key(ImGuiKey_Enter);
        CHECK_TEXT(e, "alpine");
        CHECK(e.AutocompleteRowPos(0).x == 0);

        type(" al"); // a click on a row picks it
        click(e.AutocompleteRowPos(0));
        CHECK_TEXT(e, "alpine alpha");

        type(" b");
        key(ImGuiKey_Escape); // closes it; enter is a newline again
        CHECK(e.AutocompleteRowPos(0).x == 0);
        key(ImGuiKey_Enter);
        CHECK_TEXT(e, "alpine alpha b\n");

        key(ImGuiKey_Period, ImGuiMod_Ctrl); // forced: opens on an empty word
        CHECK(wasForced && e.AutocompleteRowPos(2).x > 0);
        key(ImGuiKey_UpArrow); // wraps to the last
        key(ImGuiKey_Tab);
        CHECK_TEXT(e, "alpine alpha b\nbeta");

        type(" zz"); // no items: no popup
        CHECK(e.AutocompleteRowPos(0).x == 0);
    }

    // everything drawn at once, two editors in one window: no duplicated id, no imgui assert
    void ime() {
        // a focused editor is a text field to the platform: the ime and the soft keyboard follow
        // its caret and the shells underline a composition there
        TextEditor e;
        start(e, "ab");
        const ImGuiPlatformImeData& data = GImGui->PlatformImeData;
        CHECK(data.WantTextInput && data.WantVisible && data.InputLineHeight > 0);
        CHECK(ImGui::GetIO().WantTextInput);
        type("c"); // its own flag does not keep it from typing
        CHECK_TEXT(e, "abc");
        // an imgui field (the find bar) has the characters while it is active
        key(ImGuiKey_F, ImGuiMod_Ctrl);
        type("zz");
        CHECK_TEXT(e, "abc");
        key(ImGuiKey_Escape);
        frames();
        type("d");
        CHECK_TEXT(e, "abcd");
        e.SetReadOnly(true); // nothing to type into: no keyboard
        frames();
        CHECK(!GImGui->PlatformImeData.WantTextInput);
    }

    void everything() {
        TextEditor e, other;
        e.SetPlaceholder("type here");
        e.SetRunnableRanges(
            [](std::string_view text) {
                std::vector<std::pair<size_t, size_t>> ranges;
                for (size_t at = 0; at < text.size();) {
                    const size_t end = std::min(text.find('\n', at), text.size());
                    ranges.emplace_back(at, end);
                    at = end + 1;
                }
                return ranges;
            },
            [](size_t, size_t) {});
        other.SetReadOnly(true);
        other.SetText("read\n\tonly");
        second = &other;
        start(e);
        start(e, "foo bar\nfoo");
        e.SetErrorMarker(4, "unexpected bar");
        CHECK(e.GetErrorOffset() == 4);
        frames();

        // find: ctrl+f starts from the selection, f3 steps through the matches
        key(ImGuiKey_LeftArrow, ImGuiMod_Ctrl, ImGuiMod_Shift);
        CHECK(e.GetSelectedText() == "foo");
        key(ImGuiKey_F, ImGuiMod_Ctrl);
        CHECK(e.GetSelectedText() == "foo" && e.GetCursorIndex() == 11);
        key(ImGuiKey_F3);
        CHECK(e.GetSelectedText() == "foo" && e.GetCursorIndex() == 3);
        key(ImGuiKey_Escape);
        frames();
        second = nullptr;
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.IniFilename = nullptr;
    io.ConfigMacOSXBehaviors = false; // the same chords on every platform
    io.Fonts->Build();
    editing();
    selection();
    undo();
    highlighting();
    completion();
    ime();
    everything();
    ImGui::DestroyContext();
    if (failures) {
        std::fprintf(stderr, "FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("editor: ok\n");
    return 0;
}

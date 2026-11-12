#pragma once

#include <string>
#include <string_view>

namespace imshell::detail {
    // the ime's composing text (utf-8) sits right before the caret of the active text field:
    // underline it there, as a native field does. call between the frame and ImGui::Render()
    void drawComposingUnderline(const char* text);

    // a composition typed into the active field as it goes, so the field shows it, and edited
    // in place on every update (backspaces for what changed, then the new text). works for any
    // widget that reads imgui's characters and keys: InputText and TextEditor alike
    namespace ime {
        void compose(std::string_view utf8); // the composing text now; "" removes it
        void commit(std::string_view utf8);  // the final text replaces the composition
        void forget();                       // the field went away: keep what it shows
        bool composing();
        const std::string& text();           // the composition, for the underline

        // the active InputText's text and caret (bytes), for imes that read around the caret;
        // false for other widgets and for password fields, which must not be read
        bool field(std::string_view& text, int& caret, int& selStart, int& selEnd);
        // utf-16 units in s (apple, windows and the web count in them)
        int utf16Length(std::string_view s);
        // the byte offset of utf-16 index i in s (clamped)
        int utf16ToByte(std::string_view s, int i);
        // remove n codepoints before the caret (an ime replacing text it already sent)
        void backspace(int n);
        int codepoints(std::string_view s);
        bool trace(); // IMSHELL_IME_TRACE=1: log what the ime sends, to stderr
    } // namespace ime
} // namespace imshell::detail

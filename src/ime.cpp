#include "ime.hpp"

#include "imgui_internal.h"
#include "imshell/frames.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace imshell::detail {
    void drawComposingUnderline(const char* text) {
        const ImGuiPlatformImeData& ime = GImGui->PlatformImeData; // this frame's, set by the field
        if (!text || !*text || !ime.WantTextInput)
            return;
        const float size = ime.InputLineHeight; // the field's font size
        // shortcut: measured in the default font; a field in another font gets a line off by its
        // width difference, and a composition scrolled out of a narrow field is not clipped
        const float width = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text).x;
        const float caret = ime.InputPos.x + 1; // imgui puts InputPos 1 px left of the caret
        const float y = ime.InputPos.y + size;  // InputPos.y is the line's top
        ImGui::GetForegroundDrawList(ImGui::FindViewportByID(ime.ViewportId))
            ->AddLine(ImVec2(caret - width, y), ImVec2(caret, y), ImGui::GetColorU32(ImGuiCol_Text),
                      std::max(1.0f, size / 14));
    }

    namespace ime {
        namespace {
            std::string gText; // what the field shows of the composition

            bool continuation(char c) {
                return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
            }

            // bytes of the longest common prefix of a and b, whole codepoints only
            size_t commonPrefix(std::string_view a, std::string_view b) {
                size_t n = 0;
                while (n < a.size() && n < b.size() && a[n] == b[n])
                    ++n;
                while (n > 0 && n < a.size() && continuation(a[n]))
                    --n;
                return n;
            }

            // the field shows `from`: make it show `to`
            void retype(std::string_view from, std::string_view to) {
                const size_t keep = commonPrefix(from, to);
                backspace(codepoints(from.substr(keep)));
                if (keep < to.size())
                    ImGui::GetIO().AddInputCharactersUTF8(std::string(to.substr(keep)).c_str());
            imshell::requestFrames();
            }
        } // namespace

        int codepoints(std::string_view s) {
            return static_cast<int>(std::count_if(s.begin(), s.end(), [](char c) { return !continuation(c); }));
        }

        void backspace(int n) {
            ImGuiIO& io = ImGui::GetIO();
            for (; n > 0; --n) {
                io.AddKeyEvent(ImGuiKey_Backspace, true);
                io.AddKeyEvent(ImGuiKey_Backspace, false);
            }
        }

        void compose(std::string_view utf8) {
            if (trace())
                std::fprintf(stderr, "ime: compose \"%.*s\"\n", static_cast<int>(utf8.size()), utf8.data());
            retype(gText, utf8);
            gText = utf8;
        }

        void commit(std::string_view utf8) {
            if (trace())
                std::fprintf(stderr, "ime: commit \"%.*s\"\n", static_cast<int>(utf8.size()), utf8.data());
            retype(gText, utf8);
            gText.clear();
        }

        void forget() {
            gText.clear();
        }

        bool composing() {
            return !gText.empty();
        }

        const std::string& text() {
            return gText;
        }

        bool field(std::string_view& text, int& caret, int& selStart, int& selEnd) {
            ImGuiContext& g = *GImGui;
            ImGuiInputTextState* s = ImGui::GetInputTextState(g.ActiveId);
            if (!s || (s->Flags & ImGuiInputTextFlags_Password))
                return false;
            text = std::string_view(s->TextA.Data ? s->TextA.Data : "", s->TextLen);
            caret = std::clamp(s->GetCursorPos(), 0, s->TextLen);
            selStart = std::clamp(s->GetSelectionStart(), 0, s->TextLen);
            selEnd = std::clamp(s->GetSelectionEnd(), 0, s->TextLen);
            if (selStart > selEnd)
                std::swap(selStart, selEnd);
            if (selStart == selEnd) // no selection: stb leaves both wherever, the caret is what counts
                selStart = selEnd = caret;
            return true;
        }

        int utf16Length(std::string_view s) {
            int n = 0;
            for (const char c : s) {
                const auto b = static_cast<unsigned char>(c);
                if ((b & 0xC0) != 0x80)
                    n += b >= 0xF0 ? 2 : 1; // 4-byte sequences are surrogate pairs
            }
            return n;
        }

        int utf16ToByte(std::string_view s, int i) {
            size_t at = 0;
            for (int units = 0; at < s.size() && units < i;) {
                const auto b = static_cast<unsigned char>(s[at]);
                units += b >= 0xF0 ? 2 : 1;
                ++at;
                while (at < s.size() && continuation(s[at]))
                    ++at;
            }
            return static_cast<int>(at);
        }

        bool trace() {
            static const bool on = [] {
                const char* v = std::getenv("IMSHELL_IME_TRACE");
                return v && *v && *v != '0';
            }();
            return on;
        }
    } // namespace ime
} // namespace imshell::detail

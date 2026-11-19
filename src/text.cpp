#include "imshell/text.hpp"

#include "imgui_internal.h"

#include <vector>

namespace imshell {
    namespace {
        float widthOf(std::string_view s) {
            return ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
        }
    } // namespace

    std::string Ellipsize(std::string_view text, const float maxWidth) {
        if (widthOf(text) <= maxWidth)
            return std::string(text);
        const bool glyph = ImGui::GetFont() && ImGui::GetFont()->IsGlyphInFont(0x2026);
        const std::string_view dots = glyph ? "\xE2\x80\xA6" : "...";
        const float room = maxWidth - widthOf(dots);
        if (room <= 0)
            return std::string(dots); // not even one character fits: the mark alone
        // where each character starts (a UTF-8 continuation byte does not start one)
        std::vector<size_t> starts;
        for (size_t i = 0; i < text.size(); ++i)
            if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
                starts.push_back(i);
        // the most characters whose prefix fits
        size_t lo = 0, hi = starts.size(); // a prefix of `n` characters ends at starts[n] (or the end)
        while (lo < hi) {
            const size_t mid = (lo + hi + 1) / 2;
            const size_t end = mid < starts.size() ? starts[mid] : text.size();
            if (widthOf(text.substr(0, end)) <= room)
                lo = mid;
            else
                hi = mid - 1;
        }
        std::string out(text.substr(0, lo < starts.size() ? starts[lo] : text.size()));
        out += dots;
        return out;
    }

    bool TextEllipsis(std::string_view text, float maxWidth) {
        if (maxWidth <= 0)
            maxWidth = ImGui::GetContentRegionAvail().x;
        const std::string shown = Ellipsize(text, maxWidth);
        const bool cut = shown != text;
        ImGui::TextUnformatted(shown.c_str(), shown.c_str() + shown.size());
        if (cut && ImGui::IsItemHovered())
            ImGui::SetTooltip("%.*s", static_cast<int>(text.size()), text.data());
        return cut;
    }
} // namespace imshell

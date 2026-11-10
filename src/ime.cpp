#include "ime.hpp"

#include "imgui_internal.h"

#include <algorithm>

namespace mydear::detail {
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
} // namespace mydear::detail

#include "imshell/toggle_button.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imshell/themes.hpp"

void imshell::ToggleButton(const char* str_id, bool* v) {
    const Theme& t = CurrentTheme();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const float height = ImGui::GetFrameHeight();
    const float width = height * 1.55f;
    const float radius = height * 0.50f;

    ImGui::InvisibleButton(str_id, ImVec2(width, height));
    if (ImGui::IsItemClicked())
        *v = !*v;

    // always drawn: a toggle that starts on needs its track before the first click. a finger's hover
    // is not a hover (it would stay lit after a tap)
    const bool hovered =
        ImGui::IsItemHovered() && ImGui::GetIO().MouseSource != ImGuiMouseSource_TouchScreen;
    const ImVec4 track = *v ? t.primary : t.border;
    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                             ImGui::GetColorU32(hovered ? Hovered(track) : track), height * 0.5f);

    draw_list->AddCircleFilled(
        ImVec2(p.x + radius + (*v ? 1.0f : 0.0f) * (width - radius * 2.0f), p.y + radius),
        radius - 1.5f, ImGui::GetColorU32(t.foreground));
}

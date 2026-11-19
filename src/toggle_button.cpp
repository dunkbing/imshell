#include "imshell/toggle_button.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imshell/animation.hpp"
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
    // the track's color and the knob's place ease with a spring (kept by the toggle's id)
    const ImGuiID id = ImGui::GetItemID();
    const ImVec4 track = *v ? t.primary : t.border;
    const ImVec4 fill = AnimateColor(ImHashStr("imshell.toggle.fill", 0, id), hovered ? Hovered(track) : track);
    const float place = Animate(ImHashStr("imshell.toggle.knob", 0, id), *v ? 1.0f : 0.0f);
    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(fill), height * 0.5f);

    draw_list->AddCircleFilled(
        ImVec2(p.x + radius + place * (width - radius * 2.0f), p.y + radius),
        radius - 1.5f, ImGui::GetColorU32(t.foreground));
}

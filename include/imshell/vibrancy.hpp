#pragma once

#include "imgui.h"

// a window material behind part of the imgui content: the blurred, tinted look of a macos
// sidebar, which shows the desktop through the window. the material is drawn by the os under
// the imgui layer, so the area must be drawn with a transparent background:
//
//   if (imshell::VibrancyAvailable()) {
//       imshell::Vibrancy(pos, size);                       // every frame the area is shown
//       ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
//   } else {
//       // the usual opaque sidebar
//   }
//
// one region per window; a frame that does not call it removes it. only macos has it for now:
// elsewhere VibrancyAvailable() is false and Vibrancy does nothing, so the host keeps its own
// opaque sidebar. while a region shows, the window is cleared transparent (the window's own
// background colour shows through), so windows that cover the whole area should draw their
// own background
namespace imshell {
    // the os's names for the materials
    enum class VibrancyMaterial { Sidebar, Header, Menu, Popover, Titlebar, Window, Content, Hud, Sheet };

    bool VibrancyAvailable();
    void Vibrancy(ImVec2 pos, ImVec2 size, VibrancyMaterial material = VibrancyMaterial::Sidebar);
} // namespace imshell

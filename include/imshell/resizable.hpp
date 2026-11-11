#pragma once

#include "imgui.h"

// panels side by side (or stacked) with a handle on each boundary between two of them, dragged
// to resize the pair. sizes are in pixels. a panel with no size shares what is left over; when
// the group grows or shrinks, the panels that are not fixed take the change in proportion to
// their sizes. hidden panels take no room and come back at their old size.
//
//   static float sidebar = 260, files = 360;
//   if (imshell::BeginResizable("main")) {
//       if (imshell::ResizablePanel({.size = &sidebar, .min = 200, .fixed = true})) { Sidebar(); }
//       if (imshell::ResizablePanel({.min = 420})) { Chat(); }
//       if (imshell::ResizablePanel({.size = &files, .min = 300, .visible = filesOpen})) { Files(); }
//       if (imshell::EndResizable()) { Save(sidebar, files); } // a drag just ended
//   }
//
// groups nest: a group can be begun inside a panel
namespace imshell {
    enum class Axis {
        Horizontal, // panels left to right, handles are vertical lines
        Vertical,   // panels top to bottom
    };

    struct ResizableOptions {
        Axis axis = Axis::Horizontal;
        ImVec2 size = ImVec2(0, 0); // as BeginChild: 0 = the room left
        bool disabled = false;      // handles drawn, not draggable
        float hitSize = 8;          // the band around a handle that takes the mouse
        ImU32 line = 0;             // the handle's colour at rest; 0 = ImGuiCol_Separator
        ImU32 lit = 0;              // while hovered or dragged; 0 = ImGuiCol_SeparatorActive
    };

    struct PanelOptions {
        // pixels, read and written: where the panel's size lives, so it can be saved. read
        // when the group first sees the panel (0 or less: no size yet) and whenever the caller
        // changes it; written as the panel is resized. null: the group keeps it
        float* size = nullptr;
        float min = 0;
        float max = 0;      // 0: no limit
        bool fixed = false; // keeps its size when the group grows or shrinks (its handle still moves it)
        bool visible = true;
        ImGuiChildFlags childFlags = 0;
        ImGuiWindowFlags windowFlags = 0;
    };

    // false: nothing to draw (no room); EndResizable is not called then
    bool BeginResizable(const char* id, const ResizableOptions& options = {});
    // the next panel: begins its child window. false while hidden: draw nothing for it
    bool ResizablePanel(const PanelOptions& options = {});
    // true once when a drag ends: the sizes are final, a time to save them
    bool EndResizable();
} // namespace imshell

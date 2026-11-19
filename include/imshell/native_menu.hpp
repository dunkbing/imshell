#pragma once

#include "imgui.h"
#include <functional>
#include <string>
#include <vector>

// a context menu the os draws and tracks (an NSMenu on macos), with imshell's own menu as the
// fallback where a platform has no backend, so a caller writes one thing:
//
//   ImGui::Selectable(name);
//   switch (imshell::NativeContextMenu("##row", [&] {
//       return std::vector<imshell::NativeMenuItem>{
//           {.label = "Copy", .id = 1, .icon = "doc.on.doc"},
//           {.label = "Rename", .id = 2},
//           imshell::NativeMenuItem::Separator(),
//           {.label = "Delete", .id = 3, .enabled = canDelete},
//       };
//   })) {
//   case 1: copy(); break;
//   case 3: remove(); break;
//   }
//
// call it right after the item the menu belongs to, every frame: a right click on that item opens
// it (a long press on a touch screen), and the id of the chosen item comes back once, otherwise
// -1. the native menu tracks the mouse in the os's own loop after the frame is drawn, so the
// choice reaches the app in the next frame, and the builder runs only when it opens; the fallback
// builds the items every frame it is open. a native menu has the os's look, shortcuts and
// accessibility, and cannot show imgui content inside it
namespace imshell {
    struct NativeMenuItem {
        std::string label;
        int id = 0;                          // what the call returns when this is chosen (0 or more)
        bool enabled = true;
        bool checked = false;
        std::string icon;                    // an SF Symbol name (imshell/icons.hpp), where the platform has one
        std::vector<NativeMenuItem> children; // a submenu
        bool separator = false;

        static NativeMenuItem Separator() {
            NativeMenuItem item;
            item.separator = true;
            return item;
        }
    };

    // true where NativeContextMenu is the os's menu
    bool NativeMenusAvailable();

    // the id of the item chosen from the menu `id`, once; -1 otherwise
    int NativeContextMenu(const char* id, const std::function<std::vector<NativeMenuItem>()>& items);
} // namespace imshell

#pragma once

#include "imshell/native_menu.hpp"

#include <vector>

// what a platform does for imshell/native_menu.hpp
namespace imshell::detail {
    bool nativeMenusAvailable();

    // shows the menu at `pos` (imgui window coordinates) and tracks it in the os's loop until it
    // closes: the chosen id, or -1
    int nativeMenuPopUp(const std::vector<NativeMenuItem>& items, ImVec2 pos);

    // for tests: builds the menu and picks the item at `path` (indices into each level) as a
    // click would, with no window; the id, or -1 for none
    int nativeMenuPick(const std::vector<NativeMenuItem>& items, const std::vector<int>& path);

    // the shell calls it after a frame: opens the menus the frame asked for
    void runNativeMenus();
} // namespace imshell::detail

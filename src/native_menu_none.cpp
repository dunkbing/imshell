#include "native_menu.hpp"

namespace imshell::detail {
    // shortcut: no native menu here yet (win32 TrackPopupMenu, gtk popover menus, uikit UIMenu), so imshell's own draws; add a backend per platform
    bool nativeMenusAvailable() {
        return false;
    }

    int nativeMenuPopUp(const std::vector<NativeMenuItem>&, ImVec2) {
        return -1;
    }

    int nativeMenuPick(const std::vector<NativeMenuItem>&, const std::vector<int>&) {
        return -1;
    }
} // namespace imshell::detail

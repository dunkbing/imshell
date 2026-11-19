#include "native_controls.hpp"

namespace imshell::detail {
    // shortcut: no native controls here yet (gtk has GtkSwitch, win32 no switch), so imshell's own draw; add a backend per platform
    bool nativeControlsAvailable() {
        return false;
    }

    bool nativeSwitch(const char*, bool*) {
        return false;
    }

    bool nativeSegmented(const char*, std::span<const char* const>, int*) {
        return false;
    }
} // namespace imshell::detail

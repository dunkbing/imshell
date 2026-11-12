#pragma once

#include "imgui.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace imshell {
    // whether the key that makes shortcuts (cmd on macos, ctrl elsewhere) is held right now.
    // on macos it asks the system, not the key events the window got: after a screenshot
    // shortcut or an app switch the window can miss a modifier, and imgui then sees cmd+v as
    // a bare v (ctrl is 0 in io.KeyCtrl though the user holds cmd)
#if defined(__APPLE__) && TARGET_OS_OSX
    bool ShortcutKeyDown(); // macos/app.mm
#else
    inline bool ShortcutKeyDown() {
        return ImGui::GetIO().KeyCtrl;
    }
#endif
} // namespace imshell

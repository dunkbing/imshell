#pragma once

#include "imgui.h"

namespace imshell::detail {
    // true when drawData paints exactly what the last frame given here did, so the shell can
    // skip drawing and presenting it. never true while a texture is being uploaded, a draw
    // command has a user callback, or the display or clear colour changed
    bool sameAsLastFrame(const ImDrawData* drawData, ImVec4 clear);
    // the shell skipped this frame's present: the glfw loop waits a refresh instead of spinning
    bool& frameSkipped();
} // namespace imshell::detail

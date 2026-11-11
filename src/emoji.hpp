#pragma once

#include "imgui.h"

namespace imshell::detail {
    // a font loader for one merged source: the system's colour emoji, drawn by the platform's
    // own text engine. null where there is none yet
    const ImFontLoader* EmojiLoader();
} // namespace imshell::detail

#pragma once

#include "imgui.h"

#include <string>

struct ImFontLoader;

namespace imshell::detail {
    // first codepoint of the icon block (supplementary private use area-A)
    constexpr unsigned kIconBase = 0xF0000;

    // a font loader for one merged source: the platform's own icons, drawn by its engine. null
    // where there is none
    const ImFontLoader* IconLoader();

    // whether the platform has an icon of this name
    bool IconAvailable(const char* name);

    // the name a codepoint in the block was handed out for, or null
    const std::string* IconName(unsigned codepoint);
} // namespace imshell::detail

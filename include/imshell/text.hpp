#pragma once

#include "imgui.h"

#include <string>
#include <string_view>

// text that must fit a width, for list rows and labels
namespace imshell {
    // `text` cut to whole characters (never inside a UTF-8 sequence) with an ellipsis so it fits
    // `maxWidth` pixels in the current font; the text itself when it already fits. the ellipsis
    // is U+2026 where the font has it, else "...". call inside a frame
    std::string Ellipsize(std::string_view text, float maxWidth);

    // draws one line of text that fits `maxWidth` (0 = the rest of the line), cut with an
    // ellipsis; a cut line shows the whole text in a tooltip while hovered. an item like Text,
    // so it can be followed by SameLine. true when the text was cut
    bool TextEllipsis(std::string_view text, float maxWidth = 0);
} // namespace imshell

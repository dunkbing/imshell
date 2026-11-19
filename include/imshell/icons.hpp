#pragma once

#include "imgui.h"

// the platform's own icons as glyphs: ask by SF Symbol name, get a string for text, labels and
// buttons. they are tinted by the text color, scale with the font and need no image files
//
//   imshell::IconButton(imshell::Icon("trash", kFaTrash), imshell::ButtonVariant::Destructive);
//   ImGui::Text("%s Settings", imshell::Icon("gearshape"));
//
// apple draws the SF Symbol of that name. elsewhere the platform has no loader yet, so Icon
// returns `fallback`, the host's own icon font glyph (or null). AddFont merges the source into
// its font; a font made some other way calls MergeIcons. needs IMGUI_USE_WCHAR32 (imshell's
// build sets it): the glyphs live in the supplementary private use area, U+F0000 and up, where
// they cannot collide with an icon font in the ordinary one
namespace imshell {
    // a glyph for `name` that stays valid for the life of the process, or `fallback` when this
    // platform cannot draw it (an unknown name, or no loader here)
    const char* Icon(const char* name, const char* fallback = nullptr);

    // the icon source merged into `font` (once; AddFont does it for its own)
    void MergeIcons(ImFont* font);
} // namespace imshell

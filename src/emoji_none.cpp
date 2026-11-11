#include "emoji.hpp"

namespace imshell::detail {
    const ImFontLoader* EmojiLoader() {
        return nullptr; // shortcut: macos and ios only; freetype with the system's colour font elsewhere
    }
} // namespace imshell::detail

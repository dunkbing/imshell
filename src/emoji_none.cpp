#include "emoji.hpp"

namespace imshell::detail {
    const ImFontLoader* EmojiLoader() {
        return nullptr; // shortcut: the web (a canvas) and linux without freetype draw no emoji yet
    }
} // namespace imshell::detail

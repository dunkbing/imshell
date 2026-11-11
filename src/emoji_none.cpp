#include "emoji.hpp"

namespace imshell::detail {
    const ImFontLoader* EmojiLoader() {
        return nullptr; // shortcut: windows (directwrite) and the web (a canvas) draw no emoji yet
    }
} // namespace imshell::detail

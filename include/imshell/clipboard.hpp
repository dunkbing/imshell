#pragma once

#include <string>
#include <vector>

// what the clipboard holds besides text (text is imgui's: GetClipboardText). for a paste
// into a chat box or an image field. macos only so far: empty on the other platforms.
// ui thread
namespace imshell {
    // the picture on the clipboard (a screenshot, a copied image) as png bytes; empty = none
    std::string ClipboardImage();
    // the files copied in the file manager, as paths; empty = none
    std::vector<std::string> ClipboardFiles();
} // namespace imshell

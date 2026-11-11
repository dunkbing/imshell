#include "imshell/clipboard.hpp"

// no pictures or files from the clipboard on this platform yet
namespace imshell {
    std::string ClipboardImage() {
        return {};
    }
    std::vector<std::string> ClipboardFiles() {
        return {};
    }
} // namespace imshell

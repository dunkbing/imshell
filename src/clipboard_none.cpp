#include "imshell/clipboard.hpp"

// no pictures or files on this platform's clipboard yet
namespace imshell::Clipboard {
    std::string ReadImage() {
        return {};
    }
    void WriteImage(const std::string&) {}
    std::vector<std::string> ReadFiles() {
        return {};
    }
} // namespace imshell::Clipboard

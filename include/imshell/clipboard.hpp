#pragma once

#include <string>
#include <vector>

// the system clipboard: read and write, per kind of content. a read gives empty when the
// clipboard holds none of that kind. ui thread.
// text works everywhere; pictures and files on macos so far (empty / ignored elsewhere)
namespace imshell::Clipboard {
    std::string ReadText();
    void WriteText(const std::string& text);

    // a picture (a screenshot, a copied image), as png bytes
    std::string ReadImage();
    void WriteImage(const std::string& png);

    // the files copied in the file manager, as paths
    std::vector<std::string> ReadFiles();
} // namespace imshell::Clipboard

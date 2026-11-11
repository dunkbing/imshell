#pragma once

#include <map>
#include <string>
#include <vector>

// the system clipboard, shaped like the web's Clipboard API: Read and Write take items
// that carry their content once per format, ReadText and WriteText are the plain-text
// shortcuts. ui thread.
// macos reads and writes every format that has a mime type; the other platforms do
// text/plain only so far
namespace imshell::Clipboard {
    // one thing on the clipboard, in each format it is offered in
    struct Item {
        std::map<std::string, std::string> data; // mime type -> bytes: "text/plain", "text/html",
                                                 // "image/png", "application/pdf", "text/uri-list", ...

        [[nodiscard]] bool has(const std::string& type) const {
            return data.contains(type);
        }
        // the bytes in that format; empty when the item is not offered in it
        [[nodiscard]] std::string get(const std::string& type) const {
            const auto found = data.find(type);
            return found == data.end() ? std::string() : found->second;
        }
        // a copied file: its path, from the item's "text/uri-list"; empty when it is not a file
        [[nodiscard]] std::string file() const;
    };

    // what is on the clipboard: usually one item, one per file when files were copied.
    // a picture always comes with "image/png" (a screenshot's tiff is converted)
    std::vector<Item> Read();
    std::string ReadText();

    // replaces what is on the clipboard
    void Write(const std::vector<Item>& items);
    void WriteText(const std::string& text);
} // namespace imshell::Clipboard

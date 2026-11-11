// what is written to the clipboard reads back, per format; a bitmap another app copied is
// offered as png. it replaces what is on the clipboard (windows has no private one)
#include "imshell/clipboard.hpp"

#include <windows.h>

#include <cstdio>
#include <string>

namespace Clipboard = imshell::Clipboard;

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAILED: %s\n", what);
            ++failed;
        }
    };

    Clipboard::Write({{{{"text/plain", "h\xC3\xA9llo"}}}});
    auto items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("text/plain") == "h\xC3\xA9llo" && items[0].file().empty(), "text reads back");

    Clipboard::Write({{{{"text/plain", "hi"}, {"text/html", "<b>hi</b>"}}}});
    items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("text/plain") == "hi", "an item keeps its text");
    check(items.size() == 1 && items[0].get("text/html").find("<b>hi</b>") != std::string::npos,
          "an item keeps its html");

    // a 2x2 red picture
    const unsigned char red[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00,
        0x14, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xfc, 0xcf, 0xc0, 0xf0, 0x9f, 0x81, 0x81, 0x81, 0x81, 0x89,
        0x01, 0x0a, 0x00, 0x1f, 0x17, 0x02, 0x02, 0x67, 0xa9, 0xe2, 0x82, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
        0x44, 0xae, 0x42, 0x60, 0x82};
    const std::string png(reinterpret_cast<const char*>(red), sizeof red);
    Clipboard::Write({{{{"image/png", png}}}});
    items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("image/png") == png, "a written png reads back the same");

    // only the bitmap left, as paint copies: still offered as png
    if (OpenClipboard(nullptr)) {
        HANDLE dib = GetClipboardData(CF_DIB);
        const SIZE_T size = dib ? GlobalSize(dib) : 0;
        HGLOBAL copy = size ? GlobalAlloc(GMEM_MOVEABLE, size) : nullptr;
        if (copy) {
            memcpy(GlobalLock(copy), GlobalLock(dib), size);
            GlobalUnlock(dib);
            GlobalUnlock(copy);
            EmptyClipboard();
            SetClipboardData(CF_DIB, copy);
        }
        CloseClipboard();
        check(copy != nullptr, "the png was also written as a bitmap");
    }
    items = Clipboard::Read();
    const std::string fromDib = items.empty() ? "" : items[0].get("image/png");
    check(fromDib.size() > 8 && fromDib.compare(1, 3, "PNG") == 0, "a bitmap is offered as png");

    Clipboard::Write({{{{"text/uri-list", "file:///C:/Windows"}}}, {{{"text/uri-list", "file:///C:/a%20b.txt"}}}});
    items = Clipboard::Read();
    check(items.size() == 2 && items[0].file() == "C:\\Windows" && items[1].file() == "C:\\a b.txt",
          "copied files come back as paths");

    std::printf(failed ? "%d failed\n" : "ok\n", failed);
    return failed ? 1 : 0;
}

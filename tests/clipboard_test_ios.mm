// what is written to a pasteboard of the test's own reads back, per format (the simulator:
// xcrun simctl spawn booted imshell_clipboard_test)
#include "imshell/clipboard.hpp"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <cstdio>

namespace imshell::Clipboard {
    extern UIPasteboard* board;
}

int main() {
    namespace Clipboard = imshell::Clipboard;
    UIPasteboard* board = [UIPasteboard pasteboardWithUniqueName];
    Clipboard::board = board;
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAILED: %s\n", what);
            ++failed;
        }
    };

    board.items = @[];
    check(Clipboard::Read().empty() && Clipboard::ReadText().empty(), "empty board");

    Clipboard::WriteText("héllo");
    check(Clipboard::ReadText() == "héllo", "text reads back");
    auto items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("text/plain") == "héllo" && items[0].file().empty(), "text is one item");

    Clipboard::Write({{{{"text/plain", "hi"}, {"text/html", "<b>hi</b>"}}}});
    items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("text/html") == "<b>hi</b>" && items[0].get("text/plain") == "hi",
          "an item keeps each of its formats");

    // a jpeg, the way a photo arrives: also offered as png
    UIGraphicsImageRenderer* renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(2, 2)];
    NSData* jpeg = [renderer JPEGDataWithCompressionQuality:0.9 actions:^(UIGraphicsImageRendererContext* ctx) {
        [UIColor.redColor setFill];
        [ctx fillRect:CGRectMake(0, 0, 2, 2)];
    }];
    board.items = @[ @{UTTypeJPEG.identifier : jpeg} ];
    items = Clipboard::Read();
    const std::string png = items.empty() ? "" : items[0].get("image/png");
    check(png.size() > 8 && png.compare(1, 3, "PNG") == 0, "a jpeg picture is offered as png");
    Clipboard::Write({{{{"image/png", png}}}});
    items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("image/png") == png, "a written png reads back the same");

    Clipboard::Write({{{{"text/uri-list", "file:///tmp"}}}, {{{"text/uri-list", "file:///var/a%20b.txt"}}}});
    items = Clipboard::Read();
    check(items.size() == 2 && items[0].file() == "/tmp" && items[1].file() == "/var/a b.txt",
          "copied files come back as paths");

    [UIPasteboard removePasteboardWithName:board.name];
    std::printf(failed ? "%d failed\n" : "ok\n", failed);
    return failed ? 1 : 0;
}

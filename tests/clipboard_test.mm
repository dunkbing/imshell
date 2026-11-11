// what is written to a pasteboard of the test's own reads back, per format
#include "imshell/clipboard.hpp"

#import <AppKit/AppKit.h>
#include <cstdio>

namespace imshell::Clipboard {
    extern NSPasteboard* board;
}

int main() {
    namespace Clipboard = imshell::Clipboard;
    NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
    Clipboard::board = board;
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAILED: %s\n", what);
            ++failed;
        }
    };

    [board clearContents];
    check(Clipboard::Read().empty() && Clipboard::ReadText().empty(), "empty board");

    Clipboard::WriteText("héllo");
    check(Clipboard::ReadText() == "héllo", "text reads back");
    auto items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("text/plain") == "héllo" && items[0].file().empty(), "text is one item");

    // one item in two formats
    Clipboard::Write({{{{"text/plain", "hi"}, {"text/html", "<b>hi</b>"}}}});
    items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("text/html") == "<b>hi</b>" && items[0].get("text/plain") == "hi",
          "an item keeps each of its formats");
    check(!items[0].has("image/png") && items[0].get("image/png").empty(), "a format it lacks is empty");

    // a 2x2 picture as tiff, the way a screenshot arrives: also offered as png
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:2 pixelsHigh:2
                                                                 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [board clearContents];
    [board setData:rep.TIFFRepresentation forType:NSPasteboardTypeTIFF];
    items = Clipboard::Read();
    const std::string png = items.empty() ? "" : items[0].get("image/png");
    check(png.size() > 8 && png.compare(1, 3, "PNG") == 0, "a tiff picture is offered as png");
    Clipboard::Write({{{{"image/png", png}}}});
    items = Clipboard::Read();
    check(items.size() == 1 && items[0].get("image/png") == png, "a written png reads back the same");

    // two copied files: an item each, with its path
    [board clearContents];
    [board writeObjects:@[ [NSURL fileURLWithPath:@"/tmp"], [NSURL fileURLWithPath:@"/private/var/a b.txt"] ]];
    items = Clipboard::Read();
    check(items.size() == 2 && items[0].file() == "/tmp" && items[1].file() == "/private/var/a b.txt",
          "copied files come back as paths");

    [board releaseGlobally];
    std::printf(failed ? "%d failed\n" : "ok\n", failed);
    return failed ? 1 : 0;
}

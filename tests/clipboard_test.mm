// pictures and files on a pasteboard of the test's own come back; an empty one gives nothing
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
    check(Clipboard::ReadImage().empty() && Clipboard::ReadFiles().empty(), "empty board");

    // a 2x2 picture as tiff, the way a screenshot arrives: comes back as png
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:2 pixelsHigh:2
                                                                 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [board setData:rep.TIFFRepresentation forType:NSPasteboardTypeTIFF];
    const std::string png = Clipboard::ReadImage();
    check(png.size() > 8 && png.compare(1, 3, "PNG") == 0, "tiff comes back as png");
    check(Clipboard::ReadFiles().empty(), "a picture is not a file");

    // what was written is what is read
    [board clearContents];
    Clipboard::WriteImage(png);
    check(Clipboard::ReadImage() == png, "a written picture reads back the same");

    [board clearContents];
    [board writeObjects:@[ [NSURL fileURLWithPath:@"/tmp"] ]];
    const auto files = Clipboard::ReadFiles();
    check(files.size() == 1 && files[0] == "/tmp", "a copied file comes back as its path");

    [board releaseGlobally];
    std::printf(failed ? "%d failed\n" : "ok\n", failed);
    return failed ? 1 : 0;
}

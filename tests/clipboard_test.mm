// a picture and a file put on a pasteboard of the test's own come back; an empty one gives nothing
#include "imshell/clipboard.hpp"

#import <AppKit/AppKit.h>
#include <cstdio>

namespace imshell {
    extern NSPasteboard* clipboardBoard;
}

int main() {
    NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
    imshell::clipboardBoard = board;
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAILED: %s\n", what);
            ++failed;
        }
    };

    [board clearContents];
    check(imshell::ClipboardImage().empty() && imshell::ClipboardFiles().empty(), "empty board");

    // a 2x2 picture as tiff, the way a screenshot arrives: comes back as png
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:2 pixelsHigh:2
                                                                 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [board setData:rep.TIFFRepresentation forType:NSPasteboardTypeTIFF];
    const std::string png = imshell::ClipboardImage();
    check(png.size() > 8 && png.compare(1, 3, "PNG") == 0, "tiff comes back as png");
    check(imshell::ClipboardFiles().empty(), "a picture is not a file");

    [board clearContents];
    [board writeObjects:@[ [NSURL fileURLWithPath:@"/tmp"] ]];
    const auto files = imshell::ClipboardFiles();
    check(files.size() == 1 && files[0] == "/tmp", "a copied file comes back as its path");

    [board releaseGlobally];
    std::printf(failed ? "%d failed\n" : "ok\n", failed);
    return failed ? 1 : 0;
}

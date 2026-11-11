#include "imshell/clipboard.hpp"

#import <AppKit/AppKit.h>

namespace imshell::Clipboard {
    // the pasteboard used: a test names its own, so the user's is left alone
    NSPasteboard* board = nil;
    static NSPasteboard* pasteboard() {
        return board ? board : NSPasteboard.generalPasteboard;
    }

    std::string ReadImage() {
        @autoreleasepool {
            NSData* png = [pasteboard() dataForType:NSPasteboardTypePNG];
            if (!png) { // a screenshot is tiff
                if (NSData* tiff = [pasteboard() dataForType:NSPasteboardTypeTIFF]) {
                    png = [[NSBitmapImageRep imageRepWithData:tiff] representationUsingType:NSBitmapImageFileTypePNG
                                                                                 properties:@{}];
                }
            }
            return png ? std::string(static_cast<const char*>(png.bytes), png.length) : std::string();
        }
    }

    void WriteImage(const std::string& png) {
        @autoreleasepool {
            [pasteboard() clearContents];
            [pasteboard() setData:[NSData dataWithBytes:png.data() length:png.size()] forType:NSPasteboardTypePNG];
        }
    }

    std::vector<std::string> ReadFiles() {
        std::vector<std::string> out;
        @autoreleasepool {
            NSArray<NSURL*>* urls = [pasteboard() readObjectsForClasses:@[ NSURL.class ]
                                                                options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
            for (NSURL* url in urls) {
                out.push_back(url.path.UTF8String);
            }
        }
        return out;
    }
} // namespace imshell::Clipboard

#include "imshell/clipboard.hpp"

#import <AppKit/AppKit.h>

namespace imshell {
    // the pasteboard to read: a test names its own, so the user's is left alone
    NSPasteboard* clipboardBoard = nil;
    static NSPasteboard* board() {
        return clipboardBoard ? clipboardBoard : NSPasteboard.generalPasteboard;
    }

    std::string ClipboardImage() {
        @autoreleasepool {
            NSData* png = [board() dataForType:NSPasteboardTypePNG];
            if (!png) { // a screenshot is tiff
                if (NSData* tiff = [board() dataForType:NSPasteboardTypeTIFF]) {
                    png = [[NSBitmapImageRep imageRepWithData:tiff] representationUsingType:NSBitmapImageFileTypePNG
                                                                                 properties:@{}];
                }
            }
            return png ? std::string(static_cast<const char*>(png.bytes), png.length) : std::string();
        }
    }

    std::vector<std::string> ClipboardFiles() {
        std::vector<std::string> out;
        @autoreleasepool {
            NSArray<NSURL*>* urls = [board() readObjectsForClasses:@[ NSURL.class ]
                                                           options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
            for (NSURL* url in urls) {
                out.push_back(url.path.UTF8String);
            }
        }
        return out;
    }
} // namespace imshell

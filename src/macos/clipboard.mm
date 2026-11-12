#include "imshell/clipboard.hpp"

#include <cstdlib>

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace imshell::Clipboard {
    // the pasteboard used: a test names its own (or IMSHELL_PASTEBOARD does), so the user's is left alone
    NSPasteboard* board = nil;
    static NSPasteboard* pasteboard() {
        if (!board) {
            if (const char* name = std::getenv("IMSHELL_PASTEBOARD"); name && *name) {
                board = [NSPasteboard pasteboardWithName:@(name)];
            }
        }
        return board ? board : NSPasteboard.generalPasteboard;
    }

    // a pasteboard type as a mime type; nil when it has none (private and dynamic types)
    static NSString* mimeOf(NSPasteboardType type) {
        if ([type isEqualToString:NSPasteboardTypeFileURL] || [type isEqualToString:NSPasteboardTypeURL]) {
            return @"text/uri-list";
        }
        if ([type isEqualToString:NSPasteboardTypeString]) {
            return @"text/plain";
        }
        return [UTType typeWithIdentifier:type].preferredMIMEType;
    }
    static NSPasteboardType typeOf(NSString* mime, NSData* data) {
        if ([mime isEqualToString:@"text/uri-list"]) {
            NSString* uri = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
            return [uri hasPrefix:@"file://"] ? NSPasteboardTypeFileURL : NSPasteboardTypeURL;
        }
        if ([mime isEqualToString:@"text/plain"]) {
            return NSPasteboardTypeString; // utf-8, what other apps paste
        }
        return [UTType typeWithMIMEType:mime].identifier;
    }

    std::vector<Item> Read() {
        std::vector<Item> out;
        @autoreleasepool {
            for (NSPasteboardItem* entry in pasteboard().pasteboardItems) {
                Item item;
                for (NSPasteboardType type in entry.types) {
                    NSString* mime = mimeOf(type);
                    NSData* data = mime ? [entry dataForType:type] : nil;
                    if (data && !item.has(mime.UTF8String)) {
                        item.data[mime.UTF8String] = std::string(static_cast<const char*>(data.bytes), data.length);
                    }
                }
                // a screenshot is tiff only: every picture is also offered as png
                if (!item.has("image/png")) {
                    if (NSData* tiff = [entry dataForType:NSPasteboardTypeTIFF]) {
                        NSData* png = [[NSBitmapImageRep imageRepWithData:tiff]
                            representationUsingType:NSBitmapImageFileTypePNG
                                         properties:@{}];
                        if (png) {
                            item.data["image/png"] = std::string(static_cast<const char*>(png.bytes), png.length);
                        }
                    }
                }
                if (!item.data.empty()) {
                    out.push_back(std::move(item));
                }
            }
        }
        return out;
    }

    std::string ReadText() {
        @autoreleasepool {
            NSString* text = [pasteboard() stringForType:NSPasteboardTypeString];
            return text ? text.UTF8String : "";
        }
    }

    void Write(const std::vector<Item>& items) {
        @autoreleasepool {
            NSMutableArray<NSPasteboardItem*>* entries = [NSMutableArray array];
            for (const Item& item : items) {
                NSPasteboardItem* entry = [[NSPasteboardItem alloc] init];
                for (const auto& [mime, bytes] : item.data) {
                    NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
                    if (NSPasteboardType type = typeOf(@(mime.c_str()), data)) {
                        [entry setData:data forType:type];
                    }
                }
                [entries addObject:entry];
            }
            [pasteboard() clearContents];
            [pasteboard() writeObjects:entries];
        }
    }

    void WriteText(const std::string& text) {
        Write({{{{"text/plain", text}}}});
    }
} // namespace imshell::Clipboard

// the clipboard on ios: UIPasteboard, every format that has a mime type, an item each
#include "imshell/clipboard.hpp"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace imshell::Clipboard {
    // the pasteboard used: a test names its own, so the user's is left alone
    UIPasteboard* board = nil;
    static UIPasteboard* pasteboard() {
        return board ? board : UIPasteboard.generalPasteboard;
    }

    // a pasteboard type as a mime type; nil when it has none (private and dynamic types)
    static NSString* mimeOf(NSString* type) {
        if ([type isEqualToString:UTTypeFileURL.identifier] || [type isEqualToString:UTTypeURL.identifier])
            return @"text/uri-list";
        if ([type isEqualToString:UTTypeUTF8PlainText.identifier] || [type isEqualToString:UTTypePlainText.identifier])
            return @"text/plain";
        return [UTType typeWithIdentifier:type].preferredMIMEType;
    }

    static std::string bytesOf(NSData* data) {
        return std::string(static_cast<const char*>(data.bytes), data.length);
    }

    std::vector<Item> Read() {
        std::vector<Item> out;
        @autoreleasepool {
            UIPasteboard* pb = pasteboard();
            for (NSInteger i = 0; i < pb.numberOfItems; ++i) {
                NSIndexSet* at = [NSIndexSet indexSetWithIndex:i];
                Item item;
                for (NSString* type in [pb pasteboardTypesForItemSet:at].firstObject) {
                    NSString* mime = mimeOf(type);
                    if (!mime || item.has(mime.UTF8String))
                        continue;
                    // strings and urls are stored as objects: their bytes are the text
                    id value = [pb valuesForPasteboardType:type inItemSet:at].firstObject;
                    NSData* data = [value isKindOfClass:NSData.class]     ? value
                                   : [value isKindOfClass:NSString.class] ? [value dataUsingEncoding:NSUTF8StringEncoding]
                                   : [value isKindOfClass:NSURL.class]
                                       ? [[value absoluteString] dataUsingEncoding:NSUTF8StringEncoding]
                                       : [pb dataForPasteboardType:type inItemSet:at].firstObject;
                    // a url is stored as a property list: [string, base]
                    if (data && [mime isEqualToString:@"text/uri-list"] && ![value isKindOfClass:NSURL.class]) {
                        id plist = [NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil];
                        id first = [plist isKindOfClass:NSArray.class] ? [plist firstObject] : plist;
                        if ([first isKindOfClass:NSString.class])
                            data = [first dataUsingEncoding:NSUTF8StringEncoding];
                    }
                    if (data)
                        item.data[mime.UTF8String] = bytesOf(data);
                }
                // a picture is always offered as png (photos copy as jpeg or heic)
                if (!item.has("image/png")) {
                    for (const auto& [mime, bytes] : item.data) {
                        if (!mime.starts_with("image/"))
                            continue;
                        UIImage* image = [UIImage imageWithData:[NSData dataWithBytes:bytes.data() length:bytes.size()]];
                        if (NSData* png = image ? UIImagePNGRepresentation(image) : nil) {
                            item.data["image/png"] = bytesOf(png);
                            break;
                        }
                    }
                }
                if (!item.data.empty())
                    out.push_back(std::move(item));
            }
        }
        return out;
    }

    void Write(const std::vector<Item>& items) {
        @autoreleasepool {
            NSMutableArray<NSDictionary<NSString*, id>*>* entries = [NSMutableArray array];
            for (const Item& item : items) {
                NSMutableDictionary<NSString*, id>* entry = [NSMutableDictionary dictionary];
                for (const auto& [mime, bytes] : item.data) {
                    NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
                    NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
                    if (mime == "text/plain") {
                        entry[UTTypeUTF8PlainText.identifier] = text ?: @""; // as a string: what other apps paste
                    } else if (mime == "text/uri-list") {
                        NSURL* url = [NSURL URLWithString:[text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet].firstObject];
                        if (url)
                            entry[url.isFileURL ? UTTypeFileURL.identifier : UTTypeURL.identifier] = url;
                    } else if (UTType* type = [UTType typeWithMIMEType:@(mime.c_str())]) {
                        entry[type.identifier] = data;
                    }
                }
                [entries addObject:entry];
            }
            pasteboard().items = entries;
        }
    }

    std::string ReadText() {
        @autoreleasepool {
            NSString* text = pasteboard().string;
            return text ? text.UTF8String : "";
        }
    }

    void WriteText(const std::string& text) {
        Write({{{{"text/plain", text}}}});
    }
} // namespace imshell::Clipboard

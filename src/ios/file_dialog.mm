// ios file dialogs: the document picker for opening; saves and folders are the documents
// folder, which the files app shows under the app's name
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "../native_view.hpp"
#include "imshell/alert.hpp"
#include "imshell/file_dialog.hpp"

@interface ImShellPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, copy) void (^done)(NSArray<NSURL*>*);
@end

@implementation ImShellPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)picker
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    self.done(urls);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)picker {
    self.done(@[]);
}
@end

namespace imshell {
    namespace {
        UIViewController* topController() {
            for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
                if (![scene isKindOfClass:UIWindowScene.class])
                    continue;
                UIViewController* top = ((UIWindowScene*)scene).keyWindow.rootViewController;
                while (top.presentedViewController)
                    top = top.presentedViewController;
                if (top)
                    return top;
            }
            return nil;
        }

        std::string path(NSURL* url) {
            return url ? std::string(url.path.UTF8String) : std::string();
        }

        // "name.ext", or "name 2.ext" when taken
        NSURL* freeURL(NSURL* dir, NSString* fileName) {
            NSURL* url = [dir URLByAppendingPathComponent:fileName];
            NSString* stem = fileName.stringByDeletingPathExtension;
            NSString* ext = fileName.pathExtension;
            for (int n = 2; [NSFileManager.defaultManager fileExistsAtPath:url.path]; ++n) {
                NSString* name = [NSString stringWithFormat:@"%@ %d", stem, n];
                url = [dir URLByAppendingPathComponent:ext.length
                                                           ? [name stringByAppendingPathExtension:ext]
                                                           : name];
            }
            return url;
        }

        // a file of ours as it is, anything else copied in: sqlite needs to create its journal
        // next to the database, and access to a picked file does not cover its folder
        std::string localCopy(NSURL* url, NSURL* into) {
            NSString* container = NSHomeDirectory().stringByStandardizingPath;
            if ([url.path.stringByStandardizingPath hasPrefix:container])
                return path(url);
            [NSFileManager.defaultManager createDirectoryAtURL:into
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:nil];
            const BOOL scoped = [url startAccessingSecurityScopedResource];
            NSURL* copy = freeURL(into, url.lastPathComponent);
            NSError* error = nil;
            const BOOL ok = [NSFileManager.defaultManager copyItemAtURL:url toURL:copy error:&error];
            if (scoped)
                [url stopAccessingSecurityScopedResource];
            if (!ok) {
                Alert::show("Could not open the file", error.localizedDescription.UTF8String ?: "");
                return {};
            }
            return path(copy);
        }

        NSArray<UTType*>* contentTypes(const std::vector<FileFilter>& filters) {
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            for (const FileFilter& f : filters) {
                for (NSString* ext in [@(f.extensions.c_str()) componentsSeparatedByString:@","]) {
                    UTType* type = [ext isEqualToString:@"*"] ? nil : [UTType typeWithFilenameExtension:ext];
                    if (!type)
                        return @[ UTTypeItem ]; // "*", or no type for it: any file
                    [types addObject:type];
                }
            }
            return types.count ? types : @[ UTTypeItem ];
        }

        ImShellPickerDelegate* gPicking = nil; // the picker holds its delegate weakly
    } // namespace

    std::string DocumentsDir() {
        NSURL* url = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                          inDomains:NSUserDomainMask]
                         .firstObject;
        [NSFileManager.defaultManager createDirectoryAtURL:url
                               withIntermediateDirectories:YES
                                                attributes:nil
                                                     error:nil];
        return path(url);
    }

    void OpenDialog(const OpenOptions& options, FilesDone done) {
        if (options.directory) // no folder picker: the documents folder
            return done({DocumentsDir()});
        UIViewController* top = topController();
        if (!top)
            return;
        const std::string dir = options.copyInto.empty() ? DocumentsDir() : options.copyInto;
        NSURL* into = [NSURL fileURLWithPath:@(dir.c_str()) isDirectory:YES];
        UIDocumentPickerViewController* picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:contentTypes(options.filters)
                                                                        asCopy:NO];
        picker.allowsMultipleSelection = options.multiple;
        gPicking = [ImShellPickerDelegate new];
        __block FilesDone callback = std::move(done);
        gPicking.done = ^(NSArray<NSURL*>* urls) {
          gPicking = nil;
          [(__bridge UIView*)detail::nativeHost() becomeFirstResponder]; // keys back to imgui
          std::vector<std::string> files;
          for (NSURL* url in urls)
              if (std::string file = localCopy(url, into); !file.empty())
                  files.push_back(std::move(file));
          if (!files.empty())
              callback(files);
        };
        picker.delegate = gPicking;
        [top presentViewController:picker animated:YES completion:nil];
    }

    void SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>&,
                        FileDone done) {
        NSURL* docs = [NSURL fileURLWithPath:@(DocumentsDir().c_str()) isDirectory:YES];
        done(path(freeURL(docs, @((defaultName.empty() ? "Untitled" : defaultName).c_str()))));
    }
} // namespace imshell

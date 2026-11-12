// ios file dialogs: the document picker for opening; saves and folders are the documents
// folder, which the files app shows under the app's name
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "../native_view.hpp"
#include "imshell/alert.hpp"
#include "imshell/file_dialog.hpp"

@interface ImShellPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, copy) void (^done)(NSURL*);
@end

@implementation ImShellPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)picker
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    self.done(urls.firstObject);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)picker {
    self.done(nil);
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

    void OpenFileDialog(const std::vector<FileFilter>& filters, FileDone done,
                        const std::string& copyInto) {
        UIViewController* top = topController();
        if (!top)
            return;
        NSURL* into = [NSURL fileURLWithPath:@((copyInto.empty() ? DocumentsDir() : copyInto).c_str())
                                 isDirectory:YES];
        UIDocumentPickerViewController* picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:contentTypes(filters)
                                                                        asCopy:NO];
        gPicking = [ImShellPickerDelegate new];
        __block FileDone callback = std::move(done);
        gPicking.done = ^(NSURL* url) {
          gPicking = nil;
          [(__bridge UIView*)detail::nativeHost() becomeFirstResponder]; // keys back to imgui
          if (!url)
              return;
          if (const std::string file = localCopy(url, into); !file.empty())
              callback(file);
        };
        picker.delegate = gPicking;
        [top presentViewController:picker animated:YES completion:nil];
    }

    // shortcut: the system picker here takes one file; a list of one
    void OpenFilesDialog(const std::vector<FileFilter>& filters, FilesDone done, const std::string& copyInto) {
        OpenFileDialog(filters, [done = std::move(done)](const std::string& path) { done({path}); }, copyInto);
    }

    void SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>&,
                        FileDone done) {
        NSURL* docs = [NSURL fileURLWithPath:@(DocumentsDir().c_str()) isDirectory:YES];
        done(path(freeURL(docs, @((defaultName.empty() ? "Untitled" : defaultName).c_str()))));
    }

    void PickFolderDialog(FileDone done) {
        done(DocumentsDir());
    }
} // namespace imshell

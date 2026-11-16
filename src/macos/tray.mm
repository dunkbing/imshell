// the menu bar icon: an NSStatusItem. a left click is onActivate (or the menu when there is
// none), a right or control click opens the menu
#include "imshell/tray.hpp"

#import <AppKit/AppKit.h>

#include <functional>
#include <vector>

namespace {
    std::vector<std::function<void()>> gActions; // by menu item tag
    std::function<void()> gActivate;
} // namespace

@interface ImShellTray : NSObject
@property(strong) NSStatusItem* item;
@property(strong) NSMenu* menu;
@end

@implementation ImShellTray
- (void)picked:(NSMenuItem*)sender {
    const NSInteger i = sender.tag;
    if (i >= 0 && i < static_cast<NSInteger>(gActions.size()) && gActions[i]) {
        auto action = gActions[i]; // a copy: it may call Set() and replace the list
        action();
    }
}
- (void)clicked:(id)sender {
    NSEvent* e = NSApp.currentEvent;
    const bool menu = !gActivate || e.type == NSEventTypeRightMouseUp ||
                      (e.modifierFlags & NSEventModifierFlagControl);
    if (!menu) {
        auto activate = gActivate;
        activate();
        return;
    }
    // a status item shows its menu only while it is set: set it for this click
    self.item.menu = self.menu;
    [self.item.button performClick:nil];
    self.item.menu = nil;
}
@end

namespace {
    ImShellTray* gTray = nil;
}

namespace imshell::tray {
    void Set(const Options& options) {
        if (!gTray) {
            gTray = [[ImShellTray alloc] init];
            gTray.item = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
            NSStatusBarButton* button = gTray.item.button;
            button.target = gTray;
            button.action = @selector(clicked:);
            [button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
        }
        NSStatusBarButton* button = gTray.item.button;
        button.toolTip = options.tooltip.empty() ? nil : @(options.tooltip.c_str());
        NSImage* image = nil;
        if (options.icon.png) {
            image = [[NSImage alloc] initWithData:[NSData dataWithBytes:options.icon.png
                                                                 length:options.icon.size]];
            image.size = NSMakeSize(18, 18); // the menu bar's icon size
            [image setTemplate:YES]; // drawn in the bar's colour from its alpha
        }
        button.image = image;
        button.title = image || options.tooltip.empty() ? @"" : [@(options.tooltip.c_str()) substringToIndex:1];

        gActions.clear();
        NSMenu* menu = [[NSMenu alloc] init];
        menu.autoenablesItems = NO;
        for (const Item& it : options.items) {
            if (it.separator) {
                [menu addItem:NSMenuItem.separatorItem];
                continue;
            }
            NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@(it.label.c_str())
                                                          action:@selector(picked:)
                                                   keyEquivalent:@""];
            item.target = gTray;
            item.tag = static_cast<NSInteger>(gActions.size());
            item.enabled = it.enabled;
            gActions.push_back(it.onClick);
            [menu addItem:item];
        }
        gTray.menu = menu;
        gActivate = options.onActivate;
    }

    bool Available() {
        return gTray != nil;
    }

    void Remove() {
        if (!gTray)
            return;
        [NSStatusBar.systemStatusBar removeStatusItem:gTray.item];
        gTray = nil;
        gActions.clear();
        gActivate = nullptr;
    }
} // namespace imshell::tray

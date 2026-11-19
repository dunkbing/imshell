// the os's own context menu: an NSMenu popped up at the pointer, tracked in appkit's loop
#include "../native_menu.hpp"
#include "../native_view.hpp"

#import <AppKit/AppKit.h>

@interface ImShellMenuTarget : NSObject {
@public
    int chosen;
}
- (void)pick:(NSMenuItem*)sender;
@end

@implementation ImShellMenuTarget
- (void)pick:(NSMenuItem*)sender {
    chosen = static_cast<int>(sender.tag);
}
@end

namespace imshell::detail {
    namespace {
        NSMenu* build(const std::vector<NativeMenuItem>& items, ImShellMenuTarget* target) {
            NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
            menu.autoenablesItems = NO; // enabled is the caller's say
            for (const NativeMenuItem& item : items) {
                if (item.separator) {
                    [menu addItem:[NSMenuItem separatorItem]];
                    continue;
                }
                NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:@(item.label.c_str())
                                                               action:item.children.empty() ? @selector(pick:) : nil
                                                        keyEquivalent:@""];
                entry.target = target;
                entry.tag = item.id;
                entry.enabled = item.enabled;
                entry.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
                if (!item.icon.empty())
                    entry.image = [NSImage imageWithSystemSymbolName:@(item.icon.c_str()) accessibilityDescription:nil];
                if (!item.children.empty())
                    entry.submenu = build(item.children, target);
                [menu addItem:entry];
            }
            return menu;
        }
    } // namespace

    bool nativeMenusAvailable() {
        return true;
    }

    void nativeMenuPopUp(const std::vector<NativeMenuItem>& items, ImVec2 pos, const std::function<void(int)>& done) {
        NSView* view = (__bridge NSView*)nativeHost();
        if (!view) {
            done(-1);
            return;
        }
        ImShellMenuTarget* target = [ImShellMenuTarget new];
        target->chosen = -1;
        NSMenu* menu = build(items, target);
        // imgui's origin is the top left; the view's may be the bottom left
        const NSPoint at = NSMakePoint(pos.x, view.isFlipped ? pos.y : view.bounds.size.height - pos.y);
        [menu popUpMenuPositioningItem:nil atLocation:at inView:view]; // returns when it closes
        // the os swallowed the button release: imgui must not think a button is still down
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        done(target->chosen);
    }

    int nativeMenuPick(const std::vector<NativeMenuItem>& items, const std::vector<int>& path) {
        ImShellMenuTarget* target = [ImShellMenuTarget new];
        target->chosen = -1;
        NSMenu* menu = build(items, target);
        for (size_t i = 0; i < path.size(); ++i) {
            NSMenuItem* entry = [menu itemAtIndex:path[i]];
            if (i + 1 < path.size()) {
                menu = entry.submenu;
            } else if (entry.enabled && entry.action) { // what a click sends
                [(ImShellMenuTarget*)entry.target pick:entry];
            }
        }
        return target->chosen;
    }
} // namespace imshell::detail

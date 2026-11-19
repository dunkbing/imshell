// the os's own context menu on ios: a UIMenu shown by a UIEditMenuInteraction at the touch
// (ios 16), so the system draws it, with its blur, haptics and VoiceOver
#include "../native_menu.hpp"
#include "../native_view.hpp"

#import <UIKit/UIKit.h>

@interface ImShellMenuHost : NSObject <UIEditMenuInteractionDelegate>
@property(nonatomic) UIMenu* menu;
@end

@implementation ImShellMenuHost
- (UIMenu*)editMenuInteraction:(UIEditMenuInteraction*)interaction
          menuForConfiguration:(UIEditMenuConfiguration*)configuration
              suggestedActions:(NSArray<UIMenuElement*>*)suggestedActions {
    return self.menu; // ours alone: no cut, copy or paste
}
@end

namespace imshell::detail {
    namespace {
        // `done` is copied into each action: the handlers run after the menu closes, long after the call
        NSArray<UIMenuElement*>* build(const std::vector<NativeMenuItem>& items, std::function<void(int)> done) {
            NSMutableArray<NSMutableArray<UIMenuElement*>*>* groups = [NSMutableArray arrayWithObject:[NSMutableArray array]];
            for (const NativeMenuItem& item : items) {
                if (item.separator) { // a separator starts a section
                    [groups addObject:[NSMutableArray array]];
                    continue;
                }
                UIImage* image = item.icon.empty() ? nil : [UIImage systemImageNamed:@(item.icon.c_str())];
                if (!item.children.empty()) {
                    [groups.lastObject addObject:[UIMenu menuWithTitle:@(item.label.c_str()) image:image identifier:nil options:0 children:build(item.children, done)]];
                    continue;
                }
                const int id = item.id;
                UIAction* action = [UIAction actionWithTitle:@(item.label.c_str()) image:image identifier:nil handler:^(UIAction*) { done(id); }];
                action.attributes = item.enabled ? 0 : UIMenuElementAttributesDisabled;
                action.state = item.checked ? UIMenuElementStateOn : UIMenuElementStateOff;
                [groups.lastObject addObject:action];
            }
            if (groups.count == 1)
                return groups.firstObject;
            NSMutableArray<UIMenuElement*>* out = [NSMutableArray array];
            for (NSArray<UIMenuElement*>* group in groups)
                if (group.count)
                    [out addObject:[UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:group]];
            return out;
        }
    } // namespace

    bool nativeMenusAvailable() {
        return true;
    }

    void nativeMenuPopUp(const std::vector<NativeMenuItem>& items, ImVec2 pos, const std::function<void(int)>& done) {
        UIView* view = (__bridge UIView*)nativeHost();
        if (!view) {
            done(-1);
            return;
        }
        static ImShellMenuHost* host = [ImShellMenuHost new];
        static UIEditMenuInteraction* interaction = nil;
        if (!interaction || interaction.view != view) { // one per host view
            interaction = [[UIEditMenuInteraction alloc] initWithDelegate:host];
            [view addInteraction:interaction];
        }
        host.menu = [UIMenu menuWithChildren:build(items, done)];
        UIEditMenuConfiguration* configuration = [UIEditMenuConfiguration configurationWithIdentifier:nil sourcePoint:CGPointMake(pos.x, pos.y)];
        [interaction presentEditMenuWithConfiguration:configuration];
    }

    // shortcut: a UIAction's handler cannot be run from outside, so there is nothing to pick in a test here; check by hand on a device or the simulator
    int nativeMenuPick(const std::vector<NativeMenuItem>&, const std::vector<int>&) {
        return -1;
    }
} // namespace imshell::detail

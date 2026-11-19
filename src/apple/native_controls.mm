// native switch and segmented control (macos: NSSwitch, NSSegmentedControl; ios: UISwitch,
// UISegmentedControl), each in a box that NativeView places at the imgui frame's size, so the
// control keeps its own size and sits in the middle of it
#include "../native_controls.hpp"
#include "imgui_internal.h"
#include "imshell/frames.hpp"
#include "imshell/native_view.hpp"

#include <TargetConditionals.h>

#include <algorithm>
#include <unordered_map>

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
using PlatformView = UIView;
#else
#import <AppKit/AppKit.h>
using PlatformView = NSView;
#endif
#import <objc/runtime.h>

// clicks pass through the box wherever the control is not
@interface ImShellControlBox : PlatformView
@end
@implementation ImShellControlBox
#if TARGET_OS_IPHONE
- (UIView*)hitTest:(CGPoint)point withEvent:(UIEvent*)event {
    UIView* hit = [super hitTest:point withEvent:event];
    return hit == self ? nil : hit;
}
#else
- (BOOL)isFlipped {
    return YES;
}
- (NSView*)hitTest:(NSPoint)point {
    NSView* hit = [super hitTest:point];
    return hit == self ? nil : hit;
}
#endif
@end

@interface ImShellControlTarget : NSObject {
@public
    bool changed;
}
- (void)fire:(id)sender;
@end

@implementation ImShellControlTarget
- (void)fire:(id)sender {
    changed = true;
    imshell::requestFrames(); // the change came outside imgui: wake the idle loop
}
@end

namespace imshell::detail {
    namespace {
        // a box the size of the imgui frame with `control` centred in it on the left
        PlatformView* boxFor(PlatformView* control, ImShellControlTarget* target) {
            ImShellControlBox* box = [[ImShellControlBox alloc] initWithFrame:control.frame];
#if TARGET_OS_IPHONE
            control.autoresizingMask = UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleBottomMargin;
#else
            control.autoresizingMask = NSViewMinYMargin | NSViewMaxYMargin;
#endif
            [box addSubview:control];
            objc_setAssociatedObject(box, "target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            return box;
        }

        bool disabled() {
            return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
        }

        ImVec2 frameOf(CGSize size) {
            return ImVec2(static_cast<float>(size.width), std::max(static_cast<float>(size.height), ImGui::GetFrameHeight()));
        }

        ImShellControlTarget* targetOf(PlatformView* box) {
            return objc_getAssociatedObject(box, "target");
        }
    } // namespace

    bool nativeControlsAvailable() {
        return true;
    }

    bool nativeSwitch(const char* id, bool* value) {
        static CGSize size = CGSizeZero; // the os's own, measured once
        if (size.width == 0) {
#if TARGET_OS_IPHONE
            size = [[UISwitch new] sizeThatFits:CGSizeZero];
#else
            size = [[NSSwitch new] fittingSize];
#endif
        }
        const ImGuiID key = ImGui::GetID(id);
        PlatformView* box = (__bridge PlatformView*)NativeView(id, frameOf(size), [&] {
            ImShellControlTarget* target = [ImShellControlTarget new];
#if TARGET_OS_IPHONE
            UISwitch* control = [UISwitch new];
            [control addTarget:target action:@selector(fire:) forControlEvents:UIControlEventValueChanged];
#else
            NSSwitch* control = [NSSwitch new];
            control.target = target;
            control.action = @selector(fire:);
#endif
            control.frame = CGRectMake(0, 0, size.width, size.height); // a new NSSwitch has none
            return (__bridge_retained void*)boxFor(control, target);
        });
        // the imgui item under the view: what the test engine clicks
        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), key, &hovered, &held);
        if (!box) {
            if (pressed)
                *value = !*value;
            return pressed;
        }
#if TARGET_OS_IPHONE
        UISwitch* control = box.subviews.firstObject;
        const bool on = control.on;
#else
        NSSwitch* control = box.subviews.firstObject;
        const bool on = control.state == NSControlStateValueOn;
#endif
        bool changed = false;
        ImShellControlTarget* target = targetOf(box);
        if (target->changed) { // a click on the control itself: it already shows the new state
            target->changed = false;
            *value = on;
            changed = true;
        } else if (pressed) {
            *value = !*value;
            changed = true;
        }
        if (on != *value) { // set from the app (or the test engine), only when they differ
#if TARGET_OS_IPHONE
            [control setOn:*value animated:YES];
#else
            control.state = *value ? NSControlStateValueOn : NSControlStateValueOff;
#endif
        }
        control.enabled = !disabled();
        return changed;
    }

    bool nativeSegmented(const char* id, std::span<const char* const> labels, int* current) {
        const ImGuiID key = ImGui::GetID(id);
        // the size the control wants: made once here, since NativeView reserves before it creates
        static std::unordered_map<ImGuiID, CGSize> sizes;
        CGSize& size = sizes[key];
        PlatformView* box = (__bridge PlatformView*)NativeView(id, size.width > 0 ? frameOf(size) : ImVec2(200, ImGui::GetFrameHeight()), [&] {
            ImShellControlTarget* target = [ImShellControlTarget new];
            NSMutableArray<NSString*>* names = [NSMutableArray array];
            for (const char* label : labels)
                [names addObject:@(label)];
#if TARGET_OS_IPHONE
            UISegmentedControl* control = [[UISegmentedControl alloc] initWithItems:names];
            [control addTarget:target action:@selector(fire:) forControlEvents:UIControlEventValueChanged];
            control.selectedSegmentIndex = *current;
            [control sizeToFit];
#else
            NSSegmentedControl* control = [NSSegmentedControl segmentedControlWithLabels:names
                                                                            trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                                  target:target
                                                                                  action:@selector(fire:)];
            control.selectedSegment = *current;
#endif
            return (__bridge_retained void*)boxFor(control, target);
        });
        if (!box)
            return false;
        PlatformView* control = box.subviews.firstObject;
#if TARGET_OS_IPHONE
        UISegmentedControl* segments = (UISegmentedControl*)control;
        size = segments.frame.size;
        const int selected = static_cast<int>(segments.selectedSegmentIndex);
#else
        NSSegmentedControl* segments = (NSSegmentedControl*)control;
        size = segments.fittingSize;
        const int selected = static_cast<int>(segments.selectedSegment);
#endif
        // the first frame reserved a guess; the control is its real size from the next
        if (control.frame.size.width != size.width)
            control.frame = CGRectMake(0, 0, size.width, size.height);
        bool changed = false;
        ImShellControlTarget* target = targetOf(box);
        if (target->changed) {
            target->changed = false;
            changed = selected != *current;
            *current = selected;
        } else if (selected != *current) {
#if TARGET_OS_IPHONE
            segments.selectedSegmentIndex = *current;
#else
            segments.selectedSegment = *current;
#endif
        }
        segments.enabled = !disabled();
        return changed;
    }
} // namespace imshell::detail

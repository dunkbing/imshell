
#include "../native_view.hpp"
#include "imgui_internal.h"

#include <TargetConditionals.h>


#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
using PlatformView = UIView;
#else
#import <AppKit/AppKit.h>
using PlatformView = NSView;

// top-left origin like imgui; clicks fall through wherever it has no subview
@interface MydearFlippedView : NSView
@end
@implementation MydearFlippedView
- (BOOL)isFlipped {
    return YES;
}
- (NSView*)hitTest:(NSPoint)point {
    NSView* hit = [super hitTest:point];
    return hit == self ? nil : hit;
}
@end
#endif

namespace {
    PlatformView* overlay() {
#if TARGET_OS_IPHONE
        return (__bridge UIView*)mydear::detail::nativeHost();
#else
        // above the metal view, full size, so frames are window points from the top left
        static MydearFlippedView* view = nil;
        if (!view) {
            NSView* host = (__bridge NSView*)mydear::detail::nativeHost();
            view = [[MydearFlippedView alloc] initWithFrame:host.bounds];
            view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            [host addSubview:view];
            // a click on imgui takes the keyboard back from a native field
            [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown |
                                                          NSEventMaskRightMouseDown
                                                  handler:^NSEvent*(NSEvent* e) {
                                                    NSView* content = e.window.contentView;
                                                    if (!content || content != host)
                                                        return e;
                                                    NSView* hit = [content
                                                        hitTest:[content.superview convertPoint:e.locationInWindow
                                                                                       fromView:nil]];
                                                    if (hit && ![hit isDescendantOf:view] &&
                                                        e.window.firstResponder != content)
                                                        [e.window makeFirstResponder:content];
                                                    return e;
                                                  }];
        }
        return view;
#endif
    }

    CGRect cg(const ImRect& r) {
        return CGRectMake(r.Min.x, r.Min.y, r.GetWidth(), r.GetHeight());
    }
} // namespace

// views are window points from the top left, like imgui
namespace mydear::detail {
    void* nativeAttach(void* made) {
        PlatformView* view = (__bridge_transfer PlatformView*)made;
#if TARGET_OS_IPHONE
        UIView* clip = [[UIView alloc] init];
#else
        MydearFlippedView* clip = [[MydearFlippedView alloc] init];
#endif
        clip.clipsToBounds = YES;
        clip.hidden = YES; // the first placement shows it
        [clip addSubview:view];
        [overlay() addSubview:clip];
        return (__bridge_retained void*)clip; // the entry holds the container, the container the view
    }

    void nativePlace(void* clip, void* view, const ImRect& visible, const ImRect& full) {
        ((__bridge PlatformView*)clip).frame = cg(visible);
        ((__bridge PlatformView*)view).frame =
            CGRectOffset(cg(full), -visible.Min.x, -visible.Min.y);
    }

    void nativeSetHidden(void* clip, const bool hidden) {
        ((__bridge PlatformView*)clip).hidden = hidden;
    }

    void nativeRemove(void* clip) {
        PlatformView* view = (__bridge_transfer PlatformView*)clip;
        [view removeFromSuperview];
    }
} // namespace mydear::detail

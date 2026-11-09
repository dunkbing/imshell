#include "mydear/native_view.hpp"
#include "../native_view.hpp"
#include "imgui_internal.h"

#include <TargetConditionals.h>
#include <unordered_map>

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
    struct Entry {
        PlatformView* clip = nil; // the visible part of the item rect
        PlatformView* view = nil;
        ImRect rect, visible;
        ImGuiWindow* window = nullptr;
        int frame = -1;
        double lastSeen = 0;
    };
    std::unordered_map<ImGuiID, Entry> gEntries;
    constexpr double kFreeAfterSeconds = 3;

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

    // a window drawn above `window` (popup, tooltip, floating window) covers `r`
    bool covered(ImGuiWindow* window, const ImRect& r) {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* root = window->RootWindowDockTree;
        for (int i = ImGui::FindWindowDisplayIndex(root) + 1; i < g.Windows.Size; i++) {
            ImGuiWindow* w = g.Windows[i];
            if (w->WasActive && !w->Hidden && w->RootWindowDockTree == w && w != root &&
                w->Rect().Overlaps(r))
                return true;
        }
        return false;
    }

    CGRect cg(const ImRect& r) {
        return CGRectMake(r.Min.x, r.Min.y, r.GetWidth(), r.GetHeight());
    }
} // namespace

namespace mydear {
    void* NativeView(const char* id, const ImVec2& size, const std::function<void*()>& create) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID key = window->GetID(id);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 itemSize = ImGui::CalcItemSize(size, avail.x, avail.y);
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + itemSize.x, pos.y + itemSize.y));
        ImGui::ItemSize(bb);
        ImGui::ItemAdd(bb, key);

        Entry& e = gEntries[key];
        if (!e.view) {
            void* made = create ? create() : nullptr;
            if (!made) {
                gEntries.erase(key);
                return nullptr;
            }
            e.view = (__bridge_transfer PlatformView*)made;
#if TARGET_OS_IPHONE
            e.clip = [[UIView alloc] init];
            e.clip.clipsToBounds = YES;
#else
            e.clip = [[MydearFlippedView alloc] init];
            e.clip.clipsToBounds = YES;
#endif
            [e.clip addSubview:e.view];
            [overlay() addSubview:e.clip];
        }
        e.rect = bb;
        e.visible = bb;
        e.visible.ClipWith(window->ClipRect);
        e.window = window;
        e.frame = ImGui::GetFrameCount();
        e.lastSeen = ImGui::GetTime();
        return (__bridge void*)e.view;
    }

    namespace detail {
        void placeNativeViews() {
            const int frame = ImGui::GetFrameCount();
            const double now = ImGui::GetTime();
            for (auto it = gEntries.begin(); it != gEntries.end();) {
                Entry& e = it->second;
                if (e.frame != frame && now - e.lastSeen > kFreeAfterSeconds) {
                    [e.clip removeFromSuperview];
                    it = gEntries.erase(it);
                    continue;
                }
                const bool show = e.frame == frame && e.visible.GetWidth() > 0 &&
                                  e.visible.GetHeight() > 0 && !covered(e.window, e.visible);
                if (show) {
                    e.clip.frame = cg(e.visible);
                    e.view.frame = CGRectOffset(cg(e.rect), -e.visible.Min.x, -e.visible.Min.y);
                }
                if (e.clip.hidden == show)
                    e.clip.hidden = !show;
                ++it;
            }
        }

        void dropNativeViews() {
            for (auto& [key, e] : gEntries)
                [e.clip removeFromSuperview];
            gEntries.clear();
        }
    } // namespace detail
} // namespace mydear

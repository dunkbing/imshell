// a region of the window drawn by an NSVisualEffectView under the metal layer: the metal layer is
// not opaque, and the shell clears transparent while a region shows
#include "../vibrancy.hpp"
#include "../native_view.hpp"
#include "imshell/frames.hpp"
#include "imshell/vibrancy.hpp"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace imshell {
    namespace {
        struct Request {
            bool set = false;
            ImVec2 pos, size;
            VibrancyMaterial material = VibrancyMaterial::Sidebar;
        };
        Request gRequest;
        bool gActive = false;
        NSVisualEffectView* gView = nil;

        // the view the metal layer lives in (the shell put it last; native views come after)
        NSView* metalContainer(NSView* host) {
            for (NSView* view in host.subviews)
                if ([view.subviews.firstObject.layer isKindOfClass:CAMetalLayer.class])
                    return view;
            return nil;
        }

        NSVisualEffectMaterial materialOf(VibrancyMaterial m) {
            switch (m) {
            case VibrancyMaterial::Header: return NSVisualEffectMaterialHeaderView;
            case VibrancyMaterial::Menu: return NSVisualEffectMaterialMenu;
            case VibrancyMaterial::Popover: return NSVisualEffectMaterialPopover;
            case VibrancyMaterial::Titlebar: return NSVisualEffectMaterialTitlebar;
            case VibrancyMaterial::Window: return NSVisualEffectMaterialWindowBackground;
            case VibrancyMaterial::Content: return NSVisualEffectMaterialContentBackground;
            case VibrancyMaterial::Hud: return NSVisualEffectMaterialHUDWindow;
            case VibrancyMaterial::Sheet: return NSVisualEffectMaterialSheet;
            default: return NSVisualEffectMaterialSidebar;
            }
        }
    } // namespace

    bool VibrancyAvailable() {
        return true;
    }

    void Vibrancy(ImVec2 pos, ImVec2 size, VibrancyMaterial material) {
        gRequest = {true, pos, size, material};
    }

    namespace detail {
        bool vibrancyActive() {
            return gActive;
        }

        void applyVibrancy() {
            const bool want = gRequest.set;
            NSView* host = (__bridge NSView*)nativeHost();
            if (host) {
                if (want && !gView) {
                    gView = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
                    gView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
                    gView.state = NSVisualEffectStateActive; // not only while the window is key
                    NSView* container = metalContainer(host);
                    if (container)
                        [host addSubview:gView positioned:NSWindowBelow relativeTo:container];
                    else
                        [host addSubview:gView positioned:NSWindowBelow relativeTo:nil];
                }
                if (gView) {
                    gView.hidden = !want;
                    if (want) {
                        const NSRect bounds = host.bounds;
                        const CGFloat y = host.isFlipped ? gRequest.pos.y : bounds.size.height - (gRequest.pos.y + gRequest.size.y);
                        gView.frame = NSMakeRect(gRequest.pos.x, y, gRequest.size.x, gRequest.size.y);
                        gView.material = materialOf(gRequest.material);
                    }
                }
            }
            if (want != gActive)
                requestFrames(); // the clear colour changes with it, from the next frame
            gActive = want;
            gRequest.set = false;
        }
    } // namespace detail
} // namespace imshell

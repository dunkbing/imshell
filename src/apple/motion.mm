// the system's reduce motion setting (macos: accessibility display; ios: uiaccessibility)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

namespace imshell::detail {
    bool SystemReduceMotion() {
#if TARGET_OS_IPHONE
        return UIAccessibilityIsReduceMotionEnabled();
#else
        return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
#endif
    }
} // namespace imshell::detail

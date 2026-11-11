#include "imshell/haptics.hpp"

#include <TargetConditionals.h>
#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

void imshell::PlayHaptic(const Haptic kind) {
#if TARGET_OS_IOS
    switch (kind) {
    case Haptic::Tap:
        [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
        break;
    case Haptic::Selection:
        [[UISelectionFeedbackGenerator new] selectionChanged];
        break;
    case Haptic::LongPress: // light, as the shell's long press always gave
        [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
        break;
    case Haptic::Success:
    case Haptic::Warning:
    case Haptic::Error: {
        const UINotificationFeedbackType type = kind == Haptic::Success
                                                    ? UINotificationFeedbackTypeSuccess
                                                : kind == Haptic::Warning
                                                    ? UINotificationFeedbackTypeWarning
                                                    : UINotificationFeedbackTypeError;
        [[UINotificationFeedbackGenerator new] notificationOccurred:type];
        break;
    }
    }
#else
    const NSHapticFeedbackPattern pattern = kind == Haptic::Tap || kind == Haptic::Selection
                                                ? NSHapticFeedbackPatternAlignment
                                            : kind == Haptic::LongPress
                                                ? NSHapticFeedbackPatternLevelChange
                                                : NSHapticFeedbackPatternGeneric;
    [NSHapticFeedbackManager.defaultPerformer performFeedbackPattern:pattern
                                                     performanceTime:NSHapticFeedbackPerformanceTimeNow];
#endif
}

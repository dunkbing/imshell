#pragma once

// ios (objective-c++ only): uikit owns the loop, so the app starts through RunUIKit
#import <MetalKit/MetalKit.h>
#import <UIKit/UIKit.h>

// the root controller: owns the imgui view (touch, pointer, hardware and soft keyboard,
// edit menu) and draws it each display refresh. subclass it for native chrome (navigation
// items, key commands) and call super. its navigation bar is hidden unless a subclass shows it
@interface MydearViewController
    : UIViewController <MTKViewDelegate, UIGestureRecognizerDelegate, UIEditMenuInteractionDelegate>
@end

namespace mydear {
    // from main(): runs uikit with `root` (MydearViewController or a subclass) inside a
    // UINavigationController. create the mydear::App in root's loadView, after [super loadView]
    int RunUIKit(int argc, char* argv[], Class root);
} // namespace mydear

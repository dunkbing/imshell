#pragma once

#include <functional>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// a native tab bar along the bottom of the screen (ios): Liquid Glass on ios 26 and later, a
// system blur below. imgui keeps drawing the content; the bar is a UIKit view above the imgui
// view, so its touches never reach imgui. SafeArea().w is the bar's height while it shows, so
// ContentRect() and the work area stop above it. it hides while the soft keyboard is up.
// on the other platforms the calls do nothing
namespace imshell::ios {
    struct TabBarItem {
        std::string label;
        std::string symbol; // an SF Symbol name
        bool operator==(const TabBarItem&) const = default;
    };

#if defined(__APPLE__) && TARGET_OS_IPHONE
    // the items and the selected one; onSelect(index) runs on the main thread when one is tapped
    // (the selection has already moved). calling it again with the same items only updates the
    // selection and the callback, so a host can call it every frame
    void SetTabBar(std::vector<TabBarItem> items, int selected, std::function<void(int)> onSelect);
    void SetTabBarVisible(bool visible); // an empty item list hides it too
#else
    inline void SetTabBar(std::vector<TabBarItem>, int, std::function<void(int)>) {}
    inline void SetTabBarVisible(bool) {}
#endif
} // namespace imshell::ios

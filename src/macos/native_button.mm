// imshell::NativeButton: an NSButton placed by NativeView
#include "imgui_internal.h"
#include "imshell/frames.hpp"
#include "imshell/native_view.hpp"

#include <unordered_map>
#include <utility>

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

@interface ImShellButtonTarget : NSObject {
@public
    bool clicked;
}
- (void)fire:(id)sender;
@end

@implementation ImShellButtonTarget
- (void)fire:(id)sender {
    clicked = true;
    imshell::requestFrames(); // the click came outside imgui: wake the idle loop
}
@end

namespace imshell {
    namespace {
        std::unordered_map<ImGuiID, float> gWidths; // each button's fitting width, last frame
    }

    bool NativeButton(const char* id, const char* symbol, const char* tooltip, const char* title) {
        const ImGuiID key = ImGui::GetID(id);
        const float side = ImGui::GetFrameHeight();
        const auto known = gWidths.find(key);
        const float width = known != gWidths.end()
                                ? std::max(side, known->second)
                                : side + (title ? ImGui::CalcTextSize(title).x + side : 0);
        NSButton* button = (__bridge NSButton*)NativeView(id, ImVec2(width, side), [&] {
            ImShellButtonTarget* target = [ImShellButtonTarget new];
            NSImage* image = [NSImage imageWithSystemSymbolName:@(symbol)
                                       accessibilityDescription:@(tooltip)];
            NSButton* b = [NSButton buttonWithImage:image target:target action:@selector(fire:)];
            b.bezelStyle = NSBezelStyleAccessoryBarAction;
            b.showsBorderOnlyWhileMouseInside = YES;
            b.title = @"";
            b.imagePosition = NSImageOnly; // a title, when set, puts the image after it
            b.toolTip = @(tooltip);
            // the button holds its target weakly
            objc_setAssociatedObject(b, "target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            return (__bridge_retained void*)b;
        });
        // the imgui item under the view: what the test engine clicks
        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(
            ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), key, &hovered, &held);
        if (!button)
            return pressed;

        // tinted with the imgui text colour, so it follows the theme
        const ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        NSColor* color = [NSColor colorWithSRGBRed:c.x green:c.y blue:c.z alpha:c.w];
        if (![button.contentTintColor isEqual:color])
            button.contentTintColor = color;
        if (title) {
            NSAttributedString* text = [[NSAttributedString alloc]
                initWithString:[@(title) stringByAppendingString:@" "]
                    attributes:@{NSForegroundColorAttributeName : color}];
            if (![button.attributedTitle isEqual:text])
                button.attributedTitle = text;
            button.imagePosition = NSImageTrailing;
        }
        gWidths[key] = button.fittingSize.width;
        ImShellButtonTarget* target = objc_getAssociatedObject(button, "target");
        return std::exchange(target->clicked, false) || pressed;
    }
} // namespace imshell

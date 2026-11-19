// the macos system look as a Theme: nscolor's semantic colors, resolved for the current appearance
#include "imshell/themes.hpp"

#import <AppKit/AppKit.h>

#include <cmath>
#include <string>

namespace imshell {
    namespace {
        ImVec4 rgba(NSColor* color) {
            NSColor* c = [color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
            return c ? ImVec4(c.redComponent, c.greenComponent, c.blueComponent, c.alphaComponent) : ImVec4(0, 0, 0, 1);
        }

        // a translucent system color (label levels, separator) flattened onto the window
        ImVec4 over(const ImVec4& c, const ImVec4& bg) {
            return Mix(bg, ImVec4(c.x, c.y, c.z, 1), c.w);
        }

        float lum(const ImVec4& c) {
            const auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
            return 0.2126f * lin(c.x) + 0.7152f * lin(c.y) + 0.0722f * lin(c.z);
        }

        // black or white text, whichever reads on the fill
        ImVec4 onFill(const ImVec4& fill) {
            const float l = lum(fill);
            return (l + 0.05f) / 0.05f >= 1.05f / (l + 0.05f) ? ImVec4(0, 0, 0, 1) : ImVec4(1, 1, 1, 1);
        }
    } // namespace

    bool SystemThemeAvailable() {
        return true;
    }

    // changes when the user flips light/dark or picks another accent, so the shell can rebuild
    std::string SystemThemeKey() {
        NSAppearance* a = NSApp.effectiveAppearance ?: NSAppearance.currentDrawingAppearance;
        __block ImVec4 accent;
        [a performAsCurrentDrawingAppearance:^{ accent = rgba(NSColor.controlAccentColor); }];
        return std::string(a.name.UTF8String) + std::to_string(accent.x + accent.y * 3 + accent.z * 7);
    }

    Theme SystemTheme() {
        __block Theme t{};
        NSAppearance* a = NSApp.effectiveAppearance ?: NSAppearance.currentDrawingAppearance;
        t.id = "system";
        t.name = "System";
        t.dark = [a bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] ==
                 NSAppearanceNameDarkAqua;
        [a performAsCurrentDrawingAppearance:^{
            const ImVec4 bg = rgba(NSColor.windowBackgroundColor);
            const ImVec4 fg = over(rgba(NSColor.labelColor), bg);
            t.background = bg;
            t.foreground = fg;
            t.card = rgba(NSColor.controlBackgroundColor);
            t.cardForeground = fg;
            t.popover = t.card;
            t.popoverForeground = fg;
            t.sidebar = Mix(bg, fg, 0.03f);
            t.sidebarForeground = fg;
            t.muted = Mix(bg, fg, 0.06f);
            t.mutedForeground = over(rgba(NSColor.secondaryLabelColor), bg);

            t.primary = rgba(NSColor.controlAccentColor);
            t.primaryForeground = onFill(t.primary);
            t.secondary = Mix(bg, fg, 0.08f);
            t.secondaryForeground = fg;
            t.accent = Mix(bg, fg, 0.07f);
            t.accentForeground = fg;
            t.destructive = rgba(NSColor.systemRedColor);
            t.destructiveForeground = onFill(t.destructive);
            t.success = rgba(NSColor.systemGreenColor);
            t.successForeground = onFill(t.success);
            t.warning = rgba(NSColor.systemOrangeColor);
            t.warningForeground = onFill(t.warning);

            t.border = over(rgba(NSColor.separatorColor), bg);
            t.input = rgba(NSColor.textBackgroundColor);
            t.ring = rgba(NSColor.keyboardFocusIndicatorColor);

            t.hues = {rgba(NSColor.systemRedColor),  rgba(NSColor.systemOrangeColor), rgba(NSColor.systemYellowColor),
                      rgba(NSColor.systemGreenColor), rgba(NSColor.systemTealColor),   rgba(NSColor.systemCyanColor),
                      rgba(NSColor.systemBlueColor),  rgba(NSColor.systemPurpleColor), rgba(NSColor.systemPinkColor)};
            t.chart[0] = t.hues.blue;
            t.chart[1] = t.hues.green;
            t.chart[2] = t.hues.orange;
            t.chart[3] = t.hues.purple;
            t.chart[4] = t.hues.red;
            // xcode's reading: pink keywords, red strings, blue numbers, quiet comments
            t.syntax = {t.hues.pink, t.hues.red, t.hues.blue, t.mutedForeground, t.hues.teal, t.hues.sky, fg};
        }];
        return t;
    }
} // namespace imshell

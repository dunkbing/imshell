// every theme's roles read on the surfaces they are drawn on (wcag contrast), saved ids and tag
// keys still resolve. prints the pairs that fall short. built with -DIMSHELL_BUILD_TESTS=ON
#include "imshell/themes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
    int failures = 0;

    float luminance(const ImVec4& c) {
        const auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * lin(c.x) + 0.7152f * lin(c.y) + 0.0722f * lin(c.z);
    }

    float contrast(const ImVec4& a, const ImVec4& b) {
        const float la = luminance(a), lb = luminance(b);
        return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
    }

    void need(const imshell::Theme& t, const char* pair, const ImVec4& fg, const ImVec4& bg, float min) {
        const float c = contrast(fg, bg);
        if (c < min) {
            std::fprintf(stderr, "%-18s %-36s %.2f < %.1f\n", t.id, pair, c, min);
            ++failures;
        }
    }

    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAILED: %s\n", what);
            ++failures;
        }
    }
} // namespace

int main() {
    for (const imshell::Theme& t : imshell::Themes()) {
        // body text 4.5, quiet text and button labels 3 (wcag aa, large text and ui)
        need(t, "foreground on background", t.foreground, t.background, 4.5f);
        need(t, "foreground on popover", t.popoverForeground, t.popover, 4.5f);
        need(t, "foreground on sidebar", t.sidebarForeground, t.sidebar, 4.5f);
        need(t, "foreground on card", t.cardForeground, t.card, 4.5f);
        need(t, "foreground on secondary", t.secondaryForeground, t.secondary, 4.5f);
        need(t, "mutedForeground on background", t.mutedForeground, t.background, 3.0f);
        need(t, "primaryForeground on primary", t.primaryForeground, t.primary, 3.0f);
        need(t, "destructiveForeground on destructive", t.destructiveForeground, t.destructive, 3.0f);
        need(t, "successForeground on success", t.successForeground, t.success, 3.0f);
        need(t, "warningForeground on warning", t.warningForeground, t.warning, 3.0f);
        need(t, "destructive on background", t.destructive, t.background, 3.0f);
        need(t, "primary on background (links)", t.primary, t.background, 3.0f);
    }
    // the switch, and an edited copy stays current
    imshell::Theme edited = imshell::Themes()[1];
    edited.primary = ImVec4(1, 0, 0, 1);
    imshell::SetTheme(edited);
    check(imshell::CurrentTheme().primary.x == 1 && imshell::CurrentTheme().primary.y == 0, "SetTheme keeps a copy");
    imshell::SetTheme(imshell::Themes()[2]);
    check(imshell::CurrentTheme().id == imshell::Themes()[2].id, "SetTheme");

    std::printf("%s\n", failures ? "theme: FAILED" : "theme: ok");
    return failures ? 1 : 0;
}

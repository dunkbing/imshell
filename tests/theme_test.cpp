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
        need(t, "destructive on background", t.destructive, t.background, 3.0f);
        need(t, "primary on background (links)", t.primary, t.background, 3.0f);
        check(&imshell::ThemeById(t.id) == &t, t.id);
    }
    // ids saved by older versions
    check(std::string_view(imshell::ThemeById("light").id) == "one-light", "light");
    check(std::string_view(imshell::ThemeById("dark").id) == "one-dark", "dark");
    check(&imshell::ThemeById("no-such-theme") == &imshell::Themes()[0], "unknown id");

    // the theme switch and the tag keys stored in settings
    imshell::SetTheme("github-light");
    check(std::string_view(imshell::theme().id) == "github-light", "SetTheme");
    ImVec4 c;
    for (const char* key : {"red", "peach", "yellow", "green", "blue", "purple"})
        check(imshell::TagColor(key, c), key);
    check(!imshell::TagColor("", c) && !imshell::TagColor("teal", c), "unknown tag");

    std::printf("%s\n", failures ? "theme: FAILED" : "theme: ok");
    return failures ? 1 : 0;
}

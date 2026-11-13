#pragma once

#include "imgui.h"

#include <span>
#include <string_view>

// imshell's design tokens: colors named by what they are for, a radius scale and a spacing
// scale. every theme sets every role for its light or dark look; widgets and apps draw with
// roles, never a fixed color, so a theme change reaches everything (docs/ui.md)
//
//   const imshell::Theme& t = imshell::theme();
//   ImGui::TextColored(t.mutedForeground, "Last saved 2 min ago");
//   imshell::SetTheme("github-dark");
namespace imshell {
    struct Theme {
        const char* id;   // stored in settings
        const char* name; // for a picker
        bool dark;

        // surfaces, each with the text drawn on it
        ImVec4 background, foreground;      // the window
        ImVec4 card, cardForeground;        // a panel set apart from the background
        ImVec4 popover, popoverForeground;  // menus, popups, tooltips
        ImVec4 sidebar, sidebarForeground;  // side panels, the title bar, the tab strip
        ImVec4 muted, mutedForeground;      // quiet fills; secondary text, hints, placeholders

        // emphasis
        ImVec4 primary, primaryForeground;         // the main action, selection, links
        ImVec4 secondary, secondaryForeground;     // ordinary buttons
        ImVec4 accent, accentForeground;           // a hovered or focused row
        ImVec4 destructive, destructiveForeground; // destructive actions and errors only

        // status, only for its meaning
        ImVec4 success, warning;

        // structure
        ImVec4 border; // dividers, outlines of cards, popups and fields
        ImVec4 input;  // a text field's fill
        ImVec4 ring;   // the keyboard focus outline

        ImVec4 chart[5]; // data series, in this order

        struct Syntax {
            ImVec4 keyword, string, number, comment, function, type, operator_;
        } syntax; // code in editors and markdown

        // corners: controls and popups Md, containers Lg, small controls and tags Sm
        struct Radius {
            static constexpr float Sm = 3.0f;
            static constexpr float Md = 6.0f;
            static constexpr float Lg = 8.0f;
            static constexpr float Full = 9999.0f;
        };
        struct Spacing {
            static constexpr float XS = 2.0f;
            static constexpr float S = 4.0f;
            static constexpr float M = 8.0f;
            static constexpr float L = 16.0f;
            static constexpr float XL = 20.0f;
        };
    };
    using Radius = Theme::Radius;
    using Spacing = Theme::Spacing;

    std::span<const Theme> Themes();
    // by settings id; an unknown id is the first theme ("light" and "dark", from older
    // versions, are One Light and One Dark)
    const Theme& ThemeById(std::string_view id);

    // the theme widgets draw with
    const Theme& theme();
    // makes it current and styles imgui with it (once a context exists; before, Run applies it)
    void SetTheme(std::string_view id);
    void SetTheme(const Theme& theme);
    // imgui's style from the current theme: the shells call it, an app with its own context too
    void ApplyTheme();

    // color steps for states: a fill hovered and pressed, toward the theme's foreground
    ImVec4 Hovered(const ImVec4& color);
    ImVec4 Pressed(const ImVec4& color);
    ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t);
    ImVec4 WithAlpha(const ImVec4& color, float alpha);

    // a color the user picked for something (a connection), stored by key so it reads in every
    // theme: "red", "peach" (orange), "yellow", "green", "blue", "purple". false for an empty or
    // unknown key, leaving out as it was
    bool TagColor(std::string_view key, ImVec4& out);

    // a strip along the bottom of a tab (pagination); tabs without one leave it free too
    float BottomBarHeight();
} // namespace imshell

#include "imshell/button.hpp"

#include "imgui_internal.h" // ImTextCharFromUtf8
#include <cmath>

#include "imshell/themes.hpp"

using namespace imshell;

namespace {
    // the variant's colors (secondary and custom keep the style's, which the theme sets); how
    // many it pushed, for popVariantColors
    int pushVariantColors(imshell::ButtonVariant variant) {
        const Theme& t = CurrentTheme();
        const auto push = [](const ImVec4& rest, const ImVec4& hovered, const ImVec4& pressed, const ImVec4& text) {
            ImGui::PushStyleColor(ImGuiCol_Button, rest);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, pressed);
            ImGui::PushStyleColor(ImGuiCol_Text, text);
            return 4;
        };
        switch (variant) {
        case imshell::ButtonVariant::Primary:
            return push(t.primary, Hovered(t.primary), Pressed(t.primary), t.primaryForeground);
        case imshell::ButtonVariant::Destructive:
            return push(t.destructive, Hovered(t.destructive), Pressed(t.destructive), t.destructiveForeground);
        case imshell::ButtonVariant::Ghost:
        case imshell::ButtonVariant::Outline: // the border is the frame's (FrameBorderSize)
            return push(ImVec4(0, 0, 0, 0), t.accent, Pressed(t.accent), t.foreground);
        default:
            return 0;
        }
    }

    void popVariantColors(int pushed) {
        ImGui::PopStyleColor(pushed);
    }

    // an icon button is transparent at rest; primary and destructive tint on hover
    void pushIconColors(imshell::ButtonVariant variant) {
        const Theme& t = CurrentTheme();
        ImVec4 hovered = t.accent;
        ImVec4 active = Pressed(t.accent);
        if (variant == imshell::ButtonVariant::Primary || variant == imshell::ButtonVariant::Destructive) {
            const ImVec4& hue = variant == imshell::ButtonVariant::Primary ? t.primary : t.destructive;
            hovered = WithAlpha(hue, 0.20f);
            active = WithAlpha(hue, 0.40f);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    }

    void drawButtonShadow(const char* label, const ImVec2& size, const ImVec2& padding) {
        if (ImGui::GetStyleColorVec4(ImGuiCol_Button).w <= 0.0f) {
            return;
        }

        const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
        const ImVec2 buttonSize =
            ImGui::CalcItemSize(size, textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const float baseAlpha = CurrentTheme().dark ? 0.28f : 0.16f;
        constexpr int layers = 2; // a faint lift, not a drop shadow

        for (int layer = layers; layer >= 0; --layer) {
            const float spread = static_cast<float>(layer) * 0.5f;
            const float strength =
                1.0f - static_cast<float>(layer) / static_cast<float>(layers + 1);
            const float alpha = baseAlpha * strength * 0.22f;
            const ImVec2 min(cursorPos.x - spread, ImMax(cursorPos.y + 1.0f - spread, cursorPos.y));
            const ImVec2 max(cursorPos.x + buttonSize.x + spread,
                             cursorPos.y + buttonSize.y + 1.0f + spread);

            ImGui::GetWindowDrawList()->AddRectFilled(
                min, max, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, alpha)),
                Radius::Md + spread);
        }
    }
} // namespace

bool imshell::Button(const char* label, ButtonVariant variant, const ImVec2& size) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, variant == ButtonVariant::Outline ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius::Md);
    const int pushed = pushVariantColors(variant);
    drawButtonShadow(label, size, ImGui::GetStyle().FramePadding);
    const bool clicked = ImGui::Button(label, size);
    popVariantColors(pushed);
    ImGui::PopStyleVar(2);
    return clicked;
}

bool imshell::SmallButton(const char* label, ButtonVariant variant) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, variant == ButtonVariant::Outline ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius::Md);
    const int pushed = pushVariantColors(variant);
    drawButtonShadow(label, ImVec2(0.0f, 0.0f), ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
    const bool clicked = ImGui::SmallButton(label);
    popVariantColors(pushed);
    ImGui::PopStyleVar(2);
    return clicked;
}

bool imshell::IconButton(const char* icon, ButtonVariant variant, const ImVec2& size,
                         ButtonSize buttonSize) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius::Md);
    pushIconColors(variant);
    // one glyph in a sized button: centred by the shape it draws, not by its advance and the
    // line's height (icon glyphs sit off centre in those, so they look shifted in the box)
    unsigned int codepoint = 0;
    const int bytes = ImTextCharFromUtf8(&codepoint, icon, nullptr);
    const ImFontGlyph* glyph = buttonSize != ButtonSize::Small && bytes > 0 && icon[bytes] == '\0'
                                   ? ImGui::GetFont()->GetFontBaked(ImGui::GetFontSize())->FindGlyphNoFallback(static_cast<ImWchar>(codepoint))
                                   : nullptr;
    bool clicked;
    if (glyph && glyph->Visible) {
        // the label stays the id (lookups by label keep working), drawn clear
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        clicked = ImGui::Button(icon, size);
        ImGui::PopStyleColor();
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const ImVec2 at(std::round((min.x + max.x) * 0.5f - (glyph->X0 + glyph->X1) * 0.5f),
                        std::round((min.y + max.y) * 0.5f - (glyph->Y0 + glyph->Y1) * 0.5f));
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, text, icon);
    } else {
        clicked = buttonSize == ButtonSize::Small ? ImGui::SmallButton(icon) : ImGui::Button(icon, size);
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return clicked;
}

bool imshell::IconLabelButton(const char* icon, const char* label, ButtonVariant variant) {
    ImFont* font = ImGui::GetFont();
    const float iconSize = ImGui::GetFontSize() * 0.8f;
    const float iconWidth = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon).x;
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 size(pad.x * 2.0f + iconWidth + Theme::Spacing::S + 2.0f + textSize.x,
                      textSize.y + pad.y * 2.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, variant == ButtonVariant::Outline ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius::Md);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
    const int pushed = pushVariantColors(variant);
    drawButtonShadow(label, size, pad);
    const bool clicked = ImGui::Button(label, size);
    const ImVec2 min = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(font, iconSize,
                                        ImVec2(min.x + pad.x, min.y + (size.y - iconSize) * 0.5f),
                                        ImGui::GetColorU32(ImGuiCol_Text), icon);
    popVariantColors(pushed);
    ImGui::PopStyleVar(3);
    return clicked;
}

bool imshell::SegmentedTabs(const char* id, std::initializer_list<const char*> labels,
                            int* current) {
    const Theme& t = CurrentTheme();
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius::Md);
    int i = 0;
    for (const char* label : labels) {
        if (i > 0)
            ImGui::SameLine(0, Theme::Spacing::XS);
        const bool selected = *current == i;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? t.accent : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? t.accent : Hovered(t.muted));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Pressed(t.accent));
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? t.foreground : t.mutedForeground);
        if (ImGui::Button(label) && !selected) {
            *current = i;
            changed = true;
        }
        ImGui::PopStyleColor(4);
        ++i;
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

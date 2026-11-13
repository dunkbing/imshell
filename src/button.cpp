#include "imshell/button.hpp"

#include "imgui_internal.h" // ImTextCharFromUtf8
#include <cmath>

#include "imshell/themes.hpp"

using namespace imshell;

namespace {
    void pushVariantColors(imshell::ButtonVariant variant) {
        // secondary takes the theme's button colours (themes.hpp)
        if (variant == imshell::ButtonVariant::Custom ||
            variant == imshell::ButtonVariant::Secondary) {
            return;
        }

        const auto& colors = Theme::Current()->colors;
        const ImVec4 accent =
            variant == imshell::ButtonVariant::Primary ? colors.green : colors.red;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, colors.base);
    }

    void popVariantColors(imshell::ButtonVariant variant) {
        if (variant != imshell::ButtonVariant::Custom &&
            variant != imshell::ButtonVariant::Secondary) {
            ImGui::PopStyleColor(4);
        }
    }

    void pushIconColors(imshell::ButtonVariant variant) {
        const auto& colors = Theme::Current()->colors;
        ImVec4 hovered = colors.surface1;
        ImVec4 active = colors.surface2;

        if (variant == imshell::ButtonVariant::Primary ||
            variant == imshell::ButtonVariant::Danger) {
            const ImVec4 accent =
                variant == imshell::ButtonVariant::Primary ? colors.green : colors.red;
            hovered = ImVec4(accent.x, accent.y, accent.z, 0.25f);
            active = ImVec4(accent.x, accent.y, accent.z, 0.4f);
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
        const float baseAlpha = Theme::Current()->dark ? 0.28f : 0.16f;
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
                Theme::CornerRadius::SMALL + spread);
        }
    }
} // namespace

bool imshell::Button(const char* label, ButtonVariant variant, const ImVec2& size) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::CornerRadius::SMALL);
    pushVariantColors(variant);
    drawButtonShadow(label, size, ImGui::GetStyle().FramePadding);
    const bool clicked = ImGui::Button(label, size);
    popVariantColors(variant);
    ImGui::PopStyleVar(2);
    return clicked;
}

bool imshell::SmallButton(const char* label, ButtonVariant variant) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::CornerRadius::SMALL);
    pushVariantColors(variant);
    drawButtonShadow(label, ImVec2(0.0f, 0.0f), ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
    const bool clicked = ImGui::SmallButton(label);
    popVariantColors(variant);
    ImGui::PopStyleVar(2);
    return clicked;
}

bool imshell::IconButton(const char* icon, ButtonVariant variant, const ImVec2& size,
                         ButtonSize buttonSize) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::CornerRadius::SMALL);
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

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::CornerRadius::SMALL);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
    pushVariantColors(variant);
    drawButtonShadow(label, size, pad);
    const bool clicked = ImGui::Button(label, size);
    const ImVec2 min = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(font, iconSize,
                                        ImVec2(min.x + pad.x, min.y + (size.y - iconSize) * 0.5f),
                                        ImGui::GetColorU32(ImGuiCol_Text), icon);
    popVariantColors(variant);
    ImGui::PopStyleVar(3);
    return clicked;
}

bool imshell::SegmentedTabs(const char* id, std::initializer_list<const char*> labels,
                            int* current) {
    const auto& colors = Theme::Current()->colors;
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::CornerRadius::SMALL);
    int i = 0;
    for (const char* label : labels) {
        if (i > 0)
            ImGui::SameLine(0, Theme::Spacing::XS);
        const bool selected = *current == i;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? colors.surface1 : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? colors.surface1 : colors.surface0);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors.surface2);
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? colors.text : colors.subtext0);
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

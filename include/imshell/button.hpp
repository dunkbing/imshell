#pragma once

#include "imgui.h"
#include <initializer_list>

namespace imshell {
    // by meaning: primary for the one main action, destructive for deleting and other actions
    // that lose something, secondary for the rest; ghost and outline are quieter (toolbars, a
    // cancel next to a primary). custom draws with whatever colors the caller pushed
    enum class ButtonVariant {
        Secondary,
        Primary,
        Destructive,
        Ghost,
        Outline,
        Custom,
    };

    enum class ButtonSize {
        Default,
        Small,
    };

    bool Button(const char* label, ButtonVariant variant = ButtonVariant::Secondary,
                const ImVec2& size = ImVec2(0.0f, 0.0f));
    bool SmallButton(const char* label, ButtonVariant variant = ButtonVariant::Secondary);
    // a row of small toggle buttons, one selected; true when the selection changed
    bool SegmentedTabs(const char* id, std::initializer_list<const char*> labels, int* current);
    // label button with a smaller icon in front of the text
    bool IconLabelButton(const char* icon, const char* label,
                         ButtonVariant variant = ButtonVariant::Secondary);
    bool IconButton(const char* icon, ButtonVariant variant = ButtonVariant::Secondary,
                    const ImVec2& size = ImVec2(0.0f, 0.0f),
                    ButtonSize buttonSize = ButtonSize::Default);
} // namespace imshell

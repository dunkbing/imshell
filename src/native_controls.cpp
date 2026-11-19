#include "imshell/native_controls.hpp"

#include "imshell/button.hpp"
#include "imshell/icons.hpp"
#include "imshell/native_view.hpp"
#include "imshell/toggle_button.hpp"
#include "native_controls.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace imshell {
    bool NativeControlsAvailable() {
        return detail::nativeControlsAvailable();
    }

    bool NativeSwitch(const char* id, bool* value) {
        if (detail::nativeControlsAvailable())
            return detail::nativeSwitch(id, value);
        const bool before = *value;
        ToggleButton(id, value);
        return *value != before;
    }

    bool NativeSegmented(const char* id, std::initializer_list<const char*> labels, int* current) {
        if (detail::nativeControlsAvailable())
            return detail::nativeSegmented(id, std::span<const char* const>(labels.begin(), labels.size()), current);
        return SegmentedTabs(id, labels, current);
    }

#if !defined(__APPLE__) || TARGET_OS_IPHONE
    // macos has the NSButton (src/macos/native_button.mm); everywhere else a ghost button
    bool NativeButton(const char* id, const char* symbol, const char* tooltip, const char* title) {
        ImGui::PushID(id);
        const char* glyph = Icon(symbol);
        bool clicked;
        if (glyph && title)
            clicked = IconLabelButton(glyph, title, ButtonVariant::Ghost);
        else if (glyph)
            clicked = IconButton(glyph, ButtonVariant::Ghost);
        else
            clicked = Button(title ? title : tooltip, ButtonVariant::Ghost);
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", tooltip);
        ImGui::PopID();
        return clicked;
    }
#endif
} // namespace imshell

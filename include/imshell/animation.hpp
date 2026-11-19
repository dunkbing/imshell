#pragma once

#include "imgui.h"

#include <optional>

// a value that chases its target with a spring, so motion eases in and out and keeps its
// momentum when the target changes mid-way (a hover that ends half-way turns back smoothly, as
// in swiftui). state is kept by id between frames; the loop is asked for frames until the value
// settles, so an idle app still sleeps
//
//   const float lift = imshell::Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
//   const ImVec4 fill = imshell::AnimateColor(id, hovered ? t.accent : t.secondary);
//
// the first call for an id starts at the target (nothing animates on first sight). with the
// system's reduce motion setting on (apple) values jump to the target
namespace imshell {
    struct Spring {
        float response = 0.25f; // seconds for one swing: smaller is snappier
        float damping = 1.0f;   // 1 settles without overshoot; below 1 bounces
    };

    // swiftui's presets by feel
    inline constexpr Spring kSnappy{0.18f, 1.0f};  // hovers, presses
    inline constexpr Spring kSmooth{0.30f, 1.0f};  // panels, fades
    inline constexpr Spring kBouncy{0.35f, 0.65f}; // a little overshoot

    // the value for this frame, moving toward `target`
    float Animate(ImGuiID id, float target, Spring spring = kSnappy);
    ImVec2 Animate(ImGuiID id, ImVec2 target, Spring spring = kSnappy);
    // each channel on its own, in srgb
    ImVec4 AnimateColor(ImGuiID id, const ImVec4& target, Spring spring = kSnappy);

    // jumps `id` to `value` with no motion (a row that was recycled for another item)
    void SnapAnimation(ImGuiID id, float value);

    // true while any value is still moving
    bool Animating();

    // the system's reduce motion setting (apple; false elsewhere until a loader exists)
    bool ReduceMotion();
    // forces it on or off for the process, above the system and the environment (a video render turns it on so
    // values do not depend on the frames before); nullopt hands it back
    void SetReduceMotion(std::optional<bool> forced);
} // namespace imshell

#pragma once

#include "im_anim.h"
#include "imgui.h"

// ImAnim (the external/imanim submodule) wired to imshell: tweens, keyframe clips, paths, noise
// and colour blending in OKLAB, for what Animate's spring (imshell/animation.hpp) does not
// cover. a host links imshell::anim. ImAnim does not say when it is idle, and an idle imshell
// app sleeps, so the frame loop needs a nudge while something moves: Settle does it for a tween
//
//   imshell::anim::BeginFrame(); // once per frame, after ImGui::NewFrame()
//   const float alpha = imshell::anim::Settle(
//       iam_tween_float(id, ImHashStr("alpha"), shown ? 1.0f : 0.0f, 0.3f,
//                       iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, ImGui::GetIO().DeltaTime),
//       shown ? 1.0f : 0.0f);
//
// a clip that plays calls imshell::requestFrames() itself while it does
namespace imshell::anim {
    // iam_update_begin_frame and iam_clip_update with this frame's delta time
    void BeginFrame();

    // returns `value`, and asks the loop for frames while it has not reached `target`
    float Settle(float value, float target);
    ImVec2 Settle(ImVec2 value, ImVec2 target);
    ImVec4 Settle(ImVec4 value, ImVec4 target);
} // namespace imshell::anim

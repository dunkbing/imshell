#include "imshell/anim.hpp"

#include "imshell/frames.hpp"

#include <cmath>

namespace imshell::anim {
    void BeginFrame() {
        iam_update_begin_frame();
        iam_clip_update(ImGui::GetIO().DeltaTime);
    }

    namespace {
        bool differs(float a, float b) {
            return std::abs(a - b) > 0.001f;
        }
    } // namespace

    float Settle(float value, float target) {
        if (differs(value, target))
            requestFrames(std::chrono::milliseconds(100));
        return value;
    }

    ImVec2 Settle(ImVec2 value, ImVec2 target) {
        if (differs(value.x, target.x) || differs(value.y, target.y))
            requestFrames(std::chrono::milliseconds(100));
        return value;
    }

    ImVec4 Settle(ImVec4 value, ImVec4 target) {
        if (differs(value.x, target.x) || differs(value.y, target.y) || differs(value.z, target.z) ||
            differs(value.w, target.w))
            requestFrames(std::chrono::milliseconds(100));
        return value;
    }
} // namespace imshell::anim

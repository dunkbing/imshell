#include "imshell/animation.hpp"

#include "imshell/frames.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace imshell::detail {
    bool SystemReduceMotion(); // per platform: apple/motion.mm, motion_none.cpp
}

namespace imshell {
    namespace {
        struct State {
            float x[4] = {}, v[4] = {};
            int frame = -1; // the frame it was last stepped in
        };

        std::unordered_map<ImGuiID, State>& states() {
            static std::unordered_map<ImGuiID, State> s;
            return s;
        }

        int gMovingFrame = -100; // the last frame a value was still moving

        // IMSHELL_REDUCE_MOTION=1 or 0 overrides the system's setting (tests, a user who wants it)
        bool reduceMotionNow() {
            if (const char* env = std::getenv("IMSHELL_REDUCE_MOTION"); env && *env)
                return *env != '0';
            return detail::SystemReduceMotion();
        }

        // one frame of a damped spring on n channels: semi-implicit euler in small steps, which
        // stays stable for any response a ui uses and keeps the velocity between calls
        void step(State& s, const float* target, int n, Spring spring, float dt) {
            const float omega = 6.2831853f / std::max(spring.response, 0.01f);
            const float k = omega * omega, c = 2.0f * std::max(spring.damping, 0.0f) * omega;
            constexpr float kStep = 1.0f / 240.0f;
            for (float left = dt; left > 0.0f; left -= kStep) {
                const float h = std::min(kStep, left);
                for (int i = 0; i < n; ++i) {
                    s.v[i] += (k * (target[i] - s.x[i]) - c * s.v[i]) * h;
                    s.x[i] += s.v[i] * h;
                }
            }
        }

        bool settled(const State& s, const float* target, int n) {
            for (int i = 0; i < n; ++i)
                if (std::abs(s.x[i] - target[i]) > 0.001f || std::abs(s.v[i]) > 0.01f)
                    return false;
            return true;
        }

        void prune(int frame) {
            if (frame % 512 != 0)
                return;
            std::erase_if(states(), [frame](const auto& e) { return frame - e.second.frame > 600; });
        }

        // the value of `id` this frame, in out[0..n)
        void animate(ImGuiID id, const float* target, int n, Spring spring, float* out) {
            const int frame = ImGui::GetFrameCount();
            auto [it, fresh] = states().try_emplace(id);
            State& s = it->second;
            if (fresh) {
                std::copy(target, target + n, s.x); // nothing animates on first sight
            } else if (s.frame != frame) {
                if (reduceMotionNow()) {
                    std::copy(target, target + n, s.x);
                    std::fill(s.v, s.v + 4, 0.0f);
                } else {
                    // a long pause (a hidden window) is not a long step
                    step(s, target, n, spring, std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f));
                    if (settled(s, target, n)) {
                        std::copy(target, target + n, s.x);
                        std::fill(s.v, s.v + 4, 0.0f);
                    }
                }
            }
            if (s.frame != frame && !settled(s, target, n)) {
                gMovingFrame = frame;
                requestFrames(std::chrono::milliseconds(100)); // until it settles, then the app may sleep
            }
            s.frame = frame;
            prune(frame);
            std::copy(s.x, s.x + n, out);
        }
    } // namespace

    float Animate(ImGuiID id, float target, Spring spring) {
        float out;
        animate(id, &target, 1, spring, &out);
        return out;
    }

    ImVec2 Animate(ImGuiID id, ImVec2 target, Spring spring) {
        const float t[2] = {target.x, target.y};
        float out[2];
        animate(id, t, 2, spring, out);
        return ImVec2(out[0], out[1]);
    }

    ImVec4 AnimateColor(ImGuiID id, const ImVec4& target, Spring spring) {
        const float t[4] = {target.x, target.y, target.z, target.w};
        float out[4];
        animate(id, t, 4, spring, out);
        return ImVec4(out[0], out[1], out[2], out[3]);
    }

    void SnapAnimation(ImGuiID id, float value) {
        State& s = states()[id];
        s = State{};
        s.x[0] = value;
        s.frame = ImGui::GetFrameCount();
    }

    bool Animating() {
        return ImGui::GetFrameCount() - gMovingFrame <= 1;
    }

    bool ReduceMotion() {
        return reduceMotionNow();
    }
} // namespace imshell

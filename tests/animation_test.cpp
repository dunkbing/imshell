// the spring: starts at its target, settles without overshoot when critically damped, bounces
// when under-damped, keeps its velocity when retargeted, steps once per frame, asks for frames
// only while moving, and jumps under reduce motion
#include "imshell/animation.hpp"
#include "imshell/frames.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
    int failed = 0;

    void check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++failed;
        }
    }

    void frame() {
        ImGui::NewFrame();
        ImGui::Render();
    }

    constexpr ImGuiID kId = 7;
} // namespace

int main() {
#ifdef _WIN32
    _putenv_s("IMSHELL_REDUCE_MOTION", "0");
#else
    setenv("IMSHELL_REDUCE_MOTION", "0", 1);
#endif
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(100, 100);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();

    frame();
    check(imshell::Animate(kId, 5.0f) == 5.0f, "the first call starts at the target");

    // critically damped: up to 1 with no overshoot, settled inside a second
    float peak = 0, value = 0;
    imshell::framesWantedUntilMs().store(0);
    int frames = 0;
    bool asked = false;
    for (; frames < 120; ++frames) {
        frame();
        value = imshell::Animate(kId, 1.0f, imshell::kSnappy);
        peak = std::max(peak, value);
        asked = asked || imshell::framesRequested();
    }
    check(value == 1.0f, "settles exactly on the target");
    check(peak <= 5.0f && peak >= 1.0f, "moves from 5 down toward 1");
    check(asked, "asks for frames while moving");

    imshell::framesWantedUntilMs().store(0);
    for (int i = 0; i < 10; ++i) {
        frame();
        imshell::Animate(kId, 1.0f, imshell::kSnappy);
    }
    check(!imshell::framesRequested() && !imshell::Animating(), "asks for none once settled");

    // from 0 to 1 with no overshoot (critical) and with overshoot (bouncy)
    float critical = 0, bouncy = 0;
    imshell::SnapAnimation(10, 0.0f);
    imshell::SnapAnimation(11, 0.0f);
    for (int i = 0; i < 90; ++i) {
        frame();
        critical = std::max(critical, imshell::Animate(10, 1.0f, imshell::kSnappy));
        bouncy = std::max(bouncy, imshell::Animate(11, 1.0f, imshell::kBouncy));
    }
    check(critical <= 1.001f, "critical damping does not overshoot");
    check(bouncy > 1.02f, "under-damped overshoots");

    // retarget half-way: no jump, and the momentum carries on before it turns back
    imshell::SnapAnimation(12, 0.0f);
    float before = 0;
    for (int i = 0; i < 6; ++i) {
        frame();
        before = imshell::Animate(12, 1.0f, imshell::kSmooth);
    }
    float after = before, furthest = before;
    for (int i = 0; i < 30; ++i) {
        frame();
        const float next = imshell::Animate(12, 0.0f, imshell::kSmooth);
        if (i == 0)
            check(std::abs(next - before) < 0.1f, "retargeting does not jump");
        furthest = std::max(furthest, next);
        after = next;
    }
    check(furthest > before, "retargeting keeps the momentum");
    check(after < before, "and then turns back toward the new target");

    // twice in one frame is one step
    imshell::SnapAnimation(13, 0.0f);
    frame();
    const float a = imshell::Animate(13, 1.0f), b = imshell::Animate(13, 1.0f);
    check(a == b, "one step per frame");

    // reduce motion: straight to the target
#ifdef _WIN32
    _putenv_s("IMSHELL_REDUCE_MOTION", "1");
#else
    setenv("IMSHELL_REDUCE_MOTION", "1", 1);
#endif
    check(imshell::ReduceMotion(), "reduce motion read");
    imshell::SnapAnimation(14, 0.0f);
    frame();
    check(imshell::Animate(14, 1.0f) == 1.0f, "reduce motion jumps to the target");

    ImGui::DestroyContext();
    std::printf(failed ? "animation: %d failed\n" : "animation: ok\n", failed);
    return failed ? 1 : 0;
}

// imshell::anim over ImAnim: a tween eases to its target, and Settle asks the loop for frames
// while it moves and stops once it arrives
#include "imshell/anim.hpp"
#include "imshell/frames.hpp"

#include "imgui_internal.h"

#include <cstdio>

namespace {
    int failed = 0;

    void check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++failed;
        }
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(100, 100);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();

    const ImGuiID id = 5, channel = ImHashStr("alpha");
    float value = 0, middle = 0;
    bool asked = false;
    imshell::framesWantedUntilMs().store(0);
    for (int frame = 0; frame < 60; ++frame) {
        ImGui::NewFrame();
        imshell::anim::BeginFrame();
        value = imshell::anim::Settle(iam_tween_float(id, channel, 1.0f, 0.3f, iam_ease_preset(iam_ease_out_cubic),
                                                      iam_policy_crossfade, io.DeltaTime),
                                      1.0f);
        if (frame == 5) {
            middle = value;
            asked = imshell::framesRequested();
        }
        ImGui::Render();
    }
    check(middle > 0.0f && middle < 1.0f, "the tween is part-way after a few frames");
    check(asked, "Settle asks for frames while it moves");
    check(value > 0.999f, "the tween reaches its target");
    imshell::framesWantedUntilMs().store(0);
    ImGui::NewFrame();
    imshell::anim::BeginFrame();
    imshell::anim::Settle(iam_tween_float(id, channel, 1.0f, 0.3f, iam_ease_preset(iam_ease_out_cubic),
                                          iam_policy_crossfade, io.DeltaTime),
                          1.0f);
    ImGui::Render();
    check(!imshell::framesRequested(), "and asks for none once it arrived");

    ImGui::DestroyContext();
    std::printf(failed ? "anim: %d failed\n" : "anim: ok\n", failed);
    return failed ? 1 : 0;
}

// the fallback path of the native controls: with no backend (this test links none) NativeSwitch,
// NativeSegmented and NativeButton are imshell's own widgets, report no change when nobody
// clicked, and leave the values alone
#include "imshell/native_controls.hpp"
#include "imshell/native_view.hpp"
#include "imshell/themes.hpp"

#include "imgui.h"

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
    check(!imshell::NativeControlsAvailable(), "no native controls in this build");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400, 300);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    imshell::ApplyTheme();

    bool on = true, notified = false;
    int view = 1;
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::Begin("w");
        notified = imshell::NativeSwitch("##s", &on);
        const bool segmented = imshell::NativeSegmented("##g", {"List", "Grid", "Cards"}, &view);
        bool button = false;
#if !defined(__APPLE__) // macos has the NSButton instead, which needs a window
        button = imshell::NativeButton("##b", "gearshape", "Settings", "Settings");
#endif
        check(!notified && !segmented && !button, "nothing changes without a click");
        ImGui::End();
        ImGui::Render();
    }
    check(on && view == 1, "the values are left alone");

    ImGui::DestroyContext();
    std::printf(failed ? "native controls: %d failed\n" : "native controls: ok\n", failed);
    return failed ? 1 : 0;
}

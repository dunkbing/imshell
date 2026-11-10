// sameAsLastFrame against a bare imgui context. built with -DMYDEAR_BUILD_TESTS=ON (ctest)
#include "frame_skip.hpp"

#include "imgui.h"

#include <cstdio>

namespace {
    int failures = 0;
    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    }

    const ImVec4 clear(0.1f, 0.1f, 0.1f, 1);

    // one window with a label; returns whether the frame matched the last one
    bool frame(const char* label, ImVec4 bg = clear) {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted(label);
        ImGui::End();
        ImGui::Render();
        const bool same = mydear::detail::sameAsLastFrame(ImGui::GetDrawData(), bg);
        for (ImTextureData* t : ImGui::GetPlatformIO().Textures)
            t->SetStatus(ImTextureStatus_OK); // what a renderer does after uploading
        return same;
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400, 300);
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    // the font atlas uploads over the first frames: those always draw
    check(!frame("hello"), "the first frame draws");
    for (int i = 0; i < 5; ++i)
        frame("hello");
    check(frame("hello"), "an unchanged frame matches");
    check(!frame("world"), "changed text draws");
    // new glyphs upload, then the window fits its new text
    for (int i = 0; i < 3; ++i)
        frame("world");
    check(frame("world"), "and then matches again");
    check(!frame("world", ImVec4(1, 0, 0, 1)), "a new clear colour draws");
    io.DisplaySize = ImVec2(500, 300);
    check(!frame("world"), "a resize draws");

    ImGui::DestroyContext();
    if (failures)
        return 1;
    std::printf("frame skip: ok\n");
    return 0;
}

// the platform's emoji loader (coretext on apple, freetype elsewhere): emoji bake in colour at
// two sizes, skin tones take no width, text is left to the text font. skipped (passes) where
// the machine has no colour emoji font
#include "../src/emoji.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <initializer_list>

int main() {
    const ImFontLoader* loader = imshell::detail::EmojiLoader();
    if (!loader) {
        std::printf("emoji: skipped (no colour emoji font here)\n");
        return 0;
    }
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(100, 100);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImFont* base = io.Fonts->AddFontDefault();
    static unsigned char none = 0;
    ImFontConfig config;
    config.MergeMode = true;
    config.DstFont = base;
    config.FontLoader = loader;
    config.FontData = &none;
    config.FontDataSize = 1;
    config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFont(&config);
    int failed = 0;
    for (const float size : {16.0f, 32.0f}) {
        ImGui::NewFrame();
        ImFontBaked* baked = base->GetFontBaked(size);
        const ImFontGlyph* wave = baked->FindGlyph(static_cast<ImWchar>(0x2705)); // in every build: below U+FFFF
        const ImFontGlyph* letter = baked->FindGlyph('A');
        if (!wave || !wave->Colored || !wave->Visible || wave->X1 - wave->X0 < size * 0.8f || wave->X1 - wave->X0 > size * 1.4f) {
            std::printf("FAIL: U+2705 at %.0f px\n", size);
            ++failed;
        }
        if (!letter || letter->Colored) {
            std::printf("FAIL: 'A' should come from the text font\n");
            ++failed;
        }
#ifdef IMGUI_USE_WCHAR32
        const ImFontGlyph* tone = baked->FindGlyph(static_cast<ImWchar>(0x1F3FD));
        const ImFontGlyph* hand = baked->FindGlyph(static_cast<ImWchar>(0x1F44B));
        if (!hand || !hand->Colored || !tone || tone->AdvanceX != 0 || tone->Visible) {
            std::printf("FAIL: U+1F44B in colour, U+1F3FD with no width, at %.0f px\n", size);
            ++failed;
        }
#endif
        ImGui::Render();
    }
    ImGui::DestroyContext();
    std::printf(failed ? "emoji: %d failed\n" : "emoji: ok\n", failed);
    return failed ? 1 : 0;
}

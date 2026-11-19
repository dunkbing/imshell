// the platform's icon loader (SF Symbols on apple): a symbol bakes as a visible glyph that is
// tinted by the text (not coloured), scales with the font, sits on the text's line; an unknown
// name or a platform with no loader gives the fallback. needs IMGUI_USE_WCHAR32
#include "../src/icons.hpp"
#include "imshell/icons.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>

int main() {
    static const char fallback[] = "fallback";
    int failed = 0;
    if (imshell::Icon("no.such.symbol.anywhere", fallback) != fallback) {
        std::printf("FAIL: an unknown name should give the fallback\n");
        ++failed;
    }
    if (imshell::Icon(nullptr, fallback) != fallback || imshell::Icon("", fallback) != fallback) {
        std::printf("FAIL: no name should give the fallback\n");
        ++failed;
    }
    const ImFontLoader* loader = imshell::detail::IconLoader();
    if (!loader) {
        if (imshell::Icon("gearshape", fallback) != fallback) {
            std::printf("FAIL: no loader, so the fallback\n");
            ++failed;
        }
        std::printf(failed ? "icons: %d failed\n" : "icons: ok (no loader here, fallback only)\n", failed);
        return failed ? 1 : 0;
    }
    const char* gear = imshell::Icon("gearshape", fallback);
    const char* trash = imshell::Icon("trash", fallback);
    if (gear == fallback || trash == fallback || gear == trash || imshell::Icon("gearshape", fallback) != gear) {
        std::printf("FAIL: names should get stable, distinct glyphs\n");
        return 1;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(100, 100);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImFont* base = io.Fonts->AddFontDefault();
    imshell::MergeIcons(base);
    imshell::MergeIcons(base); // once
    int sources = 0;
    for (const ImFontConfig* s : base->Sources)
        sources += s->FontLoader == loader;
    if (sources != 1) {
        std::printf("FAIL: MergeIcons should merge once, not %d times\n", sources);
        ++failed;
    }
    float width[2] = {};
    int i = 0;
    for (const float size : {16.0f, 32.0f}) {
        ImGui::NewFrame();
        ImFontBaked* baked = base->GetFontBaked(size);
        unsigned cp = 0;
        ImTextCharFromUtf8(&cp, gear, nullptr);
        const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(cp));
        const ImFontGlyph* letter = baked->FindGlyph('A');
        if (!glyph || !glyph->Visible || glyph->Colored || glyph->AdvanceX <= 0) {
            std::printf("FAIL: the gear at %.0f px should be a visible, uncoloured glyph\n", size);
            ++failed;
        } else {
            const float w = glyph->X1 - glyph->X0, h = glyph->Y1 - glyph->Y0;
            width[i] = w;
            if (w < size * 0.6f || w > size * 1.5f || h < size * 0.6f || h > size * 1.5f) {
                std::printf("FAIL: the gear is %.1f x %.1f at %.0f px\n", w, h, size);
                ++failed;
            }
            // on the line: the middle of the glyph near the middle of the lower-case letters
            const float middle = (glyph->Y0 + glyph->Y1) * 0.5f, text = (letter->Y0 + letter->Y1) * 0.5f;
            if (std::abs(middle - text) > size * 0.2f) {
                std::printf("FAIL: the gear's middle %.1f is off the text's %.1f at %.0f px\n", middle, text, size);
                ++failed;
            }
        }
        ImGui::Render();
        ++i;
    }
    if (width[1] < width[0] * 1.6f) {
        std::printf("FAIL: the icon should scale with the font (%.1f then %.1f)\n", width[0], width[1]);
        ++failed;
    }
    ImGui::DestroyContext();
    std::printf(failed ? "icons: %d failed\n" : "icons: ok\n", failed);
    return failed ? 1 : 0;
}

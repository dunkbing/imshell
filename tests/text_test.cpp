// Ellipsize cuts on character boundaries and fits the width
#include "imshell/text.hpp"

#include "imgui.h"

#include <cstdio>
#include <string>

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400, 300);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    ImGui::NewFrame();
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++failed;
        }
    };
    auto width = [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; };

    const std::string text = "A fairly long label for a list row";
    const float full = width(text);
    check(imshell::Ellipsize(text, full + 1) == text, "text that fits is unchanged");
    check(imshell::Ellipsize("", 10).empty(), "empty stays empty");
    for (const float w : {full * 0.8f, full * 0.5f, 60.0f, 30.0f}) {
        const std::string cut = imshell::Ellipsize(text, w);
        check(width(cut) <= w + 0.5f, "a cut label fits its width");
        check(cut.size() < text.size() + 3 && cut != text, "a cut label is shorter");
    }
    // the longest prefix: one more character would not fit
    {
        const float w = full * 0.5f;
        const std::string cut = imshell::Ellipsize(text, w);
        const std::string dots = cut.substr(cut.size() - 3) == "..." ? "..." : "\xE2\x80\xA6";
        const std::string kept = cut.substr(0, cut.size() - dots.size());
        check(width(text.substr(0, kept.size() + 1) + dots) > w, "the longest prefix is kept");
    }
    // whole characters: a multi-byte sequence is never split
    {
        const std::string wide = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"; // six e-acutes
        const std::string cut = imshell::Ellipsize(wide, width(wide) * 0.5f);
        bool ok = true;
        for (size_t i = 0; i < cut.size();) {
            const unsigned char c = static_cast<unsigned char>(cut[i]);
            const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
            ok = ok && n && i + n <= cut.size();
            i += n ? n : 1;
        }
        check(ok, "a cut keeps whole characters");
    }
    check(!imshell::Ellipsize(text, 1).empty(), "a width too small still gives the mark");

    ImGui::EndFrame();
    ImGui::DestroyContext();
    std::printf(failed ? "text: %d failed\n" : "text: ok\n", failed);
    return failed ? 1 : 0;
}

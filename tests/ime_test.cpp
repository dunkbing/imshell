// the composing underline on a bare imgui context: a focused field gets a line that ends at its
// caret and is as wide as the composing text; no field, no line. built with -DMYDEAR_BUILD_TESTS=ON
#include "ime.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>

namespace {
    int failures = 0;
    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    }

    char text[32] = "hello";

    // a field (focused on the first frame), then the underline; returns the foreground list
    ImDrawList* frame(bool field, const char* composing) {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 200));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        if (field) {
            if (ImGui::GetFrameCount() == 1)
                ImGui::SetKeyboardFocusHere();
            ImGui::InputText("##f", text, sizeof text);
        }
        ImGui::End();
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        mydear::detail::drawComposingUnderline(composing);
        ImGui::Render();
        return fg;
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400, 200);
    io.Fonts->Build();
    for (int i = 0; i < 3; ++i)
        frame(true, nullptr); // focus lands, the caret settles at the end
    check(frame(true, "")->VtxBuffer.Size == 0, "nothing composing, no line");

    ImDrawList* fg = frame(true, "lo");
    check(fg->VtxBuffer.Size > 0, "a line under the composition");
    const ImGuiPlatformImeData& ime = GImGui->PlatformImeData;
    float minX = 1e9f, maxX = -1e9f;
    for (const ImDrawVert& v : fg->VtxBuffer) {
        minX = std::fmin(minX, v.pos.x);
        maxX = std::fmax(maxX, v.pos.x);
    }
    const float width = ImGui::GetFont()->CalcTextSizeA(ime.InputLineHeight, FLT_MAX, 0, "lo").x;
    check(std::fabs(maxX - (ime.InputPos.x + 1)) < 1.5f, "ends at the caret");
    check(std::fabs((maxX - minX) - width) < 1.5f, "as wide as the composing text");

    check(frame(false, "lo")->VtxBuffer.Size == 0, "no field, no line");
    ImGui::DestroyContext();
    if (failures)
        return 1;
    std::printf("ime: ok\n");
    return 0;
}

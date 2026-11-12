// the composing underline on a bare imgui context: a focused field gets a line that ends at its
// caret and is as wide as the composing text; no field, no line. built with -DIMSHELL_BUILD_TESTS=ON
#include "ime.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>
#include <string_view>

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
        imshell::detail::drawComposingUnderline(composing);
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

    // a composition typed into the field and edited in place: telex "vieet" + "j" -> "việt"
    namespace im = imshell::detail::ime;
    const auto settle = [] {
        for (int i = 0; i < 12; ++i) // queued backspaces trickle in one per frame
            frame(true, nullptr);
    };
    io.AddKeyEvent(ImGuiKey_End, true); // focus selected everything: type after it instead
    io.AddKeyEvent(ImGuiKey_End, false);
    settle();
    im::compose("vie");
    settle();
    check(std::string_view(text) == "hellovie", "the composition shows in the field");
    im::compose("viê");
    settle();
    check(std::string_view(text) == "helloviê", "an update edits only what changed");
    im::compose("việ");
    settle();
    im::commit("việt");
    settle();
    check(std::string_view(text) == "helloviệt", "the commit replaces the composition");
    check(!im::composing(), "nothing composing after the commit");
    im::compose("x");
    settle();
    im::compose("");
    settle();
    check(std::string_view(text) == "helloviệt", "an emptied composition removes itself");

    std::string_view field;
    int caret = 0, selStart = 0, selEnd = 0;
    check(im::field(field, caret, selStart, selEnd) && field == "helloviệt" && caret == (int)field.size(),
          "the ime reads the field and its caret");
    check(im::utf16Length("việt😀") == 6, "utf-16 length counts surrogate pairs");
    check(im::utf16ToByte("việt", 3) == 5, "utf-16 index to byte offset");

    check(frame(false, "lo")->VtxBuffer.Size == 0, "no field, no line");
    ImGui::DestroyContext();
    if (failures)
        return 1;
    std::printf("ime: ok\n");
    return 0;
}

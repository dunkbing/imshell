// selection handles on a bare imgui context: a selection gets a knob under each end, and a
// finger dragging a knob moves that end while the other stays. built with -DMYDEAR_BUILD_TESTS=ON
#include "touch.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>

namespace {
    int failures = 0;
    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    }

    char text[32] = "hello world";
    ImVec2 fieldMin;
    ImGuiID fieldId;

    void frame() {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 200));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::SetCursorPos(ImVec2(20, 20));
        if (ImGui::GetFrameCount() == 1)
            ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##f", text, sizeof text);
        fieldMin = ImGui::GetItemRectMin();
        fieldId = ImGui::GetItemID();
        ImGui::End();
        mydear::touch::drawHandles();
        ImGui::Render();
    }

    // where a user sees the knob under character i: from the field's rect, not the layout code
    ImVec2 knob(int i) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float size = ImGui::GetFontSize();
        const float x = fieldMin.x + style.FramePadding.x + ImGui::CalcTextSize(text, text + i).x;
        return ImVec2(x, fieldMin.y + style.FramePadding.y + size + std::max(size * 0.45f, 8.0f));
    }

    // a finger from one knob to under another character, a few moves on the way
    void drag(int from, int to) {
        double now = 1;
        mydear::touch::down(knob(from), now);
        frame();
        for (int step = 1; step <= 4; ++step) {
            const ImVec2 a = knob(from), b = knob(to);
            mydear::touch::move(ImVec2(a.x + (b.x - a.x) * step / 4, a.y), now += 0.05);
            frame();
        }
        mydear::touch::up(now, true);
        frame();
        frame();
    }

    ImGuiInputTextState* state() {
        return ImGui::GetInputTextState(fieldId);
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400, 200);
    io.Fonts->Build();
    for (int i = 0; i < 3; ++i)
        frame();
    check(state() != nullptr, "the field is active");
    if (!state())
        return 1;

    state()->SetSelection(2, 5); // "llo"
    frame();
    drag(5, 9); // the end handle to "hello wor|ld"
    check(state()->GetSelectionStart() == 2 && state()->GetSelectionEnd() == 9,
          "the end handle moves the end, the start stays");
    check(!io.KeyShift, "shift is let go");

    drag(2, 0); // the start handle to the front
    const int a = state()->GetSelectionStart(), b = state()->GetSelectionEnd();
    check(std::min(a, b) == 0 && std::max(a, b) == 9,
          "the start handle moves the start, the end stays");
    check(!io.KeyShift, "shift is let go again");

    state()->SetSelection(4, 4);
    frame();
    double now = 2;
    mydear::touch::down(knob(4), now); // no selection, no handle: an ordinary touch
    check(!mydear::touch::grabbing(), "no handles without a selection");
    mydear::touch::cancel();

    ImGui::DestroyContext();
    if (failures)
        return 1;
    std::printf("handles: ok\n");
    return 0;
}

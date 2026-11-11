// the touch engine against a bare imgui context: no window, no platform. built with
// -DIMSHELL_BUILD_TESTS=ON (ctest), or by hand:
//   c++ -std=c++20 -I<imgui> -Isrc tests/touch_test.cpp src/touch.cpp <imgui>/imgui*.cpp
#include "touch.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>

namespace touch = imshell::touch;

namespace {
    int failures = 0;
    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    }

    bool clicked = false;
    float wheel = 0;

    // a button at the top left, an input field below it, empty space under both
    void frame() {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        wheel += io.MouseWheel;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 400));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        clicked |= ImGui::Button("button", ImVec2(100, 40)); // 8..108, 8..48
        static char text[32] = "hello";
        ImGui::InputText("##field", text, sizeof(text)); // below the button, y ~52..72
        ImGui::End();
        ImGui::Render();
    }

    void frames(int n) {
        for (int i = 0; i < n; ++i)
            frame();
    }
} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400, 400);
    io.Fonts->Build();
    frames(2);

    // a tap on the button clicks it
    touch::down(ImVec2(50, 20), 0.0);
    frames(2);
    touch::up(0.1, true);
    frames(4);
    check(clicked, "a tap clicks");
    check(touch::takeLift() || !touch::animating(), "the lift happens once");

    // a drag over empty space scrolls, and a quick release coasts
    touch::down(ImVec2(200, 300), 1.0);
    frames(2); // the hover settles: the cursor is an arrow
    touch::move(ImVec2(200, 280), 1.01);
    check(touch::state() == touch::State::Scrolling, "a drag on empty space scrolls");
    touch::move(ImVec2(200, 240), 1.02);
    frames(2);
    check(wheel != 0, "scrolling sends wheel events");
    touch::up(1.03, true);
    check(touch::animating(), "a flick coasts");
    for (int i = 0; i < 600 && touch::animating(); ++i) {
        touch::coast(1.0f / 60.0f);
        frame();
        touch::takeLift();
    }
    check(!touch::animating(), "the coast stops");

    // a drag that starts on the text field presses (text selection), not scrolls
    touch::down(ImVec2(60, 62), 2.0);
    frames(2);
    check(ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput, "the field shows a text cursor");
    touch::move(ImVec2(90, 62), 2.05);
    frames(1);
    check(touch::state() == touch::State::Pressed, "a drag on text presses");
    check(ImGui::IsMouseDown(0), "the button is down while pressed");
    touch::up(2.1, true);
    frames(2);
    check(!ImGui::IsMouseDown(0), "the press lets go");

    // a finger resting past kLongPress belongs to the shell
    touch::down(ImVec2(200, 300), 3.0);
    check(!touch::longPressDue(3.2), "not a long press yet");
    check(touch::longPressDue(3.6), "a long press");
    check(touch::state() == touch::State::None, "a long press ends the touch");

    ImGui::DestroyContext();
    std::printf(failures ? "touch_test: %d failed\n" : "touch_test: ok\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

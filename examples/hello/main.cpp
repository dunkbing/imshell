// the smallest mydear app: a themed window with its own title bar and a button
#include "mydear/app.hpp"
#include "mydear/button.hpp"
#include "mydear/themes.hpp"
#include "mydear/title_bar.hpp"
#include "mydear/native_view.hpp"

#include "imgui_impl_glfw.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

void* makeTextField();
void* makeDatePicker();
void describeNativeView(void* view, char* out, int size);

struct Hello : mydear::Delegate {
    mydear::App* app = nullptr;
    int clicks = 0;
    // smoke tests: MYDEAR_HELLO_QUIT=<seconds> closes by itself, MYDEAR_HELLO_SHOT=<png>
    // saves a frame, MYDEAR_HELLO_PROBE=1 double-clicks the empty title bar (it should zoom)
    const char* quitAfter = std::getenv("MYDEAR_HELLO_QUIT");
    const char* shot = std::getenv("MYDEAR_HELLO_SHOT");
    const bool probe = std::getenv("MYDEAR_HELLO_PROBE");
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ImVec2 sizeBefore;
    ImVec2 barButton;
    int barClicks = 0;
    void* field = nullptr;
    bool wantPopup = false;

    void frame() override {
        const int f = ImGui::GetFrameCount();
        if (shot && f == 5)
            mydear::requestFrameCapture(shot, 2);
        if (probe)
            runProbe(f);

        const auto bar = mydear::BeginTitleBar();
        if (mydear::SmallButton("Bar button"))
            ++barClicks;
        barButton = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2,
                           (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2);
        const char* title = "Hello mydear";
        ImGui::SetCursorScreenPos(ImVec2(bar.pos.x + (bar.size.x - ImGui::CalcTextSize(title).x) / 2,
                                         bar.pos.y + (bar.size.y - ImGui::GetTextLineHeight()) / 2));
        ImGui::TextDisabled("%s", title);
        mydear::EndTitleBar();

        const float top = mydear::TitleBarHeight();
        ImGui::SetNextWindowPos(ImVec2(0, top));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y - top));
        ImGui::Begin("hello", nullptr, ImGuiWindowFlags_NoDecoration);
        if (mydear::Button("Click me", mydear::ButtonVariant::Primary))
            ++clicks;
        ImGui::Text("Clicked %d times", clicks);
        ImGui::Spacing();
        ImGui::TextUnformatted("Native views, placed by the imgui layout:");
        const ImVec2 fieldsAt = ImGui::GetCursorScreenPos();
        field = mydear::NativeView("##field", ImVec2(240, 22), makeTextField);
        mydear::NativeView("##date", ImVec2(160, 22), makeDatePicker);
        if (mydear::SmallButton("Open a popup over them") || std::exchange(wantPopup, false))
            ImGui::OpenPopup("##over");
        ImGui::SetNextWindowPos(ImVec2(fieldsAt.x + 40, fieldsAt.y + 4), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##over")) {
            ImGui::TextUnformatted("An imgui popup: the native views hide under it");
            ImGui::EndPopup();
        }
        ImGui::End();
    }

    void runProbe(int f) {
        auto& io = ImGui::GetIO();
        const ImVec2 empty(io.DisplaySize.x - 40, mydear::TitleBarHeight() / 2);
        if (f == 10)
            sizeBefore = io.DisplaySize;
        if (f >= 10 && f <= 15 || f >= 50 && f <= 53) {
            // the backend reads the real cursor while it thinks the pointer is outside
            ImGui_ImplGlfw_CursorEnterCallback(app->glfwWindow(), 1);
            io.AddMousePosEvent(f < 50 ? empty.x : barButton.x, f < 50 ? empty.y : barButton.y);
        }
        if (f == 51)
            io.AddMouseButtonEvent(0, true);
        if (f == 52)
            io.AddMouseButtonEvent(0, false);
        if (f == 60)
            std::printf("probe: bar button clicked %d time(s)\n", barClicks);
        // the native field: placed where the layout put it, hidden under a popup
        char where[64];
        if (f == 70 && field) {
            describeNativeView(field, where, sizeof where);
            std::printf("probe: native field %s\n", where);
            wantPopup = true;
        }
        if (f == 80 && field) {
            describeNativeView(field, where, sizeof where);
            std::printf("probe: under a popup %s\n", where);
        }
        if (f == 11 || f == 13)
            io.AddMouseButtonEvent(0, true);
        if (f == 12 || f == 14)
            io.AddMouseButtonEvent(0, false);
        if (f == 40)
            std::printf("probe: %.0fx%.0f -> %.0fx%.0f (%s)\n", sizeBefore.x, sizeBefore.y,
                        io.DisplaySize.x, io.DisplaySize.y,
                        io.DisplaySize.x != sizeBefore.x ? "zoomed" : "NOT zoomed");
    }

    bool shouldQuit() override {
        return quitAfter && std::chrono::steady_clock::now() - start >
                                std::chrono::seconds(std::atoi(quitAfter));
    }
};

int main() {
    mydear::Theme::Current() = &mydear::Theme::themeById("tokyo-night");
    mydear::Config config;
    config.title = "Hello mydear";
    config.width = 480;
    config.height = 300;
    config.titleBar = true;
    mydear::App app;
    Hello hello;
    hello.app = &app;
    if (!app.create(config, hello))
        return 1;
    app.run();
    app.destroy();
    return 0;
}

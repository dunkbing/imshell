// the smallest mydear app: a themed window with a button
#include "mydear/app.hpp"
#include "mydear/button.hpp"
#include "mydear/themes.hpp"

#include <chrono>
#include <cstdlib>

struct Hello : mydear::Delegate {
    int clicks = 0;
    // MYDEAR_HELLO_QUIT=<seconds>: close by itself (smoke tests)
    const char* quitAfter = std::getenv("MYDEAR_HELLO_QUIT");
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    void frame() override {
        // MYDEAR_HELLO_SHOT=<png>: save one frame (smoke tests)
        if (const char* shot = std::getenv("MYDEAR_HELLO_SHOT"); shot && ImGui::GetFrameCount() == 5)
            mydear::requestFrameCapture(shot, 2);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("hello", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::Text("Hello from mydear");
        if (mydear::Button("Click me", mydear::ButtonVariant::Primary))
            ++clicks;
        ImGui::Text("Clicked %d times", clicks);
        ImGui::End();
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
    config.height = 240;
    mydear::App app;
    Hello hello;
    if (!app.create(config, hello))
        return 1;
    app.run();
    app.destroy();
    return 0;
}

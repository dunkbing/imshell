# Getting started

mydear is a static library added with `add_subdirectory`. Your project compiles Dear ImGui's core once and tells mydear where it is; mydear compiles only the platform and renderer backends it needs. Two copies of ImGui's core would each have their own global context, which fails in confusing ways, so mydear never builds one.

```cmake
set(MYDEAR_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
add_subdirectory(external/mydear)
file(GLOB IMGUI_CORE ${MYDEAR_IMGUI_DIR}/imgui*.cpp)
add_executable(app main.cpp ${IMGUI_CORE})
target_link_libraries(app PRIVATE mydear::mydear)
```

The smallest app implements `mydear::Delegate` and hands it to `mydear::Run`:

```cpp
#include "mydear/alert.hpp"
#include "mydear/app.hpp"
#include "mydear/title_bar.hpp"

struct Hello : mydear::Delegate {
    bool dirty = true;

    void frame() override {
        mydear::BeginTitleBar(); // widgets here sit beside the os window buttons
        mydear::EndTitleBar();

        const mydear::Rect content = mydear::ContentRect(); // under the bar, inside the safe area
        ImGui::SetNextWindowPos(content.pos);
        ImGui::SetNextWindowSize(content.size);
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::TextUnformatted("Hello, mydear");
        ImGui::End();

        mydear::Alert::render(); // last: over everything
    }

    bool closeRequested() override { // the close button, cmd-q, alt-f4
        if (!dirty)
            return true;
        mydear::Alert::show("Quit without saving?", "",
                            {{"Quit", [this] { quit = true; }, mydear::AlertButton::Style::Destructive},
                             {"Cancel", nullptr, mydear::AlertButton::Style::Cancel}});
        return false;
    }
    bool quit = false;
    bool shouldQuit() override {
        return quit;
    }
};

int main() {
    mydear::Config config;
    config.title = "Hello";
    config.appName = "Hello"; // imgui.ini goes in mydear::DataDir("Hello")
    config.titleBar = true;
    static Hello hello;
    return mydear::Run(config, hello);
}
```

The same `main()` runs on macOS, Windows and Linux. On Android, `android_main` calls `mydear::SetAndroidApp(state)` and then the same `main()`; the delegate is `static` so it outlives an activity Android recreates (see [Platform shell](platform-shell.md#android)). On iOS, UIKit owns the run loop: `main()` calls `RunUIKit`, and the view controller calls `App::create` (see [Platform shell](platform-shell.md#ios)).

`examples/hello/main.cpp` is a fuller version with title-bar widgets and native views. Build it with `-DMYDEAR_BUILD_EXAMPLES=ON`.

## Dependencies

- macOS: GLFW (`find_package(glfw3)`), Native File Dialog Extended (`find_package(nfd)`), Metal, AppKit.
- iOS: UIKit, Metal, MetalKit.
- Windows: GLFW (`find_package(glfw3)`), Native File Dialog Extended (`find_package(nfd)`), Direct3D 11, DWM.
- Linux: GTK 4 and libepoxy (`pkg-config gtk4 epoxy`), Native File Dialog Extended (`find_package(nfd)`).
- With `-DMYDEAR_UPDATER=ON`: Sparkle (macOS), WinSparkle (Windows), or httplib, nlohmann_json and OpenSSL (Linux); see [Auto-update](auto-update.md).

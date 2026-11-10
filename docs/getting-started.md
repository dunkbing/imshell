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

## Building mydear on its own

mydear builds without a host, to work on it or to try the example. `vcpkg.json` lists what the desktops need (GLFW on macOS and Windows, Native File Dialog Extended), and Dear ImGui comes from a checkout of your choice:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DMYDEAR_IMGUI_DIR=/path/to/imgui \
  -DMYDEAR_BUILD_EXAMPLES=ON -DMYDEAR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

On Linux, GTK 4 and libepoxy come from the system (`pkg-config`). Instead of the vcpkg toolchain, `-DCMAKE_PREFIX_PATH` can point at packages installed some other way, such as a host's `vcpkg_installed/<triplet>`.

`ctest` runs two tests:
- `mydear_touch_test`: the touch engine on a bare ImGui context, no window.
- `mydear_hello_smoke`: the example in a real window, which needs a desktop session. With `MYDEAR_HELLO_PROBE=1` it drives itself: it double-clicks the title bar (the window should zoom), clicks a button in the bar, checks that a native view shows and hides under a popup, and closes the window the way the user would, expecting the first close to be refused (`closeRequested`) and the second to go through. It prints `probe: ok`, or `probe: FAILED:` and what failed.

## The web

The same app runs in a browser through [Emscripten](https://emscripten.org) (`brew install emscripten`, or emsdk). No vcpkg is needed: GLFW comes from Emscripten's `contrib.glfw3` port, fetched on the first build, and the graphics are WebGL 2.

```sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMYDEAR_IMGUI_DIR=/path/to/imgui -DMYDEAR_BUILD_EXAMPLES=ON
cmake --build build-web
```

That gives `mydear_hello.html` with its `.js` and `.wasm`; serve the folder with any static file server (a page opened from `file://` cannot load the wasm). A host's own target links `mydear` the same way and sets `SUFFIX .html`; `examples/web_shell.html` is a page to start from (`--shell-file`): a canvas filling the page and a loading line. See [Platforms](platforms.md) for what differs on the web.

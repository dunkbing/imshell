# Getting started

imshell is a static library added with `add_subdirectory`. Dear ImGui comes as a submodule (`external/imgui`, docking branch; clone with `--recursive` or run `git submodule update --init --recursive`), or from your own checkout in `IMSHELL_IMGUI_DIR`. Your project compiles its core once, from that folder; imshell compiles only the platform and renderer backends it needs. Two copies of ImGui's core would each have their own global context, which fails in confusing ways, so imshell never builds one.

```cmake
add_subdirectory(external/imshell) # sets IMSHELL_IMGUI_DIR to its submodule unless you did
file(GLOB IMGUI_CORE ${IMSHELL_IMGUI_DIR}/imgui*.cpp)
add_executable(app main.cpp ${IMGUI_CORE})
target_link_libraries(app PRIVATE imshell::imshell)
```

The smallest app implements `imshell::Delegate` and hands it to `imshell::Run`:

```cpp
#include "imshell/alert.hpp"
#include "imshell/app.hpp"
#include "imshell/title_bar.hpp"

struct Hello : imshell::Delegate {
    bool dirty = true;

    void frame() override {
        imshell::BeginTitleBar(); // widgets here sit beside the os window buttons
        imshell::EndTitleBar();

        const imshell::Rect content = imshell::ContentRect(); // under the bar, inside the safe area
        ImGui::SetNextWindowPos(content.pos);
        ImGui::SetNextWindowSize(content.size);
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::TextUnformatted("Hello, imshell");
        ImGui::End();

        imshell::Alert::render(); // last: over everything
    }

    bool closeRequested() override { // the close button, cmd-q, alt-f4
        if (!dirty)
            return true;
        imshell::Alert::show("Quit without saving?", "",
                            {{"Quit", [this] { quit = true; }, imshell::AlertButton::Style::Destructive},
                             {"Cancel", nullptr, imshell::AlertButton::Style::Cancel}});
        return false;
    }
    bool quit = false;
    bool shouldQuit() override {
        return quit;
    }
};

int main() {
    imshell::Config config;
    config.title = "Hello";
    config.appName = "Hello"; // imgui.ini goes in imshell::DataDir("Hello")
    config.titleBar = true;
    static Hello hello;
    return imshell::Run(config, hello);
}
```

The same `main()` runs on macOS, Windows and Linux. On Android, `android_main` calls `imshell::SetAndroidApp(state)` and then the same `main()`; the delegate is `static` so it outlives an activity Android recreates (see [Platform shell](platform-shell.md#android)). On iOS, UIKit owns the run loop: `main()` calls `RunUIKit`, and the view controller calls `App::create` (see [Platform shell](platform-shell.md#ios)).

`examples/hello/main.cpp` is a fuller version with title-bar widgets and native views. Build it with `-DIMSHELL_BUILD_EXAMPLES=ON`.

## Dependencies

- macOS: GLFW (`find_package(glfw3)`), Native File Dialog Extended (`find_package(nfd)`), Metal, AppKit.
- iOS: UIKit, Metal, MetalKit.
- Windows: GLFW (`find_package(glfw3)`), Native File Dialog Extended (`find_package(nfd)`), Direct3D 11, DWM.
- Linux: GTK 4 and libepoxy (`pkg-config gtk4 epoxy`), Native File Dialog Extended (`find_package(nfd)`).
- With `-DIMSHELL_UPDATER=ON`: Sparkle (macOS), WinSparkle (Windows), or httplib, nlohmann_json and OpenSSL (Linux); see [Auto-update](auto-update.md).

## Building imshell on its own

imshell builds without a host, to work on it or to try the example. `vcpkg.json` lists what the desktops need (GLFW on macOS and Windows, Native File Dialog Extended), and Dear ImGui, imgui_rich_md and imgui-node-editor come from the submodules:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DIMSHELL_BUILD_EXAMPLES=ON -DIMSHELL_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`imshell::markdown` and `imshell::node_editor` and their tests are built when their submodules are there ([Markdown](markdown.md), [Node editor](node-editor.md)).

On Linux, GTK 4 and libepoxy come from the system (`pkg-config`). Instead of the vcpkg toolchain, `-DCMAKE_PREFIX_PATH` can point at packages installed some other way, such as a host's `vcpkg_installed/<triplet>`.

`ctest` runs two tests:
- `imshell_touch_test`: the touch engine on a bare ImGui context, no window.
- `imshell_hello_smoke`: the example in a real window, which needs a desktop session. With `IMSHELL_HELLO_PROBE=1` it drives itself: it double-clicks the title bar (the window should zoom), clicks a button in the bar, checks that a native view shows and hides under a popup, and closes the window the way the user would, expecting the first close to be refused (`closeRequested`) and the second to go through. It prints `probe: ok`, or `probe: FAILED:` and what failed.

## The web

The same app runs in a browser through [Emscripten](https://emscripten.org) (`brew install emscripten`, or emsdk). No vcpkg is needed: GLFW comes from Emscripten's `contrib.glfw3` port, fetched on the first build, and the graphics are WebGL 2.

```sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DIMSHELL_BUILD_EXAMPLES=ON
cmake --build build-web
```

That gives `imshell_hello.html` with its `.js` and `.wasm`; serve the folder with any static file server (a page opened from `file://` cannot load the wasm). A host's own target links `imshell` the same way and sets `SUFFIX .html`; `examples/web_shell.html` is a page to start from (`--shell-file`): a canvas filling the page and a loading line. See [Platforms](platforms.md) for what differs on the web.

# Getting started

mydear is a static library added with `add_subdirectory`. Your project compiles Dear ImGui's core once and tells mydear where it is; mydear compiles only the platform and renderer backends it needs. Two copies of ImGui's core would each have their own global context, which fails in confusing ways, so mydear never builds one.

```cmake
set(MYDEAR_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
add_subdirectory(external/mydear)
file(GLOB IMGUI_CORE ${MYDEAR_IMGUI_DIR}/imgui*.cpp)
add_executable(app main.cpp ${IMGUI_CORE})
target_link_libraries(app PRIVATE mydear::mydear)
```

The smallest app implements `mydear::Delegate` and runs a `mydear::App`:

```cpp
#include "mydear/app.hpp"

struct Hello : mydear::Delegate {
    void frame() override {
        ImGui::Begin("Hello");
        ImGui::TextUnformatted("Hello, mydear");
        ImGui::End();
    }
};

int main() {
    Hello hello;
    mydear::App app;
    mydear::Config config;
    config.title = "Hello";
    if (!app.create(config, hello))
        return 1;
    app.run();
    app.destroy();
}
```

`examples/hello/main.cpp` is a fuller version with a title bar and native views. Build it with `-DMYDEAR_BUILD_EXAMPLES=ON`.

On iOS, UIKit owns the run loop, so `main()` looks different; see [Platform shell](platform-shell.md#ios).

## Dependencies

- macOS: GLFW (`find_package(glfw3)`), Metal, AppKit.
- iOS: UIKit, Metal, MetalKit.

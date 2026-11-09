# mydear – agent guide

A Dear ImGui toolkit for cross-platform apps, grown while building DearSQL (its first host, where it is the `external/mydear` submodule). The README says what it has and how a host uses it; this file is how to work on it.

- Everything is in namespace `mydear` (themes in `mydear::Theme`); no host names (no `dearsql`, no `DEARSQL_*` macros) inside the library. A host-specific need is a hook the host fills, not an include of the host.
- Never compile Dear ImGui here: the host passes `MYDEAR_IMGUI_DIR` and builds imgui (and its backends) once. Two copies split ImGui's global context silently.
- Widgets draw with `Theme::Current()`; the host sets it when its theme changes.
- Headers in `include/mydear/`, sources in `src/`, vendored single headers in `third_party/` (private include). Add a source to `CMakeLists.txt` when you add one.
- C++20. Comments short and lowercase. Keep it small: something moves in when a host needs it, not before.
- A change to what a host sees (a function, a header, a CMake function) updates `README.md` in the same commit.
- Build and test through a host (DearSQL: `./scripts/build`, then `./build/ui_tests -nopause`). `-DMYDEAR_BUILD_EXAMPLES=ON` adds `mydear_hello`; `MYDEAR_HELLO_QUIT=3 MYDEAR_HELLO_SHOT=out.png ./mydear_hello` runs it unattended and saves a frame.
- Changes land through a pull request, never a push to `main`. Commits: short one-line subject, no trailers or attribution lines.

## Platform shell

`mydear::App` (`include/mydear/app.hpp`) is the window, the graphics backend, the imgui platform and renderer backends, the frame loop and input; the host implements `mydear::Delegate`. One implementation per platform under `src/<platform>/`:

- macOS (`src/macos/`): glfw window without a gl context, metal through a `CAMetalLayer` (`metal_backend.mm`, also offscreen frame capture and ffmpeg recording), drag-and-drop and Finder's open-file event to `Delegate::openFile`. The loop idles after 2 s without input (waits grow to 0.2 s), never renders unfocused (`nextDrawable` blocks in the background), and sleeps out the frame with vsync off.
- iOS, Windows (glfw + dx11) and Linux (gtk4 + opengl): still in DearSQL, moving next.

Objective-C classes cannot live in a C++ namespace and their names are global: close `namespace mydear` around them and prefix them `Mydear`. `.mm` files compile as C++ (no `enable_language(OBJCXX)`: mixed with a host that does not enable it, generation fails).

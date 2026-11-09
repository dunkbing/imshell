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
- iOS (`src/ios/`): uikit runs the loop. `RunUIKit` starts a scene whose root is a `UINavigationController` around the host's controller class (`MydearViewController` or a subclass; the bar stays hidden unless the subclass shows it). The controller owns `MydearView` (an `MTKView` adopting `UITextInput` over a one-position document: insertText and deleteBackward become imgui chars and keys, the space-bar trackpad becomes arrows), the gestures (a finger touch is held back until it is a tap, a press-and-drag on a resize or text cursor, a pan that scrolls and coasts, or a long press that right-clicks or shows the edit menu over text) and the keyboard inset. `create()` must run inside the controller's `loadView`, after super: the view exists then. Not checkable from here: real touches; the simulator shows launch, layout, the keyboard and the bar.
- Windows (glfw + dx11) and Linux (gtk4 + opengl): still in DearSQL, moving next.

Title bar (`title_bar.hpp`): the bar is an ImGui window over the strip. A click reaches ImGui first; `EndTitleBar()` decides after the frame whether it hit a widget, and only a click on empty bar space starts the native drag (`performWindowDragWithEvent:` with the mouse-down a local event monitor kept, after `present()`, skipped when the button is already up) or the double-click action (`AppleActionOnDoubleClick`). This way nothing answers a hit test from last frame's rectangles. Frame captures leave the strip out (its OS buttons are native). `MYDEAR_HELLO_PROBE=1 ./mydear_hello` checks both paths with ImGui input: a double-click on empty bar space zooms, a click on the bar's button presses it.

Native views (`src/apple/native_view.mm`, both Apple shells, ARC): an entry per ImGui id holds a clip view and the host's view. `NativeView()` records the item rect and its part inside the window's clip rect; `detail::placeNativeViews()`, which each shell calls after rendering, sets the frames, hides entries not drawn this frame or covered by a root window displayed above the entry's dock tree (`FindWindowDisplayIndex`), and frees entries unused for 3 s. On macOS the views live in a flipped overlay above the Metal view that passes clicks through except over a view, and a mouse-down outside the overlay gives the keyboard back to GLFW's content view; on iOS a touch on `MydearView` makes it first responder again. `mydear_hello` shows a native text field and date picker; its probe checks the field is placed (16,127 240x22) and hidden under a popup.

Objective-C classes cannot live in a C++ namespace and their names are global: close `namespace mydear` around them and prefix them `Mydear`. `.mm` files compile as C++ (no `enable_language(OBJCXX)`: mixed with a host that does not enable it, generation fails).

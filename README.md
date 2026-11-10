# mydear

A small toolkit for building cross-platform applications with [Dear ImGui](https://github.com/ocornut/imgui), extracted from [DearSQL](https://dearsql.dev) and grown alongside it.

## What it has

- **Platform shell** (`mydear/app.hpp`): `mydear::App` opens the native window, sets up the graphics backend and the Dear ImGui platform and renderer backends, and runs a frame loop that idles when nothing happens; the app implements `mydear::Delegate` (`frame()`, plus `setup()`, `openFile()` for dropped or opened files, `wantsFrames()`, `shouldQuit()`). A test engine can drive frames itself with `pollEvents()` / `renderFrame()`. macOS (GLFW + Metal, with offscreen frame capture and video recording), Windows (GLFW + Direct3D 11) and Linux (GTK4 + OpenGL 3.3 in a `GtkGLArea`, GTK's frame clock) are done. On iOS UIKit runs the loop: `main()` calls `mydear::RunUIKit(argc, argv, rootClass)` (`mydear/ios.h`), the root controller (`MydearViewController` or a subclass that adds native chrome) owns the Metal view, and the app calls `create()` from its `loadView`; touch (tap, long press as right click or edit menu, drag to scroll with momentum, pointer hover), the hardware and soft keyboard through `UITextInput` (IME, space-bar cursor) and the clipboard come with it. On Android (`mydear/android.hpp`) the host's `android_main` calls `SetAndroidApp(state)` and runs as on the desktop: NativeActivity with EGL and OpenGL ES 3, the same touch model, the soft keyboard, the system text toolbar on a long press, the clipboard and the document picker (`PickFile`) through `io.mydear.MydearActivity`; `mydear_android_apk()` (`cmake/MydearAndroid.cmake`) packs a signed APK without Gradle.
- **Title bar** (`mydear/title_bar.hpp`, with `Config::titleBar`): the app draws its own bar with ImGui between `BeginTitleBar()` and `EndTitleBar()`, and lays the rest out below `TitleBarHeight()`. mydear keeps the OS buttons in place (`leftInset` / `rightInset` say where they are), makes empty bar space drag the window, and makes a double-click zoom or minimize it as the OS setting says. macOS (native traffic lights; `Config::titleBarHeight`, 28 pt by default, and a taller bar re-centres the traffic lights) and Windows (caption buttons drawn by mydear, snap layouts, the system move loop) and Linux (GTK client-side decorations, window buttons in GTK's order, the compositor's move).
- **Auto-update** (`mydear/updater.hpp`, `-DMYDEAR_UPDATER=ON`): `updater::Start(config)` checks in the background and `updater::Check()` is the user's "Check for Updates"; Sparkle on macOS, WinSparkle on Windows (one ed25519-signed appcast for both), and on Linux an AppImage that downloads its successor, checks its SHA-256 against a JSON manifest and renames it over itself. The host ships Sparkle or WinSparkle; see [docs/auto-update.md](docs/auto-update.md).
- **Native views** (`mydear/native_view.hpp`): `NativeView(id, size, create)` puts a real `NSView` (macOS), `UIView` (iOS), `HWND` (Windows) or `GtkWidget` (Linux), such as a text field, date picker, web view, map or video, into the ImGui layout. It takes its space like any item, mydear places and clips the view after each frame, and the view gets its own clicks and keys. It is always drawn above ImGui, so it hides while an ImGui window, popup or tooltip overlaps it, hides when not drawn in a frame, and is freed after 3 s unused. Frame captures don't include it. On macOS `NativeButton(id, symbol, tooltip, title)` is a ready-made one: an `NSButton` with an SF Symbol, tinted with the theme.
- **Themes** (`mydear/themes.hpp`): eighteen palettes (One Dark/Light, Ayu, Gruvbox, VS Code, Catppuccin, Tokyo Night, Nord, Dracula, Solarized, GitHub), `ApplyTheme(colors)` for the ImGui style, spacing and corner radius constants, and `Theme::Current()`, the theme the widgets below draw with.
- **Widgets**: `Button` / `SmallButton` / `IconButton` / `IconLabelButton` with variants (`button.hpp`), `Select` (`select.hpp`), `ToggleButton` (`toggle_button.hpp`), `Splitter` and `SegmentedTabs` (`splitter.hpp`), `Spinner` and `SpinnerOverlay` (`spinner.hpp`).
- **Menus and dialogs** (`menu.hpp`): `BeginMenuPopup`, `BeginContextMenu`, `BeginSubMenu`, `BeginSelect`/`EndSelect` with one padding for every menu; `FitScreen` / `CapToScreen` keep dialogs inside small screens.
- **Idle frames** (`frames.hpp`): `requestFrames()` asks the host loop to keep drawing for a moment (spinners call it); the loop checks `framesRequested()` before it sleeps.
- **Images** (`image.hpp`): `LoadTexture(bytes, size, create)` decodes a PNG and hands the pixels to the backend's texture upload.
- **Embedded files** (`cmake/MydearEmbed.cmake`, `embedded.hpp`): `mydear_embed_fonts(<dir>)` and `mydear_embed_images(<dir>)` bake every font or image in a folder into the binary; `findEmbeddedFont(name)` / `findEmbeddedImage(name)` find them by file name.

## Docs

The [`docs/`](docs/README.md) folder covers each part in depth: getting started, the platform shell, the title bar, native views, the UI toolkit, and what differs per platform.

## Use

The smallest app is `examples/hello/main.cpp` (configure with `-DMYDEAR_BUILD_EXAMPLES=ON`): a delegate with a `frame()` that draws a title bar and a window, `mydear::App::create`, `run`.

```cmake
set(MYDEAR_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
add_subdirectory(external/mydear)
mydear_embed_fonts(${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)   # sets EMBEDDED_FONTS_FILE
mydear_embed_images(${CMAKE_CURRENT_SOURCE_DIR}/assets/images) # sets EMBEDDED_IMAGES_FILE
add_executable(app main.cpp ${EMBEDDED_FONTS_FILE} ${EMBEDDED_IMAGES_FILE} ${IMGUI_SOURCES})
target_link_libraries(app PRIVATE mydear::mydear)
```

```cpp
#include "mydear/button.hpp"
#include "mydear/themes.hpp"

mydear::Theme::Current() = &mydear::Theme::themeById("tokyo-night");
mydear::Theme::ApplyTheme(mydear::Theme::Current()->colors);
if (mydear::Button("Save", mydear::ButtonVariant::Primary))
    save();
```

The host compiles the Dear ImGui core itself and passes the folder as `MYDEAR_IMGUI_DIR`; mydear compiles only the platform and renderer backends it uses, never a second copy of the core. If you embed anything, call both embed functions: the generated files define the lookups' data, and an empty folder still generates empty tables.

## Status

Extracted from DearSQL's app code. Done: the macOS, Windows and Linux shells and title bars, the iOS shell and touch layer, the Android shell, auto-update. Next: see the [roadmap](docs/roadmap.md).

## License

MIT, see `LICENSE`. `third_party/stb_image.h` is public domain / MIT (Sean Barrett).

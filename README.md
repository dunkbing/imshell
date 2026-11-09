# mydear

A small toolkit for building cross-platform applications with [Dear ImGui](https://github.com/ocornut/imgui), extracted from [DearSQL](https://dearsql.dev) and grown alongside it.

## What it has

- **Platform shell** (`mydear/app.hpp`): `mydear::App` opens the native window, sets up the graphics backend and the Dear ImGui platform and renderer backends, and runs a frame loop that idles when nothing happens; the app implements `mydear::Delegate` (`frame()`, plus `setup()`, `openFile()` for dropped or opened files, `wantsFrames()`, `shouldQuit()`). A test engine can drive frames itself with `pollEvents()` / `renderFrame()`. macOS (GLFW + Metal) is done, with offscreen frame capture and video recording. On iOS UIKit runs the loop: `main()` calls `mydear::RunUIKit(argc, argv, rootClass)` (`mydear/ios.h`), the root controller (`MydearViewController` or a subclass that adds native chrome) owns the Metal view, and the app calls `create()` from its `loadView`; touch (tap, long press as right click or edit menu, drag to scroll with momentum, pointer hover), the hardware and soft keyboard through `UITextInput` (IME, space-bar cursor) and the clipboard come with it. Windows and Linux are next.
- **Title bar** (`mydear/title_bar.hpp`, with `Config::titleBar`): the app draws its own bar with ImGui between `BeginTitleBar()` and `EndTitleBar()`, and lays the rest out below `TitleBarHeight()`. mydear keeps the OS buttons in place (`leftInset` / `rightInset` say where they are), makes empty bar space drag the window, and makes a double-click zoom or minimize it as the OS setting says. macOS so far (28 pt, native traffic lights); Windows (own caption buttons, snap layouts) and Linux come with their shells.
- **Themes** (`mydear/themes.hpp`): eighteen palettes (One Dark/Light, Ayu, Gruvbox, VS Code, Catppuccin, Tokyo Night, Nord, Dracula, Solarized, GitHub), `ApplyTheme(colors)` for the ImGui style, spacing and corner radius constants, and `Theme::Current()`, the theme the widgets below draw with.
- **Widgets**: `Button` / `SmallButton` / `IconButton` / `IconLabelButton` with variants (`button.hpp`), `Select` (`select.hpp`), `ToggleButton` (`toggle_button.hpp`), `Splitter` and `SegmentedTabs` (`splitter.hpp`), `Spinner` and `SpinnerOverlay` (`spinner.hpp`).
- **Menus and dialogs** (`menu.hpp`): `BeginMenuPopup`, `BeginContextMenu`, `BeginSubMenu`, `BeginSelect`/`EndSelect` with one padding for every menu; `FitScreen` / `CapToScreen` keep dialogs inside small screens.
- **Idle frames** (`frames.hpp`): `requestFrames()` asks the host loop to keep drawing for a moment (spinners call it); the loop checks `framesRequested()` before it sleeps.
- **Images** (`image.hpp`): `LoadTexture(bytes, size, create)` decodes a PNG and hands the pixels to the backend's texture upload.
- **Embedded files** (`cmake/MydearEmbed.cmake`, `embedded.hpp`): `mydear_embed_fonts(<dir>)` and `mydear_embed_images(<dir>)` bake every font or image in a folder into the binary; `findEmbeddedFont(name)` / `findEmbeddedImage(name)` find them by file name.

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

Extracted from DearSQL's app code. Done: the macOS shell and title bar, the iOS shell and touch layer. Next: Windows (GLFW + DX11) and Linux (GTK4 + OpenGL); native alerts, input and file dialogs; font loading with icon merging.

## License

MIT, see `LICENSE`. `third_party/stb_image.h` is public domain / MIT (Sean Barrett).

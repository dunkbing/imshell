# imshell

A small toolkit for building cross-platform applications with [Dear ImGui](https://github.com/ocornut/imgui), extracted from [DearSQL](https://dearsql.dev) and grown alongside it.

## What it has

- **[Platform shell](docs/platform-shell.md)** (`app.hpp`): the native window, graphics backend and frame loop on macOS (Metal), Windows (Direct3D 11), Linux (GTK4 + OpenGL), iOS (UIKit + Metal), Android (OpenGL ES) and the web (WebGL 2). The app implements `imshell::Delegate` and calls `imshell::Run(config, delegate)`.
- **[Title bar](docs/title-bar.md)** (`title_bar.hpp`): the app draws its own bar with ImGui; imshell keeps the OS window buttons, dragging and double-click behaviour.
- **[Auto-update](docs/auto-update.md)** (`updater.hpp`, `-DIMSHELL_UPDATER=ON`): Sparkle on macOS, WinSparkle on Windows, an AppImage swap on Linux; `imshell_app()` ships it and makes a signed, notarized macOS app.
- **[Notifications](docs/notifications.md)** (`notifications.hpp`): system notifications, their clicks and the app icon's badge on macOS, iOS, Android and Linux.
- **[Native views](docs/native-views.md)** (`native_view.hpp`): a real `NSView`, `UIView`, `HWND` or `GtkWidget` inside the ImGui layout.
- **[Themes, widgets, menus and dialogs](docs/ui.md)**: eighteen palettes, buttons, select, toggle, splitter, spinner, menus, alerts and prompts, fonts, file dialogs, clipboard, haptics, images and embedded files.
- **[Components](docs/components.md)** (`imshell::components`): text fields, cards, metrics, empty states.
- **[Editor](docs/editor.md)** (`imshell::editor`): a multi-line code editor with selection, undo and highlighting hooks.
- **[Markdown](docs/markdown.md)** (`imshell::markdown`): selectable rich text, tables, code blocks and Mermaid.
- **[Node editor](docs/node-editor.md)** (`imshell::node_editor`): imgui-node-editor, themed.

[`docs/`](docs/README.md) covers each part in depth, starting with [getting started](docs/getting-started.md); [`platforms.md`](docs/platforms.md) has what each platform supports.

## Use

The smallest app is `examples/hello/main.cpp` (configure with `-DIMSHELL_BUILD_EXAMPLES=ON`): a delegate with a `frame()` that draws a title bar and a window in `ContentRect()`, started with `imshell::Run`.

```cmake
add_subdirectory(external/imshell) # imgui from its submodule, or set IMSHELL_IMGUI_DIR first
file(GLOB IMGUI_CORE ${IMSHELL_IMGUI_DIR}/imgui*.cpp)
imshell_embed_fonts(${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)   # sets EMBEDDED_FONTS_FILE
imshell_embed_images(${CMAKE_CURRENT_SOURCE_DIR}/assets/images) # sets EMBEDDED_IMAGES_FILE
add_executable(app main.cpp ${EMBEDDED_FONTS_FILE} ${EMBEDDED_IMAGES_FILE} ${IMGUI_CORE})
target_link_libraries(app PRIVATE imshell::imshell)
```

```cpp
#include "imshell/button.hpp"
#include "imshell/themes.hpp"

imshell::Theme::Current() = &imshell::Theme::themeById("tokyo-night");
imshell::Theme::ApplyTheme(imshell::Theme::Current()->colors);
if (imshell::Button("Save", imshell::ButtonVariant::Primary))
    save();
```

Dear ImGui (docking), its test engine, imgui_rich_md and imgui-node-editor are submodules in `external/` (clone with `--recursive`). The host compiles the ImGui core once, from `IMSHELL_IMGUI_DIR`; see [getting started](docs/getting-started.md) for using your own copies.

## Status

Extracted from DearSQL's app code. Done: the macOS, Windows and Linux shells and title bars, the iOS shell and touch layer, the Android shell, auto-update. Next: see the [roadmap](docs/roadmap.md).

## License

MIT, see `LICENSE`. `third_party/stb_image.h` is public domain / MIT (Sean Barrett).

# mydear

A small toolkit for building cross-platform applications with [Dear ImGui](https://github.com/ocornut/imgui), extracted from [DearSQL](https://dearsql.dev) and grown alongside it.

## What it has

- **Themes** (`mydear/themes.hpp`): eighteen palettes (One Dark/Light, Ayu, Gruvbox, VS Code, Catppuccin, Tokyo Night, Nord, Dracula, Solarized, GitHub), `ApplyTheme(colors)` for the ImGui style, spacing and corner radius constants, and `Theme::Current()`, the theme the widgets below draw with.
- **Widgets**: `Button` / `SmallButton` / `IconButton` / `IconLabelButton` with variants (`button.hpp`), `Select` (`select.hpp`), `ToggleButton` (`toggle_button.hpp`), `Splitter` and `SegmentedTabs` (`splitter.hpp`), `Spinner` and `SpinnerOverlay` (`spinner.hpp`).
- **Menus and dialogs** (`menu.hpp`): `BeginMenuPopup`, `BeginContextMenu`, `BeginSubMenu`, `BeginSelect`/`EndSelect` with one padding for every menu; `FitScreen` / `CapToScreen` keep dialogs inside small screens.
- **Idle frames** (`frames.hpp`): `requestFrames()` asks the host loop to keep drawing for a moment (spinners call it); the loop checks `framesRequested()` before it sleeps.
- **Images** (`image.hpp`): `LoadTexture(bytes, size, create)` decodes a PNG and hands the pixels to the backend's texture upload.
- **Embedded files** (`cmake/MydearEmbed.cmake`, `embedded.hpp`): `mydear_embed_fonts(<dir>)` and `mydear_embed_images(<dir>)` bake every font or image in a folder into the binary; `findEmbeddedFont(name)` / `findEmbeddedImage(name)` find them by file name.

## Use

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

The host compiles Dear ImGui (and its platform backends) itself and passes the folder as `MYDEAR_IMGUI_DIR`; mydear never compiles a second copy. If you embed anything, call both embed functions: the generated files define the lookups' data, and an empty folder still generates empty tables.

## Status

Extracted from DearSQL's app code. Not yet here, planned: the per-platform shells (window, graphics backend for Metal/DX11/OpenGL, title bars), the iOS touch layer (tap and long press, pan to scroll with momentum, `UITextInput` keyboard with IME and the space-bar cursor, edit menu, document picker), native alerts and input dialogs, font loading with icon merging.

## License

Not chosen yet. `third_party/stb_image.h` is public domain / MIT (Sean Barrett).

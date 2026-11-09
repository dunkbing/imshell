# UI toolkit

## Themes (`mydear/themes.hpp`)

Eighteen palettes: One Dark and One Light, Ayu, Gruvbox, VS Code, Catppuccin, Tokyo Night, Nord, Dracula, Solarized and GitHub. Each is a `Theme::Colors` set of named colours (`text`, `subtext1`, `base`, `blue`, `red` and so on).

```cpp
mydear::Theme::Current() = &mydear::Theme::themeById("tokyo-night");
mydear::Theme::ApplyTheme(mydear::Theme::Current()->colors);
```

`ApplyTheme` sets the ImGui style. `Theme::Current()` is the theme mydear's widgets draw with; set it whenever the theme changes. Spacing constants: `Theme::Spacing::XS` (2), `S` (4), `M` (8), `L` (16), `XL` (20).

## Widgets

| Widget | Header | Notes |
|---|---|---|
| `Button`, `SmallButton` | `button.hpp` | variants: Primary, Secondary, Danger, Ghost and so on |
| `IconButton`, `IconLabelButton` | `button.hpp` | an icon font glyph, with or without a label |
| `SegmentedTabs` | `button.hpp` | a row of mutually exclusive buttons |
| `Select` | `select.hpp` | a dropdown over a string array |
| `ToggleButton` | `toggle_button.hpp` | a switch bound to a `bool` |
| `Splitter` | `splitter.hpp` | a draggable divider between two panes |
| `Spinner`, `SpinnerOverlay` | `spinner.hpp` | the overlay draws on a draw list without taking layout space |

## Menus and dialogs (`mydear/menu.hpp`)

Open menus through these so every menu gets the same padding (`kMenuPadding`):

| Instead of | Use |
|---|---|
| `ImGui::BeginPopup` | `mydear::BeginMenuPopup(id)` |
| `ImGui::BeginPopupContextItem` | `mydear::BeginContextMenu(id)` |
| `ImGui::BeginMenu` | `mydear::BeginSubMenu(label)` |
| `ImGui::BeginCombo` / `EndCombo` | `mydear::BeginSelect(id, preview)` / `EndSelect()` |

Close them with ImGui's usual `EndPopup()` / `EndMenu()`. `FitScreen(size)` and `CapToScreen()` keep dialogs inside small screens (iPhone, split view).

## Images (`mydear/image.hpp`)

`LoadTexture(bytes, size, create)` decodes a PNG with stb_image and hands the pixels to `create`, normally the app's `createTexture`.

## Embedded files (`cmake/MydearEmbed.cmake`, `mydear/embedded.hpp`)

```cmake
mydear_embed_fonts(${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)   # sets EMBEDDED_FONTS_FILE
mydear_embed_images(${CMAKE_CURRENT_SOURCE_DIR}/assets/images) # sets EMBEDDED_IMAGES_FILE
```

Every file in the folder is baked into the binary; `findEmbeddedFont(name)` and `findEmbeddedImage(name)` look one up by file name without the extension. If you use either, call both: the generated files define the lookup tables, and an empty folder still generates an empty table.

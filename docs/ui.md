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

## Alerts and prompts (`mydear/alert.hpp`)

`Alert::show(title, message, buttons)` queues a modal with a message and buttons (one OK when there are none); `InputDialog::show(title, label, initial, confirmText, onConfirm, onCancel, validator)` queues a one-line text prompt, kept open while `validator` or `onConfirm` returns an error message. Both can be called from any thread and wake the loop. Call `Alert::render()` once a frame after the rest of the UI: it draws the front entry over every popup open that frame, so an alert raised from inside a dialog stacks on top of it instead of replacing it. Buttons sit cancel first, then destructive, then default; escape answers the cancel button (or the only one), return the default one, never a destructive one. The popup's ID is `<title>###mydear_alert`, so a test engine reaches a button as `//<title>###mydear_alert/<button>`.

## Fonts (`mydear/fonts.hpp`)

Call `AddFont` from `Delegate::setup()`. The first file is the base font and the rest merge into it: icon fonts (`icons = true`: no oversampling, snapped to pixels, with the icon font's glyph `ranges`) and other scripts. With `systemCjk` (the default) the system's Chinese, Japanese and Korean font merges in last, so names and text in those scripts render; `SystemCjkFontPath()` says which one, or nothing when none is installed. That file is mapped rather than read (they are 20 MB and more), and the OS loads only the glyphs drawn. Font data is not copied, so files from `mydear_embed_fonts` work as they are:

```cpp
const EmbeddedFont* text = findEmbeddedFont("Inter-Regular");
const EmbeddedFont* icons = findEmbeddedFont("fa-solid-900");
static const ImWchar kIcons[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
mydear::AddFont({{text->data, text->size}, {icons->data, icons->size, kIcons, true}});
```

## File dialogs (`mydear/file_dialog.hpp`)

`OpenFileDialog(filters, done)`, `SaveFileDialog(defaultName, filters, done)` and `PickFolderDialog(done)` call `done` with the chosen path, and not at all when the user cancels. Filters are a name and extensions without dots (`{"CSV files", "csv,tsv"}`); `"*"` allows any file.

| | Desktops | iOS and Android |
|---|---|---|
| Open | Native File Dialog, modal: `done` runs before the call returns | the system document picker; `done` runs a moment later. Another app's file is copied into `copyInto` (`DocumentsDir()` when empty), because it can't be opened by path |
| Save | the system save dialog | a free name in `DocumentsDir()` (`name 2.ext` when taken), at once |
| Folder | the system folder picker | `DocumentsDir()`, at once |

On iOS the documents folder shows in the Files app under the app's name. Filters become content types (iOS) or MIME types (Android, from a short table; anything else allows any file). On the desktops, link Native File Dialog Extended (vcpkg `nativefiledialog-extended`); mydear finds it with `find_package(nfd)`.

## Images (`mydear/image.hpp`)

`LoadTexture(bytes, size, create)` decodes a PNG with stb_image and hands the pixels to `create`, normally the app's `createTexture`.

## Embedded files (`cmake/MydearEmbed.cmake`, `mydear/embedded.hpp`)

```cmake
mydear_embed_fonts(${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)   # sets EMBEDDED_FONTS_FILE
mydear_embed_images(${CMAKE_CURRENT_SOURCE_DIR}/assets/images) # sets EMBEDDED_IMAGES_FILE
```

Every file in the folder is baked into the binary; `findEmbeddedFont(name)` and `findEmbeddedImage(name)` look one up by file name without the extension. If you use either, call both: the generated files define the lookup tables, and an empty folder still generates an empty table.

# UI toolkit

## Themes (`imshell/themes.hpp`)

Eighteen palettes: One Dark and One Light, Ayu, Gruvbox, VS Code, Catppuccin, Tokyo Night, Nord, Dracula, Solarized and GitHub. Each is a `Theme::Colors` set of named colours (`text`, `subtext1`, `base`, `blue`, `red` and so on).

```cpp
imshell::Theme::Current() = &imshell::Theme::themeById("tokyo-night");
imshell::Theme::ApplyTheme(imshell::Theme::Current()->colors);
```

`ApplyTheme` sets the ImGui style. `Theme::Current()` is the theme imshell's widgets draw with; set it whenever the theme changes. Spacing constants: `Theme::Spacing::XS` (2), `S` (4), `M` (8), `L` (16), `XL` (20).

## Widgets

| Widget | Header | Notes |
|---|---|---|
| `Button`, `SmallButton` | `button.hpp` | variants: Primary, Secondary, Danger, Ghost and so on |
| `IconButton`, `IconLabelButton` | `button.hpp` | an icon font glyph, with or without a label |
| `SegmentedTabs` | `button.hpp` | a row of mutually exclusive buttons |
| `Select` | `select.hpp` | a dropdown over a string array |
| `ToggleButton` | `toggle_button.hpp` | a switch bound to a `bool` |
| `Splitter` | `splitter.hpp` | a draggable divider between two panes |
| `BeginResizable`, `ResizablePanel`, `EndResizable` | `resizable.hpp` | panels in a row or column with a handle between each two; see below |
| `Spinner`, `SpinnerOverlay` | `spinner.hpp` | the overlay draws on a draw list without taking layout space |

## Resizable panels (`imshell/resizable.hpp`)

A group of panels side by side (or stacked) with a handle on each boundary. Dragging a handle resizes the two panels next to it and nothing else.

```cpp
static float sidebar = 260, files = 0; // pixels; 0: not sized yet
if (imshell::BeginResizable("main")) {
    if (imshell::ResizablePanel({.size = &sidebar, .min = 200, .fixed = true})) Sidebar();
    if (imshell::ResizablePanel({.min = 420})) Chat();
    if (imshell::ResizablePanel({.size = &files, .min = 300, .visible = filesOpen})) Files();
    if (imshell::EndResizable()) Save(sidebar, files); // true once, when a drag ends
}
```

- **Sizes are pixels.** `size` points at the app's own value: it is read when the group first sees the panel and whenever the app changes it, and written when a drag resizes the panel, so saving it is enough to restore a layout. A panel without a size shares what the sized ones leave.
- **When the group changes size**, `fixed` panels keep theirs and the others take the change in proportion to their sizes.
- **Limits**: `min` and `max` (0: none) hold while dragging and while the group grows or shrinks. When the minimums do not fit, fixed panels give way to their minimum first, then the others shrink below theirs, so no panel ends past the edge. The sizes asked for are kept and come back when there is room.
- **Hidden** (`visible = false`): the panel takes no room and has no handle; shown again, it has its old size. `ResizablePanel` returns false for it, so draw nothing.
- **Options**: `axis` (`Horizontal`, `Vertical`), `size` of the group (as `BeginChild`), `disabled`, `hitSize` (the band around a handle that takes the mouse, 8 by default), and the handle's colours `line` and `lit` (`ImGuiCol_Separator` and `ImGuiCol_SeparatorActive` by default). The handle is one screen pixel at rest and two while hovered or dragged.
- Each panel is a child window: `childFlags` and `windowFlags` go to it. Groups nest: begin one inside a panel.

## Menus and dialogs (`imshell/menu.hpp`)

Open menus through these so every menu gets the same padding (`kMenuPadding`):

| Instead of | Use |
|---|---|
| `ImGui::BeginPopup` | `imshell::BeginMenuPopup(id)` |
| `ImGui::BeginPopupContextItem` | `imshell::BeginContextMenu(id)` |
| `ImGui::BeginMenu` | `imshell::BeginSubMenu(label)` |
| `ImGui::BeginCombo` / `EndCombo` | `imshell::BeginSelect(id, preview)` / `EndSelect()` |

Close them with ImGui's usual `EndPopup()` / `EndMenu()`. `FitScreen(size)` and `CapToScreen()` keep dialogs inside small screens (iPhone, split view).

## Alerts and prompts (`imshell/alert.hpp`)

`Alert::show(title, message, buttons)` queues a modal with a message and buttons (one OK when there are none); `InputDialog::show(title, label, initial, confirmText, onConfirm, onCancel, validator)` queues a one-line text prompt, kept open while `validator` or `onConfirm` returns an error message. Both can be called from any thread and wake the loop. Call `Alert::render()` once a frame after the rest of the UI: it draws the front entry over every popup open that frame, so an alert raised from inside a dialog stacks on top of it instead of replacing it. Buttons sit cancel first, then destructive, then default; escape answers the cancel button (or the only one), return the default one, never a destructive one. The popup's ID is `<title>###imshell_alert`, so a test engine reaches a button as `//<title>###imshell_alert/<button>`.

## Fonts (`imshell/fonts.hpp`)

Call `AddFont` from `Delegate::setup()`. The first file is the base font and the rest merge into it: icon fonts (`icons = true`: no oversampling, snapped to pixels, with the icon font's glyph `ranges`) and other scripts. With `systemCjk` (the default) the system's Chinese, Japanese and Korean font merges in last, so names and text in those scripts render; `SystemCjkFontPath()` says which one, or nothing when none is installed. Without FreeType it skips variable fonts (CFF2 outlines, which stb_truetype cannot read): Android 13 and later ship Noto Sans CJK as one, so Android gets Noto Serif CJK. That file is mapped rather than read (they are 20 MB and more), and the OS loads only the glyphs drawn. Font data is not copied, so files from `imshell_embed_fonts` work as they are:

```cpp
const EmbeddedFont* text = findEmbeddedFont("Inter-Regular");
const EmbeddedFont* icons = findEmbeddedFont("fa-solid-900");
static const ImWchar kIcons[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
imshell::AddFont({{text->data, text->size}, {icons->data, icons->size, kIcons, true}});
```

## File dialogs (`imshell/file_dialog.hpp`)

`OpenFileDialog(filters, done)`, `SaveFileDialog(defaultName, filters, done)` and `PickFolderDialog(done)` call `done` with the chosen path, and not at all when the user cancels. Filters are a name and extensions without dots (`{"CSV files", "csv,tsv"}`); `"*"` allows any file.

| | Desktops | iOS and Android |
|---|---|---|
| Open | Native File Dialog, modal: `done` runs before the call returns | the system document picker; `done` runs a moment later. Another app's file is copied into `copyInto` (`DocumentsDir()` when empty), because it can't be opened by path |
| Save | the system save dialog | a free name in `DocumentsDir()` (`name 2.ext` when taken), at once |
| Folder | the system folder picker | `DocumentsDir()`, at once |

On iOS the documents folder shows in the Files app under the app's name. Filters become content types (iOS) or MIME types (Android, from a short table; anything else allows any file). On the desktops, link Native File Dialog Extended (vcpkg `nativefiledialog-extended`); imshell finds it with `find_package(nfd)`.

## Emoji

`AddFont` merges the system's colour emoji into the font (`emoji = true`, the default), and `MergeEmoji(font)` does it for a font made elsewhere (the markdown renderer calls it for the faces it loads, never for the app's own fonts). The emoji font's glyphs are pictures at fixed sizes, which ImGui's rasterizers cannot draw, so a small font loader draws each one at the size asked for: CoreText on macOS and iOS, FreeType with Noto Color Emoji on Linux and Android (`IMSHELL_EMOJI_FONT` names another file). FreeType is optional: CMake looks for it, and a build without it has no emoji. Linux distributions ship it (Ubuntu: `libfreetype-dev`); the Android NDK does not, so an Android host adds it itself (vcpkg `freetype`) or goes without. Windows and the web have none yet. Text, icons and CJK keep their own rasterizer.

Most emoji are past U+FFFF, which ImGui's default 16-bit `ImWchar` cannot hold: configure with `-DIMSHELL_WCHAR32=ON` to build ImGui with `IMGUI_USE_WCHAR32`. It changes ImGui's structs, so it reaches every target that includes `imgui.h`, the host's own ImGui sources included (through linking imshell). Without it only the emoji below U+FFFF (✅ ❤ ✨) show.

ImGui does no text shaping: skin tones are drawn with no width (the emoji keeps its default colour), flags show as two letters and joined sequences (👨‍👩‍👧) as their parts.

## Haptics (`imshell/haptics.hpp`)

`PlayHaptic(kind)` gives a short vibration, from the UI thread. Kinds: `Tap` (a button or toggle), `Selection` (a value ticks over), `LongPress`, `Success`, `Warning`, `Error`. iOS uses UIKit's feedback generators, Android `performHapticFeedback` (success and error are their own patterns from Android 11), and macOS the trackpad's feedback performer, felt only on a Force Touch trackpad while the finger is down. Windows and Linux do nothing. The system setting wins: with haptics turned off, nothing plays. The shells already play `LongPress` when a long press takes hold.

## Images (`imshell/image.hpp`)

`LoadTexture(bytes, size, create)` decodes a PNG with stb_image and hands the pixels to `create`, normally the app's `createTexture`. An app started with `Run()` has no `App` to ask: `LoadTexture(bytes, size)` uploads through the running one (not on iOS).

## Embedded files (`cmake/ImShellEmbed.cmake`, `imshell/embedded.hpp`)

```cmake
imshell_embed_fonts(${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)   # sets EMBEDDED_FONTS_FILE
imshell_embed_images(${CMAKE_CURRENT_SOURCE_DIR}/assets/images) # sets EMBEDDED_IMAGES_FILE
```

Every file in the folder is baked into the binary; `findEmbeddedFont(name)` and `findEmbeddedImage(name)` look one up by file name without the extension. If you use either, call both: the generated files define the lookup tables, and an empty folder still generates an empty table.

# UI toolkit

## Themes (`imshell/themes.hpp`)

Eighteen themes: One Dark and One Light, Ayu (dark, mirage, light), Gruvbox, VS Code, Catppuccin, Tokyo Night, Nord, Dracula, Solarized and GitHub. A theme is a set of colours named by what they are for, so widgets and apps draw with a role and every theme reads right:

```cpp
imshell::SetTheme("tokyo-night"); // by settings id; before Run or at any time after
const imshell::Theme& t = imshell::theme();
ImGui::TextColored(t.mutedForeground, "Last saved 2 min ago");
if (imshell::Button("Delete", imshell::ButtonVariant::Destructive)) remove();
```

| Role | For |
|---|---|
| `background` / `foreground` | the window and its text |
| `card`, `popover`, `sidebar` (each with a `…Foreground`) | a panel set apart, menus and popups and tooltips, side panels and the title bar |
| `muted` / `mutedForeground` | quiet fills; secondary text, hints, placeholders |
| `primary` / `primaryForeground` | the main action, selection, links |
| `secondary` / `secondaryForeground` | ordinary buttons |
| `accent` / `accentForeground` | a hovered or focused row |
| `destructive` / `destructiveForeground` | deleting and errors, nothing else |
| `success`, `warning` | status, only for its meaning |
| `border`, `input`, `ring` | dividers and outlines, a text field's fill, the keyboard focus outline |
| `chart[0..4]` | data series, in order |
| `syntax.keyword`, `.string`, `.number`, `.comment`, `.function`, `.type`, `.operator_` | code in editors and markdown |

States come from the role: `Hovered(c)` and `Pressed(c)` step a fill toward the foreground (10% and 20%), `WithAlpha(c, a)` tints (selection is `primary` at 20%, pressed rows 40%), `Mix(a, b, t)` blends. Every theme meets WCAG AA on its pairs (body text 4.5, quiet text, labels on fills and links 3); `tests/theme_test.cpp` checks them.

`ApplyTheme()` styles ImGui from the current theme; the shells call it, `SetTheme` too once a context exists. `Themes()` lists them for a picker, `ThemeById(id)` finds one (the old ids `light` and `dark` are One Light and One Dark). `TagColor(key, out)` resolves a colour the user picked and saved by key (`red`, `peach`, `yellow`, `green`, `blue`, `purple`) in the current theme.

Sizes: `Radius::Sm` (3, tags and small controls), `Md` (6, controls and popups), `Lg` (8, cards and containers), `Full` (pills). `Spacing::XS` (2), `S` (4), `M` (8), `L` (16), `XL` (20); `Theme::Spacing` and `Theme::Radius` are the same.

Buttons (`button.hpp`): `Primary` for the one main action, `Secondary` (the default) for the rest, `Destructive`, and the quieter `Ghost` (transparent until hovered) and `Outline` (a border); `Custom` keeps whatever colours the caller pushed.

### From the palette API

| Before | Now |
|---|---|
| `Theme::Current() = &Theme::themeById(id); Theme::ApplyTheme(...)` | `SetTheme(id)` |
| `Theme::Current()->colors.x`, `Theme::Colors` | `theme().role`, `Theme` |
| `Theme::THEMES`, `Theme::ThemeDef` | `Themes()`, `Theme` |
| `text`, `subtext0` / `subtext1`, `overlay1` | `foreground`, `mutedForeground` |
| `base`, `mantle`, `crust` | `background`, `sidebar` |
| `surface0`, `surface1`, `surface2` | `muted` or `input`, `accent`, `Pressed(accent)` |
| `overlay0` | `border` |
| `blue`, `red`, `green`, `peach` (as warning) | `primary`, `destructive`, `success`, `warning` |
| other accents | `chart[n]`, `syntax.*`, `TagColor` |
| `ConnectionPalette` | `TagColor(key, out)` |
| `CornerRadius::SMALL` / `MEDIUM`, `LARGE` | `Radius::Md`, `Radius::Lg` |
| `ButtonVariant::Danger` | `ButtonVariant::Destructive` |

## Widgets

| Widget | Header | Notes |
|---|---|---|
| `Button`, `SmallButton` | `button.hpp` | variants: Primary, Secondary, Destructive, Ghost, Outline |
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

`OpenDialog(options, done)` opens files or a folder and calls `done` with every chosen path, and not at all when the user cancels:

```cpp
imshell::OpenDialog({.filters = {{"CSV files", "csv,tsv"}, {"All files", "*"}}, .multiple = true},
                    [](const std::vector<std::string>& paths) { for (auto& p : paths) open(p); });
```

| Option | Meaning |
|---|---|
| `filters` | a name and extensions without dots (`{"CSV files", "csv,tsv"}`); none, or any `"*"`, allows every file. Windows and Linux list "All files" themselves; macOS has no filter list, so a `"*"` there turns the filter off |
| `multiple` | several files (or folders) at once |
| `directory` | a folder instead of files |
| `defaultPath` | the folder the desktop dialog opens in; empty lets the system choose |
| `copyInto` | iOS and Android: where picked files are copied |

`OpenFileDialog(filters, done)` and `PickFolderDialog(done)` are the one-path forms of the same call. `SaveFileDialog(defaultName, filters, done)` calls `done` with the path to write.

| | Desktops | iOS and Android |
|---|---|---|
| Open | Native File Dialog, modal: `done` runs before the call returns | the system document picker (several files with `multiple`); `done` runs a moment later. Another app's file is copied into `copyInto` (`DocumentsDir()` when empty), because it can't be opened by path |
| Save | the system save dialog | a free name in `DocumentsDir()` (`name 2.ext` when taken), at once |
| Folder (`directory`) | the system folder picker | `DocumentsDir()`, at once |

On iOS the documents folder shows in the Files app under the app's name. Filters become content types (iOS) or MIME types (Android, from a short table; anything else allows any file). On the desktops, link Native File Dialog Extended (vcpkg `nativefiledialog-extended`); imshell finds it with `find_package(nfd)`.

## Emoji

`AddFont` merges the system's colour emoji into the font (`emoji = true`, the default), and `MergeEmoji(font)` does it for a font made elsewhere (the markdown renderer calls it for the faces it loads, never for the app's own fonts). The emoji font's glyphs are pictures at fixed sizes, which ImGui's rasterizers cannot draw, so a small font loader draws each one at the size asked for: CoreText on macOS and iOS, DirectWrite and Direct2D with Segoe UI Emoji on Windows, FreeType with Noto Color Emoji on Linux and Android (`IMSHELL_EMOJI_FONT` names another file). FreeType is optional: CMake looks for it, and a build without it has no emoji. Linux distributions ship it (Ubuntu: `libfreetype-dev`); the Android NDK does not, so an Android host adds it itself (vcpkg `freetype`) or goes without. The web has none yet. Text, icons and CJK keep their own rasterizer.

Most emoji are past U+FFFF, which ImGui's default 16-bit `ImWchar` cannot hold: configure with `-DIMSHELL_WCHAR32=ON` to build ImGui with `IMGUI_USE_WCHAR32`. It changes ImGui's structs, so it reaches every target that includes `imgui.h`, the host's own ImGui sources included (through linking imshell). Without it only the emoji below U+FFFF (✅ ❤ ✨) show.

ImGui does no text shaping: skin tones are drawn with no width (the emoji keeps its default colour), flags show as two letters and joined sequences (👨‍👩‍👧) as their parts.

## Clipboard (`imshell/clipboard.hpp`)

Shaped like the web's Clipboard API, in `imshell::Clipboard`, from the UI thread:

```cpp
std::vector<Clipboard::Item> items = Clipboard::Read();
std::string text = Clipboard::ReadText();
Clipboard::Write({{{{"text/plain", "hi"}, {"text/html", "<b>hi</b>"}}}});
Clipboard::WriteText("hi");
```

An `Item` is one thing on the clipboard in each format it is offered in: `item.data` maps a mime type to its bytes, `item.has(type)` and `item.get(type)` look one up (empty when absent), and `item.file()` is the path when the item is a file copied in the file manager (from its `text/uri-list`). There is usually one item; copied files give one each. A paste handler:

```cpp
for (const auto& item : Clipboard::Read()) {
    if (!item.file().empty())        attach(item.file());
    else if (item.has("image/png"))  attachPng(item.get("image/png"));
}
```

Per platform:

- **macOS and iOS**: every pasteboard format that has a mime type is read and written (`text/plain`, `text/html`, `text/rtf`, `image/png`, `image/tiff`, `application/pdf`, `text/uri-list`, ...), and a picture is always offered as `image/png` (a macOS screenshot arrives as TIFF, an iOS photo as JPEG or HEIC).
- **Linux**: every format GTK offers with a mime type (`text/plain;charset=utf-8` reads as `text/plain`). The clipboard holds one set of formats, so `Write` keeps the first item, and files from all items as one list (also offered as `x-special/gnome-copied-files`, which file managers paste). A read waits for the owner up to a second per format.
- **Windows**: `text/plain`, `text/html` (the CF_HTML header and wrapper added on write; a read gives the copied fragment, as other platforms do), `text/rtf`, `image/png` (also written as a bitmap for apps that paste only those, and a copied bitmap is read as PNG), copied files (CF_HDROP, one item each) and any format registered under a mime type name. One set of formats, as on Linux.
- **Android**: text, HTML and URLs both ways; a copied picture (a `content://` URL) is read with its bytes and as PNG, but pictures and files are not written, since another app could only read them through a content provider of the app's own.
- **The web**: `text/plain` only, through ImGui.

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

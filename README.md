# imshell

A small toolkit for building cross-platform applications with [Dear ImGui](https://github.com/ocornut/imgui), extracted from [DearSQL](https://dearsql.dev) and grown alongside it.

## What it has

- **Platform shell** (`imshell/app.hpp`): `imshell::App` opens the native window, sets up the graphics backend and the Dear ImGui platform and renderer backends, and runs a frame loop that idles when nothing happens; the app implements `imshell::Delegate` (`frame()`, plus `setup()`, `tick()` for work that goes on while the window is in the background, `openFile()` for dropped or opened files, `wantsFrames()`, `shouldQuit()`, and `closeRequested()` to keep the window open over unsaved work) and starts it with `imshell::Run(config, delegate)`, the same call on the desktops and in Android's `android_main`. `ContentRect()` is where the UI goes (inside the safe area, under the title bar), and `DataDir(appName)` the app's own settings folder, where `imgui.ini` is kept when `Config::appName` is set. A test engine can drive frames itself with `pollEvents()` / `renderFrame()`. macOS (GLFW + Metal), Windows (GLFW + Direct3D 11) and Linux (GTK4 + OpenGL 3.3 in a `GtkGLArea`, GTK's frame clock) are done. On iOS UIKit runs the loop: `main()` calls `imshell::RunUIKit(argc, argv, rootClass)` (`imshell/ios.h`), the root controller (`ImShellViewController` or a subclass that adds native chrome) owns the Metal view, and the app calls `create()` from its `loadView`; touch (tap, long press as right click or edit menu, drag to scroll with momentum, pointer hover), the hardware and soft keyboard through `UITextInput` (IME, space-bar cursor) and the clipboard come with it. On Android (`imshell/android.hpp`) the host's `android_main` calls `SetAndroidApp(state)` and runs as on the desktop, and returns without tearing down when `AndroidActivityRecreating()` (the process and the ImGui state outlive a recreated activity): NativeActivity with EGL and OpenGL ES 3, the same touch model, the soft keyboard, the system text toolbar on a long press, the clipboard and the document picker (`PickFile`) through `io.imshell.ImShellActivity`; `imshell_android_apk()` (`cmake/ImShellAndroid.cmake`) packs a signed APK without Gradle. In a browser (Emscripten) it is GLFW on a full-page canvas with WebGL 2, the same `Run` call; see [docs/getting-started.md](docs/getting-started.md#the-web).
- **Title bar** (`imshell/title_bar.hpp`, with `Config::titleBar`): the app draws its own bar with ImGui between `BeginTitleBar()` and `EndTitleBar()`, and lays the rest out in `ContentRect()` (below `TitleBarHeight()` and inside `SafeArea()`, the edges system bars, notches and cutouts cover). On iOS the calls exist and draw nothing, so the same code builds everywhere. imshell keeps the OS buttons in place (`leftInset` / `rightInset` say where they are), makes empty bar space drag the window, and makes a double-click zoom or minimize it as the OS setting says. `Config::titleBarHeight` 0 is the platform's own height. macOS (native traffic lights; 28 pt by default, and a taller bar re-centres the traffic lights) and Windows (caption buttons drawn by imshell, snap layouts, the system move loop) and Linux (GTK client-side decorations, window buttons in GTK's order, the compositor's move).
- **Auto-update** (`imshell/updater.hpp`, `-DIMSHELL_UPDATER=ON`): `updater::Start(config)` checks in the background and `updater::Check()` is the user's "Check for Updates"; Sparkle on macOS, WinSparkle on Windows (one ed25519-signed appcast for both), and on Linux an AppImage that downloads its successor, checks its SHA-256 against a JSON manifest and renames it over itself. The host ships Sparkle or WinSparkle; see [docs/auto-update.md](docs/auto-update.md).
- **Native views** (`imshell/native_view.hpp`): `NativeView(id, size, create)` puts a real `NSView` (macOS), `UIView` (iOS), `HWND` (Windows) or `GtkWidget` (Linux), such as a text field, date picker, web view, map or video, into the ImGui layout. It takes its space like any item, imshell places and clips the view after each frame, and the view gets its own clicks and keys. It is always drawn above ImGui, so it hides while an ImGui window, popup or tooltip overlaps it, hides when not drawn in a frame, and is freed after 3 s unused. On macOS `NativeButton(id, symbol, tooltip, title)` is a ready-made one: an `NSButton` with an SF Symbol, tinted with the theme.
- **Themes** (`imshell/themes.hpp`): eighteen palettes (One Dark/Light, Ayu, Gruvbox, VS Code, Catppuccin, Tokyo Night, Nord, Dracula, Solarized, GitHub), `ApplyTheme(colors)` for the ImGui style, spacing and corner radius constants, and `Theme::Current()`, the theme the widgets below draw with.
- **Widgets**: `Button` / `SmallButton` / `IconButton` / `IconLabelButton` with variants (`button.hpp`), `Select` (`select.hpp`), `ToggleButton` (`toggle_button.hpp`), `Splitter` and `SegmentedTabs` (`splitter.hpp`), `Spinner` and `SpinnerOverlay` (`spinner.hpp`).
- **Components** (`components.hpp`, optional target `imshell::components`): text field with label, hint and error; card; badge; callout; progress and skeleton; avatar; metric; empty state; chips; toasts. See [docs/components.md](docs/components.md) and `examples/gallery`.
- **Editor** (`editor.hpp`, optional target `imshell::editor`): `TextEditor`, a multi-line code editor with selection, undo, line numbers, find and replace, a read-only mode and a completion popup; highlighting (`SetHighlighter`) and completion (`SetCompletionProvider`) are hooks the app fills, so it knows no language. See [docs/editor.md](docs/editor.md).
- **Markdown** (`markdown.hpp`, optional target `imshell::markdown`): `Markdown::Render(text, idPrefix)` draws headings, lists, links, tables and selectable text through [imgui_rich_md](https://github.com/pthom/imgui_rich_md) (the `external/imgui_rich_md` submodule, or the host's own); code blocks are read-only `TextEditor`s with a copy button, and the fonts, the highlighter per fence language and extra code-block buttons are options the app fills. See [docs/markdown.md](docs/markdown.md).
- **Node editor** (`node_editor.hpp`, optional target `imshell::node_editor`): [imgui-node-editor](https://github.com/thedmd/imgui-node-editor) (`ax::NodeEditor`) from the `external/imgui-node-editor` submodule, with `NodeEditor::ApplyStyle()` and `NodeEditor::ApplyColors()` to theme it from `Theme::Current()`. See [docs/node-editor.md](docs/node-editor.md).
- **Menus and dialogs** (`menu.hpp`): `BeginMenuPopup`, `BeginContextMenu`, `BeginSubMenu`, `BeginSelect`/`EndSelect` with one padding for every menu; `FitScreen` / `CapToScreen` keep dialogs inside small screens.
- **Alerts and prompts** (`alert.hpp`): `Alert::show(title, message, buttons)` and `InputDialog::show(...)` queue an ImGui modal from any thread; `Alert::render()`, once a frame after the rest of the UI, draws it over every open popup. Escape answers the cancel button and return the default one.
- **Idle frames** (`frames.hpp`): `requestFrames()` asks the host loop to keep drawing for a moment (spinners call it); the loop checks `framesRequested()` before it sleeps, and a request from another thread wakes an idle loop at once.
- **Fonts** (`fonts.hpp`): `AddFont(files)` makes one UI font from a text font with icon fonts and other scripts merged in, then the system's Chinese, Japanese and Korean font (memory-mapped, not read in); `SystemCjkFontPath()` names it.
- **File dialogs** (`file_dialog.hpp`): `OpenFileDialog(filters, done)`, `SaveFileDialog`, `PickFolderDialog`, with filters as extensions. Native File Dialog on the desktops; the document picker on iOS and Android, where a picked file is copied in and saves go to `DocumentsDir()`.
- **Clipboard** (`clipboard.hpp`): `Clipboard::ReadText`/`WriteText`, `ReadImage`/`WriteImage` (PNG bytes) and `ReadFiles` (the files copied in the file manager), for a paste that is more than text. Pictures and files on macOS so far.
- **Haptics** (`haptics.hpp`): `PlayHaptic(Haptic::Tap)` (also `Selection`, `LongPress`, `Success`, `Warning`, `Error`): the taptic engine on iOS, the vibrator on Android, a Force Touch trackpad on macOS; nothing on Windows and Linux.
- **Images** (`image.hpp`): `LoadTexture(bytes, size, create)` decodes a PNG and hands the pixels to the backend's texture upload; `LoadTexture(bytes, size)` uploads through the app `Run()` started.
- **Embedded files** (`cmake/ImShellEmbed.cmake`, `embedded.hpp`): `imshell_embed_fonts(<dir>)` and `imshell_embed_images(<dir>)` bake every font or image in a folder into the binary; `findEmbeddedFont(name)` / `findEmbeddedImage(name)` find them by file name.

## Docs

The [`docs/`](docs/README.md) folder covers each part in depth: getting started, the platform shell, the title bar, native views, the UI toolkit, the components, the editor, markdown, the node editor, and what differs per platform.

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

Dear ImGui and the libraries built on it come as submodules in `external/` (clone with `--recursive`): imgui (docking branch), imgui_test_engine, imgui_rich_md and imgui-node-editor, at the versions imshell is tested with. A host that keeps its own copies sets `IMSHELL_IMGUI_DIR`, `IMSHELL_TEST_ENGINE_DIR`, `IMSHELL_RICH_MD_DIR` or `IMSHELL_NODE_EDITOR_DIR` before adding imshell, and reads them back after (they are cache variables). The host compiles the Dear ImGui core itself, from `IMSHELL_IMGUI_DIR`, and the test engine with it when it tests; imshell compiles only the platform and renderer backends it uses, never a second copy of the core. If you embed anything, call both embed functions: the generated files define the lookups' data, and an empty folder still generates empty tables.

## Status

Extracted from DearSQL's app code. Done: the macOS, Windows and Linux shells and title bars, the iOS shell and touch layer, the Android shell, auto-update. Next: see the [roadmap](docs/roadmap.md).

## License

MIT, see `LICENSE`. `third_party/stb_image.h` is public domain / MIT (Sean Barrett).

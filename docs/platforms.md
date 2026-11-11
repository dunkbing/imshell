# Platforms

| | macOS | iOS / iPadOS | Windows | Linux | Android | Web |
|---|---|---|---|---|---|---|
| Window and loop | GLFW | UIKit (`RunUIKit`) | GLFW | GTK4, its frame clock | NativeActivity, `android_main` | Emscripten's GLFW port on a full-page canvas, the browser's animation frames |
| Graphics | Metal | Metal | Direct3D 11 | OpenGL 3.3 (`GtkGLArea`) | OpenGL ES 3 (EGL) | WebGL 2 |
| Title bar | ImGui bar, native traffic lights | none (use the navigation bar) | ImGui bar and caption buttons, snap layouts | ImGui bar, window buttons drawn by imshell in GTK's layout | ImGui strip at the top, no OS buttons | ImGui strip at the top, no OS buttons |
| Safe area (`SafeArea()`, `WorkPos`/`WorkSize`) | zero | top (status and navigation bar); the display stops above the home indicator and keyboard | zero | zero | top (status bar), edge to edge; the display stops at the navigation bar, side cutouts and keyboard | zero |
| Native views | `NSView` | `UIView` | child `HWND` | `GtkWidget` (overlay) | — | — |
| Text input | GLFW keys and chars | `UITextInput` (IME, soft keyboard) | GLFW keys and chars | GTK keys and chars (no IME yet) | an `InputConnection` through the activity (suggestions, composing, swipe typing), hardware keys | browser keys and chars (no IME, no soft keyboard yet) |
| Opened and dropped files | `openFile` | — | `openFile` (dropped) | `openFile` (dropped) | — (`PickFile` copies one in) | — |
| File dialogs | NFD | document picker | NFD | NFD | document picker | — (the callbacks never run yet) |
| Skips identical frames | yes | yes | no | no | no | yes |
| Clipboard pictures and files (`Clipboard::ReadImage`, `WriteImage`, `ReadFiles`) | yes | — | — | — | — | — |
| Haptics (`PlayHaptic`) | Force Touch trackpad | taptic engine | — | — | vibrator | — |
| Auto-update (`IMSHELL_UPDATER`) | Sparkle | — (App Store) | WinSparkle | AppImage swap, SHA-256 checked | — (the store) | — (reload the page) |

## Keyboard shortcuts

On macOS, the key equivalents of GLFW's default menu bar (⌘Q quit, ⌘H hide, ⌘M minimize) are handled by AppKit before a key reaches ImGui, so they work in every imshell app and an app can't take them over. Every other key goes to ImGui.

On Windows, Alt+F4 closes the window and Alt+Space opens the window menu (both handled by Windows itself); Windows+arrow snaps the window. Every other key goes to ImGui.

On Linux, the window manager keeps its own shortcuts (Super, Alt+F4, Alt+drag and so on); Tab goes to ImGui, not GTK's focus chain. Every other key goes to ImGui.

## Differences worth knowing

- **Points, not pixels:** on macOS and iOS (and dp on Android), sizes in `Config` and ImGui coordinates are in points and imshell sets `DisplayFramebufferScale` for the display. On Windows they are pixels, as GLFW reports them there, so `titleBarHeight` is in pixels too. On Linux they are GTK's logical pixels, scaled like points.
- **Title bar on iOS:** there is no window to drag; `TitleBarHeight()` is 0 and `BeginTitleBar()` draws nothing, so shared code needs no `#if`. Use the `UINavigationController` bar of `ImShellViewController` for native chrome instead.
- **Title bar on Android:** `BeginTitleBar` gives a plain ImGui strip `titleBarHeight` dp tall at the top (with `Config::titleBar`); nothing drags it.
- **Fullscreen on macOS:** the system title bar slides down over yours; see [Title bar](title-bar.md#fullscreen).
- **Web:** single threaded (no `std::thread`), so it runs on any static host; threads would need cross-origin isolation headers. `DataDir` is Emscripten's in-memory file system, so `imgui.ini` and anything else written there is gone on reload. There are no native views or file dialogs yet, and `Run` never returns: the page is the app.

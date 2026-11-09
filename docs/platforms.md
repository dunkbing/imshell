# Platforms

| | macOS | iOS / iPadOS | Windows | Linux |
|---|---|---|---|---|
| Window and loop | GLFW | UIKit (`RunUIKit`) | GLFW | GTK4, its frame clock |
| Graphics | Metal | Metal | Direct3D 11 | OpenGL 3.3 (`GtkGLArea`) |
| Title bar | ImGui bar, native traffic lights | none (use the navigation bar) | ImGui bar and caption buttons, snap layouts | ImGui bar, window buttons drawn by mydear in GTK's layout |
| Native views | `NSView` | `UIView` | child `HWND` | `GtkWidget` (overlay) |
| Frame capture, recording | yes | no | no | no |
| Text input | GLFW keys and chars | `UITextInput` (IME, soft keyboard) | GLFW keys and chars | GTK keys and chars (no IME yet) |
| Opened and dropped files | `openFile` | — | `openFile` (dropped) | `openFile` (dropped) |
| Auto-update (`MYDEAR_UPDATER`) | Sparkle | — (App Store) | WinSparkle | AppImage swap, SHA-256 checked |

## Keyboard shortcuts

On macOS, the key equivalents of GLFW's default menu bar (⌘Q quit, ⌘H hide, ⌘M minimize) are handled by AppKit before a key reaches ImGui, so they work in every mydear app and an app can't take them over. Every other key goes to ImGui.

On Windows, Alt+F4 closes the window and Alt+Space opens the window menu (both handled by Windows itself); Windows+arrow snaps the window. Every other key goes to ImGui.

On Linux, the window manager keeps its own shortcuts (Super, Alt+F4, Alt+drag and so on); Tab goes to ImGui, not GTK's focus chain. Every other key goes to ImGui.

## Differences worth knowing

- **Points, not pixels:** on macOS and iOS, sizes in `Config` and ImGui coordinates are in points and mydear sets `DisplayFramebufferScale` for the display. On Windows they are pixels, as GLFW reports them there, so `titleBarHeight` is in pixels too. On Linux they are GTK's logical pixels, scaled like points.
- **Title bar on iOS:** there is no window to drag. Use the `UINavigationController` bar of `MydearViewController` for native chrome instead.
- **Fullscreen on macOS:** the system title bar slides down over yours; see [Title bar](title-bar.md#fullscreen).

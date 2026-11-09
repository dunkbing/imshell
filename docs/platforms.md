# Platforms

| | macOS | iOS / iPadOS | Windows | Linux |
|---|---|---|---|---|
| Window and loop | GLFW | UIKit (`RunUIKit`) | GLFW | planned (GTK4) |
| Graphics | Metal | Metal | Direct3D 11 | planned (OpenGL) |
| Title bar | ImGui bar, native traffic lights | none (use the navigation bar) | ImGui bar and caption buttons, snap layouts | planned |
| Native views | `NSView` | `UIView` | child `HWND` | planned (GTK widget) |
| Frame capture, recording | yes | no | no | no |
| Text input | GLFW keys and chars | `UITextInput` (IME, soft keyboard) | GLFW keys and chars | planned |
| Opened and dropped files | `openFile` | — | `openFile` (dropped) | planned |

## Keyboard shortcuts

On macOS, the key equivalents of GLFW's default menu bar (⌘Q quit, ⌘H hide, ⌘M minimize) are handled by AppKit before a key reaches ImGui, so they work in every mydear app and an app can't take them over. Every other key goes to ImGui.

On Windows, Alt+F4 closes the window and Alt+Space opens the window menu (both handled by Windows itself); Windows+arrow snaps the window. Every other key goes to ImGui.

## Differences worth knowing

- **Points, not pixels:** on macOS and iOS, sizes in `Config` and ImGui coordinates are in points and mydear sets `DisplayFramebufferScale` for the display. On Windows they are pixels, as GLFW reports them there, so `titleBarHeight` is in pixels too.
- **Title bar on iOS:** there is no window to drag. Use the `UINavigationController` bar of `MydearViewController` for native chrome instead.
- **Fullscreen on macOS:** the system title bar slides down over yours; see [Title bar](title-bar.md#fullscreen).

# Platforms

| | macOS | iOS / iPadOS | Windows | Linux |
|---|---|---|---|---|
| Window and loop | GLFW | UIKit (`RunUIKit`) | planned (GLFW) | planned (GTK4) |
| Graphics | Metal | Metal | planned (DirectX 11) | planned (OpenGL) |
| Title bar | ImGui bar, native traffic lights | none (use the navigation bar) | planned | planned |
| Native views | `NSView` | `UIView` | planned (child `HWND`) | planned (GTK widget) |
| Frame capture, recording | yes | no | no | no |
| Text input | GLFW keys and chars | `UITextInput` (IME, soft keyboard) | planned | planned |
| Opened and dropped files | `openFile` | — | planned | planned |

## Keyboard shortcuts

On macOS, the key equivalents of GLFW's default menu bar (⌘Q quit, ⌘H hide, ⌘M minimize) are handled by AppKit before a key reaches ImGui, so they work in every mydear app and an app can't take them over. Every other key goes to ImGui. Windows and Linux will document theirs with their shells.

## Differences worth knowing

- **Points, not pixels:** sizes in `Config` and ImGui coordinates are in points; mydear sets `DisplayFramebufferScale` for the display.
- **Title bar on iOS:** there is no window to drag. Use the `UINavigationController` bar of `MydearViewController` for native chrome instead.
- **Fullscreen on macOS:** the system title bar slides down over yours; see [Title bar](title-bar.md#fullscreen).

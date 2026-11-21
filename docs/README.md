# imshell docs

- [Getting started](getting-started.md): add imshell to a CMake project and open a window.
- [Platform shell](platform-shell.md): `imshell::App`, the delegate, the frame loop, idle frames.
- [Title bar](title-bar.md): an ImGui title bar with the OS buttons, dragging and double-click.
- [Native views](native-views.md): put a real `NSView`, `UIView`, `HWND` or `GtkWidget` into the ImGui layout.
- [Auto-update](auto-update.md): Sparkle, WinSparkle or an AppImage swap, behind one call.
- [Notifications](notifications.md): system notifications, their clicks and the badge on the app's icon.
- [Tray icon](tray.md): a tray or menu bar icon with a menu, hiding and showing the window, and quitting (`imshell::tray`, `HideWindow`, `Quit`).
- [UI toolkit](ui.md): themes, widgets, menus, images and embedded files.
- [Components](components.md): optional ready-made pieces (`imshell::components`): text fields, cards, badges, callouts, progress, avatars, metrics, chips, toasts.
- [Editor](editor.md): an optional code editor (`imshell::editor`) with hooks for highlighting and completion.
- [Markdown](markdown.md): optional markdown (`imshell::markdown`) through imgui_rich_md, with code blocks in the editor.
- [Node editor](node-editor.md): optional node graphs (`imshell::node_editor`) through imgui-node-editor, themed.
- [Video](video.md): design for rendering ImGui frames to a video file, offscreen and deterministic (`imshell::video`).
- [Video demos](video-demos.md): videos made with `imshell-video`, each with its script.
- [Accessibility](accessibility.md): design for screen reader support (VoiceOver, UI Automation, AT-SPI, TalkBack); not built yet.
- [Platforms](platforms.md): what each platform supports and how they differ.
- [Roadmap](roadmap.md): what comes next, with the issues.

The repository's `README.md` is the short version; these pages go into how each part behaves.

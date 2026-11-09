# Title bar

With `Config::titleBar = true`, your content runs to the top edge of the window and you draw the title bar yourself with ImGui, like any other UI. mydear does the platform part:

- the OS's own window buttons stay where users expect them (on macOS, the traffic lights);
- empty bar space drags the window;
- a double-click on empty bar space does what the OS setting says (zoom, minimize, or nothing).

```cpp
auto bar = mydear::BeginTitleBar();
if (mydear::IconButton(ICON_SIDEBAR))
    toggleSidebar();
ImGui::SetCursorScreenPos({bar.pos.x + bar.size.x - bar.rightInset - 80, ImGui::GetCursorScreenPos().y});
if (mydear::IconButton(ICON_GEAR))
    openSettings();
mydear::EndTitleBar();
```

Lay the rest of the UI out below `mydear::TitleBarHeight()`.

## What `BeginTitleBar` returns

| Field | Meaning |
|---|---|
| `pos`, `size` | the strip, in screen coordinates |
| `leftInset` | room the OS buttons take at the start; start your widgets after it |
| `rightInset` | the same at the end |
| `fullscreen` | the window is fullscreen (the insets are 0 then) |

The cursor starts at `leftInset`, vertically centred for a row of frame-height widgets.

## Height

`Config::titleBarHeight` sets the bar's height in points. On macOS the default, 28, matches the system's own title bar. A taller bar moves the traffic lights down so they stay centred. AppKit puts them back on every resize and when leaving fullscreen, so mydear re-checks each frame and only moves them when something changed.

## How clicks are decided

Whether a click hits a widget is only known after ImGui has processed it, so mydear decides at the end of the frame, in `EndTitleBar`:

- if no ImGui item took the click and the mouse is still down, the window drag starts after the frame is presented;
- a double-click runs the OS setting's action instead;
- a click on the bar hands keyboard focus back to the window that had it, so typing continues where it was.

Widgets in the bar, ImGui items or [native views](native-views.md), never start a drag.

## Fullscreen

In fullscreen the OS shows its own title bar when the mouse reaches the top edge, over yours. The insets are 0 there, since the traffic lights are not in the window.

## Windows

There is no system frame: mydear removes it (`WM_NCCALCSIZE`), keeps the window shadow with `DwmExtendFrameIntoClientArea`, and draws the minimize, maximize and close buttons itself at the right end of the bar (`rightInset` is their width, 138 pixels). They follow the theme, and close turns red on hover, like Windows' own.

- **Snap layouts:** the maximize button answers `HTMAXBUTTON` to Windows' hit test, so hovering it on Windows 11 opens the snap layout flyout, and Windows itself handles the click.
- **Dragging:** empty bar space starts the system move loop, so Aero Snap (drag to an edge), shake and moving between screens work.
- **Double-click** toggles maximize; **right-click** opens the window menu (Restore, Move, Size, Minimize, Maximize, Close).
- **Resize:** the window edges still resize, by the system frame width.
- **Dark mode:** mydear sets `DWMWA_USE_IMMERSIVE_DARK_MODE` from `Theme::Current()->dark` every time the theme changes, so borders and snap previews match.
- **Maximized**, the window stays inside the work area instead of covering the taskbar.

`Config::titleBarHeight` is in pixels on Windows; 32 matches the system caption.

## Linux

The window gets an empty client-side titlebar, so GTK still draws the window shadow and resize edges, and the bar is yours. mydear draws the window buttons in the order `gtk-decoration-layout` gives (GNOME: close on the right; elementary and others: on the left), so `leftInset` and `rightInset` follow it.

- **Dragging:** empty bar space calls `gdk_toplevel_begin_move`, so the compositor moves the window (Wayland and X11).
- **Double-click** toggles maximize; **right-click** opens the window manager's menu (`gdk_toplevel_show_window_menu`).
- **Dark mode:** mydear sets `gtk-application-prefer-dark-theme` from `Theme::Current()->dark`, so GTK dialogs and native views match.

`Config::titleBarHeight` is in logical pixels on Linux.

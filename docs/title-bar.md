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

# Native views

`imshell::NativeView` (`imshell/native_view.hpp`) puts a real platform view, an `NSView` on macOS, a `UIView` on iOS an `HWND` on Windows or a `GtkWidget` on Linux, into the ImGui layout. Use it for what ImGui can't do well: web views, maps, video, PDF previews, native text fields (full IME and spell check), and system controls such as date and colour pickers.

```objc
auto* field = (__bridge NSTextField*)imshell::NativeView("##name", ImVec2(240, 22), [] {
    return (__bridge_retained void*)[NSTextField textFieldWithString:@""];
});
```

- `id`: an ImGui id, unique in the current window's id stack.
- `size`: like `ImGui::Button`; 0 or a negative value fills the remaining width or height.
- `create`: runs once per id and returns the view with a +1 reference: `(__bridge_retained void*)view`.
- Returns the view, or nullptr when `create` returned nothing.

Call it from Objective-C++ (`.mm`), since you create Cocoa objects.

## Behaviour

- **Layout:** the view takes its space like any ImGui item. After each frame imshell moves it there and clips it to its ImGui window, so scrolling, docking and resizing all follow.
- **Input:** the view gets its own clicks and keys from the OS. A click on ImGui gives the keyboard back to ImGui (on iOS, a touch on the ImGui view).
- **Always on top:** native views sit above everything ImGui draws, because ImGui draws the whole UI into one surface. While an ImGui window, popup or tooltip overlaps the view, imshell hides the view, so menus never end up behind it.
- **Lifetime:** a view not drawn in a frame (its tab closed, scrolled away, collapsed) is hidden. After 3 seconds unused it is removed and released.

## Native buttons

`imshell::NativeButton(id, symbol, tooltip, title)` is a ready-made native view: an `NSButton` with an SF Symbol, in the accessory-bar style that shows its border only on hover, the way toolbar buttons look. It is tinted with ImGui's text colour, so it follows the theme, and with a `title` the text comes before the symbol. It reserves the width the button wanted last frame, so a changing title resizes it a frame later.

```cpp
if (imshell::NativeButton("###sidebar", "sidebar.left", "Toggle Sidebar"))
    toggleSidebar();
```

It returns true on a real click (the `NSButton`'s action, which also wakes an idle loop) and on a click on the ImGui item underneath, which is what Dear ImGui's test engine produces. Elsewhere (Windows, Linux, iOS) it is a ghost ImGui button with the platform's icon of that name (`imshell/icons.hpp`), or the title, and the tooltip, so a caller needs no `#ifdef`.

## Native controls (`imshell/native_controls.hpp`)

`NativeSwitch(id, &on)`, `NativeSegmented(id, {"List", "Grid"}, &current)` and `NativePopUp(id, {"Small", "Large"}, &current)` are the platform's own controls in the layout: an `NSSwitch`, an `NSSegmentedControl` and an `NSPopUpButton` on macOS, a `UISwitch`, a `UISegmentedControl` and a button that opens a `UIMenu` on iOS.
Each returns true in the frame its value changed, and writes the new value, so an app uses them like imshell's `ToggleButton` and `SegmentedTabs`.
Where a platform has no backend yet (Windows, Linux, Android) they are imshell's `ToggleButton`, `SegmentedTabs` and `Select`, so a caller writes one line and needs no `#ifdef`; `NativeControlsAvailable()` says which you get.

```cpp
if (imshell::NativeSwitch("##notify", &notify))
    save();
```

Each is a `NativeView`, so the view rules above apply: it hides while an ImGui popup covers it, and ImGui's keyboard navigation does not reach it (the OS's does, with VoiceOver).
It draws with the system accent and the window's light or dark, not the theme's colours, so it looks right with `SystemTheme()` and out of step with a preset such as Tokyo Night; use the imshell widgets where either matters.
The control keeps its own size and sits in the middle of the ImGui frame, so it lines up with `GetFrameHeight()`.
A value you change from the app shows in the control on the next frame, and the ImGui item under the view still answers the test engine.
A segmented control and a pop-up read their labels once per id.

## Native context menus (`imshell/native_menu.hpp`)

`NativeContextMenu(id, builder)` is a right-click menu the OS draws and tracks: an `NSMenu` on macOS, a `TrackPopupMenu` menu on Windows and a `UIMenu` on iOS (shown at the touch by a `UIEditMenuInteraction`, iOS 16), so it has the system's look, animation, SF Symbol icons and VoiceOver.
Call it right after the item the menu belongs to; it returns the `id` of the chosen `NativeMenuItem` once, otherwise -1.

```cpp
ImGui::Selectable(name);
switch (imshell::NativeContextMenu("##row", [&] {
    return std::vector<imshell::NativeMenuItem>{
        {.label = "Copy", .id = 1, .icon = "doc.on.doc"},
        imshell::NativeMenuItem::Separator(),
        {.label = "Delete", .id = 2, .enabled = canDelete},
    };
})) {
case 1: copy(); break;
case 2: remove(); break;
}
```

An item has a label, an id, `enabled`, `checked`, an SF Symbol `icon` and `children` for a submenu.
The builder runs only when the menu opens.
macOS tracks a menu in its own loop, which cannot run inside an ImGui frame, so the shell opens it right after the frame is drawn and the choice comes back in the next call; on iOS the menu is asynchronous and the choice arrives when the user taps an item.
On iOS a long press right-clicks, which opens the menu; a separator becomes an inline section.
On Windows it is a `TrackPopupMenu` popup menu (no icons yet: Windows menus take bitmaps, and there is no Windows icon loader).
Where a platform has no backend (Linux and Android for now) it is imshell's own menu (`BeginContextMenu`, icons through `Icon`), drawn inside the frame from the builder, so a caller writes one thing; `NativeMenusAvailable()` says which you get.
A native menu cannot hold ImGui content, so a menu with a slider or a search box is an ImGui popup.

## Windows

`create` returns an `HWND` (any window: a Win32 control, a WebView2 host window). imshell makes it a child of a clipping window it creates inside the main window, so the main window's `WS_CLIPCHILDREN` keeps Direct3D from drawing over it.

Win32 controls send their notifications (`WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*`) to their parent, which is now imshell's clipping window. imshell sends them back to the control as `OCM_` messages (`WM_USER + 0x1c00` plus the message), the reflection ATL and WinForms use, so handle them by subclassing your control.

A click on ImGui gives the keyboard back to the main window.

## Linux

`create` returns a `GtkWidget*` (a `GtkEntry`, a `WebKitWebView`, anything). imshell puts it in a clipping `GtkFixed` (`GTK_OVERFLOW_HIDDEN`) laid over the `GtkGLArea` in a `GtkOverlay`, and moves it after GTK finishes drawing, since a layout change while drawing is not allowed. A click on ImGui gives the keyboard back to the GL area.

## Limits

- A native view can never sit *under* ImGui content.
- It does not follow ImGui transforms or animations; it is a rectangle placed once per frame.

## Tests

ImGui's test engine finds items by the label a widget registers with `IMGUI_TEST_ENGINE_ITEM_INFO`. imshell is compiled without the test engine, so a host that wants to find a native view in tests registers it itself, in a file compiled with the test engine:

```cpp
void* view = imshell::NativeView(id, size, create);
[[maybe_unused]] ImGuiContext& g = *GImGui;
IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), id, ImGuiItemStatusFlags_None);
```

The test engine's clicks go to ImGui, not the OS, so a host that clicks native buttons in tests also runs `ImGui::ButtonBehavior` on the item. A real click reaches the native button instead.

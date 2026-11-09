# Native views

`mydear::NativeView` (`mydear/native_view.hpp`) puts a real platform view, an `NSView` on macOS, a `UIView` on iOS an `HWND` on Windows or a `GtkWidget` on Linux, into the ImGui layout. Use it for what ImGui can't do well: web views, maps, video, PDF previews, native text fields (full IME and spell check), and system controls such as date and colour pickers.

```objc
auto* field = (__bridge NSTextField*)mydear::NativeView("##name", ImVec2(240, 22), [] {
    return (__bridge_retained void*)[NSTextField textFieldWithString:@""];
});
```

- `id`: an ImGui id, unique in the current window's id stack.
- `size`: like `ImGui::Button`; 0 or a negative value fills the remaining width or height.
- `create`: runs once per id and returns the view with a +1 reference: `(__bridge_retained void*)view`.
- Returns the view, or nullptr when `create` returned nothing.

Call it from Objective-C++ (`.mm`), since you create Cocoa objects.

## Behaviour

- **Layout:** the view takes its space like any ImGui item. After each frame mydear moves it there and clips it to its ImGui window, so scrolling, docking and resizing all follow.
- **Input:** the view gets its own clicks and keys from the OS. A click on ImGui gives the keyboard back to ImGui (on iOS, a touch on the ImGui view).
- **Always on top:** native views sit above everything ImGui draws, because ImGui draws the whole UI into one surface. While an ImGui window, popup or tooltip overlaps the view, mydear hides the view, so menus never end up behind it.
- **Lifetime:** a view not drawn in a frame (its tab closed, scrolled away, collapsed) is hidden. After 3 seconds unused it is removed and released.

## Native buttons (macOS)

`mydear::NativeButton(id, symbol, tooltip, title)` is a ready-made native view: an `NSButton` with an SF Symbol, in the accessory-bar style that shows its border only on hover, the way toolbar buttons look. It is tinted with ImGui's text colour, so it follows the theme, and with a `title` the text comes before the symbol. It reserves the width the button wanted last frame, so a changing title resizes it a frame later.

```cpp
if (mydear::NativeButton("###sidebar", "sidebar.left", "Toggle Sidebar"))
    toggleSidebar();
```

It returns true on a real click (the `NSButton`'s action, which also wakes an idle loop) and on a click on the ImGui item underneath, which is what Dear ImGui's test engine produces. Windows, Linux and iOS have no equivalent: draw an ImGui button there.

## Windows

`create` returns an `HWND` (any window: a Win32 control, a WebView2 host window). mydear makes it a child of a clipping window it creates inside the main window, so the main window's `WS_CLIPCHILDREN` keeps Direct3D from drawing over it.

Win32 controls send their notifications (`WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*`) to their parent, which is now mydear's clipping window. mydear sends them back to the control as `OCM_` messages (`WM_USER + 0x1c00` plus the message), the reflection ATL and WinForms use, so handle them by subclassing your control.

A click on ImGui gives the keyboard back to the main window.

## Linux

`create` returns a `GtkWidget*` (a `GtkEntry`, a `WebKitWebView`, anything). mydear puts it in a clipping `GtkFixed` (`GTK_OVERFLOW_HIDDEN`) laid over the `GtkGLArea` in a `GtkOverlay`, and moves it after GTK finishes drawing, since a layout change while drawing is not allowed. A click on ImGui gives the keyboard back to the GL area.

## Limits

- A native view can never sit *under* ImGui content.
- It does not follow ImGui transforms or animations; it is a rectangle placed once per frame.
- Frame captures and recordings don't include it.

## Tests

ImGui's test engine finds items by the label a widget registers with `IMGUI_TEST_ENGINE_ITEM_INFO`. mydear is compiled without the test engine, so a host that wants to find a native view in tests registers it itself, in a file compiled with the test engine:

```cpp
void* view = mydear::NativeView(id, size, create);
[[maybe_unused]] ImGuiContext& g = *GImGui;
IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), id, ImGuiItemStatusFlags_None);
```

The test engine's clicks go to ImGui, not the OS, so a host that clicks native buttons in tests also runs `ImGui::ButtonBehavior` on the item. A real click reaches the native button instead.

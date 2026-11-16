# Tray icon

`imshell/tray.hpp` lets an app keep running with its window closed: an icon in the menu bar, notification area or system tray with a menu, `HideWindow()` and `ShowWindow()`, and `Quit()`.
The icon calls need a running `App`, and every callback runs on the main thread.
All of it does nothing on iOS, Android and the web, so shared code needs no `#if`.

```cpp
#include "imshell/tray.hpp"

struct MyApp : imshell::Delegate {
    void setup() override {
        imshell::tray::Options options;
        options.tooltip = "My app";
        options.icon = {kIconPng, kIconPngSize}; // a square png, 64 px or more
        options.items = {{"Show", [] { imshell::ShowWindow(); }},
                         {"", nullptr, true}, // a separator
                         {"Quit", [] { imshell::Quit(); }}};
        options.onActivate = [] { imshell::ShowWindow(); }; // a left click
        imshell::tray::Set(options);
    }

    // the close button hides the window, unless the app is quitting for real
    bool closeRequested() override {
        if (imshell::QuitRequested() || !imshell::tray::Available())
            return true;
        imshell::HideWindow();
        return false;
    }
};
```

## The icon

- `tray::Set(options)` shows the icon, or updates it: call it again when the menu changes. `tray::Remove()` takes it away.
- `Options::tooltip` is the hover text. `Options::icon` is a PNG (`png`, `size`), a square of 64 px or more; without one the icon is the first letter of the tooltip on macOS and a stock icon elsewhere.
- `Options::items` is the menu: `Item{label, onClick, separator, enabled}`. A separator is a line, the rest of the item is ignored.
- `Options::onActivate` runs on a left click. Without it a left click opens the menu. A right click always opens the menu.
- `tray::Available()` is true when an icon is really shown: false before `Set`, when Windows refused the icon, and on Linux when no StatusNotifierWatcher and host are running (stock GNOME). An app that hides its window on close should quit instead when it is false, or the window is unreachable. On Linux it asks the bus, so call it when closing, not every frame.

## Hiding and showing the window

- `HideWindow()` takes the main window off the screen and the taskbar while the app and its frame loop keep running, idling like a background window.
- `ShowWindow()` brings it back, restoring a minimised window and activating the app.
- `WindowVisible()` is true when the window is shown and not minimised.
- A click on the Dock icon while no window shows calls `Delegate::reopenRequested()` on macOS. The default shows the window.

## Quitting

A delegate whose `closeRequested()` hides the window and returns false keeps the app alive.
`imshell::Quit()` ends the run loop whatever `closeRequested()` says: it is for a tray menu's quit item.
`QuitRequested()` is true while the app is quitting for real, that is after `Quit()`, and on macOS for cmd-Q and the Dock's Quit, which reach `closeRequested()` too: it should return true then.
On Windows, logoff and shutdown set `QuitRequested()` and send the window a close request, so a `closeRequested()` that hides the window does not hold the session up.
Alt-F4 on Windows and the close button are ordinary close requests.

## Per platform

| | Icon | Menu | Notes |
|---|---|---|---|
| macOS | an `NSStatusItem` in the menu bar, the PNG as a template image (drawn in the bar's colour from its alpha) | an `NSMenu` | a control-click opens the menu too |
| Windows | a notification area icon (`Shell_NotifyIcon`, version 4 callbacks), sized for the system dpi, added again when explorer restarts | a popup menu | the icon may start in the overflow flyout; a DPI change after `Set` needs another `Set` |
| Linux | a StatusNotifierItem on the session bus, registered with the StatusNotifierWatcher (again if it restarts) | `com.canonical.dbusmenu`, served by imshell | KDE and GNOME with the AppIndicator extension show it; the PNG is scaled to 64 px at most; with `onActivate` set a left click calls it, without it the host opens the menu |
| iOS, Android, web | none | none | every call is a no-op and `Available()` is false |

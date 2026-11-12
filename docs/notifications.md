# Notifications

`imshell/notifications.hpp` (namespace `imshell::notifications`) shows system notifications and puts a count on the app's icon. Call it from any thread; every callback runs on the UI thread.

```cpp
namespace n = imshell::notifications;

// at startup, before the loop: a click that launched the app waits for it
n::OnPress([](const std::string& id) { openChat(id); });

void notifyMe() {
    n::RequestPermission([](n::Permission p) {
        if (p != n::Permission::Granted)
            return;
        n::Show({.id = "chat:42", .title = "Ada", .body = "Are you coming?"});
        n::SetBadge(1);
    });
}
```

- `GetPermission(done)` answers without asking; `RequestPermission(done)` shows the system's prompt the first time. `Permission` is `Default` (not asked yet), `Granted`, `Denied` (only the system's settings change it) or `Unavailable`.
- `Show(notification, done)`: `id`, `title`, `body` and `silent` (no sound). Another with the same `id` replaces it. `done(false)` when the system did not take it.
- `Close(id)` removes it; `SetBadge(count)` sets the count on the app's icon, `0` clears it.
- `OnPress(handler)` gets the `id` of the notification the user clicked.

## Per platform

| | Notifications | Badge | Permission | Notes |
|---|---|---|---|---|
| macOS | the notification center (`UNUserNotificationCenter`) | the Dock tile | a prompt the first time | only from a signed `.app` outside temporary folders (`imshell_app` makes one); a bare exe is `Unavailable`. Clicking also brings the app forward |
| Linux | the desktop's notification daemon, `org.freedesktop.Notifications` over D-Bus (gio) | the Unity launcher API, on docks that read it (Ubuntu's, Plasma, Dash to Dock) | no prompt: `Granted` while a daemon answers, `Unavailable` otherwise | the app's `.desktop` file is taken to be `<program name>.desktop`. A click reaches only a running app, and does not raise the window |
| Windows, iOS, Android, web | — | — | `Unavailable` | not yet |

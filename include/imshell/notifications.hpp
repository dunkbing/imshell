#pragma once

#include <functional>
#include <string>

// system notifications and the app's badge. macos: the notification center, from a signed .app
// only (imshell_app makes one; a bare exe is Unavailable), the badge on the dock tile. linux:
// the desktop's notification daemon over d-bus (org.freedesktop.Notifications), the badge
// through the unity launcher api on docks that read it; the app's .desktop file is taken to be
// <program name>.desktop. elsewhere Unavailable for now. call from any thread (OnPress: the ui
// thread); every callback runs on the ui thread
namespace imshell::notifications {
    enum class Permission {
        Default,     // not asked yet
        Granted,
        Denied,      // only the system's settings change it
        Unavailable, // no notification center: a bare exe, no daemon, an unsupported platform
    };

    struct Notification {
        std::string id;     // showing another with the same id replaces it
        std::string title;
        std::string body;
        bool silent = false; // no sound
    };

    // what the system holds, without asking
    void GetPermission(std::function<void(Permission)> done);
    // the system's prompt the first time (macos); linux has none: Granted while a daemon runs
    void RequestPermission(std::function<void(Permission)> done);
    // shown is false when the system did not take it (no permission, no daemon)
    void Show(const Notification& notification, std::function<void(bool shown)> done = {});
    void Close(const std::string& id);
    // a count on the app's icon; 0 clears it
    void SetBadge(int count);
    // the user clicked a notification. set it at startup: a click that launched the app (macos)
    // waits for it
    void OnPress(std::function<void(const std::string& id)> handler);
} // namespace imshell::notifications

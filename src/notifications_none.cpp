// no notification center on this platform yet
#include "imshell/notifications.hpp"

namespace imshell::notifications {
    void GetPermission(std::function<void(Permission)> done) {
        if (done)
            done(Permission::Unavailable);
    }
    void RequestPermission(std::function<void(Permission)> done) {
        GetPermission(std::move(done));
    }
    void Show(const Notification&, std::function<void(bool)> done) {
        if (done)
            done(false);
    }
    void Close(const std::string&) {}
    void SetBadge(int) {}
    void OnPress(std::function<void(const std::string&)>) {}
} // namespace imshell::notifications

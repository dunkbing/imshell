// the browser's Notification api and the badging api (navigator.setAppBadge, installed apps).
// the page's answers and clicks queue in Module.imshellNotify and the loop picks them up
// (runLoopPass). no threads on the web: everything is the ui thread already
#include "../loop_pass.hpp"
#include "imshell/frames.hpp"
#include "imshell/notifications.hpp"

#include <emscripten.h>
#include <utility>
#include <vector>

using namespace imshell::notifications;

// clang-format off
EM_JS(int, imshell_notify_permission, (), {
    if (typeof Notification === "undefined") return 3;
    return ({"default": 0, "granted": 1, "denied": 2})[Notification.permission] ?? 3;
});

// the browser wants a recent click for the prompt: an imgui button counts, it is a frame away
EM_JS(void, imshell_notify_request, (), {
    const q = (Module.imshellNotify ??= {answers: [], pressed: [], shown: {}});
    if (typeof Notification === "undefined") { q.answers.push(3); return; }
    Notification.requestPermission().then(
        p => q.answers.push(({"default": 0, "granted": 1, "denied": 2})[p] ?? 3),
        () => q.answers.push(2));
});

// the same tag replaces it. android browsers only show them from a service worker: 0
EM_JS(int, imshell_notify_show, (const char* id, const char* title, const char* body, int silent), {
    if (typeof Notification === "undefined" || Notification.permission !== "granted") return 0;
    const q = (Module.imshellNotify ??= {answers: [], pressed: [], shown: {}});
    const tag = UTF8ToString(id);
    try {
        const n = new Notification(UTF8ToString(title), {body: UTF8ToString(body), tag, silent: !!silent});
        n.onclick = () => { window.focus(); n.close(); q.pressed.push(tag); };
        n.onclose = () => { if (q.shown[tag] === n) delete q.shown[tag]; };
        q.shown[tag] = n;
        return 1;
    } catch (e) {
        return 0;
    }
});

EM_JS(void, imshell_notify_close, (const char* id), {
    Module.imshellNotify?.shown[UTF8ToString(id)]?.close();
});

EM_JS(void, imshell_notify_badge, (int count), {
    if (!navigator.setAppBadge) return;
    (count > 0 ? navigator.setAppBadge(count) : navigator.clearAppBadge()).catch(() => {});
});

EM_JS(int, imshell_notify_answer, (), {
    const q = Module.imshellNotify;
    return q && q.answers.length ? q.answers.shift() : -1;
});

// the next click's id into out (size bytes); 0 when there is none
EM_JS(int, imshell_notify_pressed, (char* out, int size), {
    const q = Module.imshellNotify;
    if (!q || !q.pressed.length) return 0;
    stringToUTF8(q.pressed.shift(), out, size);
    return 1;
});
// clang-format on

namespace {
    std::vector<std::function<void(Permission)>> gAsking;
    std::function<void(const std::string&)> gOnPress;
    std::vector<std::string> gPending; // clicks before OnPress

    void pass() {
        if (!gAsking.empty())
            if (const int answer = imshell_notify_answer(); answer >= 0)
                for (auto& done : std::exchange(gAsking, {}))
                    if (done)
                        done(static_cast<Permission>(answer));
        char id[256];
        while (imshell_notify_pressed(id, sizeof id)) {
            if (gOnPress)
                gOnPress(id);
            else
                gPending.emplace_back(id);
            imshell::requestFrames();
        }
    }

    void hook() {
        imshell::detail::addLoopPass(pass);
    }
} // namespace

namespace imshell::notifications {
    void GetPermission(std::function<void(Permission)> done) {
        if (done)
            done(static_cast<Permission>(imshell_notify_permission()));
    }

    void RequestPermission(std::function<void(Permission)> done) {
        hook();
        gAsking.push_back(std::move(done));
        imshell_notify_request();
    }

    void Show(const Notification& n, std::function<void(bool)> done) {
        hook();
        const bool shown = imshell_notify_show(n.id.c_str(), n.title.c_str(), n.body.c_str(), n.silent);
        if (done)
            done(shown);
    }

    void Close(const std::string& id) {
        imshell_notify_close(id.c_str());
    }

    void SetBadge(int count) {
        imshell_notify_badge(count);
    }

    void OnPress(std::function<void(const std::string&)> handler) {
        hook();
        gOnPress = std::move(handler);
        for (const std::string& id : std::exchange(gPending, {}))
            if (gOnPress)
                gOnPress(id);
    }
} // namespace imshell::notifications

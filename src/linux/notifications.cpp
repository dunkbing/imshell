// the desktop's notification daemon (org.freedesktop.Notifications) and the unity launcher
// api's badge, over the session bus with gio. calls are async on the default main context,
// which the gtk loop runs on the ui thread, so replies and signals arrive there
#include "imshell/frames.hpp"
#include "imshell/notifications.hpp"

#include <gio/gio.h>
#include <map>
#include <optional>
#include <utility>
#include <vector>

using namespace imshell::notifications;

namespace {
    constexpr const char* kName = "org.freedesktop.Notifications";
    constexpr const char* kPath = "/org/freedesktop/Notifications";

    std::map<std::string, guint32> gIds; // ours to the daemon's, while shown
    // a Notify on its way, and the one to send after it: the daemon's id is needed to replace
    struct Queued {
        Notification notification;
        std::function<void(bool)> done;
    };
    std::map<std::string, std::optional<Queued>> gInFlight;
    std::function<void(const std::string&)> gOnPress;

    const char* program() {
        const char* name = g_get_prgname();
        return name ? name : "";
    }

    void onSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signal,
                  GVariant* params, gpointer) {
        guint32 daemonId = 0;
        const gchar* action = "";
        if (g_strcmp0(signal, "ActionInvoked") == 0)
            g_variant_get(params, "(u&s)", &daemonId, &action);
        else
            g_variant_get(params, "(uu)", &daemonId, nullptr); // NotificationClosed
        // every app's notifications come by: only ours are in the map
        for (auto it = gIds.begin(); it != gIds.end(); ++it) {
            if (it->second != daemonId)
                continue;
            const std::string id = it->first;
            if (g_strcmp0(signal, "NotificationClosed") == 0)
                gIds.erase(it);
            else if (g_strcmp0(action, "default") == 0 && gOnPress)
                gOnPress(id);
            imshell::requestFrames();
            return;
        }
    }

    // null without a session bus
    GDBusConnection* bus() {
        static GDBusConnection* connection = [] {
            GDBusConnection* c = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
            if (c)
                for (const char* signal : {"ActionInvoked", "NotificationClosed"})
                    g_dbus_connection_signal_subscribe(c, kName, kName, signal, kPath, nullptr,
                                                       G_DBUS_SIGNAL_FLAGS_NONE, onSignal, nullptr, nullptr);
            return c;
        }();
        return connection;
    }

    // the ui thread: the gtk loop runs the default main context there (g_main_context_invoke
    // could run it on the calling thread while the loop is between iterations)
    void onUi(std::function<void()> fn) {
        g_idle_add(
            [](gpointer data) -> gboolean {
                auto* f = static_cast<std::function<void()>*>(data);
                (*f)();
                delete f;
                return G_SOURCE_REMOVE;
            },
            new std::function<void()>(std::move(fn)));
    }

    // a call to the daemon; done gets the reply, null on an error
    void call(const char* method, GVariant* params, std::function<void(GVariant*)> done) {
        GDBusConnection* c = bus();
        if (!c) {
            if (params)
                g_variant_unref(g_variant_ref_sink(params));
            if (done)
                done(nullptr);
            return;
        }
        auto* pending = new std::function<void(GVariant*)>(std::move(done));
        g_dbus_connection_call(
            c, kName, kPath, kName, method, params, nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
            [](GObject* source, GAsyncResult* result, gpointer data) {
                auto* finish = static_cast<std::function<void(GVariant*)>*>(data);
                GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, nullptr);
                if (*finish)
                    (*finish)(reply);
                if (reply)
                    g_variant_unref(reply);
                delete finish;
                imshell::requestFrames();
            },
            pending);
    }
    void show(const Notification& n, const std::function<void(bool)>& done);
    void dismiss(const std::string& id);
    void setBadge(int count);
} // namespace

namespace imshell::notifications {
    // no prompt on linux: whether a daemon answers (one may start on the call)
    void GetPermission(std::function<void(Permission)> done) {
        onUi([done = std::move(done)] {
            call("GetCapabilities", nullptr, [done](GVariant* reply) {
                if (done)
                    done(reply ? Permission::Granted : Permission::Unavailable);
            });
        });
    }

    void RequestPermission(std::function<void(Permission)> done) {
        GetPermission(std::move(done));
    }

    void Show(const Notification& notification, std::function<void(bool)> done) {
        onUi([n = notification, done = std::move(done)] { show(n, done); });
    }

    void Close(const std::string& id) {
        onUi([id] { dismiss(id); });
    }

    void SetBadge(int count) {
        onUi([count] { setBadge(count); });
    }
} // namespace imshell::notifications

namespace {
    void show(const Notification& n, const std::function<void(bool)>& done) {
        if (const auto busy = gInFlight.find(n.id); busy != gInFlight.end()) {
            if (busy->second && busy->second->done)
                busy->second->done(false); // replaced before it was sent
            busy->second = Queued{n, done};
            return;
        }
        gInFlight[n.id];
        const auto old = gIds.find(n.id);
        GVariantBuilder actions;
        g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&actions, "s", "default"); // a click on the body
        g_variant_builder_add(&actions, "s", "Open");
        GVariantBuilder hints;
        g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(program()));
        if (n.silent)
            g_variant_builder_add(&hints, "{sv}", "suppress-sound", g_variant_new_boolean(TRUE));
        GVariant* params = g_variant_new("(susssasa{sv}i)", program(), old == gIds.end() ? 0u : old->second,
                                         program(), n.title.c_str(), n.body.c_str(), &actions, &hints, -1);
        call("Notify", params, [id = n.id, done](GVariant* reply) {
            if (reply) {
                guint32 daemonId = 0;
                g_variant_get(reply, "(u)", &daemonId);
                gIds[id] = daemonId;
            }
            if (done)
                done(reply != nullptr);
            const auto next = std::exchange(gInFlight[id], std::nullopt);
            gInFlight.erase(id);
            if (next)
                show(next->notification, next->done);
        });
    }

    void dismiss(const std::string& id) {
        if (const auto it = gIds.find(id); it != gIds.end())
            call("CloseNotification", g_variant_new("(u)", it->second), {});
    }

    void setBadge(int count) {
        GDBusConnection* c = bus();
        if (!c || !*program()) // no .desktop name to put it on
            return;
        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&props, "{sv}", "count", g_variant_new_int64(count));
        g_variant_builder_add(&props, "{sv}", "count-visible", g_variant_new_boolean(count > 0));
        const std::string uri = std::string("application://") + program() + ".desktop";
        g_dbus_connection_emit_signal(c, nullptr, "/com/canonical/unity/launcherentry/imshell",
                                      "com.canonical.Unity.LauncherEntry", "Update",
                                      g_variant_new("(sa{sv})", uri.c_str(), &props), nullptr);
    }

} // namespace

namespace imshell::notifications {
    void OnPress(std::function<void(const std::string&)> handler) {
        bus(); // subscribed before the first notification
        gOnPress = std::move(handler);
    }
} // namespace imshell::notifications

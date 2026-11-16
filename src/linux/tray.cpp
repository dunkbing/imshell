// the tray icon as a StatusNotifierItem on the session bus (org.kde.StatusNotifierItem), its menu
// served as com.canonical.dbusmenu, registered with the desktop's StatusNotifierWatcher. kde and
// the gnome appindicator extension show it; stock gnome has no watcher, so Available() is false.
// everything runs on the main context the gtk loop iterates, so callbacks arrive on the ui thread
#include "imshell/frames.hpp"
#include "imshell/image.hpp"
#include "imshell/tray.hpp"

#include <gio/gio.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <vector>

namespace imshell::tray {
    namespace {
        constexpr const char* kWatcher = "org.kde.StatusNotifierWatcher";
        constexpr const char* kWatcherPath = "/StatusNotifierWatcher";
        constexpr const char* kItemPath = "/StatusNotifierItem";
        constexpr const char* kMenuPath = "/MenuBar";
        constexpr int kMaxIcon = 64; // larger pngs are scaled down: hosts draw 16 to 48

        const char* kItemXml = R"xml(<node><interface name="org.kde.StatusNotifierItem">
<method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
<signal name="NewTitle"/><signal name="NewIcon"/><signal name="NewToolTip"/>
<signal name="NewStatus"><arg type="s"/></signal>
<property name="Category" type="s" access="read"/>
<property name="Id" type="s" access="read"/>
<property name="Title" type="s" access="read"/>
<property name="Status" type="s" access="read"/>
<property name="WindowId" type="i" access="read"/>
<property name="IconName" type="s" access="read"/>
<property name="IconPixmap" type="a(iiay)" access="read"/>
<property name="OverlayIconName" type="s" access="read"/>
<property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
<property name="AttentionIconName" type="s" access="read"/>
<property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
<property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
<property name="ItemIsMenu" type="b" access="read"/>
<property name="Menu" type="o" access="read"/>
</interface></node>)xml";

        const char* kMenuXml = R"xml(<node><interface name="com.canonical.dbusmenu">
<method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/>
<arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method>
<method name="GetGroupProperties"><arg type="ai" direction="in"/><arg type="as" direction="in"/>
<arg type="a(ia{sv})" direction="out"/></method>
<method name="GetProperty"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
<method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method>
<method name="EventGroup"><arg type="a(isvu)" direction="in"/><arg type="ai" direction="out"/></method>
<method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
<method name="AboutToShowGroup"><arg type="ai" direction="in"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/></method>
<signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal>
<signal name="ItemActivationRequested"><arg type="i"/><arg type="u"/></signal>
<property name="Version" type="u" access="read"/>
<property name="TextDirection" type="s" access="read"/>
<property name="Status" type="s" access="read"/>
<property name="IconThemePath" type="as" access="read"/>
</interface></node>)xml";

        Options gOptions;
        GDBusConnection* gConn = nullptr;
        GDBusNodeInfo* gItemInfo = nullptr;
        GDBusNodeInfo* gMenuInfo = nullptr;
        guint gItemReg = 0, gMenuReg = 0, gOwn = 0, gWatch = 0;
        std::string gBusName;
        bool gNameOwned = false, gRegistered = false;
        guint32 gRevision = 1;
        int gPixW = 0, gPixH = 0;
        std::vector<unsigned char> gPixels; // argb, network byte order, as the protocol wants

        const char* appId() {
            const char* name = g_get_prgname();
            return name && *name ? name : "imshell";
        }

        // the png as an argb pixmap of at most kMaxIcon: a box filter, weighted by alpha
        void makePixmap(const Options::Icon& icon) {
            gPixW = gPixH = 0;
            gPixels.clear();
            if (!icon.png)
                return;
            std::vector<unsigned char> rgba;
            int w = 0, h = 0;
            LoadTexture(icon.png, icon.size, [&](const uint8_t* p, int pw, int ph) {
                rgba.assign(p, p + static_cast<size_t>(pw) * ph * 4);
                w = pw;
                h = ph;
                return ImTextureID{};
            });
            if (w <= 0 || h <= 0)
                return;
            const int s = std::min({w, h, kMaxIcon});
            gPixW = gPixH = s;
            gPixels.resize(static_cast<size_t>(s) * s * 4);
            for (int y = 0; y < s; ++y) {
                const int y0 = y * h / s, y1 = std::max(y0 + 1, (y + 1) * h / s);
                for (int x = 0; x < s; ++x) {
                    const int x0 = x * w / s, x1 = std::max(x0 + 1, (x + 1) * w / s);
                    unsigned long long r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int sy = y0; sy < y1; ++sy)
                        for (int sx = x0; sx < x1; ++sx) {
                            const unsigned char* p = &rgba[(static_cast<size_t>(sy) * w + sx) * 4];
                            r += p[0] * p[3];
                            g += p[1] * p[3];
                            b += p[2] * p[3];
                            a += p[3];
                            ++n;
                        }
                    unsigned char* o = &gPixels[(static_cast<size_t>(y) * s + x) * 4];
                    o[0] = static_cast<unsigned char>(a / n);
                    o[1] = a ? static_cast<unsigned char>(r / a) : 0;
                    o[2] = a ? static_cast<unsigned char>(g / a) : 0;
                    o[3] = a ? static_cast<unsigned char>(b / a) : 0;
                }
            }
        }

        // a(iiay): the one pixmap, or none
        GVariant* pixmaps() {
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE("a(iiay)"));
            if (!gPixels.empty())
                g_variant_builder_add(&b, "(ii@ay)", gPixW, gPixH,
                                      g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, gPixels.data(), gPixels.size(), 1));
            return g_variant_builder_end(&b);
        }

        // --- the item ---
        GVariant* itemProperty(const char* name) {
            const std::string n = name;
            if (n == "Category")
                return g_variant_new_string("ApplicationStatus");
            if (n == "Id")
                return g_variant_new_string(appId());
            if (n == "Title")
                return g_variant_new_string(gOptions.tooltip.empty() ? appId() : gOptions.tooltip.c_str());
            if (n == "Status")
                return g_variant_new_string("Active");
            if (n == "WindowId")
                return g_variant_new_int32(0);
            if (n == "IconName") // a theme icon, for a png-less tray: the app's own, by program name
                return g_variant_new_string(gPixels.empty() ? appId() : "");
            if (n == "IconPixmap")
                return pixmaps();
            if (n == "OverlayIconName" || n == "AttentionIconName")
                return g_variant_new_string("");
            if (n == "OverlayIconPixmap" || n == "AttentionIconPixmap")
                return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0);
            if (n == "ToolTip")
                return g_variant_new("(s@a(iiay)ss)", "", pixmaps(), gOptions.tooltip.c_str(), "");
            if (n == "ItemIsMenu") // without onActivate the host opens the menu on a left click
                return g_variant_new_boolean(!gOptions.onActivate);
            if (n == "Menu")
                return g_variant_new_object_path(kMenuPath);
            return nullptr;
        }

        void itemCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                      GVariant*, GDBusMethodInvocation* invocation, gpointer) {
            if (g_strcmp0(method, "Activate") == 0 && gOptions.onActivate) {
                auto activate = gOptions.onActivate; // a copy: it may call Set()
                activate();
                requestFrames();
            }
            g_dbus_method_invocation_return_value(invocation, nullptr);
        }

        GVariant* itemGet(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property,
                          GError** error, gpointer) {
            GVariant* v = itemProperty(property);
            if (!v)
                g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "no property %s", property);
            return v;
        }

        // --- the menu: id 0 is the root, item i is id i + 1 ---
        std::string mnemonicSafe(const std::string& label) { // dbusmenu reads "_" as a mnemonic
            std::string out;
            for (char c : label) {
                out += c;
                if (c == '_')
                    out += '_';
            }
            return out;
        }

        GVariant* menuProps(int id) {
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
            if (id == 0) {
                g_variant_builder_add(&b, "{sv}", "children-display", g_variant_new_string("submenu"));
            } else if (id <= static_cast<int>(gOptions.items.size())) {
                const Item& it = gOptions.items[id - 1];
                if (it.separator) {
                    g_variant_builder_add(&b, "{sv}", "type", g_variant_new_string("separator"));
                } else {
                    g_variant_builder_add(&b, "{sv}", "label", g_variant_new_string(mnemonicSafe(it.label).c_str()));
                    g_variant_builder_add(&b, "{sv}", "enabled", g_variant_new_boolean(it.enabled));
                }
            }
            return g_variant_builder_end(&b);
        }

        GVariant* layout(int id) {
            GVariantBuilder children;
            g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
            if (id == 0)
                for (size_t i = 0; i < gOptions.items.size(); ++i)
                    g_variant_builder_add(&children, "v", layout(static_cast<int>(i) + 1));
            return g_variant_new("(i@a{sv}@av)", id, menuProps(id), g_variant_builder_end(&children));
        }

        void menuCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                      GVariant* params, GDBusMethodInvocation* invocation, gpointer) {
            auto click = [](int id) {
                if (id < 1 || id > static_cast<int>(gOptions.items.size()))
                    return;
                auto action = gOptions.items[id - 1].onClick; // a copy: it may call Set()
                if (action) {
                    action();
                    requestFrames();
                }
            };
            const std::string m = method;
            if (m == "GetLayout") {
                gint32 parent = 0;
                g_variant_get_child(params, 0, "i", &parent);
                g_dbus_method_invocation_return_value(invocation,
                                                      g_variant_new("(u@(ia{sv}av))", gRevision, layout(parent)));
            } else if (m == "GetGroupProperties") {
                GVariantBuilder b;
                g_variant_builder_init(&b, G_VARIANT_TYPE("a(ia{sv})"));
                GVariant* ids = g_variant_get_child_value(params, 0);
                const gsize n = g_variant_n_children(ids);
                auto add = [&](int id) { g_variant_builder_add(&b, "(i@a{sv})", id, menuProps(id)); };
                if (n == 0)
                    for (int id = 0; id <= static_cast<int>(gOptions.items.size()); ++id)
                        add(id);
                for (gsize i = 0; i < n; ++i) {
                    gint32 id = 0;
                    g_variant_get_child(ids, i, "i", &id);
                    add(id);
                }
                g_variant_unref(ids);
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&b)));
            } else if (m == "GetProperty") {
                gint32 id = 0;
                const gchar* name = "";
                g_variant_get(params, "(i&s)", &id, &name);
                GVariant* props = menuProps(id);
                GVariant* v = g_variant_lookup_value(props, name, nullptr);
                g_dbus_method_invocation_return_value(
                    invocation, g_variant_new("(v)", v ? v : g_variant_new_string("")));
                if (v)
                    g_variant_unref(v);
                g_variant_unref(props);
            } else if (m == "Event") {
                gint32 id = 0;
                const gchar* event = "";
                g_variant_get_child(params, 0, "i", &id);
                g_variant_get_child(params, 1, "&s", &event);
                const bool clicked = g_strcmp0(event, "clicked") == 0;
                g_dbus_method_invocation_return_value(invocation, nullptr);
                if (clicked)
                    click(id);
            } else if (m == "EventGroup") {
                GVariant* events = g_variant_get_child_value(params, 0);
                std::vector<int> ids;
                for (gsize i = 0; i < g_variant_n_children(events); ++i) {
                    gint32 id = 0;
                    const gchar* event = "";
                    g_variant_get_child(events, i, "(i&svu)", &id, &event, nullptr, nullptr);
                    if (g_strcmp0(event, "clicked") == 0)
                        ids.push_back(id);
                }
                g_variant_unref(events);
                g_dbus_method_invocation_return_value(invocation,
                                                      g_variant_new("(@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
                for (int id : ids)
                    click(id);
            } else if (m == "AboutToShow") {
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            } else if (m == "AboutToShowGroup") {
                g_dbus_method_invocation_return_value(
                    invocation, g_variant_new("(@ai@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0),
                                              g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
            } else {
                g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                      "no method %s", method);
            }
        }

        GVariant* menuGet(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property,
                          GError** error, gpointer) {
            const std::string n = property;
            if (n == "Version")
                return g_variant_new_uint32(3);
            if (n == "TextDirection")
                return g_variant_new_string("ltr");
            if (n == "Status")
                return g_variant_new_string("normal");
            if (n == "IconThemePath")
                return g_variant_new_strv(nullptr, 0);
            g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "no property %s", property);
            return nullptr;
        }

        // --- the watcher ---
        // needs the bus name owned and a watcher to tell; either can come first, and the watcher
        // can leave and come back (a shell restart)
        void tryRegister() {
            if (!gConn || !gNameOwned || gRegistered)
                return;
            gRegistered = true;
            g_dbus_connection_call(gConn, kWatcher, kWatcherPath, kWatcher, "RegisterStatusNotifierItem",
                                   g_variant_new("(s)", gBusName.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
                                   +[](GObject* source, GAsyncResult* result, gpointer) {
                                       GVariant* r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, nullptr);
                                       if (r)
                                           g_variant_unref(r);
                                       else
                                           gRegistered = false; // no watcher yet: its appearing tries again
                                   },
                                   nullptr);
        }

        // a watcher and a host to draw the icon: otherwise it would go nowhere
        bool hostPresent() {
            if (!gConn)
                return false;
            GVariant* r = g_dbus_connection_call_sync(
                gConn, kWatcher, kWatcherPath, "org.freedesktop.DBus.Properties", "Get",
                g_variant_new("(ss)", kWatcher, "IsStatusNotifierHostRegistered"), G_VARIANT_TYPE("(v)"),
                G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, nullptr);
            if (!r)
                return false;
            GVariant* inner = nullptr;
            g_variant_get(r, "(v)", &inner);
            const bool present = inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(inner);
            if (inner)
                g_variant_unref(inner);
            g_variant_unref(r);
            return present;
        }

        void emit(const char* path, const char* iface, const char* signal, GVariant* params = nullptr) {
            if (gConn)
                g_dbus_connection_emit_signal(gConn, nullptr, path, iface, signal, params, nullptr);
        }
    } // namespace

    bool Available() {
        return gItemReg && hostPresent();
    }

    void Set(const Options& options) {
        gOptions = options;
        makePixmap(options.icon);
        if (gItemReg) { // already up: tell the host what changed
            ++gRevision;
            emit(kMenuPath, "com.canonical.dbusmenu", "LayoutUpdated", g_variant_new("(ui)", gRevision, 0));
            emit(kItemPath, "org.kde.StatusNotifierItem", "NewTitle");
            emit(kItemPath, "org.kde.StatusNotifierItem", "NewIcon");
            emit(kItemPath, "org.kde.StatusNotifierItem", "NewToolTip");
            return;
        }
        gConn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        if (!gConn)
            return; // no session bus
        gItemInfo = g_dbus_node_info_new_for_xml(kItemXml, nullptr);
        gMenuInfo = g_dbus_node_info_new_for_xml(kMenuXml, nullptr);
        static const GDBusInterfaceVTable itemTable = {itemCall, itemGet, nullptr, {}};
        static const GDBusInterfaceVTable menuTable = {menuCall, menuGet, nullptr, {}};
        gItemReg = g_dbus_connection_register_object(gConn, kItemPath, gItemInfo->interfaces[0], &itemTable, nullptr,
                                                     nullptr, nullptr);
        gMenuReg = g_dbus_connection_register_object(gConn, kMenuPath, gMenuInfo->interfaces[0], &menuTable, nullptr,
                                                     nullptr, nullptr);
        gBusName = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
        gOwn = g_bus_own_name_on_connection(
            gConn, gBusName.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE,
            +[](GDBusConnection*, const gchar*, gpointer) {
                gNameOwned = true;
                tryRegister();
            },
            +[](GDBusConnection*, const gchar*, gpointer) { gNameOwned = false; }, nullptr, nullptr);
        gWatch = g_bus_watch_name_on_connection(
            gConn, kWatcher, G_BUS_NAME_WATCHER_FLAGS_NONE,
            +[](GDBusConnection*, const gchar*, const gchar*, gpointer) {
                gRegistered = false;
                tryRegister();
            },
            +[](GDBusConnection*, const gchar*, gpointer) { gRegistered = false; }, nullptr, nullptr);
    }

    void Remove() {
        if (gWatch)
            g_bus_unwatch_name(gWatch);
        if (gOwn)
            g_bus_unown_name(gOwn);
        if (gConn && gItemReg)
            g_dbus_connection_unregister_object(gConn, gItemReg);
        if (gConn && gMenuReg)
            g_dbus_connection_unregister_object(gConn, gMenuReg);
        if (gItemInfo)
            g_dbus_node_info_unref(gItemInfo);
        if (gMenuInfo)
            g_dbus_node_info_unref(gMenuInfo);
        if (gConn)
            g_object_unref(gConn);
        gConn = nullptr;
        gItemInfo = gMenuInfo = nullptr;
        gItemReg = gMenuReg = gOwn = gWatch = 0;
        gNameOwned = gRegistered = false;
        gOptions = {};
        gPixels.clear();
    }
} // namespace imshell::tray

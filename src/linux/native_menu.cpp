// the toolkit's own context menu on linux: a GtkPopoverMenu from a GMenu model, its items actions
// of a GSimpleActionGroup. a popover is asynchronous (the loop keeps running), so the choice
// reaches `done` when the action activates
#include "../native_menu.hpp"
#include "../native_view.hpp"

#include <gtk/gtk.h>

#include <memory>
#include <string>

namespace imshell::detail {
    namespace {
        // the built menu: the model, the actions, and a mirror of the items so a test can walk it
        struct Node {
            std::string action; // "" for a separator or a submenu
            std::vector<Node> children;
        };

        struct Built {
            GMenu* menu = nullptr;
            GSimpleActionGroup* group = nullptr;
            std::vector<Node> nodes;
            int counter = 0;
        };

        struct Choice { // what an action hands back
            std::shared_ptr<std::function<void(int)>> done;
            int id;
        };

        // shortcut: no icons (SF Symbol names are not GTK icon names, and popover items take a named icon only); map them with the GTK icon loader when it exists
        std::vector<Node> fill(Built& built, GMenu* into, const std::vector<NativeMenuItem>& items, const std::shared_ptr<std::function<void(int)>>& done) {
            std::vector<Node> nodes;
            GMenu* section = g_menu_new();
            auto flush = [&] {
                if (g_menu_model_get_n_items(G_MENU_MODEL(section)) > 0)
                    g_menu_append_section(into, nullptr, G_MENU_MODEL(section));
                g_object_unref(section);
                section = g_menu_new();
            };
            for (const NativeMenuItem& item : items) {
                Node node;
                if (item.separator) {
                    flush(); // a separator starts a section
                    nodes.push_back(node);
                    continue;
                }
                if (!item.children.empty()) {
                    GMenu* sub = g_menu_new();
                    node.children = fill(built, sub, item.children, done);
                    GMenuItem* entry = g_menu_item_new_submenu(item.label.c_str(), G_MENU_MODEL(sub));
                    g_menu_append_item(section, entry);
                    g_object_unref(entry);
                    g_object_unref(sub);
                    nodes.push_back(std::move(node));
                    continue;
                }
                node.action = "item" + std::to_string(built.counter++);
                // a checkable item is a stateful action: the popover draws its check
                GSimpleAction* action = item.checked ? g_simple_action_new_stateful(node.action.c_str(), nullptr, g_variant_new_boolean(TRUE))
                                                     : g_simple_action_new(node.action.c_str(), nullptr);
                g_simple_action_set_enabled(action, item.enabled);
                g_signal_connect_data(action, "activate",
                                      G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer p) {
                                          auto* c = static_cast<Choice*>(p);
                                          (*c->done)(c->id);
                                      }),
                                      new Choice{done, item.id}, [](gpointer p, GClosure*) { delete static_cast<Choice*>(p); }, GConnectFlags(0));
                g_action_map_add_action(G_ACTION_MAP(built.group), G_ACTION(action));
                g_object_unref(action);
                const std::string name = "menu." + node.action;
                GMenuItem* entry = g_menu_item_new(item.label.c_str(), name.c_str());
                g_menu_append_item(section, entry);
                g_object_unref(entry);
                nodes.push_back(std::move(node));
            }
            flush();
            g_object_unref(section); // the empty one flush left
            return nodes;
        }

        // builds the menu for `items`; `done` gets the id of an action that activates
        std::unique_ptr<Built, void (*)(Built*)> build(const std::vector<NativeMenuItem>& items, std::function<void(int)> done) {
            std::unique_ptr<Built, void (*)(Built*)> built(new Built, [](Built* b) {
                if (b->menu)
                    g_object_unref(b->menu);
                if (b->group)
                    g_object_unref(b->group);
                delete b;
            });
            built->menu = g_menu_new();
            built->group = g_simple_action_group_new();
            built->nodes = fill(*built, built->menu, items, std::make_shared<std::function<void(int)>>(std::move(done)));
            return built;
        }

        const Node* walk(const std::vector<Node>& nodes, const std::vector<int>& path, size_t at) {
            if (at >= path.size() || path[at] < 0 || static_cast<size_t>(path[at]) >= nodes.size())
                return nullptr;
            const Node& node = nodes[static_cast<size_t>(path[at])];
            return at + 1 == path.size() ? &node : walk(node.children, path, at + 1);
        }
    } // namespace

    bool nativeMenusAvailable() {
        return true;
    }

    void nativeMenuPopUp(const std::vector<NativeMenuItem>& items, ImVec2 pos, const std::function<void(int)>& done) {
        GtkWidget* host = static_cast<GtkWidget*>(nativeHost());
        if (!host) {
            done(-1);
            return;
        }
        auto built = build(items, done);
        GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(built->menu));
        gtk_widget_insert_action_group(popover, "menu", G_ACTION_GROUP(built->group));
        gtk_widget_set_parent(popover, host);
        gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
        gtk_widget_set_halign(popover, GTK_ALIGN_START);
        const GdkRectangle at = {static_cast<int>(pos.x), static_cast<int>(pos.y), 1, 1};
        gtk_popover_set_pointing_to(GTK_POPOVER(popover), &at);
        // the popover closes after the action runs: take it down then, with the menu it holds
        g_signal_connect(popover, "closed", G_CALLBACK(+[](GtkPopover* p, gpointer) {
                             g_idle_add(+[](gpointer w) -> gboolean {
                                 gtk_widget_unparent(GTK_WIDGET(w));
                                 return G_SOURCE_REMOVE;
                             },
                                        p);
                         }),
                         nullptr);
        gtk_popover_popup(GTK_POPOVER(popover));
    }

    // activates the item at `path` as a click would: its action, if it is enabled
    int nativeMenuPick(const std::vector<NativeMenuItem>& items, const std::vector<int>& path) {
        int chosen = -1;
        auto built = build(items, [&](int id) { chosen = id; });
        const Node* node = walk(built->nodes, path, 0);
        if (node && !node->action.empty() && g_action_group_get_action_enabled(G_ACTION_GROUP(built->group), node->action.c_str()))
            g_action_group_activate_action(G_ACTION_GROUP(built->group), node->action.c_str(), nullptr);
        return chosen;
    }
} // namespace imshell::detail

// native switch, segmented control and pop-up on linux: a GtkSwitch, linked GtkToggleButtons and a
// GtkDropDown, each in a box that NativeView places at the imgui frame's size, so the control
// keeps its own size and sits in the middle of it vertically
#include "../native_controls.hpp"
#include "imgui_internal.h"
#include "imshell/frames.hpp"
#include "imshell/native_view.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace imshell::detail {
    namespace {
        // what the control's signals leave for the next frame
        struct State {
            bool changed = false; // the user changed it
            bool quiet = false;   // we are setting it ourselves: not a change
            int selected = -1;    // segmented control and pop-up: the chosen index
        };

        State* stateOf(GtkWidget* box) {
            return static_cast<State*>(g_object_get_data(G_OBJECT(box), "imshell-state"));
        }

        // `control` in a box that lets clicks through wherever the control is not
        GtkWidget* wrap(GtkWidget* control, State* state) {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_set_can_target(box, FALSE);
            gtk_widget_set_halign(control, GTK_ALIGN_START);
            gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(box), control);
            g_object_set_data_full(G_OBJECT(box), "imshell-state", state, [](gpointer p) { delete static_cast<State*>(p); });
            return box;
        }

        GtkWidget* controlOf(GtkWidget* box) {
            return gtk_widget_get_first_child(box);
        }

        bool disabled() {
            return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
        }

        ImVec2 measure(GtkWidget* widget) {
            int w = 0, h = 0;
            gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1, nullptr, &w, nullptr, nullptr);
            gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1, nullptr, &h, nullptr, nullptr);
            return ImVec2(static_cast<float>(w), static_cast<float>(h));
        }

        ImVec2 frameOf(ImVec2 size) {
            return ImVec2(size.x, std::max(size.y, ImGui::GetFrameHeight()));
        }

        void onNotify(GObject*, GParamSpec*, gpointer data) {
            auto* state = static_cast<State*>(data);
            if (state->quiet)
                return;
            state->changed = true;
            requestFrames(); // the change came outside imgui: wake the idle loop
        }

        // a toggle button of a segmented control: remembers its index, and counts only the one turning on
        struct Segment {
            State* state;
            int index;
        };

        void onSegment(GtkToggleButton* button, gpointer data) {
            auto* segment = static_cast<Segment*>(data);
            if (!gtk_toggle_button_get_active(button) || segment->state->quiet)
                return;
            segment->state->selected = segment->index;
            segment->state->changed = true;
            requestFrames();
        }

        void onDropDown(GObject* object, GParamSpec*, gpointer data) {
            auto* state = static_cast<State*>(data);
            if (state->quiet)
                return;
            state->selected = static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(object)));
            state->changed = true;
            requestFrames();
        }

        // the imgui item under the view: what the test engine clicks
        bool itemPressed(ImGuiID key) {
            bool hovered = false, held = false;
            return ImGui::ButtonBehavior(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), key, &hovered, &held);
        }
    } // namespace

    bool nativeControlsAvailable() {
        return true;
    }

    bool nativeSwitch(const char* id, bool* value) {
        static ImVec2 size(0, 0); // the toolkit's own, measured once
        if (size.x == 0) {
            GtkWidget* probe = g_object_ref_sink(gtk_switch_new());
            size = measure(probe);
            g_object_unref(probe);
        }
        const ImGuiID key = ImGui::GetID(id);
        GtkWidget* box = static_cast<GtkWidget*>(NativeView(id, frameOf(size), [&]() -> void* {
            GtkWidget* control = gtk_switch_new();
            gtk_switch_set_active(GTK_SWITCH(control), *value);
            auto* state = new State;
            g_signal_connect(control, "notify::active", G_CALLBACK(onNotify), state);
            return wrap(control, state);
        }));
        const bool pressed = itemPressed(key);
        if (!box) {
            if (pressed)
                *value = !*value;
            return pressed;
        }
        GtkWidget* control = controlOf(box);
        State* state = stateOf(box);
        const bool on = gtk_switch_get_active(GTK_SWITCH(control));
        bool changed = false;
        if (state->changed) { // a click on the control itself: it already shows the new state
            state->changed = false;
            *value = on;
            changed = true;
        } else if (pressed) {
            *value = !*value;
            changed = true;
        }
        if (on != *value) { // set from the app (or the test engine)
            state->quiet = true;
            gtk_switch_set_active(GTK_SWITCH(control), *value);
            state->quiet = false;
        }
        gtk_widget_set_sensitive(control, !disabled());
        return changed;
    }

    bool nativeSegmented(const char* id, std::span<const char* const> labels, int* current) {
        const ImGuiID key = ImGui::GetID(id);
        // the size the control wants: known after it exists, a guess before
        static std::unordered_map<ImGuiID, ImVec2> sizes;
        ImVec2& size = sizes[key];
        GtkWidget* box = static_cast<GtkWidget*>(NativeView(id, size.x > 0 ? frameOf(size) : ImVec2(200, ImGui::GetFrameHeight()), [&]() -> void* {
            auto* state = new State;
            state->selected = *current;
            GtkWidget* control = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_add_css_class(control, "linked");
            GtkWidget* first = nullptr;
            int index = 0;
            for (const char* label : labels) {
                GtkWidget* button = gtk_toggle_button_new_with_label(label);
                if (first)
                    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(button), GTK_TOGGLE_BUTTON(first));
                else
                    first = button;
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), index == *current);
                g_signal_connect_data(button, "toggled", G_CALLBACK(onSegment), new Segment{state, index},
                                      [](gpointer p, GClosure*) { delete static_cast<Segment*>(p); }, GConnectFlags(0));
                gtk_box_append(GTK_BOX(control), button);
                ++index;
            }
            return wrap(control, state);
        }));
        if (!box)
            return false;
        GtkWidget* control = controlOf(box);
        State* state = stateOf(box);
        size = measure(control);
        bool changed = false;
        if (state->changed) {
            state->changed = false;
            changed = state->selected != *current;
            *current = state->selected;
        } else if (state->selected != *current) { // set from the app
            state->quiet = true;
            GtkWidget* button = gtk_widget_get_first_child(control);
            for (int i = 0; button && i < *current; ++i)
                button = gtk_widget_get_next_sibling(button);
            if (button)
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
            state->selected = *current;
            state->quiet = false;
        }
        gtk_widget_set_sensitive(control, !disabled());
        return changed;
    }

    bool nativePopUp(const char* id, std::span<const char* const> items, int* current) {
        const ImGuiID key = ImGui::GetID(id);
        static std::unordered_map<ImGuiID, ImVec2> sizes;
        ImVec2& size = sizes[key];
        GtkWidget* box = static_cast<GtkWidget*>(NativeView(id, size.x > 0 ? frameOf(size) : ImVec2(160, ImGui::GetFrameHeight()), [&]() -> void* {
            auto* state = new State;
            state->selected = *current;
            std::vector<const char*> strings(items.begin(), items.end());
            strings.push_back(nullptr); // the list ends with null
            GtkWidget* control = gtk_drop_down_new_from_strings(strings.data());
            gtk_drop_down_set_selected(GTK_DROP_DOWN(control), static_cast<guint>(*current));
            g_signal_connect(control, "notify::selected", G_CALLBACK(onDropDown), state);
            return wrap(control, state);
        }));
        if (!box)
            return false;
        GtkWidget* control = controlOf(box);
        State* state = stateOf(box);
        size = measure(control);
        bool changed = false;
        if (state->changed) {
            state->changed = false;
            changed = state->selected != *current;
            *current = state->selected;
        } else if (state->selected != *current) { // set from the app
            state->quiet = true;
            gtk_drop_down_set_selected(GTK_DROP_DOWN(control), static_cast<guint>(*current));
            state->selected = *current;
            state->quiet = false;
        }
        gtk_widget_set_sensitive(control, !disabled());
        return changed;
    }
} // namespace imshell::detail

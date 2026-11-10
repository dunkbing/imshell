// the hello example's native views on linux: gtk widgets
#include <cstdio>
#include <gtk/gtk.h>

void* makeTextField() {
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "A native GtkEntry");
    return entry;
}

void* makeDatePicker() {
    return gtk_spin_button_new_with_range(1, 31, 1);
}

// "x,y wxh shown|hidden" of the view's clip box, in window pixels from the top left
void describeNativeView(void* view, char* out, int size) {
    GtkWidget* clip = gtk_widget_get_parent(static_cast<GtkWidget*>(view));
    graphene_rect_t r{};
    if (!gtk_widget_compute_bounds(clip, gtk_widget_get_parent(clip), &r))
        r = {};
    std::snprintf(out, size, "%.0f,%.0f %.0fx%.0f %s", r.origin.x, r.origin.y, r.size.width,
                  r.size.height, gtk_widget_get_visible(clip) ? "shown" : "hidden");
}

bool closeLikeUser() {
    return false; // shortcut: no user-like close here yet
}

#pragma once

#include "imgui_internal.h" // ImRect

// what a shell does for mydear/native_view.hpp. src/native_view.cpp keeps the entries and
// decides where each view goes and whether it shows; the platform file moves real views
namespace mydear::detail {
    void* nativeHost();      // the shell's view imgui draws into (NSView*, UIView*, HWND, GtkWidget*)
    void placeNativeViews(); // after ImGui::Render(): move, clip, hide, free
    void dropNativeViews();  // App::destroy()

    // per platform: `view` is what the app's create returned (it hands over one reference).
    // attach wraps it in a clipping container on the host and returns the container
    void* nativeAttach(void* view);
    // container at `visible`, the view at `full` inside it (both in imgui coordinates)
    void nativePlace(void* clip, void* view, const ImRect& visible, const ImRect& full);
    void nativeSetHidden(void* clip, bool hidden);
    void nativeRemove(void* clip); // removes and frees the container and the view
} // namespace mydear::detail

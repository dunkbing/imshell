#pragma once

// what a shell does for mydear/native_view.hpp
namespace mydear::detail {
    void* nativeHost();      // the shell's view imgui draws into (NSView* / UIView*)
    void placeNativeViews(); // after ImGui::Render(): move, clip, hide, free
    void dropNativeViews();  // App::destroy()
} // namespace mydear::detail

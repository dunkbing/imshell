#pragma once

#include "imgui.h"
#include <functional>

// a native view (NSView on macos, UIView on ios) inside the imgui layout: it reserves its
// space like any item and the shell places the view there after the frame, clipped to the
// imgui window. the view gets its own clicks and keys from the os.
//
//   auto* field = (__bridge NSTextField*)mydear::NativeView("##name", {200, 24}, [] {
//       return (__bridge_retained void*)[NSTextField textFieldWithString:@""];
//   });
//
// rules, from drawing above imgui rather than inside it:
// - it is always on top: while an imgui window, popup or tooltip overlaps it, it is hidden
// - not drawn this frame (tab closed, scrolled away): hidden; gone for a few seconds: freed
// - `create` runs once per id (in the current window's id stack) and hands over a +1
//   reference: return (__bridge_retained void*)view
namespace mydear {
    // size: like ImGui::Button (0 or negative fills the remaining width / height);
    // returns the view, or nullptr if create returned nothing
    void* NativeView(const char* id, const ImVec2& size, const std::function<void*()>& create);

#if defined(__APPLE__)
    // macos: an NSButton with an sf symbol (and an optional title before it), tinted with the
    // imgui text colour and as wide as it wants. true when clicked, natively or on the imgui
    // item under it (the test engine). not on ios
    bool NativeButton(const char* id, const char* symbol, const char* tooltip,
                      const char* title = nullptr);
#endif
} // namespace mydear

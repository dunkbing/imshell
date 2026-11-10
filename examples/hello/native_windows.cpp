// the hello example on windows: no native views yet; the probe closes through WM_CLOSE
#include "imgui.h"
#include <cstdio>
#include <windows.h>

void* makeTextField() {
    return nullptr;
}

void* makeDatePicker() {
    return nullptr;
}

void describeNativeView(void*, char* out, int size) {
    std::snprintf(out, size, "none");
}

// what the caption's close button and alt-f4 send
bool closeLikeUser() {
    PostMessageW(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), WM_CLOSE, 0, 0);
    return true;
}

// the hello example on windows: native views, and the probe closes through WM_CLOSE.
// with imshell built with IMSHELL_WINUI (cmake -DIMSHELL_WINUI=ON) they are WinUI 3 controls in a
// XAML island (the Windows 11 look, needs the Windows App Runtime installed); otherwise, or when
// the runtime is missing, a Win32 EDIT and date picker
#include "imgui.h"
#include "imshell/themes.hpp"
#include <cstdio>
#include <windows.h>
#include <commctrl.h>

#ifdef IMSHELL_WINUI
#undef GetCurrentTime
#include "imshell/winui.hpp"
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace {
    namespace mux = winrt::Microsoft::UI::Xaml;

    void* island(const mux::FrameworkElement& element) {
        element.RequestedTheme(imshell::CurrentTheme().dark ? mux::ElementTheme::Dark : mux::ElementTheme::Light);
        return imshell::WinUIIsland(winrt::get_abi(element));
    }
} // namespace
#endif

// created as popups with no parent: nativeAttach turns them into children of a clip window
void* makeTextField() {
#ifdef IMSHELL_WINUI
    if (imshell::WinUIAvailable()) {
        mux::Controls::TextBox box;
        box.Text(L"native field");
        return island(box);
    }
#endif
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"native field", WS_POPUP | WS_BORDER | ES_AUTOHSCROLL,
                           0, 0, 240, 22, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void* makeDatePicker() {
#ifdef IMSHELL_WINUI
    if (imshell::WinUIAvailable())
        return island(mux::Controls::DatePicker());
#endif
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_DATE_CLASSES};
    InitCommonControlsEx(&icc);
    return CreateWindowExW(0, DATETIMEPICK_CLASSW, nullptr, WS_POPUP | DTS_SHORTDATEFORMAT, 0, 0, 160, 22,
                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// the clip window's place in the main window (client pixels) and whether it shows
void describeNativeView(void* view, char* out, int size) {
    HWND clip = GetParent(static_cast<HWND>(view));
    RECT r = {};
    GetWindowRect(clip, &r);
    MapWindowPoints(nullptr, GetParent(clip), reinterpret_cast<POINT*>(&r), 2);
    std::snprintf(out, size, "%ld,%ld %ldx%ld %s", r.left, r.top, r.right - r.left, r.bottom - r.top,
                  IsWindowVisible(clip) ? "shown" : "hidden");
}

// imgui's coordinates are pixels: the native views' sizes for 100% scale with the window's dpi
float nativeViewScale() {
    const UINT dpi = GetDpiForWindow(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw));
    return dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

// what the caption's close button and alt-f4 send
bool closeLikeUser() {
    PostMessageW(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), WM_CLOSE, 0, 0);
    return true;
}

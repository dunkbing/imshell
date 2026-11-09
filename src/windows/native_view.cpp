// native views on windows: the app's HWND inside a clipping child window of ours
#include "../native_view.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {
    // a control notifies its parent (WM_COMMAND, WM_NOTIFY, WM_CTLCOLOR*), which is now our
    // container: send them back to the control as OCM_ messages, the reflection atl and
    // winforms use, so the app handles them by subclassing its control
    constexpr UINT kReflectBase = WM_USER + 0x1c00; // OCM__BASE

    LRESULT CALLBACK clipProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        HWND from = nullptr;
        switch (msg) {
        case WM_COMMAND:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
            from = reinterpret_cast<HWND>(lParam);
            break;
        case WM_NOTIFY:
            from = reinterpret_cast<NMHDR*>(lParam)->hwndFrom;
            break;
        }
        if (from && IsWindow(from))
            return SendMessageW(from, kReflectBase + msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    const wchar_t* clipClass() {
        static const wchar_t* name = [] {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = clipProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"MydearNativeClip";
            RegisterClassW(&wc);
            return L"MydearNativeClip";
        }();
        return name;
    }
} // namespace

namespace mydear::detail {
    void* nativeAttach(void* made) {
        HWND view = static_cast<HWND>(made);
        HWND clip = CreateWindowExW(0, clipClass(), nullptr,
                                    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, // shown when placed
                                    0, 0, static_cast<HWND>(nativeHost()), nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        LONG_PTR style = GetWindowLongPtrW(view, GWL_STYLE);
        style = (style & ~WS_POPUP) | WS_CHILD;
        SetWindowLongPtrW(view, GWL_STYLE, style);
        SetParent(view, clip);
        ShowWindow(view, SW_SHOW);
        // above the swap chain's window content; the main window clips its children out
        SetWindowPos(clip, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return clip;
    }

    void nativePlace(void* clip, void* view, const ImRect& visible, const ImRect& full) {
        SetWindowPos(static_cast<HWND>(clip), nullptr, static_cast<int>(visible.Min.x),
                     static_cast<int>(visible.Min.y), static_cast<int>(visible.GetWidth()),
                     static_cast<int>(visible.GetHeight()), SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(static_cast<HWND>(view), nullptr, static_cast<int>(full.Min.x - visible.Min.x),
                     static_cast<int>(full.Min.y - visible.Min.y),
                     static_cast<int>(full.GetWidth()), static_cast<int>(full.GetHeight()),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void nativeSetHidden(void* clip, const bool hidden) {
        ShowWindow(static_cast<HWND>(clip), hidden ? SW_HIDE : SW_SHOWNA);
    }

    void nativeRemove(void* clip) {
        DestroyWindow(static_cast<HWND>(clip)); // and the view, its child
    }
} // namespace mydear::detail

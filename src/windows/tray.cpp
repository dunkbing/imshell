// the notification area icon: Shell_NotifyIcon with a message-only window of its own for the
// callbacks. a left click is onActivate (or the menu when there is none), a right click the menu
#include "imshell/tray.hpp"

#include "imshell/image.hpp"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace imshell::tray {
    namespace {
        constexpr UINT kCallback = WM_APP + 1;
        constexpr UINT kFirstCommand = 1000;
        HWND gHwnd = nullptr;
        NOTIFYICONDATAW gData = {};
        bool gAdded = false;
        UINT gTaskbarCreated = 0; // explorer restarted: the icon has to be added again
        HICON gIcon = nullptr;
        Options gOptions;

        std::wstring wide(const std::string& s) {
            if (s.empty())
                return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        // the png as an icon: a 32 bit colour bitmap carrying the alpha, and an empty mask
        HICON makeIcon(const Options::Icon& icon) {
            if (!icon.png)
                return nullptr;
            std::vector<unsigned char> rgba;
            int w = 0, h = 0;
            LoadTexture(icon.png, icon.size, [&](const uint8_t* p, int pw, int ph) {
                rgba.assign(p, p + static_cast<size_t>(pw) * ph * 4);
                w = pw;
                h = ph;
                return ImTextureID{};
            });
            if (w <= 0 || h <= 0)
                return nullptr;
            BITMAPV5HEADER header = {};
            header.bV5Size = sizeof(header);
            header.bV5Width = w;
            header.bV5Height = -h; // top down
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;
            void* bits = nullptr;
            HDC dc = GetDC(nullptr);
            HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
            ReleaseDC(nullptr, dc);
            if (!color || !bits)
                return nullptr;
            auto* out = static_cast<unsigned char*>(bits);
            for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) { // rgba to bgra
                out[i * 4 + 0] = rgba[i * 4 + 2];
                out[i * 4 + 1] = rgba[i * 4 + 1];
                out[i * 4 + 2] = rgba[i * 4 + 0];
                out[i * 4 + 3] = rgba[i * 4 + 3];
            }
            HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
            ICONINFO info = {};
            info.fIcon = TRUE;
            info.hbmColor = color;
            info.hbmMask = mask;
            HICON result = CreateIconIndirect(&info);
            DeleteObject(color);
            DeleteObject(mask);
            return result;
        }

        void showMenu() {
            HMENU menu = CreatePopupMenu();
            for (size_t i = 0; i < gOptions.items.size(); ++i) {
                const Item& it = gOptions.items[i];
                if (it.separator) {
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    continue;
                }
                AppendMenuW(menu, MF_STRING | (it.enabled ? MF_ENABLED : MF_GRAYED),
                            kFirstCommand + static_cast<UINT>(i), wide(it.label).c_str());
            }
            POINT at;
            GetCursorPos(&at);
            SetForegroundWindow(gHwnd); // or the menu stays open when it loses focus
            const UINT command = static_cast<UINT>(TrackPopupMenu(
                menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, at.x, at.y, 0, gHwnd, nullptr));
            PostMessageW(gHwnd, WM_NULL, 0, 0);
            DestroyMenu(menu);
            if (command >= kFirstCommand && command - kFirstCommand < gOptions.items.size()) {
                auto action = gOptions.items[command - kFirstCommand].onClick; // it may call Set()
                if (action)
                    action();
            }
        }

        LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            if (msg == kCallback) {
                switch (LOWORD(lParam)) {
                case WM_LBUTTONUP:
                    if (gOptions.onActivate) {
                        auto activate = gOptions.onActivate;
                        activate();
                    } else {
                        showMenu();
                    }
                    break;
                case WM_RBUTTONUP:
                    showMenu();
                    break;
                }
                return 0;
            }
            if (msg == gTaskbarCreated && gTaskbarCreated && gAdded)
                Shell_NotifyIconW(NIM_ADD, &gData);
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    } // namespace

    void Set(const Options& options) {
        gOptions = options;
        if (!gHwnd) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"ImShellTray";
            RegisterClassW(&wc);
            gHwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
            gTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
            if (!gHwnd)
                return;
        }
        HICON icon = makeIcon(options.icon);
        gData = {};
        gData.cbSize = sizeof(gData);
        gData.hWnd = gHwnd;
        gData.uID = 1;
        gData.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
        gData.uCallbackMessage = kCallback;
        gData.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
        const std::wstring tip = wide(options.tooltip);
        wcsncpy_s(gData.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(gAdded ? NIM_MODIFY : NIM_ADD, &gData);
        gAdded = true;
        if (gIcon)
            DestroyIcon(gIcon);
        gIcon = icon;
    }

    void Remove() {
        if (gAdded)
            Shell_NotifyIconW(NIM_DELETE, &gData);
        gAdded = false;
        if (gIcon)
            DestroyIcon(gIcon);
        gIcon = nullptr;
        if (gHwnd)
            DestroyWindow(gHwnd);
        gHwnd = nullptr;
        gOptions = {};
    }
} // namespace imshell::tray

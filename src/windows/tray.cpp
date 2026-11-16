// the notification area icon: Shell_NotifyIcon (version 4 callbacks) with a message-only window of
// its own. a left click is onActivate (or the menu when there is none), a right click the menu
#include "imshell/tray.hpp"

#include "imshell/image.hpp"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace imshell::tray {
    namespace {
        constexpr UINT kCallback = WM_APP + 1;
        constexpr UINT kFirstCommand = 1000;
        HWND gHwnd = nullptr;
        NOTIFYICONDATAW gData = {};
        bool gAdded = false;  // the shell has the icon
        bool gWanted = false; // Set() was called: add it again if explorer restarts, or starts late
        ULONGLONG gLastActivate = 0;
        UINT gTaskbarCreated = 0;
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

        // the notification area's icon size at the system dpi
        int iconSize() {
            const UINT dpi = GetDpiForSystem();
            const int s = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
            return s > 0 ? s : 16;
        }

        // the png as an icon of the tray's size (a box filter, so a large png does not alias): a
        // 32 bit colour bitmap carrying the alpha, and an empty mask
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
            const int s = iconSize();
            BITMAPV5HEADER header = {};
            header.bV5Size = sizeof(header);
            header.bV5Width = s;
            header.bV5Height = -s; // top down
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
            for (int y = 0; y < s; ++y) {
                const int y0 = y * h / s, y1 = std::max(y0 + 1, (y + 1) * h / s);
                for (int x = 0; x < s; ++x) {
                    const int x0 = x * w / s, x1 = std::max(x0 + 1, (x + 1) * w / s);
                    unsigned long long r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int sy = y0; sy < y1; ++sy)
                        for (int sx = x0; sx < x1; ++sx) {
                            const unsigned char* p = &rgba[(static_cast<size_t>(sy) * w + sx) * 4];
                            r += p[0] * p[3]; // weighted by alpha, so a clear pixel's colour is ignored
                            g += p[1] * p[3];
                            b += p[2] * p[3];
                            a += p[3];
                            ++n;
                        }
                    unsigned char* o = out + (static_cast<size_t>(y) * s + x) * 4;
                    o[0] = a ? static_cast<unsigned char>(b / a) : 0; // bgra
                    o[1] = a ? static_cast<unsigned char>(g / a) : 0;
                    o[2] = a ? static_cast<unsigned char>(r / a) : 0;
                    o[3] = static_cast<unsigned char>(a / n);
                }
            }
            HBITMAP mask = CreateBitmap(s, s, 1, 1, nullptr);
            ICONINFO info = {};
            info.fIcon = TRUE;
            info.hbmColor = color;
            info.hbmMask = mask;
            HICON result = CreateIconIndirect(&info);
            DeleteObject(color);
            DeleteObject(mask);
            return result;
        }

        // the shell has to be told the callback version after every add
        bool add() {
            if (!Shell_NotifyIconW(NIM_ADD, &gData))
                return false;
            gData.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &gData);
            return true;
        }

        void showMenu(POINT at) {
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
            SetForegroundWindow(gHwnd); // or the menu stays open when it loses focus
            const UINT command = static_cast<UINT>(TrackPopupMenu(
                menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD | TPM_NONOTIFY, at.x, at.y, 0, gHwnd, nullptr));
            PostMessageW(gHwnd, WM_NULL, 0, 0); // so the next click on the menu works
            DestroyMenu(menu);
            if (command >= kFirstCommand && command - kFirstCommand < gOptions.items.size()) {
                auto action = gOptions.items[command - kFirstCommand].onClick; // it may call Set()
                if (action)
                    action();
            }
        }

        LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            if (msg == kCallback) {
                // version 4: the event is in the low word, the click's screen position in wParam
                POINT at = {GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
                switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT: {
                    // enter on the icon sends both: one action
                    const ULONGLONG now = GetTickCount64();
                    if (now - gLastActivate < 300)
                        break;
                    gLastActivate = now;
                    if (gOptions.onActivate) {
                        auto activate = gOptions.onActivate; // a copy: it may call Set()
                        activate();
                    } else {
                        showMenu(at);
                    }
                    break;
                }
                case WM_CONTEXTMENU:
                    showMenu(at);
                    break;
                }
                return 0;
            }
            // explorer restarted (or started after us): its notification area is empty
            if (gTaskbarCreated && msg == gTaskbarCreated && gWanted)
                gAdded = add();
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    } // namespace

    bool Available() {
        return gAdded;
    }

    void Set(const Options& options) {
        gOptions = options;
        gWanted = true;
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
        const UINT version = gData.uVersion;
        gData = {};
        gData.cbSize = sizeof(gData);
        gData.hWnd = gHwnd;
        gData.uID = 1;
        gData.uVersion = version;
        gData.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON | NIF_SHOWTIP; // version 4 hides the tip without it
        gData.uCallbackMessage = kCallback;
        gData.hIcon = icon ? icon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION, wide even without UNICODE
        const std::wstring tip = wide(options.tooltip);
        wcsncpy_s(gData.szTip, tip.c_str(), _TRUNCATE);
        if (!gAdded || !Shell_NotifyIconW(NIM_MODIFY, &gData))
            gAdded = add(); // false until explorer has a notification area
        if (gIcon)
            DestroyIcon(gIcon);
        gIcon = icon;
    }

    void Remove() {
        if (gAdded)
            Shell_NotifyIconW(NIM_DELETE, &gData);
        gAdded = false;
        gWanted = false;
        gData = {};
        if (gIcon)
            DestroyIcon(gIcon);
        gIcon = nullptr;
        if (gHwnd)
            DestroyWindow(gHwnd);
        gHwnd = nullptr;
        gOptions = {};
    }
} // namespace imshell::tray

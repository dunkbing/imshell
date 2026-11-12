// toast notifications (c++/winrt, from the windows sdk) and a count as the taskbar button's
// overlay icon. an app with no installer has no identity, so the first call registers one under
// HKCU\Software\Classes\AppUserModelId\imshell.<exe name>, shown as the exe's name. winrt and
// the taskbar run on the ui thread: calls queue and the loop runs them (runLoopPass), with the
// clicks the toasts report from their own threads. a click reaches only a running app
#include "../loop_pass.hpp"
#include "../native_view.hpp"
#include "imshell/frames.hpp"
#include "imshell/notifications.hpp"

#include <windows.h>
#include <shobjidl.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>

#include <filesystem>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

using namespace imshell::notifications;
namespace toast = winrt::Windows::UI::Notifications;
namespace xml = winrt::Windows::Data::Xml::Dom;

namespace {
    std::mutex gMutex;
    std::vector<std::function<void()>> gJobs; // from any thread
    std::vector<std::string> gClicks;         // from the toasts' threads
    std::function<void(const std::string&)> gOnPress;
    std::vector<std::string> gPending;              // clicks before OnPress
    std::map<std::string, toast::ToastNotification> gShown; // kept for their Activated events
    constexpr const wchar_t* kGroup = L"imshell";

    std::wstring wide(const std::string& s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
        return out;
    }

    // a tag is at most 64 characters: a longer id goes by its hash
    std::wstring tagOf(const std::string& id) {
        return id.size() <= 64 ? wide(id) : std::to_wstring(std::hash<std::string>{}(id));
    }

    // registered once: the name toasts show, and the id they are filed under
    const std::wstring& appId() {
        static const std::wstring id = [] {
            wchar_t path[MAX_PATH] = L"";
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            const std::wstring name = std::filesystem::path(path).stem().wstring();
            const std::wstring aumid = L"imshell." + name;
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\AppUserModelId\\" + aumid).c_str(), 0, nullptr, 0,
                                KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
                RegSetValueExW(key, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(name.c_str()),
                               static_cast<DWORD>((name.size() + 1) * sizeof(wchar_t)));
                RegCloseKey(key);
            }
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // the ui thread's; already set is fine
            return aumid;
        }();
        return id;
    }

    toast::ToastNotifier notifier() {
        return toast::ToastNotificationManager::CreateToastNotifier(appId());
    }

    Permission permission() {
        try {
            return notifier().Setting() == toast::NotificationSetting::Enabled ? Permission::Granted
                                                                               : Permission::Denied;
        } catch (const winrt::hresult_error&) {
            return Permission::Unavailable;
        }
    }

    void pass() {
        std::vector<std::function<void()>> jobs;
        std::vector<std::string> clicks;
        {
            std::lock_guard lock(gMutex);
            jobs.swap(gJobs);
            clicks.swap(gClicks);
        }
        for (auto& job : jobs)
            job();
        for (const std::string& id : clicks) {
            gShown.erase(id);
            if (gOnPress)
                gOnPress(id);
            else
                gPending.push_back(id);
        }
    }

    void later(std::function<void()> job) {
        imshell::detail::addLoopPass(pass);
        {
            std::lock_guard lock(gMutex);
            gJobs.push_back(std::move(job));
        }
        imshell::requestFrames(); // wakes the loop
    }

    void show(const Notification& n, const std::function<void(bool)>& done) {
        bool shown = false;
        try {
            xml::XmlDocument doc;
            doc.LoadXml(L"<toast><visual><binding template='ToastGeneric'><text/><text/></binding></visual></toast>");
            const auto texts = doc.GetElementsByTagName(L"text");
            texts.Item(0).AppendChild(doc.CreateTextNode(wide(n.title)));
            texts.Item(1).AppendChild(doc.CreateTextNode(wide(n.body)));
            if (n.silent) {
                xml::XmlElement audio = doc.CreateElement(L"audio");
                audio.SetAttribute(L"silent", L"true");
                doc.DocumentElement().AppendChild(audio);
            }
            toast::ToastNotification t(doc);
            t.Tag(tagOf(n.id)); // the same tag and group replace it
            t.Group(kGroup);
            t.Activated([id = n.id](const auto&, const auto&) {
                {
                    std::lock_guard lock(gMutex);
                    gClicks.push_back(id);
                }
                imshell::requestFrames();
            });
            toast::ToastNotifier to = notifier();
            shown = to.Setting() == toast::NotificationSetting::Enabled;
            if (shown) {
                to.Show(t);
                gShown.insert_or_assign(n.id, t);
            }
        } catch (const winrt::hresult_error&) {
            shown = false;
        }
        if (done)
            done(shown);
    }

    // a red disc with the count, for the taskbar button
    HICON badgeIcon(int count) {
        constexpr int size = 16;
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(dc, color);
        HBRUSH red = CreateSolidBrush(RGB(220, 38, 38));
        SelectObject(dc, red);
        SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, 0, 0, size + 1, size + 1);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        HFONT font = CreateFontW(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SelectObject(dc, font);
        RECT r{0, 0, size, size};
        const std::wstring text = count > 9 ? L"9+" : std::to_wstring(count);
        DrawTextW(dc, text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        // gdi leaves alpha at 0: opaque inside the disc, clear outside it
        auto* px = static_cast<DWORD*>(bits);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float dx = x + 0.5f - size / 2.0f, dy = y + 0.5f - size / 2.0f;
                px[y * size + x] = dx * dx + dy * dy <= size * size / 4.0f ? (px[y * size + x] | 0xff000000) : 0;
            }
        SelectObject(dc, old);
        HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
        ICONINFO ii{TRUE, 0, 0, mask, color};
        HICON icon = CreateIconIndirect(&ii);
        DeleteObject(mask);
        DeleteObject(color);
        DeleteObject(red);
        DeleteObject(font);
        DeleteDC(dc);
        return icon;
    }

    void setBadge(int count) {
        HWND window = GetAncestor(static_cast<HWND>(imshell::detail::nativeHost()), GA_ROOT);
        ITaskbarList3* taskbar = nullptr;
        if (!window || FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar))))
            return;
        HICON icon = count > 0 ? badgeIcon(count) : nullptr;
        taskbar->SetOverlayIcon(window, icon, count > 0 ? std::to_wstring(count).c_str() : L"");
        if (icon)
            DestroyIcon(icon);
        taskbar->Release();
    }
} // namespace

namespace imshell::notifications {
    // no prompt on windows: granted unless the user turned the app's notifications off
    void GetPermission(std::function<void(Permission)> done) {
        later([done = std::move(done)] {
            if (done)
                done(permission());
        });
    }

    void RequestPermission(std::function<void(Permission)> done) {
        GetPermission(std::move(done));
    }

    void Show(const Notification& notification, std::function<void(bool)> done) {
        later([n = notification, done = std::move(done)] { show(n, done); });
    }

    void Close(const std::string& id) {
        later([id] {
            gShown.erase(id);
            try {
                toast::ToastNotificationManager::History().Remove(tagOf(id), kGroup, appId());
            } catch (const winrt::hresult_error&) {
            }
        });
    }

    void SetBadge(int count) {
        later([count] { setBadge(count); });
    }

    void OnPress(std::function<void(const std::string&)> handler) {
        imshell::detail::addLoopPass(pass);
        gOnPress = std::move(handler);
        for (const std::string& id : std::exchange(gPending, {}))
            if (gOnPress)
                gOnPress(id);
    }
} // namespace imshell::notifications

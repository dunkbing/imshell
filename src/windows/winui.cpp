// WinUI 3 on windows: start-up of the Windows App Runtime for an unpackaged exe, and windows that
// host a XAML island for NativeView. see imshell/winui.hpp
#include "imshell/winui.hpp"

#include "../native_view.hpp"

#include <windows.h>
#undef GetCurrentTime
#include <unknwn.h>
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <cstdio>

namespace {
    namespace mux = winrt::Microsoft::UI::Xaml;

    // the application object: WinUI builds its controls by type name, so it needs the metadata
    // provider of the controls library or their styles do not load
    struct App : mux::ApplicationT<App, mux::Markup::IXamlMetadataProvider> {
        mux::XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
        mux::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type) { return provider.GetXamlType(type); }
        mux::Markup::IXamlType GetXamlType(winrt::hstring const& name) { return provider.GetXamlType(name); }
        winrt::com_array<mux::Markup::XmlnsDefinition> GetXmlnsDefinitions() { return provider.GetXmlnsDefinitions(); }
    };

    // a window whose client area is the island. the island is made on the first size change after
    // NativeView moved the window into its clip window: an island started before that loses its
    // site bridge when the window is reparented (and never lays out)
    struct Island {
        mux::UIElement content{nullptr};
        mux::Hosting::DesktopWindowXamlSource source{nullptr};
    };

    constexpr const wchar_t* kProp = L"ImShellIsland"; // not GWLP_USERDATA: WinUI uses that on its host

    LRESULT CALLBACK islandProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* island = static_cast<Island*>(GetPropW(hwnd, kProp));
        if (island && msg == WM_SIZE) {
            const int w = LOWORD(lParam), h = HIWORD(lParam);
            if (!island->source && w > 0 && h > 0 && GetParent(hwnd) != static_cast<HWND>(imshell::detail::nativeHost())) {
                island->source = mux::Hosting::DesktopWindowXamlSource();
                island->source.Initialize(winrt::Microsoft::UI::WindowId{reinterpret_cast<uint64_t>(hwnd)});
                island->source.Content(island->content);
                island->source.SiteBridge().Show();
            }
            if (island->source) {
                if (auto bridge = island->source.SiteBridge())
                    bridge.MoveAndResize({0, 0, w, h});
            }
        } else if (msg == WM_DESTROY) {
            if (island && island->source) { // the docs want the source closed before its window goes
                island->source.Close();
                island->source = nullptr;
            }
        } else if (msg == WM_NCDESTROY) {
            delete island;
            RemovePropW(hwnd, kProp);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
} // namespace

namespace imshell {
    bool WinUIAvailable() {
        static const bool ready = [] {
            // the bootstrap dll is delay loaded: without it beside the executable there is no WinUI,
            // and a call into it would raise an exception
            if (!LoadLibraryExW(L"Microsoft.WindowsAppRuntime.Bootstrap.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32))
                return false;
            if (FAILED(MddBootstrapInitialize2(WINDOWSAPPSDK_RELEASE_MAJORMINOR, WINDOWSAPPSDK_RELEASE_VERSION_SHORTTAG_W, {},
                                               MddBootstrapInitializeOptions_None)))
                return false;
            try {
                try {
                    winrt::init_apartment(winrt::apartment_type::single_threaded);
                } catch (const winrt::hresult_error&) { // the shell already initialised COM
                }
                // kept for the life of the process
                static auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
                static auto app = winrt::make<App>();
                static auto manager = mux::Hosting::WindowsXamlManager::InitializeForCurrentThread();
                app.Resources().MergedDictionaries().Append(mux::Controls::XamlControlsResources());
                return true;
            } catch (const winrt::hresult_error& e) {
                std::fprintf(stderr, "imshell: WinUI did not start: %ls\n", e.message().c_str());
                return false;
            }
        }();
        return ready;
    }

    void* WinUIIsland(void* uiElementAbi) {
        HWND parent = static_cast<HWND>(detail::nativeHost());
        if (!uiElementAbi || !parent || !WinUIAvailable())
            return nullptr;
        static const wchar_t* cls = [] {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = islandProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"ImShellIsland";
            RegisterClassW(&wc);
            return L"ImShellIsland";
        }();
        // a child window needs a parent to be made; nativeAttach moves it, and the first WM_SIZE
        // after that starts the island. 1x1 so that placing it to its real size sends WM_SIZE
        HWND host = CreateWindowExW(0, cls, nullptr, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!host)
            return nullptr;
        auto* island = new Island;
        winrt::copy_from_abi(island->content, uiElementAbi);
        SetPropW(host, kProp, island);
        return host;
    }
} // namespace imshell

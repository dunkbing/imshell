#pragma once

// WinUI 3 on windows, for a host that draws native views with it (docs/getting-started.md). only
// there when imshell is configured with -DIMSHELL_WINUI=ON; imshell's own native controls
// (native_controls.hpp) use it too. nothing here exposes C++/WinRT: a host that wants its own
// elements includes the projection headers itself (cmake/ImShellWinUI.cmake sets them up)
namespace imshell {
    // true once WinUI runs on the ui thread. the first call starts it: the Windows App Runtime
    // bootstrap, a dispatcher queue, and the application object whose metadata provider makes the
    // control styles load. false when the runtime of the version imshell was built with is not
    // installed: use something else then
    bool WinUIAvailable();

    // a window for NativeView that shows `element`: pass the abi pointer of a
    // winrt::Microsoft::UI::Xaml::UIElement (winrt::get_abi). the island starts on the window's
    // first size change after NativeView moved it into place, which is when it can keep its
    // bridge. nullptr when WinUI is not available
    void* WinUIIsland(void* uiElementAbi);
} // namespace imshell

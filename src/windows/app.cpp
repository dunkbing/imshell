#include "../activity.hpp"
#include "imshell/app.hpp"
#include "../glfw_loop.hpp"
#include "../native_view.hpp"
#include "dx11_backend.hpp"
#include "imgui_impl_glfw.h"
#include "imgui_internal.h"
#include "imshell/themes.hpp"
#include "imshell/title_bar.hpp"

#include <cstdio>
#include <utility>
#include <dwmapi.h>
#include <windowsx.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef SM_CXPADDEDBORDER
#define SM_CXPADDEDBORDER 92 // vista+, hidden by older _WIN32_WINNT
#endif

// imgui coordinates are client pixels here: glfw on windows has no points
namespace imshell::detail {
    bool imeMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result); // text_input.cpp
    void installTextInput();
    void textInputFrame();
} // namespace imshell::detail

namespace imshell {
    namespace {
        Delegate* gDelegate = nullptr; // glfw and window procedure callbacks have no user pointer
        HWND gHwnd = nullptr;
        WNDPROC gGlfwProc = nullptr;
        DX11Backend* gBackend = nullptr; // the swap chain follows WM_SIZE
        bool gTitleBar = false;
        float gBarHeight = 32;
        bool gMaxHovered = false; // the maximize button is non-client: hover comes from WM_NCMOUSEMOVE
        int gDark = -1;           // DWMWA_USE_IMMERSIVE_DARK_MODE as last set
        enum class BarAction { None, Drag, DoubleClick, Menu } gBarAction = BarAction::None;
        ImGuiWindow* gFocusBeforeBar = nullptr; // a click on the bar hands focus back to it
        constexpr float kCaptionW = 46;         // one caption button, as windows draws them

        int resizeBorder() {
            return GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        }

        void extendFrame() {
            const MARGINS margins = {0, 0, static_cast<int>(gBarHeight), 0}; // keeps the shadow
            DwmExtendFrameIntoClientArea(gHwnd, &margins);
        }

        // resize edges, and the maximize button as HTMAXBUTTON so windows 11 shows snap
        // layouts over it. the rest of the bar is client area: imgui decides what a click hit
        // and EndTitleBar starts the drag when it hit nothing
        LRESULT hitTest(HWND hwnd, LPARAM lParam) {
            const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc;
            GetWindowRect(hwnd, &rc);
            const int b = resizeBorder();
            if (!IsZoomed(hwnd)) {
                const bool left = pt.x < rc.left + b, right = pt.x >= rc.right - b;
                if (pt.y < rc.top + b)
                    return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
                if (pt.y >= rc.bottom - b)
                    return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
                if (left)
                    return HTLEFT;
                if (right)
                    return HTRIGHT;
            }
            if (pt.y < rc.top + static_cast<int>(gBarHeight) &&
                pt.x >= rc.right - static_cast<int>(2 * kCaptionW) &&
                pt.x < rc.right - static_cast<int>(kCaptionW))
                return HTMAXBUTTON;
            return HTCLIENT;
        }

        LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            if (LRESULT result = 0; detail::imeMessage(hwnd, msg, wParam, lParam, result))
                return result;
            switch (msg) {
            case WM_SIZE:
                if (wParam != SIZE_MINIMIZED && gBackend)
                    gBackend->resize();
                break; // glfw needs it too
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
                if (GetFocus() != hwnd)
                    SetFocus(hwnd); // a click on imgui takes the keyboard back from a native view
                break;
            }
            if (!gTitleBar)
                return CallWindowProcW(gGlfwProc, hwnd, msg, wParam, lParam);

            LRESULT dwm = 0;
            if (DwmDefWindowProc(hwnd, msg, wParam, lParam, &dwm))
                return dwm;
            switch (msg) {
            case WM_ACTIVATE:
                extendFrame();
                break;
            case WM_NCCALCSIZE:
                if (wParam == TRUE) {
                    // no standard frame. maximized, windows grows the window past the screen
                    // by the frame thickness: keep it to the work area instead
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    if (IsZoomed(hwnd)) {
                        MONITORINFO mi = {sizeof(mi)};
                        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                            params->rgrc[0] = mi.rcWork;
                    }
                    return 0;
                }
                break;
            case WM_NCHITTEST:
                return hitTest(hwnd, lParam);
            case WM_NCMOUSEMOVE:
                gMaxHovered = wParam == HTMAXBUTTON;
                requestFrames();
                break; // the default procedure needs it for snap layouts
            case WM_NCMOUSELEAVE:
            case WM_MOUSEMOVE:
                gMaxHovered = false;
                break;
            case WM_GETMINMAXINFO: {
                // maximized: the work area, not over the taskbar
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                MONITORINFO mi = {sizeof(mi)};
                if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
                    mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                    mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                }
                return 0;
            }
            }
            return CallWindowProcW(gGlfwProc, hwnd, msg, wParam, lParam);
        }

        // dark title bar, borders and snap previews when the theme is dark
        void followTheme() {
            const int dark = theme().dark ? 1 : 0;
            if (dark == gDark || !gHwnd)
                return;
            gDark = dark;
            const BOOL value = dark;
            DwmSetWindowAttribute(gHwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
        }

        bool primaryButtonDown() {
            const int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

        // after the frame: a click in empty bar space, now that imgui knows it hit no widget
        void runBarAction() {
            const BarAction action = gBarAction;
            gBarAction = BarAction::None;
            if (action == BarAction::Drag) {
                if (!primaryButtonDown()) // a click shorter than a frame is over already
                    return;
                POINT pt;
                GetCursorPos(&pt);
                ReleaseCapture();
                // the system move loop: aero snap, shake, drag to another screen
                SendMessageW(gHwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
                ImGui::GetIO().AddMouseButtonEvent(0, false); // the loop ate the mouse up
            } else if (action == BarAction::DoubleClick) {
                ShowWindow(gHwnd, IsZoomed(gHwnd) ? SW_RESTORE : SW_MAXIMIZE);
            } else if (action == BarAction::Menu) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU menu = GetSystemMenu(gHwnd, FALSE);
                const BOOL zoomed = IsZoomed(gHwnd);
                EnableMenuItem(menu, SC_RESTORE, zoomed ? MF_ENABLED : MF_GRAYED);
                EnableMenuItem(menu, SC_MAXIMIZE, zoomed ? MF_GRAYED : MF_ENABLED);
                EnableMenuItem(menu, SC_MOVE, zoomed ? MF_GRAYED : MF_ENABLED);
                EnableMenuItem(menu, SC_SIZE, zoomed ? MF_GRAYED : MF_ENABLED);
                const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y,
                                               0, gHwnd, nullptr);
                if (cmd)
                    PostMessageW(gHwnd, WM_SYSCOMMAND, cmd, 0);
            }
        }

        // the caption buttons, drawn like windows' own (segoe-style glyphs as lines)
        void captionButtons(const TitleBar& bar) {
            const Theme& c = theme();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool dark = theme().dark;
            const ImU32 hoverBg = dark ? IM_COL32(255, 255, 255, 26) : IM_COL32(0, 0, 0, 15);
            const ImU32 activeBg = dark ? IM_COL32(255, 255, 255, 38) : IM_COL32(0, 0, 0, 26);
            const ImU32 text = ImGui::GetColorU32(c.foreground);
            float x = bar.pos.x + bar.size.x - 3 * kCaptionW;
            const auto box = [&](float at) {
                return ImRect(ImVec2(at, bar.pos.y), ImVec2(at + kCaptionW, bar.pos.y + bar.size.y));
            };
            const auto button = [&](const char* id, ImRect r, ImU32 hover, ImU32 active) {
                ImGui::SetCursorScreenPos(r.Min);
                const bool clicked = ImGui::InvisibleButton(id, r.GetSize());
                if (ImGui::IsItemActive())
                    dl->AddRectFilled(r.Min, r.Max, active);
                else if (ImGui::IsItemHovered())
                    dl->AddRectFilled(r.Min, r.Max, hover);
                return clicked;
            };

            ImRect r = box(x);
            if (button("##imshell_minimize", r, hoverBg, activeBg))
                ShowWindow(gHwnd, SW_MINIMIZE);
            ImVec2 m = r.GetCenter();
            dl->AddLine({m.x - 5, m.y}, {m.x + 5, m.y}, text);

            // maximize: non-client (HTMAXBUTTON), so windows clicks it and imgui only draws it
            x += kCaptionW;
            r = box(x);
            if (gMaxHovered)
                dl->AddRectFilled(r.Min, r.Max, hoverBg);
            m = r.GetCenter();
            if (IsZoomed(gHwnd)) {
                dl->AddRect({m.x - 2, m.y - 6}, {m.x + 6, m.y + 2}, text);
                dl->AddRectFilled({m.x - 4, m.y - 4}, {m.x + 4, m.y + 4}, ImGui::GetColorU32(c.background));
                dl->AddRect({m.x - 4, m.y - 4}, {m.x + 4, m.y + 4}, text);
            } else {
                dl->AddRect({m.x - 5, m.y - 5}, {m.x + 5, m.y + 5}, text);
            }

            x += kCaptionW;
            r = box(x);
            const bool closeHot = ImGui::IsMouseHoveringRect(r.Min, r.Max);
            if (button("##imshell_close", r, IM_COL32(196, 43, 28, 255), IM_COL32(170, 36, 23, 255)))
                PostMessageW(gHwnd, WM_CLOSE, 0, 0);
            m = r.GetCenter();
            const ImU32 cross = closeHot ? IM_COL32_WHITE : text;
            dl->AddLine({m.x - 5, m.y - 5}, {m.x + 5, m.y + 5}, cross);
            dl->AddLine({m.x + 5, m.y - 5}, {m.x - 5, m.y + 5}, cross);
        }
    } // namespace

    struct App::Impl {
        GLFWwindow* window = nullptr;
        std::unique_ptr<DX11Backend> backend;
        Delegate* delegate = nullptr;
        bool vsync = true;
        int maxFps = 120;
    };

    App::App() : impl_(std::make_unique<Impl>()) {}

    App::~App() {
        destroy();
    }

    bool App::create(const Config& config, Delegate& delegate) {
        auto& d = *impl_;
        d.delegate = &delegate;
        gDelegate = &delegate;
        d.vsync = config.vsync;
        d.maxFps = config.maxFps;

        if (!glfwInit()) {
            std::fprintf(stderr, "imshell: glfwInit failed\n");
            return false;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // direct3d draws, not gl
        d.window =
            glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
        if (!d.window) {
            std::fprintf(stderr, "imshell: could not create the window\n");
            glfwTerminate();
            return false;
        }
        gHwnd = glfwGetWin32Window(d.window);
        gTitleBar = config.titleBar;
        gBarHeight = config.titleBarHeight > 0 ? config.titleBarHeight : 32;

        d.backend = std::make_unique<DX11Backend>(gHwnd);
        if (!d.backend->ok())
            return false;
        d.backend->vsync = d.vsync;
        gBackend = d.backend.get();

        gGlfwProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(gHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(windowProc)));
        if (gTitleBar) {
            extendFrame();
            // a WM_NCCALCSIZE now, so the frame goes before the first paint
            SetWindowPos(gHwnd, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        glfwSetDropCallback(d.window, [](GLFWwindow* w, const int count, const char** paths) {
            for (int i = 0; i < count; i++)
                if (gDelegate)
                    gDelegate->openFile(paths[i]);
            glfwFocusWindow(w);
        });

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        detail::useIniFile(config);
        if (config.docking)
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();
        ApplyTheme();
        delegate.setup();

        ImGui_ImplGlfw_InitForOther(d.window, true);

        detail::installTextInput();
        if (!d.backend->initializeImGui()) {
            std::fprintf(stderr, "imshell: the imgui dx11 backend failed\n");
            return false;
        }
        followTheme();
        return true;
    }

    bool App::pollEvents() {
        glfwPollEvents();
        return !glfwWindowShouldClose(impl_->window);
    }

    void App::renderFrame() {
        auto& d = *impl_;
        followTheme();
        d.backend->beginFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        d.delegate->frame();
        detail::textInputFrame();
        ImGui::Render();
        // an unchanged frame is not drawn or presented: the window keeps showing the last one
        const ImVec4 base = theme().background;
        const bool mustDraw = std::exchange(d.backend->mustDraw, false);
        detail::frameSkipped() = detail::sameAsLastFrame(ImGui::GetDrawData(), base) && !mustDraw;
        if (!detail::frameSkipped()) {
            d.backend->renderDrawData(ImGui::GetDrawData(), base);
            d.backend->present();
        }
        detail::placeNativeViews();
        runBarAction();
    }

    void App::run() {
        auto& d = *impl_;
        detail::runGlfwLoop(d.window, *d.delegate, d.vsync, d.maxFps, [this] { renderFrame(); });
    }

    void App::destroy() {
        auto& d = *impl_;
        if (!d.window)
            return;
        // create() may have failed part way: tear down only what it set up
        if (ImGui::GetCurrentContext()) {
            detail::dropNativeViews();
            if (d.backend && ImGui::GetIO().BackendRendererUserData)
                d.backend->shutdown();
            if (ImGui::GetIO().BackendPlatformUserData)
                ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        if (gGlfwProc)
            SetWindowLongPtrW(gHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gGlfwProc));
        gBackend = nullptr;
        d.backend.reset();
        glfwDestroyWindow(d.window);
        glfwTerminate();
        d.window = nullptr;
        gHwnd = nullptr;
        gGlfwProc = nullptr;
        gDelegate = nullptr;
    }

    float TitleBarHeight() {
        return gTitleBar ? gBarHeight : 0;
    }

    TitleBar BeginTitleBar() {
        if (ImGuiWindow* nav = GImGui->NavWindow;
            nav && nav != ImGui::FindWindowByName("##imshell_title_bar"))
            gFocusBeforeBar = nav;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        TitleBar bar;
        bar.pos = vp->Pos;
        bar.size = ImVec2(vp->Size.x, TitleBarHeight());
        bar.leftInset = Theme::Spacing::M;
        bar.rightInset = 3 * kCaptionW;

        ImGui::SetNextWindowPos(bar.pos);
        ImGui::SetNextWindowSize(bar.size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
        ImGui::Begin("##imshell_title_bar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar(4);
        if (gHwnd)
            captionButtons(bar);
        ImGui::SetCursorScreenPos(ImVec2(
            bar.pos.x + bar.leftInset, bar.pos.y + (bar.size.y - ImGui::GetFrameHeight()) * 0.5f));
        return bar;
    }

    void EndTitleBar() {
        if (GImGui->NavWindow == ImGui::GetCurrentWindow() && !ImGui::IsAnyItemActive() &&
            gFocusBeforeBar && gFocusBeforeBar->WasActive)
            ImGui::FocusWindow(gFocusBeforeBar);
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
            if (ImGui::IsMouseDoubleClicked(0))
                gBarAction = BarAction::DoubleClick;
            else if (ImGui::IsMouseClicked(0))
                gBarAction = BarAction::Drag;
            else if (ImGui::IsMouseReleased(1))
                gBarAction = BarAction::Menu;
        }
        ImGui::Dummy(ImVec2(0, 0)); // an empty bar: imgui wants an item after the cursor is set
        ImGui::End();
    }

    void App::setVSync(const bool on) {
        impl_->vsync = on;
        if (impl_->backend)
            impl_->backend->vsync = on;
    }

    void App::setMaxFps(const int fps) {
        impl_->maxFps = std::max(0, fps);
    }

    ImTextureID App::createTexture(const uint8_t* rgba, const int width, const int height) {
        return impl_->backend ? impl_->backend->createTextureFromRGBA(rgba, width, height)
                              : ImTextureID{};
    }

    GLFWwindow* App::glfwWindow() const {
        return impl_->window;
    }

    void* App::nativeWindow() const {
        return gHwnd;
    }

    void* App::nativeView() const {
        return gHwnd;
    }

    void* detail::nativeHost() {
        return gHwnd;
    }
} // namespace imshell

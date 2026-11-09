#include "mydear/app.hpp"
#include "../activity.hpp"
#include "imgui_impl_glfw.h"
#include "imgui_internal.h"
#include "metal_backend.hpp"
#include "../native_view.hpp"
#include "mydear/themes.hpp"
#include "mydear/title_bar.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#define GLFW_EXPOSE_NATIVE_COCOA
#import <AppKit/AppKit.h>
#include <GLFW/glfw3.h>
#import <GLFW/glfw3native.h>
#import <objc/runtime.h>

namespace mydear {
    namespace {
        constexpr double kIdleAfterSeconds = 2.0;   // no input for this long: start idling
        constexpr double kMinWaitSeconds = 1.0 / 120.0;
        constexpr double kMaxWaitSeconds = 0.2; // an idle app still wakes this often

        Delegate* gDelegate = nullptr; // glfw and appkit callbacks have no user pointer
        NSWindow* gWindow = nil;
        bool gTitleBar = false;
        NSEvent* gLastMouseDown = nil; // what a window drag starts from
        enum class BarAction { None, Drag, DoubleClick } gBarAction = BarAction::None;
        ImGuiWindow* gFocusBeforeBar = nullptr; // a click on the bar hands focus back to it
        constexpr float kTitleBarHeight = 28; // the native one: traffic lights stay centred

        // after the frame: a click in empty bar space, now that imgui knows it hit no widget
        void runBarAction() {
            const BarAction action = gBarAction;
            gBarAction = BarAction::None;
            if (action == BarAction::Drag) {
                // a click shorter than a frame is over already: nothing to drag
                if (!gLastMouseDown || !(NSEvent.pressedMouseButtons & 1))
                    return;
                [gWindow performWindowDragWithEvent:gLastMouseDown];
                // the drag loop ate the mouse up
                ImGui::GetIO().AddMouseButtonEvent(0, false);
            } else if (action == BarAction::DoubleClick) {
                NSString* pref =
                    [NSUserDefaults.standardUserDefaults stringForKey:@"AppleActionOnDoubleClick"];
                if ([pref isEqualToString:@"Minimize"])
                    [gWindow performMiniaturize:nil];
                else if (![pref isEqualToString:@"None"])
                    [gWindow performZoom:nil];
            }
        }
    } // namespace

    struct App::Impl {
        GLFWwindow* window = nullptr;
        std::unique_ptr<MetalBackend> backend;
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
            std::fprintf(stderr, "mydear: glfwInit failed\n");
            return false;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // metal draws, not gl
        d.window =
            glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
        if (!d.window) {
            std::fprintf(stderr, "mydear: could not create the window\n");
            glfwTerminate();
            return false;
        }

        gWindow = glfwGetCocoaWindow(d.window);
        gTitleBar = config.titleBar;
        if (config.titleBar) {
            NSWindow* w = glfwGetCocoaWindow(d.window);
            w.styleMask |= NSWindowStyleMaskFullSizeContentView;
            w.titlebarAppearsTransparent = YES;
            w.titleVisibility = NSWindowTitleHidden;
            [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                                  handler:^NSEvent*(NSEvent* e) {
                                                    gLastMouseDown = e;
                                                    return e;
                                                  }];
        }

        d.backend = std::make_unique<MetalBackend>(d.window);
        if (!d.backend->metalDevice_) {
            std::fprintf(stderr, "mydear: no metal device\n");
            return false;
        }
        d.backend->vsync = d.vsync;

        // files dropped on the window
        glfwSetDropCallback(d.window, [](GLFWwindow* w, const int count, const char** paths) {
            for (int i = 0; i < count; i++)
                if (gDelegate)
                    gDelegate->openFile(paths[i]);
            glfwFocusWindow(w);
        });
        // "open with" and double-click in finder: glfw's app delegate has no
        // application:openFile:, so add one
        Class delegateClass = [NSApp.delegate class];
        SEL openFile = @selector(application:openFile:);
        if (![delegateClass instancesRespondToSelector:openFile]) {
            IMP imp = imp_implementationWithBlock(^BOOL(id, NSApplication*, NSString* name) {
              std::string path(name.UTF8String);
              dispatch_async(dispatch_get_main_queue(), ^{
                if (gDelegate)
                    gDelegate->openFile(path);
              });
              return YES;
            });
            class_addMethod(delegateClass, openFile, imp, "c@:@@");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        if (config.docking)
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();
        Theme::ApplyTheme(Theme::Current()->colors);
        delegate.setup();

        ImGui_ImplGlfw_InitForOther(d.window, true);
        if (!d.backend->initializeImGui()) {
            std::fprintf(stderr, "mydear: the imgui metal backend failed\n");
            return false;
        }
        return true;
    }

    bool App::pollEvents() {
        glfwPollEvents();
        return !glfwWindowShouldClose(impl_->window);
    }

    void App::renderFrame() {
        auto& d = *impl_;
        @autoreleasepool {
            d.backend->beginFrame(Theme::Current()->colors.base);
            ImGui::NewFrame();
            d.delegate->frame();
            ImGui::Render();
            d.backend->renderDrawData(ImGui::GetDrawData());
            d.backend->present();
            detail::placeNativeViews();
        }
        runBarAction();
    }

    // full speed while the user works or something animates; after kIdleAfterSeconds
    // without either, the wait between frames grows to kMaxWaitSeconds
    void App::run() {
        auto& d = *impl_;
        double lastInput = glfwGetTime();
        bool wasFocused = glfwGetWindowAttrib(d.window, GLFW_FOCUSED) != 0;
        while (!glfwWindowShouldClose(d.window)) {
            if (d.delegate->shouldQuit())
                glfwSetWindowShouldClose(d.window, GLFW_TRUE);
            const double start = glfwGetTime();
            const double sinceInput = start - lastInput;
            const bool focused = glfwGetWindowAttrib(d.window, GLFW_FOCUSED) != 0;
            const bool busy = d.delegate->wantsFrames();

            double wait = 0.0;
            if (!focused && !busy)
                wait = kMaxWaitSeconds;
            else if (focused && !busy && sinceInput >= kIdleAfterSeconds)
                wait = std::clamp(sinceInput - kIdleAfterSeconds, kMinWaitSeconds, kMaxWaitSeconds);
            if (wait > 0.0)
                glfwWaitEventsTimeout(wait);
            else
                glfwPollEvents();

            const bool nowFocused = glfwGetWindowAttrib(d.window, GLFW_FOCUSED) != 0;
            if (nowFocused && !wasFocused)
                lastInput = glfwGetTime();
            wasFocused = nowFocused;
            // never render unfocused: CAMetalLayer nextDrawable blocks while the window
            // is in the background, which makes switching apps sluggish
            if (!nowFocused)
                continue;

            renderFrame();

            // without vsync nothing paces the loop: sleep out the rest of the frame
            if (!d.vsync && d.maxFps > 0) {
                const double left = start + 1.0 / d.maxFps - glfwGetTime();
                if (left > 0)
                    std::this_thread::sleep_for(std::chrono::duration<double>(left));
            }
            if (detail::isImGuiUserActive() || d.delegate->wantsFrames())
                lastInput = glfwGetTime();
        }
    }

    void App::destroy() {
        auto& d = *impl_;
        if (!d.window)
            return;
        detail::dropNativeViews();
        if (d.backend)
            d.backend->shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        d.backend.reset();
        glfwDestroyWindow(d.window);
        glfwTerminate();
        d.window = nullptr;
        gDelegate = nullptr;
    }

    void* detail::nativeHost() {
        return (__bridge void*)gWindow.contentView;
    }

    float TitleBarHeight() {
        return gTitleBar ? kTitleBarHeight : 0;
    }

    TitleBar BeginTitleBar() {
        if (ImGuiWindow* nav = GImGui->NavWindow;
            nav && nav != ImGui::FindWindowByName("##mydear_title_bar"))
            gFocusBeforeBar = nav;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        TitleBar bar;
        bar.pos = vp->Pos;
        bar.size = ImVec2(vp->Size.x, TitleBarHeight());
        bar.fullscreen = gWindow && (gWindow.styleMask & NSWindowStyleMaskFullScreen);
        if (NSButton* zoom = [gWindow standardWindowButton:NSWindowZoomButton];
            zoom && !zoom.hidden && !bar.fullscreen)
            bar.leftInset = NSMaxX(zoom.frame) + Theme::Spacing::M;

        ImGui::SetNextWindowPos(bar.pos);
        ImGui::SetNextWindowSize(bar.size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0)); // the default 32 would overlap
        ImGui::Begin("##mydear_title_bar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar(4);
        ImGui::SetCursorScreenPos(ImVec2(
            bar.pos.x + bar.leftInset, bar.pos.y + (bar.size.y - ImGui::GetFrameHeight()) * 0.5f));
        return bar;
    }

    void EndTitleBar() {
        // the bar is chrome: once its click is over, focus goes back where it was
        if (GImGui->NavWindow == ImGui::GetCurrentWindow() && !ImGui::IsAnyItemActive() &&
            gFocusBeforeBar && gFocusBeforeBar->WasActive)
            ImGui::FocusWindow(gFocusBeforeBar);
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
            if (ImGui::IsMouseDoubleClicked(0))
                gBarAction = BarAction::DoubleClick;
            else if (ImGui::IsMouseClicked(0))
                gBarAction = BarAction::Drag;
        }
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
        return impl_->window ? (__bridge void*)glfwGetCocoaWindow(impl_->window) : nullptr;
    }

    void* App::nativeView() const {
        return impl_->window
                   ? (__bridge void*)((NSWindow*)glfwGetCocoaWindow(impl_->window)).contentView
                   : nullptr;
    }
} // namespace mydear

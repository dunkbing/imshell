#include "mydear/app.hpp"
#include "../activity.hpp"
#include "imgui_impl_glfw.h"
#include "metal_backend.hpp"
#include "mydear/themes.hpp"

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
        }
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
} // namespace mydear

#include "../activity.hpp"
#include "imshell/app.hpp"
#include "../frame_skip.hpp"
#include "../glfw_loop.hpp"
#include "imgui_impl_glfw.h"
#include "imgui_internal.h"
#include "metal_backend.hpp"
#include "../native_view.hpp"
#include "imshell/themes.hpp"
#include "imshell/title_bar.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#define GLFW_EXPOSE_NATIVE_COCOA
#import <AppKit/AppKit.h>
#include <GLFW/glfw3.h>
#import <GLFW/glfw3native.h>
#import <objc/runtime.h>

namespace imshell {
    namespace detail {
        void installTextInput(NSView* view); // text_input.mm
        void textInputFrame();
    } // namespace detail

    namespace {
        Delegate* gDelegate = nullptr; // glfw and appkit callbacks have no user pointer
        NSWindow* gWindow = nil;
        bool gTitleBar = false;
        NSEvent* gLastMouseDown = nil; // what a window drag starts from
        enum class BarAction { None, Drag, DoubleClick } gBarAction = BarAction::None;
        ImGuiWindow* gFocusBeforeBar = nullptr; // a click on the bar hands focus back to it
        float gBarHeight = 28;
        const Theme* gTheme = nullptr; // the theme the window was last given

        // the window (traffic lights, sheets, native views) follows the theme, as on windows and linux
        void followTheme() {
            const Theme* theme = &imshell::CurrentTheme();
            if (theme == gTheme || !gWindow)
                return;
            gTheme = theme;
            const ImVec4 c = theme->background;
            gWindow.backgroundColor = [NSColor colorWithRed:c.x green:c.y blue:c.z alpha:c.w];
            gWindow.appearance = [NSAppearance appearanceNamed:theme->dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
        }

        // macos pins the traffic lights to its 28 pt bar: grow their container and centre them
        // (as electron's trafficLightPosition does). appkit lays them out again on resize and
        // fullscreen, so this runs every frame and only writes when something moved
        void centreTrafficLights() {
            NSButton* close = [gWindow standardWindowButton:NSWindowCloseButton];
            NSView* container = close.superview.superview;
            if (!container || (gWindow.styleMask & NSWindowStyleMaskFullScreen))
                return;
            NSRect frame = container.frame;
            frame.size.height = gBarHeight;
            frame.origin.y = NSHeight(gWindow.frame) - gBarHeight;
            if (!NSEqualRects(frame, container.frame))
                container.frame = frame;
            for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
                NSButton* b = [gWindow standardWindowButton:kind];
                NSPoint at = b.frame.origin;
                at.y = round((NSHeight(b.superview.frame) - NSHeight(b.frame)) * 0.5);
                if (!NSEqualPoints(at, b.frame.origin))
                    [b setFrameOrigin:at];
            }
        }

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
            std::fprintf(stderr, "imshell: glfwInit failed\n");
            return false;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // metal draws, not gl
        d.window =
            glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
        if (!d.window) {
            std::fprintf(stderr, "imshell: could not create the window\n");
            glfwTerminate();
            return false;
        }

        gWindow = glfwGetCocoaWindow(d.window);
        detail::installTextInput(glfwGetCocoaView(d.window));
        if (config.icon.png) {
            NSData* bytes = [NSData dataWithBytes:config.icon.png length:config.icon.size];
            if (NSImage* image = [[NSImage alloc] initWithData:bytes])
                NSApp.applicationIconImage = image;
        }
        gTitleBar = config.titleBar;
        gBarHeight = config.titleBarHeight > 0 ? config.titleBarHeight : 28;
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
            std::fprintf(stderr, "imshell: no metal device\n");
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
        detail::useIniFile(config);
        if (config.docking)
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();
        ApplyTheme();
        delegate.setup();

        ImGui_ImplGlfw_InitForOther(d.window, true);
        if (!d.backend->initializeImGui()) {
            std::fprintf(stderr, "imshell: the imgui metal backend failed\n");
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
        followTheme();
        @autoreleasepool {
            d.backend->beginFrame(CurrentTheme().background);
            ImGui::NewFrame();
            d.delegate->frame();
            detail::textInputFrame();
            ImGui::Render();
            detail::frameSkipped() =
                detail::sameAsLastFrame(ImGui::GetDrawData(), CurrentTheme().background);
            if (detail::frameSkipped()) {
                d.backend->dropFrame();
            } else {
                d.backend->renderDrawData(ImGui::GetDrawData());
                d.backend->present();
            }
            detail::placeNativeViews();
        }
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
        bar.fullscreen = gWindow && (gWindow.styleMask & NSWindowStyleMaskFullScreen);
        if (gWindow && gBarHeight != 28)
            centreTrafficLights();
        if (NSButton* zoom = [gWindow standardWindowButton:NSWindowZoomButton];
            zoom && !zoom.hidden && !bar.fullscreen)
            bar.leftInset = NSMaxX(zoom.frame) + Theme::Spacing::M;

        ImGui::SetNextWindowPos(bar.pos);
        ImGui::SetNextWindowSize(bar.size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0)); // the default 32 would overlap
        ImGui::Begin("##imshell_title_bar", nullptr,
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
        return impl_->window ? (__bridge void*)glfwGetCocoaWindow(impl_->window) : nullptr;
    }

    void* App::nativeView() const {
        return impl_->window
                   ? (__bridge void*)((NSWindow*)glfwGetCocoaWindow(impl_->window)).contentView
                   : nullptr;
    }
} // namespace imshell

#include "imshell/shortcut_key.hpp"

namespace imshell {
    bool ShortcutKeyDown() {
        return (NSEvent.modifierFlags & NSEventModifierFlagCommand) != 0 || ImGui::GetIO().KeyCtrl;
    }
} // namespace imshell

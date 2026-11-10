#pragma once

#include "imgui.h"
#include "mydear/frames.hpp"
#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;

// the platform shell: a native window, the graphics backend (metal on apple), the imgui
// context and backends, the frame loop and input. the host draws, mydear does the rest
namespace mydear {
    struct Config {
        std::string title;
        int width = 1280;
        int height = 800;
        bool docking = true; // ImGuiConfigFlags_DockingEnable
        bool vsync = true;
        int maxFps = 120; // with vsync off; 0 = unlimited
        // the app draws its own title bar: content runs under it (macos keeps the traffic
        // lights), see mydear/title_bar.hpp
        bool titleBar = false;
        float titleBarHeight = 28; // points; macos's own is 28, taller re-centres the traffic lights
    };

    class Delegate {
    public:
        virtual ~Delegate() = default;
        // the imgui context exists: load fonts, set the style
        virtual void setup() {}
        // one frame of ui, between ImGui::NewFrame() and ImGui::Render()
        virtual void frame() = 0;
        // a file dropped on the window or opened with the app (finder, explorer)
        virtual void openFile(const std::string& path) {
            (void)path;
        }
        // keep drawing while idle (something animates or loads)
        virtual bool wantsFrames() {
            return framesRequested();
        }
        // checked every loop: true closes the window
        virtual bool shouldQuit() {
            return false;
        }
    };

    // one window per app. run() is the whole loop; a host that drives frames itself
    // (a test engine) calls pollEvents() and renderFrame() instead. on ios uikit runs the
    // loop: start with RunUIKit (mydear/ios.h) and create() from the root controller
    class App {
    public:
        App();
        ~App();
        App(const App&) = delete;
        App& operator=(const App&) = delete;

        bool create(const Config& config, Delegate& delegate);
        void run();
        // false once the window should close
        bool pollEvents();
        void renderFrame();
        // shuts the backends, destroys the imgui context and the window
        void destroy();

        void setVSync(bool on);
        void setMaxFps(int fps);
        // the backend's texture upload; ImTextureID{} on failure
        ImTextureID createTexture(const uint8_t* rgba, int width, int height);

        GLFWwindow* glfwWindow() const;
        // NSWindow* on macos, UIWindow* on ios
        void* nativeWindow() const;
        // the view imgui draws into: NSView* (the window's content view) on macos, MTKView* on ios
        void* nativeView() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    namespace detail {
        inline ImVec4& safeArea() { // set by the shell before each frame
            static ImVec4 insets;
            return insets;
        }
    } // namespace detail
    // the edges system ui covers (status and navigation bars, a notch, a camera cutout) in
    // imgui units: x left, y top, z right, w bottom; zero on desktops. imgui gets the same, so
    // the main viewport's WorkPos/WorkSize leave it out (from the next frame on)
    inline ImVec4 SafeArea() {
        return detail::safeArea();
    }

    // macos: render the next frame offscreen at `scale` and save it as a png (docs shots),
    // without the title bar strip: its os buttons are native, not part of the frame
    void requestFrameCapture(const char* path, float scale);
    bool frameCapturePending();
    // macos: every frame from here on, at `scale`, piped to ffmpeg as an mp4
    bool startRecording(const char* path, float scale, int width, int height);
    void stopRecording();
} // namespace mydear

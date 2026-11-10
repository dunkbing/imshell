#pragma once

#include "imgui.h"
#include "mydear/frames.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;

// the platform shell: a native window, the graphics backend (metal on apple), the imgui
// context and backends, the frame loop and input. the host draws, mydear does the rest
namespace mydear {
    struct Config {
        std::string title;
        // names the data folder (DataDir) where imgui.ini is kept; empty = no imgui.ini
        std::string appName;
        int width = 1280;
        int height = 800;
        bool docking = true; // ImGuiConfigFlags_DockingEnable
        bool vsync = true;
        int maxFps = 120; // with vsync off; 0 = unlimited
        // the app draws its own title bar: content runs under it (macos keeps the traffic
        // lights), see mydear/title_bar.hpp
        bool titleBar = false;
        // points (dp on android, pixels on windows); 0 = the platform's own: 28 on macos, 32 on
        // windows, 36 on linux, 44 on android. taller re-centres the macos traffic lights
        float titleBarHeight = 0;
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
        // the user closes the window (close button, cmd-q, alt-f4): false keeps it open, e.g. to
        // ask about unsaved work first and quit later through shouldQuit()
        virtual bool closeRequested() {
            return true;
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

<<<<<<< HEAD
    float TitleBarHeight(); // mydear/title_bar.hpp
    struct Rect {
        ImVec2 pos, size;
    };
    // where the app's ui goes: the main viewport inside SafeArea(), under the title bar
    inline Rect ContentRect() {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec4 s = SafeArea();
        const float top = s.y + TitleBarHeight();
        return {ImVec2(vp->Pos.x + s.x, vp->Pos.y + top),
                ImVec2(std::max(0.0f, vp->Size.x - s.x - s.z), std::max(0.0f, vp->Size.y - top - s.w))};
    }

    // the app's own folder for settings and data, created on first call: application support
    // (macos), %APPDATA% (windows), $XDG_DATA_HOME or ~/.local/share (linux), the app's private
    // files (ios documents, android internal storage). empty if it cannot be made
    std::string DataDir(const std::string& appName);

    // the whole app: create, run, destroy; 1 if the window could not be made. on android call it
    // from android_main after SetAndroidApp, with a delegate that outlives it (static): a
    // recreated activity carries on in the same app. not on ios (see RunUIKit)
    int Run(const Config& config, Delegate& delegate);

=======
>>>>>>> origin/main
    // macos: render the next frame offscreen at `scale` and save it as a png (docs shots),
    // without the title bar strip: its os buttons are native, not part of the frame
    void requestFrameCapture(const char* path, float scale);
    bool frameCapturePending();
    // macos: every frame from here on, at `scale`, piped to ffmpeg as an mp4
    bool startRecording(const char* path, float scale, int width, int height);
    void stopRecording();
} // namespace mydear

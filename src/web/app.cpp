// the browser shell: emscripten's glfw port (contrib.glfw3) on a full-page canvas, imgui's
// opengl3 backend on webgl2, the browser's animation frames as the loop. single threaded, so
// it runs on any static host (threads would need cross-origin isolation headers)
#include "imshell/app.hpp"
#include "../activity.hpp"
#include "../frame_skip.hpp"
#include "../loop_pass.hpp"
#include "../native_view.hpp"
#include "../touch.hpp"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imshell/file_dialog.hpp"
#include "imshell/themes.hpp"
#include "imshell/title_bar.hpp"

#include <GLES3/gl3.h>
#include <GLFW/emscripten_glfw3.h>
#include <GLFW/glfw3.h>
#include <emscripten.h>

namespace {
    GLFWwindow* gWindow = nullptr;
    imshell::Delegate* gDelegate = nullptr;
    bool gTitleBar = false;
    float gBarHeight = 36;
    imshell::App* gApp = nullptr; // run()'s, for the browser's frame callback
} // namespace

namespace imshell::detail {
    void installTextInput(void (*runFrame)()); // text_input.cpp
    void textInputFrame();
} // namespace imshell::detail

// clang-format off
// a finger goes to the touch layer (a tap clicks, a swipe scrolls and coasts, a hold is a right
// click), as on the phone shells. glfw listens on the canvas and the document and would turn
// the finger into a left-button drag, so the touch is caught on the window, in the capture
// phase, before either sees it
EM_JS(void, imshell_touch_listen, (), {
    const c = Module.canvas;
    let id = null;
    const send = (phase, t) => {
        const r = c.getBoundingClientRect();
        Module._imshell_touch(phase, t.clientX - r.left, t.clientY - r.top);
    };
    const mine = (e) => Array.from(e.changedTouches).find((t) => t.identifier === id);
    const listen = (type, f) => window.addEventListener(type, (e) => {
        if (e.target !== c)
            return;
        e.stopPropagation();
        f(e);
    }, {capture: true, passive: false});
    listen("touchstart", (e) => {
        e.preventDefault(); // no page pan or zoom, no emulated mouse events
        if (id !== null)
            return; // shortcut: one finger, no pinch yet
        id = e.changedTouches[0].identifier;
        send(0, e.changedTouches[0]);
    });
    listen("touchmove", (e) => {
        e.preventDefault();
        const t = mine(e);
        if (t)
            send(1, t);
    });
    const end = (phase) => (e) => {
        const t = mine(e);
        if (!t)
            return;
        id = null;
        send(phase, t);
        if (phase === 2)
            Module.imshellText?.tap?.(); // a field's tap focuses the textarea inside the gesture
    };
    listen("touchend", end(2));
    listen("touchcancel", end(3));
});
// clang-format on

extern "C" EMSCRIPTEN_KEEPALIVE void imshell_touch(int phase, float x, float y) {
    namespace touch = imshell::touch;
    const double now = emscripten_get_now() / 1000.0;
    switch (phase) {
    case 0:
        // glfw re-sends its own cursor each frame while it thinks the mouse is outside, and it
        // never sees a finger: claim the window so the finger's position stands
        ImGui_ImplGlfw_CursorEnterCallback(gWindow, GLFW_TRUE);
        touch::down(ImVec2(x, y), now);
        break;
    case 1:
        touch::move(ImVec2(x, y), now);
        break;
    case 2:
        touch::move(ImVec2(x, y), now);
        touch::up(now, true); // no hover on a touch screen: nothing stays highlighted
        break;
    default:
        touch::cancel();
    }
}

namespace imshell {
    struct App::Impl {};

    App::App() : impl_(std::make_unique<Impl>()) {}

    App::~App() {
        destroy();
    }

    bool App::create(const Config& config, Delegate& delegate) {
        if (!glfwInit())
            return false;
        gDelegate = &delegate;
        gTitleBar = config.titleBar;
        gBarHeight = config.titleBarHeight > 0 ? config.titleBarHeight : 36;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);    // webgl2
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE); // the canvas at the screen's density
        gWindow =
            glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
        if (!gWindow)
            return false;
        emscripten::glfw3::MakeCanvasResizable(gWindow, "window"); // the canvas follows the page
        glfwMakeContextCurrent(gWindow);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        if (config.docking)
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // shortcut: imgui.ini goes to emscripten's in-memory file system, so it is lost on
        // reload; mount idbfs here if a host needs settings to survive
        detail::useIniFile(config);
        ImGui::StyleColorsDark();
        Theme::ApplyTheme(Theme::Current()->colors);
        delegate.setup();
        ImGui_ImplGlfw_InitForOpenGL(gWindow, true);
        io.BackendPlatformName = "imshell_web";
        imshell_touch_listen();
        detail::installTextInput([] {
            if (gApp)
                gApp->renderFrame();
        });
        return ImGui_ImplOpenGL3_Init("#version 300 es");
    }

    bool App::pollEvents() {
        glfwPollEvents();
        return !glfwWindowShouldClose(gWindow);
    }

    void App::renderFrame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        if (touch::longPressDue(emscripten_get_now() / 1000.0)) {
            // on text a double click selects the word (and focuses the field); elsewhere a
            // right click (context menus)
            if (ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput)
                for (int i = 0; i < 2; ++i) {
                    io.AddMouseButtonEvent(0, true);
                    io.AddMouseButtonEvent(0, false);
                }
            else
                touch::rightClick();
        }
        touch::coast(io.DeltaTime);
        ImGui::NewFrame();
        gDelegate->frame();
        touch::drawHandles();
        detail::textInputFrame();
        ImGui::Render();
        touch::takeLift(); // the finger is gone: nothing stays hovered
        const ImVec4 base = Theme::Current()->colors.base;
        // a canvas not drawn to keeps showing its last frame, so an unchanged one costs no gpu
        detail::frameSkipped() = detail::sameAsLastFrame(ImGui::GetDrawData(), base);
        if (detail::frameSkipped())
            return;
        int w = 0, h = 0;
        glfwGetFramebufferSize(gWindow, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(base.x, base.y, base.z, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void App::run() {
        // the browser calls back once per display refresh; this never returns (the page is the app)
        // shortcut: a frame on every refresh while the page is visible (the browser pauses hidden
        // tabs); idle like the desktop loop if a page's cpu use matters
        gApp = this;
        emscripten_set_main_loop(
            [] {
                if (gDelegate->shouldQuit()) {
                    emscripten_cancel_main_loop();
                    return;
                }
                glfwPollEvents();
                detail::runLoopPass();
                gApp->renderFrame();
            },
            0, true);
    }

    void App::destroy() {
        if (!gWindow)
            return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(gWindow);
        glfwTerminate();
        gWindow = nullptr;
        gDelegate = nullptr;
    }

    void App::setVSync(bool) {} // the browser paces frames
    void App::setMaxFps(int) {}

    ImTextureID App::createTexture(const uint8_t* rgba, const int width, const int height) {
        if (!rgba || !gWindow)
            return ImTextureID{};
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glBindTexture(GL_TEXTURE_2D, 0);
        return static_cast<ImTextureID>(texture);
    }

    GLFWwindow* App::glfwWindow() const {
        return gWindow;
    }

    void* App::nativeWindow() const {
        return gWindow;
    }

    void* App::nativeView() const {
        return gWindow;
    }

    // the title bar is a plain imgui strip at the top of the page, as on android
    float TitleBarHeight() {
        return gTitleBar ? gBarHeight : 0;
    }

    TitleBar BeginTitleBar() {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        TitleBar bar;
        bar.pos = vp->Pos;
        bar.size = ImVec2(vp->Size.x, TitleBarHeight());
        bar.leftInset = bar.rightInset = Theme::Spacing::M;
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
        ImGui::SetCursorScreenPos(ImVec2(
            bar.pos.x + bar.leftInset, bar.pos.y + (bar.size.y - ImGui::GetFrameHeight()) * 0.5f));
        return bar;
    }

    void EndTitleBar() {
        ImGui::Dummy(ImVec2(0, 0)); // an empty bar: imgui wants an item after the cursor is set
        ImGui::End();
    }

    // native views: none in the browser (a dom element over the canvas could be one later)
    void* detail::nativeHost() {
        return nullptr;
    }
    namespace detail {
        void* nativeAttach(void*) {
            return nullptr;
        }
        void nativePlace(void*, void*, const ImRect&, const ImRect&) {}
        void nativeSetHidden(void*, bool) {}
        void nativeRemove(void*) {}
    } // namespace detail

    // shortcut: no file dialogs in the browser yet (an <input type=file> into memfs and a
    // download link for saves); the callbacks never run
    void OpenFileDialog(const std::vector<FileFilter>&, FileDone, const std::string&) {}
    void SaveFileDialog(const std::string&, const std::vector<FileFilter>&, FileDone) {}
    void PickFolderDialog(FileDone) {}
    std::string DocumentsDir() {
        return "/home/web_user";
    }
} // namespace imshell

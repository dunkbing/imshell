// android shell: NativeActivity through android_native_app_glue, egl + gles 3, imgui's
// opengl3 renderer. imgui works in dp (DisplayFramebufferScale = density), like points on ios.
// touch follows the ios shell: a tap clicks, a drag on empty space or a plain button scrolls
// (and coasts), a drag on a resize or text cursor drags, a long press right-clicks
#include "mydear/android.hpp"
#include "mydear/app.hpp"
#include "mydear/themes.hpp"
#include "mydear/title_bar.hpp"
#include "../native_view.hpp"

#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>

namespace {
    android_app* gAndroid = nullptr;
    mydear::Delegate* gDelegate = nullptr;

    EGLDisplay gDisplay = EGL_NO_DISPLAY;
    EGLSurface gSurface = EGL_NO_SURFACE;
    EGLContext gContext = EGL_NO_CONTEXT;
    EGLConfig gConfig = nullptr;
    bool gGlReady = false;
    bool gVSync = true;
    bool gTitleBar = false;
    float gBarHeight = 0;

    float gDensity = 1; // pixels per dp
    double gLastTime = 0;

    JNIEnv* gEnv = nullptr; // the glue thread's, attached once in create

    double nowSeconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // ---- java (MydearActivity) ----

    jmethodID method(const char* name, const char* sig) {
        if (!gEnv || !gAndroid)
            return nullptr;
        jclass cls = gEnv->GetObjectClass(gAndroid->activity->clazz);
        jmethodID m = gEnv->GetMethodID(cls, name, sig);
        gEnv->DeleteLocalRef(cls);
        if (!m)
            gEnv->ExceptionClear(); // not a MydearActivity
        return m;
    }

    std::string callString(const char* name) {
        jmethodID m = method(name, "()Ljava/lang/String;");
        if (!m)
            return {};
        auto s = static_cast<jstring>(gEnv->CallObjectMethod(gAndroid->activity->clazz, m));
        if (!s)
            return {};
        const char* utf = gEnv->GetStringUTFChars(s, nullptr);
        std::string out = utf ? utf : "";
        gEnv->ReleaseStringUTFChars(s, utf);
        gEnv->DeleteLocalRef(s);
        return out;
    }

    void showKeyboard(bool show) {
        if (jmethodID m = method("showKeyboard", "(Z)V"))
            gEnv->CallVoidMethod(gAndroid->activity->clazz, m, static_cast<jboolean>(show));
    }

    void pollChars() {
        static jmethodID m = method("pollChar", "()I");
        if (!m)
            return;
        while (const jint c = gEnv->CallIntMethod(gAndroid->activity->clazz, m))
            ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(c));
    }

    std::function<void(const std::string&)> gPickDone; // a picker is open

    // java answers null while the picker is open, "" when cancelled, else the copy's path
    void pollPicked() {
        static jmethodID m = method("pollPicked", "()Ljava/lang/String;");
        if (!gPickDone || !m)
            return;
        auto s = static_cast<jstring>(gEnv->CallObjectMethod(gAndroid->activity->clazz, m));
        if (!s)
            return;
        const char* utf = gEnv->GetStringUTFChars(s, nullptr);
        const std::string path = utf ? utf : "";
        gEnv->ReleaseStringUTFChars(s, utf);
        gEnv->DeleteLocalRef(s);
        auto done = std::move(gPickDone);
        gPickDone = nullptr;
        if (!path.empty())
            done(path);
    }

    // ---- egl ----

    bool makeSurface() {
        if (!gAndroid->window || gDisplay == EGL_NO_DISPLAY)
            return false;
        gSurface = eglCreateWindowSurface(gDisplay, gConfig, gAndroid->window, nullptr);
        if (gSurface == EGL_NO_SURFACE)
            return false;
        eglMakeCurrent(gDisplay, gSurface, gSurface, gContext);
        eglSwapInterval(gDisplay, gVSync ? 1 : 0);
        return true;
    }

    void dropSurface() {
        if (gSurface == EGL_NO_SURFACE)
            return;
        eglMakeCurrent(gDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(gDisplay, gSurface);
        gSurface = EGL_NO_SURFACE;
    }

    // the context outlives the window (the app going to the background), so textures do too
    bool initEgl() {
        gDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(gDisplay, nullptr, nullptr);
        const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE,
                                  EGL_WINDOW_BIT,      EGL_RED_SIZE,       8,
                                  EGL_GREEN_SIZE,      8,                  EGL_BLUE_SIZE,
                                  8,                   EGL_ALPHA_SIZE,     8,
                                  EGL_NONE};
        EGLint count = 0;
        if (!eglChooseConfig(gDisplay, attribs, &gConfig, 1, &count) || count == 0)
            return false;
        const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        gContext = eglCreateContext(gDisplay, gConfig, EGL_NO_CONTEXT, ctx);
        return gContext != EGL_NO_CONTEXT && makeSurface();
    }

    // ---- touch ----

    enum class Touch { None, Pending, Pressed, Scrolling };
    Touch gTouch = Touch::None;
    int32_t gPointer = -1;
    ImVec2 gStart, gLast;
    double gStartTime = 0, gLastMoveTime = 0;
    ImVec2 gVelocity, gFling; // dp per second
    bool gLift = false;       // move the touch cursor away after the next frame (no stuck hover)

    constexpr float kSlop = 8;               // dp before a touch becomes a drag
    constexpr double kLongPress = 0.5;       // seconds
    constexpr float kFlingStop = 20;         // dp per second

    void touchAt(ImVec2 p) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(p.x, p.y);
    }

    void scrollBy(ImVec2 d) {
        const float font = std::max(ImGui::GetFontSize(), 1.0f);
        ImGui::GetIO().AddMouseWheelEvent(d.x / (2.0f * font), d.y / (5.0f * font));
    }

    // the visible part of the window: no system bars, no keyboard (pixels)
    ARect visibleRect() {
        const int w = ANativeWindow_getWidth(gAndroid->window);
        const int h = ANativeWindow_getHeight(gAndroid->window);
        const ARect r = gAndroid->contentRect;
        if (r.right <= r.left || r.bottom <= r.top)
            return ARect{0, 0, w, h};
        return ARect{std::max(r.left, 0), std::max(r.top, 0), std::min(r.right, w),
                     std::min(r.bottom, h)};
    }

    ImVec2 pointerAt(const AInputEvent* e, size_t i) {
        const ARect r = visibleRect();
        return ImVec2((AMotionEvent_getX(e, i) - r.left) / gDensity,
                      (AMotionEvent_getY(e, i) - r.top) / gDensity);
    }

    void touchEvent(const AInputEvent* e) {
        ImGuiIO& io = ImGui::GetIO();
        const int32_t action = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
        const size_t index = (AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                             AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const double now = nowSeconds();
        switch (action) {
        case AMOTION_EVENT_ACTION_DOWN: {
            gFling = ImVec2(0, 0);
            gLift = false;
            gPointer = AMotionEvent_getPointerId(e, 0);
            gStart = gLast = pointerAt(e, 0);
            gStartTime = gLastMoveTime = now;
            gVelocity = ImVec2(0, 0);
            gTouch = Touch::Pending;
            touchAt(gStart); // hover only: the cursor shape says what lies under the finger
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE: {
            size_t i = 0;
            for (; i < AMotionEvent_getPointerCount(e); ++i)
                if (AMotionEvent_getPointerId(e, i) == gPointer)
                    break;
            if (i == AMotionEvent_getPointerCount(e))
                break;
            const ImVec2 p = pointerAt(e, i);
            if (gTouch == Touch::Pending &&
                std::hypot(p.x - gStart.x, p.y - gStart.y) > kSlop) {
                const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
                if (cursor == ImGuiMouseCursor_Arrow || cursor == ImGuiMouseCursor_Hand) {
                    gTouch = Touch::Scrolling;
                    gLast = gStart;
                } else { // a splitter, column edge or text: drag it
                    gTouch = Touch::Pressed;
                    touchAt(gStart);
                    io.AddMouseButtonEvent(0, true);
                }
            }
            if (gTouch == Touch::Scrolling) {
                const ImVec2 d(p.x - gLast.x, p.y - gLast.y);
                touchAt(p); // scroll the window under the finger
                scrollBy(d);
                const float dt = static_cast<float>(std::max(now - gLastMoveTime, 1e-3));
                gVelocity = ImVec2(d.x / dt * 0.5f + gVelocity.x * 0.5f,
                                   d.y / dt * 0.5f + gVelocity.y * 0.5f);
            } else if (gTouch == Touch::Pressed) {
                touchAt(p);
            }
            gLast = p;
            gLastMoveTime = now;
            break;
        }
        case AMOTION_EVENT_ACTION_UP:
            if (gTouch == Touch::Pending) { // a tap
                touchAt(gStart);
                io.AddMouseButtonEvent(0, true);
                io.AddMouseButtonEvent(0, false);
            } else if (gTouch == Touch::Pressed) {
                io.AddMouseButtonEvent(0, false);
            } else if (gTouch == Touch::Scrolling && now - gLastMoveTime < 0.1) {
                gFling = gVelocity;
            }
            gTouch = Touch::None;
            gPointer = -1;
            gLift = true;
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            if (gTouch == Touch::Pressed) {
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                io.AddMouseButtonEvent(0, false);
            }
            gTouch = Touch::None;
            gPointer = -1;
            break;
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            (void)index; // ponytail: one finger drives the mouse; no pinch or two-finger scroll
            break;
        default:
            break;
        }
    }

    // a mouse or trackpad (chromebooks, desktop mode): a plain mouse
    void mouseEvent(const AInputEvent* e) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        const int32_t action = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
        const ImVec2 p = pointerAt(e, 0);
        io.AddMousePosEvent(p.x, p.y);
        if (action == AMOTION_EVENT_ACTION_SCROLL) {
            io.AddMouseWheelEvent(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HSCROLL, 0),
                                  AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_VSCROLL, 0));
            return;
        }
        const int32_t buttons = AMotionEvent_getButtonState(e);
        io.AddMouseButtonEvent(0, (buttons & AMOTION_EVENT_BUTTON_PRIMARY) != 0);
        io.AddMouseButtonEvent(1, (buttons & AMOTION_EVENT_BUTTON_SECONDARY) != 0);
        io.AddMouseButtonEvent(2, (buttons & AMOTION_EVENT_BUTTON_TERTIARY) != 0);
    }

    int32_t onInput(android_app*, AInputEvent* e) {
        mydear::requestFrames(std::chrono::milliseconds(500));
        if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_MOTION)
            return ImGui_ImplAndroid_HandleInputEvent(e); // keys
        if (AMotionEvent_getToolType(e, 0) == AMOTION_EVENT_TOOL_TYPE_MOUSE)
            mouseEvent(e);
        else
            touchEvent(e);
        return 1;
    }

    void onCmd(android_app* app, int32_t cmd) {
        mydear::requestFrames(std::chrono::milliseconds(500));
        switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (gContext != EGL_NO_CONTEXT)
                makeSurface();
            break;
        case APP_CMD_TERM_WINDOW:
            dropSurface();
            break;
        case APP_CMD_CONFIG_CHANGED:
            gDensity = std::max(AConfiguration_getDensity(app->config), 1) / 160.0f;
            break;
        default:
            break;
        }
    }

    // one pass over the looper: true once the activity is going away
    bool pump(int timeoutMs) {
        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(timeoutMs, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source)
                source->process(gAndroid, source);
            if (gAndroid->destroyRequested)
                return true;
            timeoutMs = 0;
        }
        return gAndroid->destroyRequested != 0;
    }

    // what keeps the loop drawing without input
    bool animating() {
        // shortcut: an open picker keeps frames coming until it answers (seconds, while hidden)
        return gTouch == Touch::Pending || std::hypot(gFling.x, gFling.y) > kFlingStop || gLift ||
               gPickDone ||
               // imgui trickles queued input one event per frame: typed text needs frames
               (GImGui && GImGui->InputEventsQueue.Size > 0);
    }
} // namespace

namespace mydear {
    void SetAndroidApp(android_app* app) {
        gAndroid = app;
    }

    android_app* AndroidApp() {
        return gAndroid;
    }

    void PickFile(const std::string& mimeTypes, const std::string& intoDir,
                  std::function<void(const std::string&)> done) {
        jmethodID m = method("pickFile", "(Ljava/lang/String;Ljava/lang/String;)V");
        if (!m || gPickDone)
            return;
        gPickDone = std::move(done);
        jstring types = gEnv->NewStringUTF(mimeTypes.c_str());
        jstring dir = gEnv->NewStringUTF(intoDir.c_str());
        gEnv->CallVoidMethod(gAndroid->activity->clazz, m, types, dir);
        gEnv->DeleteLocalRef(types);
        gEnv->DeleteLocalRef(dir);
    }

    struct App::Impl {};

    App::App() : impl_(std::make_unique<Impl>()) {}

    App::~App() {
        destroy();
    }

    bool App::create(const Config& config, Delegate& delegate) {
        if (!gAndroid) {
            __android_log_print(ANDROID_LOG_ERROR, "mydear", "call SetAndroidApp first");
            return false;
        }
        gDelegate = &delegate;
        gVSync = config.vsync;
        gTitleBar = config.titleBar;
        gBarHeight = config.titleBarHeight;
        gAndroid->onAppCmd = onCmd;
        gAndroid->onInputEvent = onInput;
        gAndroid->activity->vm->AttachCurrentThread(&gEnv, nullptr);
        gDensity = std::max(AConfiguration_getDensity(gAndroid->config), 1) / 160.0f;

        // the window arrives a few events after start
        while (!gAndroid->window)
            if (pump(-1))
                return false;
        if (!initEgl()) {
            __android_log_print(ANDROID_LOG_ERROR, "mydear", "egl setup failed");
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        if (config.docking)
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr; // the host saves what it wants (activity->internalDataPath)
        io.MouseDoubleClickMaxDist = 20.0f;
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
            static std::string text;
            text = callString("getClipboard");
            return text.c_str();
        };
        pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            jmethodID m = method("setClipboard", "(Ljava/lang/String;)V");
            if (!m)
                return;
            jstring s = gEnv->NewStringUTF(text);
            gEnv->CallVoidMethod(gAndroid->activity->clazz, m, s);
            gEnv->DeleteLocalRef(s);
        };
        ImGui::StyleColorsDark();
        Theme::ApplyTheme(Theme::Current()->colors);
        ImGui_ImplAndroid_Init(gAndroid->window);
        io.BackendPlatformName = "mydear_android";
        delegate.setup();
        gGlReady = ImGui_ImplOpenGL3_Init("#version 300 es");
        return gGlReady;
    }

    bool App::pollEvents() {
        return !pump(0);
    }

    void App::renderFrame() {
        if (gSurface == EGL_NO_SURFACE || !gGlReady)
            return;
        ImGuiIO& io = ImGui::GetIO();
        const ARect visible = visibleRect();
        io.DisplaySize = ImVec2(std::max(visible.right - visible.left, 1) / gDensity,
                                std::max(visible.bottom - visible.top, 1) / gDensity);
        io.DisplayFramebufferScale = ImVec2(gDensity, gDensity);
        const double now = nowSeconds();
        io.DeltaTime = gLastTime > 0 ? static_cast<float>(std::max(now - gLastTime, 1e-4))
                                     : 1.0f / 60.0f;
        gLastTime = now;

        pollChars();
        pollPicked();
        if (gTouch == Touch::Pending && now - gStartTime > kLongPress) { // long press: right click
            touchAt(gStart);
            io.AddMouseButtonEvent(1, true);
            io.AddMouseButtonEvent(1, false);
            gTouch = Touch::None;
        }
        if (std::hypot(gFling.x, gFling.y) > kFlingStop) { // a flick coasts, slowing down
            touchAt(gLast);
            scrollBy(ImVec2(gFling.x * io.DeltaTime, gFling.y * io.DeltaTime));
            const float decay = std::pow(0.998f, io.DeltaTime * 1000.0f);
            gFling = ImVec2(gFling.x * decay, gFling.y * decay);
        }
        const bool lift = gLift && gTouch == Touch::None && std::hypot(gFling.x, gFling.y) <= kFlingStop;

        ImGui::GetStyle().TouchExtraPadding = ImVec2(Theme::Spacing::S, 0.0f);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        gDelegate->frame();
        ImGui::Render();

        // a text field wants the keyboard
        static bool keyboard = false;
        // PlatformImeData.WantVisible: a custom editor that wants the keyboard (ios does the same)
        const bool wants = io.WantTextInput || GImGui->PlatformImeData.WantVisible;
        if (wants != keyboard) {
            keyboard = wants;
            showKeyboard(keyboard);
        }

        const int fullW = ANativeWindow_getWidth(gAndroid->window);
        const int fullH = ANativeWindow_getHeight(gAndroid->window);
        glViewport(0, 0, fullW, fullH);
        const ImVec4 base = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        glClearColor(base.x, base.y, base.z, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // the frame covers the whole window with imgui's origin at the visible rect: the
        // system bars and the keyboard area stay clear
        ImDrawData* draw = ImGui::GetDrawData();
        draw->DisplayPos = ImVec2(-visible.left / gDensity, -visible.top / gDensity);
        draw->DisplaySize = ImVec2(fullW / gDensity, fullH / gDensity);
        ImGui_ImplOpenGL3_RenderDrawData(draw);
        eglSwapBuffers(gDisplay, gSurface);
        detail::placeNativeViews();

        if (lift) { // the finger is gone: nothing stays hovered
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            gLift = false;
        }
    }

    void App::run() {
        while (!gDelegate->shouldQuit()) {
            // idle: a frame every 200 ms, like the desktop loops, so work finishing on another
            // thread shows up without asking for frames
            // no surface (backgrounded, a picker on top): nothing drains, so sleep
            const bool drawing = gSurface != EGL_NO_SURFACE &&
                                 (gDelegate->wantsFrames() || animating());
            if (pump(drawing ? 0 : 200))
                break;
            renderFrame();
        }
    }

    void App::destroy() {
        if (!gDelegate)
            return;
        detail::dropNativeViews();
        if (gGlReady) {
            ImGui_ImplOpenGL3_Shutdown();
            gGlReady = false;
        }
        if (ImGui::GetCurrentContext()) {
            ImGui_ImplAndroid_Shutdown();
            ImGui::DestroyContext();
        }
        dropSurface();
        if (gContext != EGL_NO_CONTEXT)
            eglDestroyContext(gDisplay, gContext);
        if (gDisplay != EGL_NO_DISPLAY)
            eglTerminate(gDisplay);
        gContext = EGL_NO_CONTEXT;
        gDisplay = EGL_NO_DISPLAY;
        gAndroid->activity->vm->DetachCurrentThread();
        gEnv = nullptr;
        gDelegate = nullptr;
    }

    void App::setVSync(const bool on) {
        gVSync = on;
        if (gSurface != EGL_NO_SURFACE)
            eglSwapInterval(gDisplay, on ? 1 : 0);
    }

    void App::setMaxFps(int) {} // ponytail: vsync paces android

    ImTextureID App::createTexture(const uint8_t* rgba, const int width, const int height) {
        if (!rgba || !gGlReady)
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
        return nullptr;
    }

    void* App::nativeWindow() const {
        return gAndroid ? gAndroid->window : nullptr;
    }

    void* App::nativeView() const {
        return gAndroid ? gAndroid->window : nullptr;
    }

    // the title bar is a plain imgui strip at the top: no os buttons, nothing to drag
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
        ImGui::Begin("##mydear_title_bar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar(4);
        ImGui::SetCursorScreenPos(ImVec2(bar.pos.x + bar.leftInset,
                                         bar.pos.y + (bar.size.y - ImGui::GetFrameHeight()) * 0.5f));
        return bar;
    }

    void EndTitleBar() {
        ImGui::End();
    }

    void requestFrameCapture(const char*, float) {}
    bool frameCapturePending() {
        return false;
    }
    bool startRecording(const char*, float, int, int) {
        return false;
    }
    void stopRecording() {}

    // native views: none on android yet (a View over a NativeActivity surface needs a java layout)
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
} // namespace mydear

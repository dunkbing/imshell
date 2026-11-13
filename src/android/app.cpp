// android shell: NativeActivity through android_native_app_glue, egl + gles 3, imgui's
// opengl3 renderer. imgui works in dp (DisplayFramebufferScale = density), like points on ios.
// touch follows the ios shell: a tap clicks, a drag on empty space or a plain button scrolls
// (and coasts), a drag on a resize or text cursor drags, a long press right-clicks
#include "../activity.hpp"
#include "../frame_skip.hpp"
#include "../ime.hpp"
#include "imshell/android.hpp"
#include "imshell/clipboard.hpp"
#include "imshell/haptics.hpp"
#include "imshell/text_input.hpp"
#include "imshell/app.hpp"
#include "imshell/themes.hpp"
#include "imshell/title_bar.hpp"
#include "../native_view.hpp"
#include "../touch.hpp"

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
#include <utility>
#include <vector>

namespace imshell::detail {
    void runNotifications(); // notifications.cpp
}

namespace {
    namespace touch = imshell::touch; // the shared one-finger model (src/touch.cpp)
    android_app* gAndroid = nullptr;
    imshell::Delegate* gDelegate = nullptr;

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

    // ---- java (ImShellActivity) ----

    jmethodID method(const char* name, const char* sig) {
        if (!gEnv || !gAndroid)
            return nullptr;
        jclass cls = gEnv->GetObjectClass(gAndroid->activity->clazz);
        jmethodID m = gEnv->GetMethodID(cls, name, sig);
        gEnv->DeleteLocalRef(cls);
        if (!m)
            gEnv->ExceptionClear(); // not a ImShellActivity
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

    // the keyboard opens for the field's kind: a password's neither suggests nor learns
    void showKeyboard(bool show, imshell::TextKind kind) {
        if (jmethodID m = method("showKeyboard", "(ZI)V"))
            gEnv->CallVoidMethod(gAndroid->activity->clazz, m, static_cast<jboolean>(show),
                                 static_cast<jint>(kind));
    }

    void pollChars() {
        static jmethodID m = method("pollChar", "()I");
        if (!m)
            return;
        ImGuiIO& io = ImGui::GetIO();
        while (const jint c = gEnv->CallIntMethod(gAndroid->activity->clazz, m)) {
            if (c > 0) {
                io.AddInputCharacter(static_cast<unsigned>(c));
                continue;
            }
            // the soft keyboard's edits, in order with its characters
            const ImGuiKey key = c == -1   ? ImGuiKey_Backspace
                                 : c == -2 ? ImGuiKey_Delete
                                           : ImGuiKey_Enter;
            io.AddKeyEvent(key, true);
            io.AddKeyEvent(key, false);
        }
    }


    // ImShellActivity.wake(): java hands the loop something (a toolbar pick, a picked file,
    // new insets) without a native event, so it wakes the looper itself
    void JNICALL nativeWake(JNIEnv*, jobject) {
        if (gAndroid)
            ALooper_wake(gAndroid->looper);
    }

    // through the activity's class loader: the glue thread's FindClass sees only system classes
    void registerWake() {
        jobject activity = gAndroid->activity->clazz;
        jclass activityClass = gEnv->GetObjectClass(activity);
        jmethodID getLoader =
            gEnv->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject loader = gEnv->CallObjectMethod(activity, getLoader);
        jclass loaderClass = gEnv->FindClass("java/lang/ClassLoader");
        jmethodID loadClass =
            gEnv->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring name = gEnv->NewStringUTF("io.imshell.ImShellActivity");
        auto cls = static_cast<jclass>(gEnv->CallObjectMethod(loader, loadClass, name));
        if (gEnv->ExceptionCheck()) {
            gEnv->ExceptionClear(); // not a ImShellActivity
        } else {
            const JNINativeMethod wake{"wake", "()V", reinterpret_cast<void*>(nativeWake)};
            if (gEnv->RegisterNatives(cls, &wake, 1) != JNI_OK)
                gEnv->ExceptionClear();
            gEnv->DeleteLocalRef(cls);
        }
        gEnv->DeleteLocalRef(name);
        gEnv->DeleteLocalRef(loaderClass);
        gEnv->DeleteLocalRef(loader);
        gEnv->DeleteLocalRef(activityClass);
    }
    void callVoid(const char* name) {
        if (jmethodID m = method(name, "()V"))
            gEnv->CallVoidMethod(gAndroid->activity->clazz, m);
    }

    // the system's floating text toolbar (cut, copy, paste, select all) at window pixels
    bool gEditMenu = false;

    void showEditMenu(int x, int y) {
        if (jmethodID m = method("showEditMenu", "(II)V")) {
            gEnv->CallVoidMethod(gAndroid->activity->clazz, m, x, y);
            gEditMenu = true;
        }
    }

    void hideEditMenu() {
        if (!gEditMenu)
            return;
        callVoid("hideEditMenu");
        gEditMenu = false;
    }

    // a toolbar pick comes back as its shortcut letter: the ctrl chord imgui and editors know
    void pollEdits() {
        static jmethodID m = method("pollEdit", "()I");
        if (!m)
            return;
        ImGuiIO& io = ImGui::GetIO();
        while (const jint c = gEnv->CallIntMethod(gAndroid->activity->clazz, m)) {
            const ImGuiKey key = c == 'X' ? ImGuiKey_X
                               : c == 'C' ? ImGuiKey_C
                               : c == 'V' ? ImGuiKey_V
                                          : ImGuiKey_A;
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(key, true);
            io.AddKeyEvent(key, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            gEditMenu = false;
        }
    }

    std::function<void(const std::vector<std::string>&)> gPickDone; // a picker is open

    // java answers null while the picker is open, else the copies' paths (none when cancelled)
    void pollPicked() {
        static jmethodID m = method("pollPicked", "()[Ljava/lang/String;");
        if (!gPickDone || !m)
            return;
        auto array = static_cast<jobjectArray>(gEnv->CallObjectMethod(gAndroid->activity->clazz, m));
        if (!array)
            return;
        std::vector<std::string> paths;
        for (jsize i = 0, n = gEnv->GetArrayLength(array); i < n; ++i) {
            auto s = static_cast<jstring>(gEnv->GetObjectArrayElement(array, i));
            if (const char* utf = gEnv->GetStringUTFChars(s, nullptr)) {
                paths.emplace_back(utf);
                gEnv->ReleaseStringUTFChars(s, utf);
            }
            gEnv->DeleteLocalRef(s);
        }
        gEnv->DeleteLocalRef(array);
        auto done = std::move(gPickDone);
        gPickDone = nullptr;
        if (!paths.empty())
            done(paths);
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

    // ---- touch (the shared engine, src/touch.cpp) ----

    int32_t gPointer = -1; // the finger driving the mouse

    // window pixels: left, top, right, bottom covered by system bars and cutouts, then the
    // keyboard's height (ImShellActivity.windowInsets)
    int gInsets[5] = {};
    // what the current activity was told; a recreated one starts over
    int gLightBars = -1;
    bool gKeyboardShown = false;
    imshell::TextKind gFieldKind = imshell::TextKind::Code; // the kind the keyboard is open for
    // the next frame draws even when it matches the last: a new or resized surface, or the
    // system asked for a redraw (an undrawn surface keeps its last frame otherwise)
    bool gMustDraw = true;
    bool gRecreating = false; // the activity went away only to come back (configuration change)

    void readInsets() {
        static jmethodID m = method("windowInsets", "()[I");
        if (!m)
            return;
        auto a = static_cast<jintArray>(gEnv->CallObjectMethod(gAndroid->activity->clazz, m));
        if (!a)
            return;
        if (gEnv->GetArrayLength(a) == 5)
            gEnv->GetIntArrayRegion(a, 0, 5, gInsets);
        gEnv->DeleteLocalRef(a);
    }

    // the imgui display (pixels): clear of the navigation bar, side cutouts and the keyboard,
    // like ios (imgui clamps popups to the display, and taps there go to the system). only
    // the status bar stays inside it, as the safe area
    ARect visibleRect() {
        const int w = ANativeWindow_getWidth(gAndroid->window);
        const int h = ANativeWindow_getHeight(gAndroid->window);
        const int right = std::max(w - gInsets[2], gInsets[0] + 1);
        return ARect{gInsets[0], 0, right, std::max(h - std::max(gInsets[3], gInsets[4]), 1)};
    }

    // dark bar icons when the theme is light
    void followTheme() {
        int& light = gLightBars;
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const int now = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z > 0.5f;
        if (now == light)
            return;
        light = now;
        if (jmethodID m = method("setLightBars", "(Z)V"))
            gEnv->CallVoidMethod(gAndroid->activity->clazz, m, static_cast<jboolean>(now));
    }

    ImVec2 pointerAt(const AInputEvent* e, size_t i) {
        const ARect r = visibleRect();
        return ImVec2((AMotionEvent_getX(e, i) - r.left) / gDensity,
                      (AMotionEvent_getY(e, i) - r.top) / gDensity);
    }

    void touchEvent(const AInputEvent* e) {
        const int32_t action = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
        const double now = nowSeconds();
        switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            hideEditMenu(); // a touch elsewhere dismisses the text toolbar
            if (gKeyboardShown)
                callVoid("resetInput"); // the caret may move: the keyboard starts a new word
            gPointer = AMotionEvent_getPointerId(e, 0);
            touch::down(pointerAt(e, 0), now);
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            for (size_t i = 0; i < AMotionEvent_getPointerCount(e); ++i)
                if (AMotionEvent_getPointerId(e, i) == gPointer)
                    touch::move(pointerAt(e, i), now);
            break;
        case AMOTION_EVENT_ACTION_UP:
            touch::up(now, true); // no hover on a touch screen: nothing stays highlighted
            gPointer = -1;
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            touch::cancel();
            gPointer = -1;
            break;
        default: // shortcut: one finger drives the mouse; no pinch or two-finger scroll yet (#13)
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
        imshell::requestFrames(std::chrono::milliseconds(500));
        if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_MOTION)
            return ImGui_ImplAndroid_HandleInputEvent(e); // keys
        if (AMotionEvent_getToolType(e, 0) == AMOTION_EVENT_TOOL_TYPE_MOUSE)
            mouseEvent(e);
        else
            touchEvent(e);
        return 1;
    }

    void onCmd(android_app* app, int32_t cmd) {
        imshell::requestFrames(std::chrono::milliseconds(500));
        switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (gContext != EGL_NO_CONTEXT)
                makeSurface();
            gMustDraw = true;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_WINDOW_REDRAW_NEEDED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            gMustDraw = true;
            break;
        case APP_CMD_TERM_WINDOW:
            dropSurface();
            break;
        case APP_CMD_CONFIG_CHANGED:
            gMustDraw = true;
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
        imshell::detail::runNotifications(); // their queued calls, answers and clicks
        return gAndroid->destroyRequested != 0;
    }

    // what keeps the loop drawing without input
    bool animating() {
        // shortcut: an open picker keeps frames coming until it answers (seconds, while hidden)
        return touch::animating() ||
               gPickDone ||
               // imgui trickles queued input one event per frame: typed text needs frames
               (GImGui && GImGui->InputEventsQueue.Size > 0);
    }
} // namespace

namespace imshell {
    void SetAndroidApp(android_app* app) {
        gAndroid = app;
        if (!gRecreating)
            return;
        // the next activity after a recreation: same process, imgui context and egl context;
        // a new glue thread, looper, java activity and window
        gRecreating = false;
        gLightBars = -1;
        gKeyboardShown = false;
        gFieldKind = imshell::TextKind::Code;
        gEditMenu = false;
        gAndroid->onAppCmd = onCmd;
        gAndroid->onInputEvent = onInput;
        gAndroid->activity->vm->AttachCurrentThread(&gEnv, nullptr);
        registerWake();
        gDensity = std::max(AConfiguration_getDensity(gAndroid->config), 1) / 160.0f;
        while (!gAndroid->window) // APP_CMD_INIT_WINDOW makes the surface current here
            if (pump(-1))
                return; // gone again: run() returns at once
    }

    bool AndroidActivityRecreating() {
        return gRecreating;
    }

    android_app* AndroidApp() {
        return gAndroid;
    }

    void PickFile(const std::string& mimeTypes, const std::string& intoDir, bool multiple,
                  std::function<void(const std::vector<std::string>&)> done) {
        jmethodID m = method("pickFile", "(Ljava/lang/String;Ljava/lang/String;Z)V");
        if (!m || gPickDone)
            return;
        gPickDone = std::move(done);
        jstring types = gEnv->NewStringUTF(mimeTypes.c_str());
        jstring dir = gEnv->NewStringUTF(intoDir.c_str());
        gEnv->CallVoidMethod(gAndroid->activity->clazz, m, types, dir, static_cast<jboolean>(multiple));
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
            __android_log_print(ANDROID_LOG_ERROR, "imshell", "call SetAndroidApp first");
            return false;
        }
        gDelegate = &delegate;
        gVSync = config.vsync;
        gTitleBar = config.titleBar;
        gBarHeight = config.titleBarHeight > 0 ? config.titleBarHeight : 44;
        gAndroid->onAppCmd = onCmd;
        gAndroid->onInputEvent = onInput;
        gAndroid->activity->vm->AttachCurrentThread(&gEnv, nullptr);
        registerWake();
        gDensity = std::max(AConfiguration_getDensity(gAndroid->config), 1) / 160.0f;

        // the window arrives a few events after start
        while (!gAndroid->window)
            if (pump(-1))
                return false;
        if (!initEgl()) {
            __android_log_print(ANDROID_LOG_ERROR, "imshell", "egl setup failed");
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        if (config.docking)
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        detail::useIniFile(config);
        io.MouseDoubleClickMaxDist = 20.0f;
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_GetWindowWorkAreaInsets = [](ImGuiViewport*) { return detail::safeArea(); };
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
        ApplyTheme();
        ImGui_ImplAndroid_Init(gAndroid->window);
        io.BackendPlatformName = "imshell_android";
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
        readInsets();
        ImGuiIO& io = ImGui::GetIO();
        const ARect visible = visibleRect();
        detail::safeArea() = ImVec4(0, gInsets[1] / gDensity, 0, 0);
        io.DisplaySize = ImVec2(std::max(visible.right - visible.left, 1) / gDensity,
                                std::max(visible.bottom - visible.top, 1) / gDensity);
        io.DisplayFramebufferScale = ImVec2(gDensity, gDensity);
        const double now = nowSeconds();
        io.DeltaTime = gLastTime > 0 ? static_cast<float>(std::max(now - gLastTime, 1e-4))
                                     : 1.0f / 60.0f;
        gLastTime = now;

        pollChars();
        pollPicked();
        pollEdits();
        if (touch::longPressDue(now)) {
            imshell::PlayHaptic(imshell::Haptic::LongPress);
            // the finger has hovered since it landed, so the cursor says what is under it
            if (ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput) {
                // like an android text field: a double click selects the word (and focuses
                // the field), then the system text toolbar
                for (int i = 0; i < 2; ++i) {
                    io.AddMouseButtonEvent(0, true);
                    io.AddMouseButtonEvent(0, false);
                }
                const ImVec2 at = touch::start();
                const ARect r = visibleRect();
                showEditMenu(static_cast<int>(at.x * gDensity) + r.left,
                             static_cast<int>(at.y * gDensity) + r.top);
            } else { // anything else: a right click (context menus)
                touch::rightClick();
            }
        }
        touch::coast(io.DeltaTime);

        ImGui::GetStyle().TouchExtraPadding = ImVec2(Theme::Spacing::S, 0.0f);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        gDelegate->frame();
        touch::drawHandles();
        if (gKeyboardShown && gFieldKind != imshell::TextKind::Password)
            detail::drawComposingUnderline(callString("composing").c_str());
        ImGui::Render();

        // a text field wants the keyboard
        followTheme();
        bool& keyboard = gKeyboardShown;
        // PlatformImeData.WantVisible: a custom editor that wants the keyboard (ios does the same)
        const bool wants = io.WantTextInput || GImGui->PlatformImeData.WantVisible;
        const imshell::TextKind kind = wants ? imshell::ActiveTextKind() : imshell::TextKind::Code;
        // moving between fields of different kinds reopens the keyboard in the new kind
        if (wants != keyboard || kind != gFieldKind) {
            keyboard = wants;
            gFieldKind = kind;
            showKeyboard(keyboard, kind);
        }

        const int fullW = ANativeWindow_getWidth(gAndroid->window);
        const int fullH = ANativeWindow_getHeight(gAndroid->window);
        const ImVec4 base = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        // the frame covers the whole window with imgui's origin at the visible rect: the
        // system bars and the keyboard area stay clear
        ImDrawData* draw = ImGui::GetDrawData();
        draw->DisplayPos = ImVec2(-visible.left / gDensity, -visible.top / gDensity);
        draw->DisplaySize = ImVec2(fullW / gDensity, fullH / gDensity);
        // an unchanged frame is not drawn or swapped: the surface keeps showing the last one
        const bool mustDraw = std::exchange(gMustDraw, false);
        detail::frameSkipped() = detail::sameAsLastFrame(draw, base) && !mustDraw;
        if (!detail::frameSkipped()) {
            glViewport(0, 0, fullW, fullH);
            glClearColor(base.x, base.y, base.z, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(draw);
            eglSwapBuffers(gDisplay, gSurface);
        }
        detail::placeNativeViews();

        touch::takeLift(); // the finger is gone: nothing stays hovered
    }

    void App::run() {
        // requestFrames() from any thread wakes the looper (latched: a wake before the poll counts)
        detail::wakeLoopHook().store([] {
            if (gAndroid)
                ALooper_wake(gAndroid->looper);
        });
        while (!gDelegate->shouldQuit()) {
            // no surface (backgrounded, a picker on top): nothing drains, so sleep
            const bool drawing = gSurface != EGL_NO_SURFACE &&
                                 (gDelegate->wantsFrames() || animating());
            // shortcut: idle, a 1 s heartbeat frame for work on other threads that never asks
            // for frames; drop it once hosts always call requestFrames()
            // a skipped frame did not wait for vsync in eglSwapBuffers: wait about one refresh
            if (pump(drawing ? (detail::frameSkipped() ? 8 : 0) : 1000))
                break;
            renderFrame();
        }
        detail::wakeLoopHook().store(nullptr);
        // destroyed only to be recreated (a configuration change configChanges cannot cover,
        // such as an overlay or system colours): keep the process, imgui and the egl context,
        // let this glue thread go. the next android_main's SetAndroidApp picks them up
        if (gAndroid->destroyRequested) {
            jmethodID m = method("isChangingConfigurations", "()Z");
            gRecreating = m && gEnv->CallBooleanMethod(gAndroid->activity->clazz, m);
        }
        if (gRecreating) {
            dropSurface(); // the context is current on no thread now
            gAndroid->activity->vm->DetachCurrentThread();
            gEnv = nullptr;
            gAndroid = nullptr; // freed by the glue once android_main returns
        }
    }

    void App::destroy() {
        if (!gDelegate || gRecreating)
            return; // recreating: the next activity still needs all of it
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
        // below the status bar and clear of side cutouts (landscape)
        const ImVec4 safe = SafeArea();
        bar.pos = ImVec2(vp->Pos.x + safe.x, vp->Pos.y + safe.y);
        bar.size = ImVec2(vp->Size.x - safe.x - safe.z, TitleBarHeight());
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
        ImGui::SetCursorScreenPos(ImVec2(bar.pos.x + bar.leftInset,
                                         bar.pos.y + (bar.size.y - ImGui::GetFrameHeight()) * 0.5f));
        return bar;
    }

    void EndTitleBar() {
        ImGui::Dummy(ImVec2(0, 0)); // an empty bar: imgui wants an item after the cursor is set
        ImGui::End();
    }

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
} // namespace imshell

std::string imshell::detail::androidFilesDir() {
    return gAndroid && gAndroid->activity->internalDataPath ? gAndroid->activity->internalDataPath : "";
}

void imshell::PlayHaptic(const Haptic kind) {
    if (jmethodID m = method("haptic", "(I)V"))
        gEnv->CallVoidMethod(gAndroid->activity->clazz, m, static_cast<jint>(kind));
}

// ImShellActivity.readClipboard / writeClipboard: [mime, bytes, mime, bytes, ..., "" (next item)]
std::vector<imshell::Clipboard::Item> imshell::Clipboard::Read() {
    jmethodID m = method("readClipboard", "()[[B");
    if (!m)
        return {};
    auto parts = static_cast<jobjectArray>(gEnv->CallObjectMethod(gAndroid->activity->clazz, m));
    if (gEnv->ExceptionCheck() || !parts) {
        gEnv->ExceptionClear();
        return {};
    }
    auto bytesAt = [&](jsize i) {
        auto a = static_cast<jbyteArray>(gEnv->GetObjectArrayElement(parts, i));
        std::string out(static_cast<size_t>(gEnv->GetArrayLength(a)), '\0');
        gEnv->GetByteArrayRegion(a, 0, static_cast<jsize>(out.size()), reinterpret_cast<jbyte*>(out.data()));
        gEnv->DeleteLocalRef(a);
        return out;
    };
    std::vector<Item> items;
    Item item;
    const jsize n = gEnv->GetArrayLength(parts);
    for (jsize i = 0; i < n;) {
        const std::string mime = bytesAt(i);
        if (mime.empty()) {
            if (!item.data.empty())
                items.push_back(std::move(item));
            item = {};
            i += 1;
        } else {
            item.data[mime] = i + 1 < n ? bytesAt(i + 1) : "";
            i += 2;
        }
    }
    gEnv->DeleteLocalRef(parts);
    return items;
}

void imshell::Clipboard::Write(const std::vector<Item>& items) {
    jmethodID m = method("writeClipboard", "([[B)V");
    if (!m)
        return;
    std::vector<std::string> flat;
    for (const Item& item : items) {
        for (const auto& [mime, bytes] : item.data) {
            flat.push_back(mime);
            flat.push_back(bytes);
        }
        flat.emplace_back();
    }
    jclass bytesClass = gEnv->FindClass("[B");
    jobjectArray parts = gEnv->NewObjectArray(static_cast<jsize>(flat.size()), bytesClass, nullptr);
    for (size_t i = 0; i < flat.size(); ++i) {
        jbyteArray a = gEnv->NewByteArray(static_cast<jsize>(flat[i].size()));
        gEnv->SetByteArrayRegion(a, 0, static_cast<jsize>(flat[i].size()), reinterpret_cast<const jbyte*>(flat[i].data()));
        gEnv->SetObjectArrayElement(parts, static_cast<jsize>(i), a);
        gEnv->DeleteLocalRef(a);
    }
    gEnv->CallVoidMethod(gAndroid->activity->clazz, m, parts);
    if (gEnv->ExceptionCheck())
        gEnv->ExceptionClear(); // a clip the system refused
    gEnv->DeleteLocalRef(parts);
    gEnv->DeleteLocalRef(bytesClass);
}

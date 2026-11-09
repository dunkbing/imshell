// linux: a gtk4 window, opengl through a GtkGLArea, gtk input turned into imgui events, and
// gtk's frame clock as the loop. imgui coordinates are logical pixels (gtk's), the
// framebuffer is scaled by the widget's scale factor
#include <epoxy/gl.h> // before any other gl header

#include "mydear/app.hpp"
#include "../activity.hpp"
#include "../native_view.hpp"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "keymap.hpp"
#include "mydear/themes.hpp"
#include "mydear/title_bar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace mydear {
    namespace {
        constexpr double kIdleAfterSeconds = 2.0; // no input for this long: stop the frame clock

        Delegate* gDelegate = nullptr;
        GtkWidget* gWindow = nullptr;
        GtkWidget* gOverlay = nullptr; // the gl area, and native views as overlay children
        GtkWidget* gArea = nullptr;
        bool gTitleBar = false;
        float gBarHeight = 36;
        bool gShouldClose = false;
        bool gGlReady = false;
        double gMouseX = 0, gMouseY = 0;
        gint64 gLastInputUs = 0;
        bool gWasActive = false;
        bool gHadFrames = false;
        guint gTick = 0;
        ImGuiMouseCursor gCursor = ImGuiMouseCursor_COUNT;
        int gDark = -1;

        // the last button press, for a window move or the window menu started after the frame
        GdkEvent* gLastPress = nullptr;
        enum class BarAction { None, Drag, DoubleClick, Menu } gBarAction = BarAction::None;
        bool gAfterFrame = false; // a frame was drawn: place native views, run the bar action
        ImGuiWindow* gFocusBeforeBar = nullptr;

        // clipboard: gtk4 reads it asynchronously, imgui asks synchronously
        GdkClipboard* gClipboard = nullptr;
        std::string gClipboardText;
        bool gClipboardPending = false;

        void readClipboard() {
            if (!gClipboard || gClipboardPending)
                return;
            gClipboardPending = true;
            gdk_clipboard_read_text_async(
                gClipboard, nullptr,
                [](GObject* source, GAsyncResult* result, gpointer) {
                    gClipboardPending = false;
                    char* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, nullptr);
                    gClipboardText = text ? text : "";
                    g_free(text);
                },
                nullptr);
        }

        const char* getClipboard(ImGuiContext*) {
            readClipboard();
            const gint64 until = g_get_monotonic_time() + 200000; // a paste waits up to 200 ms
            while (gClipboardPending && g_get_monotonic_time() < until)
                g_main_context_iteration(nullptr, TRUE);
            return gClipboardText.c_str();
        }

        void setClipboard(ImGuiContext*, const char* text) {
            if (gClipboard && text)
                gdk_clipboard_set_text(gClipboard, text);
        }

        void ensureTick();

        void noteInput() {
            gLastInputUs = g_get_monotonic_time();
            ensureTick();
        }

        void queueRender() {
            if (gArea)
                gtk_gl_area_queue_render(GTK_GL_AREA(gArea));
        }

        // follow the frame clock while the user works or something animates; stop it when
        // idle (noteInput and a 200 ms timer start it again)
        gboolean onTick(GtkWidget*, GdkFrameClock*, gpointer) {
            if (gShouldClose) {
                gTick = 0;
                return G_SOURCE_REMOVE;
            }
            const bool active = gtk_window_is_active(GTK_WINDOW(gWindow));
            if (active && !gWasActive)
                gLastInputUs = g_get_monotonic_time();
            gWasActive = active;
            const bool busy = gDelegate && gDelegate->wantsFrames();
            const bool justIdle = gHadFrames && !busy; // one more frame after work ends
            gHadFrames = busy;
            const double since = (g_get_monotonic_time() - gLastInputUs) / 1e6;
            if (!busy && !justIdle && (!active || since >= kIdleAfterSeconds)) {
                gTick = 0;
                return G_SOURCE_REMOVE;
            }
            queueRender();
            return G_SOURCE_CONTINUE;
        }

        void ensureTick() {
            if (gTick == 0 && gArea)
                gTick = gtk_widget_add_tick_callback(gArea, onTick, nullptr, nullptr);
        }

        void addMods(GdkModifierType state) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, (state & GDK_CONTROL_MASK) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (state & GDK_SHIFT_MASK) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (state & GDK_ALT_MASK) != 0);
            io.AddKeyEvent(ImGuiMod_Super, (state & GDK_SUPER_MASK) != 0);
        }

        // the state gtk reports is from before this key: fold the key itself in
        GdkModifierType withKey(GdkModifierType state, guint keyval, bool down) {
            GdkModifierType mask{};
            switch (keyval) {
            case GDK_KEY_Shift_L:
            case GDK_KEY_Shift_R:
                mask = GDK_SHIFT_MASK;
                break;
            case GDK_KEY_Control_L:
            case GDK_KEY_Control_R:
                mask = GDK_CONTROL_MASK;
                break;
            case GDK_KEY_Alt_L:
            case GDK_KEY_Alt_R:
                mask = GDK_ALT_MASK;
                break;
            case GDK_KEY_Super_L:
            case GDK_KEY_Super_R:
                mask = GDK_SUPER_MASK;
                break;
            default:
                return state;
            }
            return static_cast<GdkModifierType>(down ? (state | mask) : (state & ~mask));
        }

        gboolean onKey(GdkModifierType state, guint keyval, bool down) {
            ImGuiIO& io = ImGui::GetIO();
            noteInput();
            addMods(withKey(state, keyval, down));
            if (const ImGuiKey key = detail::gtkKeyToImGuiKey(keyval); key != ImGuiKey_None)
                io.AddKeyEvent(key, down);
            if (down && !(state & GDK_CONTROL_MASK))
                if (const gunichar c = gdk_keyval_to_unicode(keyval); c != 0 && c < 0x10000)
                    io.AddInputCharacter(c);
            queueRender();
            if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab)
                return TRUE; // not gtk's focus chain
            return io.WantCaptureKeyboard;
        }

        int imguiButton(guint button) {
            return button == GDK_BUTTON_SECONDARY ? 1 : button == GDK_BUTTON_MIDDLE ? 2 : 0;
        }

        void onPress(GtkGestureClick* gesture, gint, gdouble, gdouble, gpointer) {
            noteInput();
            const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
            if (GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture))) {
                if (gLastPress)
                    gdk_event_unref(gLastPress);
                gLastPress = gdk_event_ref(event);
            }
            ImGui::GetIO().AddMouseButtonEvent(imguiButton(button), true);
            gtk_widget_grab_focus(gArea); // a click on imgui takes the keyboard back from a native view
            queueRender();
        }

        void onRelease(GtkGestureClick* gesture, gint, gdouble, gdouble, gpointer) {
            noteInput();
            const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
            ImGui::GetIO().AddMouseButtonEvent(imguiButton(button), false);
            queueRender();
        }

        void onMotion(GtkEventControllerMotion*, gdouble x, gdouble y, gpointer) {
            noteInput();
            gMouseX = x;
            gMouseY = y;
            ImGui::GetIO().AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
            queueRender();
        }

        gboolean onScroll(GtkEventControllerScroll* controller, gdouble dx, gdouble dy, gpointer) {
            noteInput();
            const GdkModifierType state =
                gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
            float x = static_cast<float>(-dx), y = static_cast<float>(-dy);
            if ((state & GDK_SHIFT_MASK) && std::fabs(y) < 1e-6f) // shift + wheel scrolls sideways
                std::swap(x, y);
            if (std::fabs(x) > 1e-6f || std::fabs(y) > 1e-6f)
                ImGui::GetIO().AddMouseWheelEvent(x, y);
            queueRender();
            return TRUE;
        }

        gboolean onDrop(GtkDropTarget*, const GValue* value, double, double, gpointer) {
            if (!gDelegate || !G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST))
                return FALSE;
            noteInput();
            for (auto* l = static_cast<GSList*>(g_value_get_boxed(value)); l; l = l->next)
                if (char* path = g_file_get_path(G_FILE(l->data))) {
                    gDelegate->openFile(path);
                    g_free(path);
                }
            gtk_window_present(GTK_WINDOW(gWindow));
            return TRUE;
        }

        const char* cursorName(ImGuiMouseCursor cursor) {
            switch (cursor) {
            case ImGuiMouseCursor_TextInput:
                return "text";
            case ImGuiMouseCursor_ResizeNS:
                return "ns-resize";
            case ImGuiMouseCursor_ResizeEW:
                return "ew-resize";
            case ImGuiMouseCursor_ResizeNESW:
                return "nesw-resize";
            case ImGuiMouseCursor_ResizeNWSE:
                return "nwse-resize";
            case ImGuiMouseCursor_Hand:
                return "pointer";
            case ImGuiMouseCursor_NotAllowed:
                return "not-allowed";
            default:
                return "default";
            }
        }

        // prefer-dark for gtk's own widgets (dialogs, native views) from the theme
        void followTheme() {
            const int dark = Theme::Current() && Theme::Current()->dark ? 1 : 0;
            if (dark == gDark)
                return;
            gDark = dark;
            g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", dark,
                         nullptr);
        }

        // after the frame: a click in empty bar space, now that imgui knows it hit no widget
        gboolean runBarAction(gpointer) {
            const BarAction action = gBarAction;
            gBarAction = BarAction::None;
            GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(gWindow));
            if (action == BarAction::DoubleClick) {
                if (gtk_window_is_maximized(GTK_WINDOW(gWindow)))
                    gtk_window_unmaximize(GTK_WINDOW(gWindow));
                else
                    gtk_window_maximize(GTK_WINDOW(gWindow));
            } else if (gLastPress && GDK_IS_TOPLEVEL(surface)) {
                if (action == BarAction::Drag) {
                    // the press in surface coordinates (the window has csd shadow margins)
                    double x = 0, y = 0, sx = 0, sy = 0;
                    gdk_event_get_position(gLastPress, &x, &y);
                    gtk_native_get_surface_transform(GTK_NATIVE(gWindow), &sx, &sy);
                    graphene_point_t in = GRAPHENE_POINT_INIT(static_cast<float>(x),
                                                              static_cast<float>(y));
                    graphene_point_t out;
                    if (gtk_widget_compute_point(gArea, gWindow, &in, &out)) {
                        x = out.x + sx;
                        y = out.y + sy;
                    }
                    gdk_toplevel_begin_move(GDK_TOPLEVEL(surface), gdk_event_get_device(gLastPress),
                                            GDK_BUTTON_PRIMARY, x, y,
                                            gdk_event_get_time(gLastPress));
                    ImGui::GetIO().AddMouseButtonEvent(0, false); // the compositor has the release
                } else if (action == BarAction::Menu) {
                    gdk_toplevel_show_window_menu(GDK_TOPLEVEL(surface), gLastPress);
                }
            }
            return G_SOURCE_REMOVE;
        }

        void onRealize(GtkGLArea* area, gpointer) {
            gtk_gl_area_make_current(area);
            if (gtk_gl_area_get_error(area)) {
                std::fprintf(stderr, "mydear: no opengl context\n");
                return;
            }
            gGlReady = ImGui_ImplOpenGL3_Init("#version 330");
            // layout changes are not allowed while gtk draws, and idle callbacks starve while the
            // frame clock runs: the frame's follow-up work goes right after its paint
            g_signal_connect(gtk_widget_get_frame_clock(GTK_WIDGET(area)), "after-paint",
                             G_CALLBACK(+[](GdkFrameClock*, gpointer) {
                                 if (!std::exchange(gAfterFrame, false))
                                     return;
                                 detail::placeNativeViews();
                                 if (gBarAction != BarAction::None)
                                     runBarAction(nullptr);
                             }),
                             nullptr);
        }

        gboolean onRender(GtkGLArea* area, GdkGLContext*, gpointer) {
            if (!gGlReady || !gDelegate)
                return FALSE;
            followTheme();
            ImGuiIO& io = ImGui::GetIO();
            const int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
            const int w = gtk_widget_get_width(GTK_WIDGET(area));
            const int h = gtk_widget_get_height(GTK_WIDGET(area));
            io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
            io.DisplayFramebufferScale = ImVec2(static_cast<float>(scale), static_cast<float>(scale));
            static gint64 last = g_get_monotonic_time();
            const gint64 now = g_get_monotonic_time();
            io.DeltaTime = std::max(static_cast<float>(now - last) / 1e6f, 1e-4f);
            last = now;

            const ImVec4 clear = Theme::Current()->colors.base;
            glViewport(0, 0, w * scale, h * scale);
            glClearColor(clear.x, clear.y, clear.z, clear.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            gDelegate->frame();
            ImGui::Render();
            if (const ImGuiMouseCursor cursor = ImGui::GetMouseCursor(); cursor != gCursor) {
                // gtk loads a cursor image per call: only on change
                gtk_widget_set_cursor_from_name(gArea, cursorName(cursor));
                gCursor = cursor;
            }
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (detail::isImGuiUserActive())
                noteInput();
            gAfterFrame = true; // native views and bar actions: in after-paint, see onRealize
            return TRUE;
        }

        // the window buttons from gtk-decoration-layout ("appmenu:minimize,maximize,close"):
        // before the colon on the left, after it on the right
        struct Layout {
            std::string left, right;
        };

        Layout decorationLayout() {
            char* value = nullptr;
            g_object_get(gtk_settings_get_default(), "gtk-decoration-layout", &value, nullptr);
            std::string s = value ? value : ":minimize,maximize,close";
            g_free(value);
            const auto colon = s.find(':');
            return colon == std::string::npos ? Layout{"", s}
                                              : Layout{s.substr(0, colon), s.substr(colon + 1)};
        }

        constexpr float kButtonSide = 24; // gnome's round window buttons
        constexpr float kButtonGap = 8;

        int countButtons(const std::string& part) {
            int n = 0;
            for (const char* name : {"minimize", "maximize", "close"})
                if (part.find(name) != std::string::npos)
                    n++;
            return n;
        }

        void windowButtons(const std::string& part, float x, float centreY) {
            const auto& c = Theme::Current()->colors;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 text = ImGui::GetColorU32(c.text);
            size_t pos = 0;
            while (pos <= part.size()) {
                const size_t comma = part.find(',', pos);
                const std::string name = part.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                pos = comma == std::string::npos ? part.size() + 1 : comma + 1;
                if (name != "minimize" && name != "maximize" && name != "close")
                    continue;
                const ImVec2 min(x, centreY - kButtonSide / 2);
                ImGui::SetCursorScreenPos(min);
                const bool clicked = ImGui::InvisibleButton(("##mydear_" + name).c_str(),
                                                            ImVec2(kButtonSide, kButtonSide));
                const ImVec2 m(x + kButtonSide / 2, centreY);
                const bool hot = ImGui::IsItemHovered();
                dl->AddCircleFilled(m, kButtonSide / 2,
                                    ImGui::GetColorU32(hot ? c.surface1 : c.surface0));
                if (name == "minimize") {
                    dl->AddLine({m.x - 4, m.y + 3}, {m.x + 4, m.y + 3}, text, 1.5f);
                    if (clicked)
                        gtk_window_minimize(GTK_WINDOW(gWindow));
                } else if (name == "maximize") {
                    dl->AddRect({m.x - 4, m.y - 4}, {m.x + 4, m.y + 4}, text, 0, 0, 1.5f);
                    if (clicked)
                        g_idle_add(
                            [](gpointer) -> gboolean {
                                if (gtk_window_is_maximized(GTK_WINDOW(gWindow)))
                                    gtk_window_unmaximize(GTK_WINDOW(gWindow));
                                else
                                    gtk_window_maximize(GTK_WINDOW(gWindow));
                                return G_SOURCE_REMOVE;
                            },
                            nullptr);
                } else {
                    dl->AddLine({m.x - 4, m.y - 4}, {m.x + 4, m.y + 4}, text, 1.5f);
                    dl->AddLine({m.x + 4, m.y - 4}, {m.x - 4, m.y + 4}, text, 1.5f);
                    if (clicked)
                        gShouldClose = true;
                }
                x += kButtonSide + kButtonGap;
            }
        }
    } // namespace

    struct App::Impl {
        bool vsync = true; // gtk paces frames with the display; kept for the api
        int maxFps = 120;
    };

    App::App() : impl_(std::make_unique<Impl>()) {}

    App::~App() {
        destroy();
    }

    bool App::create(const Config& config, Delegate& delegate) {
        if (!gtk_init_check()) {
            std::fprintf(stderr, "mydear: gtk_init_check failed\n");
            return false;
        }
        gDelegate = &delegate;
        gTitleBar = config.titleBar;
        gBarHeight = config.titleBarHeight;
        gShouldClose = false;

        gWindow = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(gWindow), config.title.c_str());
        gtk_window_set_default_size(GTK_WINDOW(gWindow), config.width, config.height);
        if (config.titleBar) // an empty titlebar: no header bar, still csd edges and shadow
            gtk_window_set_titlebar(GTK_WINDOW(gWindow), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
        g_signal_connect(gWindow, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer) -> gboolean {
                             gShouldClose = true;
                             return TRUE;
                         }),
                         nullptr);

        gArea = gtk_gl_area_new();
        gtk_gl_area_set_required_version(GTK_GL_AREA(gArea), 3, 3);
        gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(gArea), FALSE);
        gtk_gl_area_set_has_stencil_buffer(GTK_GL_AREA(gArea), FALSE);
        gtk_gl_area_set_auto_render(GTK_GL_AREA(gArea), FALSE); // only queued frames
        gtk_widget_set_hexpand(gArea, TRUE);
        gtk_widget_set_vexpand(gArea, TRUE);
        gtk_widget_set_focusable(gArea, TRUE);
        g_signal_connect(gArea, "realize", G_CALLBACK(onRealize), nullptr);
        g_signal_connect(gArea, "render", G_CALLBACK(onRender), nullptr);
        g_signal_connect(gArea, "resize",
                         G_CALLBACK(+[](GtkGLArea* area, gint, gint, gpointer) {
                             gtk_gl_area_queue_render(area); // no blank frame while resizing
                         }),
                         nullptr);

        GtkEventController* keys = gtk_event_controller_key_new();
        gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
        g_signal_connect(keys, "key-pressed",
                         G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint,
                                        GdkModifierType state, gpointer) {
                             return onKey(state, keyval, true);
                         }),
                         nullptr);
        g_signal_connect(keys, "key-released",
                         G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint,
                                        GdkModifierType state, gpointer) {
                             onKey(state, keyval, false);
                         }),
                         nullptr);
        gtk_widget_add_controller(gArea, keys);
        GtkEventController* motion = gtk_event_controller_motion_new();
        g_signal_connect(motion, "motion", G_CALLBACK(onMotion), nullptr);
        gtk_widget_add_controller(gArea, motion);
        GtkGesture* click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        g_signal_connect(click, "pressed", G_CALLBACK(onPress), nullptr);
        g_signal_connect(click, "released", G_CALLBACK(onRelease), nullptr);
        gtk_widget_add_controller(gArea, GTK_EVENT_CONTROLLER(click));
        GtkEventController* scroll =
            gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
        g_signal_connect(scroll, "scroll", G_CALLBACK(onScroll), nullptr);
        gtk_widget_add_controller(gArea, scroll);
        GtkDropTarget* drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
        g_signal_connect(drop, "drop", G_CALLBACK(onDrop), nullptr);
        gtk_widget_add_controller(gArea, GTK_EVENT_CONTROLLER(drop));

        gOverlay = gtk_overlay_new();
        // overlay children sit exactly where imgui put them, even below their minimum size: the
        // clip box clips
        g_signal_connect(gOverlay, "get-child-position",
                         G_CALLBACK(+[](GtkOverlay*, GtkWidget* child, GdkRectangle* out, gpointer) {
                             auto* r = static_cast<GdkRectangle*>(
                                 g_object_get_data(G_OBJECT(child), "mydear-rect"));
                             if (r)
                                 *out = *r;
                             return r != nullptr;
                         }),
                         nullptr);
        gtk_overlay_set_child(GTK_OVERLAY(gOverlay), gArea);
        gtk_window_set_child(GTK_WINDOW(gWindow), gOverlay);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        if (config.docking)
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendPlatformName = "mydear_gtk4";
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        gClipboard = gdk_display_get_clipboard(gdk_display_get_default());
        g_signal_connect(gClipboard, "changed", G_CALLBACK(+[](GdkClipboard*, gpointer) {
                             readClipboard();
                         }),
                         nullptr);
        readClipboard();
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_GetClipboardTextFn = getClipboard;
        pio.Platform_SetClipboardTextFn = setClipboard;
        ImGui::StyleColorsDark();
        Theme::ApplyTheme(Theme::Current()->colors);
        delegate.setup();
        followTheme();
        gLastInputUs = g_get_monotonic_time();
        return true;
    }

    bool App::pollEvents() {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
        return !gShouldClose;
    }

    void App::renderFrame() {
        queueRender();
        pollEvents();
    }

    void App::run() {
        gtk_window_present(GTK_WINDOW(gWindow));
        gWasActive = gtk_window_is_active(GTK_WINDOW(gWindow));
        noteInput();
        // an idle app still wakes this often, to notice work that wants frames
        const guint timer = g_timeout_add(
            200,
            [](gpointer) -> gboolean {
                if (gDelegate && gDelegate->wantsFrames())
                    ensureTick();
                return G_SOURCE_CONTINUE;
            },
            nullptr);
        while (!gShouldClose) {
            if (gDelegate->shouldQuit())
                gShouldClose = true;
            else
                g_main_context_iteration(nullptr, TRUE);
        }
        g_source_remove(timer);
        if (gTick) {
            gtk_widget_remove_tick_callback(gArea, gTick);
            gTick = 0;
        }
    }

    void App::destroy() {
        if (!gWindow)
            return;
        detail::dropNativeViews();
        if (gGlReady) {
            gtk_gl_area_make_current(GTK_GL_AREA(gArea));
            ImGui_ImplOpenGL3_Shutdown();
            gGlReady = false;
        }
        ImGui::DestroyContext();
        if (gClipboard)
            g_signal_handlers_disconnect_matched(gClipboard, G_SIGNAL_MATCH_DATA, 0, 0, nullptr,
                                                 nullptr, nullptr);
        gClipboard = nullptr;
        if (gLastPress) {
            gdk_event_unref(gLastPress);
            gLastPress = nullptr;
        }
        gtk_window_destroy(GTK_WINDOW(gWindow));
        gWindow = gOverlay = gArea = nullptr;
        gDelegate = nullptr;
    }

    float TitleBarHeight() {
        return gTitleBar ? gBarHeight : 0;
    }

    TitleBar BeginTitleBar() {
        if (ImGuiWindow* nav = GImGui->NavWindow;
            nav && nav != ImGui::FindWindowByName("##mydear_title_bar"))
            gFocusBeforeBar = nav;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        TitleBar bar;
        bar.pos = vp->Pos;
        bar.size = ImVec2(vp->Size.x, TitleBarHeight());
        const Layout layout = decorationLayout();
        const auto inset = [](const std::string& part) {
            const int n = countButtons(part);
            return n ? Theme::Spacing::M + n * (kButtonSide + kButtonGap) : Theme::Spacing::M;
        };
        bar.leftInset = inset(layout.left);
        bar.rightInset = inset(layout.right);

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
        const float centreY = bar.pos.y + bar.size.y * 0.5f;
        windowButtons(layout.left, bar.pos.x + Theme::Spacing::M, centreY);
        windowButtons(layout.right,
                      bar.pos.x + bar.size.x - bar.rightInset + kButtonGap, centreY);
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
            else if (ImGui::IsMouseClicked(1))
                gBarAction = BarAction::Menu;
        }
        ImGui::End();
    }

    void App::setVSync(const bool on) {
        impl_->vsync = on;
    }

    void App::setMaxFps(const int fps) {
        impl_->maxFps = std::max(0, fps);
    }

    ImTextureID App::createTexture(const uint8_t* rgba, const int width, const int height) {
        if (!rgba || !gGlReady)
            return ImTextureID{};
        gtk_gl_area_make_current(GTK_GL_AREA(gArea));
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
        return nullptr; // gtk, not glfw
    }

    void* App::nativeWindow() const {
        return gWindow;
    }

    void* App::nativeView() const {
        return gArea;
    }

    // frame capture and recording are macos-only so far
    void requestFrameCapture(const char*, float) {}
    bool frameCapturePending() {
        return false;
    }
    bool startRecording(const char*, float, int, int) {
        return false;
    }
    void stopRecording() {}

    void* detail::nativeHost() {
        return gOverlay;
    }

    // native views: each one an overlay child (a clipping GtkFixed holding the view), so the
    // rest of the overlay stays click-through to the gl area
    namespace detail {
        void* nativeAttach(void* made) {
            GtkWidget* view = static_cast<GtkWidget*>(made);
            GtkWidget* clip = gtk_fixed_new();
            gtk_widget_set_overflow(clip, GTK_OVERFLOW_HIDDEN);
            gtk_widget_set_visible(clip, FALSE); // added mid-draw: no allocation until placed
            g_object_set_data_full(G_OBJECT(clip), "mydear-rect", g_new0(GdkRectangle, 1), g_free);
            gtk_fixed_put(GTK_FIXED(clip), view, 0, 0);
            gtk_overlay_add_overlay(GTK_OVERLAY(gOverlay), clip);
            return clip;
        }

        void nativePlace(void* clip, void* view, const ImRect& visible, const ImRect& full) {
            auto* c = static_cast<GtkWidget*>(clip);
            auto* r = static_cast<GdkRectangle*>(g_object_get_data(G_OBJECT(c), "mydear-rect"));
            *r = {static_cast<int>(visible.Min.x), static_cast<int>(visible.Min.y),
                  static_cast<int>(visible.GetWidth()), static_cast<int>(visible.GetHeight())};
            gtk_widget_queue_allocate(gOverlay);
            gtk_fixed_move(GTK_FIXED(c), static_cast<GtkWidget*>(view), full.Min.x - visible.Min.x,
                           full.Min.y - visible.Min.y);
            gtk_widget_set_size_request(static_cast<GtkWidget*>(view),
                                        static_cast<int>(full.GetWidth()),
                                        static_cast<int>(full.GetHeight()));
        }

        void nativeSetHidden(void* clip, const bool hidden) {
            gtk_widget_set_visible(static_cast<GtkWidget*>(clip), !hidden);
        }

        void nativeRemove(void* clip) {
            if (gOverlay)
                gtk_overlay_remove_overlay(GTK_OVERLAY(gOverlay), static_cast<GtkWidget*>(clip));
        }
    } // namespace detail
} // namespace mydear

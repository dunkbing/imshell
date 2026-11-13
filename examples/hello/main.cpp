// the smallest imshell app: a themed window with its own title bar and a button
#include "imshell/app.hpp"
#include "imshell/button.hpp"
#include "imshell/file_dialog.hpp"
#include "imshell/fonts.hpp"
#include "imshell/themes.hpp"
#include "imshell/title_bar.hpp"
#include "imshell/native_view.hpp"
#include "imshell/notifications.hpp"
#include "imshell/text_input.hpp"

#if defined(__APPLE__) || defined(_WIN32)
#include "imgui_impl_glfw.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

void* makeTextField();
void* makeDatePicker();
void describeNativeView(void* view, char* out, int size);
bool closeLikeUser(); // false where the platform cannot

struct Hello : imshell::Delegate {
    int clicks = 0;
    char typed[256] = ""; // an imgui field: the platform's input methods and keyboard type into it
    char prose[256] = "", code[16] = "", password[64] = ""; // the other kinds of field
    std::string notified; // what the notifications did
    // smoke tests: IMSHELL_HELLO_QUIT=<seconds> closes by itself (0: right after the first frame,
    // which site/bench.sh times), IMSHELL_HELLO_PROBE=1 drives the window (title bar zoom, a bar
    // button, native views, the close veto)
    const char* quitAfter = std::getenv("IMSHELL_HELLO_QUIT");
    const bool probe = std::getenv("IMSHELL_HELLO_PROBE");
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ImVec2 sizeBefore;
    ImVec2 barButton;
    float barEnd = 0; // the bar's free end, before the os buttons (windows draws them there)
    int barClicks = 0;
    void* field = nullptr;
    bool wantPopup = false;
    std::string opened;

    // a system font with vietnamese and the other latin scripts, plus the system's chinese,
    // japanese and korean font (AddFont merges it), so typed text shows as typed; imgui's own
    // font has latin-1 only
    std::string fontData; // the atlas reads it in place
    void setup() override {
        for (const char* path : {"/System/Library/Fonts/Supplemental/Arial.ttf", "C:\\Windows\\Fonts\\arial.ttf",
                                 "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"})
            if (std::FILE* f = std::fopen(path, "rb")) {
                char buf[65536];
                for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
                    fontData.append(buf, n);
                std::fclose(f);
                imshell::AddFont({{fontData.data(), fontData.size()}}, 15.0f);
                return;
            }
    }

    void frame() override {
        const int f = ImGui::GetFrameCount();
        if (probe)
            imshell::requestFrames(); // the probe counts frames: no idling
        if (probe)
            runProbe(f);

        const auto bar = imshell::BeginTitleBar();
        barEnd = bar.pos.x + bar.size.x - bar.rightInset;
        if (imshell::SmallButton("Bar button"))
            ++barClicks;
        barButton = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2,
                           (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2);
        const char* title = "Hello imshell";
        ImGui::SetCursorScreenPos(ImVec2(bar.pos.x + (bar.size.x - ImGui::CalcTextSize(title).x) / 2,
                                         bar.pos.y + (bar.size.y - ImGui::GetTextLineHeight()) / 2));
        ImGui::TextDisabled("%s", title);
        imshell::EndTitleBar();

        const imshell::Rect content = imshell::ContentRect();
        ImGui::SetNextWindowPos(content.pos);
        ImGui::SetNextWindowSize(content.size);
        ImGui::Begin("hello", nullptr, ImGuiWindowFlags_NoDecoration);
        if (imshell::Button("Click me", imshell::ButtonVariant::Primary))
            ++clicks;
        ImGui::Text("Clicked %d times", clicks);
        ImGui::SetNextItemWidth(320);
        ImGui::InputTextWithHint("##typed", "Type here, in any language", typed, sizeof typed);
        ImGui::InputTextWithHint("##prose", "Prose: spell check, autocorrect", prose, sizeof prose);
        imshell::SetTextKind(imshell::TextKind::Prose);
        ImGui::InputTextWithHint("##code", "One-time code", code, sizeof code, ImGuiInputTextFlags_CharsDecimal);
        imshell::SetTextKind(imshell::TextKind::OneTimeCode);
        ImGui::InputTextWithHint("##password", "Password", password, sizeof password, ImGuiInputTextFlags_Password);
        ImGui::Spacing();
        ImGui::TextUnformatted("Native views, placed by the imgui layout:");
        const ImVec2 fieldsAt = ImGui::GetCursorScreenPos();
        field = imshell::NativeView("##field", ImVec2(240, 22), makeTextField);
        imshell::NativeView("##date", ImVec2(160, 22), makeDatePicker);
        if (imshell::SmallButton("Open files..."))
            imshell::OpenDialog({.filters = {{"Text", "txt,md"}, {"All files", "*"}}, .multiple = true},
                               [this](const std::vector<std::string>& paths) {
                                   opened.clear();
                                   for (const std::string& p : paths)
                                       opened += (opened.empty() ? "" : "\n") + p;
                               });
        if (!opened.empty())
            ImGui::TextDisabled("%s", opened.c_str());
        if (imshell::SmallButton("Notify me")) {
            namespace n = imshell::notifications;
            n::RequestPermission([this](n::Permission p) {
                notified = "permission " + std::to_string(static_cast<int>(p));
                n::Show({.id = "hello", .title = "Hello imshell", .body = "Click to come back"},
                        [this](bool shown) { notified += shown ? ", shown" : ", not shown"; });
                n::SetBadge(1);
            });
        }
        if (!notified.empty())
            ImGui::TextDisabled("%s", notified.c_str());
        if (imshell::SmallButton("Open a popup over them") || std::exchange(wantPopup, false))
            ImGui::OpenPopup("##over");
        ImGui::SetNextWindowPos(ImVec2(fieldsAt.x + 40, fieldsAt.y + 4), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##over")) {
            ImGui::TextUnformatted("An imgui popup: the native views hide under it");
            ImGui::EndPopup();
        }
        ImGui::End();
    }

    void runProbe(int f) {
        auto& io = ImGui::GetIO();
        const ImVec2 empty(barEnd - 40, imshell::TitleBarHeight() / 2);
        if (f == 10)
            sizeBefore = io.DisplaySize;
        if (f >= 10 && f <= 15 || f >= 50 && f <= 53) {
#if defined(__APPLE__) || defined(_WIN32)
            // the backend reads the real cursor while it thinks the pointer is outside
            ImGui_ImplGlfw_CursorEnterCallback(static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle), 1);
#endif
            io.AddMousePosEvent(f < 50 ? empty.x : barButton.x, f < 50 ? empty.y : barButton.y);
        }
        if (f == 51)
            io.AddMouseButtonEvent(0, true);
        if (f == 52)
            io.AddMouseButtonEvent(0, false);
        if (f == 60) {
            std::printf("probe: bar button clicked %d time(s)\n", barClicks);
            check(barClicks == 1, "bar button");
        }
        // the native field: placed where the layout put it, hidden under a popup
        char where[64];
        if (f == 70 && field) {
            describeNativeView(field, where, sizeof where);
            std::printf("probe: native field %s\n", where);
            check(std::strstr(where, "shown"), "native field shown");
            wantPopup = true;
        }
        if (f == 80 && field) {
            describeNativeView(field, where, sizeof where);
            std::printf("probe: under a popup %s\n", where);
            check(std::strstr(where, "hidden"), "native field hidden under a popup");
        }
        if (f == 11 || f == 13)
            io.AddMouseButtonEvent(0, true);
        if (f == 12 || f == 14)
            io.AddMouseButtonEvent(0, false);
        // close like the user: the first is vetoed (closeRequested), the second goes through
        if (f == 90)
            canClose = closeLikeUser();
        if (f == 95 && canClose)
            check(closeAsks == 1, "close asked once and vetoed");
        if (f == 98)
            std::printf("probe: %s%s\n", failed.empty() ? "ok" : "FAILED:", failed.c_str());
        if (f == 100 && canClose)
            closeLikeUser();
        if (f == 40) {
            std::printf("probe: %.0fx%.0f -> %.0fx%.0f (%s)\n", sizeBefore.x, sizeBefore.y,
                        io.DisplaySize.x, io.DisplaySize.y,
                        io.DisplaySize.x != sizeBefore.x ? "zoomed" : "NOT zoomed");
            check(io.DisplaySize.x != sizeBefore.x, "double-click zooms");
        }
    }

    std::string failed; // the probe's failed checks
    bool canClose = false;
    void check(bool ok, const char* what) {
        if (!ok)
            failed += std::string(" ") + what + ";";
    }

    int closeAsks = 0;
    bool closeRequested() override {
        ++closeAsks;
        std::printf("probe: close asked (%d)\n", closeAsks);
        return !probe || closeAsks > 1;
    }

    bool shouldQuit() override {
        const bool quit = quitAfter && ImGui::GetFrameCount() > 0 &&
                          std::chrono::steady_clock::now() - start >=
                              std::chrono::seconds(std::atoi(quitAfter));
        if (quit)
            std::printf("hello: %d frames\n", ImGui::GetFrameCount());
        return quit;
    }
};

int main() {
    for (const imshell::Theme& t : imshell::Themes())
        if (std::string_view(t.id) == "tokyo-night")
            imshell::SetTheme(t);
    imshell::Config config;
    config.title = "Hello imshell";
    config.appName = "imshell hello"; // imgui.ini in DataDir("imshell hello")
    config.width = 480;
    config.height = 300;
    config.titleBar = true;
    static Hello hello; // android: outlives a recreated activity
    // a click on the notification (set before the loop: one that launched the app waits for it)
    imshell::notifications::OnPress([](const std::string& id) {
        hello.notified = "pressed " + id;
        imshell::notifications::SetBadge(0);
    });
    return imshell::Run(config, hello);
}

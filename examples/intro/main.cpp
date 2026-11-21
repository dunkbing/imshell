// imshell's own intro, rendered by imshell::video: the components gallery (real components, real themes) and its editor,
// driven by the frame number, plus a few title cards.
//   imshell_intro render -o intro.mp4 [--ffmpeg] [IMSHELL_INTRO_MUSIC=file.wav]   (imshell_intro help for the rest)
#define IMSHELL_GALLERY_NO_MAIN
#include "../gallery/main.cpp"

#include "imshell/video_cli.hpp"
#include "imshell/video_scenes.hpp"

#include <cmath>

namespace {
    using namespace imshell::video;
    constexpr float W = 1920.f, H = 1080.f;

    void useTheme(std::string_view id) {
        for (const auto& theme : imshell::Themes())
            if (std::string_view(theme.id) == id)
                imshell::SetTheme(theme);
    }

    // a draw-list colour with the scene's fade applied
    ImU32 faded(ImVec4 c, float a = 1.f) {
        c.w *= a;
        return ImGui::GetColorU32(c);
    }

    float textWidth(const char* s, float size) {
        return ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.f, s).x;
    }

    void text(const char* s, float x, float y, float size, ImU32 color) {
        ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), size, ImVec2(x, y), color, s);
    }

    void centered(const char* s, float y, float size, ImU32 color) {
        text(s, (W - textWidth(s, size)) * 0.5f, y, size, color);
    }

    float ease(const Frame& f, double a, double b, float from, float to, Ease e = Ease::OutCubic) {
        return Interpolate(f.position(), {float(a), float(b)}, {from, to}, e);
    }

    // a dark card background with a soft pair of glows
    void backdrop(std::string_view theme) {
        useTheme(theme);
        const imshell::Theme& t = imshell::CurrentTheme();
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), faded(t.background));
    }

    // a caption pill at the top, in the theme's colours
    void caption(const char* s, const Frame& f, double in = 6) {
        const imshell::Theme& t = imshell::CurrentTheme();
        const float a = ease(f, in, in + 12, 0.f, 1.f);
        const float size = 34.f, w = textWidth(s, size) + 64.f, x = (W - w) * 0.5f, y = 34.f - (1.f - a) * 14.f;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 60), faded(t.primary, a), 30.f);
        text(s, x + 32.f, y + 10.f, size, faded(t.primaryForeground, a));
    }

    // the gallery page in a window, scrolled from `from` to `to` over the scene
    void page(const Frame& f, float from, float to, float top = 120.f, bool trackScroll = true) {
        ImGui::SetNextWindowPos(ImVec2(90, top));
        ImGui::SetNextWindowSize(ImVec2(W - 180, H - top - 36));
        ImGui::SetNextWindowScroll(ImVec2(0, trackScroll ? Interpolate(f.position(), {0.f, float(f.frames - 1)}, {from, to}, Ease::InOutCubic) : from));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 22));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.f);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.7f); // the page is laid out for a ~1000 px window; zoom it for a 1080p frame
        ImGui::Begin("gallery", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);
        gallery.shot = true;
        gallery.body();
        ImGui::End();
        ImGui::PopFont();
    }

    const char* kCode = "// highlighting and completion are hooks the app fills\n"
                        "let total = 0\n"
                        "for (let i = 0; i < 10; i = i + 1) {\n"
                        "    total = add(total, i)\n"
                        "}\n"
                        "print(\"total\", total)\n";
} // namespace

int main(int argc, char** argv) {
    std::vector<Scene> scenes;

    scenes.push_back({"title", 80, 0, [](const Frame& f) {
        backdrop("tokyo-night");
        const imshell::Theme& t = imshell::CurrentTheme();
        const float a = ease(f, 0, 16, 0.f, 1.f);
        const float rise = SpringIn(f.position(), f.fps, 40.f, 0.f, 0, 30);
        centered("imshell", 330.f + rise, 190.f, faded(t.foreground, a));
        centered("a toolkit for apps made with Dear ImGui", 560.f, 52.f, faded(t.primary, ease(f, 22, 40, 0.f, 1.f)));
        centered("macOS  Windows  Linux  iOS  Android  web", 680.f, 34.f, faded(t.mutedForeground, ease(f, 40, 58, 0.f, 1.f)));
    }});

    // the same page in four palettes, switched like the picker would: the theme is global, so these are cuts inside one scene
    scenes.push_back({"themes", 200, 0, [](const Frame& f) {
        static const char* const ids[] = {"tokyo-night", "one-light", "catppuccin-mocha", "gruvbox-dark", "tokyo-night"};
        backdrop(ids[std::min(4, f.index / 40)]);
        page(f, 0.f, 120.f, 120.f);
        caption("Eighteen themes, one line to switch", f);
        // dip through black at each switch so the change reads as a transition, not a glitch
        float dip = 0.f;
        for (int cut = 40; cut <= 160; cut += 40)
            dip = std::max(dip, 1.f - std::abs(float(f.index - cut)) / 3.f);
        if (dip > 0.f)
            ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(8, 8, 14, int(255 * dip)));
    }});

    scenes.push_back({"form", 140, 0, [](const Frame& f) {
        backdrop("tokyo-night");
        const std::string email = "ada@example.com";
        const int typed = static_cast<int>(Interpolate(f.position(), {10.f, 70.f}, {0.f, float(email.size())}, Ease::Linear));
        gallery.email = email.substr(0, static_cast<size_t>(std::max(0, typed)));
        for (int i = 0; i < 5; ++i)
            gallery.chips[i] = f.index > 80 + i * 8 ? (i % 2 == 0) : gallery.chips[i];
        gallery.loading = f.index < 110;
        page(f, 0.f, 360.f, 120.f);
        caption("Cards, fields, chips, badges, progress", f);
    }});

    scenes.push_back({"editor", 130, 0, [](const Frame& f) {
        backdrop("catppuccin-mocha");
        const std::string code = kCode;
        const int typed = static_cast<int>(Interpolate(f.position(), {10.f, 100.f}, {0.f, float(code.size())}, Ease::Linear));
        gallery.editor.SetText(code.substr(0, static_cast<size_t>(std::max(0, typed))));
        page(f, 620.f, 620.f, 120.f, false);
        caption("A code editor with highlighting and completion hooks", f);
    }});

    scenes.push_back({"native", 120, 0, [](const Frame& f) {
        backdrop("tokyo-night");
        const imshell::Theme& t = imshell::CurrentTheme();
        centered("Native where it counts", 110.f, 80.f, faded(t.foreground, ease(f, 0, 10, 0.f, 1.f)));
        // the system's own symbols (SF Symbols here), drawn through imshell::Icon
        static const char* const names[] = {"gearshape", "trash", "magnifyingglass", "plus", "folder", "doc.on.doc",
                                            "arrow.clockwise", "square.and.arrow.up", "star", "heart", "lock", "bell"};
        for (int i = 0; i < 12; ++i) {
            const float a = ease(f, 8 + i * 2, 20 + i * 2, 0.f, 1.f);
            const float x = 150.f + (i % 6) * 270.f, y = 270.f + (i / 6) * 150.f + (1.f - a) * 24.f;
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 230.f, y + 120.f), faded(t.card, a), 20.f);
            dl->AddRect(ImVec2(x, y), ImVec2(x + 230.f, y + 120.f), faded(t.border, a), 20.f, 0, 2.f);
            const char* glyph = imshell::Icon(names[i], "?");
            dl->AddText(ImGui::GetFont(), 56.f, ImVec2(x + (230.f - textWidth(glyph, 56.f)) * 0.5f, y + 16.f), faded(t.primary, a), glyph);
            text(names[i], x + (230.f - textWidth(names[i], 18.f)) * 0.5f, y + 86.f, 18.f, faded(t.mutedForeground, a));
        }
        const char* lines[] = {"a real NSView, WinUI control or GtkWidget inside the layout",
                               "native title bar buttons, menus, tray icon and notifications",
                               "the system font and symbols, with your own theme on top"};
        for (int i = 0; i < 3; ++i)
            centered(lines[i], 620.f + i * 62.f, 38.f, faded(i == 0 ? t.foreground : t.mutedForeground, ease(f, 40 + i * 14, 56 + i * 14, 0.f, 1.f)));
        centered("the symbols above are the real ones; this video draws ImGui stand-ins for the OS controls", 960.f, 24.f, faded(t.mutedForeground, 0.7f * ease(f, 70, 86, 0.f, 1.f)));
    }});

    // measured with `imshell_intro bench` on this video (1080p, full components gallery, Apple M4)
    scenes.push_back({"speed", 110, 0, [](const Frame& f) {
        backdrop("tokyo-night");
        const imshell::Theme& t = imshell::CurrentTheme();
        centered("Fast, and small", 110.f, 80.f, faded(t.foreground, ease(f, 0, 10, 0.f, 1.f)));
        struct Stat { float value; const char* unit; const char* label; ImVec4 color; };
        const Stat stats[] = {{1270.f, " fps", "drawing this page", t.primary}, {227.f, " fps", "rendering it to mp4", t.primary}, {145.f, " MB", "peak memory", t.mutedForeground}};
        for (int i = 0; i < 3; ++i) {
            const float start = 8.f + i * 10.f, p = ease(f, start, start + 36.f, 0.f, 1.f), a = ease(f, start, start + 10.f, 0.f, 1.f);
            const float x = 120.f + i * 600.f, y = 330.f;
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 560.f, y + 360.f), faded(t.card, a), 28.f);
            dl->AddRect(ImVec2(x, y), ImVec2(x + 560.f, y + 360.f), faded(t.border, a), 28.f, 0, 2.f);
            char num[32];
            std::snprintf(num, sizeof num, "%d%s", int(stats[i].value * p + 0.5f), stats[i].unit);
            text(num, x + 40.f, y + 70.f, 110.f, faded(stats[i].color, a));
            text(stats[i].label, x + 44.f, y + 240.f, 38.f, faded(t.mutedForeground, a));
        }
        centered("7.6x real time. This 1080p video, Apple M4, measured by the tool itself.", 800.f, 32.f, faded(t.foreground, ease(f, 56, 72, 0.f, 1.f)));
    }});

    scenes.push_back({"everywhere", 90, 0, [](const Frame& f) {
        backdrop("tokyo-night");
        const imshell::Theme& t = imshell::CurrentTheme();
        centered("One codebase", 150.f, 84.f, faded(t.foreground, ease(f, 0, 14, 0.f, 1.f)));
        const char* names[] = {"macOS  Metal", "Windows  Direct3D 11", "Linux  OpenGL", "iOS  Metal", "Android  OpenGL ES", "Web  WebAssembly"};
        for (int i = 0; i < 6; ++i) {
            const float a = ease(f, 14 + i * 6, 28 + i * 6, 0.f, 1.f);
            const float w = 560.f, h = 150.f, x = 140.f + (i % 3) * (w + 30.f), y = 360.f + (i / 3) * (h + 40.f) + (1.f - a) * 30.f;
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), faded(t.card, a), 22.f);
            dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), faded(t.border, a), 22.f, 0, 2.f);
            text(names[i], x + 36.f, y + 52.f, 40.f, faded(t.foreground, a));
        }
        centered("native window, native title bar, one frame loop", 880.f, 38.f, faded(t.mutedForeground, ease(f, 60, 78, 0.f, 1.f)));
    }});

    scenes.push_back({"video", 110, 0, [](const Frame& f) {
        backdrop("tokyo-night");
        const imshell::Theme& t = imshell::CurrentTheme();
        centered("This video is an imshell app", 150.f, 76.f, faded(t.foreground, ease(f, 0, 14, 0.f, 1.f)));
        centered("imshell::video renders ImGui frames offscreen, deterministically", 270.f, 38.f, faded(t.primary, ease(f, 14, 30, 0.f, 1.f)));
        const char* lines[] = {"$ imshell-video scene.js strip all", "$ imshell-video scene.js inspect", "$ imshell-video scene.js verify", "$ imshell-video scene.js render -o out.mp4"};
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float a = ease(f, 24, 40, 0.f, 1.f);
        dl->AddRectFilled(ImVec2(300, 400), ImVec2(1620, 780), faded(t.card, a), 24.f);
        dl->AddRect(ImVec2(300, 400), ImVec2(1620, 780), faded(t.border, a), 24.f, 0, 2.f);
        for (int i = 0; i < 4; ++i) {
            const float la = ease(f, 36 + i * 12, 48 + i * 12, 0.f, 1.f);
            text(lines[i], 360.f, 450.f + i * 80.f, 42.f, faded(i % 2 ? t.mutedForeground : t.foreground, la));
        }
    }});

    scenes.push_back({"end", 70, 0, [](const Frame& f) {
        backdrop("tokyo-night");
        const imshell::Theme& t = imshell::CurrentTheme();
        const float a = ease(f, 0, 14, 0.f, 1.f);
        centered("imshell", 340.f, 170.f, faded(t.foreground, a));
        centered("github.com/dunkbing/imshell", 580.f, 52.f, faded(t.primary, ease(f, 14, 30, 0.f, 1.f)));
    }});

    // every scene edge dips through black (the theme is global, so a dissolve would repaint the outgoing scene)
    for (size_t i = 0; i < scenes.size(); ++i) {
        const bool first = i == 0, last = i + 1 == scenes.size();
        scenes[i].draw = [draw = scenes[i].draw, first, last](const Frame& f) {
            draw(f);
            float dip = last ? 0.f : 1.f - float(f.frames - 1 - f.index) / 4.f;
            if (!first)
                dip = std::max(dip, 1.f - float(f.index) / 4.f);
            if (dip > 0.f)
                ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(8, 8, 14, int(255 * std::min(dip, 1.f))));
        };
    }
    Composition comp = MakeComposition(static_cast<int>(W), static_cast<int>(H), 30, scenes);
    comp.setup = [] { gallery.setup(); };
    if (const char* music = std::getenv("IMSHELL_INTRO_MUSIC")) // a track to lay under it
        comp.audio.push_back({.path = music, .duration = comp.frames / 30.0, .volume = 0.8f, .fadeIn = 1.0, .fadeOut = 2.5});
    return Cli(comp, argc, argv, "imshell_intro");
}

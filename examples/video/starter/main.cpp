// a 4 s title card: a title that springs in, a subtitle that slides in, a ring and a bar.
// ./build/starter timeline | strip all -n 12 -o strip.png | inspect | verify | render -o out.mp4
#include "imshell/video.hpp"
#include "imshell/video_cli.hpp"

#include "imgui.h"

#include <cmath>

using namespace imshell::video;

namespace {
    ImFont* font = nullptr;

    ImU32 colour(int r, int g, int b, float a) { return IM_COL32(r, g, b, static_cast<int>(255.f * std::clamp(a, 0.f, 1.f))); }

    float centred(ImDrawList* dl, float size, float y, ImU32 c, const char* text, float width) {
        const ImVec2 box = font->CalcTextSizeA(size, 1e9f, 0.f, text);
        dl->AddText(font, size, ImVec2((width - box.x) / 2, y), c, text);
        return box.y;
    }
} // namespace

int main(int argc, char** argv) {
    Composition comp;
    comp.width = 1280;
    comp.height = 720;
    comp.fps = 30;
    comp.frames = 120;
    // fonts load once, on the video's own context; use a .ttf of your own (this is macOS's, Linux has /usr/share/fonts, Windows C:/Windows/Fonts)
    comp.setup = [] { font = ImGui::GetIO().Fonts->AddFontFromFileTTF("/System/Library/Fonts/Helvetica.ttc", 0.f); };
    comp.draw = [](const Frame& f) {
        const float w = static_cast<float>(f.width), h = static_cast<float>(f.height);
        const double p = f.position(); // the frame as a number: the motion helpers take it
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddRectFilled({0, 0}, {w, h}, colour(14, 18, 32, 1.f));

        // title: springs up while fading in
        const float rise = SpringIn(p, f.fps, h * 0.08f, 0.f, 6, 30);
        const float fade = Interpolate(p, {6, 20}, {0.f, 1.f}, Ease::OutCubic);
        const float titleSize = h * 0.14f;
        const float titleHeight = centred(dl, titleSize, h * 0.30f + rise, colour(240, 244, 255, fade), "Hello, imshell", w);

        // subtitle: slides in after the title
        const float slide = Interpolate(p, {30, 54}, {h * 0.03f, 0.f}, Ease::OutCubic);
        const float subFade = Interpolate(p, {30, 54}, {0.f, 1.f}, Ease::OutCubic);
        centred(dl, h * 0.055f, h * 0.30f + titleHeight * 1.1f + slide, colour(150, 170, 220, subFade), "a video from a C++ function", w);

        // a ring with a dot going round, and a bar that fills
        const ImVec2 centre(w / 2, h * 0.17f);
        const float radius = h * 0.06f, angle = static_cast<float>(p) * 0.12f;
        const float ring = Interpolate(p, {0, 15}, {0.f, 1.f}, Ease::OutCubic);
        dl->AddCircle(centre, radius, colour(60, 80, 130, ring), 48, 2.f);
        dl->AddCircleFilled({centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle)}, 7.f, colour(90, 160, 255, ring));
        const float barWidth = w * 0.4f, x = (w - barWidth) / 2, y = h * 0.78f;
        const float fill = Interpolate(p, {48, 108}, {0.f, 1.f}, Ease::InOutCubic);
        const float barFade = Interpolate(p, {40, 54}, {0.f, 1.f}, Ease::OutCubic);
        dl->AddRectFilled({x, y}, {x + barWidth, y + 10.f}, colour(40, 50, 80, barFade), 5.f);
        dl->AddRectFilled({x, y}, {x + barWidth * fill, y + 10.f}, colour(90, 160, 255, barFade), 5.f);
    };
    return Cli(comp, argc, argv, "starter");
}

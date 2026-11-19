// a composition: a card flies in on a spring and a bar fills. `video_demo` previews it,
// `video_demo out.mp4` renders it (`--ffmpeg` through ffmpeg), `video_demo --close 2` previews for two seconds
#include "imshell/video_cli.hpp"
#include "imshell/video_scenes.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    using namespace imshell::video;

    // two scenes: the second fades in over the end of the first and starts moving as it does
    const Scene card{"card", 150, 0, [](const Frame& f) {
        // everything is a function of the frame: the spring and the interpolation are closed-form
        const float x = SpringIn(f.position(), f.fps, -400.f, 120.f, 6, 40);
        const float fill = Interpolate(f.position(), {30, 130}, {0.f, 1.f}, Ease::InOutCubic);
        ImGui::SetNextWindowPos(ImVec2(x, 200.f));
        ImGui::SetNextWindowSize(ImVec2(520.f, 0.f));
        ImGui::Begin("card##a", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("imshell::video");
        ImGui::Text("frame %d, %.2f s", f.index, f.time());
        ImGui::ProgressBar(fill, ImVec2(-1.f, 0.f));
        ImGui::End();
    }};
    const Scene outro{"outro", 120, 30, [](const Frame& f) {
        const float y = Interpolate(f.position(), {0, 40}, {80.f, 0.f}, Ease::OutCubic); // enters while it fades in
        ImGui::SetNextWindowPos(ImVec2(120.f, 360.f + y));
        ImGui::SetNextWindowSize(ImVec2(520.f, 0.f));
        ImGui::Begin("card##b", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("scenes, cross-fades, a command line");
        ImGui::Text("scene %d, frame %d of %d", f.scene, f.index, f.frames);
        ImGui::End();
    }};
    Composition comp = MakeComposition(1280, 720, 60, {card, outro});

    // timeline, frame, strip, render, preview, verify: see `imshell_video_demo help`
    return Cli(comp, argc, argv, "imshell_video_demo");
}

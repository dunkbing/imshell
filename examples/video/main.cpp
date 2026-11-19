// a composition: a card flies in on a spring and a bar fills. `video_demo` previews it,
// `video_demo out.mp4` renders it (`--ffmpeg` through ffmpeg), `video_demo --close 2` previews for two seconds
#include "imshell/video_cli.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    using namespace imshell::video;

    Composition comp;
    comp.width = 1280;
    comp.height = 720;
    comp.fps = 60;
    comp.frames = 180;
    comp.draw = [](const Frame& f) {
        // everything is a function of the frame: the spring and the interpolation are closed-form
        const float x = Spring(f.index, f.fps, -400.f, 120.f, 10);
        const float fill = Interpolate(f.index, {30, 150}, {0.f, 1.f}, Ease::InOutCubic);
        ImGui::SetNextWindowPos(ImVec2(x, 200.f));
        ImGui::SetNextWindowSize(ImVec2(520.f, 0.f));
        ImGui::Begin("card", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("imshell::video");
        ImGui::Text("frame %d, %.2f s", f.index, f.time());
        ImGui::ProgressBar(fill, ImVec2(-1.f, 0.f));
        ImGui::End();
    };

    // timeline, frame, strip, render, preview, verify: see `imshell_video_demo help`
    return Cli(comp, argc, argv, "imshell_video_demo");
}

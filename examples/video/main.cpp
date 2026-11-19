// a composition: a card flies in on a spring and a bar fills. `video_demo` previews it,
// `video_demo out.mp4` renders it (`--ffmpeg` through ffmpeg), `video_demo --close 2` previews for two seconds
#include "imshell/video.hpp"

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

    PreviewOptions preview;
    Options render;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--ffmpeg")) {
            render.encoder = Encoder::Ffmpeg;
        } else if (!std::strcmp(argv[i], "--close") && i + 1 < argc) {
            preview.closeAfter = std::atof(argv[++i]);
        } else {
            out = argv[i];
        }
    }

    if (out.empty()) {
        Preview(comp, preview);
        return 0;
    }
    std::string error;
    render.progress = [](int done, int total) {
        std::fprintf(stderr, "\r%d / %d", done, total);
        return true;
    };
    if (!RenderVideo(comp, out, render, &error)) {
        std::fprintf(stderr, "\nfailed: %s\n", error.c_str());
        return 1;
    }
    std::fprintf(stderr, "\nwrote %s\n", out.c_str());
    return 0;
}

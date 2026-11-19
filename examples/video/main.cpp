// a composition: a card flies in on a spring and a bar fills. `video_demo` previews it,
// `video_demo out.mp4` renders it (`--ffmpeg` through ffmpeg), `video_demo --close 2` previews for two seconds
#include "imshell/video_captions.hpp"
#include "imshell/video_cli.hpp"
#include "imshell/video_effects.hpp"
#include "imshell/video_footage.hpp"
#include "imshell/video_scenes.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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

    // captions over everything: webvtt with a time before each word lights them as they are said
    static const std::vector<Cue> cues = ParseCaptions(
        "WEBVTT\n\n00:00.500 --> 00:02.500\nCaptions come from <00:01.000>a vtt <00:01.500>or srt <00:02.000>file\n\n"
        "00:03.000 --> 00:05.000\nand light the words as they are said\n");
    // DEMO_FOOTAGE=clip.mp4 puts a clip in the right half (its sound would be an AudioClip with the same path)
    static std::unique_ptr<Footage> footage = std::getenv("DEMO_FOOTAGE") ? std::make_unique<Footage>(std::getenv("DEMO_FOOTAGE"), comp.fps, 640) : nullptr;
    comp.draw = [scenes = comp.draw](const Frame& f) {
        scenes(f);
        if (footage && footage->ok()) {
            const float w = static_cast<float>(f.width);
            footage->Draw(ImGui::GetBackgroundDrawList(), f.time(), ImVec2(w * 0.55f, w * 0.05f), ImVec2(w * 0.4f, w * 0.4f), Fit::Cover, IM_COL32_WHITE, 16.f);
        }
        CaptionStyle style;
        style.karaoke = true;
        DrawCaptions(f, cues, style);
    };

    // DEMO_FX=1 adds a look: glow, a little colour fringing, grade, vignette and grain that moves
    if (std::getenv("DEMO_FX")) {
        comp.post = [](const Frame& f, Image& image) {
            Glow(image, 0.6f, 0.02f, 0.7f);
            Chromatic(image, 0.003f);
            Grade(image, 0.f, 1.1f, 1.15f);
            Vignette(image, 0.6f, 0.5f);
            Grain(image, 0.03f, static_cast<uint32_t>(f.index));
        };
    }

    // timeline, frame, strip, render, preview, verify: see `imshell_video_demo help`
    return Cli(comp, argc, argv, "imshell_video_demo");
}

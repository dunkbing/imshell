// the metal renderer: a frame is the same picture drawn twice or after other frames, differs
// between frames, and a render writes a playable file. needs a gpu
#include "imgui.h"
#include "imshell/animation.hpp"
#include "imshell/video.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace {
    int failures = 0;

    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL %s\n", what);
            ++failures;
        }
    }

    imshell::video::Composition make() {
        imshell::video::Composition comp;
        comp.width = 320;
        comp.height = 180;
        comp.fps = 30;
        comp.frames = 30;
        comp.draw = [](const imshell::video::Frame& f) {
            const float x = imshell::video::Interpolate(f.index, {0, 29}, {20.f, 200.f}, imshell::video::Ease::OutCubic);
            ImGui::SetNextWindowPos(ImVec2(x, 40));
            ImGui::SetNextWindowSize(ImVec2(0, 0));
            ImGui::Begin("card", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("frame %d", f.index);
            ImGui::End();
        };
        return comp;
    }

    size_t nonBlack(const imshell::video::Image& image) {
        size_t n = 0;
        for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            n += (image.rgba[i] | image.rgba[i + 1] | image.rgba[i + 2]) != 0;
        }
        return n;
    }
}

int main() {
    const auto comp = make();
    std::string error;

    const auto a = imshell::video::RenderFrame(comp, 12, &error);
    check(a.width == 320 && a.height == 180, ("renders a frame: " + error).c_str());
    check(nonBlack(a) > 100, "the frame has content");
    const auto again = imshell::video::RenderFrame(comp, 12);
    check(a.rgba == again.rgba, "the same frame twice is the same picture");
    check(imshell::video::RenderFrame(comp, 0).rgba != a.rgba, "frames differ");

    const char* dir = std::getenv("TMPDIR");
    const std::string base = std::string(dir ? dir : "/tmp") + "/imshell_video_test";
    check(imshell::video::SavePng(a, base + ".png"), "saves a png");

    int last = 0;
    imshell::video::Options options;
    options.progress = [&](int done, int) {
        last = done;
        return true;
    };
    check(imshell::video::RenderVideo(comp, base + ".mp4", options, &error), ("renders a video: " + error).c_str());
    check(last == 30, "reports every frame");
    struct stat st {};
    check(stat((base + ".mp4").c_str(), &st) == 0 && st.st_size > 1000, "the file is there");

    // a cancelled render leaves nothing
    options.progress = [](int done, int) { return done < 3; };
    check(!imshell::video::RenderVideo(comp, base + "_cancel.mp4", options), "cancel fails the render");
    check(stat((base + "_cancel.mp4").c_str(), &st) != 0, "cancel removes the file");

    // widgets that ease (Animate) jump to their target during a render, whatever frame it starts at
    std::vector<float> seen;
    auto easing = comp;
    easing.draw = [&seen](const imshell::video::Frame& f) {
        const float target = (f.index / 10) % 2 ? 1.f : 0.f;
        seen.push_back(imshell::Animate(ImGui::GetID("fade"), target) - target);
    };
    setenv("IMSHELL_REDUCE_MOTION", "0", 1); // the system setting may be on; a render must not depend on it
    check(!imshell::ReduceMotion(), "reduce motion is off before a render");
    imshell::video::RenderFrame(easing, 5);
    imshell::video::RenderFrame(easing, 15);
    bool exact = seen.size() == 4;
    for (float d : seen) {
        exact = exact && d == 0.f;
    }
    check(exact, "Animate sits on its target during a render");
    check(!imshell::ReduceMotion(), "reduce motion is handed back");

    if (failures == 0) {
        std::puts("video render tests passed");
    }
    return failures == 0 ? 0 : 1;
}

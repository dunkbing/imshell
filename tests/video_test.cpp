// the video helpers: pure functions of the frame, no imgui, no gpu
#include "imshell/video.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace {
    int failures = 0;

    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL %s\n", what);
            ++failures;
        }
    }

    bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
}

int main() {
    using namespace imshell::video;

    // easing starts at 0, ends at 1, and the in/out forms are monotonic
    for (Ease e : {Ease::Linear, Ease::InCubic, Ease::OutCubic, Ease::InOutCubic, Ease::OutBack}) {
        check(near(ease(e, 0.f), 0.f), "ease starts at 0");
        check(near(ease(e, 1.f), 1.f), "ease ends at 1");
    }
    check(ease(Ease::OutBack, 0.7f) > 1.f, "back overshoots");
    check(ease(Ease::OutCubic, 0.5f) > 0.5f && ease(Ease::InCubic, 0.5f) < 0.5f, "in and out bend opposite ways");

    // interpolate maps, clamps and handles a zero-length range
    check(near(Interpolate(30, {0, 60}, {10.f, 20.f}), 15.f), "interpolate middle");
    check(near(Interpolate(-5, {0, 60}, {10.f, 20.f}), 10.f), "interpolate clamps low");
    check(near(Interpolate(99, {0, 60}, {10.f, 20.f}), 20.f), "interpolate clamps high");
    check(near(Interpolate(5, {10, 10}, {1.f, 2.f}), 1.f) && near(Interpolate(10, {10, 10}, {1.f, 2.f}), 2.f), "zero range steps");

    // springs: at rest before the start, settle on the target, under/critical/over damped all agree
    for (float damping : {10.f, 26.f, 80.f}) {
        const SpringConfig c{170.f, damping, 1.f};
        check(near(Spring(5, 60, 0.f, 100.f, 10, c), 0.f), "spring waits for its start");
        check(near(Spring(10 + 600, 60, 0.f, 100.f, 10, c), 100.f, 0.05f), "spring settles on the target");
        float prev = 0.f;
        bool smooth = true;
        for (int f = 10; f < 10 + 300; ++f) {
            const float v = Spring(f, 60, 0.f, 100.f, 10, c);
            smooth = smooth && std::fabs(v - prev) < 20.f; // no jumps between frames
            prev = v;
        }
        check(smooth, "spring is continuous");
    }
    check(Spring(40, 60, 0.f, 100.f, 0, {170.f, 10.f, 1.f}) != Spring(40, 60, 0.f, 100.f, 0, {170.f, 80.f, 1.f}), "damping changes the path");

    // the same frame gives the same value however it is reached: there is no state
    check(Spring(77, 60, 3.f, 9.f) == Spring(77, 60, 3.f, 9.f), "spring is pure");

    // sequence and loop
    const std::array<int, 3> scenes{30, 60, 10};
    check(Sequence(0, scenes).index == 0 && Sequence(29, scenes).local == 29, "sequence first scene");
    check(Sequence(30, scenes).index == 1 && Sequence(30, scenes).local == 0, "sequence second scene");
    check(Sequence(99, scenes).index == 2 && Sequence(99, scenes).local == 9, "sequence last scene");
    check(Sequence(500, scenes).index == 2, "sequence stays on the last scene");
    check(Loop(125, 60) == 5 && Loop(-1, 60) == 59 && Loop(7, 0) == 0, "loop wraps");

    // frame
    const Frame f{.index = 30, .frames = 61, .fps = 60};
    check(near(static_cast<float>(f.time()), 0.5f) && near(f.progress(), 0.5f), "frame time and progress");
    check(Frame{.frames = 1}.progress() == 0.f, "one frame has no progress");

    if (failures == 0) {
        std::puts("video tests passed");
    }
    return failures == 0 ? 0 : 1;
}

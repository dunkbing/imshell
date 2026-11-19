#pragma once

// video: a composition is a size, a rate and a function that draws frame n. this part is the
// frame and the closed-form helpers, pure functions of the frame number (no clock, no state), so
// a frame is the same picture whether it is played to or jumped to. see docs/video.md
#include <algorithm>
#include <cmath>
#include <functional>
#include <span>

namespace imshell::video {
    // what a composition's draw function gets
    struct Frame {
        int index = 0;      // 0 .. frames-1
        int frames = 1;     // length of the composition
        int fps = 30;
        int width = 0;
        int height = 0;
        bool sequential = false; // frames are drawn in order (a render), not jumped to

        double time() const { return static_cast<double>(index) / fps; }
        // 0 on the first frame, 1 on the last
        float progress() const { return frames > 1 ? static_cast<float>(index) / static_cast<float>(frames - 1) : 0.f; }
    };

    struct Composition {
        int width = 1920;
        int height = 1080;
        int fps = 60;
        int frames = 60;
        std::function<void()> setup;           // once, on the video's own ImGui context: fonts, theme
        std::function<void(const Frame&)> draw;
    };

    enum class Ease { Linear, InCubic, OutCubic, InOutCubic, OutBack };

    // t in 0..1 to the eased 0..1 (OutBack overshoots)
    inline float ease(Ease e, float t) {
        t = std::clamp(t, 0.f, 1.f);
        switch (e) {
        case Ease::InCubic: return t * t * t;
        case Ease::OutCubic: return 1.f - std::pow(1.f - t, 3.f);
        case Ease::InOutCubic: return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
        case Ease::OutBack: {
            const float c1 = 1.70158f, c3 = c1 + 1.f;
            return 1.f + c3 * std::pow(t - 1.f, 3.f) + c1 * std::pow(t - 1.f, 2.f);
        }
        default: return t;
        }
    }

    struct Range {
        float from = 0.f;
        float to = 1.f;
    };

    // maps frame in [in.from, in.to] to [out.from, out.to], clamped outside, eased
    inline float Interpolate(int frame, Range in, Range out, Ease e = Ease::Linear) {
        const float span = in.to - in.from;
        const float t = span != 0.f ? (static_cast<float>(frame) - in.from) / span : (static_cast<float>(frame) >= in.to ? 1.f : 0.f);
        return out.from + (out.to - out.from) * ease(e, t);
    }

    struct SpringConfig {
        float stiffness = 170.f;
        float damping = 26.f;
        float mass = 1.f;
    };

    // a damped spring released at rest at `start` (a frame), moving from `from` to `to`: the exact
    // solution, so it can be jumped to. before `start` it stays at `from`
    inline float Spring(int frame, int fps, float from, float to, int start = 0, SpringConfig c = {}) {
        const double t = static_cast<double>(frame - start) / fps; // double: exp and cosh overflow floats when overdamped
        if (t <= 0.0) {
            return from;
        }
        const double w0 = std::sqrt(static_cast<double>(c.stiffness) / c.mass);
        const double z = c.damping / (2.0 * std::sqrt(static_cast<double>(c.stiffness) * c.mass));
        double y; // 0 at rest at `from`, 1 at `to`
        if (z < 1.0) {
            const double wd = w0 * std::sqrt(1.0 - z * z);
            y = 1.0 - std::exp(-z * w0 * t) * (std::cos(wd * t) + z * w0 / wd * std::sin(wd * t));
        } else if (z == 1.0) {
            y = 1.0 - std::exp(-w0 * t) * (1.0 + w0 * t);
        } else {
            const double s = w0 * std::sqrt(z * z - 1.0);
            y = 1.0 - std::exp(-z * w0 * t) * (std::cosh(s * t) + z * w0 / s * std::sinh(s * t));
        }
        return from + (to - from) * static_cast<float>(y);
    }

    // which scene a frame falls in, and the frame within it; past the end it stays on the last
    struct Slot {
        int index = 0;
        int local = 0;
    };

    inline Slot Sequence(int frame, std::span<const int> lengths) {
        int start = 0;
        for (size_t i = 0; i < lengths.size(); ++i) {
            if (frame < start + lengths[i] || i + 1 == lengths.size()) {
                return {static_cast<int>(i), frame - start};
            }
            start += lengths[i];
        }
        return {};
    }

    // frame repeating every `length` frames
    inline int Loop(int frame, int length) {
        return length > 0 ? ((frame % length) + length) % length : 0;
    }
} // namespace imshell::video

#pragma once

// audio visualizers: level, spectrum bands and waveform of the composition's own mix at any time,
// as pure functions of the time (the mix is decoded once, up front), so playing and seeking agree.
// build an AudioAnalysis from the composition before its draw function exists, then ask it in draw
#include "imgui.h"
#include "imshell/video.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <span>
#include <utility>
#include <vector>

namespace imshell::video {
    class AudioAnalysis {
    public:
        static constexpr int kRate = 48000;
        static constexpr int kWindow = 2048; // about 43 ms: 23 Hz per bin

        // interleaved 48 kHz stereo float, what RenderAudio returns
        explicit AudioAnalysis(std::vector<float> pcm) {
            mono_.resize(pcm.size() / 2);
            for (size_t i = 0; i < mono_.size(); ++i) {
                mono_[i] = 0.5f * (pcm[i * 2] + pcm[i * 2 + 1]);
            }
        }
        // the mix of the composition's sound clips (from frame 0); empty analysis when it has none
        explicit AudioAnalysis(const Composition& comp) {
            std::vector<float> pcm;
            if (RenderAudio(comp, pcm)) {
                *this = AudioAnalysis(std::move(pcm));
            }
        }

        double seconds() const { return static_cast<double>(mono_.size()) / kRate; }

        // loudness around `t` as 0..1 (rms over 50 ms, -60 dBFS to 0)
        float level(double t) const {
            const long half = kRate / 40;
            const long centre = static_cast<long>(std::llround(t * kRate));
            double sum = 0.0;
            for (long i = centre - half; i < centre + half; ++i) {
                const double v = sample(i);
                sum += v * v;
            }
            return fromDb(10.0 * std::log10(sum / (2.0 * half) + 1e-12));
        }

        // `out.size()` log-spaced bands from 40 Hz to 16 kHz as 0..1 (-60 dBFS to 0; a full scale sine is 1 in its band).
        // `release` 0..1 lets a band fall slowly: the value is the loudest of the last few windows, each scaled down
        void bands(double t, std::span<float> out, float release = 0.8f) const {
            std::fill(out.begin(), out.end(), 0.f);
            std::vector<float> now(out.size());
            float scale = 1.f;
            for (int k = 0; k < 6 && scale > 0.05f; ++k, scale *= release) {
                spectrum(t - k * 0.02, now);
                for (size_t i = 0; i < out.size(); ++i) {
                    out[i] = std::max(out[i], now[i] * scale);
                }
            }
        }

        // `out.size()` samples (-1..1, mono) over `span` seconds centred on `t`
        void waveform(double t, double span, std::span<float> out) const {
            for (size_t i = 0; i < out.size(); ++i) {
                const double at = t - span * 0.5 + span * static_cast<double>(i) / static_cast<double>(std::max<size_t>(out.size() - 1, 1));
                out[i] = sample(static_cast<long>(std::llround(at * kRate)));
            }
        }

    private:
        std::vector<float> mono_;

        float sample(long i) const { return i >= 0 && i < static_cast<long>(mono_.size()) ? mono_[static_cast<size_t>(i)] : 0.f; }
        static float fromDb(double db) { return static_cast<float>(std::clamp((db + 60.0) / 60.0, 0.0, 1.0)); }

        static void fft(std::vector<std::complex<float>>& a) {
            const size_t n = a.size();
            for (size_t i = 1, j = 0; i < n; ++i) { // bit reversal
                size_t bit = n >> 1;
                for (; j & bit; bit >>= 1) {
                    j ^= bit;
                }
                j ^= bit;
                if (i < j) {
                    std::swap(a[i], a[j]);
                }
            }
            for (size_t len = 2; len <= n; len <<= 1) {
                const float angle = -2.f * 3.14159265358979f / static_cast<float>(len);
                const std::complex<float> step(std::cos(angle), std::sin(angle));
                for (size_t i = 0; i < n; i += len) {
                    std::complex<float> w(1.f, 0.f);
                    for (size_t j = 0; j < len / 2; ++j) {
                        const std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
                        a[i + j] = u + v;
                        a[i + j + len / 2] = u - v;
                        w *= step;
                    }
                }
            }
        }

        // one window ending at `t`, as bands
        void spectrum(double t, std::span<float> out) const {
            std::fill(out.begin(), out.end(), 0.f);
            const long end = static_cast<long>(std::llround(t * kRate));
            if (end <= 0 || end - kWindow >= static_cast<long>(mono_.size())) {
                return;
            }
            std::vector<std::complex<float>> a(kWindow);
            for (int i = 0; i < kWindow; ++i) {
                const float hann = 0.5f - 0.5f * std::cos(2.f * 3.14159265358979f * static_cast<float>(i) / static_cast<float>(kWindow));
                a[static_cast<size_t>(i)] = sample(end - kWindow + i) * hann;
            }
            fft(a);
            const double hz = static_cast<double>(kRate) / kWindow;
            const double low = 40.0, high = 16000.0;
            for (size_t b = 0; b < out.size(); ++b) {
                const double f0 = low * std::pow(high / low, static_cast<double>(b) / static_cast<double>(out.size()));
                const double f1 = low * std::pow(high / low, static_cast<double>(b + 1) / static_cast<double>(out.size()));
                const int k0 = std::clamp(static_cast<int>(std::floor(f0 / hz)), 1, kWindow / 2 - 1);
                const int k1 = std::clamp(static_cast<int>(std::ceil(f1 / hz)), k0, kWindow / 2 - 1);
                float peak = 0.f; // the loudest bin: a peak reads better than a mean when a band spans many bins
                for (int k = k0; k <= k1; ++k) {
                    peak = std::max(peak, std::abs(a[static_cast<size_t>(k)]));
                }
                out[b] = fromDb(20.0 * std::log10(peak / (kWindow / 4.0) + 1e-9)); // a sine of amplitude A peaks at A * N / 4 with a hann window
            }
        }
    };

    // bars from values 0..1 into the box, growing up from its bottom edge, on the current window's draw list
    inline void DrawBars(ImDrawList* dl, ImVec2 pos, ImVec2 size, std::span<const float> values, ImU32 color, float gap = 0.25f, float rounding = 0.f) {
        const float slot = size.x / static_cast<float>(std::max<size_t>(values.size(), 1));
        for (size_t i = 0; i < values.size(); ++i) {
            const float h = std::max(2.f, size.y * std::clamp(values[i], 0.f, 1.f));
            const float x = pos.x + slot * static_cast<float>(i);
            dl->AddRectFilled(ImVec2(x + slot * gap * 0.5f, pos.y + size.y - h), ImVec2(x + slot * (1.f - gap * 0.5f), pos.y + size.y), color, rounding);
        }
    }

    // a waveform (-1..1) as a line across the box, centred vertically
    inline void DrawWave(ImDrawList* dl, ImVec2 pos, ImVec2 size, std::span<const float> samples, ImU32 color, float thickness = 2.f) {
        std::vector<ImVec2> points;
        points.reserve(samples.size());
        for (size_t i = 0; i < samples.size(); ++i) {
            points.push_back(ImVec2(pos.x + size.x * static_cast<float>(i) / static_cast<float>(std::max<size_t>(samples.size() - 1, 1)),
                                    pos.y + size.y * (0.5f - 0.5f * std::clamp(samples[i], -1.f, 1.f))));
        }
        dl->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, thickness);
    }
} // namespace imshell::video

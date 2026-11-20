#pragma once

// effects: image filters for the finished frame (Composition::post), written on the CPU so the same
// maths runs on every renderer and every machine, and a frame stays a pure function of its number.
// sizes are fractions of the frame height (0.01 = 1%), so a --draft or --scale render looks the same.
// an effect takes straight (not premultiplied) rgba; alpha is left alone unless the effect says so
#include "imshell/video.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace imshell::video {
    namespace effects_detail {
        // fn(begin, end) over [0, count) in chunks on all cores: every effect here is per pixel or per row, so the result does not depend on the split
        template <class F>
        void parallelFor(int count, F fn) {
            const int workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, std::max(1, count / 16));
            if (workers == 1) {
                fn(0, count);
                return;
            }
            std::vector<std::thread> threads;
            for (int w = 1; w < workers; ++w) {
                threads.emplace_back([&fn, w, workers, count] { fn(static_cast<int>(static_cast<long long>(count) * w / workers), static_cast<int>(static_cast<long long>(count) * (w + 1) / workers)); });
            }
            fn(0, static_cast<int>(static_cast<long long>(count) / workers));
            for (std::thread& t : threads) {
                t.join();
            }
        }

        inline unsigned char clamp8(float v) { return static_cast<unsigned char>(std::clamp(v + 0.5f, 0.f, 255.f)); }

        inline float luma(const unsigned char* p) { return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.f; }

        // a box blur of radius r pixels along one axis, edges clamped; 4 channels
        inline void boxPass(const std::vector<unsigned char>& src, std::vector<unsigned char>& dst, int w, int h, int r, bool horizontal) {
            const int lines = horizontal ? h : w, length = horizontal ? w : h;
            const size_t step = horizontal ? 4 : static_cast<size_t>(w) * 4, lineStride = horizontal ? static_cast<size_t>(w) * 4 : 4;
            const int window = 2 * r + 1;
            parallelFor(lines, [&](int first, int last) {
                for (int line = first; line < last; ++line) {
                    const unsigned char* in = src.data() + static_cast<size_t>(line) * lineStride;
                    unsigned char* out = dst.data() + static_cast<size_t>(line) * lineStride;
                    for (int c = 0; c < 4; ++c) {
                        int sum = 0;
                        for (int i = -r; i <= r; ++i) {
                            sum += in[static_cast<size_t>(std::clamp(i, 0, length - 1)) * step + c];
                        }
                        for (int x = 0; x < length; ++x) {
                            out[static_cast<size_t>(x) * step + c] = static_cast<unsigned char>((sum + window / 2) / window);
                            sum += in[static_cast<size_t>(std::min(x + r + 1, length - 1)) * step + c] - in[static_cast<size_t>(std::max(x - r, 0)) * step + c];
                        }
                    }
                }
            });
        }

        inline int pixelsOf(const Image& image, float fractionOfHeight) { return static_cast<int>(std::lround(fractionOfHeight * static_cast<float>(image.height))); }
    } // namespace effects_detail

    // brightness (added, -1..1), contrast (1 = unchanged, about the middle grey), saturation (1 = unchanged, 0 = grey), gamma (1 = unchanged, above 1 lifts the shadows)
    inline void Grade(Image& image, float brightness = 0.f, float contrast = 1.f, float saturation = 1.f, float gamma = 1.f) {
        unsigned char table[256];
        for (int i = 0; i < 256; ++i) {
            float v = std::pow(static_cast<float>(i) / 255.f, 1.f / std::max(gamma, 0.01f));
            v = (v - 0.5f) * contrast + 0.5f + brightness;
            table[i] = effects_detail::clamp8(v * 255.f);
        }
        effects_detail::parallelFor(static_cast<int>(image.rgba.size() / 4), [&](int first, int last) {
            for (size_t i = static_cast<size_t>(first) * 4; i < static_cast<size_t>(last) * 4; i += 4) {
                unsigned char* p = &image.rgba[i];
                const float grey = effects_detail::luma(p) * 255.f;
                for (int c = 0; c < 3; ++c) {
                    p[c] = table[effects_detail::clamp8(grey + (p[c] - grey) * saturation)];
                }
            }
        });
    }

    // darkens towards the corners: `strength` 0..1 is how dark the corners get, `radius` is where it starts (1 = the corners, 0 = the middle)
    inline void Vignette(Image& image, float strength = 0.5f, float radius = 0.6f) {
        const float cx = static_cast<float>(image.width) * 0.5f, cy = static_cast<float>(image.height) * 0.5f, reach = std::sqrt(cx * cx + cy * cy);
        effects_detail::parallelFor(image.height, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const float d = std::sqrt((static_cast<float>(x) + 0.5f - cx) * (static_cast<float>(x) + 0.5f - cx) + (static_cast<float>(y) + 0.5f - cy) * (static_cast<float>(y) + 0.5f - cy)) / reach;
                const float t = std::clamp((d - radius) / std::max(1.f - radius, 0.01f), 0.f, 1.f);
                const float k = 1.f - strength * t * t * (3.f - 2.f * t);
                unsigned char* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
                p[0] = effects_detail::clamp8(p[0] * k);
                p[1] = effects_detail::clamp8(p[1] * k);
                p[2] = effects_detail::clamp8(p[2] * k);
            }
          }
        });
    }

    // film grain: noise of `amount` (0..1 of full scale) from a hash of the pixel and `seed`, so the same seed gives the same grain.
    // pass the frame index as the seed for grain that moves, a constant for grain that sits still
    inline void Grain(Image& image, float amount = 0.05f, uint32_t seed = 0) {
        effects_detail::parallelFor(image.height, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            for (int x = 0; x < image.width; ++x) {
                uint32_t h =static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
                h = (h ^ (h >> 13)) * 1274126177u;
                h ^= h >> 16;
                const float n = (static_cast<float>(h & 0xffffu) / 32767.5f - 1.f) * amount * 255.f; // -amount..amount
                unsigned char* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
                p[0] = effects_detail::clamp8(p[0] + n);
                p[1] = effects_detail::clamp8(p[1] + n);
                p[2] = effects_detail::clamp8(p[2] + n);
            }
          }
        });
    }

    // a blur of about `radius` (a fraction of the height; three box passes, close to a gaussian). alpha is blurred too, with the colour weighted by it
    inline void Blur(Image& image, float radius) {
        const int r = effects_detail::pixelsOf(image, radius);
        if (r < 1 || image.width < 2 || image.height < 2) {
            return;
        }
        bool translucent = false;
        for (size_t i = 3; i < image.rgba.size() && !translucent; i += 4) {
            translucent = image.rgba[i] != 255;
        }
        if (translucent) { // blur premultiplied, so a transparent pixel's colour does not bleed
            effects_detail::parallelFor(static_cast<int>(image.rgba.size() / 4), [&](int first, int last) {
                for (size_t i = static_cast<size_t>(first) * 4; i < static_cast<size_t>(last) * 4; i += 4) {
                    for (int c = 0; c < 3; ++c) {
                        image.rgba[i + c] = static_cast<unsigned char>((image.rgba[i + c] * image.rgba[i + 3] + 127) / 255);
                    }
                }
            });
        }
        std::vector<unsigned char> tmp(image.rgba.size());
        for (int pass = 0; pass < 3; ++pass) {
            effects_detail::boxPass(image.rgba, tmp, image.width, image.height, r, true);
            effects_detail::boxPass(tmp, image.rgba, image.width, image.height, r, false);
        }
        if (translucent) {
            effects_detail::parallelFor(static_cast<int>(image.rgba.size() / 4), [&](int first, int last) {
                for (size_t i = static_cast<size_t>(first) * 4; i < static_cast<size_t>(last) * 4; i += 4) {
                    const unsigned a = image.rgba[i + 3];
                    for (int c = 0; c < 3; ++c) {
                        image.rgba[i + c] = a == 0 ? 0 : static_cast<unsigned char>(std::min(255u, (image.rgba[i + c] * 255u + a / 2) / a));
                    }
                }
            });
        }
    }

    // a halo round what is brighter than `threshold` (0..1 luma): the bright part, blurred by `radius`, added with `intensity`
    inline void Glow(Image& image, float threshold = 0.7f, float radius = 0.02f, float intensity = 0.6f) {
        Image bright = image;
        const float span = std::max(1.f - threshold, 0.01f);
        effects_detail::parallelFor(static_cast<int>(bright.rgba.size() / 4), [&](int first, int last) {
            for (size_t i = static_cast<size_t>(first) * 4; i < static_cast<size_t>(last) * 4; i += 4) {
                unsigned char* p = &bright.rgba[i];
                const float k = std::clamp((effects_detail::luma(p) - threshold) / span, 0.f, 1.f);
                p[0] = effects_detail::clamp8(p[0] * k);
                p[1] = effects_detail::clamp8(p[1] * k);
                p[2] = effects_detail::clamp8(p[2] * k);
                p[3] = 255;
            }
        });
        Blur(bright, radius);
        effects_detail::parallelFor(static_cast<int>(image.rgba.size() / 4), [&](int first, int last) {
            for (size_t i = static_cast<size_t>(first) * 4; i < static_cast<size_t>(last) * 4; i += 4) {
                for (int c = 0; c < 3; ++c) {
                    image.rgba[i + c] = effects_detail::clamp8(image.rgba[i + c] + bright.rgba[i + c] * intensity);
                }
            }
        });
    }

    // colour fringing: red moves left and blue right by `shift` (a fraction of the height), the edges clamped
    inline void Chromatic(Image& image, float shift = 0.004f) {
        const int s = effects_detail::pixelsOf(image, shift);
        if (s == 0) {
            return;
        }
        const std::vector<unsigned char> source = image.rgba;
        effects_detail::parallelFor(image.height, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            for (int x = 0; x < image.width; ++x) {
                unsigned char* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
                p[0] = source[(static_cast<size_t>(y) * image.width + std::clamp(x + s, 0, image.width - 1)) * 4];
                p[2] = source[(static_cast<size_t>(y) * image.width + std::clamp(x - s, 0, image.width - 1)) * 4 + 2];
            }
          }
        });
    }

    // blocks of `cell` (a fraction of the height), each the average of what it covers
    inline void Pixelate(Image& image, float cell) {
        const int s = effects_detail::pixelsOf(image, cell);
        if (s < 2) {
            return;
        }
        for (int by = 0; by < image.height; by += s) {
            for (int bx = 0; bx < image.width; bx += s) {
                const int w = std::min(s, image.width - bx), h = std::min(s, image.height - by);
                unsigned sum[4] = {0, 0, 0, 0};
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        const unsigned char* p = &image.rgba[(static_cast<size_t>(by + y) * image.width + bx + x) * 4];
                        for (int c = 0; c < 4; ++c) {
                            sum[c] += p[c];
                        }
                    }
                }
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        unsigned char* p = &image.rgba[(static_cast<size_t>(by + y) * image.width + bx + x) * 4];
                        for (int c = 0; c < 4; ++c) {
                            p[c] = static_cast<unsigned char>(sum[c] / static_cast<unsigned>(w * h));
                        }
                    }
                }
            }
        }
    }
} // namespace imshell::video

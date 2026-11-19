// the portable video code: RenderFrame, RenderVideo (through ffmpeg), Verify and SavePng over a
// Rasterizer (egl.cpp, d3d11.cpp). the Apple build has its own (metal.mm)
#include "audio.hpp"
#include "ffmpeg.hpp"
#include "mf.hpp"
#include "png.hpp"
#include "rasterizer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace imshell::video {
    namespace {
        bool fail(std::string* error, const std::string& what) {
            if (error) {
                *error = what;
            }
            return false;
        }

        bool validSize(const Composition& comp, std::string* error) {
            if (comp.width <= 0 || comp.height <= 0 || comp.fps <= 0 || comp.frames <= 0) {
                return fail(error, "the composition needs a size, a rate and at least one frame");
            }
            return true;
        }

        // one frame, or `blur.samples` of them averaged (in the encoded values)
        bool exposure(Rasterizer& rasterizer, const Composition& comp, int index, bool sequential, const MotionBlur& blur,
                      std::vector<unsigned char>& rgba) {
            const int samples = std::max(blur.samples, 1);
            if (samples == 1) {
                if (!rasterizer.render(index, sequential, 0.f, rgba)) {
                    return false;
                }
                if (comp.transparent) {
                    Unpremultiply(rgba);
                }
                return true;
            }
            std::vector<float> sum(static_cast<size_t>(comp.width) * comp.height * 4, 0.f);
            std::vector<unsigned char> sample;
            for (int s = 0; s < samples; ++s) {
                const float offset = ((static_cast<float>(s) + 0.5f) / static_cast<float>(samples) - 0.5f) * blur.shutter;
                if (!rasterizer.render(index, sequential, offset, sample)) {
                    return false;
                }
                for (size_t i = 0; i < sum.size(); ++i) {
                    sum[i] += sample[i];
                }
            }
            rgba.resize(sum.size());
            for (size_t i = 0; i < sum.size(); ++i) {
                rgba[i] = static_cast<unsigned char>(sum[i] / static_cast<float>(samples) + 0.5f);
            }
            if (comp.transparent) {
                Unpremultiply(rgba);
            }
            return true;
        }

        // the platform's decoder unless ffmpeg was asked for
        bool decode(const Options& options, const std::string& path, Pcm& pcm, std::string* error) {
#ifdef _WIN32
            if (options.encoder != Encoder::Ffmpeg) {
                return DecodeWithMf(path, pcm, error);
            }
#endif
            return DecodeWithFfmpeg(options.ffmpeg, path, pcm, error);
        }

        // every clip mixed for frames [from, to); empty when there is no sound
        bool mixAudio(const Composition& comp, const Options& options, int from, int to, Pcm& mix, std::string* error) {
            mix.clear();
            if (comp.audio.empty()) {
                return true;
            }
            mix.assign(WindowFrames(static_cast<double>(to - from) / comp.fps) * 2, 0.f);
            for (const AudioClip& clip : comp.audio) {
                Pcm pcm;
                if (!decode(options, clip.path, pcm, error)) {
                    return false;
                }
                MixClip(mix, static_cast<double>(from) / comp.fps, pcm, clip);
            }
            Limit(mix);
            return true;
        }
#ifdef _WIN32
        // Media Foundation: the sound goes in step with the frames, as the Apple path does
        bool renderWithMf(const Composition& comp, const std::string& path, const Options& options, int from, int to, Rasterizer& rasterizer,
                          const Pcm& mix, std::string* error) {
            MfWriter writer;
            if (!writer.open(path, comp.width, comp.height, comp.fps, !mix.empty(), error)) {
                return false;
            }
            const size_t mixFrames = mix.size() / 2;
            size_t audioDone = 0;
            auto feedAudio = [&](size_t upTo) {
                upTo = std::min(upTo, mixFrames);
                const bool ok = upTo <= audioDone || writer.audio(&mix[audioDone * 2], audioDone, upTo - audioDone);
                audioDone = std::max(audioDone, upTo);
                return ok;
            };
            bool ok = true, cancelled = false;
            std::vector<unsigned char> rgba;
            for (int n = from; ok && n < to; ++n) {
                ok = exposure(rasterizer, comp, n, true, options.blur, rgba) && writer.video(rgba.data(), n - from) &&
                     feedAudio(static_cast<size_t>(n - from + 1) * 48000 / comp.fps);
                if (!ok) {
                    fail(error, "drawing, or the encoder stopped taking frames");
                } else if (options.progress && !options.progress(n - from + 1, to - from)) {
                    cancelled = true;
                    break;
                }
            }
            ok = ok && !cancelled && feedAudio(mixFrames) && writer.finish(error);
            if (!ok || cancelled) {
                std::string ignored;
                writer.finish(&ignored);
                std::remove(path.c_str());
                return cancelled ? fail(error, "cancelled") : false;
            }
            return true;
        }
#endif

    } // namespace

    Image RenderFrame(const Composition& comp, int index, std::string* error) {
        return RenderFrame(comp, index, MotionBlur{}, error);
    }

    Image RenderFrame(const Composition& comp, int index, const MotionBlur& blur, std::string* error) {
        if (!validSize(comp, error)) {
            return {};
        }
        const auto rasterizer = MakeRasterizer(comp, error);
        Image image;
        if (rasterizer && exposure(*rasterizer, comp, std::clamp(index, 0, comp.frames - 1), false, blur, image.rgba)) {
            image.width = comp.width;
            image.height = comp.height;
        } else {
            image = {};
            if (rasterizer) {
                fail(error, "drawing the frame failed");
            }
        }
        return image;
    }

    // the only encoder here is ffmpeg: frames over a pipe, sound from a temporary raw file
    bool RenderVideo(const Composition& comp, const std::string& path, const Options& options, std::string* error) {
        if (!validSize(comp, error)) {
            return false;
        }
        if (comp.width % 2 || comp.height % 2) {
            return fail(error, "h.264 needs an even width and height");
        }
        const int from = std::clamp(options.from, 0, comp.frames - 1);
        const int to = options.to < 0 ? comp.frames : std::clamp(options.to, from + 1, comp.frames);
        const auto rasterizer = MakeRasterizer(comp, error);
        if (!rasterizer) {
            return false;
        }

        Pcm mix;
        if (!mixAudio(comp, options, from, to, mix, error)) {
            return false;
        }
#ifdef _WIN32
        if (!NeedsFfmpeg(comp, options)) {
            return renderWithMf(comp, path, options, from, to, *rasterizer, mix, error);
        }
#endif
        std::string audioFile;
        if (!mix.empty()) {
            audioFile = path + ".audio.f32";
            FILE* f = std::fopen(audioFile.c_str(), "wb");
            const bool wrote = f && std::fwrite(mix.data(), sizeof(float), mix.size(), f) == mix.size();
            if (f) {
                std::fclose(f);
            }
            if (!wrote) {
                return fail(error, "could not write the audio file next to the output");
            }
        }

        FfmpegPipe pipe;
        bool ok = pipe.open(options, comp.width, comp.height, comp.fps, path, audioFile, comp.transparent, error);
        bool cancelled = false;
        std::vector<unsigned char> rgba;
        for (int n = from; ok && n < to; ++n) {
            ok = exposure(*rasterizer, comp, n, true, options.blur, rgba) && pipe.write(rgba.data(), rgba.size());
            if (!ok) {
                fail(error, "drawing, or ffmpeg stopped taking frames (is it installed, and does it have libx264?)");
            } else if (options.progress && !options.progress(n - from + 1, to - from)) {
                cancelled = true;
                break;
            }
        }
        const bool finished = pipe.close();
        if (!audioFile.empty()) {
            std::remove(audioFile.c_str());
        }
        if (ok && !cancelled && !finished) {
            ok = fail(error, "ffmpeg failed");
        }
        if (!ok || cancelled) {
            std::remove(path.c_str());
            return cancelled ? fail(error, "cancelled") : false;
        }
        return true;
    }

    bool Verify(const Composition& comp, std::span<const int> frames, std::string* report) {
        std::string error;
        if (!validSize(comp, &error)) {
            if (report) {
                *report = error;
            }
            return false;
        }
        std::set<int> wanted;
        for (int f : frames) {
            wanted.insert(std::clamp(f, 0, comp.frames - 1));
        }
        std::map<int, std::vector<unsigned char>> played; // one rasterizer plays every frame up to the last wanted
        {
            const auto rasterizer = MakeRasterizer(comp, &error);
            if (!rasterizer) {
                if (report) {
                    *report = error;
                }
                return false;
            }
            std::vector<unsigned char> rgba;
            for (int n = 0; !wanted.empty() && n <= *wanted.rbegin(); ++n) {
                if (!rasterizer->render(n, true, 0.f, rgba)) {
                    if (report) {
                        *report = "drawing frame " + std::to_string(n) + " failed";
                    }
                    return false;
                }
                if (wanted.count(n)) {
                    played[n] = rgba;
                }
            }
        }
        bool same = true;
        std::string text;
        for (int f : wanted) {
            const Image seeked = RenderFrame(comp, f); // a fresh context, as a jump would give
            if (seeked.rgba != played[f]) {
                same = false;
                size_t differing = 0;
                for (size_t i = 0; i + 3 < seeked.rgba.size(); i += 4) {
                    differing += std::memcmp(&seeked.rgba[i], &played[f][i], 4) != 0;
                }
                text += "frame " + std::to_string(f) + ": " + std::to_string(differing) + " pixels differ between playing and seeking\n";
            }
        }
        if (report) {
            *report = text;
        }
        return same;
    }

    bool Frames(const Composition& comp, const Options& options, const std::function<bool(int, const Image&)>& fn, std::string* error) {
        if (!validSize(comp, error)) {
            return false;
        }
        const int from = std::clamp(options.from, 0, comp.frames - 1);
        const int to = options.to < 0 ? comp.frames : std::clamp(options.to, from + 1, comp.frames);
        const auto rasterizer = MakeRasterizer(comp, error);
        if (!rasterizer) {
            return false;
        }
        Image image;
        image.width = comp.width;
        image.height = comp.height;
        for (int n = from; n < to; ++n) {
            if (!exposure(*rasterizer, comp, n, true, options.blur, image.rgba)) {
                return fail(error, "drawing frame " + std::to_string(n) + " failed");
            }
            if (!fn(n, image) || (options.progress && !options.progress(n - from + 1, to - from))) {
                break;
            }
        }
        return true;
    }

    bool RenderAudio(const Composition& comp, std::vector<float>& pcm, const Options& options, std::string* error) {
        if (!validSize(comp, error)) {
            return false;
        }
        const int from = std::clamp(options.from, 0, comp.frames - 1);
        const int to = options.to < 0 ? comp.frames : std::clamp(options.to, from + 1, comp.frames);
        pcm.clear();
        return mixAudio(comp, options, from, to, pcm, error);
    }

    bool SavePng(const Image& image, const std::string& path) {
        if (image.width <= 0 || image.rgba.size() < static_cast<size_t>(image.width) * image.height * 4) {
            return false;
        }
        const std::vector<unsigned char> png = EncodePng(image.width, image.height, image.rgba.data());
        FILE* f = std::fopen(path.c_str(), "wb");
        const bool ok = f && std::fwrite(png.data(), 1, png.size(), f) == png.size();
        if (f) {
            std::fclose(f);
        }
        return ok;
    }
} // namespace imshell::video

// the metal renderer: a frame is the same picture drawn twice or after other frames, differs
// between frames, and a render writes a playable file. needs a gpu
#include "imgui.h"
#include "imshell/animation.hpp"
#include "imshell/video.hpp"
#include "imshell/video_audio_viz.hpp"
#include "imshell/video_captions.hpp"
#include "imshell/video_footage.hpp"
#include "imshell/video_check.hpp"
#include "video_test_util.hpp"

#include <array>
#include <cmath>
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

    // a 16 bit mono wav, a second of a tone: enough for the decoder to resample and widen
    void writeWav(const std::string& path) {
        const int rate = 44100, samples = rate;
        std::vector<short> pcm(samples);
        for (int i = 0; i < samples; ++i) {
            pcm[i] = static_cast<short>(12000 * std::sin(6.2831853 * 440.0 * i / rate));
        }
        const unsigned dataBytes = samples * 2, riff = 36 + dataBytes, fmtSize = 16, byteRate = rate * 2, sampleRate = rate;
        const unsigned short pcmFormat = 1, channels = 1, blockAlign = 2, bits = 16;
        FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
        std::fwrite(&fmtSize, 4, 1, f); std::fwrite(&pcmFormat, 2, 1, f); std::fwrite(&channels, 2, 1, f);
        std::fwrite(&sampleRate, 4, 1, f); std::fwrite(&byteRate, 4, 1, f); std::fwrite(&blockAlign, 2, 1, f);
        std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&dataBytes, 4, 1, f);
        std::fwrite(pcm.data(), 2, pcm.size(), f);
        std::fclose(f);
    }

    // ffprobe's answer for a stream of the file ("" without ffprobe)
    std::string ffprobe(const std::string& file, const char* stream, const char* entries) {
        const std::string cmd = std::string("ffprobe -v error -select_streams ") + stream + " -show_entries stream=" + entries +
                                " -of csv=p=0 " + videotest::quoted(file);
        return videotest::capture(cmd);
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


    const std::string base = videotest::tempDir() + "/imshell_video_test";
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

    // verify: a pure composition passes, one that keeps state between frames is caught
    const std::array<int, 3> probe{0, 7, 20};
    std::string report;
    check(imshell::video::Verify(comp, probe, &report), ("a pure composition verifies: " + report).c_str());
    auto stateful = comp;
    stateful.draw = [counter = 0](const imshell::video::Frame&) mutable {
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(10 + counter++), 40));
        ImGui::Begin("card", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("moves with the calls");
        ImGui::End();
    };
    check(!imshell::video::Verify(stateful, probe, &report) && report.find("frame 7") != std::string::npos, "state between frames is caught");

    // ffmpeg encoder, when it is installed; a missing binary is an error, not a crash
    options.progress = nullptr;
    options.encoder = imshell::video::Encoder::Ffmpeg;
    options.ffmpeg = "/nonexistent/ffmpeg";
    check(!imshell::video::RenderVideo(comp, base + "_ff.mp4", options, &error) && !error.empty(), "a missing ffmpeg fails with a message");
    if (videotest::hasTool("ffmpeg")) {
        options.ffmpeg = "ffmpeg";
        check(imshell::video::RenderVideo(comp, base + "_ff.mp4", options, &error), ("renders through ffmpeg: " + error).c_str());
        check(stat((base + "_ff.mp4").c_str(), &st) == 0 && st.st_size > 1000, "the ffmpeg file is there");
        options.from = 5;
        options.to = 15;
        check(imshell::video::RenderVideo(comp, base + "_ff_range.mp4", options, &error), "renders a frame range through ffmpeg");
    } else {
        std::puts("ffmpeg not installed: skipped the ffmpeg render");
    }
    // widgets that ease (Animate) jump to their target during a render, whatever frame it starts at
    std::vector<float> seen;
    auto easing = comp;
    easing.draw = [&seen](const imshell::video::Frame& f) {
        const float target = (f.index / 10) % 2 ? 1.f : 0.f;
        seen.push_back(imshell::Animate(ImGui::GetID("fade"), target) - target);
    };
    videotest::setEnv("IMSHELL_REDUCE_MOTION", "0"); // the system setting may be on; a render must not depend on it
    check(!imshell::ReduceMotion(), "reduce motion is off before a render");
    imshell::video::RenderFrame(easing, 5);
    imshell::video::RenderFrame(easing, 15);
    bool exact = seen.size() == 4;
    for (float d : seen) {
        exact = exact && d == 0.f;
    }
    check(exact, "Animate sits on its target during a render");
    check(!imshell::ReduceMotion(), "reduce motion is handed back");
    check(imshell::video::Verify(easing, probe, &report), ("an eased composition verifies: " + report).c_str());

    // sound: a clip placed on the timeline ends up as an aac track, through either encoder
    writeWav(base + ".wav");
    auto sound = comp;
    sound.audio.push_back({.path = base + ".wav", .start = 0.25, .volume = 0.8f, .fadeOut = 0.2});
    options = {};
    check(imshell::video::RenderVideo(sound, base + "_sound.mp4", options, &error), ("renders with sound: " + error).c_str());
    check(stat((base + "_sound.mp4").c_str(), &st) == 0 && st.st_size > 1000, "the file with sound is there");
    const bool haveProbe = videotest::hasTool("ffprobe");
    if (haveProbe) {
        check(ffprobe(base + "_sound.mp4", "a:0", "codec_name,channels,sample_rate").find("aac,48000,2") == 0, "the track is 48 kHz stereo aac");
        check(ffprobe(base + "_sound.mp4", "v:0", "nb_frames").find("30") == 0, "the picture keeps its frames");
    }
    auto missing = comp;
    missing.audio.push_back({.path = base + "_nothing.wav"});
    check(!imshell::video::RenderVideo(missing, base + "_missing.mp4", options, &error) && !error.empty(), "a missing sound file fails the render");
    check(stat((base + "_missing.mp4").c_str(), &st) != 0, "and leaves no file");
    if (videotest::hasTool("ffmpeg")) {
        options.encoder = imshell::video::Encoder::Ffmpeg;
        check(imshell::video::RenderVideo(sound, base + "_sound_ff.mp4", options, &error), ("renders with sound through ffmpeg: " + error).c_str());
        check(stat((base + "_sound_ff.mp4.audio.f32").c_str(), &st) != 0, "the temporary audio file is removed");
        if (haveProbe) {
            check(ffprobe(base + "_sound_ff.mp4", "a:0", "codec_name,channels,sample_rate").find("aac,48000,2") == 0, "ffmpeg's track is 48 kHz stereo aac");
        }
    }

    // motion blur: a rect that moves 20 px a frame is smeared over its path by the shutter,
    // samples=1 changes nothing, and the same frame blurred twice is the same picture
    auto mover = comp;
    mover.draw = [](const imshell::video::Frame& f) {
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(20.f + 20.f * static_cast<float>(f.position()), 60.f),
                                                      ImVec2(40.f + 20.f * static_cast<float>(f.position()), 120.f), IM_COL32(255, 255, 255, 255));
    };
    const auto sharp = imshell::video::RenderFrame(mover, 5);
    const auto sharpToo = imshell::video::RenderFrame(mover, 5, imshell::video::MotionBlur{1, 1.f});
    const auto blurred = imshell::video::RenderFrame(mover, 5, imshell::video::MotionBlur{8, 1.f});
    check(sharp.rgba == sharpToo.rgba, "one sample is the plain frame");
    check(blurred.rgba == imshell::video::RenderFrame(mover, 5, imshell::video::MotionBlur{8, 1.f}).rgba, "a blurred frame is repeatable");
    auto greys = [](const imshell::video::Image& image) {
        size_t n = 0;
        for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            n += image.rgba[i] > 20 && image.rgba[i] < 235;
        }
        return n;
    };
    check(greys(sharp) == 0, "a sharp rect has no in-between pixels");
    check(greys(blurred) > 300, "a blurred rect has a smear of in-between pixels");
    // the smear reaches half a frame (10 px) either way: sharp covers x 120..140, blurred 110..150
    const auto at = [&](const imshell::video::Image& image, int x) { return image.rgba[(static_cast<size_t>(90) * image.width + x) * 4]; };
    check(at(sharp, 112) == 0 && at(blurred, 112) > 0 && at(blurred, 100) == 0 && at(blurred, 160) == 0, "the smear spans the shutter");
    // the encoders take blurred frames too
    imshell::video::Options blurOptions;
    blurOptions.blur = {4, 0.5f};
    check(imshell::video::RenderVideo(mover, base + "_blur.mp4", blurOptions, &error), ("renders blurred: " + error).c_str());
    check(stat((base + "_blur.mp4").c_str(), &st) == 0 && st.st_size > 1000, "the blurred file is there");
    if (videotest::hasTool("ffmpeg")) {
        blurOptions.encoder = imshell::video::Encoder::Ffmpeg;
        check(imshell::video::RenderVideo(mover, base + "_blur_ff.mp4", blurOptions, &error), ("renders blurred through ffmpeg: " + error).c_str());
    }

    // inspect: windows cut off by the canvas, a blank start, a pop, and a cut that is not one
    {
        using namespace imshell::video;
        auto card = [](float x) {
            ImGui::SetNextWindowPos(ImVec2(x, 100.f));
            ImGui::SetNextWindowSize(ImVec2(240.f, 220.f)); // above the theme's minimum window size
            ImGui::Begin("card", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("hello");
            ImGui::End();
        };
        auto find = [](const std::vector<Finding>& all, const char* kind) -> const Finding* {
            for (const Finding& f : all) {
                if (f.kind == kind) {
                    return &f;
                }
            }
            return nullptr;
        };
        Composition c;
        c.width = 800;
        c.height = 500;
        c.fps = 30;
        c.frames = 30;
        c.draw = [&](const Frame& f) { card(f.index >= 10 && f.index <= 20 ? -100.f : 150.f); };
        std::string problem;
        std::vector<Finding> found = Inspect(c, {}, &problem);
        const Finding* clipped = find(found, "clipped");
        check(clipped && clipped->severity == "error" && clipped->from == 10 && clipped->to == 20 && clipped->what == "card" && clipped->detail.find("left") != std::string::npos,
              ("a window cut off in the middle is an error: " + problem + " [" + std::to_string(found.size()) + " findings; first: " + (found.empty() ? std::string("none") : found[0].kind + " " + found[0].what + " " + std::to_string(found[0].from) + ".." + std::to_string(found[0].to) + " " + found[0].detail) + "]").c_str());

        c.draw = [&](const Frame& f) { card(f.index < 6 ? -100.f : 150.f); };
        found = Inspect(c, {}, &problem);
        clipped = find(found, "clipped");
        check(clipped && clipped->severity == "warning" && clipped->from == 0 && clipped->to == 5, "cut off at the start is an entrance, a warning");

        c.draw = [&](const Frame& f) {
            if (f.index >= 5) {
                card(150.f);
            }
        };
        found = Inspect(c, {}, &problem);
        const Finding* blank = find(found, "blank");
        check(blank && blank->from == 0 && blank->to == 4 && blank->severity == "info", "frames with nothing drawn are noted");

        c.draw = [&](const Frame& f) {
            if (f.index >= 15) {
                card(150.f);
            }
        };
        found = Inspect(c, {}, &problem);
        const Finding* pop = find(found, "pop");
        check(pop && pop->severity == "error" && pop->from == 15, "an element that appears in one frame is a pop");
        c.marks = {{"a", 0, 15}, {"b", 15, 15}};
        found = Inspect(c, {}, &problem);
        check(!find(found, "pop"), "a cut at a scene boundary is not a pop");
        InspectOptions noPops;
        noPops.pops = false;
        c.marks.clear();
        check(!find(Inspect(c, noPops, &problem), "pop"), "pops can be left out");
    }

    // codec options go through ffmpeg: the codec in the file is the one asked for
    if (videotest::hasTool("ffmpeg") && videotest::hasTool("ffprobe")) {
        for (const char* codec : {"hevc", "vp9", "prores"}) {
            imshell::video::Options o;
            o.codec = codec;
            o.crf = std::string(codec) == "prores" ? -1 : 35;
            const std::string file = base + "_codec_" + codec + (std::string(codec) == "vp9" ? ".webm" : std::string(codec) == "prores" ? ".mov" : ".mp4");
            check(imshell::video::RenderVideo(mover, file, o, &error), (std::string("renders ") + codec + ": " + error).c_str());
            check(ffprobe(file, "v:0", "codec_name").find(codec) == 0, (std::string("the file holds ") + codec).c_str());
        }
    }

    // transparency: nothing drawn is transparent, a window keeps straight (not premultiplied) colour, and it survives png and prores/vp9
    {
        imshell::video::Composition t;
        t.width = 400;
        t.height = 300;
        t.fps = 30;
        t.frames = 6;
        t.transparent = true;
        t.draw = [](const imshell::video::Frame&) {
            ImGui::SetNextWindowPos(ImVec2(100.f, 100.f));
            ImGui::SetNextWindowSize(ImVec2(240.f, 200.f));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            ImGui::Begin("half", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::End();
            ImGui::PopStyleVar();
        };
        const imshell::video::Image image = imshell::video::RenderFrame(t, 3, &error);
        check(image.width == 400, ("renders a transparent frame: " + error).c_str());
        if (image.width == 400) {
            const unsigned char* corner = &image.rgba[0];
            const unsigned char* inside = &image.rgba[(static_cast<size_t>(200) * 400 + 200) * 4];
            check(corner[3] == 0, "the empty canvas is transparent");
            check(inside[3] > 90 && inside[3] < 170, "a half transparent window is half opaque");
            imshell::video::Composition opaque = t;
            opaque.transparent = false;
            const imshell::video::Image solid = imshell::video::RenderFrame(opaque, 3, &error);
            const unsigned char* flat = &solid.rgba[(static_cast<size_t>(200) * 400 + 200) * 4];
            // straight alpha: over black it would be colour * alpha; the stored colour is the window's own, so it is brighter than the blended one
            check(solid.width == 400 && inside[0] >= flat[0] && inside[0] <= 255, "colour is straight, not premultiplied");
        }
        imshell::video::Options o;
        check(!imshell::video::RenderVideo(t, base + "_alpha.mp4", o, &error) && error.find("transparen") != std::string::npos, "h264 refuses transparency with a message");
        if (videotest::hasTool("ffmpeg") && videotest::hasTool("ffprobe")) {
            o.codec = "prores";
            check(imshell::video::RenderVideo(t, base + "_alpha.mov", o, &error), ("renders prores 4444: " + error).c_str());
            check(ffprobe(base + "_alpha.mov", "v:0", "pix_fmt").find("yuva444p") == 0, "the prores file has an alpha plane");
            o.codec = "vp9";
            check(imshell::video::RenderVideo(t, base + "_alpha.webm", o, &error), ("renders vp9 with alpha: " + error).c_str());
            check(ffprobe(base + "_alpha.webm", "v:0", "codec_name:stream_tags=alpha_mode").find("vp9,1") == 0, "the webm is marked as having alpha");
        }
    }

    // captions draw over the bottom of the frame at the right times
    {
        using namespace imshell::video;
        Composition c;
        c.width = 800;
        c.height = 450;
        c.fps = 30;
        c.frames = 90;
        const std::vector<Cue> cues = ParseCaptions("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nA caption that is long enough to need a second line when it is drawn at this size\n");
        c.draw = [&](const Frame& f) { DrawCaptions(f, cues); };
        const Image before = RenderFrame(c, 5, &error), during = RenderFrame(c, 45, &error);
        auto lit = [](const Image& im, int y0, int y1) {
            int n = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = 0; x < im.width; ++x) {
                    n += im.rgba[(static_cast<size_t>(y) * im.width + x) * 4] > 200 ? 1 : 0;
                }
            }
            return n;
        };
        check(before.width == 800 && lit(before, 0, 450) == 0, "nothing is drawn before the cue");
        check(during.width == 800 && lit(during, 330, 440) > 200 && lit(during, 0, 250) == 0, "the cue is drawn near the bottom");
    }

    // visualizer draw helpers: bars follow the sound (a tone for the first second, then silence)
    {
        using namespace imshell::video;
        std::vector<float> pcm(48000 * 2 * 2, 0.f);
        for (size_t n = 0; n < 48000; ++n) {
            pcm[n * 2] = pcm[n * 2 + 1] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * n / 48000.0));
        }
        const AudioAnalysis analysis(pcm);
        Composition c;
        c.width = 640;
        c.height = 360;
        c.fps = 30;
        c.frames = 60;
        c.draw = [&](const Frame& f) {
            float bands[24];
            analysis.bands(f.time(), bands);
            float wave[64];
            analysis.waveform(f.time(), 0.01, wave);
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            DrawBars(dl, ImVec2(20.f, 20.f), ImVec2(600.f, 200.f), bands, IM_COL32(255, 255, 255, 255));
            DrawWave(dl, ImVec2(20.f, 240.f), ImVec2(600.f, 100.f), wave, IM_COL32(255, 255, 255, 255));
        };
        auto lit = [](const Image& im, int y0, int y1) {
            int n = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = 0; x < im.width; ++x) {
                    n += im.rgba[(static_cast<size_t>(y) * im.width + x) * 4] > 200 ? 1 : 0;
                }
            }
            return n;
        };
        const Image tone = RenderFrame(c, 15, &error), quiet = RenderFrame(c, 50, &error);
        check(tone.width == 640 && lit(tone, 20, 220) > 2000, "bars are tall while the tone plays");
        check(quiet.width == 640 && lit(quiet, 20, 215) == 0 && lit(quiet, 215, 222) > 0, "in silence the bars are a thin line at the base");
        check(lit(tone, 240, 340) > 100, "the wave is drawn");
    }

    // footage: a clip made with ffmpeg's test pattern, read in order, by seeking and past its end
    if (videotest::hasTool("ffmpeg")) {
        using namespace imshell::video;
        const std::string clip = base + "_footage.mp4";
        std::remove(clip.c_str());
        videotest::capture("ffmpeg -v error -y -f lavfi -i testsrc=size=160x90:rate=30:duration=2 -pix_fmt yuv420p " + videotest::quoted(clip) + " 2>&1");
        Footage missing("/no/such/clip.mp4", 30);
        check(!missing.ok() && !missing.error().empty() && missing.pixels(0.0) == nullptr, "a missing file is not ok and has an error");
        Footage footage(clip, 30);
        check(footage.ok() && footage.width() == 160 && footage.height() == 90 && std::fabs(footage.seconds() - 2.0) < 0.1,
              ("the clip's size and length are read: " + footage.error()).c_str());
        if (footage.ok()) {
            const size_t bytes = static_cast<size_t>(footage.width()) * footage.height() * 4;
            std::vector<std::vector<unsigned char>> played;
            for (int n = 0; n < 12; ++n) { // in order
                const unsigned char* p = footage.pixels(n / 30.0);
                check(p != nullptr, "a frame is read");
                played.emplace_back(p, p + bytes);
            }
            check(played[0] != played[5] && played[5] != played[11], "the pattern moves between frames");
            const unsigned char* seek = footage.pixels(7 / 30.0); // back, by seeking
            check(std::vector<unsigned char>(seek, seek + bytes) == played[7], "seeking back gives the frame that was played");
            const unsigned char* far = footage.pixels(45 / 30.0); // and forward past the skip window
            const std::vector<unsigned char> forty5(far, far + bytes);
            Footage fresh(clip, 30);
            const unsigned char* again = fresh.pixels(45 / 30.0); // a new decoder jumping straight there
            check(std::vector<unsigned char>(again, again + bytes) == forty5, "jumping straight to a frame equals playing to it");
            for (int n = 0; n < 12; ++n) { // each frame by a fresh decoder jumping to it
                Footage jump(clip, 30);
                const unsigned char* j = jump.pixels(n / 30.0);
                check(j && std::vector<unsigned char>(j, j + bytes) == played[static_cast<size_t>(n)], ("a fresh seek to frame " + std::to_string(n) + " equals playing to it").c_str());
            }
            const unsigned char* before = footage.pixels(-1.0);
            check(std::vector<unsigned char>(before, before + bytes) == played[0], "before the start is the first frame");
            const unsigned char* last = footage.pixels(2.0);
            const std::vector<unsigned char> lastFrame(last, last + bytes);
            const unsigned char* held = footage.pixels(9.0);
            check(std::vector<unsigned char>(held, held + bytes) == lastFrame, "after the end the last frame is held");
        }

        // drawn in a composition: playing in order and seeking give the same pictures
        Footage drawn(clip, 30);
        Composition c;
        c.width = 320;
        c.height = 180;
        c.fps = 30;
        c.frames = 40;
        c.draw = [&](const Frame& f) { drawn.Draw(ImGui::GetForegroundDrawList(), f.time(), ImVec2(0.f, 0.f), ImVec2(320.f, 180.f), Fit::Cover); };
        const Image first = RenderFrame(c, 2, &error), later = RenderFrame(c, 20, &error);
        check(first.width == 320 && first.rgba != later.rgba, "footage is drawn, and changes with time");
        auto colourOf = [](const Image& im) { // a test pattern has colour, not black
            long sum = 0;
            for (size_t i = 0; i < im.rgba.size(); i += 4) {
                sum += im.rgba[i] + im.rgba[i + 1] + im.rgba[i + 2];
            }
            return sum;
        };
        check(colourOf(first) > 320L * 180 * 100 && colourOf(later) > 320L * 180 * 100, "the footage's picture reaches every frame, each from its own render (a new imgui context each)");
        std::string report;
        const int checkFrames[] = {0, 5, 20, 33};
        check(Verify(c, checkFrames, &report), ("playing and seeking footage agree: " + report).c_str());
        std::remove(clip.c_str());
    }

    if (failures == 0) {
        std::puts("video render tests passed");
    }
    return failures == 0 ? 0 : 1;
}

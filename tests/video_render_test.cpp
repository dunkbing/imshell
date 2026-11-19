// the metal renderer: a frame is the same picture drawn twice or after other frames, differs
// between frames, and a render writes a playable file. needs a gpu
#include "imgui.h"
#include "imshell/animation.hpp"
#include "imshell/video.hpp"

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
                                " -of csv=p=0 '" + file + "' 2>/dev/null";
        std::string out;
        if (FILE* p = popen(cmd.c_str(), "r")) {
            char line[256];
            while (std::fgets(line, sizeof line, p)) {
                out += line;
            }
            pclose(p);
        }
        return out;
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
    if (std::system("command -v ffmpeg >/dev/null 2>&1") == 0) {
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
    check(imshell::video::Verify(easing, probe, &report), ("an eased composition verifies: " + report).c_str());

    // sound: a clip placed on the timeline ends up as an aac track, through either encoder
    writeWav(base + ".wav");
    auto sound = comp;
    sound.audio.push_back({.path = base + ".wav", .start = 0.25, .volume = 0.8f, .fadeOut = 0.2});
    options = {};
    check(imshell::video::RenderVideo(sound, base + "_sound.mp4", options, &error), ("renders with sound: " + error).c_str());
    check(stat((base + "_sound.mp4").c_str(), &st) == 0 && st.st_size > 1000, "the file with sound is there");
    const bool haveProbe = std::system("command -v ffprobe >/dev/null 2>&1") == 0;
    if (haveProbe) {
        check(ffprobe(base + "_sound.mp4", "a:0", "codec_name,channels,sample_rate").find("aac,48000,2") == 0, "the track is 48 kHz stereo aac");
        check(ffprobe(base + "_sound.mp4", "v:0", "nb_frames").find("30") == 0, "the picture keeps its frames");
    }
    auto missing = comp;
    missing.audio.push_back({.path = base + "_nothing.wav"});
    check(!imshell::video::RenderVideo(missing, base + "_missing.mp4", options, &error) && !error.empty(), "a missing sound file fails the render");
    check(stat((base + "_missing.mp4").c_str(), &st) != 0, "and leaves no file");
    if (std::system("command -v ffmpeg >/dev/null 2>&1") == 0) {
        options.encoder = imshell::video::Encoder::Ffmpeg;
        check(imshell::video::RenderVideo(sound, base + "_sound_ff.mp4", options, &error), ("renders with sound through ffmpeg: " + error).c_str());
        check(stat((base + "_sound_ff.mp4.audio.f32").c_str(), &st) != 0, "the temporary audio file is removed");
        if (haveProbe) {
            check(ffprobe(base + "_sound_ff.mp4", "a:0", "codec_name,channels,sample_rate").find("aac,48000,2") == 0, "ffmpeg's track is 48 kHz stereo aac");
        }
    }

    if (failures == 0) {
        std::puts("video render tests passed");
    }
    return failures == 0 ? 0 : 1;
}

// the portable video code (generic.cpp, ffmpeg.cpp, png.cpp) over a fake rasterizer, so it runs
// anywhere with no gpu: blur averaging, Verify, RenderVideo through ffmpeg with sound, the png writer
#include "ffmpeg.hpp"
#include "video_test_util.hpp"
#include "rasterizer.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace {
    int failures = 0;

    void check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL %s\n", what);
            ++failures;
        }
    }

    bool gDrift = false; // the fake keeps state between frames: Verify must catch it
    int gCalls = 0;

    // a frame is one flat colour: red = 10 per frame, +100 for a positive blur offset, + the call count when drifting
    class Fake : public imshell::video::Rasterizer {
    public:
        explicit Fake(const imshell::video::Composition& comp) : comp_(comp) {}
        bool render(int index, bool, float offset, std::vector<unsigned char>& rgba) override {
            const int red = std::min(255, index * 10 + (offset > 0.f ? 100 : 0) + (gDrift ? ++gCalls : 0));
            rgba.assign(static_cast<size_t>(comp_.width) * comp_.height * 4, 0);
            for (size_t i = 0; i < rgba.size(); i += 4) {
                rgba[i] = static_cast<unsigned char>(red);
                rgba[i + 3] = 255;
            }
            return true;
        }

    private:
        const imshell::video::Composition& comp_;
    };

    void writeWav(const std::string& path) {
        const int rate = 44100, samples = rate;
        const unsigned dataBytes = samples * 2, riff = 36 + dataBytes, fmtSize = 16, byteRate = rate * 2, sampleRate = rate;
        const unsigned short pcmFormat = 1, channels = 1, blockAlign = 2, bits = 16;
        FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite("RIFF", 1, 4, f);
        std::fwrite(&riff, 4, 1, f);
        std::fwrite("WAVEfmt ", 1, 8, f);
        std::fwrite(&fmtSize, 4, 1, f);
        std::fwrite(&pcmFormat, 2, 1, f);
        std::fwrite(&channels, 2, 1, f);
        std::fwrite(&sampleRate, 4, 1, f);
        std::fwrite(&byteRate, 4, 1, f);
        std::fwrite(&blockAlign, 2, 1, f);
        std::fwrite(&bits, 2, 1, f);
        std::fwrite("data", 1, 4, f);
        std::fwrite(&dataBytes, 4, 1, f);
        for (int i = 0; i < samples; ++i) {
            const short s = static_cast<short>(12000 * std::sin(6.2831853 * 440.0 * i / rate));
            std::fwrite(&s, 2, 1, f);
        }
        std::fclose(f);
    }

}

namespace imshell::video {
    std::unique_ptr<Rasterizer> MakeRasterizer(const Composition& comp, std::string*) {
        return std::make_unique<Fake>(comp);
    }
}

int main() {
    using namespace imshell::video;
    Composition comp;
    comp.width = 64;
    comp.height = 36;
    comp.fps = 30;
    comp.frames = 30;

    // a frame, and blur: two samples at -0.25 and +0.25 frames give (0 + 100) / 2 on top of the frame's own value
    const Image plain = RenderFrame(comp, 3);
    check(plain.width == 64 && plain.rgba[0] == 30 && plain.rgba[3] == 255, "a frame comes back");
    const Image blurred = RenderFrame(comp, 3, MotionBlur{2, 0.5f});
    check(blurred.rgba[0] == 80, "samples are averaged");
    check(RenderFrame(comp, 3, MotionBlur{1, 1.f}).rgba == plain.rgba, "one sample is the plain frame");

    // Verify: a fake with no state passes, one that remembers its calls is caught
    const std::array<int, 3> probe{0, 4, 9};
    std::string report;
    check(Verify(comp, probe, &report), ("a pure rasterizer verifies: " + report).c_str());
    gDrift = true;
    check(!Verify(comp, probe, &report) && report.find("frame 4") != std::string::npos, "drift is caught");
    gDrift = false;

    // the png writer: a file other tools read
    const std::string dir = videotest::tempDir();
    const std::string base = dir + "/imshell_video_generic_test";
    check(SavePng(plain, base + ".png"), "saves a png");
    const bool haveFfmpeg = videotest::hasTool("ffmpeg");
    const bool haveProbe = videotest::hasTool("ffprobe");
    if (haveProbe) {
        check(videotest::capture("ffprobe -v error -show_entries stream=width,height -of csv=p=0 " + videotest::quoted(base + ".png")).find("64,36") == 0, "ffprobe reads the png's size");
    }

    // RenderVideo through ffmpeg, with sound and a cancel
    if (haveFfmpeg) {
        writeWav(base + ".wav");
        auto sound = comp;
        sound.audio.push_back({.path = base + ".wav", .start = 0.25, .volume = 0.8f});
        Options options;
        std::string error;
        check(RenderVideo(sound, base + ".mp4", options, &error), ("renders through ffmpeg: " + error).c_str());
        struct stat st {};
        check(stat((base + ".mp4").c_str(), &st) == 0 && st.st_size > 500, "the file is there");
        check(stat((base + ".mp4.audio.f32").c_str(), &st) != 0, "the temporary audio file is gone");
        if (haveProbe) {
            check(videotest::capture("ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels -of csv=p=0 " + videotest::quoted(base + ".mp4"))
                          .find("aac,48000,2") == 0, "the sound is a 48 kHz stereo aac track");
            check(videotest::capture("ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames -of csv=p=0 " + videotest::quoted(base + ".mp4")).find("30") == 0, "all the frames");
        }
        std::vector<float> pcm;
        check(DecodeWithFfmpeg("ffmpeg", base + ".wav", pcm, &error) && pcm.size() == 48000 * 2, "ffmpeg decodes to 48 kHz stereo");
        check(!DecodeWithFfmpeg("ffmpeg", base + "_nothing.wav", pcm, &error) && !error.empty(), "a missing sound file is an error");

        options.progress = [](int done, int) { return done < 3; };
        check(!RenderVideo(comp, base + "_cancel.mp4", options, &error), "cancel fails the render");
        check(stat((base + "_cancel.mp4").c_str(), &st) != 0, "cancel removes the file");
        options.progress = nullptr;
        options.ffmpeg = "/nonexistent/ffmpeg";
        check(!RenderVideo(comp, base + "_missing.mp4", options, &error), "a missing ffmpeg fails");
        for (const char* suffix : {".wav", ".mp4"}) {
            std::remove((base + suffix).c_str());
        }
    } else {
        std::puts("ffmpeg not installed: skipped the ffmpeg checks");
    }
    std::remove((base + ".png").c_str());

    if (failures == 0) {
        std::puts("video generic tests passed");
    }
    return failures == 0 ? 0 : 1;
}

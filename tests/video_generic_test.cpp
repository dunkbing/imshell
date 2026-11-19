// the portable video code (generic.cpp, ffmpeg.cpp, png.cpp) over a fake rasterizer, so it runs
// anywhere with no gpu: blur averaging, Verify, RenderVideo through ffmpeg with sound, the png writer
#include "ffmpeg.hpp"
#include "imshell/video_audio_viz.hpp"
#include "imshell/video_captions.hpp"
#include "imshell/video_check.hpp"
#include "imshell/video_effects.hpp"
#include "imshell/video_cli.hpp"
#include "imshell/video_scenes.hpp"
#include "video_test_util.hpp"
#include "rasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    void Preview(const Composition&, const PreviewOptions&) {} // the window is not what this test is about
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

    // Composition::post: the finished frame (after blur) gets the frame index in green, and verify sees it on both sides
    {
        Composition styled = comp;
        styled.post = [](const Frame& f, Image& image) {
            for (size_t i = 0; i < image.rgba.size(); i += 4) {
                image.rgba[i + 1] = static_cast<unsigned char>(f.index);
            }
        };
        check(RenderFrame(styled, 3).rgba[1] == 3 && RenderFrame(styled, 3).rgba[0] == 30, "post sees the frame index and keeps the rest");
        check(RenderFrame(styled, 3, MotionBlur{2, 0.5f}).rgba[1] == 3 && RenderFrame(styled, 3, MotionBlur{2, 0.5f}).rgba[0] == 80, "post runs once, after the blur is averaged");
        check(Verify(styled, probe, &report), ("playing and seeking agree with post: " + report).c_str());
        styled.post = [](const Frame&, Image& image) { image.rgba.resize(4); };
        check(RenderFrame(styled, 3).rgba.size() == static_cast<size_t>(64) * comp.height * 4, "a filter cannot change the picture's size");
    }

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
        options.encoder = Encoder::Ffmpeg; // this test is about the ffmpeg path, whatever the platform default
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

        // Frames plays in order and stops when asked; RenderAudio is the mix a render makes
        {
            std::vector<int> seen;
            Options range;
            range.from = 5;
            range.to = 12;
            check(Frames(comp, range, [&](int n, const Image& image) { seen.push_back(n); return image.width == 64; }, &error) && seen.size() == 7 && seen.front() == 5 && seen.back() == 11, "Frames plays a range in order");
            seen.clear();
            check(Frames(comp, Options{}, [&](int n, const Image&) { seen.push_back(n); return n < 3; }, &error) && seen.size() == 4, "Frames stops when asked");
            Composition withSound = comp;
            withSound.audio.push_back({.path = base + ".wav", .volume = 0.5f});
            std::vector<float> mixed;
            check(RenderAudio(withSound, mixed, Options{}, &error) && mixed.size() == 48000 * 2 && AnalyzeAudio(mixed).samplePeakDb > -20.0 && AnalyzeAudio(mixed).samplePeakDb < -12.0, "RenderAudio mixes the clip at its volume (a mono file upmixed, halved: -15 to -18 dB)");
            check(RenderAudio(comp, mixed, Options{}, &error) && mixed.empty(), "no clips, no sound");
            const std::string wavOut = base + "_mix.wav";
            const char* analyze[] = {"video", "audio", "analyze"};
            const char* at[] = {"video", "audio", "at", "0.5s"};
            const char* render[] = {"video", "audio", "render", "-o", wavOut.c_str()};
            check(Cli(withSound, 3, const_cast<char**>(analyze)) == 0, "audio analyze runs");
            check(Cli(withSound, 4, const_cast<char**>(at)) == 0, "audio at runs");
            check(Cli(withSound, 5, const_cast<char**>(render)) == 0 && stat(wavOut.c_str(), &st) == 0 && st.st_size == 44 + 48000 * 2 * 4, "audio render writes a float wav of the mix");
            std::remove(wavOut.c_str());
        }

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

    // scenes: layers at a frame, the overlap cross-fade, marks and their time words
    {
        const std::vector<Scene> scenes = {{"intro", 30, 0, nullptr}, {"main", 40, 10, nullptr}, {"outro", 20, 0, nullptr}};
        check(SceneFrames(scenes) == 90, "scenes add up");
        auto layers = SceneLayers(5, scenes);
        check(layers.size() == 1 && layers[0].scene == 0 && layers[0].local == 5 && layers[0].alpha == 1.f, "a frame in the first scene");
        layers = SceneLayers(25, scenes); // main fades in over the last 10 frames of intro: it starts at 20
        check(layers.size() == 2 && layers[0].scene == 0 && layers[1].scene == 1 && layers[1].local == 5 && std::fabs(layers[1].alpha - 0.5f) < 1e-6f, "the overlap shows both, the new one half in");
        check(std::fabs(layers[0].alpha - 0.5f) < 1e-6f, "the one leaving fades as the new one comes (a dissolve)");
        layers = SceneLayers(30, scenes);
        check(layers.size() == 1 && layers[0].scene == 1 && layers[0].local == 10 && layers[0].length == 50 && layers[0].alpha == 1.f, "after the overlap only the new scene");
        layers = SceneLayers(70, scenes);
        check(layers.size() == 1 && layers[0].scene == 2 && layers[0].local == 0, "a cut at the scene boundary");
        layers = SceneLayers(500, scenes);
        check(layers.size() == 1 && layers[0].scene == 2 && layers[0].local == 19, "past the end the last scene holds its last frame");
        layers = SceneLayers(25.5, scenes);
        check(layers.size() == 2 && layers[1].local == 5 && std::fabs(layers[1].offset - 0.5) < 1e-9, "a blur sample keeps its fraction");

        const Composition made = MakeComposition(64, 36, 30, scenes);
        check(made.frames == 90 && made.marks.size() == 3 && made.marks[1].start == 20 && made.marks[1].frames == 50, "marks start where the scene starts drawing");
        int f = -1;
        check(ParseTime(made, "main", f) && f == 20, "a scene name is its first frame");
        check(ParseTime(made, "MAIN@1s", f) && f == 50, "Scene@time counts from the scene, any case");
        check(ParseTime(made, "main@end", f) && f == 69, "Scene@end is the scene's last frame");
        check(ParseTime(made, "#2", f) && f == 70, "#n is a scene by number");
        check(!ParseTime(made, "nothere@1s", f), "an unknown scene is no time");

        check(std::fabs(SpringIn(0, 30, 0.f, 1.f, 0, 30) - 0.f) < 1e-6f, "a settling spring starts at its start");
        check(std::fabs(SpringIn(30, 30, 0.f, 1.f, 0, 30) - 1.f) < 0.002f, "and is at rest after the frames it was given");
        check(std::fabs(SpringIn(100, 30, 0.f, 1.f, 0, 30) - 1.f) < 0.002f, "and stays there");
    }

    // captions: webvtt with inline word times, plain webvtt, srt
    {
        const std::string vtt =
            "WEBVTT\n\nNOTE a comment\n\n1\n00:00:01.000 --> 00:00:03.000 align:center\nHello <00:00:01.500>big <00:00:02.000><c>world</c>\n\n"
            "00:04.000 --> 00:06.000\nsecond cue\nover two lines\n";
        const std::vector<Cue> cues = ParseCaptions(vtt);
        check(cues.size() == 2, "two cues");
        if (cues.size() == 2) {
            check(std::fabs(cues[0].start - 1.0) < 1e-9 && std::fabs(cues[0].end - 3.0) < 1e-9 && cues[0].text == "Hello big world", "the first cue's time and text");
            check(cues[0].words.size() == 3 && std::fabs(cues[0].words[1].start - 1.5) < 1e-9 && std::fabs(cues[0].words[1].end - 2.0) < 1e-9 && std::fabs(cues[0].words[2].end - 3.0) < 1e-9,
                  "inline timestamps set the words' times");
            check(std::fabs(cues[0].words[0].start - 1.0) < 1e-9, "the first word starts with the cue");
            check(cues[1].text == "second cue over two lines" && cues[1].words.size() == 5 && std::fabs(cues[1].words[1].start - 4.4) < 1e-9, "untimed words are spread evenly");
        }
        const std::vector<Cue> srt = ParseCaptions("1\r\n00:00:00,500 --> 00:00:01,500\r\nfrom srt\r\n\r\n2\r\n01:00:00,000 --> 01:00:02,000\r\nan hour in\r\n");
        check(srt.size() == 2 && std::fabs(srt[0].start - 0.5) < 1e-9 && std::fabs(srt[1].start - 3600.0) < 1e-9, "srt times, with commas and an hour");
        check(ParseCaptions("not captions at all").empty() && ParseCaptions("").empty(), "no cues in text that is not captions");
        check(CueAt(cues, 2.0) == &cues[0] && CueAt(cues, 3.5) == nullptr && CueAt(cues, 4.0) == &cues[1], "the cue showing at a time");
    }

    // audio visualizer analysis: a 1 kHz sine at half scale for two seconds, then silence
    {
        std::vector<float> pcm(48000 * 4 * 2, 0.f);
        for (size_t n = 0; n < 48000 * 2; ++n) {
            pcm[n * 2] = pcm[n * 2 + 1] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * n / 48000.0));
        }
        const AudioAnalysis a(pcm);
        check(std::fabs(a.seconds() - 4.0) < 1e-9, "the analysis knows the length");
        std::vector<float> bands(32);
        a.bands(1.0, bands);
        const size_t top = static_cast<size_t>(std::max_element(bands.begin(), bands.end()) - bands.begin());
        check(top >= 16 && top <= 18 && bands[top] > 0.8f, "the 1 kHz band is the loudest and near full");
        check(bands[2] < 0.3f && bands[30] < 0.3f, "far bands are quiet");
        check(a.level(1.0) > 0.8f && a.level(3.5) == 0.f, "level: loud while it plays, zero in the silence");
        std::vector<float> again(32);
        a.bands(1.0, again);
        check(bands == again, "the same time gives the same bands");
        std::vector<float> after(32);
        a.bands(3.5, after);
        check(*std::max_element(after.begin(), after.end()) == 0.f, "a release does not reach 1.5 s past the end");
        a.bands(2.04, after);
        check(*std::max_element(after.begin(), after.end()) > 0.1f && *std::max_element(after.begin(), after.end()) < bands[top], "just after the end the bands fall away");
        std::vector<float> wave(200);
        a.waveform(1.0, 0.01, wave);
        check(*std::max_element(wave.begin(), wave.end()) > 0.45f && *std::min_element(wave.begin(), wave.end()) < -0.45f, "the waveform reaches the amplitude");
        check(AudioAnalysis(std::vector<float>{}).level(1.0) == 0.f, "no sound is level 0");
    }

    // effects: pure filters on an image
    {
        auto solid = [](int w, int h, unsigned char r, unsigned char g, unsigned char b) {
            Image im;
            im.width = w;
            im.height = h;
            im.rgba.resize(static_cast<size_t>(w) * h * 4);
            for (size_t i = 0; i < im.rgba.size(); i += 4) {
                im.rgba[i] = r;
                im.rgba[i + 1] = g;
                im.rgba[i + 2] = b;
                im.rgba[i + 3] = 255;
            }
            return im;
        };
        auto at = [](Image& im, int x, int y) { return &im.rgba[(static_cast<size_t>(y) * im.width + x) * 4]; };

        Image a = solid(100, 100, 120, 80, 200);
        const Image original = a;
        Grade(a);
        check(a.rgba == original.rgba, "Grade with defaults changes nothing");
        Grade(a, 0.f, 1.f, 0.f);
        check(at(a, 5, 5)[0] == at(a, 5, 5)[1] && at(a, 5, 5)[1] == at(a, 5, 5)[2], "saturation 0 is grey");
        Image b = original;
        Grade(b, 0.2f);
        check(at(b, 5, 5)[0] > 120 && at(b, 5, 5)[3] == 255, "brightness lifts and leaves alpha");

        Image v = solid(200, 100, 200, 200, 200);
        Vignette(v, 0.8f, 0.3f);
        check(at(v, 100, 50)[0] == 200 && at(v, 1, 1)[0] < 120, "a vignette leaves the middle and darkens the corner");

        Image g1 = solid(64, 64, 128, 128, 128), g2 = g1, g3 = g1;
        Grain(g1, 0.1f, 7);
        Grain(g2, 0.1f, 7);
        Grain(g3, 0.1f, 8);
        long sum = 0;
        for (size_t i = 0; i < g1.rgba.size(); i += 4) {
            sum += g1.rgba[i];
        }
        check(g1.rgba == g2.rgba && g1.rgba != g3.rgba && g1.rgba != solid(64, 64, 128, 128, 128).rgba, "grain: same seed same noise, another seed another");
        check(std::labs(sum / (64 * 64) - 128) < 4, "grain is noise around the picture, not a shift");

        Image dot = solid(101, 101, 0, 0, 0);
        at(dot, 50, 50)[0] = at(dot, 50, 50)[1] = at(dot, 50, 50)[2] = 255;
        Blur(dot, 0.05f);
        check(at(dot, 50, 50)[0] < 255 && at(dot, 53, 50)[0] > 0 && at(dot, 0, 0)[0] == 0, "a blur spreads a dot but not across the whole picture");
        Image still = solid(50, 50, 90, 90, 90);
        Blur(still, 0.1f);
        check(at(still, 25, 25)[0] == 90 && at(still, 0, 0)[0] == 90, "a blur leaves a flat picture alone");
        Image clear = solid(50, 50, 255, 0, 0);
        for (size_t i = 3; i < clear.rgba.size(); i += 4) {
            clear.rgba[i] = 0;
        }
        Blur(clear, 0.1f);
        check(at(clear, 25, 25)[3] == 0 && at(clear, 25, 25)[0] == 0, "a fully transparent picture stays so");

        Image halo = solid(101, 101, 0, 0, 0);
        for (int y = 48; y < 53; ++y) {
            for (int x = 48; x < 53; ++x) {
                at(halo, x, y)[0] = at(halo, x, y)[1] = at(halo, x, y)[2] = 255;
            }
        }
        Glow(halo, 0.5f, 0.05f, 1.f);
        check(at(halo, 56, 50)[0] > 0 && at(halo, 0, 0)[0] == 0 && at(halo, 50, 50)[0] == 255, "glow lights the neighbours of something bright");
        Image dark = solid(40, 40, 30, 30, 30);
        Glow(dark, 0.5f, 0.1f, 1.f);
        check(at(dark, 10, 10)[0] == 30, "nothing below the threshold glows");

        Image edge = solid(100, 100, 0, 0, 0);
        for (int y = 0; y < 100; ++y) {
            for (int x = 50; x < 100; ++x) {
                at(edge, x, y)[0] = at(edge, x, y)[1] = at(edge, x, y)[2] = 255;
            }
        }
        Chromatic(edge, 0.03f);
        check(at(edge, 48, 50)[0] == 255 && at(edge, 48, 50)[2] == 0 && at(edge, 52, 50)[2] == 0 && at(edge, 54, 50)[2] == 255, "chromatic: red reaches the edge three pixels early, blue three late");

        Image blocks = solid(40, 40, 0, 0, 0);
        for (int y = 0; y < 40; ++y) {
            for (int x = 0; x < 40; ++x) {
                at(blocks, x, y)[0] = static_cast<unsigned char>(x * 6);
            }
        }
        Pixelate(blocks, 0.25f);
        check(at(blocks, 0, 0)[0] == at(blocks, 9, 9)[0] && at(blocks, 0, 0)[0] != at(blocks, 12, 0)[0], "pixelate: a block is one colour, the next is another");
        Image tiny = solid(10, 10, 5, 5, 5);
        Pixelate(tiny, 0.f);
        Blur(tiny, 0.f);
        Chromatic(tiny, 0.f);
        check(at(tiny, 3, 3)[0] == 5, "zero sizes are no-ops");
    }

    // a sound bound to a scene moves with it
    {
        Composition scenes = comp;
        scenes.fps = 30;
        scenes.marks = {{"intro", 0, 30}, {"main", 30, 60}};
        AudioClip bound;
        bound.path = "x.wav";
        bound.start = 0.5;
        bound.scene = "main";
        AudioClip stray = bound;
        stray.scene = "nope";
        AudioClip plain;
        plain.path = "y.wav";
        plain.start = 2.0;
        scenes.audio = {bound, stray, plain};
        const std::vector<AudioClip> placed = PlacedAudio(scenes);
        check(placed.size() == 3 && std::fabs(placed[0].start - 1.5) < 1e-9, "a scene sound starts after its scene's first frame");
        check(std::fabs(placed[1].start - 0.5) < 1e-9 && std::fabs(placed[2].start - 2.0) < 1e-9, "unbound sounds and unknown scenes keep their time");
        scenes.marks[1].start = 60; // the intro grew by a second: the sound follows
        check(std::fabs(PlacedAudio(scenes)[0].start - 2.5) < 1e-9, "it follows the scene when the scene moves");
    }

    // the sound analysis: a 1 kHz tone at -20 dBFS is -20 LUFS (the K-weighting's offset is made for it)
    {
        std::vector<float> tone(48000 * 3 * 2, 0.f);
        for (size_t n = 0; n < 48000 * 3; ++n) {
            const float s = 0.1f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * n / 48000.0)); // 0.1 = -20 dBFS
            tone[n * 2] = tone[n * 2 + 1] = n >= 48000 && n < 96000 ? 0.f : s; // a second of silence in the middle
        }
        const AudioReport r = AnalyzeAudio(tone);
        check(std::fabs(r.seconds - 3.0) < 1e-9, "the length");
        check(std::fabs(r.samplePeakDb - -20.0) < 0.1, "the sample peak");
        check(std::fabs(r.truePeakDb - -20.0) < 0.3, "the true peak of a sine is its peak");
        check(r.lufs < -19.5 && r.lufs > -22.0, "the loudness of a quiet tone");
        check(r.clippedSamples == 0, "no clipping");
        check(r.silent.size() == 1 && std::fabs(r.silent[0].first - 1.0) < 0.06 && std::fabs(r.silent[0].second - 2.0) < 0.06, "the silent second is found");
        std::vector<float> hot(48000 * 2, 1.f);
        check(AnalyzeAudio(hot).clippedSamples == 48000 * 2, "full scale samples are counted");
        check(AnalyzeAudio({}).lufs == -120.0, "nothing is nothing");
    }

    // the command line: time words, and the commands writing files
    {
        int f = -1;
        check(ParseTime(comp, "end", f) && f == comp.frames - 1, "end is the last frame");
        check(ParseTime(comp, "start", f) && f == 0, "start is frame 0");
        check(ParseTime(comp, "4", f) && f == 4, "a bare number is a frame");
        check(ParseTime(comp, "0.5s", f) && f == std::min(comp.frames - 1, static_cast<int>(std::lround(0.5 * comp.fps))), "seconds");
        check(ParseTime(comp, "500ms", f) && f == std::min(comp.frames - 1, static_cast<int>(std::lround(0.5 * comp.fps))), "milliseconds");
        check(ParseTime(comp, "0:01.5", f) && f == std::min(comp.frames - 1, static_cast<int>(std::lround(1.5 * comp.fps))), "minutes and seconds");
        check(ParseTime(comp, "50%", f) && f == static_cast<int>(std::lround(0.5 * (comp.frames - 1))), "a percentage");
        check(ParseTime(comp, "9999", f) && f == comp.frames - 1, "a time past the end is clamped");
        check(!ParseTime(comp, "soon", f) && !ParseTime(comp, "", f), "words that are no time");

        const std::string shots = dir + "/imshell_video_cli_frames";
        std::string sheet = base + "_strip.png";
        const char* frameArgs[] = {"video", "frame", "3,end", "-o", shots.c_str(), "--crop", "8,4,16,8"};
        check(Cli(comp, 7, const_cast<char**>(frameArgs)) == 0, "frame writes");
        const char* stripArgs[] = {"video", "strip", "all", "-n", "5", "--columns", "3", "--width", "32", "-o", sheet.c_str()};
        check(Cli(comp, 11, const_cast<char**>(stripArgs)) == 0, "strip writes");
        const char* verifyArgs[] = {"video", "verify"};
        check(Cli(comp, 2, const_cast<char**>(verifyArgs)) == 0, "verify agrees on a deterministic composition");
        const char* timelineArgs[] = {"video", "timeline", "--json"};
        check(Cli(comp, 3, const_cast<char**>(timelineArgs)) == 0, "timeline prints");
        const char* badArgs[] = {"video", "frame", "soon"};
        check(Cli(comp, 3, const_cast<char**>(badArgs)) == 1, "a bad time is a usage error");
        if (haveProbe) {
            // the crop is zoomed to about 960 px wide; the strip is 3 columns of 32 px and 2 rows with 8 px gaps
            check(videotest::capture("ffprobe -v error -show_entries stream=width -of csv=p=0 " + videotest::quoted(shots + "/frame_3.png")).find("960") == 0, "the crop is zoomed");
            check(videotest::capture("ffprobe -v error -show_entries stream=width,height -of csv=p=0 " + videotest::quoted(sheet)).find("128,") == 0, "the strip has its columns");
        }
        std::remove(sheet.c_str());
        std::filesystem::remove_all(shots);
    }

    if (failures == 0) {
        std::puts("video generic tests passed");
    }
    return failures == 0 ? 0 : 1;
}

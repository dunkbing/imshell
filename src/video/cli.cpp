#include "imshell/video_cli.hpp"

#include "imshell/video_check.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>

namespace imshell::video {
    namespace {
        // a double that is the whole of `s`
        bool number(const std::string& s, double& out) {
            if (s.empty()) {
                return false;
            }
            char* end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && *end == '\0';
        }

        std::vector<std::string> split(const std::string& s, char sep) {
            std::vector<std::string> out;
            std::stringstream stream(s);
            for (std::string part; std::getline(stream, part, sep);) {
                out.push_back(part);
            }
            return out;
        }

        std::string quote(const std::string& s) {
            std::string out = "\"";
            for (char c : s) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                }
                out += c;
            }
            return out + "\"";
        }

        // a scene by name (any case) or `#n`
        const Mark* findMark(const Composition& comp, const std::string& name) {
            auto lower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return s;
            };
            if (name.size() > 1 && name[0] == '#') {
                double n = 0;
                if (number(name.substr(1), n) && n >= 0 && n < static_cast<double>(comp.marks.size())) {
                    return &comp.marks[static_cast<size_t>(n)];
                }
                return nullptr;
            }
            for (const Mark& mark : comp.marks) {
                if (lower(mark.name) == lower(name)) {
                    return &mark;
                }
            }
            return nullptr;
        }

        // `a..b` (b excluded), `a..`, `..b`, `all`, a scene's name, or one time; false on a word it does not know
        bool parseRange(const Composition& comp, const std::string& text, int& from, int& to) {
            if (text == "all") {
                from = 0;
                to = comp.frames;
                return true;
            }
            if (const Mark* mark = findMark(comp, text)) {
                from = std::clamp(mark->start, 0, comp.frames - 1);
                to = std::clamp(mark->start + mark->frames, from + 1, comp.frames);
                return true;
            }
            const size_t dots = text.find("..");
            if (dots == std::string::npos) {
                return false;
            }
            const std::string a = text.substr(0, dots), b = text.substr(dots + 2);
            from = 0;
            to = comp.frames;
            if (!a.empty() && !ParseTime(comp, a, from)) {
                return false;
            }
            if (!b.empty()) {
                if (!ParseTime(comp, b, to)) {
                    return false;
                }
                if (b == "end") {
                    to = comp.frames;
                }
            }
            return to > from;
        }

        // the arguments after the command: positionals, `--flag`, `--flag value`
        struct Args {
            std::vector<std::string> positional;
            std::map<std::string, std::string> flags;
            bool has(const char* flag) const { return flags.count(flag) > 0; }
            std::string get(const char* flag, const std::string& fallback = "") const {
                const auto it = flags.find(flag);
                return it == flags.end() ? fallback : it->second;
            }
            double num(const char* flag, double fallback) const {
                double v = fallback;
                return has(flag) && number(get(flag), v) ? v : fallback;
            }
        };

        Args parse(int argc, char** argv, int from) {
            static const char* const booleans[] = {"--json", "--draft", "--ffmpeg", "--paused", "--help", "--no-pops", "--images", "--transparent"};
            Args args;
            for (int i = from; i < argc; ++i) {
                const std::string a = argv[i];
                if (a.size() > 1 && a[0] == '-' && !std::isdigit(static_cast<unsigned char>(a[1])) && a[1] != '.') {
                    const bool boolean = std::find(std::begin(booleans), std::end(booleans), a) != std::end(booleans);
                    args.flags[a] = !boolean && i + 1 < argc ? argv[++i] : "";
                } else {
                    args.positional.push_back(a);
                }
            }
            return args;
        }

        // the composition at the size asked for: --draft is half, --scale any factor; even, as h.264 needs
        Composition scaled(const Composition& comp, const Args& args) {
            Composition out = comp;
            out.transparent = out.transparent || args.has("--transparent");
            const double factor = args.has("--draft") ? 0.5 : args.num("--scale", 1.0);
            if (factor > 0.0 && factor != 1.0) {
                out.width = std::max(2, static_cast<int>(std::lround(comp.width * factor / 2.0)) * 2);
                out.height = std::max(2, static_cast<int>(std::lround(comp.height * factor / 2.0)) * 2);
            }
            return out;
        }

        bool timesOf(const Composition& comp, const std::string& list, std::vector<int>& frames) {
            for (const std::string& word : split(list, ',')) {
                int f = 0;
                if (!ParseTime(comp, word, f)) {
                    std::fprintf(stderr, "not a time: %s\n", word.c_str());
                    return false;
                }
                frames.push_back(f);
            }
            return !frames.empty();
        }

        // `n` frames evenly spread over [from, to], both ends included
        std::vector<int> spread(int from, int to, int n) {
            std::vector<int> frames;
            const int last = to - 1;
            n = std::clamp(n, 1, std::max(1, last - from + 1));
            for (int i = 0; i < n; ++i) {
                frames.push_back(n == 1 ? from : from + static_cast<int>(std::lround(static_cast<double>(last - from) * i / (n - 1))));
            }
            return frames;
        }

        // area average down to w x h
        Image shrink(const Image& in, int w, int h) {
            Image out;
            out.width = w;
            out.height = h;
            out.rgba.resize(static_cast<size_t>(w) * h * 4);
            for (int y = 0; y < h; ++y) {
                const int y0 = y * in.height / h, y1 = std::max(y0 + 1, (y + 1) * in.height / h);
                for (int x = 0; x < w; ++x) {
                    const int x0 = x * in.width / w, x1 = std::max(x0 + 1, (x + 1) * in.width / w);
                    unsigned sum[4] = {0, 0, 0, 0};
                    for (int sy = y0; sy < y1; ++sy) {
                        for (int sx = x0; sx < x1; ++sx) {
                            const unsigned char* p = &in.rgba[(static_cast<size_t>(sy) * in.width + sx) * 4];
                            for (int c = 0; c < 4; ++c) {
                                sum[c] += p[c];
                            }
                        }
                    }
                    const unsigned count = static_cast<unsigned>((y1 - y0) * (x1 - x0));
                    for (int c = 0; c < 4; ++c) {
                        out.rgba[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<unsigned char>(sum[c] / count);
                    }
                }
            }
            return out;
        }

        // nearest neighbour up to w x h: a crop is looked at in sharp pixels
        Image grow(const Image& in, int w, int h) {
            Image out;
            out.width = w;
            out.height = h;
            out.rgba.resize(static_cast<size_t>(w) * h * 4);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    std::memcpy(&out.rgba[(static_cast<size_t>(y) * w + x) * 4], &in.rgba[(static_cast<size_t>(y * in.height / h) * in.width + x * in.width / w) * 4], 4);
                }
            }
            return out;
        }

        Image crop(const Image& in, int x, int y, int w, int h) {
            x = std::clamp(x, 0, in.width - 1);
            y = std::clamp(y, 0, in.height - 1);
            w = std::clamp(w, 1, in.width - x);
            h = std::clamp(h, 1, in.height - y);
            Image out;
            out.width = w;
            out.height = h;
            out.rgba.resize(static_cast<size_t>(w) * h * 4);
            for (int row = 0; row < h; ++row) {
                std::memcpy(&out.rgba[static_cast<size_t>(row) * w * 4], &in.rgba[(static_cast<size_t>(y + row) * in.width + x) * 4], static_cast<size_t>(w) * 4);
            }
            return out;
        }

        // shortcut: a 3x5 digit font for the frame labels, no letters; swap for the real font when a label needs words
        void drawNumber(Image& image, int x, int y, int value, int scale) {
            static const unsigned short digits[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
            const std::string text = std::to_string(value);
            const int width = static_cast<int>(text.size()) * 4 * scale + scale;
            for (int py = -scale; py < 5 * scale + scale; ++py) { // a dark chip behind the digits
                for (int px = -scale; px < width; ++px) {
                    if (x + px >= 0 && x + px < image.width && y + py >= 0 && y + py < image.height) {
                        unsigned char* p = &image.rgba[(static_cast<size_t>(y + py) * image.width + x + px) * 4];
                        p[0] = p[1] = p[2] = 20;
                    }
                }
            }
            for (size_t d = 0; d < text.size(); ++d) {
                const unsigned short bits = digits[text[d] - '0'];
                for (int row = 0; row < 5; ++row) {
                    for (int col = 0; col < 3; ++col) {
                        if (!(bits & (1 << (14 - row * 3 - col)))) {
                            continue;
                        }
                        for (int sy = 0; sy < scale; ++sy) {
                            for (int sx = 0; sx < scale; ++sx) {
                                const int px = x + (static_cast<int>(d) * 4 + col) * scale + sx, py = y + row * scale + sy;
                                if (px >= 0 && px < image.width && py >= 0 && py < image.height) {
                                    unsigned char* p = &image.rgba[(static_cast<size_t>(py) * image.width + px) * 4];
                                    p[0] = p[1] = p[2] = 255;
                                }
                            }
                        }
                    }
                }
            }
        }

        std::string seconds(const Composition& comp, int frame) {
            char text[32];
            std::snprintf(text, sizeof text, "%.3f", static_cast<double>(frame) / comp.fps);
            return text;
        }

        const char*& programName() { // set by Cli, for the usage text of every command
            static const char* name = "video";
            return name;
        }

        int usage(const char* name, bool error) {
            std::fprintf(error ? stderr : stdout,
                         "usage: %s <command> [options]\n"
                         "  timeline                          size, rate, length and the sound clips\n"
                         "  frame TIMES [-o dir] [--crop x,y,w,h] [--zoom N]\n"
                         "                                    full size pngs (frame_<n>.png); a crop is shown zoomed\n"
                         "  strip [RANGE | TIMES] [-n 12] [--columns 4] [--width 480] [-o strip.png]\n"
                         "                                    a contact sheet, each cell labelled with its frame\n"
                         "  render [RANGE] [-o out.mp4] [--draft] [--scale F] [--ffmpeg] [--codec h264|hevc|vp9|prores] [--bitrate KBPS] [--crf N] [--images] [--transparent]\n"
                         "                                    encode (--draft: half size; a codec, bitrate or crf other than the default, or --ffmpeg, uses the ffmpeg binary;\n"
                         "                                    the extension picks the container: .mp4, .webm, .mov; --images: -o is a folder of frame_000000.png;\n"
                         "                                    --transparent: clear to transparent, for prores .mov, vp9 .webm or --images)\n"
                         "  preview [TIME] [--paused]         the real-time window\n"
                         "  verify [RANGE]                    playing in order and seeking must give the same pictures\n"
                         "  inspect [RANGE] [--no-pops] [--fail-on warning]\n"
                         "                                    windows cut off by the canvas, blank frames, pops between frames (exit 2 on errors)\n"
                         "  audio analyze [RANGE]             loudness, peaks, clipping and silence of the mix (exit 2 on clipping)\n"
                         "  audio at TIMES                    which sounds play at a moment, from where in the file, how loud\n"
                         "  audio render [RANGE] [-o a.wav]   the mix as a 32 bit float wav\n"
                         "times: 120 (a frame), 3.2s, 500ms, 1:05.5, 50%%, start, end, a scene (outro, outro@1.2s, outro@end, #2). ranges: a..b (b excluded), a.., ..b, all, a scene\n"
                         "--json prints one json object on stdout; sizes with --draft/--scale need a draw function that lays out from Frame::width/height\n",
                         name);
            return error ? 1 : 0;
        }

        // ---- commands

        int timeline(const Composition& comp, const Args& args) {
            const double length = static_cast<double>(comp.frames) / comp.fps;
            if (args.has("--json")) {
                std::printf("{\"width\":%d,\"height\":%d,\"fps\":%d,\"frames\":%d,\"seconds\":%.3f,\"audio\":[", comp.width, comp.height, comp.fps, comp.frames, length);
                const std::vector<AudioClip> placed = PlacedAudio(comp);
                for (size_t i = 0; i < placed.size(); ++i) {
                    const AudioClip& c = placed[i];
                    std::printf("%s{\"path\":%s,\"start\":%.3f,\"offset\":%.3f,\"duration\":%.3f,\"volume\":%.3f,\"fadeIn\":%.3f,\"fadeOut\":%.3f}", i ? "," : "",
                                quote(c.path).c_str(), c.start, c.offset, c.duration, c.volume, c.fadeIn, c.fadeOut);
                }
                std::printf("],\"scenes\":[");
                for (size_t i = 0; i < comp.marks.size(); ++i) {
                    const Mark& m = comp.marks[i];
                    std::printf("%s{\"name\":%s,\"start\":%d,\"frames\":%d,\"startSeconds\":%s}", i ? "," : "", quote(m.name).c_str(), m.start, m.frames, seconds(comp, m.start).c_str());
                }
                std::printf("]}\n");
                return 0;
            }
            std::printf("%dx%d at %d fps, %d frames, %.3f s\n", comp.width, comp.height, comp.fps, comp.frames, length);
            for (const Mark& m : comp.marks) {
                std::printf("scene %s: frames %d..%d (%s s, %.3f s long)\n", m.name.c_str(), m.start, m.start + m.frames, seconds(comp, m.start).c_str(), static_cast<double>(m.frames) / comp.fps);
            }
            if (comp.audio.empty()) {
                std::printf("no sound\n");
            }
            for (const AudioClip& c : PlacedAudio(comp)) {
                std::printf("sound %s: from %.3f s, file offset %.3f s, %s, volume %.2f, fade in %.2f s, fade out %.2f s\n", c.path.c_str(), c.start, c.offset,
                            c.duration < 0 ? "to the end of the file" : (std::to_string(c.duration) + " s").c_str(), c.volume, c.fadeIn, c.fadeOut);
            }
            for (const AudioClip& c : comp.audio) {
                const bool known = std::any_of(comp.marks.begin(), comp.marks.end(), [&](const Mark& m) { return m.name == c.scene; });
                if (!c.scene.empty() && !known) {
                    std::printf("note: sound %s names the scene \"%s\", which the composition does not have (its start is on the whole timeline)\n", c.path.c_str(), c.scene.c_str());
                }
            }
            return 0;
        }

        int frameCommand(const Composition& comp, const Args& args) {
            std::vector<int> frames;
            if (args.positional.size() < 2 || !timesOf(comp, args.positional[1], frames)) {
                return usage(programName(), true);
            }
            const std::string dir = args.get("-o", "frames");
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::vector<std::string> written;
            for (int f : frames) {
                std::string error;
                Image image = RenderFrame(comp, f, &error);
                if (image.width == 0) {
                    std::fprintf(stderr, "frame %d failed: %s\n", f, error.c_str());
                    return 1;
                }
                if (args.has("--crop")) {
                    const std::vector<std::string> p = split(args.get("--crop"), ',');
                    double v[4] = {0, 0, 0, 0};
                    if (p.size() != 4 || !number(p[0], v[0]) || !number(p[1], v[1]) || !number(p[2], v[2]) || !number(p[3], v[3])) {
                        std::fprintf(stderr, "--crop wants x,y,w,h in video pixels\n");
                        return 1;
                    }
                    image = crop(image, static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]), static_cast<int>(v[3]));
                    const double zoom = args.num("--zoom", 960.0 / image.width); // about 960 px wide
                    image = grow(image, std::max(1, static_cast<int>(image.width * zoom)), std::max(1, static_cast<int>(image.height * zoom)));
                }
                const std::string path = (std::filesystem::path(dir) / ("frame_" + std::to_string(f) + ".png")).string();
                if (!SavePng(image, path)) {
                    std::fprintf(stderr, "could not write %s\n", path.c_str());
                    return 1;
                }
                written.push_back(path);
            }
            if (args.has("--json")) {
                std::printf("{\"files\":[");
                for (size_t i = 0; i < written.size(); ++i) {
                    std::printf("%s%s", i ? "," : "", quote(written[i]).c_str());
                }
                std::printf("]}\n");
            } else {
                for (const std::string& path : written) {
                    std::printf("%s\n", path.c_str());
                }
            }
            return 0;
        }

        int strip(const Composition& comp, const Args& args) {
            std::vector<int> frames;
            int from = 0, to = comp.frames;
            if (args.positional.size() >= 2) {
                const std::string& what = args.positional[1];
                if (parseRange(comp, what, from, to)) {
                    frames = spread(from, to, static_cast<int>(args.num("-n", 12)));
                } else if (!timesOf(comp, what, frames)) {
                    return 1;
                }
            } else {
                frames = spread(from, to, static_cast<int>(args.num("-n", 12)));
            }
            const int width = std::max(32, static_cast<int>(args.num("--width", 480)));
            const int height = std::max(2, width * comp.height / comp.width);
            const int columns = std::clamp(static_cast<int>(args.num("--columns", std::min<size_t>(frames.size(), 4))), 1, static_cast<int>(frames.size()));
            const int rows = (static_cast<int>(frames.size()) + columns - 1) / columns;
            const int gap = 8;
            Image sheet;
            sheet.width = columns * width + (columns + 1) * gap;
            sheet.height = rows * height + (rows + 1) * gap;
            sheet.rgba.assign(static_cast<size_t>(sheet.width) * sheet.height * 4, 40);
            for (size_t i = 0; i < frames.size(); ++i) {
                std::string error;
                const Image frame = RenderFrame(comp, frames[i], &error);
                if (frame.width == 0) {
                    std::fprintf(stderr, "frame %d failed: %s\n", frames[i], error.c_str());
                    return 1;
                }
                const Image cell = shrink(frame, width, height);
                const int ox = gap + static_cast<int>(i % columns) * (width + gap), oy = gap + static_cast<int>(i / columns) * (height + gap);
                for (int y = 0; y < height; ++y) {
                    std::memcpy(&sheet.rgba[(static_cast<size_t>(oy + y) * sheet.width + ox) * 4], &cell.rgba[static_cast<size_t>(y) * width * 4], static_cast<size_t>(width) * 4);
                }
                drawNumber(sheet, ox + 4, oy + 4, frames[i], 2);
            }
            const std::string path = args.get("-o", "strip.png");
            if (!SavePng(sheet, path)) {
                std::fprintf(stderr, "could not write %s\n", path.c_str());
                return 1;
            }
            if (args.has("--json")) {
                std::printf("{\"file\":%s,\"columns\":%d,\"frames\":[", quote(path).c_str(), columns);
                for (size_t i = 0; i < frames.size(); ++i) {
                    std::printf("%s{\"frame\":%d,\"seconds\":%s}", i ? "," : "", frames[i], seconds(comp, frames[i]).c_str());
                }
                std::printf("]}\n");
            } else {
                std::printf("%s: %d frames, %d columns\n", path.c_str(), static_cast<int>(frames.size()), columns);
                for (int f : frames) {
                    std::printf("  frame %d  %s s\n", f, seconds(comp, f).c_str());
                }
            }
            return 0;
        }

        int render(const Composition& comp, const Args& args) {
            Options options;
            int from = 0, to = comp.frames;
            if (args.positional.size() >= 2 && !parseRange(comp, args.positional[1], from, to)) {
                std::fprintf(stderr, "not a range: %s\n", args.positional[1].c_str());
                return 1;
            }
            options.from = from;
            options.to = to;
            options.codec = args.get("--codec", "h264");
            options.bitrate = static_cast<int>(args.num("--bitrate", 0));
            options.crf = static_cast<int>(args.num("--crf", -1));
            if (options.codec != "h264" && options.codec != "hevc" && options.codec != "vp9" && options.codec != "prores") {
                std::fprintf(stderr, "--codec is h264, hevc, vp9 or prores\n");
                return 1;
            }
            if (args.has("--images")) { // a numbered png per frame, with the alpha the renderer gives
                const std::string dir = args.get("-o", "frames");
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                std::string error;
                int written = 0;
                options.progress = [](int done, int total) {
                    std::fprintf(stderr, "\r%d / %d", done, total);
                    return true;
                };
                const bool played = Frames(comp, options, [&](int n, const Image& image) {
                    char name[32];
                    std::snprintf(name, sizeof name, "frame_%06d.png", n);
                    ++written;
                    return SavePng(image, (std::filesystem::path(dir) / name).string());
                }, &error);
                std::fprintf(stderr, "\n");
                if (!played || written != to - from) {
                    std::fprintf(stderr, "failed: %s\n", error.empty() ? "could not write the images" : error.c_str());
                    return 1;
                }
                std::printf("%s: %d images, %dx%d\n", dir.c_str(), written, comp.width, comp.height);
                return 0;
            }
            if (args.has("--ffmpeg")) {
                options.encoder = Encoder::Ffmpeg;
            }
            options.progress = [](int done, int total) {
                std::fprintf(stderr, "\r%d / %d", done, total);
                return true;
            };
            const std::string path = args.get("-o", "out.mp4");
            std::string error;
            const bool ok = RenderVideo(comp, path, options, &error);
            std::fprintf(stderr, "\n");
            if (!ok) {
                std::fprintf(stderr, "failed: %s\n", error.c_str());
                return 1;
            }
            if (args.has("--json")) {
                std::printf("{\"file\":%s,\"width\":%d,\"height\":%d,\"frames\":%d,\"seconds\":%.3f}\n", quote(path).c_str(), comp.width, comp.height, to - from,
                            static_cast<double>(to - from) / comp.fps);
            } else {
                std::printf("%s: %dx%d, %d frames, %.3f s\n", path.c_str(), comp.width, comp.height, to - from, static_cast<double>(to - from) / comp.fps);
            }
            return 0;
        }

        int previewCommand(const Composition& comp, const Args& args) {
            PreviewOptions options;
            options.play = !args.has("--paused");
            if (args.positional.size() >= 2 && !ParseTime(comp, args.positional[1], options.startFrame)) {
                std::fprintf(stderr, "not a time: %s\n", args.positional[1].c_str());
                return 1;
            }
            options.closeAfter = args.num("--close", 0.0);
            Preview(comp, options);
            return 0;
        }

        int verify(const Composition& comp, const Args& args) {
            int from = 0, to = comp.frames;
            if (args.positional.size() >= 2 && !parseRange(comp, args.positional[1], from, to)) {
                std::fprintf(stderr, "not a range: %s\n", args.positional[1].c_str());
                return 1;
            }
            const std::vector<int> frames = spread(from, to, 8);
            std::string report;
            const bool same = Verify(comp, frames, &report);
            if (args.has("--json")) {
                std::printf("{\"same\":%s,\"report\":%s}\n", same ? "true" : "false", quote(report).c_str());
            } else {
                std::printf("%s", same ? "playing and seeking agree\n" : report.c_str());
            }
            return same ? 0 : 2;
        }

        int inspectCommand(const Composition& comp, const Args& args) {
            InspectOptions options;
            int from = 0, to = comp.frames;
            if (args.positional.size() >= 2 && !parseRange(comp, args.positional[1], from, to)) {
                std::fprintf(stderr, "not a range: %s\n", args.positional[1].c_str());
                return 1;
            }
            options.from = from;
            options.to = to;
            options.pops = !args.has("--no-pops");
            std::string error;
            const std::vector<Finding> findings = Inspect(comp, options, &error);
            if (!error.empty()) {
                std::fprintf(stderr, "inspect failed: %s\n", error.c_str());
                return 1;
            }
            int errors = 0, warnings = 0;
            for (const Finding& f : findings) {
                errors += f.severity == "error";
                warnings += f.severity == "warning";
            }
            if (args.has("--json")) {
                std::printf("{\"errors\":%d,\"warnings\":%d,\"findings\":[", errors, warnings);
                for (size_t i = 0; i < findings.size(); ++i) {
                    const Finding& f = findings[i];
                    std::printf("%s{\"severity\":%s,\"kind\":%s,\"what\":%s,\"scene\":%s,\"from\":%d,\"to\":%d,\"detail\":%s,\"look\":%s}", i ? "," : "", quote(f.severity).c_str(),
                                quote(f.kind).c_str(), quote(f.what).c_str(), quote(f.scene).c_str(), f.from, f.to, quote(f.detail).c_str(), quote(f.look).c_str());
                }
                std::printf("]}\n");
            } else {
                for (const Finding& f : findings) {
                    std::printf("%-7s %-7s %s%s frames %d..%d%s: %s\n        look: %s\n", f.severity.c_str(), f.kind.c_str(), f.what.c_str(), f.what.empty() ? "" : "", f.from, f.to,
                                f.scene.empty() ? "" : (" (" + f.scene + ")").c_str(), f.detail.c_str(), f.look.c_str());
                }
                std::printf("%d errors, %d warnings, %d notes\n", errors, warnings, static_cast<int>(findings.size()) - errors - warnings);
            }
            const bool failOnWarning = args.get("--fail-on") == "warning";
            return errors > 0 || (failOnWarning && warnings > 0) ? 2 : 0;
        }

        // shortcut: 32 bit float wav, the one format that needs no conversion; an editor that wants 16 bit converts
        bool writeWav(const std::string& path, const std::vector<float>& pcm) {
            std::FILE* file = std::fopen(path.c_str(), "wb");
            if (!file) {
                return false;
            }
            auto u32 = [&](unsigned v) { std::fwrite(&v, 4, 1, file); };
            auto u16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, file); };
            const unsigned bytes = static_cast<unsigned>(pcm.size() * 4);
            std::fwrite("RIFF", 1, 4, file);
            u32(36 + bytes);
            std::fwrite("WAVEfmt ", 1, 8, file);
            u32(16);
            u16(3); // ieee float
            u16(2);
            u32(48000);
            u32(48000 * 8);
            u16(8);
            u16(32);
            std::fwrite("data", 1, 4, file);
            u32(bytes);
            const bool ok = std::fwrite(pcm.data(), 4, pcm.size(), file) == pcm.size();
            std::fclose(file);
            return ok;
        }

        // the gain a clip has at `t` seconds of the timeline from its volume and fades
        double clipGain(const AudioClip& c, double t) {
            double gain = c.volume;
            if (c.fadeIn > 0.0) {
                gain *= std::clamp((t - c.start) / c.fadeIn, 0.0, 1.0);
            }
            if (c.fadeOut > 0.0 && c.duration >= 0.0) {
                gain *= std::clamp((c.start + c.duration - t) / c.fadeOut, 0.0, 1.0);
            }
            return gain;
        }

        int audioCommand(const Composition& comp, const Args& args) {
            const std::string sub = args.positional.size() >= 2 ? args.positional[1] : "";
            if (sub == "at") {
                std::vector<int> frames;
                if (args.positional.size() < 3 || !timesOf(comp, args.positional[2], frames)) {
                    return usage(programName(), true);
                }
                for (int f : frames) {
                    const double t = static_cast<double>(f) / comp.fps;
                    std::printf("%s s (frame %d):\n", seconds(comp, f).c_str(), f);
                    bool any = false;
                    for (const AudioClip& c : PlacedAudio(comp)) {
                        const bool playing = t >= c.start && (c.duration < 0.0 || t < c.start + c.duration);
                        if (!playing) {
                            continue;
                        }
                        any = true;
                        const double gain = clipGain(c, t);
                        std::printf("  %s at %.3f s into the file, gain %.2f (%.1f dB)\n", c.path.c_str(), c.offset + (t - c.start), gain, gain > 1e-6 ? 20.0 * std::log10(gain) : -120.0);
                    }
                    if (!any) {
                        std::printf("  nothing plays\n");
                    }
                }
                return 0;
            }
            if (sub != "analyze" && sub != "render") {
                return usage(programName(), true);
            }
            Options options;
            int from = 0, to = comp.frames;
            if (args.positional.size() >= 3 && !parseRange(comp, args.positional[2], from, to)) {
                std::fprintf(stderr, "not a range: %s\n", args.positional[2].c_str());
                return 1;
            }
            options.from = from;
            options.to = to;
            std::vector<float> pcm;
            std::string error;
            if (!RenderAudio(comp, pcm, options, &error)) {
                std::fprintf(stderr, "failed: %s\n", error.c_str());
                return 1;
            }
            if (sub == "render") {
                const std::string path = args.get("-o", "audio.wav");
                if (pcm.empty() || !writeWav(path, pcm)) {
                    std::fprintf(stderr, pcm.empty() ? "the composition has no sound\n" : "could not write %s\n", path.c_str());
                    return 1;
                }
                std::printf("%s: %.3f s\n", path.c_str(), static_cast<double>(pcm.size() / 2) / 48000.0);
                return 0;
            }
            if (pcm.empty()) {
                std::printf(args.has("--json") ? "{\"sound\":false}\n" : "no sound\n");
                return 0;
            }
            const AudioReport r = AnalyzeAudio(pcm);
            if (args.has("--json")) {
                std::printf("{\"sound\":true,\"seconds\":%.3f,\"lufs\":%.2f,\"truePeakDb\":%.2f,\"samplePeakDb\":%.2f,\"rmsDb\":%.2f,\"clippedSamples\":%d,\"silent\":[", r.seconds, r.lufs,
                            r.truePeakDb, r.samplePeakDb, r.rmsDb, r.clippedSamples);
                for (size_t i = 0; i < r.silent.size(); ++i) {
                    std::printf("%s[%.3f,%.3f]", i ? "," : "", r.silent[i].first, r.silent[i].second);
                }
                std::printf("]}\n");
            } else {
                std::printf("%.3f s\nloudness %.1f LUFS (about -14 for web video)\ntrue peak %.1f dBTP (keep below -1)\nsample peak %.1f dBFS, rms %.1f dBFS\n", r.seconds, r.lufs, r.truePeakDb,
                            r.samplePeakDb, r.rmsDb);
                std::printf(r.clippedSamples ? "CLIPPING: %d samples at full scale\n" : "no clipping (%d samples at full scale)\n", r.clippedSamples);
                for (const auto& s : r.silent) {
                    std::printf("silence %.3f s .. %.3f s\n", s.first, s.second);
                }
            }
            return r.clippedSamples > 0 ? 2 : 0;
        }
    } // namespace

    namespace {
        // `text` as a number of frames in a stretch of `count` frames at `fps`; not clamped
        bool timeWord(int fps, int count, const std::string& text, double& frames) {
            double v = 0.0;
            if (text == "start") {
                frames = 0;
            } else if (text == "end") {
                frames = count - 1;
            } else if (text.size() > 1 && text.back() == '%' && number(text.substr(0, text.size() - 1), v)) {
                frames = v / 100.0 * (count - 1);
            } else if (text.size() > 2 && text.compare(text.size() - 2, 2, "ms") == 0 && number(text.substr(0, text.size() - 2), v)) {
                frames = v / 1000.0 * fps;
            } else if (text.size() > 1 && text.back() == 's' && number(text.substr(0, text.size() - 1), v)) {
                frames = v * fps;
            } else if (text.find(':') != std::string::npos) { // m:ss.s or h:mm:ss
                double total = 0.0;
                for (const std::string& part : split(text, ':')) {
                    if (!number(part, v)) {
                        return false;
                    }
                    total = total * 60.0 + v;
                }
                frames = total * fps;
            } else if (number(text, v)) {
                frames = v; // a bare number is a frame
            } else {
                return false;
            }
            return true;
        }
    } // namespace

    bool ParseTime(const Composition& comp, const std::string& text, int& frame) {
        double frames = 0.0;
        const size_t at = text.find('@');
        if (at != std::string::npos) { // Scene@time: the time counts from the scene's first frame
            const Mark* mark = findMark(comp, text.substr(0, at));
            if (!mark || !timeWord(comp.fps, mark->frames, text.substr(at + 1), frames)) {
                return false;
            }
            frames += mark->start;
        } else if (const Mark* mark = findMark(comp, text)) {
            frames = mark->start;
        } else if (!timeWord(comp.fps, comp.frames, text, frames)) {
            return false;
        }
        frame = std::clamp(static_cast<int>(std::lround(frames)), 0, comp.frames - 1);
        return true;
    }

    int Cli(const Composition& comp, int argc, char** argv, const char* name) {
        if (argc < 2 || !std::strcmp(argv[1], "help") || !std::strcmp(argv[1], "--help")) {
            return usage(name, argc < 2);
        }
        programName() = name;
        const Args args = parse(argc, argv, 1);
        const std::string command = argv[1];
        const Composition sized = scaled(comp, args);
        if (command == "timeline") {
            return timeline(sized, args);
        }
        if (command == "frame") {
            return frameCommand(sized, args);
        }
        if (command == "strip") {
            return strip(sized, args);
        }
        if (command == "render") {
            return render(sized, args);
        }
        if (command == "preview") {
            return previewCommand(sized, args);
        }
        if (command == "inspect") {
            return inspectCommand(sized, args);
        }
        if (command == "audio") {
            return audioCommand(sized, args);
        }
        if (command == "verify") {
            return verify(sized, args);
        }
        std::fprintf(stderr, "unknown command: %s\n", command.c_str());
        return usage(name, true);
    }
} // namespace imshell::video

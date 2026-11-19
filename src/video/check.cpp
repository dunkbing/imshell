#include "imshell/video_check.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace imshell::video {
    namespace {
        constexpr int kRate = 48000;

        double toDb(double amplitude) { return amplitude > 1e-6 ? 20.0 * std::log10(amplitude) : -120.0; }

        struct Biquad {
            double b0, b1, b2, a1, a2;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            double run(double x) {
                const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1;
                x1 = x;
                y2 = y1;
                y1 = y;
                return y;
            }
        };

        double lanczos(double x) {
            if (x == 0.0) {
                return 1.0;
            }
            if (std::fabs(x) >= 4.0) {
                return 0.0;
            }
            const double px = 3.14159265358979323846 * x;
            return std::sin(px) / px * std::sin(px / 4.0) / (px / 4.0);
        }
    } // namespace

    AudioReport AnalyzeAudio(const std::vector<float>& pcm) {
        AudioReport report;
        const size_t frames = pcm.size() / 2;
        report.seconds = static_cast<double>(frames) / kRate;
        if (frames == 0) {
            return report;
        }

        // levels, clipping and the true peak (the four sample positions around each one, by a Lanczos kernel)
        double peak = 0.0, truePeak = 0.0, square = 0.0;
        for (size_t i = 0; i < pcm.size(); ++i) {
            const double v = std::fabs(static_cast<double>(pcm[i]));
            peak = std::max(peak, v);
            square += v * v;
            report.clippedSamples += v >= 0.999 ? 1 : 0;
        }
        truePeak = peak;
        for (size_t c = 0; c < 2; ++c) {
            for (size_t n = 0; n < frames; ++n) {
                for (int p = 1; p <= 3; ++p) {
                    double sum = 0.0;
                    for (int k = -3; k <= 4; ++k) {
                        const long idx = static_cast<long>(n) + k;
                        if (idx >= 0 && idx < static_cast<long>(frames)) {
                            sum += pcm[static_cast<size_t>(idx) * 2 + c] * lanczos(k - p / 4.0);
                        }
                    }
                    truePeak = std::max(truePeak, std::fabs(sum));
                }
            }
        }
        report.samplePeakDb = toDb(peak);
        report.truePeakDb = toDb(truePeak);
        report.rmsDb = toDb(std::sqrt(square / static_cast<double>(pcm.size())));

        // integrated loudness: K-weighting at 48 kHz, 400 ms blocks every 100 ms, the absolute gate at -70 LUFS and the relative one 10 LU under the rest
        std::vector<double> energy[2];
        for (size_t c = 0; c < 2; ++c) {
            Biquad shelf{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585};
            Biquad high{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
            energy[c].resize(frames);
            for (size_t n = 0; n < frames; ++n) {
                const double y = high.run(shelf.run(pcm[n * 2 + c]));
                energy[c][n] = y * y;
            }
        }
        const size_t block = std::min<size_t>(frames, kRate * 4 / 10), hop = kRate / 10;
        std::vector<double> loudness; // per block, LUFS
        std::vector<double> power;
        for (size_t start = 0; start + block <= frames; start += hop) {
            double sum = 0.0;
            for (size_t c = 0; c < 2; ++c) {
                double e = 0.0;
                for (size_t n = start; n < start + block; ++n) {
                    e += energy[c][n];
                }
                sum += e / static_cast<double>(block);
            }
            power.push_back(sum);
            loudness.push_back(sum > 0.0 ? -0.691 + 10.0 * std::log10(sum) : -120.0);
        }
        double gated = 0.0;
        size_t kept = 0;
        for (size_t i = 0; i < power.size(); ++i) {
            if (loudness[i] > -70.0) {
                gated += power[i];
                ++kept;
            }
        }
        if (kept > 0) {
            const double threshold = -0.691 + 10.0 * std::log10(gated / static_cast<double>(kept)) - 10.0;
            double sum = 0.0;
            size_t used = 0;
            for (size_t i = 0; i < power.size(); ++i) {
                if (loudness[i] > -70.0 && loudness[i] > threshold) {
                    sum += power[i];
                    ++used;
                }
            }
            if (used > 0) {
                report.lufs = -0.691 + 10.0 * std::log10(sum / static_cast<double>(used));
            }
        }

        // silence: 50 ms windows under -60 dBFS, runs of at least 0.3 s
        const size_t window = kRate / 20;
        double runStart = -1.0;
        for (size_t start = 0; start < frames; start += window) {
            const size_t end = std::min(frames, start + window);
            double e = 0.0;
            for (size_t n = start; n < end; ++n) {
                e += static_cast<double>(pcm[n * 2]) * pcm[n * 2] + static_cast<double>(pcm[n * 2 + 1]) * pcm[n * 2 + 1];
            }
            const bool quiet = std::sqrt(e / static_cast<double>((end - start) * 2)) < 1e-3;
            const double t = static_cast<double>(start) / kRate;
            if (quiet && runStart < 0.0) {
                runStart = t;
            } else if (!quiet && runStart >= 0.0) {
                if (t - runStart >= 0.3) {
                    report.silent.push_back({runStart, t});
                }
                runStart = -1.0;
            }
        }
        if (runStart >= 0.0 && report.seconds - runStart >= 0.3) {
            report.silent.push_back({runStart, report.seconds});
        }
        return report;
    }

    namespace {
        struct Box {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            bool empty() const { return x0 > x1; }
        };

        // a window (a draw list) that left the canvas, over the frames it did
        struct Run {
            int from = 0, to = 0;
            float left = 0, top = 0, right = 0, bottom = 0; // the worst overhang, in pixels
        };

        const Mark* sceneOf(const Composition& comp, int frame) {
            const Mark* found = nullptr;
            for (const Mark& m : comp.marks) {
                if (frame >= m.start && frame < m.start + m.frames) {
                    found = &m; // a later scene is the one on top
                }
            }
            return found;
        }

        // the frame as small cells: the mean of 4x4 pixels, so a pop is a count of cells that changed
        struct Cells {
            int w = 0, h = 0;
            std::vector<unsigned char> rgb;
        };

        Cells shrink4(const Image& image) {
            Cells cells;
            cells.w = image.width / 4;
            cells.h = image.height / 4;
            cells.rgb.resize(static_cast<size_t>(cells.w) * cells.h * 3);
            for (int y = 0; y < cells.h; ++y) {
                for (int x = 0; x < cells.w; ++x) {
                    unsigned sum[3] = {0, 0, 0};
                    for (int dy = 0; dy < 4; ++dy) {
                        for (int dx = 0; dx < 4; ++dx) {
                            const unsigned char* p = &image.rgba[(static_cast<size_t>(y * 4 + dy) * image.width + x * 4 + dx) * 4];
                            sum[0] += p[0];
                            sum[1] += p[1];
                            sum[2] += p[2];
                        }
                    }
                    for (int c = 0; c < 3; ++c) {
                        cells.rgb[(static_cast<size_t>(y) * cells.w + x) * 3 + c] = static_cast<unsigned char>(sum[c] / 16);
                    }
                }
            }
            return cells;
        }

        // how much of the picture changed between two frames: the share of cells that differ, and where
        struct Change {
            double share = 0.0;
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // in pixels
        };

        Change compare(const Cells& a, const Cells& b) {
            Change change;
            int minX = a.w, minY = a.h, maxX = -1, maxY = -1;
            size_t differing = 0;
            for (int y = 0; y < a.h; ++y) {
                for (int x = 0; x < a.w; ++x) {
                    const size_t i = (static_cast<size_t>(y) * a.w + x) * 3;
                    const int d = std::max({std::abs(a.rgb[i] - b.rgb[i]), std::abs(a.rgb[i + 1] - b.rgb[i + 1]), std::abs(a.rgb[i + 2] - b.rgb[i + 2])});
                    if (d > 12) {
                        ++differing;
                        minX = std::min(minX, x);
                        minY = std::min(minY, y);
                        maxX = std::max(maxX, x);
                        maxY = std::max(maxY, y);
                    }
                }
            }
            if (a.w > 0 && a.h > 0) {
                change.share = static_cast<double>(differing) / (static_cast<double>(a.w) * a.h);
            }
            if (maxX >= 0) {
                change.x0 = minX * 4;
                change.y0 = minY * 4;
                change.x1 = (maxX + 1) * 4;
                change.y1 = (maxY + 1) * 4;
            }
            return change;
        }

        std::string number(double v) {
            char text[32];
            std::snprintf(text, sizeof text, "%.0f", v);
            return text;
        }
    } // namespace

    std::vector<Finding> Inspect(const Composition& comp, const InspectOptions& options, std::string* error) {
        std::vector<Finding> findings;
        const int from = std::clamp(options.from, 0, std::max(comp.frames - 1, 0));
        const int to = options.to < 0 ? comp.frames : std::clamp(options.to, from + 1, comp.frames);

        // what the draw data says about every frame: where each window ended up, and whether anything is drawn
        std::map<std::string, Run> open;     // windows off the canvas right now
        std::vector<std::pair<std::string, Run>> closed;
        std::vector<int> blank;
        const float w = static_cast<float>(comp.width), h = static_cast<float>(comp.height);
        auto probe = [&](int frame, const ImDrawData* data) {
            std::map<std::string, Run> now;
            for (const ImDrawList* list : data->CmdLists) {
                const std::string name = list->_OwnerName ? list->_OwnerName : "";
                if (name.rfind("##Background", 0) == 0 || name.rfind("##Foreground", 0) == 0 || list->VtxBuffer.Size == 0) {
                    continue; // layers that are meant to be bigger than the canvas
                }
                Box box;
                for (const ImDrawVert& v : list->VtxBuffer) {
                    if ((v.col >> IM_COL32_A_SHIFT) > 8) { // an invisible vertex (a fade at 0) is nothing to see
                        box.x0 = std::min(box.x0, v.pos.x);
                        box.y0 = std::min(box.y0, v.pos.y);
                        box.x1 = std::max(box.x1, v.pos.x);
                        box.y1 = std::max(box.y1, v.pos.y);
                    }
                }
                if (box.empty()) {
                    continue;
                }
                const bool inside = box.x1 > 0 && box.y1 > 0 && box.x0 < w && box.y0 < h; // some of it shows
                const float left = std::max(0.f, -box.x0), top = std::max(0.f, -box.y0), right = std::max(0.f, box.x1 - w), bottom = std::max(0.f, box.y1 - h);
                if (inside && std::max({left, top, right, bottom}) > 1.f) {
                    now[name] = Run{frame, frame, left, top, right, bottom};
                }
            }
            for (auto it = open.begin(); it != open.end();) { // runs that ended before this frame
                if (!now.count(it->first)) {
                    closed.push_back(*it);
                    it = open.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto& [name, run] : now) {
                auto found = open.find(name);
                if (found == open.end()) {
                    open[name] = run;
                } else {
                    found->second.to = frame;
                    found->second.left = std::max(found->second.left, run.left);
                    found->second.top = std::max(found->second.top, run.top);
                    found->second.right = std::max(found->second.right, run.right);
                    found->second.bottom = std::max(found->second.bottom, run.bottom);
                }
            }
            if (data->TotalVtxCount == 0) {
                blank.push_back(frame);
            }
        };

        // the pixels: one frame back and two, for the pops
        struct Seen {
            int frame = -1;
            Cells cells;
        };
        Seen previous;
        std::map<int, Change> byFrame;
        drawProbe() = probe;
        Options frameOptions;
        frameOptions.from = from;
        frameOptions.to = to;
        const bool played = Frames(comp, frameOptions, [&](int frame, const Image& image) {
            if (options.pops) {
                Cells cells = shrink4(image);
                if (previous.frame == frame - 1 && previous.cells.w == cells.w) {
                    byFrame[frame] = compare(previous.cells, cells);
                }
                previous = {frame, std::move(cells)};
            }
            return true;
        }, error);
        drawProbe() = nullptr;
        if (!played) {
            return findings;
        }
        for (const auto& item : open) {
            closed.push_back(item);
        }

        for (const auto& [name, run] : closed) {
            const Mark* scene = sceneOf(comp, run.from);
            const int sceneStart = scene ? scene->start : 0;
            const int sceneEnd = scene ? scene->start + scene->frames - 1 : comp.frames - 1;
            const bool entrance = run.from <= sceneStart, leaves = run.to >= sceneEnd;
            Finding f;
            f.severity = entrance || leaves ? "warning" : "error";
            f.kind = "clipped";
            f.what = name;
            f.scene = scene ? scene->name : "";
            f.from = run.from;
            f.to = run.to;
            std::string sides;
            auto add = [&](const char* side, float px) {
                if (px > 1.f) {
                    sides += (sides.empty() ? "" : ", ") + number(px) + " px past the " + side + " edge";
                }
            };
            add("left", run.left);
            add("top", run.top);
            add("right", run.right);
            add("bottom", run.bottom);
            f.detail = sides + (entrance ? " (at the start of its scene: an entrance?)" : leaves ? " (at the end of its scene: an exit?)" : " (in the middle of its scene)");
            f.look = "frame " + std::to_string(std::min(run.to, run.from + (run.to - run.from) / 2)) + " --ids";
            findings.push_back(f);
        }

        if (!blank.empty()) { // runs of frames with nothing drawn
            size_t i = 0;
            while (i < blank.size()) {
                size_t j = i;
                while (j + 1 < blank.size() && blank[j + 1] == blank[j] + 1) {
                    ++j;
                }
                Finding f;
                f.severity = "info";
                f.kind = "blank";
                const Mark* scene = sceneOf(comp, blank[i]);
                f.scene = scene ? scene->name : "";
                f.from = blank[i];
                f.to = blank[j];
                f.detail = "nothing is drawn";
                f.look = "frame " + std::to_string(blank[i]);
                findings.push_back(f);
                i = j + 1;
            }
        }

        if (options.pops) { // a frame that differs a lot from the one before it and from the one after, while those two are still
            for (const auto& [frame, change] : byFrame) {
                const auto before = byFrame.find(frame - 1), after = byFrame.find(frame + 1);
                const double around = std::max(before == byFrame.end() ? 0.0 : before->second.share, after == byFrame.end() ? 0.0 : after->second.share);
                if (change.share < 0.01 || change.share < 8.0 * std::max(around, 1e-4)) {
                    continue;
                }
                bool cut = false; // a hard cut between scenes is a decision, not a pop
                for (const Mark& m : comp.marks) {
                    cut = cut || m.start == frame;
                }
                if (cut) {
                    continue;
                }
                Finding f;
                f.severity = "error";
                f.kind = "pop";
                const Mark* scene = sceneOf(comp, frame);
                f.scene = scene ? scene->name : "";
                f.from = f.to = frame;
                f.what = number(change.x0) + "," + number(change.y0) + "," + number(change.x1 - change.x0) + "," + number(change.y1 - change.y0);
                f.detail = number(change.share * 100.0) + "% of the picture changed in one frame while the frames around it hardly move";
                f.look = "frame " + std::to_string(frame - 1) + "," + std::to_string(frame) + " --crop " + f.what;
                findings.push_back(f);
            }
        }
        std::sort(findings.begin(), findings.end(), [](const Finding& a, const Finding& b) { return a.from < b.from; });
        return findings;
    }
} // namespace imshell::video

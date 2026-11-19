#pragma once

// captions: WebVTT and SRT parsed into cues with per-word timing, and a draw helper that wraps a cue
// over the bottom of the frame and, optionally, lights the words as they are spoken. a pure function
// of the time, so playing and seeking give the same picture
#include "imgui.h"
#include "imshell/video.hpp"

#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace imshell::video {
    struct CaptionWord {
        std::string text;
        double start = 0.0; // seconds
        double end = 0.0;
    };

    struct Cue {
        double start = 0.0;
        double end = 0.0;
        std::string text;
        std::vector<CaptionWord> words; // from the file's inline timestamps, or spread evenly over the cue
    };

    namespace captions_detail {
        // hh:mm:ss.mmm, mm:ss.mmm (webvtt) or hh:mm:ss,mmm (srt); false when it is not a timestamp
        inline bool timestamp(std::string s, double& seconds) {
            for (char& c : s) {
                c = c == ',' ? '.' : c;
            }
            double parts[3] = {0, 0, 0};
            int n = 0;
            size_t i = 0;
            while (i < s.size() && n < 3) {
                size_t j = i;
                while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) {
                    ++j;
                }
                if (j == i) {
                    return false;
                }
                parts[n++] = std::atof(s.substr(i, j - i).c_str());
                if (j < s.size() && s[j] == ':') {
                    ++j;
                } else if (j != s.size()) {
                    return false;
                }
                i = j;
            }
            if (n < 2 || i != s.size()) {
                return false;
            }
            seconds = n == 3 ? parts[0] * 3600 + parts[1] * 60 + parts[2] : parts[0] * 60 + parts[1];
            return true;
        }

        inline std::string trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
                ++a;
            }
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
                --b;
            }
            return s.substr(a, b - a);
        }

        // the cue's words: text split at spaces, tags dropped, `<00:01.200>` starting the word after it
        inline void words(Cue& cue, const std::string& raw) {
            double pending = -1.0;
            std::string word;
            bool timed = false;
            auto flush = [&]() {
                if (!word.empty()) {
                    cue.words.push_back({word, pending, 0.0});
                    pending = -1.0;
                    word.clear();
                }
            };
            for (size_t i = 0; i < raw.size(); ++i) {
                const char c = raw[i];
                if (c == '<') {
                    const size_t close = raw.find('>', i);
                    if (close == std::string::npos) {
                        word += c;
                        continue;
                    }
                    double t = 0.0;
                    if (timestamp(raw.substr(i + 1, close - i - 1), t)) {
                        flush();
                        pending = t;
                        timed = true;
                    }
                    i = close;
                } else if (std::isspace(static_cast<unsigned char>(c))) {
                    flush();
                } else {
                    word += c;
                }
            }
            flush();
            const size_t n = cue.words.size();
            for (size_t i = 0; i < n; ++i) {
                CaptionWord& w = cue.words[i];
                if (!timed) {
                    w.start = cue.start + (cue.end - cue.start) * static_cast<double>(i) / static_cast<double>(n);
                } else if (w.start < 0.0) {
                    w.start = i == 0 ? cue.start : cue.words[i - 1].start; // untimed words ride with the one before
                }
            }
            for (size_t i = 0; i < n; ++i) {
                cue.words[i].end = i + 1 < n ? cue.words[i + 1].start : cue.end;
            }
        }
    } // namespace captions_detail

    // cues from the text of a .vtt or .srt file; blocks it cannot read are skipped
    inline std::vector<Cue> ParseCaptions(const std::string& text) {
        std::vector<Cue> cues;
        std::istringstream in(text);
        std::string line;
        Cue cue;
        bool open = false;
        std::string body;
        auto close = [&]() {
            if (open) {
                captions_detail::words(cue, body);
                for (size_t i = 0; i < cue.words.size(); ++i) {
                    cue.text += (i ? " " : "") + cue.words[i].text;
                }
                if (!cue.words.empty()) {
                    cues.push_back(cue);
                }
            }
            open = false;
            body.clear();
            cue = Cue{};
        };
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const size_t arrow = line.find("-->");
            if (arrow != std::string::npos) {
                close();
                const std::string from = captions_detail::trim(line.substr(0, arrow));
                std::string to = captions_detail::trim(line.substr(arrow + 3));
                to = to.substr(0, to.find(' ')); // cue settings follow the end time
                open = captions_detail::timestamp(from, cue.start) && captions_detail::timestamp(to, cue.end);
            } else if (captions_detail::trim(line).empty()) {
                close();
            } else if (open) {
                body += (body.empty() ? "" : " ") + line;
            }
        }
        close();
        return cues;
    }

    inline bool LoadCaptions(const std::string& path, std::vector<Cue>& cues) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        std::stringstream all;
        all << file.rdbuf();
        cues = ParseCaptions(all.str());
        return !cues.empty();
    }

    // the cue showing at `seconds`, or null
    inline const Cue* CueAt(std::span<const Cue> cues, double seconds) {
        for (const Cue& c : cues) {
            if (seconds >= c.start && seconds < c.end) {
                return &c;
            }
        }
        return nullptr;
    }

    struct CaptionStyle {
        float size = 0.f;          // font size in pixels; 0 = 5% of the frame height
        float width = 0.8f;        // widest line, as a fraction of the frame width
        float bottom = 0.1f;       // gap under the block, as a fraction of the frame height
        ImU32 color = IM_COL32(255, 255, 255, 255);
        ImU32 spoken = IM_COL32(255, 214, 64, 255); // words already spoken, when karaoke is on
        ImU32 background = IM_COL32(0, 0, 0, 150);  // behind the block; 0 for none
        bool karaoke = false;      // light the words as they are spoken
    };

    // draws the cue showing at `f.time()` on the foreground draw list
    inline void DrawCaptions(const Frame& f, std::span<const Cue> cues, const CaptionStyle& style = {}) {
        const Cue* cue = CueAt(cues, f.time());
        if (!cue) {
            return;
        }
        ImFont* font = ImGui::GetFont();
        const float size = style.size > 0.f ? style.size : static_cast<float>(f.height) * 0.05f;
        const float space = font->CalcTextSizeA(size, FLT_MAX, 0.f, " ").x;
        const float limit = static_cast<float>(f.width) * style.width;

        struct Placed {
            const CaptionWord* word;
            float x;
            int line;
            float width;
        };
        std::vector<Placed> placed;
        std::vector<float> lineWidth(1, 0.f);
        float x = 0.f;
        for (const CaptionWord& w : cue->words) {
            const float width = font->CalcTextSizeA(size, FLT_MAX, 0.f, w.text.c_str()).x;
            if (x > 0.f && x + space + width > limit) { // greedy wrap
                lineWidth.back() = x;
                lineWidth.push_back(0.f);
                x = 0.f;
            }
            x += x > 0.f ? space : 0.f;
            placed.push_back({&w, x, static_cast<int>(lineWidth.size()) - 1, width});
            x += width;
        }
        lineWidth.back() = x;

        const float lineHeight = size * 1.25f, pad = size * 0.4f;
        float widest = 0.f;
        for (float w : lineWidth) {
            widest = std::max(widest, w);
        }
        const float blockHeight = lineHeight * static_cast<float>(lineWidth.size());
        const float top = static_cast<float>(f.height) * (1.f - style.bottom) - blockHeight;
        const float centre = static_cast<float>(f.width) * 0.5f;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float alpha = ImGui::GetStyle().Alpha; // a scene fading in fades its captions too
        auto faded = [&](ImU32 c) {
            ImColor col(c);
            col.Value.w *= alpha;
            return static_cast<ImU32>(col);
        };
        if (style.background != 0) {
            dl->AddRectFilled(ImVec2(centre - widest * 0.5f - pad, top - pad * 0.5f), ImVec2(centre + widest * 0.5f + pad, top + blockHeight + pad * 0.5f), faded(style.background), pad);
        }
        for (const Placed& p : placed) {
            const float left = centre - lineWidth[static_cast<size_t>(p.line)] * 0.5f + p.x;
            const bool lit = style.karaoke && f.time() >= p.word->start;
            dl->AddText(font, size, ImVec2(left, top + lineHeight * static_cast<float>(p.line)), faded(lit ? style.spoken : style.color), p.word->text.c_str());
        }
    }
} // namespace imshell::video

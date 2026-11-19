#pragma once

// checks that read a composition without anyone watching it: the loudness and level of its sound,
// and an inspection of its frames (windows cut off by the canvas, frames with nothing on them, parts
// of the picture that change in one frame between still frames)
#include "imshell/video.hpp"

#include <string>
#include <utility>
#include <vector>

namespace imshell::video {
    struct AudioReport {
        double seconds = 0.0;
        double lufs = -120.0;         // integrated loudness, ITU-R BS.1770 (K-weighted, gated); -120 when there is nothing above the gate
        double samplePeakDb = -120.0; // dBFS
        double truePeakDb = -120.0;   // dBTP, 4x oversampled
        double rmsDb = -120.0;
        int clippedSamples = 0;       // samples at full scale
        std::vector<std::pair<double, double>> silent; // seconds [from, to) quieter than -60 dBFS for at least 0.3 s
    };

    // pcm is interleaved 48 kHz stereo float (what RenderAudio returns)
    AudioReport AnalyzeAudio(const std::vector<float>& pcm);

    struct Finding {
        std::string severity; // "error" or "warning" or "info"
        std::string kind;     // "clipped", "blank", "pop"
        std::string what;     // a window's name, or the region of a pop
        std::string scene;    // the scene the first frame belongs to, if the composition has marks
        int from = 0;         // frames, both included
        int to = 0;
        std::string detail;
        std::string look;     // the command that shows it
    };

    struct InspectOptions {
        int from = 0;
        int to = -1;      // one past the last frame; -1 = the end
        bool pops = true; // compare consecutive frames; the only part that reads pixels
    };

    // plays the frames and reports what a viewer would notice. a window cut off at the start or end of its
    // scene is a warning (an entrance or an exit); anywhere else, an error. a cut between scenes is not a pop
    std::vector<Finding> Inspect(const Composition& comp, const InspectOptions& options = {}, std::string* error = nullptr);
} // namespace imshell::video

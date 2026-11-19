#pragma once

// the sound mixer: decoded clips summed into one stereo float track. decoding is the platform's
// (AVAssetReader on apple); the mixing here is plain arithmetic
#include "imshell/video.hpp"

#include <vector>

namespace imshell::video {
    constexpr int kAudioRate = 48000;

    // interleaved stereo float at kAudioRate
    using Pcm = std::vector<float>;

    // adds `clip` (already decoded to `pcm`) into `out`, a window of the timeline that starts at
    // `windowStart` seconds: placement, trim, volume and the fades
    void MixClip(Pcm& out, double windowStart, const Pcm& pcm, const AudioClip& clip);

    // clamps to -1..1 so a loud sum does not wrap in the encoder
    void Limit(Pcm& out);

    // the frames of a window of `seconds`
    inline size_t WindowFrames(double seconds) { return static_cast<size_t>(seconds * kAudioRate + 0.5); }
} // namespace imshell::video

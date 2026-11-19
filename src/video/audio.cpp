#include "audio.hpp"

#include <algorithm>
#include <cmath>

namespace imshell::video {
    void MixClip(Pcm& out, double windowStart, const Pcm& pcm, const AudioClip& clip) {
        const long long srcFrames = static_cast<long long>(pcm.size() / 2);
        const long long skip = std::llround(std::max(clip.offset, 0.0) * kAudioRate);
        long long length = srcFrames - skip;
        if (clip.duration >= 0.0) {
            length = std::min(length, std::llround(clip.duration * kAudioRate));
        }
        const long long outFrames = static_cast<long long>(out.size() / 2);
        const long long first = std::llround((clip.start - windowStart) * kAudioRate); // out frame of the clip's first
        const double fadeIn = clip.fadeIn * kAudioRate;
        const double fadeOut = clip.fadeOut * kAudioRate;
        for (long long k = std::max(0LL, -first); k < length && first + k < outFrames; ++k) {
            float gain = clip.volume;
            if (fadeIn > 0.0 && k < fadeIn) {
                gain *= static_cast<float>(k / fadeIn);
            }
            if (fadeOut > 0.0 && length - k < fadeOut) {
                gain *= static_cast<float>((length - k) / fadeOut);
            }
            const size_t o = static_cast<size_t>(first + k) * 2;
            const size_t s = static_cast<size_t>(skip + k) * 2;
            out[o] += pcm[s] * gain;
            out[o + 1] += pcm[s + 1] * gain;
        }
    }

    void Limit(Pcm& out) {
        for (float& v : out) {
            v = std::clamp(v, -1.f, 1.f);
        }
    }
} // namespace imshell::video

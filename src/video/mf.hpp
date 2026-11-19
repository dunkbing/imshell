#pragma once

// windows: Media Foundation as the encoder (h.264 + aac in an mp4) and the sound decoder, so a
// render needs no ffmpeg. the system's own software h.264 encoder; no GPU needed
#ifdef _WIN32
#include <memory>
#include <string>
#include <vector>

namespace imshell::video {
    class MfWriter {
    public:
        MfWriter();
        ~MfWriter();
        MfWriter(const MfWriter&) = delete;
        MfWriter& operator=(const MfWriter&) = delete;

        // withAudio: 48 kHz stereo float will follow through audio()
        bool open(const std::string& path, int width, int height, int fps, bool withAudio, std::string* error);
        // frame `index` (counted from 0), top-down rgba
        bool video(const unsigned char* rgba, int index);
        // `frames` stereo frames of interleaved float starting at audio frame `start`
        bool audio(const float* interleaved, size_t start, size_t frames);
        // flushes and closes the file
        bool finish(std::string* error);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // a sound file decoded to interleaved 48 kHz stereo float; false and `error` on failure
    bool DecodeWithMf(const std::string& path, std::vector<float>& pcm, std::string* error);
} // namespace imshell::video
#endif

#pragma once

// raw rgba frames into an ffmpeg process over a pipe. no link dependency: the binary is found at run time
#include <cstdio>
#include <string>
#include <vector>

namespace imshell::video {
    // one argument for the shell, quoted
    std::string ShellQuote(const std::string& s);
    // a sound file decoded by ffmpeg to interleaved 48 kHz stereo float; false and `error` on failure
    bool DecodeWithFfmpeg(const std::string& ffmpeg, const std::string& path, std::vector<float>& pcm, std::string* error);

    class FfmpegPipe {
    public:
        FfmpegPipe() = default;
        FfmpegPipe(const FfmpegPipe&) = delete;
        FfmpegPipe& operator=(const FfmpegPipe&) = delete;
        ~FfmpegPipe() { close(); }

        // starts `ffmpeg` writing `path` (h.264, yuv420p, so every player opens it)
        // `audioFile`: raw 48 kHz stereo f32le to add as an aac track, or empty for none
        bool open(const std::string& ffmpeg, int width, int height, int fps, const std::string& path, const std::string& audioFile, std::string* error);
        // one frame, width * height * 4 bytes
        bool write(const unsigned char* rgba, size_t size);
        // ends the input and waits for ffmpeg; false when it failed
        bool close();

    private:
        FILE* pipe_ = nullptr;
    };
} // namespace imshell::video
